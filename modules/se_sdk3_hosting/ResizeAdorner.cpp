#include "ResizeAdorner.h"
#include "ViewBase.h"
#include <cassert>
#include <cmath>

using namespace gmpi::drawing;

namespace SE2
{
	std::vector<ResizeAdorner::node> ResizeAdorner::getNodes() const
	{
		auto r = offsetRect(getNodeRect(), { -bounds.left, -bounds.top });

//		const Size offsetToModule(module->bounds_.left - bounds.left, module->bounds_.top - bounds.top);
//		auto r = offsetRect(module->pluginGraphicsPos, offsetToModule); // outline of module graphics insert.

		int startX, endX;
		if (isResizableX)
		{
			startX = 0;
			endX = 2;
		}
		else
		{
			startX = endX = 1;
		}

		int startY, endY;
		if (isResizableY)
		{
			startY = parent->getViewType() == CF_PANEL_VIEW ? 0 : 1;
			endY = 2;
		}
		else
		{
			startY = endY = 1;
		}

		std::vector<node> nodes;
		for (int x = startX; x <= endX; ++x)
		{
			const auto pointx = r.left + getWidth(r) * (float)x * 0.5f;
			for (int y = startY; y <= endY; ++y)
			{
				const auto pointy = r.top + getHeight(r) * (float)y * 0.5f;
				if (x != 1 || y != 1)
					nodes.push_back({ x, y, {pointx, pointy} });
			}
		}

		return nodes;
	}

	ResizeAdorner::ResizeAdorner(ViewBase* pParent, ModuleView* pModule)
		: parent(pParent)
		, module(pModule)
		, moduleHandle(pModule->getModuleHandle())
		, color(gmpi::drawing::Colors::DodgerBlue)
		, hasGripper(true)
	{
		if (pModule->ignoreMouse)
		{
			hasGripper = false;
			color = gmpi::drawing::Colors::Gray;
		}

		bounds = clientBoundsToAdorner(module->getLayoutRect());
	}

	ResizeAdorner::~ResizeAdorner()
	{
		assert(!parent || parent->mouseOverObject != this);
	}

	void ResizeAdorner::measure(gmpi::drawing::Size availableSize, gmpi::drawing::Size* returnDesiredSize)
	{
		gmpi::drawing::Size desiredMax(0, 0);
		gmpi::drawing::Size desiredMin(0, 0);
		module->measure(gmpi::drawing::Size(0, 0), &desiredMin);
		module->measure(gmpi::drawing::Size(10000, 10000), &desiredMax);

		isResizableX = desiredMin.width != desiredMax.width;
		isResizableY = desiredMin.height != desiredMax.height;

		*returnDesiredSize = availableSize;
	}

	void ResizeAdorner::arrange(gmpi::drawing::Rect finalRect)
	{
		bounds = clientBoundsToAdorner(finalRect);
	}

	gmpi::drawing::Rect ResizeAdorner::clientBoundsToAdorner(gmpi::drawing::Rect r)
	{
		const int penThickness = 1;
		auto r2 = inflateRect(r, ResizeHandleRadius + SelectionFrameOffset + penThickness);

		if (hasGripper)
			r2.top -= DragAreaheight;

		return r2;
	}

	gmpi::drawing::Rect ResizeAdorner::getNodeRect() const
	{
		const Size offsetToModule(module->bounds_.left - bounds.left, module->bounds_.top - bounds.top);
		auto r = offsetRect(module->pluginGraphicsPos, offsetToModule); // outline of module graphics insert.
		r = inflateRect(r, (float)SelectionFrameOffset);

		if(hasGripper)
			r.top -= DragAreaheight - 0.5f;

		return offsetRect(r, { bounds.left, bounds.top });
	}

	gmpi::drawing::Rect ResizeAdorner::getLayoutRect()
	{
		return bounds;
	}

	gmpi::drawing::Rect ResizeAdorner::getClipArea()
	{
		return inflateRect(getNodeRect(), ResizeHandleRadius + 1.0f);
	}

	gmpi::drawing::Rect ResizeAdornerStructure::getClipArea()
	{
		return inflateRect(module->getClipArea(), 2.5f);
	}

	void ResizeAdorner::OnMoved(gmpi::drawing::Rect& r)
	{
		auto invalidRect = bounds;
		bounds = clientBoundsToAdorner(r);
		invalidRect = unionRect(invalidRect, bounds);
		invalidRect = inflateRect(invalidRect, (float)ResizeHandleRadius);

		parent->ChildInvalidateRect(invalidRect);
	}

	void ResizeAdorner::OnNodesMoved(std::vector<gmpi::drawing::Point>&)
	{

	}

	void ResizeAdorner::render(gmpi::drawing::Graphics& g)
	{
		auto r = offsetRect(getNodeRect(), { -bounds.left, -bounds.top });
		auto brush = g.createSolidColorBrush(color);
		auto highlightBrush = g.createSolidColorBrush(gmpi::drawing::Colors::DeepSkyBlue);
		const float strokeWidth = 2.5;

		// Blue Outline
		{
			auto outlineGeometry = module->getOutline(g.getFactory());
			if(outlineGeometry) // structure view
			{
				auto offsetToModule = Size(module->bounds_.left - bounds.left, module->bounds_.top - bounds.top);
				auto before = g.getTransform();

				g.setTransform(makeTranslation(offsetToModule) * before);

				auto moduleOutlineBrush = g.createSolidColorBrush(Colors::DodgerBlue);
				g.drawGeometry(outlineGeometry, moduleOutlineBrush, strokeWidth);

				g.setTransform(before);
			}
			else // panel view. no structure view outline, so draw a simple rect.
			{
				g.drawRectangle(r, brush, strokeWidth);
			}
		}

		if (hasGripper)
		{
			auto dragArea = inflateRect(r, strokeWidth * 0.5f);
			dragArea.bottom = dragArea.top + DragAreaheight;
			g.fillRectangle(dragArea, brush);

			// dash pattern
			auto bg = g.createSolidColorBrush(gmpi::drawing::Color{ 0.0f, 0.0f, 0.0f, 0.125f });
			for (int x = 1; x < getWidth(r) - 1; x += 3)
			{
				for (int y = 1; y < DragAreaheight - 1; y += 2)
				{
					if (((x / 3) ^ (y / 2)) & 0x1)
					{
						gmpi::drawing::Rect rg(r.left + x, r.top + y, r.left + x + 2, r.top + y + 1);
						g.fillRectangle(rg, bg);
					}
				}
			}
		}

		auto fillBrush = g.createSolidColorBrush(gmpi::drawing::Colors::White);

		for (auto& n : getNodes())
		{
			const auto isHighlighted = n.xIndex == currentNodeX && n.yIndex == currentNodeY;

			g.fillCircle(n.location, (float)ResizeHandleRadius, isHighlighted ? highlightBrush : fillBrush);
			g.drawCircle(n.location, (float)ResizeHandleRadius, brush);
		}
	}

	gmpi::ReturnCode ResizeAdorner::hitTest(gmpi::drawing::Point point, int32_t)
	{
		auto [distance, nx, ny] = hitTestWhat(point);
		return distance <= 0.0f ? gmpi::ReturnCode::Ok : gmpi::ReturnCode::Unhandled;

#if 0
		gmpi::drawing::Rect r(getNodeRect());

		gmpi::drawing::Rect outerRect(r);
		const float outerThickness = ResizeHandleRadius;
		outerRect = inflateRect(outerRect, outerThickness);
		if (!pointInRect(point, outerRect))
		{
			return gmpi::ReturnCode::Unhandled;
		}

		if (hasGripper && point.y >= 0 && point.y < r.top + DragAreaheight)
		{
			return gmpi::ReturnCode::Ok;
		}

		gmpi::drawing::Point pointLocal{ point.x - bounds.left, point.y - bounds.top };

		int hitNodeX, hitNodeY;
		hitTestNodes(pointLocal, hitNodeX, hitNodeY);
		if (hitNodeX >= 0 || hitNodeY >= 0)
		{
			return gmpi::ReturnCode::Ok;
		}

		gmpi::drawing::Rect innerRect(r);
		innerRect = inflateRect(innerRect, -1.5f);
		return !pointInRect(point, innerRect) ? gmpi::ReturnCode::Ok : gmpi::ReturnCode::Unhandled;
#endif

	}

	bool ResizeAdorner::hitTestR(int32_t, gmpi::drawing::Rect selectionRect)
	{
		return overlaps(selectionRect, getNodeRect());
	}

	//     distance nodeX, nodeY
	std::tuple<float, int, int> ResizeAdorner::hitTestWhat(gmpi::drawing::Point point)
	{
		const auto definatalyOutside = !pointInRect(point, inflateRect(bounds, fuzzyHitTestLimit + ResizeHandleRadius));

		if(definatalyOutside)
			return { 1000.0f, -1, -1 }; // not-hit.

		const Point pointLocal(point.x - bounds.left, point.y - bounds.top);

		// if point is inside module imbedded graphics, then return a miss.
		{
			const Size offsetToModule(module->bounds_.left - bounds.left, module->bounds_.top - bounds.top);
			const auto pluginGraphicsPos = offsetRect(module->pluginGraphicsPos, offsetToModule); // outline of module graphics insert.
			if(pointInRect(pointLocal, pluginGraphicsPos))
				return { 1000.0f, -1, -1 }; // not-hit.
		}

		gmpi::drawing::Rect r(getNodeRect());

		if(hasGripper) // hit gripper?
		{
			gmpi::drawing::Rect dragArea(r);
			dragArea.bottom = dragArea.top + DragAreaheight;

			if(pointInRect(point, dragArea))
				return { 0.0f, -1, -1 };
		}

		// distance outside rect.
		const auto distanceOutside = std::max(std::max(r.left - point.x, point.x - r.right), std::max(r.top - point.y, point.y - r.bottom));

		float best = distanceOutside > 0.0f ? distanceOutside : fuzzyHitTestLimit;

		// destance inside rect
		const auto distanceInside = std::min(std::min(point.x - r.left, r.right - point.x), std::min(point.y - r.top, r.bottom - point.y));

		if(distanceInside >= 0.0f && distanceInside < best) // negative distance are not inside. ignore.
			best = distanceInside;

		// distance to nodes.
		float bestSquared = (best + ResizeHandleRadius) * (best + ResizeHandleRadius);
		int hitNodeX = -1;
		int hitNodeY = -1;
		for(auto& n : getNodes())
		{
			const float dx = n.location.x - pointLocal.x;
			const float dy = n.location.y - pointLocal.y;
			const auto distance2 = dx * dx + dy * dy;

			if(distance2 > bestSquared)
				continue;

			bestSquared = distance2;

			hitNodeX = n.xIndex;
			hitNodeY = n.yIndex;
		}
//		const auto nearestNode = sqrtf(bestSquared) - ResizeHandleRadius;

		if(hitNodeX > -1)
			best = sqrtf(bestSquared) - ResizeHandleRadius;

//		_RPTN(0, "ResizeAdorner::hitTestFuzzy: distanceOutside=%.1f distanceInside=%.1f best=%.1f\n", distanceOutside, distanceInside, best);
		return { best, hitNodeX, hitNodeY };
	}

	float ResizeAdorner::hitTestFuzzy(int32_t flags, gmpi::drawing::Point point)
	{
		auto [distance, nx, ny] = hitTestWhat(point);
		return distance;
	}

	std::string ResizeAdorner::getToolTip(gmpi::drawing::Point)
	{
		return {};
	}

	void ResizeAdorner::receiveMessageFromAudio(void*)
	{
	}

	void ResizeAdorner::hitTestNodes(gmpi::drawing::Point point, int& hitNodeX, int& hitNodeY)
	{
		for (auto& n : getNodes())
		{
			const float dx = n.location.x - point.x;
			const float dy = n.location.y - point.y;

			if ((dx * dx + dy * dy) <= (float)((1 + ResizeHandleRadius) * (1 + ResizeHandleRadius)))
			{
				hitNodeX = n.xIndex;
				hitNodeY = n.yIndex;
				return;
			}
		}

		hitNodeX = hitNodeY = -1;
	}

	gmpi::ReturnCode ResizeAdorner::setHover(bool isMouseOverMe)
	{
		mouseHover = isMouseOverMe;
		parent->invalidateRect(&bounds);

		if(!isMouseOverMe)
		{
			currentNodeX = currentNodeY = -1;
			OnMoved(module->bounds_); // redraws.
		}

		return gmpi::ReturnCode::Ok;
	}

	gmpi::ReturnCode ResizeAdorner::onPointerDown(gmpi::drawing::Point point, int32_t)
	{
		pointPrev = point;
		parent->setCapture(this);
		parent->autoScrollStart();
		return gmpi::ReturnCode::Unhandled;
	}

	gmpi::ReturnCode ResizeAdorner::onPointerMove(gmpi::drawing::Point point, int32_t)
	{
		if(!parent->isCaptured(this))
		{
			auto [distance, nx, ny] = hitTestWhat(point);

			if(nx != currentNodeX || ny != currentNodeY)
				OnMoved(module->bounds_); // redraws.

			currentNodeX = nx;
			currentNodeY = ny;

			return gmpi::ReturnCode::Unhandled;
		}

		auto snapGridSize = parent->Presenter()->GetSnapSize();
		gmpi::drawing::Size delta(point.x - pointPrev.x, point.y - pointPrev.y);

		if (delta.width == 0.0f && delta.height == 0.0f)
			return gmpi::ReturnCode::Unhandled;

		gmpi::drawing::Point snapReference(module->getLayoutRect().left, module->getLayoutRect().top);

		switch (currentNodeX)
		{
		case -1: // dragging entire module
		{
			gmpi::drawing::Point newPoint = snapReference;
			newPoint.x += delta.width;
			newPoint.y += delta.height;
			newPoint.x = floorf((snapGridSize / 2 + newPoint.x) / snapGridSize) * snapGridSize;
			newPoint.y = floorf((snapGridSize / 2 + newPoint.y) / snapGridSize) * snapGridSize;
			gmpi::drawing::Size snapDelta{ newPoint.x - snapReference.x, newPoint.y - snapReference.y };

			pointPrev.x += snapDelta.width;
			pointPrev.y += snapDelta.height;

			if (snapDelta.width != 0.0 || snapDelta.height != 0.0)
				parent->Presenter()->DragSelection(snapDelta);

			return gmpi::ReturnCode::Unhandled;
		}
		case 1:
			delta.width = 0;
			break;
		case 2:
			snapReference.x = module->getLayoutRect().right;
			break;
		}

		switch (currentNodeY)
		{
		case 1:
			delta.height = 0;
			break;
		case 2:
			snapReference.y = module->getLayoutRect().bottom;
			break;
		}

		gmpi::drawing::Point newPoint = snapReference;
		newPoint.x += delta.width;
		newPoint.y += delta.height;
		newPoint.x = floorf((snapGridSize / 2 + newPoint.x) / snapGridSize) * snapGridSize;
		newPoint.y = floorf((snapGridSize / 2 + newPoint.y) / snapGridSize) * snapGridSize;
		gmpi::drawing::Size snapDelta{ newPoint.x - snapReference.x, newPoint.y - snapReference.y };

		pointPrev.x += snapDelta.width;
		pointPrev.y += snapDelta.height;

		if (snapDelta.width != 0.0 || snapDelta.height != 0.0)
			parent->Presenter()->ResizeModule(getModuleHandle(), currentNodeX, currentNodeY, snapDelta);

		return gmpi::ReturnCode::Unhandled;
	}

	gmpi::ReturnCode ResizeAdorner::onPointerUp(gmpi::drawing::Point, int32_t)
	{
		if (parent->isCaptured(this))
		{
			parent->releaseCapture();
			parent->autoScrollStop();
		}

		return gmpi::ReturnCode::Unhandled;
	}

	gmpi::ReturnCode ResizeAdorner::onMouseWheel(gmpi::drawing::Point, int32_t, int32_t) { return gmpi::ReturnCode::Unhandled; }
	gmpi::ReturnCode ResizeAdorner::populateContextMenu(gmpi::drawing::Point, gmpi::api::IUnknown*) { return gmpi::ReturnCode::Unhandled; }
	gmpi::ReturnCode ResizeAdorner::onContextMenu(int32_t) { return gmpi::ReturnCode::Unhandled; }
	gmpi::ReturnCode ResizeAdorner::onKeyPress(wchar_t) { return gmpi::ReturnCode::Unhandled; }

	int32_t ResizeAdorner::getModuleHandle()
	{
		return moduleHandle;
	}

	bool ResizeAdorner::getSelected()
	{
		return false;
	}

	void ResizeAdorner::setSelected(bool) {}
	void ResizeAdorner::preDelete() {}

	gmpi::drawing::Point ResizeAdorner::getConnectionPoint(CableType, int)
	{
		return {};
	}

	ResizeAdornerStructure::ResizeAdornerStructure(ViewBase* pParent, ModuleView* pModule)
		: ResizeAdorner(pParent, pModule)
	{
		hasGripper = false;
		drawOutline = false;
	}

	gmpi::drawing::Rect ResizeAdornerStructure::getNodeRect() const
	{
		const Size offsetToModule(module->bounds_.left, module->bounds_.top);
		auto r = offsetRect(module->pluginGraphicsPos, offsetToModule); // outline of module graphics insert.
		return inflateRect(r, 2.5f);
	}

#if 0 // debug
	gmpi::ReturnCode ResizeAdornerStructure::hitTest(gmpi::drawing::Point point, int32_t)
	{
		gmpi::drawing::Rect outerRect(getNodeRect());
		const float outerThickness = ResizeHandleRadius;
		outerRect = inflateRect(outerRect, outerThickness);
		if (!pointInRect(point, outerRect))
			return gmpi::ReturnCode::Unhandled;

		gmpi::drawing::Point pointLocal{ point.x - bounds.left, point.y - bounds.top };

		int hitNodeX, hitNodeY;
		hitTestNodes(pointLocal, hitNodeX, hitNodeY);
		return (hitNodeX >= 0 || hitNodeY >= 0) ? gmpi::ReturnCode::Ok : gmpi::ReturnCode::Unhandled;
	}

	bool ResizeAdornerStructure::hitTestR(int32_t, gmpi::drawing::Rect selectionRect)
	{
		return overlaps(selectionRect, getNodeRect());
	}

	void ResizeAdornerStructure::render(gmpi::drawing::Graphics& g)
	{
		const auto nodes = getNodes();
		if (nodes.empty())
			return;

		gmpi::drawing::Ellipse circle(gmpi::drawing::Point(0., 0.), (float)ResizeHandleRadius, (float)ResizeHandleRadius);
		auto fillBrush = g.createSolidColorBrush(gmpi::drawing::Colors::White);
		auto outlineBrush = g.createSolidColorBrush(color);

		for (auto& n : nodes)
		{
			circle.point = n.location;
			g.fillEllipse(circle, fillBrush);
			g.drawEllipse(circle, outlineBrush);
		}

		{
#if 0 // debug
			const Size offsetToModule(module->bounds_.left - bounds.left, module->bounds_.top - bounds.top);
			auto moduleBoundsLocal = offsetRect(module->bounds_, Size(-bounds.left, -bounds.top));
			auto moduleGfx = offsetRect(module->pluginGraphicsPos, offsetToModule);
			auto boundsLocal = offsetRect(module->bounds_, Size(-bounds.left, -bounds.top));

			g.drawRectangle(moduleBoundsLocal, fillBrush, 1.0f);
			fillBrush.setColor(gmpi::drawing::Colors::Orange);
			g.drawRectangle(moduleGfx, fillBrush, 0.5f);
			fillBrush.setColor(gmpi::drawing::Colors::Red);
			g.drawRectangle(boundsLocal, fillBrush, 0.5f);

			auto r = offsetRect(getNodeRect(), { -bounds.left, -bounds.top });
			fillBrush.setColor(gmpi::drawing::Colors::Red);
			g.drawRectangle(r, fillBrush, 0.5f);
#endif

		}
	}
#endif
}