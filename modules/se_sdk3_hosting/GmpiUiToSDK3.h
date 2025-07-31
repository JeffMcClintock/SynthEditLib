#pragma once

/*
#include "GmpiUiToSDK3.h"
*/

#include "./gmpi_gui_hosting.h"
#include "GmpiApiDrawing.h"

namespace se
{
// provide for running SynthEdit modules in a host that uses GMPI
class GmpiToSDK3Factory : public GmpiDrawing_API::IMpFactory2
{
	gmpi::drawing::api::IFactory* native{};

public:
	GmpiToSDK3Factory(gmpi::drawing::api::IFactory* pnative) : native(pnative) {}

	// IMpFactory
	int32_t MP_STDCALL CreatePathGeometry(GmpiDrawing_API::IMpPathGeometry** pathGeometry) override;
	int32_t MP_STDCALL CreateTextFormat(const char* fontFamilyName, void* unused /* fontCollection */, GmpiDrawing_API::MP1_FONT_WEIGHT fontWeight, GmpiDrawing_API::MP1_FONT_STYLE fontStyle, GmpiDrawing_API::MP1_FONT_STRETCH fontStretch, float fontSize, void* unused2 /* localeName */, GmpiDrawing_API::IMpTextFormat** textFormat)  override;
	int32_t MP_STDCALL CreateImage(int32_t width, int32_t height, GmpiDrawing_API::IMpBitmap** returnDiBitmap) override;
	int32_t MP_STDCALL LoadImageU(const char* utf8Uri, GmpiDrawing_API::IMpBitmap** returnDiBitmap) override;
	int32_t MP_STDCALL CreateStrokeStyle(const GmpiDrawing_API::MP1_STROKE_STYLE_PROPERTIES* strokeStyleProperties, float* dashes, int32_t dashesCount, GmpiDrawing_API::IMpStrokeStyle** strokeStyle) override;
	// IMpFactory2
	int32_t MP_STDCALL GetFontFamilyName(int32_t fontIndex, gmpi::IString* returnString) override;

	int32_t queryInterface(const gmpi::MpGuid& iid, void** returnInterface) override
	{
		*returnInterface = 0;
		if (iid == GmpiDrawing_API::SE_IID_FACTORY2_MPGUI || iid == GmpiDrawing_API::SE_IID_FACTORY_MPGUI || iid == gmpi::MP_IID_UNKNOWN)
		{
			*returnInterface = static_cast<GmpiDrawing_API::IMpFactory2*>(this);
			addRef();
			return gmpi::MP_OK;
		}
		return (int32_t) native->queryInterface((const gmpi::api::Guid*) &iid, returnInterface);
	}

	GMPI_REFCOUNT_NO_DELETE;
};

class GmpiToSDK3Context_base : public GmpiDrawing_API::IMpDeviceContext, public GmpiDrawing_API::IMpDeviceContextExt
{
	friend class GmpiToSDK3Factory;
	friend class g3_BitmapRenderTarget;

protected:
	gmpi::drawing::api::IDeviceContext* context_{};
	GmpiDrawing_API::IMpFactory2* factory{};
	gmpi::IMpUnknown* fallback{};

	class g3_BrushBase
	{
	protected:
		mutable gmpi::shared_ptr<gmpi::drawing::api::IBrush> native_;
		GmpiDrawing_API::IMpFactory* factory_{};
	public:
		g3_BrushBase(GmpiDrawing_API::IMpFactory* factory, gmpi::drawing::api::IBrush* native) : native_(native) {}
		auto native() const {return native_.get();}
	};

	inline static gmpi::drawing::api::IBrush* toGMPI(const GmpiDrawing_API::IMpBrush* brush)
	{
		if(brush)
			return dynamic_cast<const g3_BrushBase*>(brush)->native();
		return {};
	}

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

		// IMpResource
		void MP_STDCALL GetFactory(GmpiDrawing_API::IMpFactory** factory) override{*factory = factory_;}

		GMPI_QUERYINTERFACE1(GmpiDrawing_API::SE_IID_SOLIDCOLORBRUSH_MPGUI, GmpiDrawing_API::IMpSolidColorBrush);
		GMPI_REFCOUNT;
	};

	class g3_GradientStopCollection final : public GmpiDrawing_API::IMpGradientStopCollection
	{
		mutable gmpi::shared_ptr<gmpi::drawing::api::IGradientstopCollection> native_;
		GmpiDrawing_API::IMpFactory* factory_{};
	public:
		g3_GradientStopCollection(GmpiDrawing_API::IMpFactory* factory, gmpi::drawing::api::IGradientstopCollection* native) : native_(native), factory_(factory) {}
		auto native() const { return native_.get(); }

		// IMpResource
		void MP_STDCALL GetFactory(GmpiDrawing_API::IMpFactory** factory) override { *factory = factory_; }

		GMPI_QUERYINTERFACE1(GmpiDrawing_API::SE_IID_GRADIENTSTOPCOLLECTION_MPGUI, GmpiDrawing_API::IMpGradientStopCollection);
		GMPI_REFCOUNT;
	};

	class g3_LinearGradientBrush final : public GmpiDrawing_API::IMpLinearGradientBrush, public g3_BrushBase
	{
//		mutable gmpi::shared_ptr<gmpi::drawing::api::ILinearGradientBrush> native_;
//		GmpiDrawing_API::IMpFactory* factory_{};
		mutable gmpi::drawing::api::ILinearGradientBrush* derivedNative_;
	public:
		g3_LinearGradientBrush(GmpiDrawing_API::IMpFactory* factory, gmpi::drawing::api::ILinearGradientBrush* native) : g3_BrushBase(factory, native), derivedNative_(native) {}
		auto native() const { return native_.get(); }

		// IMpLinearGradientBrush
		void MP_STDCALL SetStartPoint(GmpiDrawing_API::MP1_POINT startPoint) override
		{
			derivedNative_->setStartPoint(*(gmpi::drawing::Point*)&startPoint);
		}
		void MP_STDCALL SetEndPoint(GmpiDrawing_API::MP1_POINT endPoint) override
		{
			derivedNative_->setEndPoint(*(gmpi::drawing::Point*)&endPoint);
		}

		// IMpResource
		void MP_STDCALL GetFactory(GmpiDrawing_API::IMpFactory** factory) override { *factory = factory_; }

		GMPI_QUERYINTERFACE1(GmpiDrawing_API::SE_IID_LINEARGRADIENTBRUSH_MPGUI, GmpiDrawing_API::IMpLinearGradientBrush);
		GMPI_REFCOUNT;
	};

	class g3_RadialGradientBrush final : public GmpiDrawing_API::IMpRadialGradientBrush, public g3_BrushBase
	{
		mutable gmpi::drawing::api::IRadialGradientBrush* derivedNative_;
	public:
		g3_RadialGradientBrush(GmpiDrawing_API::IMpFactory* factory, gmpi::drawing::api::IRadialGradientBrush* native) : g3_BrushBase(factory, native), derivedNative_(native){}
		auto native() const { return native_.get(); }

		// IMpRadialGradientBrush
		void MP_STDCALL SetCenter(GmpiDrawing_API::MP1_POINT center) override
		{
			derivedNative_->setCenter(*(gmpi::drawing::Point*)&center);
		}
		void MP_STDCALL SetGradientOriginOffset(GmpiDrawing_API::MP1_POINT gradientOriginOffset) override
		{
			derivedNative_->setGradientOriginOffset(*(gmpi::drawing::Point*)&gradientOriginOffset);
		}
		void MP_STDCALL SetRadiusX(float radiusX) override
		{
			derivedNative_->setRadiusX(radiusX);
		}
		void MP_STDCALL SetRadiusY(float radiusY) override
		{
			derivedNative_->setRadiusY(radiusY);
		}

		// IMpResource
		void MP_STDCALL GetFactory(GmpiDrawing_API::IMpFactory** factory) override { *factory = factory_; }

		GMPI_QUERYINTERFACE1(GmpiDrawing_API::SE_IID_RADIALGRADIENTBRUSH_MPGUI, GmpiDrawing_API::IMpRadialGradientBrush);
		GMPI_REFCOUNT;
	};

	class g3_BitmapBrush final : public GmpiDrawing_API::IMpBitmapBrush
	{
		mutable gmpi::shared_ptr<gmpi::drawing::api::IBitmapBrush> native_;
		GmpiDrawing_API::IMpFactory* factory_{};
	public:
		g3_BitmapBrush(GmpiDrawing_API::IMpFactory* factory, gmpi::drawing::api::IBitmapBrush* native) : native_(native), factory_(factory) {}
		auto native() const { return native_.get(); }

		// IMpBitmapBrush, methods not supported by GMPI-UI
		void MP_STDCALL SetExtendModeX(GmpiDrawing_API::MP1_EXTEND_MODE extendModeX) override
		{
		}
		void MP_STDCALL SetExtendModeY(GmpiDrawing_API::MP1_EXTEND_MODE extendModeY) override
		{
		}
		void MP_STDCALL SetInterpolationMode(GmpiDrawing_API::MP1_BITMAP_INTERPOLATION_MODE interpolationMode) override
		{
		}

		// IMpResource
		void MP_STDCALL GetFactory(GmpiDrawing_API::IMpFactory** factory) override { *factory = factory_; }

		GMPI_QUERYINTERFACE1(GmpiDrawing_API::SE_IID_BITMAPBRUSH_MPGUI, GmpiDrawing_API::IMpBitmapBrush);
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

	inline static gmpi::drawing::api::IStrokeStyle* toGMPI(const GmpiDrawing_API::IMpStrokeStyle* strokeStyle)
	{
		if (auto g3 = dynamic_cast<const g3_StrokeStyle*>(strokeStyle); g3)
			return g3->native();
		return {};
	}

	class g3_GeometrySink final : public GmpiDrawing_API::IMpGeometrySink2
	{
		mutable gmpi::shared_ptr<gmpi::drawing::api::IGeometrySink> native_;
		GmpiDrawing_API::IMpFactory* factory_{};
	public:
		g3_GeometrySink(GmpiDrawing_API::IMpFactory* factory, gmpi::drawing::api::IGeometrySink* native) : native_(native), factory_(factory) {}
		auto native() const { return native_.get(); }

		// IMpSimplifiedGeometrySink
		void MP_STDCALL BeginFigure(GmpiDrawing_API::MP1_POINT startPoint, GmpiDrawing_API::MP1_FIGURE_BEGIN figureBegin) override
		{
			native()->beginFigure(*(gmpi::drawing::Point*) &startPoint, (gmpi::drawing::FigureBegin) figureBegin);
		}
		void MP_STDCALL AddLines(const GmpiDrawing_API::MP1_POINT* points, uint32_t pointsCount) override
		{
			native()->addLines((const gmpi::drawing::Point*) points, pointsCount);
		}
		void MP_STDCALL AddBeziers(const GmpiDrawing_API::MP1_BEZIER_SEGMENT* beziers, uint32_t beziersCount) override
		{
			native()->addBeziers((const gmpi::drawing::BezierSegment*) beziers, beziersCount);
		}
		void MP_STDCALL EndFigure(GmpiDrawing_API::MP1_FIGURE_END figureEnd) override
		{
			native()->endFigure((gmpi::drawing::FigureEnd) figureEnd);
		}
		int32_t MP_STDCALL Close() override
		{
			return (int32_t) native()->close();
		}

		// IMpGeometrySink
		void MP_STDCALL AddLine(GmpiDrawing_API::MP1_POINT point)  override
		{
			native()->addLine(*(gmpi::drawing::Point*)&point);
		}
		void MP_STDCALL AddBezier(const GmpiDrawing_API::MP1_BEZIER_SEGMENT* bezier) override
		{
			native()->addBezier((const gmpi::drawing::BezierSegment*)bezier);
		}
		void MP_STDCALL AddQuadraticBezier(const GmpiDrawing_API::MP1_QUADRATIC_BEZIER_SEGMENT* bezier) override
		{
			native()->addQuadraticBezier((const gmpi::drawing::QuadraticBezierSegment*)bezier);
		}
		void MP_STDCALL AddQuadraticBeziers(const GmpiDrawing_API::MP1_QUADRATIC_BEZIER_SEGMENT* beziers, uint32_t beziersCount) override
		{
			native()->addQuadraticBeziers((const gmpi::drawing::QuadraticBezierSegment*)beziers, beziersCount);
		}
		void MP_STDCALL AddArc(const GmpiDrawing_API::MP1_ARC_SEGMENT* arc) override
		{
			native()->addArc((const gmpi::drawing::ArcSegment*)arc);
		}

		// IMpGeometrySink2
		virtual void MP_STDCALL SetFillMode(GmpiDrawing_API::MP1_FILL_MODE fillMode) override
		{
			native()->setFillMode((gmpi::drawing::FillMode) fillMode);
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

	class g3_PathGeometry final : public GmpiDrawing_API::IMpPathGeometry
	{
		mutable gmpi::shared_ptr<gmpi::drawing::api::IPathGeometry> native_;
		GmpiDrawing_API::IMpFactory* factory_{};
	public:
		g3_PathGeometry(GmpiDrawing_API::IMpFactory* factory, gmpi::drawing::api::IPathGeometry* native) : native_(native), factory_(factory) {}
		auto native() const { return native_.get(); }

		// IMpPathGeometry
		int32_t MP_STDCALL Open(GmpiDrawing_API::IMpGeometrySink** geometrySink) override
		{
			gmpi::shared_ptr<gmpi::drawing::api::IGeometrySink> nativeGeometrySink;
			native()->open(nativeGeometrySink.put());
			*geometrySink = new g3_GeometrySink(factory_, nativeGeometrySink);
			return gmpi::MP_OK;
		}
		int32_t MP_STDCALL StrokeContainsPoint(GmpiDrawing_API::MP1_POINT point, float strokeWidth, GmpiDrawing_API::IMpStrokeStyle* strokeStyle, const GmpiDrawing_API::MP1_MATRIX_3X2* worldTransform, bool* returnContains) override
		{
			return (int32_t) native()->strokeContainsPoint(*(gmpi::drawing::Point*)&point, strokeWidth, toGMPI(strokeStyle), (const gmpi::drawing::Matrix3x2*)worldTransform, returnContains);
		}
		int32_t MP_STDCALL FillContainsPoint(GmpiDrawing_API::MP1_POINT point, const GmpiDrawing_API::MP1_MATRIX_3X2* worldTransform, bool* returnContains) override
		{
			return (int32_t) native()->fillContainsPoint(*(gmpi::drawing::Point*)&point, (const gmpi::drawing::Matrix3x2*)worldTransform, returnContains);
		}
		int32_t MP_STDCALL GetWidenedBounds(float strokeWidth, GmpiDrawing_API::IMpStrokeStyle* strokeStyle, const GmpiDrawing_API::MP1_MATRIX_3X2* worldTransform, GmpiDrawing_API::MP1_RECT* returnBounds) override
		{
			return (int32_t) native()->getWidenedBounds(strokeWidth, toGMPI(strokeStyle), (const gmpi::drawing::Matrix3x2*)worldTransform, (gmpi::drawing::Rect*)returnBounds);
		}

		// IMpResource
		void MP_STDCALL GetFactory(GmpiDrawing_API::IMpFactory** factory) override { *factory = factory_; }

		GMPI_QUERYINTERFACE1(GmpiDrawing_API::SE_IID_PATHGEOMETRY_MPGUI, GmpiDrawing_API::IMpPathGeometry);
		GMPI_REFCOUNT;
	};

	inline static gmpi::drawing::api::IPathGeometry* toGMPI(const GmpiDrawing_API::IMpPathGeometry* geometry)
	{
		if (auto g3 = dynamic_cast<const g3_PathGeometry*>(geometry); g3)
			return g3->native();
		return {};
	}

	class g3_TextFormat final : public GmpiDrawing_API::IMpTextFormat
	{
		mutable gmpi::shared_ptr<gmpi::drawing::api::ITextFormat> native_;
		GmpiDrawing_API::IMpFactory* factory_{};
	public:
		g3_TextFormat(GmpiDrawing_API::IMpFactory* factory, gmpi::drawing::api::ITextFormat* native) : native_(native), factory_(factory) {}
		auto native() const { return native_.get(); }

		// IMpTextFormat
		int32_t MP_STDCALL SetTextAlignment(GmpiDrawing_API::MP1_TEXT_ALIGNMENT textAlignment) override
		{
			return (int32_t)native()->setTextAlignment((gmpi::drawing::TextAlignment)textAlignment);
		}
		int32_t MP_STDCALL SetParagraphAlignment(GmpiDrawing_API::MP1_PARAGRAPH_ALIGNMENT paragraphAlignment) override
		{
			return (int32_t)native()->setParagraphAlignment((gmpi::drawing::ParagraphAlignment)paragraphAlignment);
		}
		int32_t MP_STDCALL SetWordWrapping(GmpiDrawing_API::MP1_WORD_WRAPPING wordWrapping) override
		{
			return (int32_t)native()->setWordWrapping((gmpi::drawing::WordWrapping)wordWrapping);
		}
		void MP_STDCALL GetTextExtentU(const char* utf8String, int32_t stringLength, GmpiDrawing_API::MP1_SIZE* returnSize) override
		{
			native()->getTextExtentU(utf8String, stringLength, (gmpi::drawing::Size*)returnSize);
		}
		int32_t MP_STDCALL GetFontMetrics(GmpiDrawing_API::MP1_FONT_METRICS* returnFontMetrics) override
		{
			return (int32_t)native()->getFontMetrics((gmpi::drawing::FontMetrics*)returnFontMetrics);
		}
		int32_t MP_STDCALL SetLineSpacing(float lineSpacing, float baseline) override
		{
			return (int32_t)native()->setLineSpacing(lineSpacing, baseline);
		}
		GMPI_QUERYINTERFACE1(GmpiDrawing_API::SE_IID_TEXTFORMAT_MPGUI, GmpiDrawing_API::IMpTextFormat);
		GMPI_REFCOUNT;
	};

	inline static gmpi::drawing::api::ITextFormat* toGMPI(const GmpiDrawing_API::IMpTextFormat* textFormat)
	{
		if (auto g3 = dynamic_cast<const g3_TextFormat*>(textFormat); g3)
			return g3->native();
		return {};
	}

	class g3_BitmapPixels final : public GmpiDrawing_API::IMpBitmapPixels
	{
		mutable gmpi::shared_ptr<gmpi::drawing::api::IBitmapPixels> native_;
		GmpiDrawing_API::IMpFactory* factory_{};
	public:
		g3_BitmapPixels(GmpiDrawing_API::IMpFactory* factory, gmpi::drawing::api::IBitmapPixels* native) : native_(native), factory_(factory) {}
		auto native() const { return native_.get(); }

		// IMpBitmapPixels
		uint8_t* MP_STDCALL getAddress() const override
		{
			uint8_t* returnAddress{};
			native()->getAddress(&returnAddress);
			return returnAddress;
		}
		int32_t MP_STDCALL getBytesPerRow() const override
		{
			int32_t returnBytesPerRow{};
			native()->getBytesPerRow(&returnBytesPerRow);
			return returnBytesPerRow;
		}
		int32_t MP_STDCALL getPixelFormat() const override
		{
			int32_t returnPixelFormat{};
			native()->getPixelFormat(&returnPixelFormat);
			return returnPixelFormat;
		}

		GMPI_QUERYINTERFACE1(GmpiDrawing_API::SE_IID_BITMAP_PIXELS_MPGUI, GmpiDrawing_API::IMpBitmapPixels);
		GMPI_REFCOUNT;
	};

	class g3_Bitmap final : public GmpiDrawing_API::IMpBitmap
	{
		mutable gmpi::shared_ptr<gmpi::drawing::api::IBitmap> native_;
		GmpiDrawing_API::IMpFactory* factory_{};
	public:
		g3_Bitmap(GmpiDrawing_API::IMpFactory* factory, gmpi::drawing::api::IBitmap* native) : native_(native), factory_(factory) {}
		auto native() const { return native_.get(); }

		// IMpBitmap
		GmpiDrawing_API::MP1_SIZE MP_STDCALL GetSizeF() override
		{
			gmpi::drawing::SizeU sizeU{};
			native()->getSizeU(&sizeU);

			return { static_cast<float>(sizeU.width), static_cast<float>(sizeU.height) };
		}
		int32_t MP_STDCALL lockPixelsOld(GmpiDrawing_API::IMpBitmapPixels** returnPixels, bool alphaPremultiplied = false) override
		{
			return lockPixels(returnPixels, GmpiDrawing_API::MP1_BITMAP_LOCK_READ | GmpiDrawing_API::MP1_BITMAP_LOCK_WRITE);
		}
		void MP_STDCALL ApplyAlphaCorrection() override {}
		int32_t MP_STDCALL GetSize(GmpiDrawing_API::MP1_SIZE_U* returnSize) override
		{
			return (int32_t) native()->getSizeU((gmpi::drawing::SizeU*)returnSize);
		}
		int32_t MP_STDCALL lockPixels(GmpiDrawing_API::IMpBitmapPixels** returnPixels, int32_t flags)
		{
			gmpi::drawing::api::IBitmapPixels* pixels{};
			auto hr = native()->lockPixels(&pixels, flags);
			if (hr == gmpi::ReturnCode::Ok)
			{
				*returnPixels = new g3_BitmapPixels(factory_, pixels);
			}
			return (int32_t) hr;
		}

		// IMpResource
		void MP_STDCALL GetFactory(GmpiDrawing_API::IMpFactory** factory) override { *factory = factory_; }

		GMPI_QUERYINTERFACE1(GmpiDrawing_API::SE_IID_BITMAP_MPGUI, GmpiDrawing_API::IMpBitmap);
		GMPI_REFCOUNT;
	};

public:
	GmpiToSDK3Context_base(GmpiDrawing_API::IMpFactory2* pfactory, gmpi::IMpUnknown* pfallback, gmpi::drawing::api::IDeviceContext* native) :
		fallback(pfallback)
		, context_(native)
		, factory(pfactory) //factoryGetter(native))
	{
	}

	// IMpDeviceContext
	void GetFactory(GmpiDrawing_API::IMpFactory** pfactory) override
	{
		*pfactory = factory;
	}

	void DrawRectangle(const GmpiDrawing_API::MP1_RECT* rect, const GmpiDrawing_API::IMpBrush* brush, float strokeWidth, const GmpiDrawing_API::IMpStrokeStyle* strokeStyle) override
	{
		context_->drawRectangle((const gmpi::drawing::Rect*)rect, toGMPI(brush), strokeWidth, toGMPI(strokeStyle));
	}

	void FillRectangle(const GmpiDrawing_API::MP1_RECT* rect, const GmpiDrawing_API::IMpBrush* brush) override
	{
		context_->fillRectangle((const gmpi::drawing::Rect*)rect, toGMPI(brush));
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
			toGMPI(brush),
			strokeWidth,
			toGMPI(strokeStyle)
		);
	}

	void DrawGeometry(const GmpiDrawing_API::IMpPathGeometry* geometry, const GmpiDrawing_API::IMpBrush* brush, float strokeWidth = 1.0f, const GmpiDrawing_API::IMpStrokeStyle* strokeStyle = 0) override
	{
		context_->drawGeometry(toGMPI(geometry), toGMPI(brush), strokeWidth, toGMPI(strokeStyle));
	}

	void FillGeometry(const GmpiDrawing_API::IMpPathGeometry* geometry, const GmpiDrawing_API::IMpBrush* brush, const GmpiDrawing_API::IMpBrush* opacityBrush) override
	{
		context_->fillGeometry(toGMPI(geometry), toGMPI(brush), toGMPI(opacityBrush));
	}

	void DrawTextU(const char* utf8String, int32_t stringLength, const GmpiDrawing_API::IMpTextFormat* textFormat, const GmpiDrawing_API::MP1_RECT* layoutRect, const GmpiDrawing_API::IMpBrush* brush, int32_t flags) override
	{
		context_->drawTextU(utf8String, stringLength, toGMPI(textFormat), (const gmpi::drawing::Rect*)layoutRect, toGMPI(brush), flags);
	}

	//	void DrawBitmap( GmpiDrawing_API::IMpBitmap* mpBitmap, GmpiDrawing::Rect destinationRectangle, float opacity, int32_t interpolationMode, GmpiDrawing::Rect sourceRectangle) override
	void DrawBitmap(const GmpiDrawing_API::IMpBitmap* mpBitmap, const GmpiDrawing_API::MP1_RECT* destinationRectangle, float opacity, /* MP1_BITMAP_INTERPOLATION_MODE*/ int32_t interpolationMode, const GmpiDrawing_API::MP1_RECT* sourceRectangle) override
	{
		context_->drawBitmap(dynamic_cast<const g3_Bitmap*>(mpBitmap)->native(), (const gmpi::drawing::Rect*)destinationRectangle, opacity, (gmpi::drawing::BitmapInterpolationMode)interpolationMode, (const gmpi::drawing::Rect*)sourceRectangle);
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

		gmpi::drawing::BrushProperties bp{};
		gmpi::shared_ptr<gmpi::drawing::api::ISolidColorBrush> b;
		auto hr = context_->createSolidColorBrush((const gmpi::drawing::Color*) color, &bp, b.put());

		if (hr == gmpi::ReturnCode::Ok)
		{
			gmpi_sdk::mp_shared_ptr<gmpi::IMpUnknown> b2;
			b2.Attach(new g3_SolidColorBrush(factory, b));

			b2->queryInterface(GmpiDrawing_API::SE_IID_SOLIDCOLORBRUSH_MPGUI, reinterpret_cast<void**>(solidColorBrush));
		}

		return (int32_t) hr;
	}

	int32_t CreateGradientStopCollection(const GmpiDrawing_API::MP1_GRADIENT_STOP* gradientStops, uint32_t gradientStopsCount, /* GmpiDrawing_API::MP1_GAMMA colorInterpolationGamma, GmpiDrawing_API::MP1_EXTEND_MODE extendMode,*/ GmpiDrawing_API::IMpGradientStopCollection** gradientStopCollection) override
	{
		*gradientStopCollection = nullptr;

		gmpi::shared_ptr<gmpi::drawing::api::IGradientstopCollection> b;
		auto hr = context_->createGradientstopCollection((const gmpi::drawing::Gradientstop*) gradientStops, gradientStopsCount, gmpi::drawing::ExtendMode::Clamp, b.put());

		if (hr == gmpi::ReturnCode::Ok)
		{
			gmpi_sdk::mp_shared_ptr<gmpi::IMpUnknown> b2;
			b2.Attach(new g3_GradientStopCollection(factory, b));

			b2->queryInterface(GmpiDrawing_API::SE_IID_GRADIENTSTOPCOLLECTION_MPGUI, reinterpret_cast<void**>(gradientStopCollection));
		}

		return (int32_t)hr;
	}

	int32_t CreateLinearGradientBrush(const GmpiDrawing_API::MP1_LINEAR_GRADIENT_BRUSH_PROPERTIES* linearGradientBrushProperties, const GmpiDrawing_API::MP1_BRUSH_PROPERTIES* brushProperties, const  GmpiDrawing_API::IMpGradientStopCollection* gradientStopCollection, GmpiDrawing_API::IMpLinearGradientBrush** linearGradientBrush) override
	{
		*linearGradientBrush = nullptr;

		gmpi::shared_ptr<gmpi::drawing::api::ILinearGradientBrush> b;
		auto hr = context_->createLinearGradientBrush(
			(const gmpi::drawing::LinearGradientBrushProperties*) linearGradientBrushProperties,
			(const gmpi::drawing::BrushProperties*) brushProperties,
			dynamic_cast<const g3_GradientStopCollection*>(gradientStopCollection)->native(),
			b.put()
		);

		if (hr == gmpi::ReturnCode::Ok)
		{
			gmpi_sdk::mp_shared_ptr<gmpi::IMpUnknown> b2;
			b2.Attach(new g3_LinearGradientBrush(factory, b));

			b2->queryInterface(GmpiDrawing_API::SE_IID_LINEARGRADIENTBRUSH_MPGUI, reinterpret_cast<void**>(linearGradientBrush));
		}

		return (int32_t)hr;
	}

	int32_t CreateBitmapBrush(const GmpiDrawing_API::IMpBitmap* bitmap, const GmpiDrawing_API::MP1_BITMAP_BRUSH_PROPERTIES* bitmapBrushProperties, const GmpiDrawing_API::MP1_BRUSH_PROPERTIES* brushProperties, GmpiDrawing_API::IMpBitmapBrush** returnBrush) override
	{
		returnBrush = nullptr;

		gmpi::shared_ptr<gmpi::drawing::api::IBitmapBrush> b;
		auto hr = context_->createBitmapBrush(dynamic_cast<const g3_Bitmap*>(bitmap)->native(), (const gmpi::drawing::BrushProperties*)brushProperties, b.put());

		if (hr == gmpi::ReturnCode::Ok)
		{
			gmpi_sdk::mp_shared_ptr<gmpi::IMpUnknown> b2;
			b2.Attach(new g3_BitmapBrush(factory, b));

			b2->queryInterface(GmpiDrawing_API::SE_IID_BITMAPBRUSH_MPGUI, reinterpret_cast<void**>(returnBrush));
		}

		return (int32_t)hr;
	}

	int32_t CreateRadialGradientBrush(const GmpiDrawing_API::MP1_RADIAL_GRADIENT_BRUSH_PROPERTIES* radialGradientBrushProperties, const GmpiDrawing_API::MP1_BRUSH_PROPERTIES* brushProperties, const GmpiDrawing_API::IMpGradientStopCollection* gradientStopCollection, GmpiDrawing_API::IMpRadialGradientBrush** radialGradientBrush) override
	{
		*radialGradientBrush = nullptr;

		gmpi::shared_ptr<gmpi::drawing::api::IRadialGradientBrush> b;
		auto hr = context_->createRadialGradientBrush(
			(const gmpi::drawing::RadialGradientBrushProperties*) radialGradientBrushProperties,
			(const gmpi::drawing::BrushProperties*) brushProperties,
			dynamic_cast<const g3_GradientStopCollection*>(gradientStopCollection)->native(),
			b.put()
		);

		if (hr == gmpi::ReturnCode::Ok)
		{
			gmpi_sdk::mp_shared_ptr<gmpi::IMpUnknown> b2;
			b2.Attach(new g3_RadialGradientBrush(factory, b));

			b2->queryInterface(GmpiDrawing_API::SE_IID_RADIALGRADIENTBRUSH_MPGUI, reinterpret_cast<void**>(radialGradientBrush));
		}

		return (int32_t)hr;
	}

	int32_t CreateCompatibleRenderTarget(const GmpiDrawing_API::MP1_SIZE* desiredSize, GmpiDrawing_API::IMpBitmapRenderTarget** bitmapRenderTarget) override;

	// IMpDeviceContextExt
	int32_t MP_STDCALL CreateBitmapRenderTarget(GmpiDrawing_API::MP1_SIZE_L desiredSize, bool enableLockPixels, GmpiDrawing_API::IMpBitmapRenderTarget** bitmapRenderTarget) override;

	void DrawRoundedRectangle(const GmpiDrawing_API::MP1_ROUNDED_RECT* roundedRect, const GmpiDrawing_API::IMpBrush* brush, float strokeWidth, const GmpiDrawing_API::IMpStrokeStyle* strokeStyle) override
	{
		context_->drawRoundedRectangle((const gmpi::drawing::RoundedRect*)roundedRect, toGMPI(brush), strokeWidth, toGMPI(strokeStyle));
	}
	void FillRoundedRectangle(const GmpiDrawing_API::MP1_ROUNDED_RECT* roundedRect, const GmpiDrawing_API::IMpBrush* brush) override
	{
		context_->fillRoundedRectangle((const gmpi::drawing::RoundedRect*)roundedRect, toGMPI(brush));
	}

	void DrawEllipse(const GmpiDrawing_API::MP1_ELLIPSE* ellipse, const GmpiDrawing_API::IMpBrush* brush, float strokeWidth, const GmpiDrawing_API::IMpStrokeStyle* strokeStyle) override
	{
		context_->drawEllipse((const gmpi::drawing::Ellipse*)ellipse, toGMPI(brush), strokeWidth, toGMPI(strokeStyle));
	}

	void FillEllipse(const GmpiDrawing_API::MP1_ELLIPSE* ellipse, const GmpiDrawing_API::IMpBrush* brush) override
	{
		context_->fillEllipse((const gmpi::drawing::Ellipse*)ellipse, toGMPI(brush));
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
};

class GmpiToSDK3Context final : public GmpiToSDK3Context_base
{
public:
	GmpiToSDK3Context(GmpiDrawing_API::IMpFactory2* pfactory, gmpi::IMpUnknown* pfallback, gmpi::drawing::api::IDeviceContext* native) : GmpiToSDK3Context_base(pfactory, pfallback, native) {}

	int32_t MP_STDCALL queryInterface(const gmpi::MpGuid& iid, void** returnInterface) override
	{
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


class g3_BitmapRenderTarget final : public GmpiToSDK3Context_base // emulated by careful layout: public IBitmapRenderTarget
{
//	mutable gmpi::shared_ptr<gmpi::drawing::api::IBitmapRenderTarget> native_;

	gmpi::drawing::api::IBitmapRenderTarget* makeNative(GmpiToSDK3Context_base* g, const gmpi::drawing::Size* desiredSize) const
	{
		gmpi::drawing::api::IBitmapRenderTarget* native{};
		g->context_->createCompatibleRenderTarget(*desiredSize, 0, &native);
		return native;
	}
public:
	g3_BitmapRenderTarget(GmpiDrawing_API::IMpFactory2* pfactory, GmpiToSDK3Context_base* g, const gmpi::drawing::Size* desiredSize/*, GmpiDrawing_API::IMpFactory* pfactory*/) :
		GmpiToSDK3Context_base(pfactory, nullptr, makeNative(g, desiredSize))
	{
		context_->queryInterface(&gmpi::drawing::api::IBitmapRenderTarget::guid, (void**)&context_);
	}

	// HACK, to be ABI compatible with IBitmapRenderTarget we need this virtual function,
	// and it needs to be in the vtable right after all virtual functions of GraphicsContext
	virtual gmpi::ReturnCode getBitmap(gmpi::drawing::api::IBitmap** returnBitmap)
	{
		gmpi::drawing::api::IBitmapRenderTarget* native{};
		context_->queryInterface(&gmpi::drawing::api::IBitmapRenderTarget::guid, (void**)&native);

		gmpi::shared_ptr<gmpi::drawing::api::IBitmap> bitmap;
		native->getBitmap(bitmap.put());

		gmpi_sdk::mp_shared_ptr<gmpi::IMpUnknown> b2;
		b2.Attach(new GmpiToSDK3Context::g3_Bitmap(factory, bitmap));

		return (gmpi::ReturnCode) b2->queryInterface(GmpiDrawing_API::SE_IID_BITMAP_MPGUI, reinterpret_cast<void**>(returnBitmap));
	}

	int32_t MP_STDCALL queryInterface(const gmpi::MpGuid& iid, void** returnInterface) override
	{
		*returnInterface = {};
		if (iid == GmpiDrawing_API::SE_IID_BITMAP_RENDERTARGET_MPGUI)
		{
			// non-standard. Forcing this class (which has the correct vtable) to pretend it's the emulated interface.
			*returnInterface = reinterpret_cast<GmpiDrawing_API::IMpBitmapRenderTarget*>(this);
			addRef();
			return gmpi::MP_OK;
		}
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
	GMPI_REFCOUNT;
};

inline int32_t GmpiToSDK3Context_base::CreateCompatibleRenderTarget(const GmpiDrawing_API::MP1_SIZE* desiredSize, GmpiDrawing_API::IMpBitmapRenderTarget** bitmapRenderTarget)
{
	*bitmapRenderTarget = nullptr;

	gmpi_sdk::mp_shared_ptr<GmpiDrawing_API::IMpDeviceContext> b2;
	b2.Attach(new g3_BitmapRenderTarget(factory, this, (const gmpi::drawing::Size*) &desiredSize/*, &factory*/));
	return b2->queryInterface(GmpiDrawing_API::SE_IID_BITMAP_RENDERTARGET_MPGUI, reinterpret_cast<void**>(bitmapRenderTarget));
}

// IMpDeviceContextExt
inline int32_t MP_STDCALL GmpiToSDK3Context_base::CreateBitmapRenderTarget(GmpiDrawing_API::MP1_SIZE_L desiredSize, bool enableLockPixels, GmpiDrawing_API::IMpBitmapRenderTarget** bitmapRenderTarget)
{
	*bitmapRenderTarget = nullptr;

	const gmpi::drawing::Size sizef{ static_cast<float>(desiredSize.width), static_cast<float>(desiredSize.height) };

	gmpi_sdk::mp_shared_ptr<GmpiDrawing_API::IMpDeviceContext> b2;
	b2.Attach(static_cast<GmpiDrawing_API::IMpDeviceContext*>(new g3_BitmapRenderTarget(factory, this, &sizef/*, &factory, enableLockPixels*/)));
	return b2->queryInterface(GmpiDrawing_API::SE_IID_BITMAP_RENDERTARGET_MPGUI, reinterpret_cast<void**>(bitmapRenderTarget));
}


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

	gmpi::shared_ptr<gmpi::drawing::api::IStrokeStyle> b;
	auto hr = native->createStrokeStyle(
		&strokeStylePropertiesNative,
		dashes,
		dashesCount,
		b.put()
	);

	if (hr == gmpi::ReturnCode::Ok)
	{
		gmpi_sdk::mp_shared_ptr<gmpi::IMpUnknown> b2;
		b2.Attach(new GmpiToSDK3Context::g3_StrokeStyle(this, strokeStyleProperties, b));

		b2->queryInterface(GmpiDrawing_API::SE_IID_STROKESTYLE_MPGUI, reinterpret_cast<void**>(strokeStyle));
	}

	return (int32_t)hr;
}

inline int32_t MP_STDCALL GmpiToSDK3Factory::CreatePathGeometry(GmpiDrawing_API::IMpPathGeometry** pathGeometry)
{
	*pathGeometry = nullptr;

	gmpi::shared_ptr<gmpi::drawing::api::IPathGeometry> b;
	auto hr = native->createPathGeometry(b.put());

	if (hr == gmpi::ReturnCode::Ok)
	{
		gmpi_sdk::mp_shared_ptr<gmpi::IMpUnknown> b2;
		b2.Attach(new GmpiToSDK3Context::g3_PathGeometry(this, b));

		b2->queryInterface(GmpiDrawing_API::SE_IID_PATHGEOMETRY_MPGUI, reinterpret_cast<void**>(pathGeometry));
	}

	return (int32_t)hr;
}

inline int32_t MP_STDCALL GmpiToSDK3Factory::CreateTextFormat(const char* fontFamilyName, void* unused /* fontCollection */, GmpiDrawing_API::MP1_FONT_WEIGHT fontWeight, GmpiDrawing_API::MP1_FONT_STYLE fontStyle, GmpiDrawing_API::MP1_FONT_STRETCH fontStretch, float fontSize, void* unused2 /* localeName */, GmpiDrawing_API::IMpTextFormat** textFormat)
{
	*textFormat = nullptr;

	gmpi::shared_ptr<gmpi::drawing::api::ITextFormat> b;
	auto hr = native->createTextFormat(
		fontFamilyName,
		(gmpi::drawing::FontWeight) fontWeight,
		(gmpi::drawing::FontStyle) fontStyle,
		(gmpi::drawing::FontStretch) fontStretch,
		fontSize,
		0,
		b.put()
	);

	if (hr == gmpi::ReturnCode::Ok)
	{
		gmpi_sdk::mp_shared_ptr<gmpi::IMpUnknown> b2;
		b2.Attach(new GmpiToSDK3Context::g3_TextFormat(this, b));

		b2->queryInterface(GmpiDrawing_API::SE_IID_TEXTFORMAT_MPGUI, reinterpret_cast<void**>(textFormat));
	}

	return (int32_t)hr;
}

inline int32_t MP_STDCALL GmpiToSDK3Factory::CreateImage(int32_t width, int32_t height, GmpiDrawing_API::IMpBitmap** returnDiBitmap)
{
	*returnDiBitmap = nullptr;

	gmpi::shared_ptr<gmpi::drawing::api::IBitmap> b;
	auto hr = native->createImage(width, height, (int32_t)gmpi::drawing::BitmapRenderTargetFlags::EightBitPixels, b.put());

	if (hr == gmpi::ReturnCode::Ok)
	{
		gmpi_sdk::mp_shared_ptr<gmpi::IMpUnknown> b2;
		b2.Attach(new GmpiToSDK3Context::g3_Bitmap(this, b));

		b2->queryInterface(GmpiDrawing_API::SE_IID_BITMAP_MPGUI, reinterpret_cast<void**>(returnDiBitmap));
	}

	return (int32_t)hr;
}

inline int32_t MP_STDCALL GmpiToSDK3Factory::LoadImageU(const char* utf8Uri, GmpiDrawing_API::IMpBitmap** returnDiBitmap)
{
	*returnDiBitmap = nullptr;

	gmpi::shared_ptr<gmpi::drawing::api::IBitmap> b;
	auto hr = native->loadImageU(utf8Uri, b.put());

	if (hr == gmpi::ReturnCode::Ok)
	{
		gmpi_sdk::mp_shared_ptr<gmpi::IMpUnknown> b2;
		b2.Attach(new GmpiToSDK3Context::g3_Bitmap(this, b));

		b2->queryInterface(GmpiDrawing_API::SE_IID_BITMAP_MPGUI, reinterpret_cast<void**>(returnDiBitmap));
	}

	return (int32_t)hr;
}

inline int32_t MP_STDCALL GmpiToSDK3Factory::GetFontFamilyName(int32_t fontIndex, gmpi::IString* returnString)
{
	return (int32_t) native->getFontFamilyName(fontIndex, (gmpi::api::IString*) returnString); // relying on IString being binary compatible with gmpi::api::IString
}

struct UniversalGraphicsContext2 : public gmpi::api::IUnknown
{
	gmpi::drawing::api::IDeviceContext* gmpiContext;
	GmpiToSDK3Context sdk3Context;

	UniversalGraphicsContext2(GmpiDrawing_API::IMpFactory2* factory, gmpi::drawing::api::IDeviceContext* nativeContext = {}) :
		gmpiContext(nativeContext),
		sdk3Context(factory, (gmpi::IMpUnknown*) static_cast<gmpi::api::IUnknown*>(this), nativeContext)
	{
	}

	gmpi::ReturnCode queryInterface(const gmpi::api::Guid* iid, void** returnInterface) override
	{
		*returnInterface = {};
		if (*iid == gmpi::drawing::api::IDeviceContext::guid || *iid == gmpi::drawing::api::IResource::guid)
		{
			return gmpiContext->queryInterface(iid, returnInterface);
		}
		if (
			*iid == *reinterpret_cast<const gmpi::api::Guid*>(&GmpiDrawing_API::SE_IID_DEVICECONTEXT_MPGUI) ||
			*iid == *reinterpret_cast<const gmpi::api::Guid*>(&GmpiDrawing_API::IMpDeviceContextExt::guid)
			)
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