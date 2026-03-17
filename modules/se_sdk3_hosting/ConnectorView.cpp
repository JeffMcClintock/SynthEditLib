#include "ConnectorView.h"
#include "ContainerView.h"
#include "ModuleView.h"
#include "ModuleViewStruct.h"
#include "modules/shared/xplatform_modifier_keys.h"
#include "modules/shared/VectorMath.h"

using namespace gmpi::drawing;
using namespace Gmpi::VectorMath;

// perceptually even colors. (rainbow). Might need the one with purple instead (for BLOBs).
// https://bokeh.github.io/colorcet/

namespace SE2
{
	ConnectorViewBase::ConnectorViewBase(Json::Value* pDatacontext, ViewBase* pParent) : ViewChild(pDatacontext, pParent)
	{
		auto& object_json = *datacontext;

		fmPin = { object_json["fMod"].asInt(), object_json["fPlg"].asInt() };
		toPin = { object_json["tMod"].asInt(), object_json["tPlg"].asInt() };

		setSelected(object_json["selected"].asBool());
		highlightFlags = object_json["highlightFlags"].asInt();

#if defined( _DEBUG )
//		if(highlightFlags)
		{
//			_RPTN(0, "ConnectorViewBase::ConnectorViewBase highlightFlags =  %d\n", highlightFlags);
		}
		cancellation = object_json["Cancellation"].asFloat();
#endif
	}

	void ConnectorViewBase::measure(gmpi::drawing::Size availableSize, gmpi::drawing::Size* returnDesiredSize)
	{
		// Measure/Arrange not really applicable to lines.
		returnDesiredSize->height = 10;
		returnDesiredSize->width = 10;
	}

	void ConnectorViewBase::arrange(gmpi::drawing::Rect finalRect)
	{
		// Measure/Arrange not applicable to lines. Determines it's own bounds during arrange phase.
		// don't overwright.
		// ViewChild::arrange(finalRect);

		OnModuleMoved();
	}

	void ConnectorViewBase::setHighlightFlags(int flags)
	{
//		_RPTN(0, "ConnectorViewBase::setHighlightFlags %d\n", flags);
		highlightFlags = flags;

		if (flags == 0)
			parent->MoveToBack(this);
		else
			parent->MoveToFront(this);

		parent->ChildInvalidateRect(getClipArea());
	}

	void ConnectorViewBase::pickup(int pdraggingFromEnd, gmpi::drawing::Point pMousePos)
	{
		parent->ChildInvalidateRect(getClipArea());

		if (pdraggingFromEnd == 0)
			from_ = pMousePos;
		else
			to_ = pMousePos;

		draggingFromEnd = pdraggingFromEnd;
		wasPickedUp = true;

		parent->setCapture(this);

		CalcBounds();

		parent->MoveToFront(this);

		parent->ChildInvalidateRect(getClipArea());
	}

	void ConnectorViewBase::OnModuleMoved()
	{
		auto module1 = Presenter()->HandleToObject(fmPin.module);
		auto module2 = Presenter()->HandleToObject(toPin.module);

		if (module1 == nullptr || module2 == nullptr)
			return;

		auto from = module1->getConnectionPoint(type, fmPin.index);
		auto to = module2->getConnectionPoint(type, toPin.index);

		from = module1->parent->MapPointToView(parent, from);
		to = module2->parent->MapPointToView(parent, to);

		if (from != from_ || to != to_)
		{
			from_ = from;
			to_ = to;
			CalcBounds();
		}
	}

	void PatchCableView::CreateGeometry()
	{
		gmpi::drawing::Factory factory;

		{
			gmpi::shared_ptr<gmpi::api::IUnknown> unknown;
			parent->drawingHost->getDrawingFactory(unknown.put());
			unknown->queryInterface(&gmpi::drawing::api::IFactory::guid, (void**) AccessPtr::put(factory));

//			auto fac = unknown.as<gmpi::drawing::Factory>();

	//		AccessPtr.put(factory) = AccessPtr::get(fac);
		}

//		gmpi::drawing::Factory factory(parent->GetDrawingFactory());

		strokeStyle = factory.createStrokeStyle(useDroop() ? CapStyle::Round : CapStyle::Flat);
		geometry = factory.createPathGeometry();
		auto sink = geometry.open();

		//			_RPT4(_CRT_WARN, "Calc [%.3f,%.3f] -> [%.3f,%.3f]\n", from_.x, from_.y, to_.x, to_.y );

		// No curve when dragging.
		if (/*draggingFromEnd >= 0 ||*/ !useDroop())
		{
			// straight line.
			sink.beginFigure(from_, FigureBegin::Hollow);
			sink.addLine(to_);

			//			_RPT4(_CRT_WARN, "FRom[%.3f,%.3f] -> [%.3f,%.3f]\n", from_.x, from_.y, to_.x, to_.y );
		}
		else
		{
			sink.beginFigure(from_, FigureBegin::Hollow);
			// sagging curve.
			gmpi::drawing::Size droopOffset(0, 20);
			sink.addBezier(BezierSegment(from_ + droopOffset, to_ + droopOffset, to_));
		}

		sink.endFigure(FigureEnd::Open);
		sink.close();
	}

	void PatchCableView::CalcBounds()
	{
		OnVisibilityUpdate();
		CreateGeometry();

		auto oldBounds = bounds_;
		bounds_ = geometry.getWidenedBounds((float)cableDiameter, strokeStyle);

		if (oldBounds != bounds_)
		{
			oldBounds = unionRect(oldBounds, bounds_);
			parent->ChildInvalidateRect(oldBounds);
		}
	}

	bool PatchCableView::isShown() // Indicates if cable should be drawn/clickable or not (because of 'Show on Parent' state).
	{
		if (draggingFromEnd >= 0)
			return true;

		auto module1 = Presenter()->HandleToObject(fmPin.module);
		auto module2 = Presenter()->HandleToObject(toPin.module);

		return module1 && module2 && module1->isShown() && module2->isShown();
	}

	void PatchCableView::OnVisibilityUpdate()
	{
		bool newIsShown = isShown();
		bool changed = newIsShown != isShownCached;
		isShownCached = newIsShown;
		if (changed)
		{
			auto r = getClipArea();
			parent->invalidateRect(&r);
		}
	}

	GraphicsResourceCache<sharedGraphicResources_patchcables> drawingResourcesCachePatchCables;

	sharedGraphicResources_patchcables* PatchCableView::getDrawingResources(gmpi::drawing::Graphics& g)
	{
		if (!drawingResources)
		{
			drawingResources = drawingResourcesCachePatchCables.get(g);
		}

		return drawingResources.get();
	}


	void PatchCableView::render(gmpi::drawing::Graphics& g)
	{
		if (!geometry || !isShownCached)
			return;

		auto resources = getDrawingResources(g);

//		g.fillRectangle(bounds_, g.createSolidColorBrush(Colors::AliceBlue));

		const bool drawSolid = isHovered || draggingFromEnd >= 0;

		if (drawSolid)
		{
			g.drawGeometry(geometry, resources->outlineBrush, 6.0f, strokeStyle);
		}

		// Colored fill.
		g.drawGeometry(geometry, resources->brushes[colorIndex][1 - static_cast<int>(drawSolid)], 4.0f, strokeStyle);

		if (!drawSolid || draggingFromEnd >= 0)
			return;

		// draw white highlight on cable.
		Matrix3x2 originalTransform = g.getTransform();

		auto adjustedTransform = makeTranslation(-1, -1) * originalTransform;

		g.setTransform(adjustedTransform);

		g.drawGeometry(geometry, resources->highlightBrush, 1.0f, strokeStyle);

		g.setTransform(originalTransform);
	}

	gmpi::ReturnCode PatchCableView::setHover(bool mouseIsOverMe)
	{
		if(isHovered != mouseIsOverMe)
		{
			isHovered = mouseIsOverMe;

			const auto r = getClipArea();
			parent->invalidateRect(&r);
		}

		return gmpi::ReturnCode::Ok;
	}
	
	// Mis-used as a global mouse tracker.
	gmpi::ReturnCode PatchCableView::hitTest(gmpi::drawing::Point point, int32_t flags)
	{
		if (!isShownCached)
			return gmpi::ReturnCode::Unhandled;

		// <ctrl> or <shift> click ignores patch cables (so patch point can spawn new cable)
		if ((flags & (gmpi_gui_api::GG_POINTER_KEY_CONTROL| gmpi_gui_api::GG_POINTER_KEY_SHIFT)) != 0)
			return gmpi::ReturnCode::Unhandled;

		if (!pointInRect(point, bounds_) || !geometry) // FM-Lab has null geometries for hidden patch cables.
			return gmpi::ReturnCode::Unhandled;

		// Hits ignored, except at line ends. So cables don't interfere with knobs.
		float distanceToendSquared = {};
		constexpr float lineHitWidth = 7.0f;

		{
			gmpi::drawing::Size delta = from_ - Point(point);
			distanceToendSquared = delta.width * delta.width + delta.height * delta.height;
			constexpr float hitRadiusSquared = mouseNearEndDist * mouseNearEndDist;
			if (distanceToendSquared > hitRadiusSquared)
			{
				delta = to_ - Point(point);
				distanceToendSquared = delta.width * delta.width + delta.height * delta.height;

				if (distanceToendSquared > hitRadiusSquared)
					return gmpi::ReturnCode::Unhandled;
			}
		}

		// Do proper hit testing.
		return geometry.strokeContainsPoint(point, lineHitWidth, strokeStyle) ? gmpi::ReturnCode::Ok : gmpi::ReturnCode::Unhandled;
	}

	PatchCableView::~PatchCableView()
	{
		parent->OnChildDeleted(this);
		parent->autoScrollStop();
	}

	gmpi::ReturnCode PatchCableView::onPointerDown(gmpi::drawing::Point point, int32_t flags)
	{
		if (imCaptured()) //parent->getCapture()) // dragging?
		{
			parent->releaseCapture();
			parent->EndCableDrag(point, this, flags);
			// I am now DELETED!!!
			return gmpi::ReturnCode::Unhandled;
		}

		if (!isHovered)
			return gmpi::ReturnCode::Unhandled;

		// Select Object.
		Presenter()->ObjectClicked(handle, flags); //gmpi::modifier_keys::getHeldKeys());

		if ((flags & gmpi_gui_api::GG_POINTER_FLAG_FIRSTBUTTON) != 0)
		{
/* confusing
			// <SHIFT> deletes cable.
			if ((flags & gmpi_gui_api::GG_POINTER_KEY_SHIFT) != 0)
			{
				parent->RemoveCables(this);
				return gmpi::MP_HANDLED;
			}
*/

			// Left-click
			gmpi::drawing::Size delta = from_ - Point(point);
			const float lengthSquared = delta.width * delta.width + delta.height * delta.height;
			constexpr float hitRadiusSquared = mouseNearEndDist * mouseNearEndDist;
			const int hitEnd = (lengthSquared < hitRadiusSquared) ? 0 : 1;

			pickup(hitEnd, point);
		}

		return gmpi::ReturnCode::Unhandled;
	}

	gmpi::ReturnCode ConnectorViewBase::onPointerMove(gmpi::drawing::Point point, int32_t flags)
	{
		if (imCaptured())
		{
			if (draggingFromEnd == 0)
				from_ = point;
			else
				to_ = point;

			endIsSnapped = parent->OnCableMove(this);

			CalcBounds();
			return gmpi::ReturnCode::Unhandled;
		}

		return gmpi::ReturnCode::Unhandled;
	}

	gmpi::ReturnCode ConnectorViewBase::onPointerUp(gmpi::drawing::Point point, int32_t flags)
	{
		endIsSnapped = false;
		if (imCaptured())
		{
			// detect single clicks on pin, continue dragging.
			const float dragThreshold = 6;
			if (abs(from_.x - to_.x) < dragThreshold && abs(from_.y - to_.y) < dragThreshold)
				return gmpi::ReturnCode::Unhandled;

			if(wasPickedUp)
			{
				wasPickedUp = false;
				return gmpi::ReturnCode::Unhandled;
			}

			parent->autoScrollStop();
			parent->releaseCapture();
			parent->EndCableDrag(point, this, 0); // passsing zero flags on mouse-up, since alt key only relevant when clicking on pins while dragging.
			// I am now DELETED!!!
		}

		return gmpi::ReturnCode::Unhandled;
	}

	gmpi::ReturnCode PatchCableView::populateContextMenu(gmpi::drawing::Point point, gmpi::api::IUnknown* contextMenuItemsSink)
	{
#if 0 // TODO
		gmpi_sdk::mp_shared_ptr<gmpi::IMpContextItemSink> menu;
		auto r = contextMenuItemsSink->queryInterface(gmpi::MP_IID_CONTEXT_ITEMS_SINK, menu.asIMpUnknownPtr());

		//		contextMenuItemsSink->currentCallback = [this](int32_t idx, gmpi::drawing::Point point) { return onContextMenu(idx); };
		menu->AddItem("Remove Cable", 0);
#endif
		return gmpi::ReturnCode::Unhandled;
	}

	gmpi::ReturnCode ConnectorViewBase::onContextMenu(int32_t idx)
	{
		if (idx == 0)
		{
			//!!! Probably should just use selection and deletion like all else!!!
			// might need some special-case handling, since objects don't exist as docobs.
			parent->RemoveCables(this);
		}

		return gmpi::ReturnCode::Unhandled;
	}

} // namespace

