/* Copyright (c) 2007-2023 SynthEdit Ltd

Permission to use, copy, modify, and /or distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THIS SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS.IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
#include "mp_sdk_gui2.h"
#include "Drawing.h"

using namespace gmpi;
using namespace GmpiDrawing;

class GammaTestGui final : public gmpi_gui::MpGuiGfxBase
{
 	void onSetDisplay()
	{
		// pinDisplay changed
	}

 	IntGuiPin pinDisplay;

public:
	GammaTestGui()
	{
		initializePin( pinDisplay, static_cast<MpGuiBaseMemberPtr2>(&GammaTestGui::onSetDisplay) );
	}

	int32_t MP_STDCALL OnRender(GmpiDrawing_API::IMpDeviceContext* drawingContext ) override
	{
		Graphics g(drawingContext);
		ClipDrawingToBounds _clip(g, getRect());

		// white background
		g.Clear(Color::White);

		auto textFormat = GetGraphicsFactory().CreateTextFormat();
		textFormat.SetTextAlignment(TextAlignment::Center);
		auto brush = g.CreateSolidColorBrush(Color::Red);

//		g.DrawTextU("Hello World!", textFormat, 0.0f, 0.0f, brush);

		// draw 8 levels of transparency
		constexpr int sz = 32;

		int top = 12;

		{
			brush.SetColor(Color::Blue);
			const Rect r(12, top, sz * 5, top + sz);
			g.DrawTextU("brush", textFormat, r, brush);

			top += 16;

			int x = 12;
			brush.SetColor(Color::Black);
			for (int i = 0; i < 11; i += 2)
			{
				// Draw a rectangle with the current alpha value
				const int y = top;
				const Rect r(x, y, x + sz, y + 12);

				std::string label = i == 10 ? "1.0" : "0." + std::to_string(i);
				g.DrawTextU(label, textFormat, r, brush);

				x += sz;
			}
		}
		top += 12;

		for (auto col : { Color::Black, Color::Red })
		{
			int x = 12;
			for(int i = 0 ; i < 11 ; i += 2)
			{
				// vary alpha
				float alpha = i / 10.0f;
				Color color = col;
				color.a = alpha;
				brush.SetColor(color);

				// Draw a rectangle with the current alpha value
				const int y = top;
				const Rect r(x, y, x + sz, y + sz);
				g.FillRectangle(r, brush);

				x += sz;
			}

			top += sz + 4;
		}

		{
			brush.SetColor(Color::Blue);
			const Rect r(12, top, sz * 5, top + sz);
			g.DrawTextU("png", textFormat, r, brush);
		}
		top += 16;

		// .png version of same gradients.
		gmpi_sdk::MpString fullUri;
		getHost()->RegisterResourceUri("GammaTest.png", "Image", &fullUri);

		auto png = g.GetFactory().LoadImageU(fullUri.c_str());

		if(!png.isNull())
			g.DrawBitmap(png, Point(12, top), Rect(0, 0, png.GetSize().width, png.GetSize().height));

		top += sz * 2 + 16;

		// draw the alternating-lines gamma test pattern.
		{
			const Rect r(12, top, 12 + sz * 6, top + sz * 6);
			brush.SetColor(Color::White);
			g.FillRectangle(r, brush);
			Color c = Color::Black;
			c.a = 0.5f;
			brush.SetColor(c);
			g.FillRectangle(r, brush);

			const Rect r2(12 + sz * 2, top + sz * 2, 12 + sz * 4, top + sz * 4);
			brush.SetColor(Color::White);
			g.FillRectangle(r2, brush);

			brush.SetColor(Color::Black);
			Rect r3(12 + sz * 2, top + sz * 2, 12 + sz * 4, top + sz * 2 + 1);

			for(int y = 0 ; y < sz ; y++)
			{
				g.FillRectangle(r3, brush);

				r3.top += 2;
				r3.bottom += 2;
			}
		}

		return gmpi::MP_OK;
	}
};

namespace
{
	auto r = Register<GammaTestGui>::withId(L"SE Gamma Test");
}
