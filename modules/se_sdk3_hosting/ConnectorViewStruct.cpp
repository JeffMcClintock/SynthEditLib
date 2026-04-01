#include "ConnectorViewStruct.h"
#include "ModuleViewStruct.h"
#include "ContainerView.h"
#include "modules/shared/xplatform_modifier_keys.h"
#include "modules/shared/VectorMath.h"
#include "modules/shared/cardinalSpline.h"
#include "helpers/PixelSnapper.h"

using namespace gmpi::drawing;
using namespace Gmpi::VectorMath;
using namespace gmpi::drawing::utils;

namespace SE2
{
	inline GmpiDrawing::Point toLegacyPoint(const gmpi::drawing::Point& p)
	{
		return { p.x, p.y };
	}

	inline gmpi::drawing::Point toDrawingPoint(const GmpiDrawing::Point& p)
	{
		return { p.x, p.y };
	}

	inline Vector2D vectorFromPoints(const gmpi::drawing::Point& p1, const gmpi::drawing::Point& p2)
	{
		return Vector2D::FromPoints(toLegacyPoint(p1), toLegacyPoint(p2));
	}

	inline gmpi::drawing::Point addVector(const gmpi::drawing::Point& p, const Vector2D& v)
	{
		return { p.x + v.x, p.y + v.y };
	}

	inline gmpi::drawing::Point subVector(const gmpi::drawing::Point& p, const Vector2D& v)
	{
		return { p.x - v.x, p.y - v.y };
	}

	gmpi::drawing::Point ConnectorView2::pointPrev{}; // for dragging nodes

	void ConnectorView2::CalcArrowGeometery(GeometrySink& sink, Point ArrowTip, Vector2D v1)
	{
		auto vn = Perpendicular(v1);
		vn *= arrowWidth * 0.5f;

		Point ArrowBase = addVector(ArrowTip, v1 * arrowLength);
		auto arrowBaseLeft = addVector(ArrowBase, vn);
		auto arrowBaseRight = subVector(ArrowBase, vn);
#if 0
		// draw base line
		if (beginFigure)
		{
			sink.beginFigure(ArrowBase, FigureBegin::Hollow);
		}
		sink.addLine(arrowBaseLeft);
		sink.addLine(ArrowTip);
		sink.addLine(arrowBaseRight);
		sink.addLine(ArrowBase);
#else
		// don't draw base line. V-shape.
		sink.beginFigure(arrowBaseLeft, FigureBegin::Hollow);
		sink.addLine(ArrowTip);
		sink.addLine(arrowBaseRight);
#endif
	}

	// calc 'elbow' when line doubles back to the left.
	inline float calcEndFolds(int draggingFromEnd, const Point& from, const Point& to)
	{
		const float endAdjust = 10.0f;

		if (draggingFromEnd < 0 && (from.x > to.x - endAdjust) && fabs(from.y - to.y) > endAdjust)
		{
			return endAdjust;
		}

		return 0.0f;
	}

	bool isClose(const Point& p1, const Point& p2)
	{
		const float closeDistanceSquared = 0.01f;
		return vectorFromPoints(p1, p2).LengthSquared() < closeDistanceSquared;
	}

	void ConnectorView2::CreateGeometry()
	{
		auto factory = getFactory();

		StrokeStyleProperties strokeStyleProperties;
		strokeStyleProperties.lineCap = draggingFromEnd != -1 ? CapStyle::Round : CapStyle::Flat;
		strokeStyleProperties.lineJoin = LineJoin::Round;
		strokeStyle = factory.createStrokeStyle(strokeStyleProperties);

		geometry = factory.createPathGeometry();
		auto sink = geometry.open();

		//			_RPT4(_CRT_WARN, "Calc [%.3f,%.3f] -> [%.3f,%.3f]\n", from_.x, from_.y, to_.x, to_.y );
		// No curve when dragging.
		// straight line.

		// 'back' going lines need extra curve
		const auto endAdjust = calcEndFolds(draggingFromEnd, from_, to_);

		std::vector<Point> nodesInclusive; // of start and end point.
		nodesInclusive.push_back(from_);

		if (endAdjust > 0.0f)
		{
			auto p = from_;
			p.x += endAdjust;
			nodesInclusive.push_back(p);
		}

		nodesInclusive.insert(std::end(nodesInclusive), std::begin(nodes), std::end(nodes));

		if (endAdjust > 0.0f)
		{
			auto p = to_;
			p.x -= endAdjust;
			nodesInclusive.push_back(p);
		}

		nodesInclusive.push_back(to_);

		// remove overlapping points. (causes failure to draw entire path)
		for (auto it = std::begin(nodesInclusive) + 1; it != std::end(nodesInclusive) - 1; )
		{
			const auto& prev = *(it - 1);
			const auto& p = *it;

			if (isClose(p, prev))
				it = nodesInclusive.erase(it);
			else
				++it;
		}

		if (lineType_ == CURVEY)
		{
			// Transform on-line nodes into Bezier Spline control points.
			std::vector<GmpiDrawing::Point> legacyNodesInclusive;
			legacyNodesInclusive.reserve(nodesInclusive.size());
			for (const auto& p : nodesInclusive)
			{
				legacyNodesInclusive.push_back(toLegacyPoint(p));
			}

			auto legacySplinePoints = cardinalSpline(legacyNodesInclusive);
			std::vector<Point> splinePoints;
			splinePoints.reserve(legacySplinePoints.size());
			for (const auto& p : legacySplinePoints)
			{
				splinePoints.push_back(toDrawingPoint(p));
			}

			sink.beginFigure(splinePoints[0], FigureBegin::Hollow);

			for (int i = 1; i < splinePoints.size(); i += 3)
			{
				sink.addBezier({ splinePoints[i], splinePoints[i + 1], splinePoints[i + 2] });
			}

			if (!drawArrows)//&& drawArrows)
			{
				int splineCount = (static_cast<int>(splinePoints.size()) - 1) / 3;
				int middleSplineIdx = splineCount / 2;
				if (splineCount & 0x01) // odd number
				{
					// Arrow
					constexpr float t = 0.5f;
					constexpr auto oneMinusT = 1.0f - t;
					Point* p = splinePoints.data() + middleSplineIdx * 3;
					{
						// calulate mid-point on cubic bezier curve.
						arrowPoint.x = oneMinusT * oneMinusT * oneMinusT * p[0].x + 3 * oneMinusT * oneMinusT * t * p[1].x + 3 * oneMinusT * t * t * p[2].x + t * t * t * p[3].x;
						arrowPoint.y = oneMinusT * oneMinusT * oneMinusT * p[0].y + 3 * oneMinusT * oneMinusT * t * p[1].y + 3 * oneMinusT * t * t * p[2].y + t * t * t * p[3].y;
					}
					{
						// calulate tangent at mid-point.
						arrowDirection = 3.f * oneMinusT * oneMinusT * vectorFromPoints(p[0], p[1])
							+ 6.f * t * oneMinusT * vectorFromPoints(p[1], p[2])
							+ 3.f * t * t * vectorFromPoints(p[2], p[3]);
					}
				}
				else
				{
					arrowPoint = splinePoints[middleSplineIdx * 3];
					arrowDirection = vectorFromPoints(splinePoints[middleSplineIdx * 3 - 1], splinePoints[middleSplineIdx * 3 + 1]);
				}
				arrowDirection.Normalize();

				const auto arrowCenter = addVector(arrowPoint, 0.5f * arrowLength * arrowDirection);
				sink.endFigure(FigureEnd::Open);
				CalcArrowGeometery(sink, arrowCenter, -arrowDirection);
				sink.endFigure(FigureEnd::Closed);
			}
			else
			{
				sink.endFigure(FigureEnd::Open);
			}
		}
		else
		{
			Vector2D v1 = vectorFromPoints(nodesInclusive.front(), nodesInclusive.back());
			if (v1.LengthSquared() < 0.01f) // fix coincident points.
			{
				v1.x = 0.1f;
			}

			//_RPT1(_CRT_WARN, "v1.Length() %f\n", v1.Length() );
//			constexpr float minDrawLengthSquared = 40.0f * 40.0f;
//			bool drawArrows = v1.LengthSquared() > minDrawLengthSquared && draggingFromEnd < 0;

			v1.Normalize();

			// vector normal.
			auto vn = Perpendicular(v1);
			vn *= arrowWidth * 0.5f;

#if 0
			// left end arrow.
			if (isGuiConnection && drawArrows)
			{
				Point ArrowTip = from_ + v1 * (3 + 0.5f * ModuleViewStruct::plugDiameter);

				CalcArrowGeometery(sink, ArrowTip, v1);
				sink.endFigure(FigureEnd::Open);
				Point lineStart = ArrowTip + v1 * arrowLength * 0.5f;
				sink.beginFigure(lineStart, FigureBegin::Hollow);
			}
			else
#endif
			{
				//				sink.beginFigure(from_, FigureBegin::Hollow);
			}

			bool first = true;
			for (auto& n : nodesInclusive)
			{
				if (first)
				{
					sink.beginFigure(n, FigureBegin::Hollow);
					first = false;
				}
				else
				{
					sink.addLine(n);
				}
			}

			if (!nodes.empty())
			{
				Point from = nodes.back();

				v1 = vectorFromPoints(from, to_);
				if (v1.LengthSquared() < 0.01f) // fix coincident points.
				{
					v1.x = 0.1f;
				}

//				drawArrows = v1.LengthSquared() > minDrawLengthSquared;

				v1.Normalize();

				// vector normal.
				vn = Perpendicular(v1);
				vn *= arrowWidth * 0.5f;
			}
#if 0
			// right end.
			{
				Point ArrowTip = to_ - v1 * (3 + 0.5f * ModuleViewStruct::plugDiameter);
				if (drawArrows)
				{
					Point ArrowBase = ArrowTip - v1 * arrowLength * 0.5f;
					sink.addLine(ArrowBase);
					sink.endFigure(FigureEnd::Open);
					CalcArrowGeometery(sink, ArrowTip, -v1);
				}
				else
				{
					sink.addLine(to_);
				}
			}
			sink.endFigure(FigureEnd::Open);
#else
			//			sink.addLine(to_);
			sink.endFigure(FigureEnd::Open); // complete line

			// center arrow
			if (!drawArrows)
			{
				int splineCount = (static_cast<int>(nodesInclusive.size()) + 1) / 2;
				int middleSplineIdx = splineCount / 2;
				//				if (splineCount & 0x01) // odd number
				{
					//arrowPoint.x = 0.5f * (nodesInclusive[middleSplineIdx].x + nodesInclusive[middleSplineIdx + 1].x);
					//arrowPoint.y = 0.5f * (nodesInclusive[middleSplineIdx].y + nodesInclusive[middleSplineIdx + 1].y);

					const auto segmentVector = vectorFromPoints(nodesInclusive[middleSplineIdx], nodesInclusive[middleSplineIdx + 1]);
					const auto segmentLength = segmentVector.Length();
					const auto distanceToArrowPoint = (std::min)(segmentLength, 0.5f * (segmentLength + arrowLength));
					arrowDirection = vectorFromPoints(nodesInclusive[middleSplineIdx], nodesInclusive[middleSplineIdx + 1]);
					arrowPoint = addVector(nodesInclusive[middleSplineIdx], (distanceToArrowPoint / segmentLength) * arrowDirection);
				}
				/*
				else
				{
				// ugly on node
					arrowPoint = nodesInclusive[middleSplineIdx];
					arrowDirection = Vector2D::FromPoints(nodesInclusive[middleSplineIdx], nodesInclusive[middleSplineIdx + 1]);
				}
				*/
				arrowDirection.Normalize();

				// Add arrow figure.
				CalcArrowGeometery(sink, arrowPoint, -arrowDirection);
				sink.endFigure(FigureEnd::Closed);
			}
#endif
		}

		sink.close();
		segmentGeometrys.clear();
	}

	// as individual segments
	std::vector<gmpi::drawing::PathGeometry>& ConnectorView2::GetSegmentGeometrys()
	{
		if (segmentGeometrys.empty())
		{
			auto factory = getFactory();

			std::vector<Point> nodesInclusive; // of start and end point plus 'elbows'.

			nodesInclusive.push_back(from_);

			// exclude the elbows, else inserting nodes gets confused.
			const auto endAdjust = calcEndFolds(draggingFromEnd, from_, to_);
			const bool hasElbows = endAdjust > 0.0f;
			if (hasElbows)
			{
				auto p = from_;
				p.x += endAdjust;
				nodesInclusive.push_back(p);
			}

			nodesInclusive.insert(std::end(nodesInclusive), std::begin(nodes), std::end(nodes));

			if (hasElbows)
			{
				auto p = to_;
				p.x -= endAdjust;
				nodesInclusive.push_back(p);
			}

			nodesInclusive.push_back(to_);

			if (lineType_ == CURVEY)
			{
				// Transform on-line nodes into Bezier Spline control points.
				std::vector<GmpiDrawing::Point> legacyNodesInclusive;
				legacyNodesInclusive.reserve(nodesInclusive.size());
				for (const auto& p : nodesInclusive)
				{
					legacyNodesInclusive.push_back(toLegacyPoint(p));
				}

				auto legacySplinePoints = cardinalSpline(legacyNodesInclusive);
				std::vector<Point> splinePoints;
				splinePoints.reserve(legacySplinePoints.size());
				for (const auto& p : legacySplinePoints)
				{
					splinePoints.push_back(toDrawingPoint(p));
				}

				const size_t fromIdx = hasElbows ? 3 : 0;
				const size_t toIdx = hasElbows ? splinePoints.size() - 4 : splinePoints.size() - 1;

				for (size_t i = fromIdx; i < toIdx; i += 3)
				{
					auto segment = factory.createPathGeometry();
					auto sink = segment.open();
					sink.beginFigure(splinePoints[i], FigureBegin::Hollow);
					sink.addBezier({ splinePoints[i + 1], splinePoints[i + 2], splinePoints[i + 3] });
					sink.endFigure(FigureEnd::Open);
					sink.close();
					segmentGeometrys.push_back(segment);
				}
			}
			else
			{
				bool first = true;
				PathGeometry segment;
				GeometrySink sink;

				const size_t fromIdx = hasElbows ? 1 : 0;
				const size_t toIdx = hasElbows ? nodesInclusive.size() - 1 : nodesInclusive.size();

				for (size_t i = fromIdx; i < toIdx; ++i)
				{
					if (!first)
					{
						sink.addLine(nodesInclusive[i]);

//						if (hasElbows && (i == 1 || i == nodesInclusive.size() - 2))
//							continue;
						
						sink.endFigure(FigureEnd::Open);
						sink.close();
						segmentGeometrys.push_back(segment);
					}
					first = false;

					if (i == toIdx - 1) // last.
						continue;

					segment = factory.createPathGeometry();
					sink = segment.open();
					sink.beginFigure(nodesInclusive[i], FigureBegin::Hollow);
				}
			}
		}
		return segmentGeometrys;
	}

	void ConnectorView2::CalcBounds()
	{
		CreateGeometry();

		auto oldBounds = bounds_;

		float expand = getSelected() ? (float)NodeRadius * 2 + 1 : (float)cableDiameter;

		bounds_ = geometry.getWidenedBounds(expand, strokeStyle);

		if (oldBounds != bounds_)
		{
			oldBounds = unionRect(oldBounds, bounds_);
			parent->ChildInvalidateRect(oldBounds);
//			parent->invalidateRect(&oldBounds);
		}
	}

	GraphicsResourceCache<sharedGraphicResources_connectors> drawingResourcesCache;

	sharedGraphicResources_connectors* ConnectorView2::getDrawingResources(gmpi::drawing::Graphics& g)
	{
		if (!drawingResources)
		{
			drawingResources = drawingResourcesCache.get(g);
		}

		return drawingResources.get();
	}

	void ConnectorView2::render(gmpi::drawing::Graphics& g)
	{
		if (!geometry)
			return;

		auto resources = getDrawingResources(g);
		float width = 2.0f;
		SolidColorBrush* brush3 = {};
#if defined( _DEBUG )
		auto pinkBrush = g.createSolidColorBrush(Colors::Pink);
		if (cancellation != 0.0f)
		{
			brush3 = &pinkBrush;
			width = 9.0f * cancellation;
		}
		else
#endif 
		{
			if (draggingFromEnd < 0)
			{
				if (highlightFlags != 0)
				{
					if ((highlightFlags & 3) != 0) // error
						brush3 = &resources->errorBrush;
					else
						brush3 = &resources->emphasiseBrush;// Emphasise
				}
				else
				{
					brush3 = &resources->brushes[datatype];
				}
			}
			else
			{
				// highlighted (dragging).
				brush3 = &resources->draggingBrush;

				if(endIsSnapped)
					g.fillCircle(draggingFromEnd == 0 ? from_ : to_, 2.0f, *brush3);
			}
		}

		if (getSelected())
			brush3 = &resources->selectedBrush;

		if (getSelected() || mouseHover)
			width = 3.f;

		// calc line thickness and offset to align nicely on pixel. Don't need to snap other end, it's either the same Y, or a diagonal line - which don't need snapping anyhow.
		pixelSnapper2 snap(g.getTransform(), parent->drawingHost->getRasterizationScale());

		const auto lineSpec = snap.thickness(width);
		const auto snappedStrokeWidth = lineSpec.width;

		// snap the line from point to the pixel-grid.
		const auto Ysnapped = snap.snapY(from_.y);
		const auto offset = from_.y - Ysnapped + lineSpec.center_offset;

		const auto orig = g.getTransform();
		g.setTransform(makeTranslation(0.0f, offset) * orig);

		assert(brush3);
		g.drawGeometry(geometry, *brush3, snappedStrokeWidth, strokeStyle);

		if (getSelected() && hoverSegment != -1)
		{
			auto segments = GetSegmentGeometrys();
			g.drawGeometry(segments[hoverSegment], *brush3, snappedStrokeWidth + 1);
		}

		// Nodes
		if (getSelected())
		{
			auto outlineBrush = g.createSolidColorBrush(Colors::DodgerBlue);
			auto fillBrush = g.createSolidColorBrush(Colors::White);

			for (auto& n : nodes)
			{
				gmpi::drawing::Ellipse circle(n, static_cast<float>(NodeRadius));
				g.fillEllipse(circle, fillBrush);
				g.drawEllipse(circle, outlineBrush);
			}
		}
#ifdef _DEBUG
		//		g.DrawCircle(arrowPoint, static_cast<float>(NodeRadius), g.createSolidColorBrush(Colors::DodgerBlue));
		//		g.DrawLine(arrowPoint - arrowDirection * 10.0f, arrowPoint + arrowDirection * 10.0f, g.createSolidColorBrush(Colors::Black));
#endif
		g.setTransform(orig);
	}

	gmpi::ReturnCode ConnectorView2::onPointerMove(gmpi::drawing::Point point, int32_t flags)
	{
		// dragging something.
		if (imCaptured())
		{
			// dragging a node.
			if (draggingNode != -1)
			{
				const auto snapGridSize = Presenter()->GetSnapSize();
				gmpi::drawing::Size delta(point.x - pointPrev.x, point.y - pointPrev.y);
				if (delta.width != 0.0f || delta.height != 0.0f) // avoid false snap on selection
				{
					const float halfGrid = snapGridSize * 0.5f;

					gmpi::drawing::Point snapReference = nodes[draggingNode];

					// nodes snap to center of grid, not lines of grid like modules do
					gmpi::drawing::Point newPoint{ snapReference.x + delta.width, snapReference.y + delta.height };
					newPoint.x = halfGrid + floorf((newPoint.x) / snapGridSize) * snapGridSize;
					newPoint.y = halfGrid + floorf((newPoint.y) / snapGridSize) * snapGridSize;
					gmpi::drawing::Size snapDelta{ newPoint.x - snapReference.x, newPoint.y - snapReference.y };

					pointPrev.x += snapDelta.width;
					pointPrev.y += snapDelta.height;

					if (snapDelta.width != 0.0 || snapDelta.height != 0.0)
					{
						Presenter()->DragNode(getModuleHandle(), draggingNode, pointPrev);
						nodes[draggingNode] = pointPrev;
						CalcBounds();

						parent->ChildInvalidateRect(bounds_);
					}
				}

				return gmpi::ReturnCode::Unhandled;
			}

			// dragging new line
			ConnectorViewBase::onPointerMove(point, flags);
			return gmpi::ReturnCode::Unhandled;
		}

		return gmpi::ReturnCode::Unhandled;
	}

	gmpi::ReturnCode ConnectorView2::onPointerUp(gmpi::drawing::Point point, int32_t flags)
	{
		if (imCaptured() && draggingNode == -1)
		{
			ConnectorViewBase::onPointerUp(point, flags);
			return gmpi::ReturnCode::Unhandled;
		}

		parent->releaseCapture();
		return gmpi::ReturnCode::Unhandled;
	}

	gmpi::ReturnCode ConnectorView2::hitTest(gmpi::drawing::Point point, int32_t flags)
	{
		hoverNode = -1;
		hoverSegment = -1;

		if (!pointInRect(point, getClipArea()) || !geometry)
			return gmpi::ReturnCode::Unhandled;


		if (!geometry.strokeContainsPoint(point, 3.0f))
			return gmpi::ReturnCode::Unhandled;

		// when highlighted, line moves in front of pin, so ignore hits near to the end.
		if(highlightFlags != 0)
		{
			const float endIgnoreDistanceSquared = 10.0f * 10.0f;
			if (   vectorFromPoints(point, from_).LengthSquared() < endIgnoreDistanceSquared
				|| vectorFromPoints(point, to_  ).LengthSquared() < endIgnoreDistanceSquared)
				return gmpi::ReturnCode::Unhandled;
		}

		// hit test individual node points.
		int i = 0;
		for (auto& n : nodes)
		{
			float dx = n.x - point.x;
			float dy = n.y - point.y;

			if ((dx * dx + dy * dy) <= (float)((1 + NodeRadius) * (1 + NodeRadius)))
			{
				hoverNode = i;
				break;
			}
			++i;
		}

		// hit test individual segments.
		if (hoverNode == -1)
		{
			hoverSegment = -1;

			auto segments = GetSegmentGeometrys();
			int j = 0;
			for (auto& s : segments)
			{
				if (s.strokeContainsPoint(point, hitTestWidth))
				{
					hoverSegment = j;
					break;
				}
				++j;
			}

			if (hoverSegment == -1)
			{
				// handle mouse over elbows and minor hit test glitches by defaulting to nearest end.
				auto d1 = vectorFromPoints(from_, point).LengthSquared();
				auto d2 = vectorFromPoints(to_, point).LengthSquared();
				if (d1 < d2)
				{
					hoverSegment = 0;
				}
				else
				{
					hoverSegment = static_cast<int>(segments.size()) - 1;
				}
			}
		}

		return gmpi::ReturnCode::Ok;
	}

	bool ConnectorView2::hitTestR(int32_t flags, gmpi::drawing::Rect selectionRect)
	{
		if (!overlaps(getClipArea(), selectionRect) || !geometry)
		{
			return false;
		}
		return true;
	}

	gmpi::ReturnCode ConnectorView2::setHover(bool mouseIsOverMe)
	{
		mouseHover = mouseIsOverMe;

		if (!mouseHover)
		{
			hoverNode = hoverSegment = -1;
		}

		const auto redrawRect = getClipArea();
//		parent->invalidateRect(&redrawRect);
		parent->ChildInvalidateRect(redrawRect);

		return gmpi::ReturnCode::Ok;
	}

	void ConnectorView2::OnNodesMoved(std::vector<gmpi::drawing::Point>& newNodes)
	{
		nodes = newNodes;
		parent->markDirtyChild(this);
		parent->invalidateRect();
	}

	// TODO: !!! hit-testing lines should be 'fuzzy' and return the closest line when more than 1 is hittable (same as plugs).
	// This allows a bigger hit radius without losing accuracy. maybe hit tests return a 'confidence (0.0 - 1.0).
	gmpi::ReturnCode ConnectorView2::onPointerDown(gmpi::drawing::Point point, int32_t flags)
	{
//		_RPT0(_CRT_WARN, "ConnectorView2::onPointerDown\n");

		if (imCaptured()) // then we are *already* draging.
		{
			parent->autoScrollStop();
			parent->releaseCapture();
			parent->EndCableDrag(point, this, flags);
			// I am now DELETED!!!
			return gmpi::ReturnCode::Unhandled;
		}
		else
		{
			const bool wasSelected = getSelected();

			// Change selection, depending on shift etc
			Presenter()->ObjectClicked(handle, flags);// gmpi::modifier_keys::getHeldKeys());

			if ((flags & gmpi_gui_api::GG_POINTER_FLAG_FIRSTBUTTON) != 0)
			{
				// if we were not selected, or still are not - don't interact with nodes
				// if shift or ctlr held - indicates a possible 'is_selected' change, not node editing.
				if (!wasSelected || !getSelected() || (flags & (gmpi_gui_api::GG_POINTER_KEY_CONTROL | gmpi_gui_api::GG_POINTER_KEY_SHIFT)) != 0)
					return gmpi::ReturnCode::Handled; // handled means don't clear selection.

				// Clicked a node?
				if (hoverNode >= 0)
				{
					draggingNode = hoverNode;
					pointPrev = point;
					parent->setCapture(this);
					return gmpi::ReturnCode::Unhandled;
				}
				else
				{
					// When already selected, clicks add new nodes.
					assert(hoverSegment >= 0); // shouldn't get mouse-down without previously calling hit-test

					Presenter()->InsertNode(handle, hoverSegment + 1, point);

					nodes.insert(nodes.begin() + hoverSegment, point);

					draggingNode = hoverSegment;
					parent->setCapture(this);
					CalcBounds();
					parent->ChildInvalidateRect(bounds_); // sometimes bounds don't change, but still need to draw new node.

					hitTest(point, flags); // re-hit-test to get new hoverNode.

#if 0 // clicking end, not using yet

					int hitEnd = -1;
					// Is hit at line end?
					gmpi::drawing::Size delta = from_ - Point(point);
					float lengthSquared = delta.width * delta.width + delta.height * delta.height;
					float hitRadiusSquared = 100;
					if (lengthSquared < hitRadiusSquared)
					{
						hitEnd = 0;
					}
					else
					{
						delta = to_ - Point(point);
						lengthSquared = delta.width * delta.width + delta.height * delta.height;
						if (lengthSquared < hitRadiusSquared)
						{
							hitEnd = 1;
						}
					}

					// Select Object.
					Presenter()->ObjectClicked(handle, gmpi::modifier_keys::getHeldKeys());

					if (hitEnd == -1)
						return gmpi::MP_OK; // normal hit.
					// TODO pickup from end, mayby when <ALT> held.
#endif
				}
				return gmpi::ReturnCode::Unhandled;
			}
		}

		return gmpi::ReturnCode::Unhandled;
	}

	void ConnectorView2::measure(gmpi::drawing::Size availableSize, gmpi::drawing::Size* returnDesiredSize)
	{
		// Measure/Arrange not really applicable to lines.
		returnDesiredSize->height = 10;
		returnDesiredSize->width = 10;

		auto module1 = dynamic_cast<ModuleViewStruct*>(Presenter()->HandleToObject(fmPin.module));
		if (module1)
		{
			datatype = static_cast<char>(module1->getPinDatatype(fmPin.index));
			drawArrows = module1->getPinGuiType(fmPin.index) && !module1->isMonoDirectional();
		}

	}
} // namespace

