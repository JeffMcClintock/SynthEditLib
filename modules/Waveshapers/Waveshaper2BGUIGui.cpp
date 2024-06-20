/* Copyright (c) 2007-2022 SynthEdit Ltd
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*     * Redistributions in binary form must reproduce the above copyright
*       notice, this list of conditions and the following disclaimer in the
*       documentation and/or other materials provided with the distribution.
*     * Neither the name SEM, nor SynthEdit, nor 'Music Plugin Interface' nor the
*       names of its contributors may be used to endorse or promote products
*       derived from this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY SynthEdit Ltd ``AS IS'' AND ANY
* EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL SynthEdit Ltd BE LIABLE FOR ANY
* DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
* (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
* LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
* ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
* SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#include "mp_sdk_gui2.h"
#include "Drawing.h"
#include "../shared/FontCache.h"
#include "../shared/expression_evaluate.h"
#include "../shared/unicode_conversion.h"

using namespace gmpi;
using namespace GmpiDrawing;

const float scaleFactor = 2.15f;
const float scaleFactorB = scaleFactor * 0.5f;

class Waveshaper2BGUIGui final : public gmpi_gui::MpGuiGfxBase, public FontCacheClient
{
 	void onSetShape()
	{
		invalidateRect();
	}

 	StringGuiPin pinShape;

public:
	Waveshaper2BGUIGui()
	{
		initializePin( pinShape, static_cast<MpGuiBaseMemberPtr2>(&Waveshaper2BGUIGui::onSetShape) );
	}

	int32_t OnRender(GmpiDrawing_API::IMpDeviceContext* drawingContext ) override
	{
		Graphics g(drawingContext);

		//const auto originalTransform = g.GetTransform();
		//auto adjustedTransform = Matrix3x2::Translation(position.left, position.top) * originalTransform;
		//g.SetTransform(adjustedTransform);

		auto r2 = getRect();
		//		g.PushAxisAlignedClip(r2);

		DrawScale(g);

		float vscale = r2.getHeight() * 0.01f / scaleFactorB;
		float hscale = r2.getWidth() * 0.01f / scaleFactorB;
		int mid_x = r2.getWidth() / 2;
		int mid_y = r2.getHeight() / 2;

		auto brush = g.CreateSolidColorBrush(Color::FromArgb(0xFF00FF00));

		Evaluator ee;
		const auto formula_ascii = JmUnicodeConversions::WStringToUtf8(pinShape.getValue());

		int flags = 0;

		const float ten_over_scale = scaleFactorB * 10.f / r2.getWidth();

		auto geometry = g.GetFactory().CreatePathGeometry();
		auto sink = geometry.Open();

		for (int x = 0; x < r2.getWidth(); x++)
		{
			double xf = ten_over_scale * (float)(x - mid_x);
			ee.SetValue("x", &xf);
			double yf;
			ee.Evaluate(formula_ascii.c_str(), &yf, &flags);
			if (isnan(yf))
			{
				yf = 0.0;
			}

			yf = mid_y - 10.0f * yf * vscale; // 0.1 * yf * r2.getHeight() / scaleFactorB;

			if (x == 0)
			{
				sink.BeginFigure(x, yf);
			}
			else
			{
				sink.AddLine(GmpiDrawing::Point(x, yf));
			}
		}
		sink.EndFigure(FigureEnd::Open);
		sink.Close();

		g.DrawGeometry(geometry, brush, 1.0f);

		// Transform back.
//		g.SetTransform(originalTransform);
		return gmpi::MP_OK;
	}

	void DrawScale(Graphics g)
	{
		FontMetadata* typeface = nullptr;
		GetTextFormat(getHost(), getGuiHost(), "tty", &typeface); // get ttyp font color etc (ignore actual font becuase we need to create a custom one to modify_
		auto dtextFormat = g.GetFactory().CreateTextFormat2(10);

		const float snapToPixelOffset = 0.5f;

		auto r2 = getRect();
		int width = r2.right - r2.left;
		int height = r2.bottom - r2.top;

		auto background_brush = g.CreateSolidColorBrush(typeface->getBackgroundColor());
		g.FillRectangle(Rect(0, 0, width, height), background_brush);

		float v_scale = height / scaleFactor;
		float h_scale = width / scaleFactor;
		int mid_x = width / 2;
		int mid_y = height / 2;

		// create a green pen
		auto darked_col = typeface->getColor();
		darked_col.r *= 0.5f;
		darked_col.g *= 0.5f;
		darked_col.b *= 0.5f;

		auto brush2 = g.CreateSolidColorBrush(darked_col);

		// BACKGROUND LINES
		// horizontal line
		const float penWidth = 1.0f;
		g.DrawLine(GmpiDrawing::Point(0, mid_y + snapToPixelOffset), GmpiDrawing::Point(width, mid_y + snapToPixelOffset), brush2, penWidth);

		// horiz center line
		g.DrawLine(GmpiDrawing::Point(mid_x + snapToPixelOffset, 0), GmpiDrawing::Point(mid_x + snapToPixelOffset, height), brush2, penWidth);

		// diagonal
		g.DrawLine(GmpiDrawing::Point(snapToPixelOffset, height), GmpiDrawing::Point(width + snapToPixelOffset, 0), brush2, penWidth);

		int tick_width;
		for (int v = -10; v < 11; v += 2)
		{
			float y = v * v_scale / 10.f;
			float x = v * h_scale / 10.f;

			if (v % 5 == 0)
				tick_width = 4;
			else
				tick_width = 2;

			// X-Axis ticks
			g.DrawLine(GmpiDrawing::Point(snapToPixelOffset + mid_x - tick_width, snapToPixelOffset + mid_y + (int)y), GmpiDrawing::Point(snapToPixelOffset + mid_x + tick_width, snapToPixelOffset + mid_y + (int)y), brush2, penWidth);

			// Y-Axis ticks
			g.DrawLine(GmpiDrawing::Point(snapToPixelOffset + mid_x + (int)x, snapToPixelOffset + mid_y - tick_width), GmpiDrawing::Point(snapToPixelOffset + mid_x + (int)x, snapToPixelOffset + mid_y + tick_width), brush2, penWidth);
		}

		// labels
		if (height > 30)
		{
			// Set up the font
			int fontHeight = 10;

			auto brush = g.CreateSolidColorBrush(typeface->getColor());// Color::FromArgb(0xFF00FA00));

			//SetTextColor(hDC, RGB(0, 250, 0));
			//SetBkMode(hDC, TRANSPARENT);
			//SetTextAlign(hDC, TA_LEFT);
			dtextFormat.SetTextAlignment(TextAlignment::Leading);

			char txt[10];
			// Y-Axis text
			for (float fv = -5; fv < 5.1; fv += 2.0)
			{
				float y = fv * v_scale / 5.f;
				if (fv != -1.f)
				{
					snprintf(txt, std::size(txt), "%2.0f", fv);
					//TextOut(hDC, mid_x + tick_width, mid_y - (int)y - fontHeight / 2, txt, (int)wcslen(txt));
					int tx = mid_x + tick_width;
					int ty = mid_y - (int)y - fontHeight / 2;
					GmpiDrawing::Rect textRect(tx, ty, tx + 100, ty + typeface->pixelHeight_);
					g.DrawTextU(txt, dtextFormat, textRect, brush2);
				}
			}

			//			int orig_ta = SetTextAlign(hDC, TA_CENTER);
			//TODO			auto originalAlignment = dtextFormat.GetTextAlignment();
			dtextFormat.SetTextAlignment(TextAlignment::Center);
			// X-Axis text
			for (float fv = -4; fv < 4.1; fv += 2.0)
			{
				if (fv != -1.f)
				{
					snprintf(txt, std::size(txt), "%2.0f", fv);
					//	TextOut(hDC, mid_x + (int)y, mid_y + tick_width, txt, (int)wcslen(txt));
					float x = fv * h_scale / 5.f;
					int tx = mid_x + (int)x;
					int ty = mid_y + tick_width;
					GmpiDrawing::Rect textRect(tx - 50, ty, tx + 50, ty + typeface->pixelHeight_);
					g.DrawTextU(txt, dtextFormat, textRect, brush2);
				}
			}
		}
	}
};

namespace
{
	auto r = Register<Waveshaper2BGUIGui>::withId(L"SE Waveshaper2B GUI");
}
