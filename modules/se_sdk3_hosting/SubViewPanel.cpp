
#include "SubViewPanel.h"
#include <cmath>
#include "../shared/xplatform.h"

#include "ConnectorView.h"
#include "ModuleView.h"

using namespace gmpi;
using namespace gmpi::drawing;
using namespace std;

namespace
{
	auto r = gmpi::Register<SubView>::withXml(

R"XML(
<?xml version="1.0" encoding="utf-8" ?>
<PluginList>
  <Plugin id="ContainerX" name="Container" category="Debug" graphicsApi="GmpiGui">
    <GUI>
      <Pin name="Controls on Parent" datatype="bool" direction="out" isMinimised="true" />
      <Pin name="Controls on Module" datatype="bool" noAutomation="true" />
      <Pin name="Visible" datatype="bool" />
      <Pin name="Ignore Program Change" datatype="bool" noAutomation="true" />
    </GUI>
<!--
    <Audio>
      <Pin name="Spare" datatype="float" rate="audio" autoRename="true" autoDuplicate="true" isContainerIoPlug="true" />
	  -- obsolete --
      <Pin name="Polyphony" datatype="enum" default="6" private="true" isMinimised="true" metadata="range 1,128" />
      <Pin name="MIDI Automation" datatype="bool" private="true" isMinimised="true" />
      <Pin name="Show Controls" datatype="enum" private="true" isMinimised="true" metadata="Off,On Panel,On Module" />
    </Audio>
-->
  </Plugin>
</PluginList>
)XML");

}

int32_t SubView::StartCableDrag(SE2::IViewChild* fromModule, int fromPin, gmpi::drawing::Point dragStartPoint, bool isHeldAlt, SE2::CableType type)
{
	auto moduleview = dynamic_cast<SE2::ModuleView*>(drawingHost.get()); // this->getGuiHost());
	dragStartPoint += offset_;
	dragStartPoint = transformPoint( moduleview->OffsetToClient(), dragStartPoint);

	return moduleview->parent->StartCableDrag(fromModule, fromPin, dragStartPoint, isHeldAlt);
}

void SubView::OnCableDrag(SE2::ConnectorViewBase* dragline, gmpi::drawing::Point dragPoint, float& bestDistance, SE2::ModuleView*& bestModule, int& bestPinIndex)
{
	dragPoint -= offset_;
//	dragPoint = transformPoint(offset_, dragPoint);

	for (auto it = children.rbegin(); it != children.rend(); ++it) // iterate in reverse for correct Z-Order.
	{
		(*it)->OnCableDrag(dragline, dragPoint, bestDistance, bestModule, bestPinIndex);
	}
}

SubView::SubView(SE2::ViewChild* pparent, int pparentViewType) : ViewBase({1000, 1000})
	, parent(pparent)
	, parentViewType(pparentViewType)
{
#if 0
	if (parentViewType == CF_PANEL_VIEW)
	{
		initializePin(0, showControlsLegacy, static_cast<MpGuiBaseMemberPtr2>(&SubView::onValueChanged));
		// 1 : "Controls on Module" not needed.
		initializePin(2, showControls, static_cast<MpGuiBaseMemberPtr2>(&SubView::onValueChanged));
		// 3 : "Ignore Program Change" not needed.
	}
	else
	{
		// Hack - Reroute Show-Controls-On-Module to Show-Controls-On-Panel
		initializePin(1, showControls, static_cast<MpGuiBaseMemberPtr2>(&SubView::onValueChanged));
	}
#endif
	offset_.height = offset_.width = -99999; // un-initialized
}

void SubView::BuildModules(Json::Value* context, std::map<int, SE2::ModuleView*>& guiObjectMap)
{
	mouseOverObject = {};

#if _DEBUG
	{
		Json::StyledWriter writer;
		auto factoryXml = writer.write(*context);
		auto s = factoryXml;
	}
#endif

	// Modules.
	Json::Value& modules_json = (*context)["modules"];

	for (auto& module_json : modules_json)
	{
		const auto typeName = module_json["type"].asString();

		assert(typeName != "Line");  // no lines on GUI.
		assert(typeName != "SE Structure Group2");
#ifdef _DEBUG
		// avoid trying to create unavailable modules
		const auto typeId = JmUnicodeConversions::Utf8ToWstring(typeName);
		auto moduleInfo = CModuleFactory::Instance()->GetById(typeId);
		//			if (moduleInfo)
#endif
		auto module = std::make_unique<SE2::ModuleViewPanel>(&module_json, this, guiObjectMap);

		if (module)
		{
			const auto isBackground = !module_json["ignoremouse"].empty();

			if ((module->getSelected() || isBackground) && Presenter()->editEnabled())
			{
				assert(!isIteratingChildren);
				children.push_back(module->createAdorner(this));
			}

			guiObjectMap.insert({ module->getModuleHandle(), module.get() });
			assert(!isIteratingChildren);
			children.push_back(std::move(module));
		}
	}

	// get Z-Order same as SE.
	std::reverse(std::begin(children), std::end(children));
}

void SubView::onValueChanged()
{
	// if we just blinked into existence, need to update mouse over object
	if (isShown())
	{
		auto moduleview = dynamic_cast<SE2::ModuleView*>(drawingHost.get());
		moduleview->parent->onSubPanelMadeVisible();
	}

	OnPatchCablesVisibilityUpdate();

	invalidateRect();
}

void SubView::OnPatchCablesVisibilityUpdate()
{
	auto parent = dynamic_cast<SE2::ViewChild*> (drawingHost.get());
	parent->parent->OnPatchCablesVisibilityUpdate();
}

SE2::ConnectorViewBase* SubView::createCable(SE2::CableType type, int32_t handleFrom, int32_t fromPin)
{
	return new SE2::PatchCableView(this, handleFrom, fromPin, -1, -1);
}

void SubView::markDirtyChild(SE2::IViewChild* child)
{
	SE2::ViewBase::markDirtyChild(child);
	auto parent = dynamic_cast<SE2::ViewChild*> (drawingHost.get());
	parent->parent->markDirtyChild(parent);
}

int32_t SubView::setCapture(SE2::IViewChild* module)
{
	// Avoid situation where some module turns off panel then captures mouse (ensuring it never can un-capture it).
	if (isShown())
	{
		return ViewBase::setCapture(module);
	}

	return gmpi::MP_FAIL;
}


gmpi::ReturnCode SubView::initialize()
{
	onValueChanged(); // nesc in case initial value is 0.

	int32_t x, y;
	Presenter()->GetViewScroll(x, y);

	offset_.width = static_cast<float>(x);
	offset_.height = static_cast<float>(y);

	if( x != -99999)
	{
		auto module = dynamic_cast<SE2::ViewChild*> (drawingHost.get());
		offset_.width -= module->bounds_.left;
		offset_.height -= module->bounds_.top;
	}

	return SE2::ViewBase::initialize();
}

gmpi::ReturnCode SubView::measure(const gmpi::drawing::Size* availableSize, gmpi::drawing::Size* returnDesiredSize)
{
	if (!availableSize || !returnDesiredSize)
		return gmpi::ReturnCode::Fail;

	(void)availableSize;

	// calc my bounds.
	// Start with inverted rect (no area).
    viewBounds = gmpi::drawing::Rect(200000, 200000, -200000, -200000);

    const gmpi::drawing::Size veryLarge(100000, 100000);
	gmpi::drawing::Size notused;

	for (auto& m : children)
	{
		// Copied form ViewBase::arrange().
		if (m->isVisable() && dynamic_cast<SE2::ConnectorViewBase*>(m.get()) == nullptr)
		{
			gmpi::drawing::Size savedSize(getWidth(m->getLayoutRect()), getHeight(m->getLayoutRect()));
			gmpi::drawing::Size desired;
			gmpi::drawing::Size actualSize;
			bool changedSize = false;
			/*
			if (debug)
			{
			_RPT4(_CRT_WARN, "savedSize r[ %f %f %f %f]\n", m->getBounds().left, m->getBounds().top, m->getBounds().left + m->getBounds().getWidth(), m->getBounds().top + m->getBounds().getHeight());
			}
			*/
			// Detect brand-new objects that haven't had size calculated yet.
			if (savedSize.width == 0 && savedSize.height == 0)
			{
				const int defaultDimensions = 100;
				gmpi::drawing::Size defaultSize(defaultDimensions, defaultDimensions);
				m->measure(defaultSize, &desired);
				actualSize = desired;
				// stick with integer sizes for compatibility.
				actualSize.height = ceilf(actualSize.height);
				actualSize.width = ceilf(actualSize.width);
				changedSize = true;
			}
			else
			{
#ifdef _DEBUG
				desired.width = std::numeric_limits<float>::quiet_NaN();
#endif

				m->measure(savedSize, &desired);

#ifdef _DEBUG
				assert(!std::isnan(desired.width)); // object does not implement measure()!
#endif
													/*
													if (debug)
													{
													_RPT2(_CRT_WARN, "desired s[ %f %f]\n", desired.width, desired.height);
													}
													*/
													// Font variations cause Slider to report different desired size.
													// However resizing it causes alignment errors on Panel. It shifts left or right.
													// Avoid resizing unless module clearly needs a different size. Structure view always sizes to fit (else plugs end up with wrapped text)
				float tolerence = getViewType() == CF_PANEL_VIEW ? 3.0f : 0.0f;
				if (isArranged || (fabsf(desired.width - savedSize.width) > tolerence || fabsf(desired.height - savedSize.height) > tolerence))
				{
					actualSize = desired;
					// stick with integer sizes for compatibility.
					actualSize.height = ceilf(actualSize.height);
					actualSize.width = ceilf(actualSize.width);
					changedSize = true;
				}
				else
				{
					// Used save size from project, even if it varies a little.
					actualSize = savedSize;
				}
			}

			// Note, due to font width differences, this may result in different size/layout than original GDI graphics. e..g knobs shifting.
			/*
			if (debug)
			{
			_RPT4(_CRT_WARN, "arrange r[ %f %f %f %f]\n", m->getBounds().left, m->getBounds().top, m->getBounds().left + actualSize.width, m->getBounds().top + actualSize.height);
			}
			*/
			gmpi::drawing::Rect moduleRect(m->getLayoutRect().left, m->getLayoutRect().top, m->getLayoutRect().left + actualSize.width, m->getLayoutRect().top + actualSize.height);

			// Typically only when new object inserted.
			viewBounds.left = (std::min)(viewBounds.left, moduleRect.left);
			viewBounds.right = (std::max)(viewBounds.right, moduleRect.right);
			viewBounds.top = (std::min)(viewBounds.top, moduleRect.top);
			viewBounds.bottom = (std::max)(viewBounds.bottom, moduleRect.bottom);
		}
	}

	if (viewBounds.right == -200000) // no children. Default to small rectangle.
	{
		viewBounds.left = viewBounds.top = 0;
		viewBounds.right = viewBounds.bottom = 10;
	}

	returnDesiredSize->width = (std::max)(0.0f, getWidth(viewBounds));
	returnDesiredSize->height = (std::max)(0.0f, getHeight(viewBounds));

	// On first open, need to calc offset relative to view.
	// ref control_group_auto_size::RecalcBounds()
	if( offset_.width == -99999.f )
	{
		offset_.width = static_cast<float>(-static_cast<int32_t>(viewBounds.left));
		offset_.height = static_cast<float>(-static_cast<int32_t>(viewBounds.top));

		// avoid 'show on module' structure view messing up panel view's offset.
		if (parentViewType == CF_PANEL_VIEW)
		{
			auto module = dynamic_cast<SE2::ViewChild*>(drawingHost.get());
			Presenter()->SetViewScroll(
				static_cast<int32_t>(offset_.width + module->bounds_.left),
				static_cast<int32_t>(offset_.height + module->bounds_.top)
			);
		}
	}
	else
	{
		// if top-left coords have changed last opened.
		// then shift sub-panel to compensate (panel view only).
		int32_t parentAdjustX(static_cast<int32_t>(offset_.width + viewBounds.left));
		int32_t parentAdjustY(static_cast<int32_t>(offset_.height + viewBounds.top));

		if (parentAdjustX != 0 || parentAdjustY != 0)
		{
			offset_.width -= parentAdjustX;
			offset_.height -= parentAdjustY;

			// Adjust module top-left.
			auto parent = dynamic_cast<SE2::ViewChild*> (drawingHost.get());

			if (parent->parent->getViewType() == CF_PANEL_VIEW)
				parent->Presenter()->ResizeModule(parent->handle, 0, 0, gmpi::drawing::Size((float)parentAdjustX, (float)parentAdjustY));
		}
	}
	return gmpi::ReturnCode::Ok;
}

gmpi::ReturnCode SubView::arrange(const gmpi::drawing::Rect* finalRect)
{
	if (!finalRect)
		return gmpi::ReturnCode::Fail;

	return ViewBase::arrange(finalRect);
}

bool SubView::isShown()
{
//	auto parent = dynamic_cast<SE2::ViewChild*> (drawingHost.get());
	if (!parent->isShown())
		return false;

	if (parentViewType == CF_PANEL_VIEW)
		return showControls || showControlsLegacy;
	else
		return showControls;
}

gmpi::ReturnCode SubView::render(gmpi::drawing::api::IDeviceContext* drawingContext)
{
	if (!drawingContext)
		return gmpi::ReturnCode::Fail;

	if (isShown())
	{
		gmpi::drawing::Graphics g(drawingContext);

		// Transform to module-relative.
		const auto originalTransform = g.getTransform();
		auto adjustedTransform = makeTranslation(offset_.width, offset_.height) * originalTransform;
		g.setTransform(adjustedTransform);

		// Render.
		auto res = SE2::ViewBase::render(drawingContext);

		// Transform back.
		g.setTransform(originalTransform);
		return res;
	}
	else
		return gmpi::ReturnCode::Unhandled;
}

#if 0 // todo
int32_t SubView::getToolTip(gmpi::drawing::Point point, gmpi::api::IString* returnString)
{
	const auto localPoint = gmpi::drawing::Point(point.x - offset_.width, point.y - offset_.height);
	return ViewBase::getToolTip(localPoint, returnString);
}
#endif

void SubView::process()
{
	processUnidirectionalModules();
}

bool SubView::hitTest(int32_t flags, gmpi::drawing::Point* point)
{
	if (isShown())
	{
		const auto localPoint = gmpi::drawing::Point(point->x - offset_.width, point->y - offset_.height );
		for (auto it = children.rbegin(); it != children.rend(); ++it) // iterate in reverse for correct Z-Order.
		{
			auto m = (*it).get();
			if (m->hitTest(localPoint, flags) == gmpi::ReturnCode::Ok)
				return true;
		}
	}
	
	return false;
}

gmpi::ReturnCode SubView::onPointerDown(gmpi::drawing::Point point, int32_t flags)
{
	if (isShown())
	{
		point.x -= offset_.width;
		point.y -= offset_.height;
		return ViewBase::onPointerDown(point, flags);
	}
	else
		return gmpi::ReturnCode::Unhandled;
}

gmpi::ReturnCode SubView::onPointerMove(gmpi::drawing::Point point, int32_t flags)
{
	if (isShown())
	{
		point.x -= offset_.width;
		point.y -= offset_.height;
		return ViewBase::onPointerMove(point, flags);
	}
	else
		return gmpi::ReturnCode::Unhandled;
}

gmpi::ReturnCode SubView::onPointerUp(gmpi::drawing::Point point, int32_t flags)
{
	if (isShown() || mouseCaptureObject)  // attempt to fix it when object on panel captures mouse, then hides itself
	{
		point.x -= offset_.width;
		point.y -= offset_.height;
		return ViewBase::onPointerUp(point, flags);
	}
	else
	{
		return gmpi::ReturnCode::Unhandled;
	}
}

gmpi::ReturnCode SubView::onMouseWheel(gmpi::drawing::Point point, int32_t flags, int32_t delta)
{
	if (!isShown())
		return gmpi::ReturnCode::Unhandled;

	point.x -= offset_.width;
	point.y -= offset_.height;

	return ViewBase::onMouseWheel(point, flags, delta);
}

void SubView::OnChildMoved()
{
	// TODO : enhancement - also calc my cliprect on sum of child cliprects.

	auto parent = dynamic_cast<SE2::ViewChild*> (drawingHost.get());

	gmpi::drawing::Rect viewBoundsNew;
	gmpi::drawing::Rect unused2;
	calcBounds(viewBoundsNew, unused2);

	if (viewBounds == viewBoundsNew)
		return;

	// Adjust module layout rect
	auto parentLayoutRect = parent->getLayoutRect();

	// shift top-left origin if nesc. (panel only)
	if (parent->parent->getViewType() == CF_PANEL_VIEW)
	{
		parentLayoutRect.left += viewBoundsNew.left - viewBounds.left;
		parentLayoutRect.top += viewBoundsNew.top - viewBounds.top;
	}

	// set size.
	parentLayoutRect.right = parentLayoutRect.left + viewBoundsNew.right - viewBoundsNew.left;
	parentLayoutRect.bottom = parentLayoutRect.top + viewBoundsNew.bottom - viewBoundsNew.top;

	viewBounds = viewBoundsNew;
	offset_.width = -viewBoundsNew.left;
	offset_.height = -viewBoundsNew.top;

	parent->parent->OnChangedChildPosition(parent->handle, parentLayoutRect);
}

void SubView::calcBounds(gmpi::drawing::Rect& returnLayoutRect, gmpi::drawing::Rect& returnClipRect)
{
	// calc my bounds.
	// Start with inverted rect (no area).
	returnLayoutRect = gmpi::drawing::Rect(200000, 200000, -200000, -200000);

	const gmpi::drawing::Size veryLarge(100000, 100000);
	gmpi::drawing::Size notused;

	for (auto& m : children)
	{
		// Copied form ViewBase::arrange().
		if (m->isVisable() && dynamic_cast<SE2::ConnectorViewBase*>(m.get()) == nullptr)
		{
			gmpi::drawing::Size savedSize(getWidth(m->getLayoutRect()), getHeight(m->getLayoutRect()));
			gmpi::drawing::Size desired;
			gmpi::drawing::Size actualSize;
			bool changedSize = false;
			/*
			if (debug)
			{
			_RPT4(_CRT_WARN, "savedSize r[ %f %f %f %f]\n", m->getBounds().left, m->getBounds().top, m->getBounds().left + m->getBounds().getWidth(), m->getBounds().top + m->getBounds().getHeight());
			}
			*/
			// Detect brand-new objects that haven't had size calculated yet.
			if (savedSize.width == 0 && savedSize.height == 0)
			{
				const int defaultDimensions = 100;
				gmpi::drawing::Size defaultSize(defaultDimensions, defaultDimensions);
				m->measure(defaultSize, &desired);
				actualSize = desired;
				// stick with integer sizes for compatibility.
				actualSize.height = ceilf(actualSize.height);
				actualSize.width = ceilf(actualSize.width);
				changedSize = true;
			}
			else
			{
#ifdef _DEBUG
				desired.width = std::numeric_limits<float>::quiet_NaN();
#endif

				m->measure(savedSize, &desired);

#ifdef _DEBUG
				assert(!std::isnan(desired.width)); // object does not implement measure()!
#endif
				/*
				if (debug)
				{
				_RPT2(_CRT_WARN, "desired s[ %f %f]\n", desired.width, desired.height);
				}
				*/
				// Font variations cause Slider to report different desired size.
				// However resizing it causes alignment errors on Panel. It shifts left or right.
				// Avoid resizing unless module clearly needs a different size. Structure view always sizes to fit (else plugs end up with wrapped text)
				float tolerence = getViewType() == CF_PANEL_VIEW ? 3.0f : 0.0f;
				if (isArranged || (fabsf(desired.width - savedSize.width) > tolerence || fabsf(desired.height - savedSize.height) > tolerence))
				{
					actualSize = desired;
					// stick with integer sizes for compatibility.
					actualSize.height = ceilf(actualSize.height);
					actualSize.width = ceilf(actualSize.width);
					changedSize = true;
				}
				else
				{
					// Used save size from project, even if it varies a little.
					actualSize = savedSize;
				}
			}

			// Note, due to font width differences, this may result in different size/layout than original GDI graphics. e..g knobs shifting.
			/*
			if (debug)
			{
			_RPT4(_CRT_WARN, "arrange r[ %f %f %f %f]\n", m->getBounds().left, m->getBounds().top, m->getBounds().left + actualSize.width, m->getBounds().top + actualSize.height);
			}
			*/
			gmpi::drawing::Rect moduleRect(m->getLayoutRect().left, m->getLayoutRect().top, m->getLayoutRect().left + actualSize.width, m->getLayoutRect().top + actualSize.height);
			//m->arrange(gmpi::drawing::Rect(m->getLayoutRect().left, m->getLayoutRect().top, m->getLayoutRect().left + actualSize.width, m->getLayoutRect().top + actualSize.height));

			// Typically only when new object inserted.
			returnLayoutRect.left = (std::min)(returnLayoutRect.left, moduleRect.left);
			returnLayoutRect.right = (std::max)(returnLayoutRect.right, moduleRect.right);
			returnLayoutRect.top = (std::min)(returnLayoutRect.top, moduleRect.top);
			returnLayoutRect.bottom = (std::max)(returnLayoutRect.bottom, moduleRect.bottom);
		}
	}

	if (returnLayoutRect.right == -200000) // no children. Default to small rectangle.
	{
		returnLayoutRect.left = returnLayoutRect.top = 0;
		returnLayoutRect.right = returnLayoutRect.bottom = 10;
	}

	returnLayoutRect.left = floorf(returnLayoutRect.left);
	returnLayoutRect.top = floorf(returnLayoutRect.top);
	returnLayoutRect.right = ceilf(returnLayoutRect.right);
	returnLayoutRect.bottom = ceilf(returnLayoutRect.bottom);
}
