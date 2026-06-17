#include "ConnectorViewStruct.h"
#include "ModuleViewStruct.h"
#include "ContainerView.h"
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

	// Rounded U-turn replacing the sharp elbow of a doubled-back line. The path leaves 'pin'
	// horizontally (exitDirX: +1 rightward, -1 leftward) for 'stub' DIPs to clear the module
	// edge, then follows a circle of radius r until it meets the tangent line through
	// 'target', so the straight run leaves the curve smoothly. valid=false when 'target' is
	// too close to fit the turn (caller falls back to the elbow).
	EndArcInfo calcEndArc(const Point pin, const float exitDirX, const Point target, const float stub, const float r)
	{
		EndArcInfo res;
		res.S = { pin.x + exitDirX * stub, pin.y };

		// The circle centre sits on the side of the exit line that the wire must turn toward.
		const float signDown = (target.y >= res.S.y) ? 1.0f : -1.0f;
		const Point centre{ res.S.x, res.S.y + signDown * r };

		const float dx = target.x - centre.x;
		const float dy = target.y - centre.y;
		const float distSquared = dx * dx + dy * dy;
		if (distSquared <= r * r * 1.1f) // target inside (or grazing) the circle: no room to turn.
			return res;

		const float dist = sqrtf(distSquared);
		const float alpha = atan2f(dy, dx);
		const float beta = acosf((std::min)(1.0f, r / dist)); // angle at centre between 'target' and either tangent point

		// Which way round the circle the wire travels. (y-down coordinates: increasing
		// parametric angle (cos, sin) tracks clockwise on screen.)
		const bool clockwise = signDown * exitDirX > 0.0f;

		const float phiS = atan2f(res.S.y - centre.y, res.S.x - centre.x);

		// Two candidate tangent points; the wire leaves at the one where the circular motion
		// continues toward 'target' rather than away from it.
		for (const float phiT : { alpha + beta, alpha - beta })
		{
			const Point tangentPoint{ centre.x + r * cosf(phiT), centre.y + r * sinf(phiT) };

			const float velX = clockwise ? -sinf(phiT) : sinf(phiT);
			const float velY = clockwise ? cosf(phiT) : -cosf(phiT);
			if (velX * (target.x - tangentPoint.x) + velY * (target.y - tangentPoint.y) <= 0.0f)
				continue;

			constexpr float twoPi = 2.0f * 3.14159265f;
			float sweep = clockwise ? phiT - phiS : phiS - phiT;
			while (sweep < 0.0f)
				sweep += twoPi;

			res.T = tangentPoint;
			res.clockwise = clockwise;
			res.largeArc = sweep > 0.5f * twoPi;
			res.valid = true;
			break;
		}

		return res;
	}

	// 'reversed' draws the arc from T back to S (used at the 'to' end, where the arc was
	// calculated outward from the pin but is drawn toward it), which flips the sweep.
	ArcSegment makeArcSegment(const EndArcInfo& arc, float radius, bool reversed)
	{
		return {
			reversed ? arc.S : arc.T,
			{ radius, radius },
			0.0f,
			(arc.clockwise != reversed) ? SweepDirection::Clockwise : SweepDirection::CounterClockwise,
			arc.largeArc ? ArcSize::Large : ArcSize::Small };
	}

	std::vector<Point> ConnectorView2::curveyUTurnSeed(float endAdjust) const
	{
		std::vector<Point> seed;

		if (fromArc.valid)
		{
			seed.push_back(fromArc.T);
		}
		else
		{
			seed.push_back(from_);
			seed.push_back({ from_.x + endAdjust, from_.y });
		}

		seed.insert(seed.end(), nodes.begin(), nodes.end());

		if (toArc.valid)
		{
			seed.push_back(toArc.T);
		}
		else
		{
			seed.push_back({ to_.x - endAdjust, to_.y });
			seed.push_back(to_);
		}

		// remove overlapping points. (causes failure to draw entire path)
		for (auto it = seed.begin() + 1; it != seed.end() - 1; )
		{
			if (isClose(*it, *(it - 1)))
				it = seed.erase(it);
			else
				++it;
		}

		return seed;
	}

	void ConnectorView2::CreateGeometry()
	{
		auto factory = getFactory();

		StrokeStyleProperties strokeStyleProperties;
		strokeStyleProperties.lineCap = CapStyle::Round; // draggingFromEnd != -1 ? CapStyle::Round : CapStyle::Flat;
		strokeStyleProperties.lineJoin = LineJoin::Round;
		strokeStyle = factory.createStrokeStyle(strokeStyleProperties);

		geometry = factory.createPathGeometry();
		auto sink = geometry.open();

		//			_RPT4(_CRT_WARN, "Calc [%.3f,%.3f] -> [%.3f,%.3f]\n", from_.x, from_.y, to_.x, to_.y );
		// No curve when dragging.
		// straight line.

		// 'back' going lines need extra curve
		const auto endAdjust = calcEndFolds(draggingFromEnd, from_, to_);

		fromArc = {}; // recalculated below (doubled-back lines only)
		toArc = {};

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

		// Where the line doubles back, the sharp elbows are replaced with rounded U-turns
		// (arc segments) that sweep the line clear of the module instead of kinking.
		// Shared by both line styles; CURVEY splines only the run between the arcs.
		constexpr float stub = 2.0f;
		constexpr float uTurnRadius = 8.0f; // stub + radius ~= endAdjust: same clearance as the elbows

		const Point elbowFrom{ from_.x + endAdjust, from_.y };
		const Point elbowTo{ to_.x - endAdjust, to_.y };

		std::vector<Point> interior; // user nodes, overlapping points removed
		if (endAdjust > 0.0f)
		{
			for (const auto& n : nodes)
			{
				if (interior.empty() || !isClose(interior.back(), n))
					interior.push_back(n);
			}

			fromArc = calcEndArc(from_, 1.0f, interior.empty() ? elbowTo : interior.front(), stub, uTurnRadius);
			toArc = calcEndArc(to_, -1.0f, interior.empty() ? (fromArc.valid ? fromArc.T : elbowFrom) : interior.back(), stub, uTurnRadius);
			if (interior.empty() && toArc.valid) // re-aim the from arc at the to arc, aligning the straight run with both curves.
				fromArc = calcEndArc(from_, 1.0f, toArc.T, stub, uTurnRadius);
		}

		if (lineType_ == CURVEY)
		{
			// Points the spline passes through. When U-turn arcs are active, the spline
			// covers only the run between the arc tangent points; the stubs and arcs are
			// drawn as exact segments, the same look as straight mode at the pins.
			const auto splineSeed = endAdjust > 0.0f ? curveyUTurnSeed(endAdjust) : nodesInclusive;

			// Transform on-line nodes into Bezier Spline control points.
			std::vector<GmpiDrawing::Point> legacyNodesInclusive;
			legacyNodesInclusive.reserve(splineSeed.size());
			for (const auto& p : splineSeed)
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

			if (fromArc.valid)
			{
				// horizontal stub out of the pin, then the U-turn arc onto the spline's first point.
				sink.beginFigure(from_, FigureBegin::Hollow);
				sink.addLine(fromArc.S);
				sink.addArc(makeArcSegment(fromArc, uTurnRadius, false));
			}
			else
			{
				sink.beginFigure(splinePoints[0], FigureBegin::Hollow);
			}

			for (int i = 1; i < splinePoints.size(); i += 3)
			{
				sink.addBezier({ splinePoints[i], splinePoints[i + 1], splinePoints[i + 2] });
			}

			if (toArc.valid)
			{
				// U-turn arc off the spline's last point, then the horizontal stub into the pin.
				sink.addArc(makeArcSegment(toArc, uTurnRadius, true));
				sink.addLine(to_);
			}

			if (!drawArrows)
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

			std::vector<Point> drawnPath; // the rendered points; differs from nodesInclusive when U-turn arcs replace the elbows

			if (endAdjust > 0.0f)
			{
				sink.beginFigure(from_, FigureBegin::Hollow);
				drawnPath.push_back(from_);

				if (fromArc.valid)
				{
					sink.addLine(fromArc.S);
					sink.addArc(makeArcSegment(fromArc, uTurnRadius, false));
					drawnPath.push_back(fromArc.T);
				}
				else // no room for the arc: keep the sharp elbow.
				{
					sink.addLine(elbowFrom);
					drawnPath.push_back(elbowFrom);
				}

				for (const auto& n : interior)
				{
					sink.addLine(n);
					drawnPath.push_back(n);
				}

				if (toArc.valid)
				{
					sink.addLine(toArc.T);
					sink.addArc(makeArcSegment(toArc, uTurnRadius, true));
					drawnPath.push_back(toArc.T);
				}
				else
				{
					sink.addLine(elbowTo);
					drawnPath.push_back(elbowTo);
				}

				sink.addLine(to_);
				drawnPath.push_back(to_);
			}
			else
			{
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
				// when U-turn arcs replaced the elbows, place the arrow on the path actually drawn.
				const auto& arrowPath = drawnPath.empty() ? nodesInclusive : drawnPath;

				int splineCount = (static_cast<int>(arrowPath.size()) + 1) / 2;
				int middleSplineIdx = splineCount / 2;
				//				if (splineCount & 0x01) // odd number
				{
					const auto segmentVector = vectorFromPoints(arrowPath[middleSplineIdx], arrowPath[middleSplineIdx + 1]);
					const auto segmentLength = segmentVector.Length();
					const auto distanceToArrowPoint = (std::min)(segmentLength, 0.5f * (segmentLength + arrowLength));
					arrowDirection = vectorFromPoints(arrowPath[middleSplineIdx], arrowPath[middleSplineIdx + 1]);
					arrowPoint = addVector(arrowPath[middleSplineIdx], (distanceToArrowPoint / segmentLength) * arrowDirection);
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
				// must match the spline CreateGeometry drew: arc tangent points bracket the
				// nodes where U-turn arcs are active.
				const auto splineSeed = hasElbows ? curveyUTurnSeed(endAdjust) : nodesInclusive;

				// Transform on-line nodes into Bezier Spline control points.
				std::vector<GmpiDrawing::Point> legacyNodesInclusive;
				legacyNodesInclusive.reserve(splineSeed.size());
				for (const auto& p : splineSeed)
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

				// skip a lead-in/lead-out bezier only where a sharp elbow remains; with an
				// arc the spline starts/ends at the arc tangent point and every piece is a
				// real segment.
				const size_t fromIdx = (hasElbows && !fromArc.valid) ? 3 : 0;
				const size_t toIdx = (hasElbows && !toArc.valid) ? splinePoints.size() - 4 : splinePoints.size() - 1;

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
				// match the drawn U-turn arcs: hover/hit segments anchor at the arc ends,
				// not at the old sharp elbow points.
				if (hasElbows)
				{
					if (fromArc.valid)
						nodesInclusive[1] = fromArc.T;
					if (toArc.valid)
						nodesInclusive[nodesInclusive.size() - 2] = toArc.T;
				}

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
		const auto oldBounds = bounds_;
		const auto oldClipRect = getClipArea();

		CreateGeometry();

		bounds_ = geometry.getWidenedBounds((float)cableDiameter, strokeStyle);

		// Node circles are drawn outside the path geometry, so the path-widened bounds
		// don't cover them. Union in each node's visual extent (radius + outline + AA).
		if (getSelected())
		{
			const float nodeOutset = (float)NodeRadius + 2.0f;
			for (const auto& n : nodes)
			{
				bounds_ = unionRect(bounds_, gmpi::drawing::Rect{
					n.x - nodeOutset, n.y - nodeOutset,
					n.x + nodeOutset, n.y + nodeOutset });
			}
		}


		if(oldBounds != bounds_)
		{
			if(isNull(oldClipRect))
			{
				parent->ChildInvalidateRect(getClipArea());
			}
			else
			{
				const auto r = inflateRect(unionRect(oldClipRect, getClipArea()), 0.5f);
				parent->ChildInvalidateRect(r);
			}
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

		const bool snap_the_y = from_.y == to_.y;

		Matrix3x2 orig;
		auto snappedStrokeWidth = width;
		if(snap_the_y)
		{
			// calc line thickness and offset to align nicely on pixel. Don't need to snap other end, it's either the same Y, or a diagonal line - which don't need snapping anyhow.
			pixelSnapper2 snap(g.getTransform(), parent->drawingHost->getRasterizationScale());

			const auto lineSpec = snap.thickness(width);
			snappedStrokeWidth = lineSpec.width;

			// snap the line from point to the pixel-grid.
			const auto Ysnapped = snap.snapY(from_.y);
			const auto offset = from_.y - Ysnapped + lineSpec.center_offset;

			orig = g.getTransform();
			g.setTransform(makeTranslation(0.0f, offset) * orig);
		}

		assert(brush3);
		g.drawGeometry(geometry, *brush3, snappedStrokeWidth, strokeStyle);

		if (getSelected() && mouseHover && hoverSegment != -1)
		{
			auto& segments = GetSegmentGeometrys();
			g.drawGeometry(segments[hoverSegment], *brush3, snappedStrokeWidth + 1);
		}

		// Nodes
		if (getSelected())
		{
			auto outlineBrush = g.createSolidColorBrush(Colors::DodgerBlue);
			auto fillBrush = g.createSolidColorBrush(Colors::White);

			for (auto& n : nodes)
			{
				gmpi::drawing::Ellipse circle{n, static_cast<float>(NodeRadius), static_cast<float>(NodeRadius)};
				g.fillEllipse(circle, fillBrush);
				g.drawEllipse(circle, outlineBrush);
			}
		}
#ifdef _DEBUG
		//		g.DrawCircle(arrowPoint, static_cast<float>(NodeRadius), g.createSolidColorBrush(Colors::DodgerBlue));
		//		g.DrawLine(arrowPoint - arrowDirection * 10.0f, arrowPoint + arrowDirection * 10.0f, g.createSolidColorBrush(Colors::Black));
#endif
		if(snap_the_y)
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

						invalidate();
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

		parent->autoScrollStop();
		parent->releaseCapture();
		return gmpi::ReturnCode::Unhandled;
	}

	// returns { distance, hoverNode, hoverSegment }
	//   distance: 0 = solid hit, 0..fuzzyHitTestLimit = fuzzy hit, totalMiss otherwise.
	std::tuple<float, int, int> ConnectorView2::hitTestWhat(gmpi::drawing::Point point)
	{
		constexpr float totalMiss = 1000.0f;

		if (!geometry)
			return { totalMiss, -1, -1 };

		// Cheap reject: outside fuzzy bounds.
		if (!pointInRect(point, inflateRect(bounds_, fuzzyHitTestLimit)))
			return { totalMiss, -1, -1 };

		// Cheap reject by max-fuzzy stroke width before iterating geometry repeatedly.
		// strokeContainsPoint(p, w) is true iff distance(p, line) <= w/2,
		// so testing at hitTestWidth + 2*fuzzyHitTestLimit covers any point within fuzzyHitTestLimit of the solid-hit area.
		const float maxFuzzyStrokeWidth = hitTestWidth + 2.0f * fuzzyHitTestLimit;
		if (!geometry.strokeContainsPoint(point, maxFuzzyStrokeWidth))
			return { totalMiss, -1, -1 };

		// Ignore hits near to the end that mess with clickin plugs. Unless it's a direct hit on a node.
		constexpr float endIgnoreDistanceSquared = 18.0f * 18.0f;
		bool mouseNearEndZone = vectorFromPoints(point, from_).LengthSquared() < endIgnoreDistanceSquared
			|| vectorFromPoints(point, to_).LengthSquared() < endIgnoreDistanceSquared;

		// Distance to closest node (clamped to 0 for solid hit).
		constexpr float nodeHitRadius = 1.0f + NodeRadius;
		constexpr float nodeHitRadiusSquared = nodeHitRadius * nodeHitRadius;
		int closestNode = -1;
		float closestNodeDistanceSquared = mouseNearEndZone ? nodeHitRadiusSquared : totalMiss; // near line end we become very strict, to avoid missing clicks on pins.
		for(int i = 0; i < (int)nodes.size(); ++i)
		{
			const float dx = nodes[i].x - point.x;
			const float dy = nodes[i].y - point.y;
			const float distanceSquared = dx * dx + dy * dy;
			if(distanceSquared < closestNodeDistanceSquared)
			{
				closestNodeDistanceSquared = distanceSquared;
				closestNode = i;
			}
		}

		float closestNodeDistance = mouseNearEndZone ? nodeHitRadius : totalMiss;
		if(closestNode != -1)
		{
			closestNodeDistance = closestNodeDistanceSquared <= nodeHitRadiusSquared
				? 0.0f
				: sqrtf(closestNodeDistanceSquared) - nodeHitRadius;
		}

		if(mouseNearEndZone && closestNode == -1)
			return { totalMiss, -1, -1 };

		if (closestNodeDistance < 0.0f) closestNodeDistance = 0.0f;
		if (closestNodeDistance > fuzzyHitTestLimit) closestNode = -1;

		// Distance to closest line segment, plus the stroke width to use when identifying which segment.
		float closestLineDistance;
		float lineMatchStrokeWidth;
		if (geometry.strokeContainsPoint(point, hitTestWidth))
		{
			closestLineDistance = 0.0f;
			lineMatchStrokeWidth = hitTestWidth;
		}
		else
		{
			// Binary search for minimum stroke width that contains point.
			// Invariant: strokeContainsPoint(point, lo)==false, strokeContainsPoint(point, hi)==true.
			// fuzzy_distance = (hi - hitTestWidth) / 2, so 2 DIPs of width-precision yields ~1 DIP of distance-precision.
			float lo = hitTestWidth;
			float hi = maxFuzzyStrokeWidth;
			while (hi - lo > 2.0f)
			{
				const float mid = (lo + hi) * 0.5f;
				if (geometry.strokeContainsPoint(point, mid))
					hi = mid;
				else
					lo = mid;
			}
			closestLineDistance = (hi - hitTestWidth) * 0.5f;
			lineMatchStrokeWidth = hi;
		}

		// Pick winner: closer wins, node breaks ties.
		if (closestNode != -1 && closestNodeDistance <= closestLineDistance)
		{
			return { closestNodeDistance, closestNode, -1 };
		}

		// Identify which segment is closest using the stroke width that just barely contains the point.
		int hitSegment = -1;
		auto& segments = GetSegmentGeometrys();
		for (int j = 0; j < (int)segments.size(); ++j)
		{
			if (segments[j].strokeContainsPoint(point, lineMatchStrokeWidth))
			{
				hitSegment = j;
				break;
			}
		}

		if (hitSegment == -1 && !segments.empty())
		{
			// handle mouse over elbows and minor hit test glitches by defaulting to nearest end.
			const auto d1 = vectorFromPoints(from_, point).LengthSquared();
			const auto d2 = vectorFromPoints(to_, point).LengthSquared();
			hitSegment = (d1 < d2) ? 0 : static_cast<int>(segments.size()) - 1;
		}

		return { closestLineDistance, -1, hitSegment };
	}

	gmpi::ReturnCode ConnectorView2::hitTest(gmpi::drawing::Point point, int32_t flags)
	{
		auto [distance, hn, hs] = hitTestWhat(point);
		hoverNode = hn;
		hoverSegment = hs;
		return distance <= 0.0f ? gmpi::ReturnCode::Ok : gmpi::ReturnCode::Unhandled;
	}

	float ConnectorView2::hitTestFuzzy(int32_t flags, gmpi::drawing::Point point)
	{
		auto [distance, hn, hs] = hitTestWhat(point);
		hoverNode = hn;
		hoverSegment = hs;
		return distance;
	}

	bool ConnectorView2::hitTestR(int32_t flags, gmpi::drawing::Rect selectionRect)
	{
		if (!overlaps(getClipArea(), selectionRect) || !geometry)
			return false;

		// Sample points in a grid within the rectangle to approximate path intersection.
		// Use stroke width >= grid spacing to ensure no line slips between samples.
		// Shrink rect by half spacing to avoid selecting lines just outside the rectangle.
		constexpr float spacing = 10.0f;
		constexpr float minSpacing = 0.1f; // prevent infinite loop on zero-size rect

		const float rectWidth = selectionRect.right - selectionRect.left;
		const float rectHeight = selectionRect.bottom - selectionRect.top;
		const float xSpacing = (std::max)(minSpacing, (std::min)(spacing, rectWidth));
		const float ySpacing = (std::max)(minSpacing, (std::min)(spacing, rectHeight));

		for (float y = selectionRect.top + ySpacing * 0.5f; y <= selectionRect.bottom - ySpacing * 0.5f; y += ySpacing)
		{
			for (float x = selectionRect.left + xSpacing * 0.5f; x <= selectionRect.right - xSpacing * 0.5f; x += xSpacing)
			{
				if (geometry.strokeContainsPoint({x, y}, spacing))
					return true;
			}
		}

		return false;
	}

	gmpi::ReturnCode ConnectorView2::setHover(bool mouseIsOverMe)
	{
		mouseHover = mouseIsOverMe;

		if (!mouseHover)
			hoverNode = hoverSegment = -1;

		invalidate();

		return gmpi::ReturnCode::Ok;
	}

	void ConnectorView2::OnNodesMoved(std::vector<gmpi::drawing::Point>& newNodes)
	{
		invalidate();

		nodes = newNodes;
		CalcBounds(); // rebuild geometry/segment cache so hitTest stays in sync with nodes
		parent->markDirtyChild(this);

		invalidate();
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
					parent->autoScrollStart();
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
					parent->autoScrollStart();
					CalcBounds();
					invalidate();

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

