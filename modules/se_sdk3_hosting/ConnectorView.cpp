
#include "ConnectorView.h"
#include "ContainerView.h"
#include "ModuleView.h"
#include "modules/shared/xplatform_modifier_keys.h"
#include "modules/shared/VectorMath.h"

using namespace GmpiDrawing;
using namespace Gmpi::VectorMath;

// perceptualy even colors. (rainbow). Might need the one with purple instead (for BLOBs).
// https://bokeh.github.io/colorcet/

namespace SE2
{
	ConnectorViewBase::ConnectorViewBase(Json::Value* pDatacontext, ViewBase* pParent) : ViewChild(pDatacontext, pParent)
	{
		auto& object_json = *datacontext;

		fromModuleH = object_json["fMod"].asInt();
		toModuleH = object_json["tMod"].asInt();

		fromModulePin = object_json["fPlg"].asInt();
		toModulePin = object_json["tPlg"].asInt();

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

	int32_t ConnectorViewBase::measure(GmpiDrawing::Size availableSize, GmpiDrawing::Size* returnDesiredSize)
	{
		// Measure/Arrange not really applicable to lines.
		returnDesiredSize->height = 10;
		returnDesiredSize->width = 10;

		return gmpi::MP_OK;
	}

	int32_t ConnectorViewBase::arrange(GmpiDrawing::Rect finalRect)
	{
		// Measure/Arrange not applicable to lines. Determines it's own bounds during arrange phase.
		// don't overwright.
		// ViewChild::arrange(finalRect);

		OnModuleMoved();

		return gmpi::MP_OK;
	}

	void ConnectorViewBase::setHighlightFlags(int flags)
	{
//		_RPTN(0, "ConnectorViewBase::setHighlightFlags %d\n", flags);
		highlightFlags = flags;

		if (flags == 0)
		{
			parent->MoveToBack(this);
		}
		else
		{
			parent->MoveToFront(this);
		}

		const auto r = GetClipRect();
		parent->invalidateRect(&r);
	}

	void ConnectorViewBase::pickup(int pdraggingFromEnd, GmpiDrawing_API::MP1_POINT pMousePos)
	{
		if (pdraggingFromEnd == 0)
			from_ = pMousePos;
		else
			to_ = pMousePos;

		draggingFromEnd = pdraggingFromEnd;
		parent->setCapture(this);

		parent->invalidateRect(); // todo bounds only. !!!

		CalcBounds();
		parent->invalidateRect(&bounds_);
	}

	void ConnectorViewBase::OnModuleMoved()
	{
		auto module1 = Presenter()->HandleToObject(fromModuleH);
		auto module2 = Presenter()->HandleToObject(toModuleH);

		if (module1 == nullptr || module2 == nullptr)
			return;

		auto from = module1->getConnectionPoint(type, fromModulePin);
		auto to = module2->getConnectionPoint(type, toModulePin);

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
		GmpiDrawing::Factory factory(parent->GetDrawingFactory());
		strokeStyle = factory.CreateStrokeStyle(useDroop() ? CapStyle::Round : CapStyle::Flat);
		geometry = factory.CreatePathGeometry();
		auto sink = geometry.Open();

		//			_RPT4(_CRT_WARN, "Calc [%.3f,%.3f] -> [%.3f,%.3f]\n", from_.x, from_.y, to_.x, to_.y );

		// No curve when dragging.
		if (/*draggingFromEnd >= 0 ||*/ !useDroop())
		{
			// straight line.
			sink.BeginFigure(from_, FigureBegin::Hollow);
			sink.AddLine(to_);

			//			_RPT4(_CRT_WARN, "FRom[%.3f,%.3f] -> [%.3f,%.3f]\n", from_.x, from_.y, to_.x, to_.y );
		}
		else
		{
			sink.BeginFigure(from_, FigureBegin::Hollow);
			// sagging curve.
			GmpiDrawing::Size droopOffset(0, 20);
			sink.AddBezier(BezierSegment(from_ + droopOffset, to_ + droopOffset, to_));
		}

		sink.EndFigure(FigureEnd::Open);
		sink.Close();
	}

	void PatchCableView::CalcBounds()
	{
		OnVisibilityUpdate();
		CreateGeometry();

		auto oldBounds = bounds_;
		bounds_ = geometry.GetWidenedBounds((float)cableDiameter, strokeStyle);

		if (oldBounds != bounds_)
		{
			oldBounds.Union(bounds_);
			parent->ChildInvalidateRect(oldBounds);
		}
	}

	bool PatchCableView::isShown() // Indicates if cable should be drawn/clickable or not (because of 'Show on Parent' state).
	{
		if (draggingFromEnd >= 0)
			return true;

		auto module1 = Presenter()->HandleToObject(fromModuleH);
		auto module2 = Presenter()->HandleToObject(toModuleH);

		return module1 && module2 && module1->isShown() && module2->isShown();
	}

	void PatchCableView::OnVisibilityUpdate()
	{
		bool newIsShown = isShown();
		bool changed = newIsShown != isShownCached;
		isShownCached = newIsShown;
		if (changed)
		{
			auto r = GetClipRect();
			parent->invalidateRect(&r);
		}
	}

	GraphicsResourceCache<sharedGraphicResources_patchcables> drawingResourcesCachePatchCables;

	sharedGraphicResources_patchcables* PatchCableView::getDrawingResources(GmpiDrawing::Graphics& g)
	{
		if (!drawingResources)
		{
			drawingResources = drawingResourcesCachePatchCables.get(g);
		}

		return drawingResources.get();
	}


	void PatchCableView::OnRender(GmpiDrawing::Graphics& g)
	{
		if (geometry.isNull() || !isShownCached)
			return;

		auto resources = getDrawingResources(g);

//		g.FillRectangle(bounds_, g.CreateSolidColorBrush(Color::AliceBlue));

		const bool drawSolid = isHovered || draggingFromEnd >= 0;

		if (drawSolid)
		{
			g.DrawGeometry(geometry, resources->outlineBrush, 6.0f, strokeStyle);
		}

		// Colored fill.
		g.DrawGeometry(geometry, resources->brushes[colorIndex][1 - static_cast<int>(drawSolid)], 4.0f, strokeStyle);

		if (!drawSolid || draggingFromEnd >= 0)
			return;

		// draw white highlight on cable.
		Matrix3x2 originalTransform = g.GetTransform();

		auto adjustedTransform = Matrix3x2::Translation(-1, -1) * originalTransform;

		g.SetTransform(adjustedTransform);

		g.DrawGeometry(geometry, resources->highlightBrush, 1.0f, strokeStyle);

		g.SetTransform(originalTransform);
	}

	void PatchCableView::setHover(bool mouseIsOverMe)
	{
		if(isHovered != mouseIsOverMe)
		{
			isHovered = mouseIsOverMe;

			const auto r = GetClipRect();
			parent->invalidateRect(&r);
		}
	}

	// Mis-used as a global mouse tracker.
	bool PatchCableView::hitTest(int32_t flags, GmpiDrawing_API::MP1_POINT point)
	{
		if (!isShownCached)
			return false;

		// <ctrl> or <shift> click ignores patch cables (so patch point can spawn new cable)
		if ((flags & (gmpi_gui_api::GG_POINTER_KEY_CONTROL| gmpi_gui_api::GG_POINTER_KEY_SHIFT)) != 0)
			return false;

		if (!bounds_.ContainsPoint(point) || geometry.isNull()) // FM-Lab has null geometries for hidden patch cables.
			return false;

		// Hits ignored, except at line ends. So cables don't interfere with knobs.
		float distanceToendSquared = {};
		constexpr float lineHitWidth = 7.0f;

		{
			GmpiDrawing::Size delta = from_ - Point(point);
			distanceToendSquared = delta.width * delta.width + delta.height * delta.height;
			constexpr float hitRadiusSquared = mouseNearEndDist * mouseNearEndDist;
			if (distanceToendSquared > hitRadiusSquared)
			{
				delta = to_ - Point(point);
				distanceToendSquared = delta.width * delta.width + delta.height * delta.height;
				if (distanceToendSquared > hitRadiusSquared)
				{
					return false;
				}
			}
		}

		// Do proper hit testing.
		return geometry.StrokeContainsPoint(point, lineHitWidth, strokeStyle.Get());
	}

	PatchCableView::~PatchCableView()
	{
		parent->OnChildDeleted(this);
		parent->autoScrollStop();
	}

	int32_t PatchCableView::onPointerDown(int32_t flags, GmpiDrawing_API::MP1_POINT point)
	{
		if (parent->getCapture()) // dragging?
		{
			parent->releaseCapture();
			parent->EndCableDrag(point, this);
			// I am now DELETED!!!
			return gmpi::MP_HANDLED;
		}

		if (!isHovered)
			return gmpi::MP_UNHANDLED;

		// Select Object.
		Presenter()->ObjectClicked(handle, gmpi::modifier_keys::getHeldKeys());

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
			GmpiDrawing::Size delta = from_ - Point(point);
			const float lengthSquared = delta.width * delta.width + delta.height * delta.height;
			constexpr float hitRadiusSquared = mouseNearEndDist * mouseNearEndDist;
			const int hitEnd = (lengthSquared < hitRadiusSquared) ? 0 : 1;

			pickup(hitEnd, point);
		}

		return gmpi::MP_HANDLED; // Indicate menu already shown.
	}

	int32_t ConnectorViewBase::onPointerMove(int32_t flags, GmpiDrawing_API::MP1_POINT point)
	{
		if (parent->getCapture())
		{
			if (draggingFromEnd == 0)
				from_ = point;
			else
				to_ = point;

			parent->OnCableMove(this);

			CalcBounds();

			return gmpi::MP_OK;
		}

		return gmpi::MP_UNHANDLED;
	}

	int32_t ConnectorViewBase::onPointerUp(int32_t flags, GmpiDrawing_API::MP1_POINT point)
	{
		if (parent->getCapture())
		{
			// detect single clicks on pin, continue dragging.
			const float dragThreshold = 6;
			if (abs(from_.x - to_.x) < dragThreshold && abs(from_.y - to_.y) < dragThreshold)
			{
				return gmpi::MP_HANDLED;
			}

			parent->autoScrollStop();
			parent->releaseCapture();
			parent->EndCableDrag(point, this);
			// I am now DELETED!!!
		}

		return gmpi::MP_OK;
	}

	int32_t PatchCableView::populateContextMenu(float x, float y, gmpi::IMpUnknown* contextMenuItemsSink)
	{
		gmpi_sdk::mp_shared_ptr<gmpi::IMpContextItemSink> menu;
		auto r = contextMenuItemsSink->queryInterface(gmpi::MP_IID_CONTEXT_ITEMS_SINK, menu.asIMpUnknownPtr());

		//		contextMenuItemsSink->currentCallback = [this](int32_t idx, GmpiDrawing_API::MP1_POINT point) { return onContextMenu(idx); };
		menu->AddItem("Remove Cable", 0);

		return gmpi::MP_OK;
	}

	int32_t ConnectorViewBase::onContextMenu(int32_t idx)
	{
		if (idx == 0)
		{
			//!!! Probably should just use selection and deletion like all else!!!
			// might need some special-case handling, since objects don't exist as docobs.
			parent->RemoveCables(this);
		}
		return gmpi::MP_OK;
	}

} // namespace

