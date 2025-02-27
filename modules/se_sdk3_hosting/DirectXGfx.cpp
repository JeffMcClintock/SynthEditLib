#ifdef _WIN32 // skip compilation on macOS

#include <sstream>
#include "./DirectXGfx.h"
#include "../shared/xplatform.h"

#include "../shared/fast_gamma.h"
#include "../shared/unicode_conversion.h"
#include "../se_sdk3_hosting/gmpi_gui_hosting.h"
#include "BundleInfo.h"
#include "d2d1helper.h"

using namespace GmpiGuiHosting;

namespace se //gmpi
{
	namespace directx
	{
		std::wstring_convert<std::codecvt_utf8<wchar_t>> Factory_base::stringConverter;

		int32_t Factory_base::CreateStrokeStyle(const GmpiDrawing_API::MP1_STROKE_STYLE_PROPERTIES* strokeStyleProperties, float* dashes, int32_t dashesCount, GmpiDrawing_API::IMpStrokeStyle** returnValue)
		{
			*returnValue = nullptr;

			ID2D1StrokeStyle* b = nullptr;

			auto hr = info.m_pDirect2dFactory->CreateStrokeStyle((const D2D1_STROKE_STYLE_PROPERTIES*)strokeStyleProperties, dashes, dashesCount, &b);

			if (hr == 0)
			{
				gmpi_sdk::mp_shared_ptr<gmpi::IMpUnknown> wrapper;
				wrapper.Attach(new StrokeStyle(b, this));

				//					auto wrapper = gmpi_sdk::make_shared_ptr<StrokeStyle>(b, this);

				return wrapper->queryInterface(GmpiDrawing_API::SE_IID_STROKESTYLE_MPGUI, reinterpret_cast<void**>(returnValue));
			}

			return hr == 0 ? (gmpi::MP_OK) : (gmpi::MP_FAIL);
		}

		int32_t Geometry::Open(GmpiDrawing_API::IMpGeometrySink** geometrySink)
		{
			ID2D1GeometrySink* sink = nullptr;

			auto hr = geometry_->Open(&sink);

			if (hr == 0)
			{
				gmpi_sdk::mp_shared_ptr<gmpi::IMpUnknown> b2;
				b2.Attach(new se::directx::GeometrySink(sink));

				b2->queryInterface(GmpiDrawing_API::SE_IID_GEOMETRYSINK_MPGUI, reinterpret_cast<void**>(geometrySink));

#ifdef LOG_DIRECTX_CALLS
				_RPT1(_CRT_WARN, "ID2D1GeometrySink* sink%x = nullptr;\n", (int)* geometrySink);
				_RPT0(_CRT_WARN, "{\n");
				_RPT2(_CRT_WARN, "geometry%x->Open(&sink%x);\n", (int) this, (int)* geometrySink);
				_RPT0(_CRT_WARN, "}\n");
#endif

			}

			return hr == 0 ? (gmpi::MP_OK) : (gmpi::MP_FAIL);
		}

		int32_t TextFormat::GetFontMetrics(GmpiDrawing_API::MP1_FONT_METRICS* returnFontMetrics)
		{
			IDWriteFontCollection *collection;
			IDWriteFontFamily *family;
			IDWriteFontFace *fontface;
			IDWriteFont *font;
			WCHAR nameW[255];
			UINT32 index;
			BOOL exists;
			HRESULT hr;

			hr = native()->GetFontCollection(&collection);
			//	ok(hr == S_OK, "got 0x%08x\n", hr);

			hr = native()->GetFontFamilyName(nameW, sizeof(nameW) / sizeof(WCHAR));
			//	ok(hr == S_OK, "got 0x%08x\n", hr);

			hr = collection->FindFamilyName(nameW, &index, &exists);
			if (exists == 0) // font not available. Fallback.
			{
				index = 0;
			}

			hr = collection->GetFontFamily(index, &family);
			//	ok(hr == S_OK, "got 0x%08x\n", hr);
			collection->Release();

			hr = family->GetFirstMatchingFont(
				native()->GetFontWeight(),
				native()->GetFontStretch(),
				native()->GetFontStyle(),
				&font);
			//	ok(hr == S_OK, "got 0x%08x\n", hr);

			hr = font->CreateFontFace(&fontface);
			//	ok(hr == S_OK, "got 0x%08x\n", hr);

			font->Release();
			family->Release();

			DWRITE_FONT_METRICS metrics;
			fontface->GetMetrics(&metrics);
			fontface->Release();

			// Sizes returned must always be in DIPs.
			float emsToDips = native()->GetFontSize() / metrics.designUnitsPerEm;

			returnFontMetrics->ascent = emsToDips * metrics.ascent;
			returnFontMetrics->descent = emsToDips * metrics.descent;
			returnFontMetrics->lineGap = emsToDips * metrics.lineGap;
			returnFontMetrics->capHeight = emsToDips * metrics.capHeight;
			returnFontMetrics->xHeight = emsToDips * metrics.xHeight;
			returnFontMetrics->underlinePosition = emsToDips * metrics.underlinePosition;
			returnFontMetrics->underlineThickness = emsToDips * metrics.underlineThickness;
			returnFontMetrics->strikethroughPosition = emsToDips * metrics.strikethroughPosition;
			returnFontMetrics->strikethroughThickness = emsToDips * metrics.strikethroughThickness;

			return gmpi::MP_OK;
		}

		void TextFormat::GetTextExtentU(const char* utf8String, int32_t stringLength, GmpiDrawing_API::MP1_SIZE* returnSize)
		{
			const auto widestring = JmUnicodeConversions::Utf8ToWstring(utf8String, stringLength);

			IDWriteFactory* writeFactory = 0;
			auto hr = DWriteCreateFactory(
				DWRITE_FACTORY_TYPE_SHARED,
				__uuidof(writeFactory),
				reinterpret_cast<IUnknown **>(&writeFactory)
			);

			IDWriteTextLayout* pTextLayout_ = 0;

			hr = writeFactory->CreateTextLayout(
				widestring.data(),      // The string to be laid out and formatted.
				(UINT32)widestring.size(),  // The length of the string.
				native(),  // The text format to apply to the string (contains font information, etc).
				100000,         // The width of the layout box.
				100000,        // The height of the layout box.
				&pTextLayout_  // The IDWriteTextLayout interface pointer.
			);

			DWRITE_TEXT_METRICS textMetrics;
			pTextLayout_->GetMetrics(&textMetrics);

			returnSize->height = textMetrics.height;
			returnSize->width = textMetrics.widthIncludingTrailingWhitespace;

			if (!useLegacyBaseLineSnapping)
			{
				returnSize->height -= topAdjustment;
			}

			SafeRelease(pTextLayout_);
			SafeRelease(writeFactory);
		}

		void Factory_SDK3::Init() //ID2D1Factory1* existingFactory)
		{
			//if (existingFactory)
			//{
			//	m_pDirect2dFactory = existingFactory;
			//	m_pDirect2dFactory->AddRef();
			//}
			//else
			{
				D2D1_FACTORY_OPTIONS o;
#ifdef _DEBUG
				o.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;// D2D1_DEBUG_LEVEL_WARNING; // Need to install special stuff. https://msdn.microsoft.com/en-us/library/windows/desktop/ee794278%28v=vs.85%29.aspx?f=255&MSPPError=-2147217396 
#else
				o.debugLevel = D2D1_DEBUG_LEVEL_NONE;
#endif
//				auto rs = D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, __uuidof(ID2D1Factory1), &o, (void**)&m_pDirect2dFactory);
				auto rs = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &o, (void**)&info.m_pDirect2dFactory);

#ifdef _DEBUG
				if (FAILED(rs))
				{
					o.debugLevel = D2D1_DEBUG_LEVEL_NONE; // fallback
					rs = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &o, (void**)&info.m_pDirect2dFactory);
				}
#endif
				if (FAILED(rs))
				{
					_RPT1(_CRT_WARN, "D2D1CreateFactory FAIL %d\n", rs);
					return;  // Fail.
				}
				//		_RPT2(_CRT_WARN, "D2D1CreateFactory OK %d : %x\n", rs, m_pDirect2dFactory);
			}

			info.writeFactory = nullptr;

			auto hr = DWriteCreateFactory(
				DWRITE_FACTORY_TYPE_SHARED, // no improvment to glitching DWRITE_FACTORY_TYPE_ISOLATED
				__uuidof(info.writeFactory),
				reinterpret_cast<IUnknown**>(&info.writeFactory)
			);

			info.pIWICFactory = nullptr;

			hr = CoCreateInstance(
				CLSID_WICImagingFactory,
				NULL,
				CLSCTX_INPROC_SERVER,
				IID_IWICImagingFactory,
				(LPVOID*)&info.pIWICFactory
			);

			// Cache font family names
			{
				// TODO IDWriteFontSet is improved API, GetSystemFontSet()

				IDWriteFontCollection* fonts = nullptr;
				info.writeFactory->GetSystemFontCollection(&fonts, TRUE);

				auto count = fonts->GetFontFamilyCount();

				for (int index = 0; index < (int)count; ++index)
				{
					IDWriteFontFamily* family = nullptr;
					fonts->GetFontFamily(index, &family);

					IDWriteLocalizedStrings* names = nullptr;
					family->GetFamilyNames(&names);

					BOOL exists;
					unsigned int nameIndex;
					names->FindLocaleName(L"en-us", &nameIndex, &exists);
					if (exists)
					{
						wchar_t name[64];
						names->GetString(nameIndex, name, sizeof(name) / sizeof(name[0]));

						info.supportedFontFamilies.push_back(stringConverter.to_bytes(name));

						std::transform(name, name + wcslen(name), name, ::tolower);
						info.supportedFontFamiliesLowerCase.push_back(name);
					}

					names->Release();
					family->Release();
				}

				fonts->Release();
			}
#if 0
			// test matrix rotation calc
			for (int rot = 0; rot < 8; ++rot)
			{
				const float angle = (rot / 8.f) * 2.f * 3.14159274101257324219f;
				auto test = GmpiDrawing::Matrix3x2::Rotation(angle, { 23, 7 });
				auto test2 = D2D1::Matrix3x2F::Rotation(angle * 180.f / 3.14159274101257324219f, { 23, 7 });

				_RPTN(0, "\nangle=%f\n", angle);
				_RPTN(0, "%f, %f\n", test._11, test._12);
				_RPTN(0, "%f, %f\n", test._21, test._22);
				_RPTN(0, "%f, %f\n", test._31, test._32);
		}

			// test matrix scaling
			const auto test = GmpiDrawing::Matrix3x2::Scale({ 3, 5 }, { 7, 9 });
			const auto test2 = D2D1::Matrix3x2F::Scale({ 3, 5 }, { 7, 9 });
			const auto breakpointer = test._11 + test2._11;
#endif
		}

		Factory_SDK3::~Factory_SDK3()
		{
			SafeRelease(info.m_pDirect2dFactory);
			SafeRelease(info.writeFactory);
			SafeRelease(info.pIWICFactory);
		}

		int32_t Factory_base::CreatePathGeometry(GmpiDrawing_API::IMpPathGeometry** pathGeometry)
		{
			*pathGeometry = nullptr;
			//*pathGeometry = new GmpiGuiHosting::PathGeometry();
			//return gmpi::MP_OK;

			ID2D1PathGeometry* d2d_geometry = nullptr;
			HRESULT hr = info.m_pDirect2dFactory->CreatePathGeometry(&d2d_geometry);

			if (hr == 0)
			{
				gmpi_sdk::mp_shared_ptr<gmpi::IMpUnknown> b2;
				b2.Attach(new se::directx::Geometry(d2d_geometry));

				b2->queryInterface(GmpiDrawing_API::SE_IID_PATHGEOMETRY_MPGUI, reinterpret_cast<void**>(pathGeometry));

#ifdef LOG_DIRECTX_CALLS
				_RPT1(_CRT_WARN, "ID2D1PathGeometry* geometry%x = nullptr;\n", (int)*pathGeometry);
				_RPT0(_CRT_WARN, "{\n");
				_RPT1(_CRT_WARN, "factory->CreatePathGeometry(&geometry%x);\n", (int)*pathGeometry);
				_RPT0(_CRT_WARN, "}\n");
#endif
			}

			return hr == 0 ? (gmpi::MP_OK) : (gmpi::MP_FAIL);
		}

		int32_t Factory_base::CreateTextFormat(const char* fontFamilyName, void* unused /* fontCollection */, GmpiDrawing_API::MP1_FONT_WEIGHT fontWeight, GmpiDrawing_API::MP1_FONT_STYLE fontStyle, GmpiDrawing_API::MP1_FONT_STRETCH fontStretch, float fontSize, void* unused2 /* localeName */, GmpiDrawing_API::IMpTextFormat** TextFormat)
		{
			*TextFormat = nullptr;

			//auto fontFamilyNameW = stringConverter.from_bytes(fontFamilyName);
			auto fontFamilyNameW = JmUnicodeConversions::Utf8ToWstring(fontFamilyName);
			std::wstring lowercaseName(fontFamilyNameW);
			std::transform(lowercaseName.begin(), lowercaseName.end(), lowercaseName.begin(), ::tolower);

			if (std::find(info.supportedFontFamiliesLowerCase.begin(), info.supportedFontFamiliesLowerCase.end(), lowercaseName) == info.supportedFontFamiliesLowerCase.end())
			{
				fontFamilyNameW = fontMatch(fontFamilyNameW, fontWeight, fontSize);
			}

			IDWriteTextFormat* dwTextFormat = nullptr;

			auto hr = info.writeFactory->CreateTextFormat(
				fontFamilyNameW.c_str(),
				NULL,
				(DWRITE_FONT_WEIGHT)fontWeight,
				(DWRITE_FONT_STYLE)fontStyle,
				(DWRITE_FONT_STRETCH)fontStretch,
				fontSize,
				L"", //locale
				&dwTextFormat
			);

			if (hr == 0)
			{
				gmpi_sdk::mp_shared_ptr<gmpi::IMpUnknown> b2;
				b2.Attach(new se::directx::TextFormat(&stringConverter, dwTextFormat));

				b2->queryInterface(GmpiDrawing_API::SE_IID_TEXTFORMAT_MPGUI, reinterpret_cast<void**>(TextFormat));

//				_RPT2(_CRT_WARN, "factory.CreateTextFormat() -> %x %S\n", (int)dwTextFormat, fontFamilyNameW.c_str());
#ifdef LOG_DIRECTX_CALLS
//				_RPT4(_CRT_WARN, "auto c = D2D1::ColorF(%.3f, %.3f, %.3f, %.3f);\n", color->r, color->g, color->b, color->a);
				_RPT1(_CRT_WARN, "IDWriteTextFormat* textformat%x = nullptr;\n", (int)*TextFormat);
				_RPT4(_CRT_WARN, "writeFactory->CreateTextFormat(L\"%S\",NULL, (DWRITE_FONT_WEIGHT)%d, (DWRITE_FONT_STYLE)%d, DWRITE_FONT_STRETCH_NORMAL, %f, L\"\",", fontFamilyNameW.c_str(), fontWeight, fontStyle, fontSize);
				_RPT1(_CRT_WARN, "&textformat%x);\n", (int)*TextFormat);
#endif
			}

			return hr == 0 ? (gmpi::MP_OK) : (gmpi::MP_FAIL);
		}

		// 2nd pass - GDI->DirectWrite conversion. "Arial Black" -> "Arial"
		std::wstring Factory_base::fontMatch(std::wstring fontFamilyNameW, GmpiDrawing_API::MP1_FONT_WEIGHT fontWeight, float fontSize)
		{
			auto it = info.GdiFontConversions.find(fontFamilyNameW);
			if (it != info.GdiFontConversions.end())
			{
				return (*it).second;
			}

			IDWriteGdiInterop* interop = nullptr;
			info.writeFactory->GetGdiInterop(&interop);

			LOGFONT lf;
			memset(&lf, 0, sizeof(LOGFONT));   // Clear out structure.
			lf.lfHeight = (LONG) -fontSize;
			lf.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
			const wchar_t* actual_facename = fontFamilyNameW.c_str();

			if (fontFamilyNameW == _T("serif"))
			{
				actual_facename = _T("Times New Roman");
				lf.lfPitchAndFamily = DEFAULT_PITCH | FF_ROMAN;
			}

			if (fontFamilyNameW == _T("sans-serif"))
			{
				//		actual_facename = _T("Helvetica");
				actual_facename = _T("Arial"); // available on all version of windows
				lf.lfPitchAndFamily = DEFAULT_PITCH | FF_SWISS;
			}

			if (fontFamilyNameW == _T("cursive"))
			{
				actual_facename = _T("Zapf-Chancery");
				lf.lfPitchAndFamily = DEFAULT_PITCH | FF_SCRIPT;
			}

			if (fontFamilyNameW == _T("fantasy"))
			{
				actual_facename = _T("Western");
				lf.lfPitchAndFamily = DEFAULT_PITCH | FF_DECORATIVE;
			}

			if (fontFamilyNameW == _T("monospace"))
			{
				actual_facename = _T("Courier New");
				lf.lfPitchAndFamily = DEFAULT_PITCH | FF_MODERN;
			}
			wcscpy_s(lf.lfFaceName, 32, actual_facename);
			/*
			if ((p_desc->flags & TTL_UNDERLINE) != 0)
			{
			lf.lfUnderline = 1;
			}
			*/
			if (fontWeight > GmpiDrawing_API::MP1_FONT_WEIGHT_SEMI_BOLD)
			{
				lf.lfWeight = FW_BOLD;
			}
			else
			{
				if (fontWeight < 350)
				{
					lf.lfWeight = FW_LIGHT;
				}
			}

			IDWriteFont* font = nullptr;
			auto hr = interop->CreateFontFromLOGFONT(&lf, &font);

			if (font && hr == 0)
			{
				IDWriteFontFamily* family = nullptr;
				font->GetFontFamily(&family);

				IDWriteLocalizedStrings* names = nullptr;
				family->GetFamilyNames(&names);

				BOOL exists;
				unsigned int nameIndex;
				names->FindLocaleName(L"en-us", &nameIndex, &exists);
				if (exists)
				{
					wchar_t name[64];
					names->GetString(nameIndex, name, sizeof(name) / sizeof(name[0]));
					std::transform(name, name + wcslen(name), name, ::tolower);

					//						supportedFontFamiliesLowerCase.push_back(name);
					info.GdiFontConversions.insert({ fontFamilyNameW, name });
					fontFamilyNameW = name;
				}

				names->Release();
				family->Release();

				font->Release();
			}

			interop->Release();
			return fontFamilyNameW;
		}

		int32_t Factory_base::CreateImage(int32_t width, int32_t height, GmpiDrawing_API::IMpBitmap** returnDiBitmap)
		{
			IWICBitmap* wicBitmap = nullptr;
			auto hr = info.pIWICFactory->CreateBitmap(width, height, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &wicBitmap); // pre-muliplied alpha
	// nuh	auto hr = pIWICFactory->CreateBitmap(width, height, GUID_WICPixelFormat32bppBGRA, WICBitmapCacheOnLoad, &wicBitmap);

			if (hr == 0)
			{
				gmpi_sdk::mp_shared_ptr<Bitmap> b2;
				b2.Attach(new Bitmap(getInfo(), getPlatformPixelFormat(), wicBitmap));

				b2->queryInterface(GmpiDrawing_API::SE_IID_BITMAP_MPGUI, (void**)returnDiBitmap);
			}

			SafeRelease(wicBitmap);

			return gmpi::MP_OK;
		}

		int32_t Factory_base::GetFontFamilyName(int32_t fontIndex, gmpi::IString* returnString)
		{
			if (fontIndex < 0 || fontIndex >= info.supportedFontFamilies.size())
			{
				return gmpi::MP_FAIL;
			}

			returnString->setData(info.supportedFontFamilies[fontIndex].data(), static_cast<int32_t>(info.supportedFontFamilies[fontIndex].size()));
			return gmpi::MP_OK;
		}

		int32_t Factory_base::LoadImageU(const char* utf8Uri, GmpiDrawing_API::IMpBitmap** returnDiBitmap)
		{
			*returnDiBitmap = nullptr;

			HRESULT hr{};
			IWICBitmapDecoder* pDecoder{};
			IWICStream* pIWICStream{};

			// is this an in-memory resource?
			std::string uriString(utf8Uri);
			std::string binaryData;
			if (uriString.find(BundleInfo::resourceTypeScheme) == 0)
			{
				binaryData = BundleInfo::instance()->getResource(utf8Uri + strlen(BundleInfo::resourceTypeScheme));

				// Create a WIC stream to map onto the memory.
				hr = info.pIWICFactory->CreateStream(&pIWICStream);

				// Initialize the stream with the memory pointer and size.
				if (SUCCEEDED(hr)) {
					hr = pIWICStream->InitializeFromMemory(
						(WICInProcPointer)(binaryData.data()),
						(DWORD) binaryData.size());
				}

				// Create a decoder for the stream.
				if (SUCCEEDED(hr)) {
					hr = info.pIWICFactory->CreateDecoderFromStream(
						pIWICStream,                   // The stream to use to create the decoder
						NULL,                          // Do not prefer a particular vendor
						WICDecodeMetadataCacheOnLoad,  // Cache metadata when needed
						&pDecoder);                    // Pointer to the decoder
				}
			}
			else
			{
				// auto uriW = stringConverter.from_bytes(utf8Uri);
				const auto uriW = JmUnicodeConversions::Utf8ToWstring(utf8Uri);

				// To load a bitmap from a file, first use WIC objects to load the image and to convert it to a Direct2D-compatible format.
				hr = info.pIWICFactory->CreateDecoderFromFilename(
					uriW.c_str(),
					NULL,
					GENERIC_READ,
					WICDecodeMetadataCacheOnLoad,
					&pDecoder
				);
			}

			IWICBitmapFrameDecode *pSource = NULL;
			if (hr == 0)
			{
				// 2.Retrieve a frame from the image and store the frame in an IWICBitmapFrameDecode object.
				hr = pDecoder->GetFrame(0, &pSource);
			}

			IWICFormatConverter *pConverter = NULL;
			if (hr == 0)
			{
				// 3.The bitmap must be converted to a format that Direct2D can use.
				hr = info.pIWICFactory->CreateFormatConverter(&pConverter);
			}
			if (hr == 0)
			{
				hr = pConverter->Initialize(
					pSource,
					GUID_WICPixelFormat32bppPBGRA, //Premultiplied
					WICBitmapDitherTypeNone,
					NULL,
					0.f,
					WICBitmapPaletteTypeCustom
				);
			}

			IWICBitmap* wicBitmap = nullptr;
			if (hr == 0)
			{
				hr = info.pIWICFactory->CreateBitmapFromSource(
					pConverter,
					WICBitmapCacheOnLoad,
					&wicBitmap);
			}
			/*
D3D11 ERROR: ID3D11Device::CreateTexture2D: The Dimensions are invalid. For feature level D3D_FEATURE_LEVEL_11_0, the Width (value = 32) must be between 1 and 16384, inclusively. The Height (value = 60000) must be between 1 and 16384, inclusively. And, the ArraySize (value = 1) must be between 1 and 2048, inclusively. [ STATE_CREATION ERROR #101: CREATETEXTURE2D_INVALIDDIMENSIONS]
			*/
			if (hr == 0)
			{
				// I've removed this as the max size can vary depending on hardware.
				// So we need to have a robust check elsewhere anyhow.

#if 0
				UINT width, height;

				wicBitmap->GetSize(&width, &height);

				const int maxDirectXImageSize = 16384; // TODO can be smaller. Query hardware.
				if (width > maxDirectXImageSize || height > maxDirectXImageSize)
				{
					hr = E_FAIL; // fail, too big for DirectX.
				}
				else
#endif
				{
					auto bitmap = new Bitmap(getInfo(), getPlatformPixelFormat(), wicBitmap);
#ifdef _DEBUG
					bitmap->debugFilename = utf8Uri;
#endif
					gmpi_sdk::mp_shared_ptr<GmpiDrawing_API::IMpBitmap> b2;
					b2.Attach(bitmap);
					b2->queryInterface(GmpiDrawing_API::SE_IID_BITMAP_MPGUI, (void**)returnDiBitmap);
				}
			}

			SafeRelease(pDecoder);
			SafeRelease(pSource);
			SafeRelease(pConverter);
			SafeRelease(wicBitmap);
			SafeRelease(pIWICStream);
			
			return hr == 0 ? (gmpi::MP_OK) : (gmpi::MP_FAIL);
		}

		void GraphicsContext_SDK3::DrawGeometry(const GmpiDrawing_API::IMpPathGeometry* geometry, const GmpiDrawing_API::IMpBrush* brush, float strokeWidth, const GmpiDrawing_API::IMpStrokeStyle* strokeStyle)
		{
#ifdef LOG_DIRECTX_CALLS
			_RPT3(_CRT_WARN, "context_->DrawGeometry(geometry%x, brush%x, %f, 0);\n", (int)geometry, (int)brush, strokeWidth);
#endif

			auto& d2d_geometry = ((se::directx::Geometry*)geometry)->geometry_;
			context_->DrawGeometry(d2d_geometry, ((Brush*)brush)->nativeBrush(), (FLOAT)strokeWidth, toNative(strokeStyle));
		}

		void GraphicsContext_SDK3::DrawTextU(const char* utf8String, int32_t stringLength, const GmpiDrawing_API::IMpTextFormat* textFormat, const GmpiDrawing_API::MP1_RECT* layoutRect, const GmpiDrawing_API::IMpBrush* brush, int32_t flags)
		{
			const auto widestring = JmUnicodeConversions::Utf8ToWstring(utf8String, stringLength);
			
			assert(dynamic_cast<const TextFormat*>(textFormat));
			auto DxTextFormat = reinterpret_cast<const TextFormat*>(textFormat);
			auto b = ((Brush*)brush)->nativeBrush();
			auto tf = DxTextFormat->native();

			// Don't draw bounding box padding that some fonts have above ascent.
			auto adjusted = *layoutRect;
			if (!DxTextFormat->getUseLegacyBaseLineSnapping())
			{
				adjusted.top -= DxTextFormat->getTopAdjustment();

				// snap to pixel to match Mac.
                const float scale = 0.5f; // Hi DPI x2
				const float offset = -0.25f;
                const auto winBaseline = layoutRect->top + DxTextFormat->getAscent();
                const auto winBaselineSnapped = floorf((offset + winBaseline) / scale) * scale;
				const auto adjust = winBaselineSnapped - winBaseline + scale;

				adjusted.top += adjust;
				adjusted.bottom += adjust;
			}

			context_->DrawText(widestring.data(), (UINT32)widestring.size(), tf, reinterpret_cast<const D2D1_RECT_F*>(&adjusted), b, (D2D1_DRAW_TEXT_OPTIONS)flags | D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);

#ifdef LOG_DIRECTX_CALLS
			{
				std::wstring widestring2 = widestring;
				replacein( widestring2, L"\n", L"\\n");
				_RPT0(_CRT_WARN, "{\n");
				_RPT4(_CRT_WARN, "auto r = D2D1::RectF(%.3f, %.3f, %.3f, %.3ff);\n", layoutRect->left, layoutRect->top, layoutRect->right, layoutRect->bottom);
				_RPT4(_CRT_WARN, "context_->DrawTextW(L\"%S\", %d, textformat%x, &r, brush%x,", widestring2.c_str(), (int)widestring.size(), (int)textFormat, (int) brush);
				_RPT1(_CRT_WARN, " (D2D1_DRAW_TEXT_OPTIONS) %d);\n}\n", flags);
			}
#endif
/*

#if 0
			{
				GmpiDrawing_API::MP1_FONT_METRICS fontMetrics;
				((GmpiDrawing_API::IMpTextFormat*)textFormat)->GetFontMetrics(&fontMetrics);

				float predictedBaseLine = layoutRect->top + fontMetrics.ascent;
				const float scale = 0.5f;
				predictedBaseLine = floorf(-0.5 + predictedBaseLine / scale) * scale;

				GmpiDrawing::Graphics g(this);
				auto brush = g.CreateSolidColorBrush(GmpiDrawing::Color::Lime);
				g.DrawLine(GmpiDrawing::Point(layoutRect->left, predictedBaseLine + 0.25f), GmpiDrawing::Point(layoutRect->left + 2, predictedBaseLine + 0.25f), brush, 0.5);
			}
#endif
*/
		}

		void Bitmap::GetFactory(GmpiDrawing_API::IMpFactory** pfactory)
		{
			*pfactory = &factory;
		}

		int32_t Bitmap::lockPixels(GmpiDrawing_API::IMpBitmapPixels** returnInterface, int32_t flags)
		{
			*returnInterface = nullptr;

			// If image was not loaded from a WicBitmap (i.e. was created from device context) you can't read it.
			if (diBitmap_.isNull())
			{
				_RPT0(0, "Bitmap::lockPixels() - no WIC bitmap. Use IMpDeviceContextExt to create a WIC render target instead.\n");
				return gmpi::MP_FAIL;
			}

			if (0 != (flags & GmpiDrawing_API::MP1_BITMAP_LOCK_WRITE))
			{
				// invalidate device bitmap (will be automatically recreated as needed)
				nativeBitmap_ = {};
			}

			gmpi_sdk::mp_shared_ptr<gmpi::IMpUnknown> b2;
			b2.Attach(new bitmapPixels(diBitmap_, true, flags));

			return b2->queryInterface(GmpiDrawing_API::SE_IID_BITMAP_PIXELS_MPGUI, (void**)(returnInterface));
		}

		ID2D1Bitmap* Bitmap::GetNativeBitmap(ID2D1DeviceContext* nativeContext)
		{
			// Check for loss of surface. If so recreate device-bitmap
			if (nativeContext != nativeContext_)
			{
				nativeContext_ = nativeContext;

				// invalidate stale bitmap.
				nativeBitmap_ = nullptr;

				assert(diBitmap_); // oops where you gonna get the bitmap from now?
			}
			else if (nativeBitmap_)
			{
				return nativeBitmap_.get();
			}

			if (diBitmap_.isNull())
				return nullptr;

			if (!factory.isHdr())
			{
#if 0
				// format of the device bitmap we are creating;
				D2D1_BITMAP_PROPERTIES props;
				props.dpiX = props.dpiY = 96;
				props.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;

				// get the WIC bitmap format.
				WICPixelFormatGUID wicFormat{};
				diBitmap_->GetPixelFormat(&wicFormat);

				if (std::memcmp(&wicFormat, &GUID_WICPixelFormat32bppPBGRA, sizeof(wicFormat)) == 0)
				{
					// 8-bit pixels. SRGB usually, otherwise not for fallback option.
					if (pixelFormat_ == GmpiDrawing_API::IMpBitmapPixels::kBGRA_SRGB)
					{
						props.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
					}
					else
					{
						props.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
					}
				}
				else if (std::memcmp(&wicFormat, &GUID_WICPixelFormat64bppPRGBAHalf, sizeof(wicFormat)) == 0)
				{
					props.pixelFormat.format = DXGI_FORMAT_R16G16B16A16_FLOAT;
				}
				else if (std::memcmp(&wicFormat, &GUID_WICPixelFormat128bppPRGBAFloat, sizeof(wicFormat)) == 0)
				{
					props.pixelFormat.format = DXGI_FORMAT_R32G32B32A32_FLOAT;
				}
				else
				{
					assert(false); // unrecognised format
				}
#endif
				try
				{
					// Convert to D2D format and cache.
					auto hr = nativeContext_->CreateBitmapFromWicBitmap(
						diBitmap_,
//						&props,
						nativeBitmap_.put()
					);

					if (hr) // Common failure is bitmap too big for D2D.
					{
						return nullptr;
					}
				}
				catch (...)
				{
					return nullptr;
				}
			}
			else
			{
				// https://walbourn.github.io/windows-imaging-component-and-windows-8/

				gmpi::drawing::SizeU bitmapSize{};
				diBitmap_->GetSize(&bitmapSize.width, &bitmapSize.height);

				// Create a WIC bitmap to draw on.
				gmpi::directx::ComPtr<IWICBitmap> diBitmap_HDR_;
				HRESULT hr = factory.getWicFactory()->CreateBitmap(
					  static_cast<UINT>(bitmapSize.width)
					, static_cast<UINT>(bitmapSize.height)
					, GUID_WICPixelFormat64bppPRGBAHalf
					, WICBitmapNoCache
					, diBitmap_HDR_.put()
				);

				if (SUCCEEDED(hr))
				{
					// Create a WIC render target.
					D2D1_RENDER_TARGET_PROPERTIES renderTargetProperties = D2D1::RenderTargetProperties(
						D2D1_RENDER_TARGET_TYPE_DEFAULT,
						D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_UNKNOWN)
//						D2D1::PixelFormat(DXGI_FORMAT_R16G16B16A16_FLOAT, D2D1_ALPHA_MODE_PREMULTIPLIED)
					);

					gmpi::directx::ComPtr<ID2D1RenderTarget> pWICRenderTarget;
					hr = factory.getFactory()->CreateWicBitmapRenderTarget(
						diBitmap_HDR_,
						renderTargetProperties,
						pWICRenderTarget.put()
					);

					// Create a device context from the WIC render target.
					//gmpi::directx::ComPtr<ID2D1DeviceContext> pDeviceContext;
					//hr = pWICRenderTarget->QueryInterface(IID_PPV_ARGS(pDeviceContext.put()));
					auto pDeviceContext = pWICRenderTarget.as<ID2D1DeviceContext>();

					if (SUCCEEDED(hr))
					{
						// Convert original image to D2D format
#if 0
						D2D1_BITMAP_PROPERTIES props;
						props.dpiX = props.dpiY = 96;
						if (factory.getPlatformPixelFormat() == GmpiDrawing_API::IMpBitmapPixels::kBGRA_SRGB)
						{
							props.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
						}
						else
						{
							props.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
						}
						props.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
#endif
						gmpi::directx::ComPtr<ID2D1Bitmap> pSourceBitmap;
						hr = pDeviceContext->CreateBitmapFromWicBitmap(
							diBitmap_,
//							&props,
							pSourceBitmap.getAddressOf()
						);

						// create whitescale effect
						gmpi::directx::ComPtr<ID2D1Effect> m_whiteScaleEffect;
						{
							// White level scale is used to multiply the color values in the image; this allows the user
							// to adjust the brightness of the image on an HDR display.
							pDeviceContext->CreateEffect(CLSID_D2D1ColorMatrix, m_whiteScaleEffect.getAddressOf());

							// SDR white level scaling is performing by multiplying RGB color values in linear gamma.
							// We implement this with a Direct2D matrix effect.
							D2D1_MATRIX_5X4_F matrix = D2D1::Matrix5x4F(
								factory.getWhiteMult(), 0, 0, 0,  // [R] Multiply each color channel
								0, factory.getWhiteMult(), 0, 0,  // [G] by the scale factor in 
								0, 0, factory.getWhiteMult(), 0,  // [B] linear gamma space.
								0, 0, 0, 1,		 // [A] Preserve alpha values.
								0, 0, 0, 0);	 //     No offset.

							m_whiteScaleEffect->SetValue(D2D1_COLORMATRIX_PROP_COLOR_MATRIX, matrix);

							// increase the bit-depth of the filter, else it does a shitty 8-bit conversion. Which results in serious degredation of the image.
							if (nativeContext->IsBufferPrecisionSupported(D2D1_BUFFER_PRECISION_16BPC_FLOAT))
							{
								auto hr = m_whiteScaleEffect->SetValue(D2D1_PROPERTY_PRECISION, D2D1_BUFFER_PRECISION_16BPC_FLOAT);
							}
							else if (nativeContext->IsBufferPrecisionSupported(D2D1_BUFFER_PRECISION_32BPC_FLOAT))
							{
								auto hr = m_whiteScaleEffect->SetValue(D2D1_PROPERTY_PRECISION, D2D1_BUFFER_PRECISION_32BPC_FLOAT);
							}
						}

						if (SUCCEEDED(hr))
						{
							// Set the effect input.
							m_whiteScaleEffect->SetInput(0, pSourceBitmap.get());

							// Begin drawing on the device context.
							pDeviceContext->BeginDraw();

							// Draw the effect onto the device context.
							pDeviceContext->DrawImage(m_whiteScaleEffect.get());

							// End drawing.
							hr = pDeviceContext->EndDraw();
						}
					}
				}

				if (SUCCEEDED(hr))
				{
					auto hr = nativeContext_->CreateBitmapFromWicBitmap(
						diBitmap_HDR_,
						nativeBitmap_.put()
					);
				}
			}

			return nativeBitmap_.get();
		}

		Bitmap::Bitmap(gmpi::directx::DxFactoryInfo& factoryInfo, GmpiDrawing_API::IMpBitmapPixels::PixelFormat pixelFormat, IWICBitmap* diBitmap) :
			  factory(factoryInfo, nullptr)
			, pixelFormat_(pixelFormat)
		{
			diBitmap_ = diBitmap;

			// on Windows 7, leave image as-is
			if (/*factory->getPlatformPixelFormat()*/pixelFormat_ == GmpiDrawing_API::IMpBitmapPixels::kBGRA_SRGB)
			{
				ApplyPreMultiplyCorrection();
			}
		}

		// WIX premultiplies images automatically on load, but wrong (assumes linear not SRGB space). Fix it.
		void Bitmap::ApplyPreMultiplyCorrection()
		{
			return;
#if 1
			GmpiDrawing::Bitmap bitmap(this);

			auto pixelsSource = bitmap.lockPixels(true);
			auto imageSize = bitmap.GetSize();
			size_t totalPixels = imageSize.height * pixelsSource.getBytesPerRow() / sizeof(uint32_t);
			uint8_t* sourcePixels = pixelsSource.getAddress();

			// WIX currently not premultiplying correctly, so redo it respecting gamma.
			const float over255 = 1.0f / 255.0f;
			for (size_t i = 0; i < totalPixels; ++i)
			{
				int alpha = sourcePixels[3];

				if (alpha != 255 && alpha != 0)
				{
					float AlphaNorm = alpha * over255;
					float overAlphaNorm = 1.f / AlphaNorm;

					for (int j = 0; j < 3; ++j)
					{
						int p = sourcePixels[j];
						if (p != 0)
						{
							float originalPixel = p * overAlphaNorm; // un-premultiply.

							// To linear
							auto cf = se_sdk::FastGamma::sRGB_to_float(static_cast<unsigned char>(static_cast<int32_t>(originalPixel + 0.5f)));

							cf *= AlphaNorm;						// pre-multiply (correctly).

							// back to SRGB
							sourcePixels[j] = se_sdk::FastGamma::float_to_sRGB(cf);
						}
					}
				}

				sourcePixels += sizeof(uint32_t);
			}
#if 0
			for (unsigned char i = 0; i < 256; ++i)
			{
				_RPT2(_CRT_WARN, "%f %f\n", FastGamma::sRGB_to_float(i), (float)FastGamma::float_to_sRGB(FastGamma::sRGB_to_float(i)));
			}
#endif
#endif
		}

#if 0
		void Bitmap::ApplyAlphaCorrection_win7()
		{

#if 1 // apply gamma correction to compensate for linear blending in SRGB space (DirectX 1.0 limitation)
			GmpiDrawing::Bitmap bitmap(this);

			auto pixelsSource = bitmap.lockPixels(true);
			auto imageSize = bitmap.GetSize();
			int totalPixels = (int)imageSize.height * pixelsSource.getBytesPerRow() / sizeof(uint32_t);

			uint8_t* sourcePixels = pixelsSource.getAddress();
			const float gamma = 2.2f;
			const float overTwoFiftyFive = 1.0f / 255.0f;
			for (int i = 0; i < totalPixels; ++i)
			{
				int alpha = sourcePixels[3];

				if (alpha != 0 && alpha != 255)
				{
					float bitmapAlpha = alpha * overTwoFiftyFive;

					// Calc pixel lumination (linear).
					float components[3];
					float foreground = 0.0f;
					for (int c = 0; c < 3; ++c)
					{
						float pixel = sourcePixels[c] * overTwoFiftyFive;
						pixel /= bitmapAlpha; // un-premultiply
						pixel = powf(pixel, gamma);
						components[c] = pixel;
					}
					//					foreground = 0.2126 * components[2] + 0.7152 * components[1] + 0.0722 * components[0]; // Luminance.
					foreground = 0.3333f * components[2] + 0.3333f * components[1] + 0.3333f * components[0]; // Average. Much the same as Luminance.

					float blackAlpha = 1.0f - powf(1.0f - bitmapAlpha, 1.0 / gamma);
					float whiteAlpha = powf(bitmapAlpha, 1.0f / gamma);

					float mix = powf(foreground, 1.0f / gamma);

					float bitmapAlphaCorrected = blackAlpha * (1.0f - mix) + whiteAlpha * mix;

					for (int c = 0; c < 3; ++c)
					{
						float pixel = components[c];
						pixel = powf(pixel, 1.0f / gamma); // linear -> sRGB space.
						pixel *= bitmapAlphaCorrected; // premultiply
						pixel = pixel * 255.0f + 0.5f; // back to 8-bit
						sourcePixels[c] = (std::min)(255, static_cast<int32_t>(pixel));
					}

					bitmapAlphaCorrected = bitmapAlphaCorrected * 255.0f + 0.5f; // back to 8-bit
		//			int alphaVal = (int)(bitmapAlphaCorrected * 255.0f + 0.5f);
					sourcePixels[3] = static_cast<int32_t>(bitmapAlphaCorrected);
				}
				sourcePixels += sizeof(uint32_t);
			}
#endif
		}
#endif

		int32_t GraphicsContext_SDK3::CreateSolidColorBrush(const GmpiDrawing_API::MP1_COLOR* color, GmpiDrawing_API::IMpSolidColorBrush **solidColorBrush)
		{
			*solidColorBrush = nullptr;

//			HRESULT hr = context_->CreateSolidColorBrush(*(D2D1_COLOR_F*)color, &b);

#if	ENABLE_HDR_SUPPORT
			const D2D1_COLOR_F c
			{
				color->r * whiteMult,
				color->g * whiteMult,
				color->b * whiteMult,
				color->a
			};
#else
			const D2D1_COLOR_F c
			{
				color->r,
				color->g,
				color->b,
				color->a
			};
#endif

			ID2D1SolidColorBrush* b = nullptr;
			HRESULT hr = context_->CreateSolidColorBrush(c, &b);

			if (hr == 0)
			{
				gmpi_sdk::mp_shared_ptr<gmpi::IMpUnknown> b2;
				b2.Attach(new SolidColorBrush(b, &factory

#if	ENABLE_HDR_SUPPORT
					, whiteMult
#endif
				));

				b2->queryInterface(GmpiDrawing_API::SE_IID_SOLIDCOLORBRUSH_MPGUI, reinterpret_cast<void **>(solidColorBrush));
			}

#ifdef LOG_DIRECTX_CALLS
			_RPT1(_CRT_WARN, "ID2D1SolidColorBrush* brush%x = nullptr;\n", (int)* solidColorBrush);
			_RPT0(_CRT_WARN, "{\n");
			_RPT4(_CRT_WARN, "auto c = D2D1::ColorF(%.3ff, %.3ff, %.3ff, %.3ff);\n", color->r, color->g, color->b, color->a);
			_RPT1(_CRT_WARN, "context_->CreateSolidColorBrush(c, &brush%x);\n", (int)* solidColorBrush);
			_RPT0(_CRT_WARN, "}\n");
#endif

			return hr == 0 ? (gmpi::MP_OK) : (gmpi::MP_FAIL);
		}

		int32_t GraphicsContext_SDK3::CreateGradientStopCollection(const GmpiDrawing_API::MP1_GRADIENT_STOP *gradientStops, uint32_t gradientStopsCount, GmpiDrawing_API::IMpGradientStopCollection** gradientStopCollection)
		{
			*gradientStopCollection = nullptr;

			HRESULT hr = 0;

#if 1
			std::vector<D2D1_GRADIENT_STOP> stops(gradientStopsCount);
			for (uint32_t i = 0; i < gradientStopsCount; ++i)
			{
				stops[i].color = D2D1::ColorF(
#if	ENABLE_HDR_SUPPORT
					gradientStops[i].color.r * whiteMult,
					gradientStops[i].color.g * whiteMult,
					gradientStops[i].color.b * whiteMult,
#else
					gradientStops[i].color.r,
					gradientStops[i].color.g,
					gradientStops[i].color.b,
#endif
					gradientStops[i].color.a
				);
				stops[i].position = gradientStops[i].position;
			}
			{
				// New way. Gamma-correct gradients without banding. White->Black mid color seems wrong (too light).
				// requires ID2D1DeviceContext, not merely ID2D1RenderTarget
				ID2D1GradientStopCollection1* native2 = nullptr;

				hr = context_->CreateGradientStopCollection(
					stops.data(), // (D2D1_GRADIENT_STOP*)gradientStops,
					gradientStopsCount,
					D2D1_COLOR_SPACE_SRGB,
					D2D1_COLOR_SPACE_SRGB,
					//D2D1_BUFFER_PRECISION_8BPC_UNORM_SRGB, // Buffer precision. fails in HDR
					D2D1_BUFFER_PRECISION_16BPC_FLOAT, // the same in normal, correct in HDR
					D2D1_EXTEND_MODE_CLAMP,
					D2D1_COLOR_INTERPOLATION_MODE_STRAIGHT,
					&native2);

				if (hr == 0)
				{
					gmpi_sdk::mp_shared_ptr<gmpi::IMpUnknown> wrapper;
					wrapper.Attach(new GradientStopCollection1(native2, &factory));

					wrapper->queryInterface(GmpiDrawing_API::SE_IID_GRADIENTSTOPCOLLECTION_MPGUI, reinterpret_cast<void**>(gradientStopCollection));
				}
			}
#else
			{
				ID2D1GradientStopCollection* native1 = nullptr;

				// for proper gradient in SRGB target, need to set gamma. hmm not sure. https://msdn.microsoft.com/en-us/library/windows/desktop/dd368113(v=vs.85).aspx
				hr = context_->CreateGradientStopCollection(
					(D2D1_GRADIENT_STOP*)gradientStops,
					gradientStopsCount,
					D2D1_GAMMA_2_2,	// gamma-correct, but not smooth.
					//	D2D1_GAMMA_1_0, // smooth, but not gamma-correct.
					D2D1_EXTEND_MODE_CLAMP,
					&native1);
				if (hr == 0)
				{
					gmpi_sdk::mp_shared_ptr<gmpi::IMpUnknown> wrapper;
					wrapper.Attach(new GradientStopCollection(native1, factory));

					wrapper->queryInterface(GmpiDrawing_API::SE_IID_GRADIENTSTOPCOLLECTION_MPGUI, reinterpret_cast<void**>(gradientStopCollection));
				}
			}
#endif

			return hr == 0 ? (gmpi::MP_OK) : (gmpi::MP_FAIL);
		}


		//int32_t GraphicsContext_SDK3::CreateMesh(GmpiDrawing_API::IMpMesh** returnObject)
		//{
		//	*returnObject = nullptr;

		//	auto mesh = new Mesh(factory, context_);
		//	return mesh->queryInterface(GmpiDrawing_API::SE_IID_MESH_MPGUI, reinterpret_cast<void **>(returnObject));
		//}
		/*
		int32_t GraphicsContext_SDK3::CreateBitmap(GmpiDrawing_API::MP1_SIZE_U size, const GmpiDrawing_API::MP1_BITMAP_PROPERTIES* bitmapProperties, GmpiDrawing_API::IMpBitmap** bitmap)
		{
			*bitmap = nullptr;

			D2D1_BITMAP_PROPERTIES nativeBitmapProperties;
			nativeBitmapProperties.dpiX = 0.0f;
			nativeBitmapProperties.dpiY = 0.0f;
			nativeBitmapProperties.pixelFormat = context_->GetPixelFormat();

			ID2D1Bitmap* b = nullptr;
			auto hr = context_->CreateBitmap(*(D2D1_SIZE_U*) &size, nativeBitmapProperties, &b);

			if (hr == 0)
			{
				gmpi_sdk::mp_shared_ptr<gmpi::IMpUnknown> b2;
				b2.Attach(new  bitmap(context_, b));

				b2->queryInterface(GmpiDrawing_API::SE_IID_BITMAP_MPGUI, reinterpret_cast<void **>(bitmap));
			}

			return hr == 0 ? (gmpi::MP_OK) : (gmpi::MP_FAIL);
		}
		*/

		int32_t GraphicsContext_SDK3::CreateCompatibleRenderTarget(const GmpiDrawing_API::MP1_SIZE* desiredSize, GmpiDrawing_API::IMpBitmapRenderTarget** returnObject)
		{
			*returnObject = nullptr;

			gmpi_sdk::mp_shared_ptr<gmpi::IMpUnknown> b2;
			b2.Attach(new BitmapRenderTarget(this, *desiredSize, factory.getInfo()));
			return b2->queryInterface(GmpiDrawing_API::SE_IID_BITMAP_RENDERTARGET_MPGUI, reinterpret_cast<void **>(returnObject));
		}

		// new version supports drawing on CPU bitmaps
		int32_t GraphicsContext2::CreateBitmapRenderTarget(GmpiDrawing_API::MP1_SIZE_L desiredSize, bool enableLockPixels, GmpiDrawing_API::IMpBitmapRenderTarget** returnObject)
		{
			*returnObject = nullptr;

			GmpiDrawing_API::MP1_SIZE sizef{ static_cast<float>(desiredSize.width),  static_cast<float>(desiredSize.height) };

			gmpi_sdk::mp_shared_ptr<gmpi::IMpUnknown> b2;
			b2.Attach(new BitmapRenderTarget(this, sizef, factory.getInfo(), enableLockPixels));
			return b2->queryInterface(GmpiDrawing_API::SE_IID_BITMAP_RENDERTARGET_MPGUI, reinterpret_cast<void**>(returnObject));
		}

		BitmapRenderTarget::BitmapRenderTarget(GraphicsContext_SDK3* g, GmpiDrawing_API::MP1_SIZE desiredSize, gmpi::directx::DxFactoryInfo& info, bool enableLockPixels) :
			GraphicsContext_SDK3(nullptr, info)
			, originalContext(g->native())
		{
			if (enableLockPixels) // TODO !!! wrong gamma.
			{
				// Create a WIC render target. Modifyable by CPU (lock pixels). More expensive.
				const bool use8bit = true;

				// Create a WIC bitmap to draw on.
				[[maybe_unused]] auto hr =
					factory.getWicFactory()->CreateBitmap(
						  static_cast<UINT>(desiredSize.width)
						, static_cast<UINT>(desiredSize.height)
						, use8bit ? GUID_WICPixelFormat32bppPBGRA : GUID_WICPixelFormat64bppPRGBAHalf // GUID_WICPixelFormat128bppPRGBAFloat // GUID_WICPixelFormat64bppPRGBAHalf // GUID_WICPixelFormat128bppPRGBAFloat
						, use8bit ? WICBitmapCacheOnDemand : WICBitmapNoCache
						, wicBitmap.put()
					);

				D2D1_RENDER_TARGET_PROPERTIES renderTargetProperties = D2D1::RenderTargetProperties(
					D2D1_RENDER_TARGET_TYPE_DEFAULT,
					D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_UNKNOWN)
					//D2D1::PixelFormat(DXGI_FORMAT_R16G16B16A16_FLOAT, D2D1_ALPHA_MODE_PREMULTIPLIED)
				);

				gmpi::directx::ComPtr<ID2D1RenderTarget> wikBitmapRenderTarget;
				factory.getD2dFactory()->CreateWicBitmapRenderTarget(
					wicBitmap,
					renderTargetProperties,
					wikBitmapRenderTarget.put()
				);

				// wikBitmapRenderTarget->QueryInterface(IID_ID2D1DeviceContext, (void**)&context_);
				// hr = wikBitmapRenderTarget->QueryInterface(IID_PPV_ARGS(&context_));
				context_ = wikBitmapRenderTarget.as<ID2D1DeviceContext>();

//context_->AddRef(); //?????
			}
			else
			{
				// Create a render target on the GPU. Not modifyable by CPU.
				/* auto hr = */ g->native()->CreateCompatibleRenderTarget(*(D2D1_SIZE_F*)&desiredSize, gpuBitmapRenderTarget.put());
				gpuBitmapRenderTarget->QueryInterface(IID_ID2D1DeviceContext, (void**)&context_);
			}

			clipRectStack.push_back({ 0, 0, desiredSize.width, desiredSize.height });
		}

		int32_t BitmapRenderTarget::GetBitmap(GmpiDrawing_API::IMpBitmap** returnBitmap)
		{
			*returnBitmap = nullptr;

			HRESULT hr{ E_FAIL };

			if (gpuBitmapRenderTarget)
			{
				ID2D1Bitmap* nativeBitmap{};
				hr = gpuBitmapRenderTarget->GetBitmap(&nativeBitmap);

				if (hr == S_OK)
				{
					gmpi_sdk::mp_shared_ptr<gmpi::IMpUnknown> b2;
					b2.Attach(new Bitmap(factory.getInfo(), factory.getPlatformPixelFormat(), /*context_*/originalContext, nativeBitmap));
					nativeBitmap->Release();

					b2->queryInterface(GmpiDrawing_API::SE_IID_BITMAP_MPGUI, reinterpret_cast<void**>(returnBitmap));
				}
			}
			else // if (wikBitmapRenderTarget)
			{
				context_ = nullptr;

				gmpi_sdk::mp_shared_ptr<gmpi::IMpUnknown> b2;
				b2.Attach(new Bitmap(factory.getInfo(), factory.getPlatformPixelFormat(), wicBitmap)); //temp factory about to go out of scope (when using a bitmap render target)

				b2->queryInterface(GmpiDrawing_API::SE_IID_BITMAP_MPGUI, reinterpret_cast<void**>(returnBitmap));

				hr = S_OK;
			}

			return hr == S_OK ? gmpi::MP_OK : gmpi::MP_FAIL;
		}

		void GraphicsContext_SDK3::PushAxisAlignedClip(const GmpiDrawing_API::MP1_RECT* clipRect/*, GmpiDrawing_API::MP1_ANTIALIAS_MODE antialiasMode*/)
		{
#ifdef LOG_DIRECTX_CALLS
			_RPT0(_CRT_WARN, "{\n");
			_RPT4(_CRT_WARN, "auto r = D2D1::RectF(%.3f, %.3f, %.3f, %.3ff);\n", clipRect->left, clipRect->top, clipRect->right, clipRect->bottom);
			_RPT0(_CRT_WARN, "context_->PushAxisAlignedClip(&r, D2D1_ANTIALIAS_MODE_ALIASED);\n");
			_RPT0(_CRT_WARN, "}\n");
#endif

			context_->PushAxisAlignedClip((D2D1_RECT_F*)clipRect, D2D1_ANTIALIAS_MODE_ALIASED /*, (D2D1_ANTIALIAS_MODE)antialiasMode*/);

			// Transform to original position.
			GmpiDrawing::Matrix3x2 currentTransform{};
			context_->GetTransform(reinterpret_cast<D2D1_MATRIX_3X2_F*>(&currentTransform));
			auto r2 = currentTransform.TransformRect(*clipRect);
			clipRectStack.push_back(r2);

//			_RPT4(_CRT_WARN, "                 PushAxisAlignedClip( %f %f %f %f)\n", r2.left, r2.top, r2.right , r2.bottom);
		}

		void GraphicsContext_SDK3::GetAxisAlignedClip(GmpiDrawing_API::MP1_RECT* returnClipRect)
		{
#ifdef LOG_DIRECTX_CALLS
			_RPT0(_CRT_WARN, "{\n");
			_RPT0(_CRT_WARN, "D2D1_MATRIX_3X2_F t;\n");
			_RPT0(_CRT_WARN, "context_->GetTransform(&t);\n");
			_RPT0(_CRT_WARN, "}\n");
#endif

			// Transform to original position.
			GmpiDrawing::Matrix3x2 currentTransform;
			context_->GetTransform(reinterpret_cast<D2D1_MATRIX_3X2_F*>(&currentTransform));
			currentTransform.Invert();
			auto r2 = currentTransform.TransformRect(clipRectStack.back());

			*returnClipRect = r2;
		}
	} // namespace
} // namespace


#endif // skip compilation on macOS
