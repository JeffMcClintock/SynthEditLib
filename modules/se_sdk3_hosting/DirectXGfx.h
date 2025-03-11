#pragma once

/*
#include "DirectXGfx.h"
*/

#include <d2d1_2.h>
#include <dwrite.h>
#include <Wincodec.h>
#include "Drawing_API.h"
#include "GmpiApiDrawing.h"
#include "backends/DirectXGfx.h" // GMPI-UI DirectX implementation

namespace se // gmpi
{
namespace directx
{
inline int32_t toReturnCode(HRESULT r)
{
	return r == 0 ? gmpi::MP_OK : gmpi::MP_FAIL;
}

inline HRESULT check_hresult(HRESULT const result)
{
	if (result < 0)
	{
		throw result;
	}
	return result;
}

inline void SafeRelease(IUnknown* object)
{
	if (object)
		object->Release();
}

// Classes without GetFactory()
template<class MpInterface, class DxType>
class GmpiDXWrapper : public MpInterface
{
protected:
	DxType* native_;

	~GmpiDXWrapper()
	{
		if (native_)
		{
			native_->Release();
//					_RPT1(_CRT_WARN, "Release() -> %x\n", (int)native_);
		}
	}

public:
	GmpiDXWrapper(DxType* native = nullptr) : native_(native) {}

	inline DxType* native() const
	{
		return native_;
	}
};

// Classes with GetFactory()
template<class MpInterface, class DxType>
class GmpiDXResourceWrapper : public GmpiDXWrapper<MpInterface, DxType>
{
protected:
	GmpiDrawing_API::IMpFactory* factory_;

public:
	GmpiDXResourceWrapper(DxType* native, GmpiDrawing_API::IMpFactory* factory) : GmpiDXWrapper<MpInterface, DxType>(native), factory_(factory) {}
	GmpiDXResourceWrapper(GmpiDrawing_API::IMpFactory* factory) : factory_(factory) {}

	void GetFactory(GmpiDrawing_API::IMpFactory **factory) override
	{
		*factory = factory_;
	}
};

class Factory_base : public GmpiDrawing_API::IMpFactory2
{
protected:
	gmpi::directx::DxFactoryInfo& info;
	gmpi::IMpUnknown* fallback{};

public:
	Factory_base(gmpi::directx::DxFactoryInfo& pinfo, gmpi::IMpUnknown* pfallback) : info(pinfo), fallback(pfallback) {}

	gmpi::directx::DxFactoryInfo& getInfo() {
		return info;
	}

	// for diagnostics only.
	auto getDirectWriteFactory()
	{
		return info.writeFactory.get();
	}
	auto getWicFactory()
	{
		return info.wicFactory.get();
	}
	ID2D1Factory1* getD2dFactory()
	{
		return info.d2dFactory.get();
	}

	void setSrgbSupport(bool s)
	{
		info.DX_support_sRGB = s;
	}

	GmpiDrawing_API::IMpBitmapPixels::PixelFormat getPlatformPixelFormat()
	{
		return info.DX_support_sRGB ? GmpiDrawing_API::IMpBitmapPixels::kBGRA_SRGB : GmpiDrawing_API::IMpBitmapPixels::kBGRA;
	}

	int32_t CreatePathGeometry(GmpiDrawing_API::IMpPathGeometry** pathGeometry) override;
	int32_t CreateTextFormat(const char* fontFamilyName, void* unused /* fontCollection */, GmpiDrawing_API::MP1_FONT_WEIGHT fontWeight, GmpiDrawing_API::MP1_FONT_STYLE fontStyle, GmpiDrawing_API::MP1_FONT_STRETCH fontStretch, float fontSize, void* unused2 /* localeName */, GmpiDrawing_API::IMpTextFormat** textFormat) override;
	int32_t CreateImage(int32_t width, int32_t height, GmpiDrawing_API::IMpBitmap** returnDiBitmap) override;
	int32_t LoadImageU(const char* utf8Uri, GmpiDrawing_API::IMpBitmap** returnDiBitmap) override;
	int32_t CreateStrokeStyle(const GmpiDrawing_API::MP1_STROKE_STYLE_PROPERTIES* strokeStyleProperties, float* dashes, int32_t dashesCount, GmpiDrawing_API::IMpStrokeStyle** returnValue) override;

	// IMpFactory2
	int32_t GetFontFamilyName(int32_t fontIndex, gmpi::IString* returnString) override;

	int32_t queryInterface(const gmpi::MpGuid& iid, void** returnInterface) override
	{
		*returnInterface = 0;
		if (iid == GmpiDrawing_API::SE_IID_FACTORY2_MPGUI || iid == GmpiDrawing_API::SE_IID_FACTORY_MPGUI || iid == gmpi::MP_IID_UNKNOWN)
		{
			*returnInterface = reinterpret_cast<GmpiDrawing_API::IMpFactory2*>(this);
			addRef();
			return gmpi::MP_OK;
		}

		if (fallback)
		{
			assert(false); // not required?
			return fallback->queryInterface(iid, returnInterface);
		}

		return gmpi::MP_NOSUPPORT;
	}

	GMPI_REFCOUNT_NO_DELETE;
};

class Brush : /* public GmpiDrawing_API::IMpBrush,*/ public GmpiDXResourceWrapper<GmpiDrawing_API::IMpBrush, ID2D1Brush> // Resource
{
public:
	Brush(ID2D1Brush* native, GmpiDrawing_API::IMpFactory* factory) : GmpiDXResourceWrapper(native, factory) {}

	inline ID2D1Brush* nativeBrush()
	{
		return (ID2D1Brush*)native_;
	}
};

class SolidColorBrush final : /* Simulated: public GmpiDrawing_API::IMpSolidColorBrush,*/ public Brush
{
public:
	SolidColorBrush(ID2D1SolidColorBrush* b, GmpiDrawing_API::IMpFactory *factory
	) : Brush(b, factory)
	{}

	inline ID2D1SolidColorBrush* nativeSolidColorBrush()
	{
		return (ID2D1SolidColorBrush*)native_;
	}

	// IMPORTANT: Virtual functions must 100% match simulated interface (GmpiDrawing_API::IMpSolidColorBrush)
	virtual void SetColor(const GmpiDrawing_API::MP1_COLOR* color) // simulated: override
	{
		nativeSolidColorBrush()->SetColor(*(const D2D1_COLOR_F*)color);
	}
	GmpiDrawing_API::MP1_COLOR GetColor() // simulated:  override
	{
		const auto c = nativeSolidColorBrush()->GetColor();
		return *(GmpiDrawing_API::MP1_COLOR*)&c;
	}

	//	GMPI_QUERYINTERFACE1(GmpiDrawing_API::SE_IID_SOLIDCOLORBRUSH_MPGUI, GmpiDrawing_API::IMpSolidColorBrush);
	int32_t queryInterface(const gmpi::MpGuid& iid, void** returnInterface) override
	{
		*returnInterface = 0;
		if (iid == GmpiDrawing_API::SE_IID_SOLIDCOLORBRUSH_MPGUI || iid == gmpi::MP_IID_UNKNOWN)
		{
			// non-standard. Forcing this class (which has the correct vtable) to pretend it's the emulated interface.
			*returnInterface = reinterpret_cast<GmpiDrawing_API::IMpSolidColorBrush*>(this);
			addRef();
			return gmpi::MP_OK;
		}
		return gmpi::MP_NOSUPPORT;
	}

	GMPI_REFCOUNT;
};

class SolidColorBrush_Win7 final : /* Simulated: public GmpiDrawing_API::IMpSolidColorBrush,*/ public Brush
{
public:
	SolidColorBrush_Win7(ID2D1RenderTarget* context, const GmpiDrawing_API::MP1_COLOR* color, GmpiDrawing_API::IMpFactory* factory) : Brush(nullptr, factory)
	{
		const auto nativeColor = gmpi::directx::toNativeWin7((const gmpi::drawing::Color*)color);

		/*HRESULT hr =*/ context->CreateSolidColorBrush(nativeColor, (ID2D1SolidColorBrush**) &native_);
	}

	inline ID2D1SolidColorBrush* nativeSolidColorBrush()
	{
		return (ID2D1SolidColorBrush*)native_;
	}

	// IMPORTANT: Virtual functions must 100% match simulated interface (GmpiDrawing_API::IMpSolidColorBrush)
	virtual void SetColor(const GmpiDrawing_API::MP1_COLOR* color) // simulated: override
	{
		const auto nativeColor = gmpi::directx::toNativeWin7((const gmpi::drawing::Color*)color);
		nativeSolidColorBrush()->SetColor(&nativeColor);
	}

	virtual GmpiDrawing_API::MP1_COLOR GetColor() // simulated:  override
	{
		const auto b = nativeSolidColorBrush()->GetColor();
		return GmpiDrawing_API::MP1_COLOR{ b.r, b.g, b.b, b.a };
	}

	//	GMPI_QUERYINTERFACE1(GmpiDrawing_API::SE_IID_SOLIDCOLORBRUSH_MPGUI, GmpiDrawing_API::IMpSolidColorBrush);
	int32_t queryInterface(const gmpi::MpGuid& iid, void** returnInterface) override
	{
		*returnInterface = 0;
		if (iid == GmpiDrawing_API::SE_IID_SOLIDCOLORBRUSH_MPGUI || iid == gmpi::MP_IID_UNKNOWN)
		{
			// non-standard. Forcing this class (which has the correct vtable) to pretend it's the emulated interface.
			*returnInterface = reinterpret_cast<GmpiDrawing_API::IMpSolidColorBrush*>(this);
			addRef();
			return gmpi::MP_OK;
		}
		return gmpi::MP_NOSUPPORT;
	}

	GMPI_REFCOUNT;
};

class GradientStopCollection final : public GmpiDXResourceWrapper<GmpiDrawing_API::IMpGradientStopCollection, ID2D1GradientStopCollection>
{
public:
	GradientStopCollection(ID2D1GradientStopCollection* native, GmpiDrawing_API::IMpFactory* factory) : GmpiDXResourceWrapper(native, factory) {}

	GMPI_QUERYINTERFACE1(GmpiDrawing_API::SE_IID_GRADIENTSTOPCOLLECTION_MPGUI, GmpiDrawing_API::IMpGradientStopCollection);
	GMPI_REFCOUNT;
};

class GradientStopCollection1 final : public GmpiDXResourceWrapper<GmpiDrawing_API::IMpGradientStopCollection, ID2D1GradientStopCollection1>
{
public:
	GradientStopCollection1(ID2D1GradientStopCollection1* native, GmpiDrawing_API::IMpFactory* factory) : GmpiDXResourceWrapper(native, factory) {}

	GMPI_QUERYINTERFACE1(GmpiDrawing_API::SE_IID_GRADIENTSTOPCOLLECTION_MPGUI, GmpiDrawing_API::IMpGradientStopCollection);
	GMPI_REFCOUNT;
};

class LinearGradientBrush final : /* Simulated: public GmpiDrawing_API::IMpLinearGradientBrush,*/ public Brush
{
public:
	LinearGradientBrush(GmpiDrawing_API::IMpFactory *factory, ID2D1RenderTarget* context, const GmpiDrawing_API::MP1_LINEAR_GRADIENT_BRUSH_PROPERTIES* linearGradientBrushProperties, const GmpiDrawing_API::MP1_BRUSH_PROPERTIES* brushProperties, const  GmpiDrawing_API::IMpGradientStopCollection* gradientStopCollection)
		: Brush(nullptr, factory)
	{
		[[maybe_unused]] HRESULT hr = context->CreateLinearGradientBrush((D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES*)linearGradientBrushProperties, (D2D1_BRUSH_PROPERTIES*)brushProperties, ((GradientStopCollection*)gradientStopCollection)->native(), (ID2D1LinearGradientBrush **)&native_);
		assert(hr == 0);
	}

	inline ID2D1LinearGradientBrush* native()
	{
		return (ID2D1LinearGradientBrush*)native_;
	}

	// IMPORTANT: Virtual functions must 100% match simulated interface (GmpiDrawing_API::IMpLinearGradientBrush)
	virtual void SetStartPoint(GmpiDrawing_API::MP1_POINT startPoint) // simulated: override
	{
		native()->SetStartPoint(*reinterpret_cast<D2D1_POINT_2F*>(&startPoint));
	}
	virtual void SetEndPoint(GmpiDrawing_API::MP1_POINT endPoint) // simulated: override
	{
		native()->SetEndPoint(*reinterpret_cast<D2D1_POINT_2F*>(&endPoint));
	}

	//	GMPI_QUERYINTERFACE1(GmpiDrawing_API::SE_IID_LINEARGRADIENTBRUSH_MPGUI, GmpiDrawing_API::IMpLinearGradientBrush);
	int32_t queryInterface(const gmpi::MpGuid& iid, void** returnInterface) override
	{
		*returnInterface = 0;
		if (iid == GmpiDrawing_API::SE_IID_LINEARGRADIENTBRUSH_MPGUI || iid == gmpi::MP_IID_UNKNOWN)
		{
			// non-standard. Forcing this class (which has the correct vtable) to pretend it's the emulated interface.
			*returnInterface = reinterpret_cast<GmpiDrawing_API::IMpLinearGradientBrush*>(this);
			addRef();
			return gmpi::MP_OK;
		}
		return gmpi::MP_NOSUPPORT;
	}

	GMPI_REFCOUNT;
};

class RadialGradientBrush final : /* Simulated: public GmpiDrawing_API::IMpRadialGradientBrush,*/ public Brush
{
public:
	RadialGradientBrush(GmpiDrawing_API::IMpFactory *factory, ID2D1RenderTarget* context, const GmpiDrawing_API::MP1_RADIAL_GRADIENT_BRUSH_PROPERTIES* linearGradientBrushProperties, const GmpiDrawing_API::MP1_BRUSH_PROPERTIES* brushProperties, const GmpiDrawing_API::IMpGradientStopCollection* gradientStopCollection)
		: Brush(nullptr, factory)
	{
		[[maybe_unused]] HRESULT hr = context->CreateRadialGradientBrush((D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES*)linearGradientBrushProperties, (D2D1_BRUSH_PROPERTIES*)brushProperties, ((GradientStopCollection*)gradientStopCollection)->native(), (ID2D1RadialGradientBrush **)&native_);
		assert(hr == 0);
	}

	inline ID2D1RadialGradientBrush* native()
	{
		return (ID2D1RadialGradientBrush*)native_;
	}

	// IMPORTANT: Virtual functions must 100% match simulated interface.
	virtual void SetCenter(GmpiDrawing_API::MP1_POINT center)  // simulated: override
	{
		native()->SetCenter(*reinterpret_cast<D2D1_POINT_2F*>(&center));
	}

	virtual void SetGradientOriginOffset(GmpiDrawing_API::MP1_POINT gradientOriginOffset) // simulated: override
	{
		native()->SetGradientOriginOffset(*reinterpret_cast<D2D1_POINT_2F*>(&gradientOriginOffset));
	}

	virtual void SetRadiusX(float radiusX) // simulated: override
	{
		native()->SetRadiusX(radiusX);
	}

	virtual void SetRadiusY(float radiusY) // simulated: override
	{
		native()->SetRadiusY(radiusY);
	}

	int32_t queryInterface(const gmpi::MpGuid& iid, void** returnInterface) override
	{
		*returnInterface = 0;
		if (iid == GmpiDrawing_API::SE_IID_RADIALGRADIENTBRUSH_MPGUI || iid == gmpi::MP_IID_UNKNOWN)
		{
			// non-standard. Forcing this class (which has the correct vtable) to pretend it's the emulated interface.
			*returnInterface = reinterpret_cast<GmpiDrawing_API::IMpRadialGradientBrush*>(this);
			addRef();
			return gmpi::MP_OK;
		}
		return gmpi::MP_NOSUPPORT;
	}

	GMPI_REFCOUNT;
};

class StrokeStyle final : public GmpiDXResourceWrapper<GmpiDrawing_API::IMpStrokeStyle, ID2D1StrokeStyle>
{
public:
	StrokeStyle(ID2D1StrokeStyle* native, GmpiDrawing_API::IMpFactory* factory) : GmpiDXResourceWrapper(native, factory) {}

	GmpiDrawing_API::MP1_CAP_STYLE GetStartCap() override
	{
		return (GmpiDrawing_API::MP1_CAP_STYLE) native()->GetStartCap();
	}

	GmpiDrawing_API::MP1_CAP_STYLE GetEndCap() override
	{
		return (GmpiDrawing_API::MP1_CAP_STYLE) native()->GetEndCap();
	}

	GmpiDrawing_API::MP1_CAP_STYLE GetDashCap() override
	{
		return (GmpiDrawing_API::MP1_CAP_STYLE) native()->GetDashCap();
	}

	float GetMiterLimit() override
	{
		return native()->GetMiterLimit();
	}

	GmpiDrawing_API::MP1_LINE_JOIN GetLineJoin() override
	{
		return (GmpiDrawing_API::MP1_LINE_JOIN) native()->GetLineJoin();
	}

	float GetDashOffset() override
	{
		return native()->GetDashOffset();
	}

	GmpiDrawing_API::MP1_DASH_STYLE GetDashStyle() override
	{
		return (GmpiDrawing_API::MP1_DASH_STYLE) native()->GetDashStyle();
	}

	uint32_t GetDashesCount() override
	{
		return native()->GetDashesCount();
	}

	void GetDashes(float* dashes, uint32_t dashesCount) override
	{
		return native()->GetDashes(dashes, dashesCount);
	}

	GMPI_QUERYINTERFACE1(GmpiDrawing_API::SE_IID_STROKESTYLE_MPGUI, GmpiDrawing_API::IMpStrokeStyle);
	GMPI_REFCOUNT;
};

inline ID2D1StrokeStyle* toNative(const GmpiDrawing_API::IMpStrokeStyle* strokeStyle)
{
	if (strokeStyle)
	{
		return ((StrokeStyle*)strokeStyle)->native();
	}
	return nullptr;
}

class TessellationSink final : public GmpiDXWrapper<GmpiDrawing_API::IMpTessellationSink, ID2D1TessellationSink>
{
public:
	TessellationSink(ID2D1Mesh* mesh)
	{
		[[maybe_unused]] HRESULT hr = mesh->Open(&native_);
		assert(hr == S_OK);
	}

	void AddTriangles(const GmpiDrawing_API::MP1_TRIANGLE* triangles, uint32_t trianglesCount) override
	{
		native_->AddTriangles((const D2D1_TRIANGLE*) triangles, trianglesCount);
	}

	int32_t Close() override
	{
		native_->Close();
		return gmpi::MP_OK;
	}

	GMPI_QUERYINTERFACE1(GmpiDrawing_API::SE_IID_TESSELLATIONSINK_MPGUI, GmpiDrawing_API::IMpTessellationSink);
	GMPI_REFCOUNT;
};

#if 0
class Mesh final : public GmpiDXResourceWrapper<GmpiDrawing_API::IMpMesh, ID2D1Mesh>
{
public:
	Mesh(GmpiDrawing_API::IMpFactory* factory, ID2D1RenderTarget* context) :
		GmpiDXResourceWrapper(factory)
	{
		[[maybe_unused]] HRESULT hr = context->CreateMesh(&native_);
		assert(hr == S_OK);
	}

	// IMpMesh
	int32_t Open(GmpiDrawing_API::IMpTessellationSink** returnObject) override
	{
		*returnObject = {};
		gmpi_sdk::mp_shared_ptr<gmpi::IMpUnknown> wrapper;
		wrapper.Attach(new TessellationSink(native_));
		return wrapper->queryInterface(GmpiDrawing_API::SE_IID_TESSELLATIONSINK_MPGUI, reinterpret_cast<void **>(returnObject));
	}

	GMPI_QUERYINTERFACE1(GmpiDrawing_API::SE_IID_MESH_MPGUI, GmpiDrawing_API::IMpMesh);
	GMPI_REFCOUNT;
};
#endif
class TextFormat final : public GmpiDXWrapper<GmpiDrawing_API::IMpTextFormat, IDWriteTextFormat>
{
	bool useLegacyBaseLineSnapping = true;
	float topAdjustment = {};
	float fontMetrics_ascent = {};
	IDWriteFactory* writeFactory{};

	void CalculateTopAdjustment()
	{
		assert(topAdjustment == 0.0f); // else boundingBoxSize calculation will be affected, and won't be actual native size.

		GmpiDrawing_API::MP1_FONT_METRICS fontMetrics;
		GetFontMetrics(&fontMetrics);

		GmpiDrawing_API::MP1_SIZE boundingBoxSize;
		GetTextExtentU("A", 1, &boundingBoxSize);

		topAdjustment = boundingBoxSize.height - (fontMetrics.ascent + fontMetrics.descent);
		fontMetrics_ascent = fontMetrics.ascent;
	}

public:
	TextFormat(IDWriteFactory* pwriteFactory, IDWriteTextFormat* native) :
		GmpiDXWrapper<GmpiDrawing_API::IMpTextFormat, IDWriteTextFormat>(native)
		, writeFactory(pwriteFactory)
	{
		CalculateTopAdjustment();
	}

	int32_t SetTextAlignment(GmpiDrawing_API::MP1_TEXT_ALIGNMENT textAlignment) override
	{
		native()->SetTextAlignment((DWRITE_TEXT_ALIGNMENT)textAlignment);
		return gmpi::MP_OK;
	}

	int32_t SetParagraphAlignment(GmpiDrawing_API::MP1_PARAGRAPH_ALIGNMENT paragraphAlignment) override
	{
		native()->SetParagraphAlignment((DWRITE_PARAGRAPH_ALIGNMENT)paragraphAlignment);
		return gmpi::MP_OK;
	}

	int32_t SetWordWrapping(GmpiDrawing_API::MP1_WORD_WRAPPING wordWrapping) override
	{
		return native()->SetWordWrapping((DWRITE_WORD_WRAPPING)wordWrapping);
	}

	int32_t SetLineSpacing(float lineSpacing, float baseline) override
	{
		// Hack, reuse this method to enable legacy-mode.
		if (static_cast<float>(IMpTextFormat::ImprovedVerticalBaselineSnapping) == lineSpacing)
		{
			useLegacyBaseLineSnapping = false;
			return gmpi::MP_OK;
		}

		// For the default method, spacing depends solely on the content. For uniform spacing, the specified line height overrides the content.
		DWRITE_LINE_SPACING_METHOD method = lineSpacing < 0.0f ? DWRITE_LINE_SPACING_METHOD_DEFAULT : DWRITE_LINE_SPACING_METHOD_UNIFORM;
		return native()->SetLineSpacing(method, fabsf(lineSpacing), baseline);
	}

	int32_t GetFontMetrics(GmpiDrawing_API::MP1_FONT_METRICS* returnFontMetrics) override;

	// TODO!!!: Probably needs to accept constraint rect like DirectWrite. !!!
	//	void GetTextExtentU(const char* utf8String, int32_t stringLength, GmpiDrawing::Size& returnSize)
	void GetTextExtentU(const char* utf8String, int32_t stringLength, GmpiDrawing_API::MP1_SIZE* returnSize) override;

	float getTopAdjustment() const
	{
		return topAdjustment;
	}

	float getAscent() const
	{
		return fontMetrics_ascent;
	}

	bool getUseLegacyBaseLineSnapping() const
	{
		return useLegacyBaseLineSnapping;
	}

	GMPI_QUERYINTERFACE1(GmpiDrawing_API::SE_IID_TEXTFORMAT_MPGUI, GmpiDrawing_API::IMpTextFormat);
	GMPI_REFCOUNT;
};

class BitmapPixels final : public GmpiDrawing_API::IMpBitmapPixels
{
	gmpi::directx::ComPtr<IWICBitmap> bitmap;
    gmpi::directx::ComPtr<IWICBitmapLock> pBitmapLock;
    bool alphaPremultiplied{};
	UINT bytesPerRow{};
	BYTE* ptr{};
	int flags{};
	IMpBitmapPixels::PixelFormat pixelFormat = kBGRA; // default to non-SRGB Win7 (not tested)

public:
	BitmapPixels(IWICBitmap* inBitmap, bool _alphaPremultiplied, int32_t pflags)
	{
		assert(inBitmap);
		UINT w{}, h{};
		inBitmap->GetSize(&w, &h);

		{
			WICPixelFormatGUID formatGuid{};
			inBitmap->GetPixelFormat(&formatGuid);

			// premultiplied BGRA (default)
			if (std::memcmp(&formatGuid, &GUID_WICPixelFormat32bppPBGRA, sizeof(formatGuid)) == 0)
			{
				pixelFormat = kBGRA_SRGB;
			}
		}

		WICRect rcLock = { 0, 0, (INT)w, (INT)h };
		flags = pflags;

        if (0 <= inBitmap->Lock(&rcLock, flags, pBitmapLock.put()))
		{
			pBitmapLock->GetStride(&bytesPerRow);
			UINT bufferSize{};
			pBitmapLock->GetDataPointer(&bufferSize, &ptr);

			bitmap = inBitmap;

			alphaPremultiplied = _alphaPremultiplied;
			if (!alphaPremultiplied)
				gmpi::directx::unpremultiplyAlpha(pBitmapLock.get());
		}
		else
		{
			alphaPremultiplied = true; // prevent possible null deference of 'bitmap' in destructor
		}
	}

    ~BitmapPixels()
	{
		if (!alphaPremultiplied)
			gmpi::directx::premultiplyAlpha(pBitmapLock.get());
	}

	virtual uint8_t* getAddress() const override { return ptr; }
	int32_t getBytesPerRow() const override{ return bytesPerRow; }
	int32_t getPixelFormat() const override
	{
		return pixelFormat;
	}

	GMPI_QUERYINTERFACE1(GmpiDrawing_API::SE_IID_BITMAP_PIXELS_MPGUI, GmpiDrawing_API::IMpBitmapPixels);
	GMPI_REFCOUNT;
};

class Bitmap : public GmpiDrawing_API::IMpBitmap
{
public:
	gmpi::directx::ComPtr<ID2D1Bitmap> nativeBitmap_;
	gmpi::directx::ComPtr<IWICBitmap> diBitmap_;
	ID2D1DeviceContext* nativeContext_ = {};
	se::directx::Factory_base factory;
	GmpiDrawing_API::IMpBitmapPixels::PixelFormat pixelFormat_ = GmpiDrawing_API::IMpBitmapPixels::kBGRA_SRGB;
#ifdef _DEBUG
	std::string debugFilename;
#endif
	Bitmap(gmpi::directx::DxFactoryInfo& factoryInfo, GmpiDrawing_API::IMpBitmapPixels::PixelFormat pixelFormat, IWICBitmap* diBitmap);

	Bitmap(gmpi::directx::DxFactoryInfo& factoryInfo, GmpiDrawing_API::IMpBitmapPixels::PixelFormat pixelFormat, ID2D1DeviceContext* nativeContext, ID2D1Bitmap* nativeBitmap) :
			nativeContext_(nativeContext)
		, factory(factoryInfo, nullptr)
		, pixelFormat_(pixelFormat)
	{
		nativeBitmap_ = nativeBitmap;
	}

	ID2D1Bitmap* GetNativeBitmap(ID2D1DeviceContext* nativeContext);

	GmpiDrawing_API::MP1_SIZE GetSizeF() override
	{
		if (diBitmap_)
		{
			UINT width{}, height{};
			diBitmap_->GetSize(&width, &height);
			return { (float)width, (float)height };
		}
		else if (nativeBitmap_)
		{
			const auto sizef = nativeBitmap_->GetSize();
			return { sizef.width, sizef.height };
		}

		return {};
	}

	int32_t GetSize(GmpiDrawing_API::MP1_SIZE_U* returnSize) override
	{
		if (diBitmap_)
		{
			diBitmap_->GetSize(&returnSize->width, &returnSize->height);
		}
		else if (nativeBitmap_)
		{
			const auto sizef = nativeBitmap_->GetSize();
			returnSize->width = (uint32_t)sizef.width;
			returnSize->height = (uint32_t)sizef.height;
		}
		else
		{
			*returnSize = {};
			return gmpi::MP_FAIL;
		}

		return gmpi::MP_OK;
	}

	int32_t lockPixelsOld(GmpiDrawing_API::IMpBitmapPixels** returnInterface, bool alphaPremultiplied) override
	{
		*returnInterface = 0;

		// invalidate device bitmap (will be automatically recreated as needed)
		nativeBitmap_ = {};

		gmpi_sdk::mp_shared_ptr<gmpi::IMpUnknown> b2;
		b2.Attach(new BitmapPixels(diBitmap_, alphaPremultiplied, GmpiDrawing_API::MP1_BITMAP_LOCK_READ | GmpiDrawing_API::MP1_BITMAP_LOCK_WRITE));

		return b2->queryInterface(GmpiDrawing_API::SE_IID_BITMAP_PIXELS_MPGUI, (void**)(returnInterface));
	}

	int32_t lockPixels(GmpiDrawing_API::IMpBitmapPixels** returnInterface, int32_t flags) override;

	void ApplyAlphaCorrection() override{} // deprecated

	void GetFactory(GmpiDrawing_API::IMpFactory** pfactory) override;

	GMPI_QUERYINTERFACE1(GmpiDrawing_API::SE_IID_BITMAP_MPGUI, GmpiDrawing_API::IMpBitmap);
	GMPI_REFCOUNT;
};

class BitmapBrush final : /* Simulated: public GmpiDrawing_API::IMpBitmapBrush,*/ public Brush
{
public:
	BitmapBrush(
		GmpiDrawing_API::IMpFactory *factory,
		ID2D1DeviceContext* context,
		const GmpiDrawing_API::IMpBitmap* bitmap,
		const GmpiDrawing_API::MP1_BITMAP_BRUSH_PROPERTIES* bitmapBrushProperties,
		const GmpiDrawing_API::MP1_BRUSH_PROPERTIES* brushProperties
	)
		: Brush(nullptr, factory)
	{
		auto bm = ((Bitmap*)bitmap);
		auto nativeBitmap = bm->GetNativeBitmap(context);

		[[maybe_unused]] const auto hr = context->CreateBitmapBrush(nativeBitmap, (D2D1_BITMAP_BRUSH_PROPERTIES*)bitmapBrushProperties, (D2D1_BRUSH_PROPERTIES*)brushProperties, (ID2D1BitmapBrush**)&native_);
		assert(hr == 0);
	}

	inline ID2D1BitmapBrush* native()
	{
		return (ID2D1BitmapBrush*)native_;
	}

	// IMPORTANT: Virtual functions must 100% match simulated interface.
	virtual void SetExtendModeX(GmpiDrawing_API::MP1_EXTEND_MODE extendModeX)
	{
		native()->SetExtendModeX((D2D1_EXTEND_MODE)extendModeX);
	}

	virtual void SetExtendModeY(GmpiDrawing_API::MP1_EXTEND_MODE extendModeY)
	{
		native()->SetExtendModeY((D2D1_EXTEND_MODE)extendModeY);
	}

	virtual void SetInterpolationMode(GmpiDrawing_API::MP1_BITMAP_INTERPOLATION_MODE interpolationMode)
	{
		native()->SetInterpolationMode((D2D1_BITMAP_INTERPOLATION_MODE)interpolationMode);
	}

	int32_t queryInterface(const gmpi::MpGuid& iid, void** returnInterface) override
	{
		*returnInterface = 0;
		if (iid == GmpiDrawing_API::SE_IID_BITMAPBRUSH_MPGUI || iid == gmpi::MP_IID_UNKNOWN)
		{
			// non-standard. Forcing this class (which has the correct vtable) to pretend it's the emulated interface.
			*returnInterface = reinterpret_cast<GmpiDrawing_API::IMpLinearGradientBrush*>(this);
			addRef();
			return gmpi::MP_OK;
		}
		return gmpi::MP_NOSUPPORT;
	}

	GMPI_REFCOUNT;
};

class GeometrySink : public GmpiDrawing_API::IMpGeometrySink2
{
	ID2D1GeometrySink* geometrysink_;

public:
	GeometrySink(ID2D1GeometrySink* context) : geometrysink_(context) {}
	~GeometrySink()
	{
		if (geometrysink_)
		{
			geometrysink_->Release();

		}
	}
	void SetFillMode(GmpiDrawing_API::MP1_FILL_MODE fillMode) override
	{
		geometrysink_->SetFillMode((D2D1_FILL_MODE)fillMode);
	}
	void BeginFigure(GmpiDrawing_API::MP1_POINT startPoint, GmpiDrawing_API::MP1_FIGURE_BEGIN figureBegin) override
	{
		geometrysink_->BeginFigure(*reinterpret_cast<D2D1_POINT_2F*>(&startPoint), (D2D1_FIGURE_BEGIN)figureBegin);
	}
	void AddLines(const GmpiDrawing_API::MP1_POINT* points, uint32_t pointsCount) override
	{
		geometrysink_->AddLines(reinterpret_cast<const D2D1_POINT_2F*>(points), pointsCount);
	}
	void AddBeziers(const GmpiDrawing_API::MP1_BEZIER_SEGMENT* beziers, uint32_t beziersCount) override
	{
		geometrysink_->AddBeziers(reinterpret_cast<const D2D1_BEZIER_SEGMENT*>(beziers), beziersCount);
	}
	void EndFigure(GmpiDrawing_API::MP1_FIGURE_END figureEnd) override
	{
		geometrysink_->EndFigure((D2D1_FIGURE_END)figureEnd);
	}
	int32_t Close() override
	{
		auto hr = geometrysink_->Close();
		return toReturnCode(hr);
	}
	void AddLine(GmpiDrawing_API::MP1_POINT point) override
	{
		geometrysink_->AddLine(*reinterpret_cast<D2D1_POINT_2F*>(&point));
	}
	void AddBezier(const GmpiDrawing_API::MP1_BEZIER_SEGMENT* bezier) override
	{
		geometrysink_->AddBezier(reinterpret_cast<const D2D1_BEZIER_SEGMENT*>(bezier));
	}
	void AddQuadraticBezier(const GmpiDrawing_API::MP1_QUADRATIC_BEZIER_SEGMENT* bezier) override
	{
		geometrysink_->AddQuadraticBezier(reinterpret_cast<const D2D1_QUADRATIC_BEZIER_SEGMENT*>(bezier));
	}
	void AddQuadraticBeziers(const GmpiDrawing_API::MP1_QUADRATIC_BEZIER_SEGMENT* beziers, uint32_t beziersCount) override
	{
		geometrysink_->AddQuadraticBeziers(reinterpret_cast<const D2D1_QUADRATIC_BEZIER_SEGMENT*>(beziers), beziersCount);
	}
	void AddArc(const GmpiDrawing_API::MP1_ARC_SEGMENT* arc) override
	{
		geometrysink_->AddArc(reinterpret_cast<const D2D1_ARC_SEGMENT*>(arc));
	}

	int32_t queryInterface(const gmpi::MpGuid& iid, void** returnInterface) override
	{
		if (iid == GmpiDrawing_API::SE_IID_GEOMETRYSINK2_MPGUI || iid == GmpiDrawing_API::SE_IID_GEOMETRYSINK_MPGUI || iid == gmpi::MP_IID_UNKNOWN)
		{
			*returnInterface = reinterpret_cast<void*>(static_cast<GmpiDrawing_API::IMpGeometrySink2*>(this));
			addRef();
			return gmpi::MP_OK;
		}

		*returnInterface = 0;
		return gmpi::MP_NOSUPPORT;
	}
	GMPI_REFCOUNT;
};


class Geometry : public GmpiDrawing_API::IMpPathGeometry
{
	friend class GraphicsContext_SDK3;

	ID2D1PathGeometry* geometry_;

public:
	Geometry(ID2D1PathGeometry* context) : geometry_(context)
	{}
	~Geometry()
	{
		if (geometry_)
		{
			geometry_->Release();
		}
	}

	ID2D1PathGeometry* native()
	{
		return geometry_;
	}

	int32_t Open(GmpiDrawing_API::IMpGeometrySink** geometrySink) override;
	void GetFactory(GmpiDrawing_API::IMpFactory** factory) override
	{
		//		native_->GetFactory((ID2D1Factory**)factory);
	}

	int32_t StrokeContainsPoint(GmpiDrawing_API::MP1_POINT point, float strokeWidth, GmpiDrawing_API::IMpStrokeStyle* strokeStyle, const GmpiDrawing_API::MP1_MATRIX_3X2* worldTransform, bool* returnContains) override
	{
		BOOL result = FALSE;
		geometry_->StrokeContainsPoint(*(D2D1_POINT_2F*)&point, strokeWidth, toNative(strokeStyle), (const D2D1_MATRIX_3X2_F *)worldTransform, &result);
		*returnContains = result == TRUE;

		return gmpi::MP_OK;
	}
	int32_t FillContainsPoint(GmpiDrawing_API::MP1_POINT point, const GmpiDrawing_API::MP1_MATRIX_3X2* worldTransform, bool* returnContains) override
	{
		BOOL result = FALSE;
		geometry_->FillContainsPoint(*(D2D1_POINT_2F*)&point, (const D2D1_MATRIX_3X2_F *)worldTransform, &result);
		*returnContains = result == TRUE;

		return gmpi::MP_OK;
	}
	int32_t GetWidenedBounds(float strokeWidth, GmpiDrawing_API::IMpStrokeStyle* strokeStyle, const GmpiDrawing_API::MP1_MATRIX_3X2* worldTransform, GmpiDrawing_API::MP1_RECT* returnBounds) override
	{
		geometry_->GetWidenedBounds(strokeWidth, toNative(strokeStyle), (const D2D1_MATRIX_3X2_F *)worldTransform, (D2D_RECT_F*)returnBounds);
		return gmpi::MP_OK;
	}

	GMPI_QUERYINTERFACE1(GmpiDrawing_API::SE_IID_PATHGEOMETRY_MPGUI, GmpiDrawing_API::IMpPathGeometry);
	GMPI_REFCOUNT;
};

class Factory_SDK3 : public Factory_base
{
	gmpi::directx::DxFactoryInfo concreteInfo;

public:
	Factory_SDK3(gmpi::IMpUnknown* pfallback) : Factory_base(concreteInfo, pfallback){}
	void Init();
};

// GMPI-UI version
class GraphicsContext_RG : public gmpi::directx::GraphicsContext_base
{
protected:
	ID2D1DeviceContext* context_{};
	gmpi::directx::Factory_base factory;
	gmpi::api::IUnknown* fallback{};

public:
	GraphicsContext_RG(gmpi::api::IUnknown* pfallback, gmpi::directx::DxFactoryInfo& pinfo, ID2D1DeviceContext* deviceContext) :
		gmpi::directx::GraphicsContext_base(&factory, deviceContext)
		, context_(deviceContext)
		, factory(pinfo, nullptr)
		, fallback(pfallback)
	{
	}

	gmpi::ReturnCode queryInterface(const gmpi::api::Guid* iid, void** returnInterface) override {
		*returnInterface = {};
		if ((*iid) == gmpi::drawing::api::IDeviceContext::guid) {
			*returnInterface = static_cast<gmpi::drawing::api::IDeviceContext*>(this); addRef();
			return gmpi::ReturnCode::Ok;
		}
		if ((*iid) == gmpi::drawing::api::IResource::guid) {
			*returnInterface = static_cast<gmpi::drawing::api::IResource*>(this); addRef();
			return gmpi::ReturnCode::Ok;
		}

		if(fallback)
			return fallback->queryInterface(iid, returnInterface);

		return gmpi::ReturnCode::NoSupport;
	}
	GMPI_REFCOUNT_NO_DELETE;
};

// Classic version
class GraphicsContext_SDK3 : public GmpiDrawing_API::IMpDeviceContext
{
protected:
	gmpi::directx::ComPtr<ID2D1DeviceContext> context_;
	Factory_base factory;
	gmpi::IMpUnknown* fallback{};

	std::vector<GmpiDrawing_API::MP1_RECT> clipRectStack;
public:
	GraphicsContext_SDK3(gmpi::IMpUnknown* pfallback, gmpi::directx::DxFactoryInfo& pinfo, ID2D1DeviceContext* deviceContext = 0) :
		context_(deviceContext)
		, factory(pinfo, nullptr)
		, fallback(pfallback)
	{
		// gmpiContext inits it's clip rect, SDK3 does not.
		const float defaultClipBounds = 100000.0f;
		GmpiDrawing_API::MP1_RECT r;
		r.top = r.left = -defaultClipBounds;
		r.bottom = r.right = defaultClipBounds;
		clipRectStack.push_back(r);
	}

	~GraphicsContext_SDK3(){}

	ID2D1DeviceContext* native()
	{
		return context_;
	}

	void GetFactory(GmpiDrawing_API::IMpFactory** pfactory) override
	{
		*pfactory = &factory;
	}

	void DrawRectangle(const GmpiDrawing_API::MP1_RECT* rect, const GmpiDrawing_API::IMpBrush* brush, float strokeWidth, const GmpiDrawing_API::IMpStrokeStyle* strokeStyle) override
	{
		context_->DrawRectangle(D2D1::RectF(rect->left, rect->top, rect->right, rect->bottom), ((Brush*)brush)->nativeBrush(), strokeWidth, toNative(strokeStyle) );
	}

	void FillRectangle(const GmpiDrawing_API::MP1_RECT* rect, const GmpiDrawing_API::IMpBrush* brush) override
	{
		context_->FillRectangle((D2D1_RECT_F*)rect, (ID2D1Brush*)((Brush*)brush)->nativeBrush());
	}

	void Clear(const GmpiDrawing_API::MP1_COLOR* clearColor) override
	{
		native()->Clear(*(D2D1_COLOR_F*)clearColor);
	}

	void DrawLine(GmpiDrawing_API::MP1_POINT point0, GmpiDrawing_API::MP1_POINT point1, const GmpiDrawing_API::IMpBrush* brush, float strokeWidth, const GmpiDrawing_API::IMpStrokeStyle* strokeStyle) override
	{
		context_->DrawLine(*((D2D_POINT_2F*)&point0), *((D2D_POINT_2F*)&point1), ((Brush*)brush)->nativeBrush(), strokeWidth, toNative(strokeStyle));
	}

	void DrawGeometry(const GmpiDrawing_API::IMpPathGeometry* geometry, const GmpiDrawing_API::IMpBrush* brush, float strokeWidth = 1.0f, const GmpiDrawing_API::IMpStrokeStyle* strokeStyle = 0) override;

	void FillGeometry(const GmpiDrawing_API::IMpPathGeometry* geometry, const GmpiDrawing_API::IMpBrush* brush, const GmpiDrawing_API::IMpBrush* opacityBrush) override
	{
		auto d2d_geometry = ((Geometry*)geometry)->geometry_;

		ID2D1Brush* opacityBrushNative;
		if (opacityBrush)
		{
			opacityBrushNative = ((Brush*)brush)->nativeBrush();
		}
		else
		{
			opacityBrushNative = {};
		}

		context_->FillGeometry(d2d_geometry, ((Brush*)brush)->nativeBrush(), opacityBrushNative);
	}

	void DrawTextU(const char* utf8String, int32_t stringLength, const GmpiDrawing_API::IMpTextFormat* textFormat, const GmpiDrawing_API::MP1_RECT* layoutRect, const GmpiDrawing_API::IMpBrush* brush, int32_t flags) override;

	void DrawBitmap(const GmpiDrawing_API::IMpBitmap* mpBitmap, const GmpiDrawing_API::MP1_RECT* destinationRectangle, float opacity, /* MP1_BITMAP_INTERPOLATION_MODE*/ int32_t interpolationMode, const GmpiDrawing_API::MP1_RECT* sourceRectangle) override
	{
		auto bm = ((Bitmap*)mpBitmap);
		auto bitmap = bm->GetNativeBitmap(context_);
		if (bitmap)
		{
			context_->DrawBitmap(
				bitmap,
				(D2D1_RECT_F*)destinationRectangle,
				opacity,
				(D2D1_BITMAP_INTERPOLATION_MODE) interpolationMode,
				(D2D1_RECT_F*)sourceRectangle
			);
		}
	}

	void SetTransform(const GmpiDrawing_API::MP1_MATRIX_3X2* transform) override
	{
		context_->SetTransform(reinterpret_cast<const D2D1_MATRIX_3X2_F*>(transform));
	}

	void GetTransform(GmpiDrawing_API::MP1_MATRIX_3X2* transform) override
	{
		context_->GetTransform(reinterpret_cast<D2D1_MATRIX_3X2_F*>(transform));
	}

	int32_t CreateSolidColorBrush(const GmpiDrawing_API::MP1_COLOR* color, GmpiDrawing_API::IMpSolidColorBrush **solidColorBrush) override;

	int32_t CreateGradientStopCollection(const GmpiDrawing_API::MP1_GRADIENT_STOP* gradientStops, uint32_t gradientStopsCount, /* GmpiDrawing_API::MP1_GAMMA colorInterpolationGamma, GmpiDrawing_API::MP1_EXTEND_MODE extendMode,*/ GmpiDrawing_API::IMpGradientStopCollection** gradientStopCollection) override;

	template <typename T>
	int32_t make_wrapped(gmpi::IMpUnknown* object, const gmpi::MpGuid& iid, T** returnObject)
	{
		*returnObject = {};
		gmpi_sdk::mp_shared_ptr<gmpi::IMpUnknown> b2;
		b2.Attach(object);
		return b2->queryInterface(iid, reinterpret_cast<void**>(returnObject));
	};

	int32_t CreateLinearGradientBrush(const GmpiDrawing_API::MP1_LINEAR_GRADIENT_BRUSH_PROPERTIES* linearGradientBrushProperties, const GmpiDrawing_API::MP1_BRUSH_PROPERTIES* brushProperties, const  GmpiDrawing_API::IMpGradientStopCollection* gradientStopCollection, GmpiDrawing_API::IMpLinearGradientBrush** linearGradientBrush) override
	{
		return make_wrapped(
			new LinearGradientBrush(&factory, context_, linearGradientBrushProperties, brushProperties, gradientStopCollection),
			GmpiDrawing_API::SE_IID_LINEARGRADIENTBRUSH_MPGUI,
			linearGradientBrush);
	}

	int32_t CreateBitmapBrush(const GmpiDrawing_API::IMpBitmap* bitmap, const GmpiDrawing_API::MP1_BITMAP_BRUSH_PROPERTIES* bitmapBrushProperties, const GmpiDrawing_API::MP1_BRUSH_PROPERTIES* brushProperties, GmpiDrawing_API::IMpBitmapBrush** returnBrush) override
	{
		*returnBrush = {};
		gmpi_sdk::mp_shared_ptr<gmpi::IMpUnknown> b2;
		b2.Attach(new BitmapBrush(&factory, context_, bitmap, bitmapBrushProperties, brushProperties));
		return b2->queryInterface(GmpiDrawing_API::SE_IID_BITMAPBRUSH_MPGUI, reinterpret_cast<void **>(returnBrush));
	}
	int32_t CreateRadialGradientBrush(const GmpiDrawing_API::MP1_RADIAL_GRADIENT_BRUSH_PROPERTIES* radialGradientBrushProperties, const GmpiDrawing_API::MP1_BRUSH_PROPERTIES* brushProperties, const GmpiDrawing_API::IMpGradientStopCollection* gradientStopCollection, GmpiDrawing_API::IMpRadialGradientBrush** radialGradientBrush) override
	{
		*radialGradientBrush = {};
		gmpi_sdk::mp_shared_ptr<gmpi::IMpUnknown> b2;
		b2.Attach(new RadialGradientBrush(&factory, context_, radialGradientBrushProperties, brushProperties, gradientStopCollection));
		return b2->queryInterface(GmpiDrawing_API::SE_IID_RADIALGRADIENTBRUSH_MPGUI, reinterpret_cast<void **>(radialGradientBrush));
	}

	int32_t CreateCompatibleRenderTarget(const GmpiDrawing_API::MP1_SIZE* desiredSize, GmpiDrawing_API::IMpBitmapRenderTarget** bitmapRenderTarget) override;

	void DrawRoundedRectangle(const GmpiDrawing_API::MP1_ROUNDED_RECT* roundedRect, const GmpiDrawing_API::IMpBrush* brush, float strokeWidth, const GmpiDrawing_API::IMpStrokeStyle* strokeStyle) override
	{
		context_->DrawRoundedRectangle((D2D1_ROUNDED_RECT*)roundedRect, (ID2D1Brush*)((Brush*)brush)->nativeBrush(), (FLOAT)strokeWidth, toNative(strokeStyle));
	}

	void FillRoundedRectangle(const GmpiDrawing_API::MP1_ROUNDED_RECT* roundedRect, const GmpiDrawing_API::IMpBrush* brush) override
	{
		context_->FillRoundedRectangle((D2D1_ROUNDED_RECT*)roundedRect, (ID2D1Brush*)((Brush*)brush)->nativeBrush());
	}

	void DrawEllipse(const GmpiDrawing_API::MP1_ELLIPSE* ellipse, const GmpiDrawing_API::IMpBrush* brush, float strokeWidth, const GmpiDrawing_API::IMpStrokeStyle* strokeStyle) override
	{
		context_->DrawEllipse((D2D1_ELLIPSE*)ellipse, (ID2D1Brush*)((Brush*)brush)->nativeBrush(), (FLOAT)strokeWidth, toNative(strokeStyle));
	}

	void FillEllipse(const GmpiDrawing_API::MP1_ELLIPSE* ellipse, const GmpiDrawing_API::IMpBrush* brush) override
	{
		context_->FillEllipse((D2D1_ELLIPSE*)ellipse, (ID2D1Brush*)((Brush*)brush)->nativeBrush());
	}

	void PushAxisAlignedClip(const GmpiDrawing_API::MP1_RECT* clipRect/*, GmpiDrawing_API::MP1_ANTIALIAS_MODE antialiasMode*/) override;

	void PopAxisAlignedClip() override
	{
		context_->PopAxisAlignedClip();
		clipRectStack.pop_back();
	}

	void GetAxisAlignedClip(GmpiDrawing_API::MP1_RECT* returnClipRect) override;

	void BeginDraw() override
	{
		context_->BeginDraw();
	}

	int32_t EndDraw() override
	{
		auto hr = context_->EndDraw();

		return hr == S_OK ? gmpi::MP_OK : gmpi::MP_FAIL;
	}

	//	void InsetNewMethodHere(){}

	bool SupportSRGB()
	{
		return factory.getPlatformPixelFormat() == GmpiDrawing_API::IMpBitmapPixels::kBGRA_SRGB;
	}

	int32_t queryInterface(const gmpi::MpGuid& iid, void** returnInterface) override
	{
		*returnInterface = 0;
		if (iid == GmpiDrawing_API::SE_IID_DEVICECONTEXT_MPGUI || iid == gmpi::MP_IID_UNKNOWN)
		{
			*returnInterface = static_cast<GmpiDrawing_API::IMpDeviceContext*>(this);
			addRef();
			return gmpi::MP_OK;
		}

		if (fallback)
			return fallback->queryInterface(iid, returnInterface);

		return gmpi::MP_NOSUPPORT;
	}
	GMPI_REFCOUNT_NO_DELETE;
};

class GraphicsContext2 : public GraphicsContext_SDK3, public GmpiDrawing_API::IMpDeviceContextExt
{
public:
	GraphicsContext2(gmpi::IMpUnknown* pfallback, gmpi::directx::DxFactoryInfo& pinfo, ID2D1DeviceContext* deviceContext = {}) : GraphicsContext_SDK3(pfallback, pinfo, deviceContext){}

	int32_t MP_STDCALL CreateBitmapRenderTarget(GmpiDrawing_API::MP1_SIZE_L desiredSize, bool enableLockPixels, GmpiDrawing_API::IMpBitmapRenderTarget** returnObject) override;

	int32_t MP_STDCALL queryInterface(const gmpi::MpGuid& iid, void** returnInterface) override
	{
		*returnInterface = {};
		if (iid == GmpiDrawing_API::IMpDeviceContextExt::guid)
		{
			*returnInterface = static_cast<GmpiDrawing_API::IMpDeviceContextExt*>(this);
			addRef();
			return gmpi::MP_OK;
		}

		return GraphicsContext_SDK3::queryInterface(iid, returnInterface);
	}
	GMPI_REFCOUNT_NO_DELETE;
};

class BitmapRenderTarget : public GraphicsContext_SDK3
{
	gmpi::directx::ComPtr<IWICBitmap> wicBitmap;
	ID2D1DeviceContext* originalContext{};

public:

	// Create on GPU only
	BitmapRenderTarget(GraphicsContext_SDK3* g, GmpiDrawing_API::MP1_SIZE desiredSize, gmpi::directx::DxFactoryInfo& info, bool enableLockPixels = false);

	~BitmapRenderTarget(){}

	// HACK, to be ABI compatible with IMpBitmapRenderTarget we need this virtual function,
	// and it needs to be in the vtable right after all virtual functions of GraphicsContext
	virtual int32_t GetBitmap(GmpiDrawing_API::IMpBitmap** returnBitmap);

	int32_t queryInterface(const gmpi::MpGuid& iid, void** returnInterface) override
	{
		*returnInterface = 0;
		if (iid == GmpiDrawing_API::SE_IID_BITMAP_RENDERTARGET_MPGUI)
		{
			// non-standard. Forcing this class (which has the correct vtable) to pretend it's the emulated interface.
			*returnInterface = reinterpret_cast<GmpiDrawing_API::IMpBitmapRenderTarget*>(this);
			addRef();
			return gmpi::MP_OK;
		}
		return GraphicsContext_SDK3::queryInterface(iid, returnInterface);
	}

	GMPI_REFCOUNT;
};

// Direct2D context tailored to devices without sRGB high-color support. i.e. Windows 7.
class GraphicsContext_Win7 : public GraphicsContext2
{
public:

	GraphicsContext_Win7(gmpi::IMpUnknown* pfallback, gmpi::directx::DxFactoryInfo& pinfo, ID2D1DeviceContext* context) :
		GraphicsContext2(pfallback, pinfo, context)
	{}

	int32_t CreateSolidColorBrush(const GmpiDrawing_API::MP1_COLOR* color, GmpiDrawing_API::IMpSolidColorBrush **solidColorBrush) override
	{
		*solidColorBrush = {};
		gmpi_sdk::mp_shared_ptr<gmpi::IMpUnknown> b;
		b.Attach(new SolidColorBrush_Win7(context_, color, &factory));
		return b->queryInterface(GmpiDrawing_API::SE_IID_SOLIDCOLORBRUSH_MPGUI, reinterpret_cast<void **>(solidColorBrush));
	}

	int32_t CreateGradientStopCollection(const GmpiDrawing_API::MP1_GRADIENT_STOP* gradientStops, uint32_t gradientStopsCount, /* GmpiDrawing_API::MP1_GAMMA colorInterpolationGamma, GmpiDrawing_API::MP1_EXTEND_MODE extendMode,*/ GmpiDrawing_API::IMpGradientStopCollection** gradientStopCollection) override
	{
		// Adjust gradient gamma.
		std::vector<GmpiDrawing_API::MP1_GRADIENT_STOP> stops;
		stops.assign(gradientStopsCount, GmpiDrawing_API::MP1_GRADIENT_STOP());

		for(uint32_t i = 0 ; i < gradientStopsCount ; ++i)
		{
			auto& srce = gradientStops[i];
			auto& dest = stops[i];
			dest.position = srce.position;

			*(D2D1_COLOR_F*) &dest.color = gmpi::directx::toNativeWin7((const gmpi::drawing::Color*)&srce.color);
		}

		return GraphicsContext_SDK3::CreateGradientStopCollection(stops.data(), gradientStopsCount, gradientStopCollection);
	}

	void Clear(const GmpiDrawing_API::MP1_COLOR* color) override
	{
		const auto native = gmpi::directx::toNativeWin7((const gmpi::drawing::Color*)color);
		context_->Clear(&native);
	}
};

struct UniversalGraphicsContext : public gmpi::api::IUnknown
{
	GraphicsContext_RG gmpiContext;
	GraphicsContext2 sdk3Context;

	UniversalGraphicsContext(gmpi::directx::DxFactoryInfo& pinfo, ID2D1DeviceContext* nativeContext = {}) :
		gmpiContext(this, pinfo, nativeContext),
		sdk3Context((gmpi::IMpUnknown*) static_cast<gmpi::api::IUnknown*>(this), pinfo, nativeContext)
	{
	}

	gmpi::ReturnCode queryInterface(const gmpi::api::Guid* iid, void** returnInterface) override
	{
		*returnInterface = {};
		if (*iid == gmpi::drawing::api::IDeviceContext::guid || *iid == gmpi::drawing::api::IResource::guid)
		{
			return gmpiContext.queryInterface(iid, returnInterface);
		}
		if (*iid == *reinterpret_cast<const gmpi::api::Guid*>(&GmpiDrawing_API::SE_IID_DEVICECONTEXT_MPGUI))
		{
			return (gmpi::ReturnCode)sdk3Context.queryInterface(*reinterpret_cast<const gmpi::MpGuid*>(iid), returnInterface);
		}
		if (*iid == gmpi::api::IUnknown::guid)
		{
			*returnInterface = this;
			addRef();
			return gmpi::ReturnCode::Ok;
		}
		return gmpi::ReturnCode::NoSupport;
	}
	GMPI_REFCOUNT_NO_DELETE;
};

struct UniversalGraphicsContext_win7 : public gmpi::api::IUnknown
{
	GraphicsContext_RG gmpiContext;
	GraphicsContext_Win7 sdk3Context;

	UniversalGraphicsContext_win7(gmpi::directx::DxFactoryInfo& pinfo, ID2D1DeviceContext* nativeContext = {}) :
		gmpiContext(this, pinfo, nativeContext),
		sdk3Context((gmpi::IMpUnknown*) static_cast<gmpi::api::IUnknown*>(this), pinfo, nativeContext)
	{}

	gmpi::ReturnCode queryInterface(const gmpi::api::Guid* iid, void** returnInterface) override
	{
		*returnInterface = {};
		if (*iid == gmpi::drawing::api::IDeviceContext::guid || *iid == gmpi::drawing::api::IResource::guid)
		{
			return gmpiContext.queryInterface(iid, returnInterface);
		}
		if (*iid == *reinterpret_cast<const gmpi::api::Guid*>(&GmpiDrawing_API::SE_IID_DEVICECONTEXT_MPGUI))
		{
			return (gmpi::ReturnCode)sdk3Context.queryInterface(*reinterpret_cast<const gmpi::MpGuid*>(iid), returnInterface);
		}
		if (*iid == gmpi::api::IUnknown::guid)
		{
			*returnInterface = this;
			addRef();
			return gmpi::ReturnCode::Ok;
		}
		return gmpi::ReturnCode::NoSupport;
	}
	GMPI_REFCOUNT_NO_DELETE;
};
} // Namespace
} // Namespace