#pragma once
/*
#include "Gfx_base.h"
*/

#include "./gmpi_gui_hosting.h"
#define _USE_MATH_DEFINES
#include <math.h>
#include "./Bezier.h"

#pragma warning(disable : 4100)

using namespace se_sdk;

namespace gmpi
{
	namespace generic_graphics
	{
		class Resource
		{
		protected:
			GmpiDrawing_API::IMpFactory* factory;

		public:
			Resource(GmpiDrawing_API::IMpFactory* pfactory) :factory(pfactory)
			{}
		};

		class StrokeStyle : public Resource, public GmpiDrawing_API::IMpStrokeStyle
		{
		public:
			GmpiDrawing_API::MP1_STROKE_STYLE_PROPERTIES strokeStyleProperties;
			std::vector<float> dashes;

			StrokeStyle(GmpiDrawing_API::IMpFactory* pfactory, const GmpiDrawing_API::MP1_STROKE_STYLE_PROPERTIES* pstrokeStyleProperties, float* pdashes, int32_t dashesCount) :
				Resource(pfactory)
				, strokeStyleProperties(*pstrokeStyleProperties)
			{
				for (int i = 0; i < dashesCount; ++i)
				{
					dashes.push_back(pdashes[i]);
				}
			}

			// GmpiDrawing_API::IMpStrokeStyle
			GmpiDrawing_API::MP1_CAP_STYLE MP_STDCALL GetStartCap() override
			{
				return strokeStyleProperties.startCap;
			}

			GmpiDrawing_API::MP1_CAP_STYLE MP_STDCALL GetEndCap() override
			{
				return strokeStyleProperties.endCap;
			}

			GmpiDrawing_API::MP1_CAP_STYLE MP_STDCALL GetDashCap() override
			{
				return strokeStyleProperties.dashCap;
			}

			float MP_STDCALL GetMiterLimit() override
			{
				return strokeStyleProperties.miterLimit;
			}

			GmpiDrawing_API::MP1_LINE_JOIN MP_STDCALL GetLineJoin() override
			{
				return strokeStyleProperties.lineJoin;
			}

			float MP_STDCALL GetDashOffset() override
			{
				return strokeStyleProperties.dashOffset;
			}

            GmpiDrawing_API::MP1_DASH_STYLE MP_STDCALL GetDashStyle() override
			{
				return strokeStyleProperties.dashStyle;
			}

			uint32_t MP_STDCALL GetDashesCount() override
			{
				return static_cast<uint32_t>(dashes.size());
			}

			void MP_STDCALL GetDashes(float* pdashes, uint32_t dashesCount) override
			{
				if (dashesCount == dashes.size())
				{
					for (size_t i = 0; i < dashes.size(); ++i)
					{
						pdashes[i] = dashes[i];
					}
				}
			}

			// IMpResource
			void MP_STDCALL GetFactory(GmpiDrawing_API::IMpFactory** pfactory) override
			{
				*pfactory = factory;
			}

			GMPI_QUERYINTERFACE1(GmpiDrawing_API::SE_IID_STROKESTYLE_MPGUI, GmpiDrawing_API::IMpStrokeStyle);
			GMPI_REFCOUNT;
		};

		// Provide fallback implementations of Arcs, Quadratic beziers, and bulk operations.
		class GeometrySink : public GmpiDrawing_API::IMpGeometrySink2
		{
		protected:
			GmpiDrawing_API::MP1_POINT startPoint;
			GmpiDrawing_API::MP1_POINT lastPoint;

		public:
            virtual ~GeometrySink(){}
            
			void MP_STDCALL BeginFigure(GmpiDrawing_API::MP1_POINT pStartPoint, GmpiDrawing_API::MP1_FIGURE_BEGIN figureBegin) override
			{
				startPoint = lastPoint = pStartPoint;
			}

			int32_t MP_STDCALL Close() override
			{
				return gmpi::MP_OK;
			}

			void MP_STDCALL AddLines(const GmpiDrawing_API::MP1_POINT* points, uint32_t pointsCount) override
			{
				for (uint32_t i = 0; i < pointsCount; ++i)
					AddLine(points[i]);
			}

			void MP_STDCALL AddBeziers(const GmpiDrawing_API::MP1_BEZIER_SEGMENT* beziers, uint32_t beziersCount) override
			{
				for (uint32_t i = 0; i < beziersCount; ++i)
					AddBezier(beziers + i);
			}

			void MP_STDCALL AddQuadraticBezier(const GmpiDrawing_API::MP1_QUADRATIC_BEZIER_SEGMENT* bezier) override
			{
				/*
				A cubic Bézier curve (yellow) can be made identical to a quadratic one (black) by
		1. Copying the end points, and
		2. Placing its 2 middle control points (yellow circles) 2/3 along line segments from the end points to the quadratic curve's middle control point (black rectangle)
				*/
				GmpiDrawing_API::MP1_BEZIER_SEGMENT cubicbezier;
//				auto startPoint = pathGeometry_->GetLastPoint();
				cubicbezier.point3 = bezier->point2; // end point.
				cubicbezier.point1.x = lastPoint.x + 0.66666f * (bezier->point1.x - lastPoint.x);
				cubicbezier.point1.y = lastPoint.y + 0.66666f * (bezier->point1.y - lastPoint.y);
				cubicbezier.point2.x = bezier->point2.x + 0.66666f * (bezier->point1.x - bezier->point2.x);
				cubicbezier.point2.y = bezier->point2.y + 0.66666f * (bezier->point1.y - bezier->point2.y);

				AddBezier(&cubicbezier);
			}

			void MP_STDCALL AddQuadraticBeziers(const GmpiDrawing_API::MP1_QUADRATIC_BEZIER_SEGMENT* beziers, uint32_t beziersCount) override
			{
				for (uint32_t i = 0; i < beziersCount; ++i)
					AddQuadraticBezier(beziers + i);
			}

			static void nsvg__xformPoint(float* dx, float* dy, float x, float y, float* t)
			{
				*dx = x*t[0] + y*t[2] + t[4];
				*dy = x*t[1] + y*t[3] + t[5];
			}
			static void nsvg__xformVec(float* dx, float* dy, float x, float y, float* t)
			{
				*dx = x*t[0] + y*t[2];
				*dy = x*t[1] + y*t[3];
			}
			static float nsvg__sqr(float x) { return x*x; }
			static float nsvg__vmag(float x, float y) { return sqrtf(x*x + y*y); }
			static float nsvg__vecrat(float ux, float uy, float vx, float vy)
			{
				return (ux*vx + uy*vy) / (nsvg__vmag(ux, uy) * nsvg__vmag(vx, vy));
			}
			static float nsvg__vecang(float ux, float uy, float vx, float vy)
			{
				float r = nsvg__vecrat(ux, uy, vx, vy);
				if (r < -1.0f) r = -1.0f;
				if (r > 1.0f) r = 1.0f;
				return ((ux*vy < uy*vx) ? -1.0f : 1.0f) * acosf(r);
			}
			
			void MP_STDCALL AddArc(const GmpiDrawing_API::MP1_ARC_SEGMENT* arc) override
			{
                constexpr auto PI = static_cast<float>(M_PI);
                
				// Ported from canvg (https://code.google.com/p/canvg/)
				float rx, ry, rotx;
				float x1, y1, x2, y2, cx, cy, dx, dy, d;
				float x1p, y1p, cxp, cyp, s, sa, sb;
				float ux, uy, vx, vy, a1, da;
				float x, y, tanx, tany, a, px = 0, py = 0, ptanx = 0, ptany = 0, t[6];
				float sinrx, cosrx;
				int fa, fs;
				int i, ndivs;
				float hda, kappa;

				rx = fabsf(arc->size.height);// args[0]);			// y radius
				ry = fabsf(arc->size.width);//args[1]);				// x radius
				rotx = arc->rotationAngle /* args[2] */ / 180.0f * static_cast<float>(M_PI); // NSVG_PI;		// x rotation angle
				fa = (int)arc->arcSize;// fabsf(args[3]) > 1e-6 ? 1 : 0;	// Large arc
				fs = (int)arc->sweepDirection;// fabsf(args[4]) > 1e-6 ? 1 : 0;	// Sweep direction
				x1 = lastPoint.x;// pathGeometry_->GetLastPoint().X;// *cpx;						// start point
				y1 = lastPoint.y; // pathGeometry_->GetLastPoint().Y;//*cpy;
				/* not relative
				if (rel) {							// end point
					x2 = *cpx + args[5];
					y2 = *cpy + args[6];
				}
				else
				*/
				{
					x2 = arc->point.x;// args[5];
					y2 = arc->point.y;// args[6];
				}

				dx = x1 - x2;
				dy = y1 - y2;
				d = sqrtf(dx*dx + dy*dy);
				if (d < 1e-6f || rx < 1e-6f || ry < 1e-6f /*|| fabsf(arc->rotationAngle) < 1e-6f */ ) {
					// The arc degenerates to a line
					//nsvg__lineTo(p, x2, y2);
					AddLine(GmpiDrawing::Point(x2, y2));
					//				*cpx = x2;
					//				*cpy = y2;
					return;
				}

				sinrx = sinf(rotx);
				cosrx = cosf(rotx);

				// Convert to center point parameterization.
				// http://www.w3.org/TR/SVG11/implnote.html#ArcImplementationNotes
				// 1) Compute x1', y1'
				x1p = cosrx * dx / 2.0f + sinrx * dy / 2.0f;
				y1p = -sinrx * dx / 2.0f + cosrx * dy / 2.0f;
				d = nsvg__sqr(x1p) / nsvg__sqr(rx) + nsvg__sqr(y1p) / nsvg__sqr(ry);
				if (d > 1) {
					d = sqrtf(d);
					rx *= d;
					ry *= d;
				}
				// 2) Compute cx', cy'
				s = 0.0f;
				sa = nsvg__sqr(rx)*nsvg__sqr(ry) - nsvg__sqr(rx)*nsvg__sqr(y1p) - nsvg__sqr(ry)*nsvg__sqr(x1p);
				sb = nsvg__sqr(rx)*nsvg__sqr(y1p) + nsvg__sqr(ry)*nsvg__sqr(x1p);
				if (sa < 0.0f) sa = 0.0f;
				if (sb > 0.0f)
					s = sqrtf(sa / sb);
				if (fa == fs)
					s = -s;
				cxp = s * rx * y1p / ry;
				cyp = s * -ry * x1p / rx;

				// 3) Compute cx,cy from cx',cy'
				cx = (x1 + x2) / 2.0f + cosrx*cxp - sinrx*cyp;
				cy = (y1 + y2) / 2.0f + sinrx*cxp + cosrx*cyp;

				// 4) Calculate theta1, and delta theta.
				ux = (x1p - cxp) / rx;
				uy = (y1p - cyp) / ry;
				vx = (-x1p - cxp) / rx;
				vy = (-y1p - cyp) / ry;
				a1 = nsvg__vecang(1.0f, 0.0f, ux, uy);	// Initial angle
				da = nsvg__vecang(ux, uy, vx, vy);		// Delta angle

                if (nsvg__vecrat(ux,uy,vx,vy) <= -1.0f) da = PI;
                if (nsvg__vecrat(ux,uy,vx,vy) >= 1.0f) da = 0;

				if (fs == 0 && da > 0)
					da -= 2 * static_cast<float>(M_PI); // NSVG_PI;
				else if (fs == 1 && da < 0)
					da += 2 * static_cast<float>(M_PI); // NSVG_PI;

                if(da == 0.0f) // prevent crash on malformed arcs
                    return;
                
				// Approximate the arc using cubic spline segments.
				t[0] = cosrx; t[1] = sinrx;
				t[2] = -sinrx; t[3] = cosrx;
				t[4] = cx; t[5] = cy;

				// Split arc into max 90 degree segments.
				// The loop assumes an iteration per end point (including start and end), this +1.
				ndivs = (int)(fabsf(da) / (static_cast<float>(M_PI)*0.5f) + 1.0f);
				hda = (da / (float)ndivs) / 2.0f;
				kappa = fabsf(4.0f / 3.0f * (1.0f - cosf(hda)) / sinf(hda));
				if (da < 0.0f)
					kappa = -kappa;

				for (i = 0; i <= ndivs; i++) {
					a = a1 + da * ((float)i / (float)ndivs);
					dx = cosf(a);
					dy = sinf(a);
					nsvg__xformPoint(&x, &y, dx*rx, dy*ry, t); // position
					nsvg__xformVec(&tanx, &tany, -dy*rx * kappa, dx*ry * kappa, t); // tangent
					if (i > 0)
					{
						//nsvg__cubicBezTo(p, px + ptanx, py + ptany, x - tanx, y - tany, x, y);
						GmpiDrawing::BezierSegment bs(GmpiDrawing::Point(px + ptanx, py + ptany), GmpiDrawing::Point(x - tanx, y - tany), GmpiDrawing::Point(x, y));
						AddBezier(&bs);
					}
					px = x;
					py = y;
					ptanx = tanx;
					ptany = tany;
				}
			}

			void MP_STDCALL AddBezier(const GmpiDrawing_API::MP1_BEZIER_SEGMENT* bezier) override
			{
				// Convert bezier to line segments.
				agg::curve4_div bezierToLines;
				bezierToLines.geometrySink = this;
				bezierToLines.init(lastPoint.x, lastPoint.y, bezier->point1.x, bezier->point1.y, bezier->point2.x, bezier->point2.y, bezier->point3.x, bezier->point3.y);
			}

			void MP_STDCALL EndFigure(GmpiDrawing_API::MP1_FIGURE_END figureEnd) override
			{
				if (figureEnd == GmpiDrawing_API::MP1_FIGURE_END_CLOSED)
				{
					AddLine(startPoint);
				}
			}

			GMPI_QUERYINTERFACE1(GmpiDrawing_API::SE_IID_GEOMETRYSINK_MPGUI, GmpiDrawing_API::IMpGeometrySink);
			GMPI_REFCOUNT;
		};

		class GraphicsContext : public GmpiDrawing_API::IMpDeviceContext
		{
		protected:
			std::vector<GmpiDrawing_API::MP1_RECT> clipRectStack;

		public:
			GraphicsContext()
			{
				const float defaultClipBounds = 100000.0f;
				GmpiDrawing_API::MP1_RECT r;
				r.top = r.left = -defaultClipBounds;
				r.bottom = r.right = defaultClipBounds;
				clipRectStack.push_back(r);
			}

			virtual ~GraphicsContext()
			{
			}

			GmpiDrawing::PathGeometry createRectangleGeometry(const GmpiDrawing_API::MP1_RECT* rect, bool filled = false)
			{
				// create geometry
				GmpiDrawing::Factory factory;
				GetFactory(factory.GetAddressOf());

				auto geometry = factory.CreatePathGeometry();
				auto sink = geometry.Open();

				sink.BeginFigure(GmpiDrawing::Point(rect->left, rect->top), filled ? GmpiDrawing::FigureBegin::Filled : GmpiDrawing::FigureBegin::Hollow);
				sink.AddLine(GmpiDrawing::Point(rect->left, rect->bottom));
				sink.AddLine(GmpiDrawing::Point(rect->right, rect->bottom));
				sink.AddLine(GmpiDrawing::Point(rect->right, rect->top));

				sink.EndFigure(GmpiDrawing::FigureEnd::Closed);
				sink.Close();

				return geometry;
			}

			void MP_STDCALL DrawRectangle(const GmpiDrawing_API::MP1_RECT* rect, const GmpiDrawing_API::IMpBrush* brush, float strokeWidth, const GmpiDrawing_API::IMpStrokeStyle* strokeStyle) override
			{
				auto geometry = createRectangleGeometry(rect, false);
				DrawGeometry(geometry.Get(), brush, strokeWidth, strokeStyle);
			}

			void MP_STDCALL FillRectangle(const GmpiDrawing_API::MP1_RECT* rect, const GmpiDrawing_API::IMpBrush* brush) override
			{
				auto geometry = createRectangleGeometry(rect);
				FillGeometry(geometry.Get(), brush);
			}

			void MP_STDCALL Clear(const GmpiDrawing_API::MP1_COLOR* clearColor) override
			{
				//				context_->Clear((D2D1_COLOR_F*)clearColor);
			}

			GmpiDrawing::PathGeometry createLineGeometry(GmpiDrawing_API::MP1_POINT point0, GmpiDrawing_API::MP1_POINT point1)
			{
				// create geometry
				GmpiDrawing::Factory factory;
				GetFactory(factory.GetAddressOf());

				auto geometry = factory.CreatePathGeometry();
				auto sink = geometry.Open();

				sink.BeginFigure(point0, GmpiDrawing::FigureBegin::Hollow);
				sink.AddLine(point1);

				sink.EndFigure(GmpiDrawing::FigureEnd::Open);
				sink.Close();

				return geometry;
			}

			void MP_STDCALL DrawLine(GmpiDrawing_API::MP1_POINT point0, GmpiDrawing_API::MP1_POINT point1, const GmpiDrawing_API::IMpBrush* brush, float strokeWidth, const GmpiDrawing_API::IMpStrokeStyle* strokeStyle) override
			{
				auto geometry = createLineGeometry(point0, point1);
				DrawGeometry(geometry.Get(), brush, strokeWidth, strokeStyle);
			}
/*
			void MP_STDCALL DrawRoundedRectangle(const GmpiDrawing_API::MP1_ROUNDED_RECT* roundedRect, const GmpiDrawing_API::IMpBrush* brush, float strokeWidth, const GmpiDrawing_API::IMpStrokeStyle* strokeStyle) override
			{
				auto geometry = createRoundedRectangleGeometry(roundedRect);
				DrawGeometry(geometry.Get(), brush, strokeWidth, strokeStyle);
			}

			//			int32_t MP_STDCALL CreateMesh(GmpiDrawing_API::IMpMesh** returnObject) override;

			void MP_STDCALL FillRoundedRectangle(const GmpiDrawing_API::MP1_ROUNDED_RECT* roundedRect, const GmpiDrawing_API::IMpBrush* brush) override
			{
				auto geometry = createRoundedRectangleGeometry(roundedRect);
				FillGeometry(geometry.Get(), brush);
			}
*/
			GmpiDrawing::PathGeometry createEllipseGeometry(const GmpiDrawing_API::MP1_ELLIPSE* ellipse)
			{
				// create geometry
				GmpiDrawing::Factory factory;
				GetFactory(factory.GetAddressOf());

				auto geometry = factory.CreatePathGeometry();
				auto sink = geometry.Open();

				/* from lines
				const double step = 0.1;
				Point p;
				bool first = true;
				for (double a = 0.0; a < M_PI * 2.0; a += step)
				{
				p.x = ellipse->point.x + ellipse->radiusX * sin(a);
				p.y = ellipse->point.y + ellipse->radiusY * cos(a);

				if (first)
				{
				sink.BeginFigure(p, FigureBegin::Filled);
				first = false;
				}
				else
				{
				sink.AddLine(p);
				}
				}
				*/
				auto size = GmpiDrawing::Size(ellipse->radiusX, ellipse->radiusY);

				GmpiDrawing::Point topCenter(ellipse->point.x, ellipse->point.y - size.height);
				GmpiDrawing::Point bottomCenter(ellipse->point.x, ellipse->point.y + size.height);

				sink.BeginFigure(topCenter, GmpiDrawing::FigureBegin::Filled);
				GmpiDrawing::ArcSegment firstHalf(bottomCenter, size);
				GmpiDrawing::ArcSegment secondHalf(topCenter, size);

				sink.AddArc(firstHalf);
				sink.AddArc(secondHalf);

				sink.EndFigure(GmpiDrawing::FigureEnd::Closed);
				sink.Close();

/*

				// From Single Arc. Not so accurate up close.
				GmpiDrawing::Point p;
				p.x = ellipse->point.x;
				p.y = ellipse->point.y + ellipse->radiusY;
				sink.BeginFigure(p, GmpiDrawing::FigureBegin::Filled);

				float a = 0.01f;
				p.x = ellipse->point.x + ellipse->radiusX * sinf(a);
				p.y = ellipse->point.y + ellipse->radiusY * cosf(a);

				sink.AddArc(GmpiDrawing::ArcSegment(p, GmpiDrawing::Size(ellipse->radiusX, ellipse->radiusY), static_cast<float>(M_PI) * 1.99f, GmpiDrawing::SweepDirection::Clockwise, GmpiDrawing::ArcSize::Large));

				sink.EndFigure(GmpiDrawing::FigureEnd::Closed);
				sink.Close();
*/				
				return geometry;
			}

			void MP_STDCALL DrawEllipse(const GmpiDrawing_API::MP1_ELLIPSE* ellipse, const GmpiDrawing_API::IMpBrush* brush, float strokeWidth, const GmpiDrawing_API::IMpStrokeStyle* strokeStyle) override
			{
				auto geometry = createEllipseGeometry(ellipse);
				DrawGeometry(geometry.Get(), brush, strokeWidth, strokeStyle);
			}

			void MP_STDCALL FillEllipse(const GmpiDrawing_API::MP1_ELLIPSE* ellipse, const GmpiDrawing_API::IMpBrush* brush) override
			{
				auto geometry = createEllipseGeometry(ellipse);
				FillGeometry(geometry.Get(), brush);
			}

			void MP_STDCALL DrawTextU(const char* utf8String, int32_t stringLength, const GmpiDrawing_API::IMpTextFormat* textFormat, const GmpiDrawing_API::MP1_RECT* layoutRect, const GmpiDrawing_API::IMpBrush* brush, int32_t flags) override
			{}

			//	virtual void MP_STDCALL DrawBitmap( GmpiDrawing_API::IMpBitmap* mpBitmap, GmpiDrawing::Rect destinationRectangle, float opacity, int32_t interpolationMode, GmpiDrawing::Rect sourceRectangle) override
			void MP_STDCALL DrawBitmap(const GmpiDrawing_API::IMpBitmap* mpBitmap, const GmpiDrawing_API::MP1_RECT* destinationRectangle, float opacity, /* MP1_BITMAP_INTERPOLATION_MODE*/ int32_t interpolationMode, const GmpiDrawing_API::MP1_RECT* sourceRectangle) override
			{
			}

			void MP_STDCALL SetTransform(const GmpiDrawing_API::MP1_MATRIX_3X2* transform) override
			{
				//				context_->SetTransform(reinterpret_cast<const D2D1_MATRIX_3X2_F*>(transform));
			}

			void MP_STDCALL GetTransform(GmpiDrawing_API::MP1_MATRIX_3X2* transform) override
			{
				//				context_->GetTransform(reinterpret_cast<D2D1_MATRIX_3X2_F*>(transform));
			}

			//	int32_t MP_STDCALL CreateBitmap(GmpiDrawing_API::MP1_SIZE_U size, const GmpiDrawing_API::MP1_BITMAP_PROPERTIES* bitmapProperties, GmpiDrawing_API::IMpBitmap** bitmap) override;

			int32_t MP_STDCALL CreateBitmapBrush(const GmpiDrawing_API::IMpBitmap* bitmap, const GmpiDrawing_API::MP1_BITMAP_BRUSH_PROPERTIES* bitmapBrushProperties, const GmpiDrawing_API::MP1_BRUSH_PROPERTIES* brushProperties, GmpiDrawing_API::IMpBitmapBrush** bitmapBrush) override
			{
				//				return context_->CreateBitmapBrush((ID2D1Bitmap*)bitmap, (D2D1_BITMAP_BRUSH_PROPERTIES*)bitmapBrushProperties, (D2D1_BRUSH_PROPERTIES*)brushProperties, (ID2D1BitmapBrush**)bitmapBrush);
				return gmpi::MP_FAIL;
			}
			int32_t MP_STDCALL CreateRadialGradientBrush(const GmpiDrawing_API::MP1_RADIAL_GRADIENT_BRUSH_PROPERTIES* radialGradientBrushProperties, const GmpiDrawing_API::MP1_BRUSH_PROPERTIES* brushProperties, const GmpiDrawing_API::IMpGradientStopCollection* gradientStopCollection, GmpiDrawing_API::IMpRadialGradientBrush** radialGradientBrush) override
			{
				//				return context_->CreateRadialGradientBrush((D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES*)radialGradientBrushProperties, (D2D1_BRUSH_PROPERTIES*)brushProperties, (ID2D1GradientStopCollection*)gradientStopCollection, (ID2D1RadialGradientBrush**)radialGradientBrush);
				return gmpi::MP_FAIL;
			}

			int32_t MP_STDCALL CreateCompatibleRenderTarget(const GmpiDrawing_API::MP1_SIZE* desiredSize, GmpiDrawing_API::IMpBitmapRenderTarget** bitmapRenderTarget) override
			{
				return gmpi::MP_FAIL;
			}

			void MP_STDCALL PushAxisAlignedClip(const GmpiDrawing_API::MP1_RECT* clipRect/*, GmpiDrawing_API::MP1_ANTIALIAS_MODE antialiasMode*/) override
			{}

			void MP_STDCALL PopAxisAlignedClip() override
			{
				//				context_->PopAxisAlignedClip();
				clipRectStack.pop_back();
			}

			void MP_STDCALL GetAxisAlignedClip(GmpiDrawing_API::MP1_RECT* returnClipRect) override
			{}

			void MP_STDCALL BeginDraw() override
			{
				//				context_->BeginDraw();
			}

			int32_t MP_STDCALL EndDraw() override
			{
				return gmpi::MP_OK;
			}
			GMPI_QUERYINTERFACE1(GmpiDrawing_API::SE_IID_DEVICECONTEXT_MPGUI, GmpiDrawing_API::IMpDeviceContext);
			GMPI_REFCOUNT_NO_DELETE;
		};

	}
}
