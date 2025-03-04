#ifdef _WIN32 // skip compilation on macOS

#include <sstream>
#include "./DirectXGfx.h"
#include "../shared/xplatform.h"
#include "../se_sdk3_hosting/gmpi_gui_hosting.h"
#include "BundleInfo.h"

using namespace GmpiGuiHosting;

namespace se
{
namespace directx
{
int32_t Factory_base::CreateStrokeStyle(const GmpiDrawing_API::MP1_STROKE_STYLE_PROPERTIES* strokeStyleProperties, float* dashes, int32_t dashesCount, GmpiDrawing_API::IMpStrokeStyle** returnValue)
{
	*returnValue = {};

	ID2D1StrokeStyle* b{};

	auto hr = info.d2dFactory->CreateStrokeStyle((const D2D1_STROKE_STYLE_PROPERTIES*)strokeStyleProperties, dashes, dashesCount, &b);

	if (hr == 0)
	{
		gmpi_sdk::mp_shared_ptr<gmpi::IMpUnknown> wrapper;
		wrapper.Attach(new StrokeStyle(b, this));

		return wrapper->queryInterface(GmpiDrawing_API::SE_IID_STROKESTYLE_MPGUI, reinterpret_cast<void**>(returnValue));
	}

	return toReturnCode(hr);
}

int32_t Geometry::Open(GmpiDrawing_API::IMpGeometrySink** geometrySink)
{
	ID2D1GeometrySink* sink = {};

	auto hr = geometry_->Open(&sink);

	if (hr == 0)
	{
		gmpi_sdk::mp_shared_ptr<gmpi::IMpUnknown> b2;
		b2.Attach(new se::directx::GeometrySink(sink));

		b2->queryInterface(GmpiDrawing_API::SE_IID_GEOMETRYSINK_MPGUI, reinterpret_cast<void**>(geometrySink));
	}

	return toReturnCode(hr);
}

int32_t TextFormat::GetFontMetrics(GmpiDrawing_API::MP1_FONT_METRICS* returnFontMetrics)
{
	auto gmpi_fm = gmpi::directx::getFontMetricsHelper(native());

	*returnFontMetrics =
	{
		gmpi_fm.ascent,
		gmpi_fm.descent,
		gmpi_fm.lineGap,
		gmpi_fm.capHeight,
		gmpi_fm.xHeight,
		gmpi_fm.underlinePosition,
		gmpi_fm.underlineThickness,
		gmpi_fm.strikethroughPosition,
		gmpi_fm.strikethroughThickness
	};

	return gmpi::MP_OK;
}

void TextFormat::GetTextExtentU(const char* utf8String, int32_t stringLength, GmpiDrawing_API::MP1_SIZE* returnSize)
{
	const auto size = gmpi::directx::getTextExtentHelper(writeFactory, native(), { utf8String, static_cast<size_t>(stringLength) }, topAdjustment, useLegacyBaseLineSnapping);
	*returnSize = { size.width, size.height };
}

void Factory_SDK3::Init()
{
	gmpi::directx::initFactoryHelper(
		  info.writeFactory
		, info.wicFactory
		, info.d2dFactory
		, info.supportedFontFamilies
		, info.supportedFontFamiliesLowerCase
	);
}

int32_t Factory_base::CreatePathGeometry(GmpiDrawing_API::IMpPathGeometry** pathGeometry)
{
	*pathGeometry = {};

	ID2D1PathGeometry* d2d_geometry{};
	HRESULT hr = info.d2dFactory->CreatePathGeometry(&d2d_geometry);

	if (hr == 0)
	{
		gmpi_sdk::mp_shared_ptr<gmpi::IMpUnknown> b2;
		b2.Attach(new se::directx::Geometry(d2d_geometry));

		b2->queryInterface(GmpiDrawing_API::SE_IID_PATHGEOMETRY_MPGUI, reinterpret_cast<void**>(pathGeometry));
	}

	return toReturnCode(hr);
}

int32_t Factory_base::CreateTextFormat(const char* fontFamilyName, void* unused /* fontCollection */, GmpiDrawing_API::MP1_FONT_WEIGHT fontWeight, GmpiDrawing_API::MP1_FONT_STYLE fontStyle, GmpiDrawing_API::MP1_FONT_STRETCH fontStretch, float fontSize, void* unused2 /* localeName */, GmpiDrawing_API::IMpTextFormat** TextFormat)
{
	*TextFormat = {};

	auto fontFamilyNameW = gmpi::directx::Utf8ToWstring(fontFamilyName);
	std::wstring lowercaseName(fontFamilyNameW);
	std::transform(lowercaseName.begin(), lowercaseName.end(), lowercaseName.begin(), ::tolower);

	if (std::find(info.supportedFontFamiliesLowerCase.begin(), info.supportedFontFamiliesLowerCase.end(), lowercaseName) == info.supportedFontFamiliesLowerCase.end())
	{
		fontFamilyNameW = gmpi::directx::fontMatchHelper(
			info.writeFactory
			, info.GdiFontConversions
			, fontFamilyNameW
			, (gmpi::drawing::FontWeight) fontWeight
			, fontSize
		);
	}

	IDWriteTextFormat* dwTextFormat{};

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
		b2.Attach(new se::directx::TextFormat(info.writeFactory, dwTextFormat));

		b2->queryInterface(GmpiDrawing_API::SE_IID_TEXTFORMAT_MPGUI, reinterpret_cast<void**>(TextFormat));
	}

	return toReturnCode(hr);
}

int32_t Factory_base::CreateImage(int32_t width, int32_t height, GmpiDrawing_API::IMpBitmap** returnDiBitmap)
{
	IWICBitmap* wicBitmap{};
	auto hr = info.wicFactory->CreateBitmap(width, height, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &wicBitmap); // pre-muliplied alpha

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

int32_t Factory_base::LoadImageU(const char* utf8Uri, GmpiDrawing_API::IMpBitmap** returnBitmap)
{
	*returnBitmap = {};

	HRESULT hr{};
	gmpi::directx::ComPtr<IWICBitmapDecoder> pDecoder;
	gmpi::directx::ComPtr<IWICStream> pIWICStream;

	// is this an in-memory resource?
	std::string uriString(utf8Uri);
	std::string binaryData;
	if (uriString.find(BundleInfo::resourceTypeScheme) == 0)
	{
		binaryData = BundleInfo::instance()->getResource(utf8Uri + strlen(BundleInfo::resourceTypeScheme));

		// Create a WIC stream to map onto the memory.
		hr = info.wicFactory->CreateStream(pIWICStream.put());

		// Initialize the stream with the memory pointer and size.
		if (SUCCEEDED(hr)) {
			hr = pIWICStream->InitializeFromMemory(
				(WICInProcPointer)(binaryData.data()),
				(DWORD) binaryData.size());
		}

		// Create a decoder for the stream.
		if (SUCCEEDED(hr)) {
			hr = info.wicFactory->CreateDecoderFromStream(
				pIWICStream,                   // The stream to use to create the decoder
				NULL,                          // Do not prefer a particular vendor
				WICDecodeMetadataCacheOnLoad,  // Cache metadata when needed
				pDecoder.put());               // Pointer to the decoder
		}
	}
	else
	{
		const auto uriW = gmpi::directx::Utf8ToWstring(utf8Uri);

		// To load a bitmap from a file, first use WIC objects to load the image and to convert it to a Direct2D-compatible format.
		hr = info.wicFactory->CreateDecoderFromFilename(
			uriW.c_str(),
			NULL,
			GENERIC_READ,
			WICDecodeMetadataCacheOnLoad,
			pDecoder.put()
		);
	}

	auto wicBitmap = gmpi::directx::loadWicBitmap(info.wicFactory, pDecoder.get());

	if (wicBitmap)
	{
		auto bitmap = new Bitmap(getInfo(), getPlatformPixelFormat(), wicBitmap);
#ifdef _DEBUG
		bitmap->debugFilename = utf8Uri;
#endif
		gmpi_sdk::mp_shared_ptr<GmpiDrawing_API::IMpBitmap> b2;
		b2.Attach(bitmap);

		// on Windows 7, leave image as-is
		if (getPlatformPixelFormat() == GmpiDrawing_API::IMpBitmapPixels::kBGRA_SRGB)
		{
			gmpi::directx::applyPreMultiplyCorrection(wicBitmap.get());
		}

		b2->queryInterface(GmpiDrawing_API::SE_IID_BITMAP_MPGUI, (void**)returnBitmap);
	}
			
	return toReturnCode(hr);
}

void GraphicsContext_SDK3::DrawGeometry(const GmpiDrawing_API::IMpPathGeometry* geometry, const GmpiDrawing_API::IMpBrush* brush, float strokeWidth, const GmpiDrawing_API::IMpStrokeStyle* strokeStyle)
{
	auto& d2d_geometry = ((se::directx::Geometry*)geometry)->geometry_;
	context_->DrawGeometry(d2d_geometry, ((Brush*)brush)->nativeBrush(), (FLOAT)strokeWidth, toNative(strokeStyle));
}

void GraphicsContext_SDK3::DrawTextU(const char* utf8String, int32_t stringLength, const GmpiDrawing_API::IMpTextFormat* textFormat, const GmpiDrawing_API::MP1_RECT* layoutRect, const GmpiDrawing_API::IMpBrush* brush, int32_t flags)
{
	const auto widestring = gmpi::directx::Utf8ToWstring({ utf8String, static_cast<size_t>(stringLength) });
			
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
}

void Bitmap::GetFactory(GmpiDrawing_API::IMpFactory** pfactory)
{
	*pfactory = &factory;
}

int32_t Bitmap::lockPixels(GmpiDrawing_API::IMpBitmapPixels** returnInterface, int32_t flags)
{
	*returnInterface = {};

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
	b2.Attach(new BitmapPixels(diBitmap_, true, flags));

	return b2->queryInterface(GmpiDrawing_API::SE_IID_BITMAP_PIXELS_MPGUI, (void**)(returnInterface));
}

ID2D1Bitmap* Bitmap::GetNativeBitmap(ID2D1DeviceContext* nativeContext)
{
	// Check for loss of surface. If so invalidate device-bitmap
	if (nativeContext != nativeContext_)
	{
		nativeContext_ = nativeContext;
		nativeBitmap_ = {};
		assert(diBitmap_); // Is this a GPU-only bitmap?
	}

	return gmpi::directx::bitmapToNative(
			nativeContext
		, nativeBitmap_
		, diBitmap_
		, factory.getD2dFactory()
		, factory.getWicFactory()
	);
}

Bitmap::Bitmap(gmpi::directx::DxFactoryInfo& factoryInfo, GmpiDrawing_API::IMpBitmapPixels::PixelFormat pixelFormat, IWICBitmap* diBitmap) :
		factory(factoryInfo, nullptr)
	, pixelFormat_(pixelFormat)
{
	diBitmap_ = diBitmap;
}

int32_t GraphicsContext_SDK3::CreateSolidColorBrush(const GmpiDrawing_API::MP1_COLOR* color, GmpiDrawing_API::IMpSolidColorBrush **solidColorBrush)
{
	*solidColorBrush = {};

	ID2D1SolidColorBrush* b{};
	HRESULT hr = context_->CreateSolidColorBrush(*(D2D1_COLOR_F*)color, &b);

	if (hr == 0)
	{
		gmpi_sdk::mp_shared_ptr<gmpi::IMpUnknown> b2;
		b2.Attach(new SolidColorBrush(b, &factory));

		b2->queryInterface(GmpiDrawing_API::SE_IID_SOLIDCOLORBRUSH_MPGUI, reinterpret_cast<void **>(solidColorBrush));
	}

	return toReturnCode(hr);
}

int32_t GraphicsContext_SDK3::CreateGradientStopCollection(const GmpiDrawing_API::MP1_GRADIENT_STOP *gradientStops, uint32_t gradientStopsCount, GmpiDrawing_API::IMpGradientStopCollection** gradientStopCollection)
{
	*gradientStopCollection = {};

	// New way. Gamma-correct gradients without banding. White->Black mid color seems wrong (too light).
	// requires ID2D1DeviceContext, not merely ID2D1RenderTarget
	ID2D1GradientStopCollection1* native2{};

	HRESULT hr = context_->CreateGradientStopCollection(
		(const D2D1_GRADIENT_STOP*)gradientStops,
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

	return toReturnCode(hr);
}

int32_t GraphicsContext_SDK3::CreateCompatibleRenderTarget(const GmpiDrawing_API::MP1_SIZE* desiredSize, GmpiDrawing_API::IMpBitmapRenderTarget** returnObject)
{
	*returnObject = {};

	gmpi_sdk::mp_shared_ptr<gmpi::IMpUnknown> b2;
	b2.Attach(new BitmapRenderTarget(this, *desiredSize, factory.getInfo()));
	return b2->queryInterface(GmpiDrawing_API::SE_IID_BITMAP_RENDERTARGET_MPGUI, reinterpret_cast<void **>(returnObject));
}

// new version supports drawing on CPU bitmaps
int32_t GraphicsContext2::CreateBitmapRenderTarget(GmpiDrawing_API::MP1_SIZE_L desiredSize, bool enableLockPixels, GmpiDrawing_API::IMpBitmapRenderTarget** returnObject)
{
	*returnObject = {};

	GmpiDrawing_API::MP1_SIZE sizef{ static_cast<float>(desiredSize.width),  static_cast<float>(desiredSize.height) };

	gmpi_sdk::mp_shared_ptr<gmpi::IMpUnknown> b2;
	b2.Attach(new BitmapRenderTarget(this, sizef, factory.getInfo(), enableLockPixels));
	return b2->queryInterface(GmpiDrawing_API::SE_IID_BITMAP_RENDERTARGET_MPGUI, reinterpret_cast<void**>(returnObject));
}

BitmapRenderTarget::BitmapRenderTarget(GraphicsContext_SDK3* g, GmpiDrawing_API::MP1_SIZE desiredSize, gmpi::directx::DxFactoryInfo& info, bool enableLockPixels) :
	GraphicsContext_SDK3(nullptr, info)
	, originalContext(g->native())
{
	createBitmapRenderTarget(
			static_cast<UINT>(desiredSize.width)
		, static_cast<UINT>(desiredSize.height)
		, enableLockPixels
		, originalContext
		, factory.getD2dFactory()
		, factory.getWicFactory()
		, wicBitmap
		, context_
	);

	clipRectStack.push_back({ 0, 0, desiredSize.width, desiredSize.height });
}

int32_t BitmapRenderTarget::GetBitmap(GmpiDrawing_API::IMpBitmap** returnBitmap)
{
	*returnBitmap = {};

	HRESULT hr{ E_FAIL };

	gmpi_sdk::mp_shared_ptr<gmpi::IMpUnknown> b2;
	if (wicBitmap)
	{
		b2.Attach(new Bitmap(factory.getInfo(), factory.getPlatformPixelFormat(), wicBitmap)); //temp factory about to go out of scope (when using a bitmap render target)

		hr = S_OK;
	}
	else
	{
		gmpi::directx::ComPtr<ID2D1Bitmap> nativeBitmap;
		hr = context_.as<ID2D1BitmapRenderTarget>()->GetBitmap(nativeBitmap.put());

		if (hr == S_OK)
		{
			b2.Attach(new Bitmap(factory.getInfo(), factory.getPlatformPixelFormat(), /*context_*/originalContext, nativeBitmap.get()));
		}
	}

	if (hr == S_OK)
		b2->queryInterface(GmpiDrawing_API::SE_IID_BITMAP_MPGUI, reinterpret_cast<void**>(returnBitmap));

	return hr == S_OK ? gmpi::MP_OK : gmpi::MP_FAIL;
}

void GraphicsContext_SDK3::PushAxisAlignedClip(const GmpiDrawing_API::MP1_RECT* clipRect/*, GmpiDrawing_API::MP1_ANTIALIAS_MODE antialiasMode*/)
{
	context_->PushAxisAlignedClip((D2D1_RECT_F*)clipRect, D2D1_ANTIALIAS_MODE_ALIASED /*, (D2D1_ANTIALIAS_MODE)antialiasMode*/);

	// Transform to original position.
	GmpiDrawing::Matrix3x2 currentTransform{};
	context_->GetTransform(reinterpret_cast<D2D1_MATRIX_3X2_F*>(&currentTransform));
	auto r2 = currentTransform.TransformRect(*clipRect);
	clipRectStack.push_back(r2);
}

void GraphicsContext_SDK3::GetAxisAlignedClip(GmpiDrawing_API::MP1_RECT* returnClipRect)
{
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
