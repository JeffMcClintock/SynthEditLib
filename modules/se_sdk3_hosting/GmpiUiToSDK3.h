#pragma once

/*
#include "GmpiUiToSDK3.h"
*/

#include "./gmpi_gui_hosting.h"
#include "GmpiApiDrawing.h"

namespace se
{
class GmpiToSDK3Factory : public GmpiDrawing_API::IMpFactory
{
	gmpi::drawing::api::IFactory* native{};

public:
	GmpiToSDK3Factory(gmpi::drawing::api::IFactory* pnative) : native(pnative) {}

	// IMpFactory
	int32_t MP_STDCALL CreatePathGeometry(GmpiDrawing_API::IMpPathGeometry** pathGeometry) override
	{ return gmpi::MP_NOSUPPORT; }
	int32_t MP_STDCALL CreateTextFormat(const char* fontFamilyName, void* unused /* fontCollection */, GmpiDrawing_API::MP1_FONT_WEIGHT fontWeight, GmpiDrawing_API::MP1_FONT_STYLE fontStyle, GmpiDrawing_API::MP1_FONT_STRETCH fontStretch, float fontSize, void* unused2 /* localeName */, GmpiDrawing_API::IMpTextFormat** textFormat)  override
	{ return gmpi::MP_NOSUPPORT; }
	int32_t MP_STDCALL CreateImage(int32_t width, int32_t height, GmpiDrawing_API::IMpBitmap** returnDiBitmap) override
	{ return gmpi::MP_NOSUPPORT; }
	int32_t MP_STDCALL LoadImageU(const char* utf8Uri, GmpiDrawing_API::IMpBitmap** returnDiBitmap) override
	{ return gmpi::MP_NOSUPPORT; }
	int32_t MP_STDCALL CreateStrokeStyle(const GmpiDrawing_API::MP1_STROKE_STYLE_PROPERTIES* strokeStyleProperties, float* dashes, int32_t dashesCount, GmpiDrawing_API::IMpStrokeStyle** strokeStyle) override;

	int32_t queryInterface(const gmpi::MpGuid& iid, void** returnInterface) override
	{
		*returnInterface = 0;
		if (iid == GmpiDrawing_API::SE_IID_FACTORY2_MPGUI || iid == GmpiDrawing_API::SE_IID_FACTORY_MPGUI || iid == gmpi::MP_IID_UNKNOWN)
		{
			*returnInterface = reinterpret_cast<GmpiDrawing_API::IMpFactory2*>(this);
			addRef();
			return gmpi::MP_OK;
		}
		return gmpi::MP_NOSUPPORT;
	}

	GMPI_REFCOUNT_NO_DELETE;
};

class GmpiToSDK3Context : public GmpiDrawing_API::IMpDeviceContext, public GmpiDrawing_API::IMpDeviceContextExt
{
	friend class GmpiToSDK3Factory;

	gmpi::drawing::api::IDeviceContext* context_{};
	GmpiToSDK3Factory factory;
	gmpi::IMpUnknown* fallback{};

	gmpi::drawing::api::IFactory* factoryGetter(gmpi::drawing::api::IResource* resource) const
	{
		gmpi::drawing::api::IFactory* f{};
		resource->getFactory(&f);
		return f;
	}

	class g3_BrushBase
	{
	protected:
		mutable gmpi::shared_ptr<gmpi::drawing::api::IBrush> native_;
		GmpiDrawing_API::IMpFactory* factory_{};
	public:
		g3_BrushBase(GmpiDrawing_API::IMpFactory* factory, gmpi::drawing::api::IBrush* native) : native_(native) {}
		auto native() const {return native_.get();}
	};

	class g3_SolidColorBrush final : public GmpiDrawing_API::IMpSolidColorBrush, public g3_BrushBase
	{
		GmpiDrawing_API::MP1_COLOR color_{};
	public:
		g3_SolidColorBrush(GmpiDrawing_API::IMpFactory* factory, gmpi::drawing::api::ISolidColorBrush* native) : g3_BrushBase(factory, native) {}
		inline gmpi::drawing::api::ISolidColorBrush* nativeSolidColorBrush()
		{
			return (gmpi::drawing::api::ISolidColorBrush*) native_.get();
		}
		void SetColor(const GmpiDrawing_API::MP1_COLOR* color) override
		{
			color_ = *color;
			nativeSolidColorBrush()->setColor((const gmpi::drawing::Color*)color);
		}
		GmpiDrawing_API::MP1_COLOR GetColor() override
		{
			return color_;
		}

		void MP_STDCALL GetFactory(GmpiDrawing_API::IMpFactory** factory) override{*factory = factory_;}
		GMPI_QUERYINTERFACE1(GmpiDrawing_API::SE_IID_SOLIDCOLORBRUSH_MPGUI, GmpiDrawing_API::IMpSolidColorBrush);
		GMPI_REFCOUNT;
	};

	class g3_StrokeStyle final : public GmpiDrawing_API::IMpStrokeStyle
	{
		mutable gmpi::shared_ptr <gmpi::drawing::api::IStrokeStyle> native_;
		GmpiDrawing_API::IMpFactory* factory_{};
		GmpiDrawing_API::MP1_STROKE_STYLE_PROPERTIES strokeStyleProperties_{};
	public:
		g3_StrokeStyle(GmpiDrawing_API::IMpFactory* factory, const GmpiDrawing_API::MP1_STROKE_STYLE_PROPERTIES* strokeStyleProperties, gmpi::drawing::api::IStrokeStyle* native) : native_(native), strokeStyleProperties_(*strokeStyleProperties), factory_(factory){}
		auto native() const { return native_.get(); }

		// IMpStrokeStyle
		GmpiDrawing_API::MP1_CAP_STYLE MP_STDCALL GetStartCap() override {return strokeStyleProperties_.startCap;}
		GmpiDrawing_API::MP1_CAP_STYLE MP_STDCALL GetEndCap() override { return strokeStyleProperties_.endCap; }
		GmpiDrawing_API::MP1_CAP_STYLE MP_STDCALL GetDashCap() override { return strokeStyleProperties_.dashCap; }
		float MP_STDCALL GetMiterLimit() override { return 10.f; }
		GmpiDrawing_API::MP1_LINE_JOIN MP_STDCALL GetLineJoin() override { return strokeStyleProperties_.lineJoin; }
		float MP_STDCALL GetDashOffset() override { return strokeStyleProperties_.dashOffset; }
		GmpiDrawing_API::MP1_DASH_STYLE MP_STDCALL GetDashStyle() override { return strokeStyleProperties_.dashStyle; }
		uint32_t MP_STDCALL GetDashesCount() override { return 0; }
		void MP_STDCALL GetDashes(float* dashes, uint32_t dashesCount) override {}

		// IMpResource
		void MP_STDCALL GetFactory(GmpiDrawing_API::IMpFactory** factory) override {*factory = factory_;}

		GMPI_QUERYINTERFACE1(GmpiDrawing_API::SE_IID_STROKESTYLE_MPGUI, GmpiDrawing_API::IMpStrokeStyle);
		GMPI_REFCOUNT;
	};


	gmpi::drawing::api::IBrush* toNative(const GmpiDrawing_API::IMpBrush* brush) const
	{
		return dynamic_cast<const g3_BrushBase*>(brush)->native();
	}

	gmpi::drawing::api::IStrokeStyle* toNative(const GmpiDrawing_API::IMpStrokeStyle* strokeStyle)
	{
		if (strokeStyle)
		{
			if (auto g3 = dynamic_cast<const g3_StrokeStyle*>(strokeStyle);g3)
			{
				return g3->native();
			}
		}
		return nullptr;
	}


public:
	GmpiToSDK3Context(gmpi::IMpUnknown* pfallback, gmpi::drawing::api::IDeviceContext* native) :
		fallback(pfallback)
		, context_(native)
		, factory(factoryGetter(native))
	{
	}

	// IMpDeviceContextExt
	int32_t MP_STDCALL CreateBitmapRenderTarget(GmpiDrawing_API::MP1_SIZE_L desiredSize, bool enableLockPixels, GmpiDrawing_API::IMpBitmapRenderTarget** bitmapRenderTarget) override
	{
		return gmpi::MP_NOSUPPORT;
	}

	// IMpDeviceContext
	void GetFactory(GmpiDrawing_API::IMpFactory** pfactory) override
	{
		*pfactory = &factory;
	}

	void DrawRectangle(const GmpiDrawing_API::MP1_RECT* rect, const GmpiDrawing_API::IMpBrush* brush, float strokeWidth, const GmpiDrawing_API::IMpStrokeStyle* strokeStyle) override
	{
		context_->drawRectangle((const gmpi::drawing::Rect*)rect, toNative(brush), strokeWidth, toNative(strokeStyle));
	}

	void FillRectangle(const GmpiDrawing_API::MP1_RECT* rect, const GmpiDrawing_API::IMpBrush* brush) override
	{
		context_->fillRectangle((const gmpi::drawing::Rect*)rect, toNative(brush));
	}

	void Clear(const GmpiDrawing_API::MP1_COLOR* clearColor) override
	{
		context_->clear((const gmpi::drawing::Color*)clearColor);
	}

	void DrawLine(GmpiDrawing_API::MP1_POINT point0, GmpiDrawing_API::MP1_POINT point1, const GmpiDrawing_API::IMpBrush* brush, float strokeWidth, const GmpiDrawing_API::IMpStrokeStyle* strokeStyle) override
	{
		context_->drawLine(
			*((gmpi::drawing::Point*)&point0),
			*((gmpi::drawing::Point*)&point1),
			toNative(brush),
			strokeWidth,
			toNative(strokeStyle)
		);
	}

	void DrawGeometry(const GmpiDrawing_API::IMpPathGeometry* geometry, const GmpiDrawing_API::IMpBrush* brush, float strokeWidth = 1.0f, const GmpiDrawing_API::IMpStrokeStyle* strokeStyle = 0) override
	{
	}

	void FillGeometry(const GmpiDrawing_API::IMpPathGeometry* geometry, const GmpiDrawing_API::IMpBrush* brush, const GmpiDrawing_API::IMpBrush* opacityBrush) override
	{
#if 0
		auto d2d_geometry = ((Geometry*)geometry)->geometry_;

		ID2D1Brush* opacityBrushNative;
		if (opacityBrush)
		{
			opacityBrushNative = ((Brush*)brush)->nativeBrush();
		}
		else
		{
			opacityBrushNative = nullptr;
		}

		context_->fillGeometry(d2d_geometry, ((Brush*)brush)->nativeBrush(), opacityBrushNative);
#endif
	}

	void DrawTextU(const char* utf8String, int32_t stringLength, const GmpiDrawing_API::IMpTextFormat* textFormat, const GmpiDrawing_API::MP1_RECT* layoutRect, const GmpiDrawing_API::IMpBrush* brush, int32_t flags) override
	{
	}

	//	void DrawBitmap( GmpiDrawing_API::IMpBitmap* mpBitmap, GmpiDrawing::Rect destinationRectangle, float opacity, int32_t interpolationMode, GmpiDrawing::Rect sourceRectangle) override
	void DrawBitmap(const GmpiDrawing_API::IMpBitmap* mpBitmap, const GmpiDrawing_API::MP1_RECT* destinationRectangle, float opacity, /* MP1_BITMAP_INTERPOLATION_MODE*/ int32_t interpolationMode, const GmpiDrawing_API::MP1_RECT* sourceRectangle) override
	{
#if 0
		auto bm = ((Bitmap*)mpBitmap);
		auto bitmap = bm->GetNativeBitmap(context_);
		if (bitmap)
		{
			context_->drawBitmap(
				bitmap,
				(const gmpi::drawing::Rect*)destinationRectangle,
				opacity,
				(D2D1_BITMAP_INTERPOLATION_MODE)interpolationMode,
				(const gmpi::drawing::Rect*)sourceRectangle
			);
		}
#endif
	}

	void SetTransform(const GmpiDrawing_API::MP1_MATRIX_3X2* transform) override
	{
		context_->setTransform(reinterpret_cast<const gmpi::drawing::Matrix3x2*>(transform));
	}

	void GetTransform(GmpiDrawing_API::MP1_MATRIX_3X2* transform) override
	{
		context_->getTransform(reinterpret_cast<gmpi::drawing::Matrix3x2*>(transform));
	}

	int32_t CreateSolidColorBrush(const GmpiDrawing_API::MP1_COLOR* color, GmpiDrawing_API::IMpSolidColorBrush** solidColorBrush) override
	{
		*solidColorBrush = nullptr;

		gmpi::drawing::api::ISolidColorBrush* b{};
		gmpi::drawing::BrushProperties bp{};
		auto hr = context_->createSolidColorBrush((const gmpi::drawing::Color*) color, &bp, &b);

		if (hr == gmpi::ReturnCode::Ok)
		{
			gmpi_sdk::mp_shared_ptr<gmpi::IMpUnknown> b2;
			b2.Attach(new g3_SolidColorBrush(&factory, b));

			b2->queryInterface(GmpiDrawing_API::SE_IID_SOLIDCOLORBRUSH_MPGUI, reinterpret_cast<void**>(solidColorBrush));
		}

		return (int32_t) hr;
	}

	int32_t CreateGradientStopCollection(const GmpiDrawing_API::MP1_GRADIENT_STOP* gradientStops, uint32_t gradientStopsCount, /* GmpiDrawing_API::MP1_GAMMA colorInterpolationGamma, GmpiDrawing_API::MP1_EXTEND_MODE extendMode,*/ GmpiDrawing_API::IMpGradientStopCollection** gradientStopCollection) override
	{
		return gmpi::MP_NOSUPPORT;
	}
#if 0
	template <typename T>
	int32_t make_wrapped(gmpi::IMpUnknown* object, const gmpi::MpGuid& iid, T** returnObject)
	{
		*returnObject = nullptr;
		gmpi_sdk::mp_shared_ptr<gmpi::IMpUnknown> b2;
		b2.Attach(object);
		return b2->queryInterface(iid, reinterpret_cast<void**>(returnObject));
	};
#endif
	int32_t CreateLinearGradientBrush(const GmpiDrawing_API::MP1_LINEAR_GRADIENT_BRUSH_PROPERTIES* linearGradientBrushProperties, const GmpiDrawing_API::MP1_BRUSH_PROPERTIES* brushProperties, const  GmpiDrawing_API::IMpGradientStopCollection* gradientStopCollection, GmpiDrawing_API::IMpLinearGradientBrush** linearGradientBrush) override
	{
		//return make_wrapped(
		//	new LinearGradientBrush(&factory, context_, linearGradientBrushProperties, brushProperties, gradientStopCollection),
		//	GmpiDrawing_API::SE_IID_LINEARGRADIENTBRUSH_MPGUI,
		//	linearGradientBrush);
		return gmpi::MP_NOSUPPORT;
	}

	int32_t CreateBitmapBrush(const GmpiDrawing_API::IMpBitmap* bitmap, const GmpiDrawing_API::MP1_BITMAP_BRUSH_PROPERTIES* bitmapBrushProperties, const GmpiDrawing_API::MP1_BRUSH_PROPERTIES* brushProperties, GmpiDrawing_API::IMpBitmapBrush** returnBrush) override
	{
		//*returnBrush = nullptr;
		//gmpi_sdk::mp_shared_ptr<gmpi::IMpUnknown> b2;
		//b2.Attach(new BitmapBrush(&factory, context_, bitmap, bitmapBrushProperties, brushProperties));
		//return b2->queryInterface(GmpiDrawing_API::SE_IID_BITMAPBRUSH_MPGUI, reinterpret_cast<void**>(returnBrush));
		return gmpi::MP_NOSUPPORT;
	}
	int32_t CreateRadialGradientBrush(const GmpiDrawing_API::MP1_RADIAL_GRADIENT_BRUSH_PROPERTIES* radialGradientBrushProperties, const GmpiDrawing_API::MP1_BRUSH_PROPERTIES* brushProperties, const GmpiDrawing_API::IMpGradientStopCollection* gradientStopCollection, GmpiDrawing_API::IMpRadialGradientBrush** radialGradientBrush) override
	{
		//*radialGradientBrush = nullptr;
		//gmpi_sdk::mp_shared_ptr<gmpi::IMpUnknown> b2;
		//b2.Attach(new RadialGradientBrush(&factory, context_, radialGradientBrushProperties, brushProperties, gradientStopCollection));
		//return b2->queryInterface(GmpiDrawing_API::SE_IID_RADIALGRADIENTBRUSH_MPGUI, reinterpret_cast<void**>(radialGradientBrush));
		return gmpi::MP_NOSUPPORT;
	}

	int32_t CreateCompatibleRenderTarget(const GmpiDrawing_API::MP1_SIZE* desiredSize, GmpiDrawing_API::IMpBitmapRenderTarget** bitmapRenderTarget) override
	{
		return gmpi::MP_NOSUPPORT;
	}

	void DrawRoundedRectangle(const GmpiDrawing_API::MP1_ROUNDED_RECT* roundedRect, const GmpiDrawing_API::IMpBrush* brush, float strokeWidth, const GmpiDrawing_API::IMpStrokeStyle* strokeStyle) override
	{
		context_->drawRoundedRectangle((const gmpi::drawing::RoundedRect*)roundedRect, toNative(brush), strokeWidth, toNative(strokeStyle));
	}
	void FillRoundedRectangle(const GmpiDrawing_API::MP1_ROUNDED_RECT* roundedRect, const GmpiDrawing_API::IMpBrush* brush) override
	{
		context_->fillRoundedRectangle((const gmpi::drawing::RoundedRect*)roundedRect, toNative(brush));
	}

	void DrawEllipse(const GmpiDrawing_API::MP1_ELLIPSE* ellipse, const GmpiDrawing_API::IMpBrush* brush, float strokeWidth, const GmpiDrawing_API::IMpStrokeStyle* strokeStyle) override
	{
		context_->drawEllipse((const gmpi::drawing::Ellipse*)ellipse, toNative(brush), strokeWidth, toNative(strokeStyle));
	}

	void FillEllipse(const GmpiDrawing_API::MP1_ELLIPSE* ellipse, const GmpiDrawing_API::IMpBrush* brush) override
	{
		context_->fillEllipse((const gmpi::drawing::Ellipse*)ellipse, toNative(brush));
	}

	void PushAxisAlignedClip(const GmpiDrawing_API::MP1_RECT* clipRect/*, GmpiDrawing_API::MP1_ANTIALIAS_MODE antialiasMode*/) override
	{
		context_->pushAxisAlignedClip((const gmpi::drawing::Rect*)clipRect);
	}
	void PopAxisAlignedClip() override
	{
		context_->popAxisAlignedClip();
	}
	void GetAxisAlignedClip(GmpiDrawing_API::MP1_RECT* returnClipRect) override
	{
		context_->getAxisAlignedClip((gmpi::drawing::Rect*)returnClipRect);
	}

	void BeginDraw() override
	{
		context_->beginDraw();
	}

	int32_t EndDraw() override
	{
		return (int32_t) context_->endDraw();
	}

	int32_t MP_STDCALL queryInterface(const gmpi::MpGuid& iid, void** returnInterface) override {
		*returnInterface = {};
		if (iid == GmpiDrawing_API::IMpDeviceContextExt::guid)
		{
			*returnInterface = static_cast<GmpiDrawing_API::IMpDeviceContextExt*>(this);
			addRef();
			return gmpi::MP_OK;
		}
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

inline int32_t MP_STDCALL GmpiToSDK3Factory::CreateStrokeStyle(const GmpiDrawing_API::MP1_STROKE_STYLE_PROPERTIES* strokeStyleProperties, float* dashes, int32_t dashesCount, GmpiDrawing_API::IMpStrokeStyle** strokeStyle)
{
	*strokeStyle = nullptr;

	gmpi::drawing::StrokeStyleProperties strokeStylePropertiesNative
	{
		(gmpi::drawing::CapStyle)strokeStyleProperties->startCap,
		(gmpi::drawing::LineJoin)strokeStyleProperties->lineJoin,
		strokeStyleProperties->miterLimit,
		(gmpi::drawing::DashStyle)strokeStyleProperties->dashStyle,
		strokeStyleProperties->dashOffset
	};

	gmpi::drawing::api::IStrokeStyle* b{};
	auto hr = native->createStrokeStyle(
		&strokeStylePropertiesNative,
		dashes,
		dashesCount,
		&b
	);

	if (hr == gmpi::ReturnCode::Ok)
	{
		gmpi_sdk::mp_shared_ptr<gmpi::IMpUnknown> b2;
		b2.Attach(new GmpiToSDK3Context::g3_StrokeStyle(this, strokeStyleProperties, b));

		b2->queryInterface(GmpiDrawing_API::SE_IID_STROKESTYLE_MPGUI, reinterpret_cast<void**>(strokeStyle));
	}

	return (int32_t)hr;
}

struct UniversalGraphicsContext2 : public gmpi::api::IUnknown
{
	gmpi::drawing::api::IDeviceContext* gmpiContext;
	GmpiToSDK3Context sdk3Context;

	UniversalGraphicsContext2(gmpi::drawing::api::IDeviceContext* nativeContext = {}) :
		gmpiContext(nativeContext),
		sdk3Context((gmpi::IMpUnknown*) static_cast<gmpi::api::IUnknown*>(this), nativeContext)
	{
	}

	gmpi::ReturnCode queryInterface(const gmpi::api::Guid* iid, void** returnInterface) override
	{
		*returnInterface = {};
		if (*iid == gmpi::drawing::api::IDeviceContext::guid || *iid == gmpi::drawing::api::IResource::guid)
		{
			return gmpiContext->queryInterface(iid, returnInterface);
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