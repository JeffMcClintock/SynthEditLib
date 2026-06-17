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

int32_t SubView::StartCableDrag(SE2::CableType type, SE2::IViewChild* fromModule, int fromPin, gmpi::drawing::Point dragStartPoint, gmpi::drawing::Point mousePoint)
{
	auto moduleview = parent;
	// child-local -> Container plugin-local via SubView's pan,
	// then Container plugin-local -> Container doc-local via OffsetToClient's inverse.
	const auto clientToParent = gmpi::drawing::invert(moduleview->OffsetToClient());
	dragStartPoint = dragStartPoint * viewTransform;
	dragStartPoint = transformPoint(clientToParent, dragStartPoint);

	mousePoint = mousePoint * viewTransform;
	mousePoint = transformPoint(clientToParent, mousePoint);

	return moduleview->parent->StartCableDrag(type, fromModule, fromPin, dragStartPoint, mousePoint);
}

void SubView::OnCableDrag(SE2::ConnectorViewBase* dragline, gmpi::drawing::Point dragPoint, float& bestDistance, SE2::ModuleView*& bestModule, int& bestPinIndex)
{
	dragPoint = dragPoint * inv_viewTransform;

	for (auto it = children.rbegin(); it != children.rend(); ++it) // iterate in reverse for correct Z-Order.
		(*it)->OnCableDrag(dragline, dragPoint, bestDistance, bestModule, bestPinIndex);
}

SubView::SubView(SE2::ModuleView* pparent, int pparentViewType) : ViewBase({1000, 1000})
	, parent(pparent)
	, parentViewType(pparentViewType)
{
    assert(parent);

	showControlsLegacy.onUpdate = [this](auto*) { onValueChanged(); };
	showControls.onUpdate = [this](auto*) { onValueChanged(); };

	// pan starts at identity (viewTransform defaults to identity Matrix3x2).
	// panInitialized_ stays false until initialize() or measure() computes the real pan.
}

void SubView::BuildModules(Json::Value* context, std::map<int, SE2::ModuleView*>& guiObjectMap)
{
	assert(drawingHost); // needs to be initialised

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
		parent->parent->onSubPanelMadeVisible();
	}

	OnPatchCablesVisibilityUpdate();

	invalidateRect();
}

void SubView::OnPatchCablesVisibilityUpdate()
{
	parent->parent->OnPatchCablesVisibilityUpdate();
}

SE2::ConnectorViewBase* SubView::createCable(SE2::CableType type, int32_t handleFrom, int32_t fromPin)
{
	return new SE2::PatchCableView(this, handleFrom, fromPin, -1, -1);
}

void SubView::markDirtyChild(SE2::IViewChild* child)
{
	SE2::ViewBase::markDirtyChild(child);
	parent->parent->markDirtyChild(parent);
}

int32_t SubView::setCapture(SE2::IViewChild* module)
{
	// Avoid situation where some module turns off panel then captures mouse (ensuring it never can un-capture it).
	if (isShown())
		return ViewBase::setCapture(module);

	return gmpi::MP_FAIL;
}

gmpi::ReturnCode SubView::initialize()
{
	onValueChanged(); // nesc in case initial value is 0.

	// Pan is computed by measure() from the children's bbox, not restored
	// here — for a SubView the children's own layoutRects ARE the persisted
	// state (each module has its own panel-position in JSON), so there's
	// nothing extra to restore. Setting pan here based on a stored ViewCenter
	// raced with the children-loading cycle and committed bad pans.
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
	// Clip bounds will be initialized from viewBounds after it's calculated
	viewClipBounds = viewBounds;

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
													_RPT2(_CRT_WARN, "desired s[ %f %f]\n", desired.width, desirable.height);
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
			// SubView's children have their bounds_ loaded from a JSON
			// snapshot that may not include panel rects (only emitted when
			// isImbeddedView is set during export). When bounds_ is null
			// but the data model has a real persisted rect, use the
			// persisted rect for viewBounds — otherwise our bbox would be
			// stuck at the origin and the SubView would report a too-small
			// desired size, leaving the parent module's bounds clipped.
			auto effectiveLayout = m->getLayoutRect();
			if (effectiveLayout.left == 0.0f && effectiveLayout.top == 0.0f &&
				effectiveLayout.right == 0.0f && effectiveLayout.bottom == 0.0f)
			{
				const auto persistedRect = Presenter()->GetModuleRect(m->getModuleHandle());
				if (persistedRect.right != 0.0f || persistedRect.bottom != 0.0f
					|| persistedRect.left != 0.0f || persistedRect.top != 0.0f)
				{
					effectiveLayout = persistedRect;
					actualSize.width  = effectiveLayout.right  - effectiveLayout.left;
					actualSize.height = effectiveLayout.bottom - effectiveLayout.top;
				}
			}

			gmpi::drawing::Rect moduleRect(effectiveLayout.left, effectiveLayout.top,
				effectiveLayout.left + actualSize.width, effectiveLayout.top + actualSize.height);

			// Typically only when new object inserted.
			viewBounds.left = (std::min)(viewBounds.left, moduleRect.left);
			viewBounds.right = (std::max)(viewBounds.right, moduleRect.right);
			viewBounds.top = (std::min)(viewBounds.top, moduleRect.top);
			viewBounds.bottom = (std::max)(viewBounds.bottom, moduleRect.bottom);

			// Child's getClipArea() already returns the child's clip rect in our
			// child-local coord space (its bounds_ is our child-local layout rect),
			// possibly inflated to include shadow/glow overhang. No offset needed.
			const auto childClipArea = m->getClipArea();
			viewClipBounds.left = (std::min)(viewClipBounds.left, childClipArea.left);
			viewClipBounds.right = (std::max)(viewClipBounds.right, childClipArea.right);
			viewClipBounds.top = (std::min)(viewClipBounds.top, childClipArea.top);
			viewClipBounds.bottom = (std::max)(viewClipBounds.bottom, childClipArea.bottom);
		}
	}

	if (viewBounds.right == -200000) // no children. Default to small rectangle.
	{
		viewBounds.left = viewBounds.top = 0;
		viewBounds.right = viewBounds.bottom = 10;
		viewClipBounds = viewBounds;
	}

	returnDesiredSize->width = (std::max)(0.0f, getWidth(viewBounds));
	returnDesiredSize->height = (std::max)(0.0f, getHeight(viewBounds));

	// Re-align pan to the current children's bbox so the children's top-left
	// maps to the SubView's plugin-local origin. Recomputing on every
	// measure (rather than only the first) handles layout cascades — e.g.
	// after paste a SubView is rebuilt and may receive several measures with
	// placeholder children before real bounds_ load; pan auto-corrects on
	// each call. We deliberately do NOT shift the parent module here: doing
	// that during innocuous cascades yanked the parent to a wrong position.
	// User-driven moves go through OnChildMoved() which keeps pan + parent
	// in sync atomically.
	//
	// Skip when viewBounds is at origin (placeholder pattern from children
	// whose bounds_ haven't been loaded yet); a later real-data measure or
	// the render-time fallback will set pan correctly.
	const bool placeholderViewBounds =
		viewBounds.left == 0.0f && viewBounds.top == 0.0f;
	if (!placeholderViewBounds)
	{
		const float newPanX = -viewBounds.left;
		const float newPanY = -viewBounds.top;
		const bool firstRealSet = !panInitialized_;

		setPan(newPanX, newPanY);

		// On the first measure with real data, persist the pan offset so
		// future sessions can restore it without going through a layout
		// cascade. (Save/load uses module.bounds_.topLeft + pan.)
		if (firstRealSet && parentViewType == CF_PANEL_VIEW)
		{
			auto module = parent;
			Presenter()->SetViewCenter({
				newPanX + module->bounds_.left,
				newPanY + module->bounds_.top
			});
		}
	}
	return gmpi::ReturnCode::Ok;
}

gmpi::ReturnCode SubView::arrange(const gmpi::drawing::Rect* finalRect)
{
	if (!finalRect)
		return gmpi::ReturnCode::Fail;

	// ViewBase::arrange no longer touches viewTransform — pan/zoom is a TopView
	// concern. SubView's pan (installed via setPan) is preserved across arrange.
	return ViewBase::arrange(finalRect);
}

bool SubView::isShown()
{
    assert(parent);
    
    // parent can be null during initialise, so can't read the pins just yet.
	if (!parent->isShown())
		return false;

	if (parentViewType == CF_PANEL_VIEW)
		return showControls.value || showControlsLegacy.value;
	else
		return showControlsOnModule.value;
}

gmpi::ReturnCode SubView::getClipArea(gmpi::drawing::Rect* returnRect)
{
	if (!returnRect)
		return gmpi::ReturnCode::Fail;

	// Return plugin-local coords. viewClipBounds is in child-local coord space;
	// viewTransform (pan) maps child-local -> plugin-local. The owning
	// ModuleViewPanel offsets by bounds_ + pluginGraphicsPos to reach parent view.
	*returnRect = gmpi::drawing::offsetRect(viewClipBounds, { panX(), panY() });

	return gmpi::ReturnCode::Ok;
}

void SubView::refreshPan()
{
	// Belt-and-braces: re-derive pan from current children's bbox just
	// before drawing. Measure may have run with placeholder data (e.g.
	// during a post-paste rebuild where child bounds_ load asynchronously
	// across measure cycles); by render time the children's bounds_ may
	// have settled into real values that an earlier measure didn't see.
	// This guarantees the render transform matches the layout state.
	gmpi::drawing::Rect freshBounds(200000.0f, 200000.0f, -200000.0f, -200000.0f);
	for (auto& m : children)
	{
		if (m->isVisable() && dynamic_cast<SE2::ConnectorViewBase*>(m.get()) == nullptr)
		{
			const auto r = m->getLayoutRect();
			freshBounds.left   = (std::min)(freshBounds.left,   r.left);
			freshBounds.top    = (std::min)(freshBounds.top,    r.top);
			freshBounds.right  = (std::max)(freshBounds.right,  r.right);
			freshBounds.bottom = (std::max)(freshBounds.bottom, r.bottom);
		}
	}
	const bool freshIsReal =
		freshBounds.right > -200000.0f
		&& (freshBounds.left != 0.0f || freshBounds.top != 0.0f);
	if (freshIsReal
		&& (viewTransform._31 != -freshBounds.left
		 || viewTransform._32 != -freshBounds.top))
	{
		setPan(-freshBounds.left, -freshBounds.top);
	}
}

gmpi::ReturnCode SubView::render(gmpi::drawing::api::IDeviceContext* drawingContext)
{
	if (!drawingContext)
		return gmpi::ReturnCode::Fail;

	if (!isShown())
		return gmpi::ReturnCode::Unhandled;

	gmpi::drawing::Graphics g(drawingContext);

	refreshPan();

	// Apply SubView's pan (child-local -> Container plugin-local) on top of the
	// caller's transform so children render in the right place.
	const auto originalTransform = g.getTransform();
	g.setTransform(viewTransform * originalTransform);

	auto res = SE2::ViewBase::render(drawingContext);

	g.setTransform(originalTransform);
	return res;
}

// Single-layer entry point used when the enclosing view hoists our layers into its
// own passes (e.g. it pulls every shadow before any body). Same pan setup as render(),
// but emits just one layer; renderChildrenLayer recurses into nested sub-views.
gmpi::ReturnCode SubView::renderLayer(gmpi::drawing::api::IDeviceContext* drawingContext, int32_t layer)
{
	if (!drawingContext)
		return gmpi::ReturnCode::Fail;

	if (!isShown())
		return gmpi::ReturnCode::Unhandled;

	gmpi::drawing::Graphics g(drawingContext);

	refreshPan();

	const auto originalTransform = g.getTransform();
	g.setTransform(viewTransform * originalTransform);

	renderChildrenLayer(g, layer);

	g.setTransform(originalTransform);
	return gmpi::ReturnCode::Ok;
}

gmpi::ReturnCode SubView::getToolTip(gmpi::drawing::Point point, gmpi::api::IString* returnString)
{
	// Follows the same pattern as onPointerDown/onPointerMove: ViewBase's
	// base version applies inv_viewTransform (which holds SubView's pan), so
	// no manual transform here would double-apply.
	return ViewBase::getToolTip(point, returnString);
}

void SubView::process()
{
	processUnidirectionalModules();
}

gmpi::ReturnCode SubView::hitTest(gmpi::drawing::Point point, int32_t flags)
{
	if (isShown())
	{
		const auto localPoint = point * inv_viewTransform;
		for (auto it = children.rbegin(); it != children.rend(); ++it) // iterate in reverse for correct Z-Order.
		{
			auto m = (*it).get();
			if (m->hitTest(localPoint, flags) == gmpi::ReturnCode::Ok)
				return gmpi::ReturnCode::Ok;
		}
	}

	return gmpi::ReturnCode::Unhandled;
}

// These four overrides exist only for the isShown()/mouseCaptureObject guards.
// The coordinate transform (point * inv_viewTransform) happens inside ViewBase's
// base versions — SubView's pan is in inv_viewTransform, so no manual work needed.
gmpi::ReturnCode SubView::onPointerDown(gmpi::drawing::Point point, int32_t flags)
{
	if (!isShown())
		return gmpi::ReturnCode::Unhandled;
	return ViewBase::onPointerDown(point, flags);
}

gmpi::ReturnCode SubView::onPointerMove(gmpi::drawing::Point point, int32_t flags)
{
	if (!isShown())
		return gmpi::ReturnCode::Unhandled;
	return ViewBase::onPointerMove(point, flags);
}

gmpi::ReturnCode SubView::onPointerUp(gmpi::drawing::Point point, int32_t flags)
{
	// Allow up-events through when a captured object hides itself mid-click,
	// otherwise the capture would never release.
	if (!isShown() && !mouseCaptureObject)
		return gmpi::ReturnCode::Unhandled;
	return ViewBase::onPointerUp(point, flags);
}

gmpi::ReturnCode SubView::onMouseWheel(gmpi::drawing::Point point, int32_t flags, int32_t delta)
{
	if (!isShown())
		return gmpi::ReturnCode::Unhandled;
	return ViewBase::onMouseWheel(point, flags, delta);
}

// Embedded sub-views are read-only — skip the editor-level items
// (Cut/Copy/Paste/Delete/Arrange and the background "Panel Edit.../Goto Parent.../Screenshot"
// tail) that the inherited ViewBase::populateContextMenu would add. Without this override,
// right-clicking a module inside a sub-view shows those items twice: once from the enclosing
// view's ViewBase::populateContextMenu (correct, operating on the sub-view module itself) and
// once from the sub-view's own inherited body (a duplicate, with moduleHandle == -1 because
// the sub-view's mouseOverObject isn't tracking — producing the background-menu tail).
//
// We still forward to mouseOverObject so any inner module's own plugin items (e.g. a Knob's
// Learn=1/UnLearn/Edit) can appear when its pointer-tracking does land on a child.
gmpi::ReturnCode SubView::populateContextMenu(gmpi::drawing::Point point, gmpi::api::IUnknown* contextMenuItemsSink)
{
	if (!isShown())
		return gmpi::ReturnCode::Unhandled;

	point *= inv_viewTransform;

	gmpi::shared_ptr<gmpi::api::IContextItemSink> menu;
	contextMenuItemsSink->queryInterface(&gmpi::api::IContextItemSink::guid, menu.put_void());

	if (mouseOverObject)
		mouseOverObject->populateContextMenu(point, menu);

	return gmpi::ReturnCode::Ok;
}

void SubView::OnChildMoved()
{
	// TODO : enhancement - also calc my cliprect on sum of child cliprects.

//	auto parent = dynamic_cast<SE2::ViewChild*> (drawingHost.get());

	gmpi::drawing::Rect viewBoundsNew;
	gmpi::drawing::Rect viewClipBoundsNew;
	calcBounds(viewBoundsNew, viewClipBoundsNew);

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
	viewClipBounds = viewClipBoundsNew;

	setPan(-viewBoundsNew.left, -viewBoundsNew.top);

	parent->parent->OnChangedChildPosition(parent->handle, parentLayoutRect);
}

void SubView::calcBounds(gmpi::drawing::Rect& returnLayoutRect, gmpi::drawing::Rect& returnClipRect)
{
	// calc my bounds.
	// Start with inverted rect (no area).
	returnLayoutRect = gmpi::drawing::Rect(200000, 200000, -200000, -200000);
	returnClipRect = returnLayoutRect;

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

			// Child's getClipArea() is already in child-local SubView coords.
			const auto childClipArea = m->getClipArea();
			returnClipRect.left = (std::min)(returnClipRect.left, childClipArea.left);
			returnClipRect.right = (std::max)(returnClipRect.right, childClipArea.right);
			returnClipRect.top = (std::min)(returnClipRect.top, childClipArea.top);
			returnClipRect.bottom = (std::max)(returnClipRect.bottom, childClipArea.bottom);
		}
	}

	if (returnLayoutRect.right == -200000) // no children. Default to small rectangle.
	{
		returnLayoutRect.left = returnLayoutRect.top = 0;
		returnLayoutRect.right = returnLayoutRect.bottom = 10;
		returnClipRect = returnLayoutRect;
	}

	returnLayoutRect.left = floorf(returnLayoutRect.left);
	returnLayoutRect.top = floorf(returnLayoutRect.top);
	returnLayoutRect.right = ceilf(returnLayoutRect.right);
	returnLayoutRect.bottom = ceilf(returnLayoutRect.bottom);
}
