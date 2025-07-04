#pragma once
#define _LIBCPP_DISABLE_DEPRECATION_WARNINGS 1
/*
#include "Cocoa_Gfx.h"
*/

#include <codecvt>
#include <map>
#include <mach-o/dyld.h>
#include "../se_sdk3/Drawing.h"
#include "./Gfx_base.h"
#include "BundleInfo.h"
#include "mfc_emulation.h"
#include "backends/CocoaGfx.h"

#define USE_BACKING_BUFFER 1

/* TODO

investigate CGContextSetShouldSmoothFonts, CGContextSetAllowsFontSmoothing (for less heavy fonts)
*/

namespace se
{
	namespace cocoa
	{
        class DrawingFactory;
    
		// Conversion utilities.
    
        inline NSPoint toNative(GmpiDrawing_API::MP1_POINT p)
        {
            return NSMakePoint(p.x, p.y);
        }
    
//		inline GmpiDrawing::Rect RectFromNSRect(NSRect nsr)
//		{
//			GmpiDrawing::Rect r(nsr.origin.x, nsr.origin.y, nsr.origin.x + nsr.size.width, nsr.origin.y + nsr.size.height);
//			return r;
//		}

		inline NSRect NSRectFromRect(GmpiDrawing_API::MP1_RECT rect)
		{
			return NSMakeRect(rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top);
		}
#if 0
        CGMutablePathRef NsToCGPath(NSBezierPath* geometry) // could be cached in some cases.
        {
            CGMutablePathRef cgPath = CGPathCreateMutable();
            NSInteger n = [geometry elementCount];
            
            for (NSInteger i = 0; i < n; i++) {
                NSPoint ps[3];
                switch ([geometry elementAtIndex:i associatedPoints:ps]) {
                    case NSMoveToBezierPathElement: {
                        CGPathMoveToPoint(cgPath, NULL, ps[0].x, ps[0].y);
                        break;
                    }
                    case NSLineToBezierPathElement: {
                        CGPathAddLineToPoint(cgPath, NULL, ps[0].x, ps[0].y);
                        break;
                    }
                    case NSCurveToBezierPathElement: {
                        CGPathAddCurveToPoint(cgPath, NULL, ps[0].x, ps[0].y, ps[1].x, ps[1].y, ps[2].x, ps[2].y);
                        break;
                    }
                    case NSClosePathBezierPathElement: {
                        CGPathCloseSubpath(cgPath);
                        break;
                    }
                    default:
                        assert(false && @"Invalid NSBezierPathElement");
                }
            }
            return cgPath;
        }
#endif
        // helper
        inline void SetNativePenStrokeStyle(NSBezierPath * path, GmpiDrawing_API::IMpStrokeStyle* strokeStyle)
        {
            GmpiDrawing_API::MP1_CAP_STYLE capstyle = strokeStyle == nullptr ? GmpiDrawing_API::MP1_CAP_STYLE_FLAT : strokeStyle->GetStartCap();
            
            switch(capstyle)
            {
                default:
                case GmpiDrawing_API::MP1_CAP_STYLE_FLAT:
                    [ path setLineCapStyle:NSLineCapStyleButt ];
                    break;
                    
                case GmpiDrawing_API::MP1_CAP_STYLE_SQUARE:
                    [ path setLineCapStyle:NSLineCapStyleSquare ];
                    break;
                    
                case GmpiDrawing_API::MP1_CAP_STYLE_TRIANGLE:
                case GmpiDrawing_API::MP1_CAP_STYLE_ROUND:
                    [ path setLineCapStyle:NSLineCapStyleRound ];
                    break;
            }
        }
    
        inline void applyDashStyleToPath(NSBezierPath* path, const GmpiDrawing_API::IMpStrokeStyle* strokeStyleIm, float strokeWidth)
        {
            auto strokeStyle = const_cast<GmpiDrawing_API::IMpStrokeStyle*>(strokeStyleIm); // work arround const-correctness issues.

            const auto dashStyle = strokeStyle ? strokeStyle->GetDashStyle() : GmpiDrawing_API::MP1_DASH_STYLE_SOLID;
            const auto phase = strokeStyle ? strokeStyle->GetDashOffset() : 0.0f;
            
            std::vector<CGFloat> dashes;
            
            switch(dashStyle)
            {
                case GmpiDrawing_API::MP1_DASH_STYLE_SOLID:
                    break;
                    
                case GmpiDrawing_API::MP1_DASH_STYLE_CUSTOM:
                {
                    std::vector<float> dashesf;
                    dashesf.resize(strokeStyle->GetDashesCount());
                    strokeStyle->GetDashes(dashesf.data(), static_cast<uint32_t>(dashesf.size()));
                    
                    for(auto d : dashesf)
                    {
                        dashes.push_back((CGFloat) (d * strokeWidth));
                    }
                }
                break;
                    
                case GmpiDrawing_API::MP1_DASH_STYLE_DOT:
                    dashes.push_back(0.0f);
                    dashes.push_back(strokeWidth * 2.f);
                    break;
                    
                case GmpiDrawing_API::MP1_DASH_STYLE_DASH_DOT:
                    dashes.push_back(strokeWidth * 2.f);
                    dashes.push_back(strokeWidth * 2.f);
                    dashes.push_back(0.f);
                    dashes.push_back(strokeWidth * 2.f);
                    break;
                    
                case GmpiDrawing_API::MP1_DASH_STYLE_DASH_DOT_DOT:
                    dashes.push_back(strokeWidth * 2.f);
                    dashes.push_back(strokeWidth * 2.f);
                    dashes.push_back(0.f);
                    dashes.push_back(strokeWidth * 2.f);
                    dashes.push_back(0.f);
                    dashes.push_back(strokeWidth * 2.f);
                    break;
                    
                case GmpiDrawing_API::MP1_DASH_STYLE_DASH:
                default:
                    dashes.push_back(strokeWidth * 2.f);
                    dashes.push_back(strokeWidth * 2.f);
                    break;
            };
                        
            [path setLineDash: dashes.data() count: dashes.size() phase: phase];
        }
#if 1
		// Classes without GetFactory()
		template<class MpInterface, class CocoaType>
		class CocoaWrapper : public MpInterface
		{
		protected:
			CocoaType* native_;

			virtual ~CocoaWrapper()
			{
#if !__has_feature(objc_arc)
                if (native_)
				{
//					[native_ release];
				}
#endif
			}

		public:
			CocoaWrapper(CocoaType* native) : native_(native) {}

			inline CocoaType* native()
			{
				return native_;
			}

			GMPI_REFCOUNT;
		};

		// Classes with GetFactory()
		template<class MpInterface, class CocoaType>
		class CocoaWrapperWithFactory : public CocoaWrapper<MpInterface, CocoaType>
		{
		protected:
			GmpiDrawing_API::IMpFactory* factory_;

		public:
			CocoaWrapperWithFactory(CocoaType* native, GmpiDrawing_API::IMpFactory* factory) : CocoaWrapper<MpInterface, CocoaType>(native), factory_(factory) {}

			void MP_STDCALL GetFactory(GmpiDrawing_API::IMpFactory **factory) override
			{
				*factory = factory_;
			}
		};

		class nothing
		{
		};
#endif
    
    class DrawingFactory : public GmpiDrawing_API::IMpFactory2
    {
    public:
        gmpi::cocoa::FactoryInfo& info;
        
        DrawingFactory(gmpi::cocoa::FactoryInfo& pinfo) : info(pinfo)
        {
        }
            
        // utility
        inline NSColor* toNative(const GmpiDrawing_API::MP1_COLOR& color)
        {
/*
            // This should be equivalent to sRGB, except it allows extended color components beyond 0.0 - 1.0
            // for plain old sRGB, colorWithCalibratedRed would be next best.
            
            return [NSColor colorWithDisplayP3Red:
                    (CGFloat)se_sdk::FastGamma::pixelToNormalised(se_sdk::FastGamma::float_to_sRGB(color.r))
                    green : (CGFloat)se_sdk::FastGamma::pixelToNormalised(se_sdk::FastGamma::float_to_sRGB(color.g))
                    blue : (CGFloat)se_sdk::FastGamma::pixelToNormalised(se_sdk::FastGamma::float_to_sRGB(color.b))
                    alpha : (CGFloat)color.a];
            
            // nativec_ = [NSColor colorWithRed:color.r green:color.g blue:color.b alpha:color.a]; // wrong gamma
 */
//            native2 = [[NSGradient alloc] initWithColors:colors atLocations: locations2.data() colorSpace: nsColorSpace];
            
            // same as manual sRGB conversion, but not linear alpha blending
 //           return [NSColor colorWithCalibratedRed:color.r green:color.g blue:color.b alpha:color.a];
// too dark in all screen spaces            return [NSColor colorWithCalibratedRed:color.r green:color.g blue:color.b alpha:color.a ];
#if 0
            // looks correct in all screen spaces, at least for brushes. shows quantization from conversion routines is only weakness.
            return [NSColor colorWithSRGBRed:
                    (CGFloat)se_sdk::FastGamma::pixelToNormalised(se_sdk::FastGamma::float_to_sRGB(color.r))
                    green : (CGFloat)se_sdk::FastGamma::pixelToNormalised(se_sdk::FastGamma::float_to_sRGB(color.g))
                    blue : (CGFloat)se_sdk::FastGamma::pixelToNormalised(se_sdk::FastGamma::float_to_sRGB(color.b))
                    alpha : (CGFloat)color.a];
#endif
            const CGFloat components[4] = {color.r, color.g, color.b, color.a};
            return [NSColor colorWithColorSpace:info.gmpiColorSpace components:components count:4];
            
// I think this is wrong because it's saying that the gmpi color is in the screen's color space. Which is a crapshoot out of our control.
// we need to supply our color in a known color space, then let the OS convert it to whatever unknown color-space the screen is using.
//            return [NSColor colorWithColorSpace:nsColorSpace components:components count:4]; // washed-out on big Sur (with extended linier) good with genericlinier
        }

        int32_t MP_STDCALL CreatePathGeometry(GmpiDrawing_API::IMpPathGeometry** pathGeometry) override;

        int32_t MP_STDCALL CreateTextFormat(const char* fontFamilyName, void* unused /* fontCollection */, GmpiDrawing_API::MP1_FONT_WEIGHT fontWeight, GmpiDrawing_API::MP1_FONT_STYLE fontStyle, GmpiDrawing_API::MP1_FONT_STRETCH fontStretch, float fontSize, void* unused2 /* localeName */, GmpiDrawing_API::IMpTextFormat** textFormat) override;

        int32_t MP_STDCALL CreateImage(int32_t width, int32_t height, GmpiDrawing_API::IMpBitmap** returnDiBitmap) override;

        int32_t MP_STDCALL LoadImageU(const char* utf8Uri, GmpiDrawing_API::IMpBitmap** returnDiBitmap) override;

        int32_t CreateStrokeStyle(const GmpiDrawing_API::MP1_STROKE_STYLE_PROPERTIES* strokeStyleProperties, float* dashes, int32_t dashesCount, GmpiDrawing_API::IMpStrokeStyle** returnValue) override
        {
            *returnValue = nullptr;

            gmpi_sdk::mp_shared_ptr<gmpi::IMpUnknown> b2;
            b2.Attach(new gmpi::generic_graphics::StrokeStyle(this, strokeStyleProperties, dashes, dashesCount));

            return b2->queryInterface(GmpiDrawing_API::SE_IID_STROKESTYLE_MPGUI, reinterpret_cast<void **>(returnValue));
        }

        // IMpFactory2
        int32_t MP_STDCALL GetFontFamilyName(int32_t fontIndex, gmpi::IString* returnString) override
        {
            if(info.supportedFontFamilies.empty())
            {
                NSFontManager* fontManager = [NSFontManager sharedFontManager];
    /*
                auto fontNames = [fontManager availableFonts]; // tends to add "-bold" or "-italic" on end of name
    */
                //for IOS see [UIFont familynames]
                NSArray* fontFamilies = nil;
                try
                {
                    fontFamilies = [fontManager availableFontFamilies];
                }
                catch(...)
                {
                    _RPT0(0, "NSFontManager threw!");
                } // Logic Pro may throw an Memory exception here. Don't know why. Maybe due to it using a AU2 wrapper.

                for( NSString* familyName in fontFamilies)
                {
                    info.supportedFontFamilies.push_back([familyName UTF8String]);
                }
            }
            
            if (fontIndex < 0 || fontIndex >= info.supportedFontFamilies.size())
            {
                return gmpi::MP_FAIL;
            }

            returnString->setData(info.supportedFontFamilies[fontIndex].data(), static_cast<int32_t>(info.supportedFontFamilies[fontIndex].size()));
            return gmpi::MP_OK;
        }

        int32_t MP_STDCALL queryInterface(const gmpi::MpGuid& iid, void** returnInterface) override
        {
            *returnInterface = 0;
            if ( iid == GmpiDrawing_API::SE_IID_FACTORY2_MPGUI || iid == GmpiDrawing_API::SE_IID_FACTORY_MPGUI || iid == gmpi::MP_IID_UNKNOWN)
            {
                *returnInterface = reinterpret_cast<GmpiDrawing_API::IMpFactory2*>(this);
                addRef();
                return gmpi::MP_OK;
            }
            return gmpi::MP_NOSUPPORT;
        }

        GMPI_REFCOUNT_NO_DELETE;
    };
/*
		class Brush : / * public GmpiDrawing_API::IMpBrush,* / public CocoaWrapperWithFactory<GmpiDrawing_API::IMpBrush, nothing> // Resource
		{
		public:
			Brush(GmpiDrawing_API::IMpFactory* factory) : CocoaWrapperWithFactory(nullptr, factory) {}
		};
*/
        
        class CocoaBrushBase
        {
		protected:
            se::cocoa::DrawingFactory* factory_;
            
        public:
			CocoaBrushBase(se::cocoa::DrawingFactory* pfactory) :
                factory_(pfactory)
            {}
            
            virtual ~CocoaBrushBase(){}
            
			virtual void FillPath(class GraphicsContext* context, NSBezierPath* nsPath) const = 0;

            // Default to black fill for fancy brushes that don't implement line drawing yet.
            virtual void StrokePath(NSBezierPath* nsPath, float strokeWidth, const GmpiDrawing_API::IMpStrokeStyle* strokeStyle = nullptr) const
            {
                [[NSColor blackColor] set]; /// !!!TODO!!!, color set to black always.
                
                [nsPath setLineWidth : strokeWidth];
                SetNativePenStrokeStyle(nsPath, (GmpiDrawing_API::IMpStrokeStyle*) strokeStyle);
                
                [nsPath stroke];
            }
        };

		class SolidColorBrush : public GmpiDrawing_API::IMpSolidColorBrush, public CocoaBrushBase
		{
			GmpiDrawing_API::MP1_COLOR color;
			NSColor* nativec_ = nullptr;
            
            inline void setNativeColor()
            {
                nativec_ = factory_->toNative(color);
                [nativec_ retain];
            }
            
		public:
			SolidColorBrush(const GmpiDrawing_API::MP1_COLOR* pcolor, se::cocoa::DrawingFactory* factory) : CocoaBrushBase(factory)
				,color(*pcolor)
			{
                setNativeColor();
			}

            inline NSColor* nativeColor() const
            {
                return nativec_;
            }

            void FillPath(GraphicsContext* context, NSBezierPath* nsPath) const override
            {
                [nativec_ set];
                [nsPath fill];
            }

            void StrokePath(NSBezierPath* nsPath, float strokeWidth, const GmpiDrawing_API::IMpStrokeStyle* strokeStyle = nullptr) const override
			{
				[nativec_ set];
				[nsPath setLineWidth : strokeWidth];
                SetNativePenStrokeStyle(nsPath, (GmpiDrawing_API::IMpStrokeStyle*) strokeStyle);
                
                [nsPath stroke];
			}

			~SolidColorBrush()
			{
				[nativec_ release];
			}

			// IMPORTANT: Virtual functions much 100% match GmpiDrawing_API::IMpSolidColorBrush to simulate inheritance.
			void MP_STDCALL SetColor(const GmpiDrawing_API::MP1_COLOR* pcolor) override
			{
				color = *pcolor;
                setNativeColor();
			}

			GmpiDrawing_API::MP1_COLOR MP_STDCALL GetColor() override
			{
				return color;
			}

            void MP_STDCALL GetFactory(GmpiDrawing_API::IMpFactory **factory) override
            {
                *factory = factory_;
            }
            
            GMPI_QUERYINTERFACE1(GmpiDrawing_API::SE_IID_SOLIDCOLORBRUSH_MPGUI, GmpiDrawing_API::IMpSolidColorBrush);
			GMPI_REFCOUNT;
		};

		class GradientStopCollection : public CocoaWrapperWithFactory<GmpiDrawing_API::IMpGradientStopCollection, nothing>
		{
		public:
			std::vector<GmpiDrawing_API::MP1_GRADIENT_STOP> gradientstops;

			GradientStopCollection(GmpiDrawing_API::IMpFactory* factory, const GmpiDrawing_API::MP1_GRADIENT_STOP* gradientStops, uint32_t gradientStopsCount) : CocoaWrapperWithFactory(nullptr, factory)
			{
				for (uint32_t i = 0; i < gradientStopsCount; ++i)
				{
					gradientstops.push_back(gradientStops[i]);
				}
			}
			GMPI_QUERYINTERFACE1(GmpiDrawing_API::SE_IID_GRADIENTSTOPCOLLECTION_MPGUI, GmpiDrawing_API::IMpGradientStopCollection);
		};

        class Gradient
        {
        protected:
            NSGradient* native2 = {};
            
        public:
            Gradient(se::cocoa::DrawingFactory* factory, const GmpiDrawing_API::IMpGradientStopCollection* gradientStopCollection)
            {
                auto stops = static_cast<const GradientStopCollection*>(gradientStopCollection);

                NSMutableArray* colors = [NSMutableArray array];
                std::vector<CGFloat> locations2;

                // reversed, so radial gradient draws same as PC
                for (auto it = stops->gradientstops.rbegin() ; it != stops->gradientstops.rend() ; ++it)//  auto& stop : stops->gradientstops)
                {
                    const auto& stop = *it;
                    [colors addObject: factory->toNative(stop.color)];
                    locations2.push_back(1.0 - stop.position);
                }
/* faded on big sur
                CGColorSpaceRef colorSpace = CGColorSpaceCreateWithName(kCGColorSpaceGenericRGBLinear);
                NSColorSpace* nsColorSpace = [[NSColorSpace alloc] initWithCGColorSpace:colorSpace];

                native2 = [[NSGradient alloc] initWithColors:colors atLocations: locations2.data() colorSpace: nsColorSpace];
*/
                //native2 = [[NSGradient alloc] initWithColors:colors atLocations: locations2.data() colorSpace: [NSColorSpace genericRGBColorSpace]]; // too dark on genericlinier window cs

//                CFRelease(colorSpace);
                
                native2 = [[NSGradient alloc] initWithColors:colors atLocations: locations2.data() colorSpace:factory->info.gmpiColorSpace];
            }
            
            ~Gradient()
            {
                [native2 release];
            }
        };
    
		class LinearGradientBrush : public GmpiDrawing_API::IMpLinearGradientBrush, public CocoaBrushBase, public Gradient
		{
            GmpiDrawing_API::MP1_LINEAR_GRADIENT_BRUSH_PROPERTIES brushProperties;
            
		public:
			LinearGradientBrush(
                se::cocoa::DrawingFactory* factory,
                const GmpiDrawing_API::MP1_LINEAR_GRADIENT_BRUSH_PROPERTIES* linearGradientBrushProperties,
                const GmpiDrawing_API::MP1_BRUSH_PROPERTIES* brushProperties,
                const GmpiDrawing_API::IMpGradientStopCollection* gradientStopCollection) :
                    CocoaBrushBase(factory)
                    , Gradient(factory, gradientStopCollection)
                    , brushProperties(*linearGradientBrushProperties)
			{
			}
            
        public:

			float getAngle() const
			{
				// TODO cache. e.g. nan = not calculated yet.
				return (180.0f / M_PI) * atan2(brushProperties.endPoint.y - brushProperties.startPoint.y, brushProperties.endPoint.x - brushProperties.startPoint.x);
			}

			// IMPORTANT: Virtual functions much 100% match simulated interface.
			void MP_STDCALL SetStartPoint(GmpiDrawing_API::MP1_POINT pstartPoint) override
			{
                brushProperties.startPoint = pstartPoint;
			}
			void MP_STDCALL SetEndPoint(GmpiDrawing_API::MP1_POINT pendPoint) override
			{
                brushProperties.endPoint = pendPoint;
			}

			void FillPath(GraphicsContext* context, NSBezierPath* nsPath) const override
			{
//				[native2 drawInBezierPath:nsPath angle : getAngle()];
                
                // If you plan to do more drawing later, it's a good idea
                // to save the graphics state before clipping.
                [NSGraphicsContext saveGraphicsState];
                
                // clip following output to the path
                [nsPath addClip];
                
                [native2 drawFromPoint:toNative(brushProperties.endPoint) toPoint:toNative(brushProperties.startPoint) options:NSGradientDrawsBeforeStartingLocation|NSGradientDrawsAfterEndingLocation];

                // restore clip region
                [NSGraphicsContext restoreGraphicsState];
			}
            
            void StrokePath(NSBezierPath* nsPath, float strokeWidth, const GmpiDrawing_API::IMpStrokeStyle* strokeStyle = nullptr) const override
			{
                SetNativePenStrokeStyle(nsPath, (GmpiDrawing_API::IMpStrokeStyle*) strokeStyle);

                // convert NSPath to CGPath
                CGPathRef strokePath;
			{
                CGMutablePathRef cgPath = gmpi::cocoa::NsToCGPath(nsPath);

                    strokePath = CGPathCreateCopyByStrokingPath(cgPath, NULL, strokeWidth, (CGLineCap)[nsPath lineCapStyle],
                                                                          (CGLineJoin)[nsPath lineJoinStyle], [nsPath miterLimit]);
                    CGPathRelease(cgPath);
				}
                
               
                // If you plan to do more drawing later, it's a good idea
                // to save the graphics state before clipping.
                [NSGraphicsContext saveGraphicsState];
                
                // clip following output to the path
                CGContextRef ctx = (CGContextRef) [[NSGraphicsContext currentContext] graphicsPort];

                CGContextAddPath(ctx, strokePath);
                CGContextClip(ctx);
                
                [native2 drawFromPoint:toNative(brushProperties.endPoint) toPoint:toNative(brushProperties.startPoint) options:NSGradientDrawsBeforeStartingLocation|NSGradientDrawsAfterEndingLocation];

                // restore clip region
                [NSGraphicsContext restoreGraphicsState];
                
                CGPathRelease(strokePath);
			}
            
            void MP_STDCALL GetFactory(GmpiDrawing_API::IMpFactory **factory) override
            {
                *factory = factory_;
            }

			GMPI_REFCOUNT;
			GMPI_QUERYINTERFACE1(GmpiDrawing_API::SE_IID_LINEARGRADIENTBRUSH_MPGUI, GmpiDrawing_API::IMpLinearGradientBrush);
		};

		class RadialGradientBrush : public GmpiDrawing_API::IMpRadialGradientBrush, public CocoaBrushBase, public Gradient
		{
            GmpiDrawing_API::MP1_RADIAL_GRADIENT_BRUSH_PROPERTIES gradientProperties;

		public:
			RadialGradientBrush(se::cocoa::DrawingFactory* factory, const GmpiDrawing_API::MP1_RADIAL_GRADIENT_BRUSH_PROPERTIES* radialGradientBrushProperties, const GmpiDrawing_API::MP1_BRUSH_PROPERTIES* brushProperties, const  GmpiDrawing_API::IMpGradientStopCollection* gradientStopCollection) :
                CocoaBrushBase(factory)
                ,Gradient(factory, gradientStopCollection)
                ,gradientProperties(*radialGradientBrushProperties)
			{
			}

			void FillPath(GraphicsContext* context, NSBezierPath* nsPath) const override
			{
                const auto bounds = [nsPath bounds];
                
                const auto centerX = bounds.origin.x + 0.5 * std::max(0.1, bounds.size.width);
                const auto centerY = bounds.origin.y + 0.5 * std::max(0.1, bounds.size.height);
                
 //               auto relativeX = (gradientProperties.center.x - centerX) / (0.5 * bounds.size.width);
 //               auto relativeY = (gradientProperties.center.y - centerY) / (0.5 * bounds.size.height);
                
//                relativeX = std::max(-1.0, std::min(1.0, relativeX));
//                relativeY = std::max(-1.0, std::min(1.0, relativeY));
                
                const auto origin = NSMakePoint(
                    gradientProperties.center.x + gradientProperties.gradientOriginOffset.x,
                    gradientProperties.center.y + gradientProperties.gradientOriginOffset.y);
                
                // If you plan to do more drawing later, it's a good idea
                // to save the graphics state before clipping.
                [NSGraphicsContext saveGraphicsState];
                
                // clip following output to the path
                [nsPath addClip];
 /*
                [native2 drawFromCenter:origin
                    radius:0.0
                    toCenter:toNative(gradientProperties.center)
                    radius:gradientProperties.radiusX
                    options:NSGradientDrawsAfterEndingLocation];
*/
                
                [native2 drawFromCenter:toNative(gradientProperties.center)
                    radius:gradientProperties.radiusX
                    toCenter:origin
                    radius:0.0
                    options:NSGradientDrawsBeforeStartingLocation|NSGradientDrawsAfterEndingLocation];
                
                // restore clip region
                [NSGraphicsContext restoreGraphicsState];
			}

            void StrokePath(NSBezierPath* nsPath, float strokeWidth, const GmpiDrawing_API::IMpStrokeStyle* strokeStyle = nullptr) const override
            {
                SetNativePenStrokeStyle(nsPath, (GmpiDrawing_API::IMpStrokeStyle*) strokeStyle);

                // convert NSPath to CGPath
                CGPathRef strokePath;
                {
                    CGMutablePathRef cgPath = gmpi::cocoa::NsToCGPath(nsPath);

                    strokePath = CGPathCreateCopyByStrokingPath(cgPath, NULL, strokeWidth, (CGLineCap)[nsPath lineCapStyle],
                                                                          (CGLineJoin)[nsPath lineJoinStyle], [nsPath miterLimit]);
                    CGPathRelease(cgPath);
                }
                
                const auto origin = NSMakePoint(
                    gradientProperties.center.x + gradientProperties.gradientOriginOffset.x,
                    gradientProperties.center.y + gradientProperties.gradientOriginOffset.y);

                // If you plan to do more drawing later, it's a good idea
                // to save the graphics state before clipping.
                [NSGraphicsContext saveGraphicsState];
                
                // clip following output to the path
                CGContextRef ctx = (CGContextRef) [[NSGraphicsContext currentContext] graphicsPort];

                CGContextAddPath(ctx, strokePath);
                CGContextClip(ctx);
                
                [native2 drawFromCenter:toNative(gradientProperties.center)
                    radius:gradientProperties.radiusX
                    toCenter:origin
                    radius:0.0
                    options:NSGradientDrawsBeforeStartingLocation|NSGradientDrawsAfterEndingLocation];

                // restore clip region
                [NSGraphicsContext restoreGraphicsState];
                
                CGPathRelease(strokePath);
            }

			void MP_STDCALL SetCenter(GmpiDrawing_API::MP1_POINT pcenter) override
			{
                gradientProperties.center = pcenter;
			}

			void MP_STDCALL SetGradientOriginOffset(GmpiDrawing_API::MP1_POINT gradientOriginOffset) override
			{
			}

			void MP_STDCALL SetRadiusX(float radiusX) override
			{
                gradientProperties.radiusX = radiusX;
			}

			void MP_STDCALL SetRadiusY(float radiusY) override
			{
                gradientProperties.radiusY = radiusY;
			}
            
            void MP_STDCALL GetFactory(GmpiDrawing_API::IMpFactory **factory) override
            {
                *factory = factory_;
            }
            
			GMPI_REFCOUNT;
			GMPI_QUERYINTERFACE1(GmpiDrawing_API::SE_IID_RADIALGRADIENTBRUSH_MPGUI, GmpiDrawing_API::IMpRadialGradientBrush);
		};

		class TextFormat: public GmpiDrawing_API::IMpTextFormat // : public CocoaWrapper<GmpiDrawing_API::IMpTextFormat, const __CFDictionary>
		{
		public:
			std::string fontFamilyName;
			GmpiDrawing_API::MP1_FONT_WEIGHT fontWeight;
			GmpiDrawing_API::MP1_FONT_STYLE fontStyle;
			GmpiDrawing_API::MP1_FONT_STRETCH fontStretch;
			GmpiDrawing_API::MP1_TEXT_ALIGNMENT textAlignment;
			GmpiDrawing_API::MP1_PARAGRAPH_ALIGNMENT paragraphAlignment;
            GmpiDrawing_API::MP1_WORD_WRAPPING wordWrapping = GmpiDrawing_API::MP1_WORD_WRAPPING_WRAP;
			float fontSize;
			bool useLegacyBaseLineSnapping = true;
            
            // Cache some constants to make snap calculation faster.
            float topAdjustment = {}; // Mac includes extra space at top in font bounding box.
            float ascent = {};
            float baselineCorrection = {};

            NSMutableDictionary* native2 = {};
            NSMutableParagraphStyle* nativeStyle = {};
            
			static const char* fontSubstitute(const char* windowsFont)
			{
				static std::map< std::string, std::string > substitutes =
				{
                    // Windows, macOS
					{"Tahoma",              "Geneva"},
					{"MS Sans Serif",       "Geneva"},
					{"MS Serif",            "New York"},
					{"Lucida Console",      "Monaco"},
					{"Lucida Sans Unicode", "Lucida Grande"},
					{"Palatino Linotype",   "Palatino"},
					{"Book Antiqua",        "Palatino"},
//nope                    {"Verdana UI",          "Verdana"},
				};

				const auto it = substitutes.find(windowsFont);
				if (it != substitutes.end())
				{
					return (*it).second.c_str();
				}

				return windowsFont;
			}

			TextFormat(std::wstring_convert<std::codecvt_utf8<wchar_t>>* pstringConverter, const char* pfontFamilyName, GmpiDrawing_API::MP1_FONT_WEIGHT pfontWeight, GmpiDrawing_API::MP1_FONT_STYLE pfontStyle, GmpiDrawing_API::MP1_FONT_STRETCH pfontStretch, float pfontSize) :
				fontWeight(pfontWeight)
				, fontStyle(pfontStyle)
				, fontStretch(pfontStretch)
				, fontSize(pfontSize)
			{
				fontFamilyName = fontSubstitute(pfontFamilyName);
                
                NSFontTraitMask fontTraits = 0;
                if(pfontWeight >= GmpiDrawing_API::MP1_FONT_WEIGHT_DEMI_BOLD)
                    fontTraits |= NSBoldFontMask;
                
                // Simulate Oblique with Italic
                if(pfontStyle == GmpiDrawing_API::MP1_FONT_STYLE_ITALIC || pfontStyle == GmpiDrawing_API::MP1_FONT_STYLE_OBLIQUE)
                    fontTraits |= NSItalicFontMask;
                
                auto nsFontName = [NSString stringWithCString: fontFamilyName.c_str() encoding: NSUTF8StringEncoding ];
                NSFont* nativefont = {};
                
                // AU plugin on Logic Pro (ARM) asserts first time here with a memory error.
                // not sure why, but ignoring it seems to work OK.
                try
                {
                    const int roundNearest = 50;
                    const int nativeFontWeight = 1 + (pfontWeight + roundNearest) / 100;

                    auto fm = [NSFontManager sharedFontManager];
                    nativefont = [fm fontWithFamily:nsFontName traits:fontTraits weight:nativeFontWeight size:fontSize ];
                }
                catch(...)
                {
                    _RPT0(0, "NSFontManager threw!");
                } // Logic Pro may throw an Memory exception here. Don't know why. Maybe due to it using a AU2 wrapper.
                
                // fallback to system font if nesc.
                if(!nativefont)
                {
                    // systemFontOfSize uses a different weight system
                    static const CGFloat weightConversion[] = {
                        NSFontWeightUltraLight, // MP1_FONT_WEIGHT_THIN = 100
                        NSFontWeightThin,       // MP1_FONT_WEIGHT_ULTRA_LIGHT = 200
                        NSFontWeightLight,      // MP1_FONT_WEIGHT_LIGHT = 300
                        NSFontWeightRegular,    // MP1_FONT_WEIGHT_NORMAL = 400
                        NSFontWeightMedium,     // MP1_FONT_WEIGHT_MEDIUM = 500
                        NSFontWeightSemibold,   // MP1_FONT_WEIGHT_SEMI_BOLD = 600
                        NSFontWeightBold,       // MP1_FONT_WEIGHT_BOLD = 700
                        NSFontWeightHeavy,      // MP1_FONT_WEIGHT_BLACK = 900
                        NSFontWeightBlack       // MP1_FONT_WEIGHT_ULTRA_BLACK = 950
                    };

                    const int arrMax = std::size(weightConversion) - 1;
                    const int roundNearest = 50;
                    const int nativeFontWeightIndex
                        = std::max(0, std::min(arrMax, -1 + (pfontWeight + roundNearest) / 100));
                    const auto nativeFontWeight = weightConversion[nativeFontWeightIndex];

                    // final fallback. system font.
                    nativefont = [NSFont systemFontOfSize:fontSize weight:nativeFontWeight];
                }

				nativeStyle = [[NSMutableParagraphStyle alloc] init];
                
//auto test = [NSMutableParagraphStyle defaultParagraphStyle];
                
				[nativeStyle setAlignment : NSTextAlignmentLeft];
                
                // line height seems to vary between Apple DAWs and others.
                // might need to investigate lineHeight and lineSpacing settings also
                const CGFloat fixedLineHeight = [nativefont leading] + [nativefont ascender] - [nativefont descender];
                [nativeStyle setMinimumLineHeight:fixedLineHeight];
                [nativeStyle setMaximumLineHeight:fixedLineHeight];
                
				native2 = [[NSMutableDictionary alloc] initWithObjectsAndKeys:
                           nativefont   , NSFontAttributeName,
                           nativeStyle  , NSParagraphStyleAttributeName,
                           nil];
                
                CalculateTopAdjustment();
            }
            
            ~TextFormat()
            {
//                   _RPT1(0, "~TextFormat() %d\n", this);

#if !__has_feature(objc_arc)
//                [native2 release];
#endif
                [native2 release];
                [nativeStyle release];
            }
            
            void CalculateTopAdjustment()
            {
                // Calculate compensation for different bounding box height between mac and direct2D.
                // On Direct2D bounding rect height is typicaly much less than Cocoa.
                // I don't know any algorithm for converting the extra height.
                // Fix is to disregard extra height on both platforms.
                GmpiDrawing_API::MP1_FONT_METRICS fontMetrics {};
                GetFontMetrics(&fontMetrics);
                    
                auto boundingBoxSize = [@"A" sizeWithAttributes : native2];
                
                topAdjustment = boundingBoxSize.height - (fontMetrics.ascent + fontMetrics.descent);
                ascent = fontMetrics.ascent;
                
                baselineCorrection = 0.5f;
                if((fontMetrics.descent - floor(fontMetrics.descent)) <= 0.5f)
                {
                    baselineCorrection += 0.5f;
                }
			}

			int32_t MP_STDCALL SetTextAlignment(GmpiDrawing_API::MP1_TEXT_ALIGNMENT ptextAlignment) override
			{
				textAlignment = ptextAlignment;

				switch (textAlignment)
				{
				case (int)GmpiDrawing::TextAlignment::Leading: // Left.
					[native2[NSParagraphStyleAttributeName] setAlignment:NSTextAlignmentLeft];
					break;
				case (int)GmpiDrawing::TextAlignment::Trailing: // Right
					[native2[NSParagraphStyleAttributeName] setAlignment:NSTextAlignmentRight];
					break;
				case (int)GmpiDrawing::TextAlignment::Center:
					[native2[NSParagraphStyleAttributeName] setAlignment:NSTextAlignmentCenter];
					break;
				}

				return gmpi::MP_OK;
			}

			int32_t MP_STDCALL SetParagraphAlignment(GmpiDrawing_API::MP1_PARAGRAPH_ALIGNMENT pparagraphAlignment) override
			{
				paragraphAlignment = pparagraphAlignment;
				return gmpi::MP_OK;
			}

			int32_t MP_STDCALL SetWordWrapping(GmpiDrawing_API::MP1_WORD_WRAPPING pwordWrapping) override
			{
                wordWrapping = pwordWrapping;
				return gmpi::MP_OK;
			}

			int32_t MP_STDCALL SetLineSpacing(float lineSpacing, float baseline) override
			{
				// Hack, reuse this method to enable legacy-mode.
				if (IMpTextFormat::ImprovedVerticalBaselineSnapping == lineSpacing)
				{
					useLegacyBaseLineSnapping = false;
					return gmpi::MP_OK;
				}

				return gmpi::MP_NOSUPPORT;
			}

			int32_t MP_STDCALL GetFontMetrics(GmpiDrawing_API::MP1_FONT_METRICS* returnFontMetrics) override
			{
                NSFont* font = native2[NSFontAttributeName];  // Get font from dictionary.
                returnFontMetrics->xHeight = [font xHeight];
				returnFontMetrics->ascent = [font ascender];
				returnFontMetrics->descent = -[font descender]; // Descent is negative on OSX (positive on Windows)
				returnFontMetrics->lineGap = [font leading];
				returnFontMetrics->capHeight = [font capHeight];
				returnFontMetrics->underlinePosition = [font underlinePosition];
				returnFontMetrics->underlineThickness = [font underlineThickness];
				returnFontMetrics->strikethroughPosition = returnFontMetrics->xHeight / 2;
				returnFontMetrics->strikethroughThickness = returnFontMetrics->underlineThickness;
/* same
                auto fontRef = CGFontCreateWithFontName((CFStringRef) [font fontName]);
                float d = CGFontGetDescent(fontRef);
                float x = CGFontGetUnitsPerEm(fontRef);
                float descent = d/x;
                float descent2 = descent * fontSize;
                CFRelease(fontRef);
*/
				return gmpi::MP_OK;
			}

			// TODO!!!: Probly needs to accept constraint rect like DirectWrite. !!!
			void MP_STDCALL GetTextExtentU(const char* utf8String, int32_t stringLength, GmpiDrawing_API::MP1_SIZE* returnSize) override
			{
				auto str = [NSString stringWithCString : utf8String encoding : NSUTF8StringEncoding];

				auto r = [str sizeWithAttributes : native2];
				returnSize->width = r.width;
                returnSize->height = r.height;// - topAdjustment;

				if (!useLegacyBaseLineSnapping)
				{
					returnSize->height -= topAdjustment;
				}
 			}
			
			bool getUseLegacyBaseLineSnapping() const
			{
				return useLegacyBaseLineSnapping;
			}

			GMPI_QUERYINTERFACE1(GmpiDrawing_API::SE_IID_TEXTFORMAT_MPGUI, GmpiDrawing_API::IMpTextFormat);
			GMPI_REFCOUNT;
		};


		class bitmapPixels final : public GmpiDrawing_API::IMpBitmapPixels
		{
			int bytesPerRow;
            class Bitmap* seBitmap = {};
            NSImage** inBitmap_;
            NSBitmapImageRep* bitmap2 = {};
			int32_t flags;
            
        public:
            bitmapPixels(Bitmap* bitmap /*NSImage** inBitmap*/, bool _alphaPremultiplied, int32_t pflags);
			~bitmapPixels();

			uint8_t* MP_STDCALL getAddress() const override
            {
                return static_cast<uint8_t*>([bitmap2 bitmapData]);
            }
			int32_t MP_STDCALL getBytesPerRow() const override { return bytesPerRow; }
			int32_t MP_STDCALL getPixelFormat() const override { return kRGBA; }

			inline uint8_t fast8bitScale(uint8_t a, uint8_t b) const
			{
				int t = (int)a * (int)b;
				return (uint8_t)((t + 1 + (t >> 8)) >> 8); // fast way to divide by 255
			}

            GMPI_QUERYINTERFACE1(GmpiDrawing_API::SE_IID_BITMAP_PIXELS_MPGUI, GmpiDrawing_API::IMpBitmapPixels);
			GMPI_REFCOUNT;
		};

		class Bitmap : public GmpiDrawing_API::IMpBitmap
		{
			GmpiDrawing_API::IMpFactory* factory = nullptr;

		public:
			NSImage* nativeBitmap_ = nullptr;
			NSBitmapImageRep* additiveBitmap_ = nullptr;

			Bitmap(GmpiDrawing_API::IMpFactory* pfactory, const char* utf8Uri) :
				nativeBitmap_(nullptr)
				, factory(pfactory)
			{
                // is this an in-memory resource?
                std::string uriString(utf8Uri);
                std::string binaryData;
                if (uriString.find(BundleInfo::resourceTypeScheme) == 0)
                {
//                    _RPT1(0, "Bitmap() A1: %d\n", this);
                
                    binaryData = BundleInfo::instance()->getResource(utf8Uri + strlen(BundleInfo::resourceTypeScheme));
                    
                    nativeBitmap_ = [[NSImage alloc] initWithData:[NSData dataWithBytes:(binaryData.data())
                                                                          length:binaryData.size()]];
                }
                else
                {
//                    _RPT1(0, "Bitmap() A2: %d\n", this);
                    
                    NSString * url = [NSString stringWithCString : utf8Uri encoding : NSUTF8StringEncoding];
                    nativeBitmap_ = [[NSImage alloc] initWithContentsOfFile:url];
                }
                
                // undo scaling of image (which reports scaled size, screwing up animation frame).
                NSSize max = NSZeroSize;
                for (NSObject* o in nativeBitmap_.representations) {
                    if ([o isKindOfClass: NSImageRep.class]) {
                        NSImageRep* r = (NSImageRep*)o;
                        if (r.pixelsWide != NSImageRepMatchesDevice && r.pixelsHigh != NSImageRepMatchesDevice) {
                            max.width = MAX(max.width, r.pixelsWide);
                            max.height = MAX(max.height, r.pixelsHigh);
                        }
                    }
                }
                if (max.width > 0 && max.height > 0) {
                    nativeBitmap_.size = max;
                }
			}
            
            Bitmap(GmpiDrawing_API::IMpFactory* pfactory, int32_t width, int32_t height)
				: factory(pfactory)
            {
//                _RPT1(0, "Bitmap() B: %d\n", this);
                
                nativeBitmap_ = [[NSImage alloc] initWithSize:NSMakeSize((CGFloat)width, (CGFloat)height)];
// not sure yet                [nativeBitmap_ setFlipped:TRUE];
                
#if !__has_feature(objc_arc)
//                [nativeBitmap_ retain];
#endif
            }

			Bitmap(GmpiDrawing_API::IMpFactory* pfactory, NSImage* native) : nativeBitmap_(native)
				, factory(pfactory)
			{
//               _RPT1(0, "Bitmap() C: %d\n", this);
               [nativeBitmap_ retain];
#if !__has_feature(objc_arc)
//                [nativeBitmap_ retain];
#endif
            }

			bool isLoaded()
			{
				return nativeBitmap_ != nil;
			}

			virtual ~Bitmap()
			{
//                _RPT1(0, "~Bitmap() %d\n", this);
         
#if !__has_feature(objc_arc)
//                [nativeBitmap_ release];
#endif
                if(nativeBitmap_)
                {
                    [nativeBitmap_ release];
                }
                if(additiveBitmap_)
                {
                    [additiveBitmap_ release];
                }
			}

			inline NSImage* GetNativeBitmap()
			{
				return nativeBitmap_;
			}

			GmpiDrawing_API::MP1_SIZE MP_STDCALL GetSizeF() override
			{
				NSSize s = [nativeBitmap_ size];
				return GmpiDrawing::Size(s.width, s.height);
			}

			int32_t MP_STDCALL GetSize(GmpiDrawing_API::MP1_SIZE_U* returnSize) override
			{
				NSSize s = [nativeBitmap_ size];

				returnSize->width = static_cast<int32_t>(0.5f + s.width);
				returnSize->height = static_cast<int32_t>(0.5f + s.height);

				/* hmm, assumes image representation at index 0 is actual size. size is already set correctly in constructor.
				 * 
#ifdef _DEBUG
                // Check images NOT scaled by cocoa.
                // https://stackoverflow.com/questions/2190027/nsimage-acting-weird
                NSImageRep *rep = [[nativeBitmap_ representations] objectAtIndex:0];
                
                assert(returnSize->width == rep.pixelsWide);
                assert(returnSize->height == rep.pixelsHigh);
#endif
				 */
				return gmpi::MP_OK;
			}

			int32_t MP_STDCALL lockPixelsOld(GmpiDrawing_API::IMpBitmapPixels** returnInterface, bool alphaPremultiplied) override
			{
				*returnInterface = 0;
return gmpi::MP_FAIL;
/* TODO!!!
				gmpi_sdk::mp_shared_ptr<gmpi::IMpUnknown> b2;
				b2.Attach(new bitmapPixels(&nativeBitmap_, alphaPremultiplied, GmpiDrawing_API::MP1_BITMAP_LOCK_READ | GmpiDrawing_API::MP1_BITMAP_LOCK_WRITE));

				return b2->queryInterface(GmpiDrawing_API::SE_IID_BITMAP_PIXELS_MPGUI, (void**)(returnInterface));
 */
			}

			int32_t MP_STDCALL lockPixels(GmpiDrawing_API::IMpBitmapPixels** returnInterface, int32_t flags) override
			{
//               _RPT1(0, "Bitmap() lockPixels: %d\n", this);
				*returnInterface = 0;

				gmpi_sdk::mp_shared_ptr<gmpi::IMpUnknown> b2;
				b2.Attach(new bitmapPixels(this /*&nativeBitmap_*/, true, flags));

				return b2->queryInterface(GmpiDrawing_API::SE_IID_BITMAP_PIXELS_MPGUI, (void**)(returnInterface));
			}

			void MP_STDCALL ApplyAlphaCorrection() override {}

			void ApplyAlphaCorrection2() {}

			void MP_STDCALL GetFactory(GmpiDrawing_API::IMpFactory** rfactory) override
			{
				*rfactory = factory;
			}

			GMPI_QUERYINTERFACE1(GmpiDrawing_API::SE_IID_BITMAP_MPGUI, GmpiDrawing_API::IMpBitmap);
			GMPI_REFCOUNT;
		};

        class BitmapBrush : public GmpiDrawing_API::IMpBitmapBrush, public CocoaBrushBase
        {
            Bitmap bitmap_;
            GmpiDrawing_API::MP1_BITMAP_BRUSH_PROPERTIES bitmapBrushProperties_;
            GmpiDrawing_API::MP1_BRUSH_PROPERTIES brushProperties_;

        public:
            BitmapBrush(
                se::cocoa::DrawingFactory* factory,
                const GmpiDrawing_API::IMpBitmap* bitmap,
                const GmpiDrawing_API::MP1_BITMAP_BRUSH_PROPERTIES* bitmapBrushProperties,
                const GmpiDrawing_API::MP1_BRUSH_PROPERTIES* brushProperties
            )
             : CocoaBrushBase(factory),
             bitmap_(factory, ((Bitmap*)bitmap)->nativeBitmap_),
             bitmapBrushProperties_(*bitmapBrushProperties),
             brushProperties_(*brushProperties)
            {
            }
            
            void StrokePath(NSBezierPath* nsPath, float strokeWidth, const GmpiDrawing_API::IMpStrokeStyle* strokeStyle = nullptr) const override
            {
                [[NSColor colorWithPatternImage:bitmap_.nativeBitmap_] set];
                
                [nsPath setLineWidth : strokeWidth];
                SetNativePenStrokeStyle(nsPath, (GmpiDrawing_API::IMpStrokeStyle*) strokeStyle);
                
                [nsPath stroke];
            }
            void FillPath(GraphicsContext* context, NSBezierPath* nsPath) const override;
            
            void MP_STDCALL SetExtendModeX(GmpiDrawing_API::MP1_EXTEND_MODE extendModeX) override
            {
//                native()->SetExtendModeX((D2D1_EXTEND_MODE)extendModeX);
            }

            void MP_STDCALL SetExtendModeY(GmpiDrawing_API::MP1_EXTEND_MODE extendModeY) override
            {
 //               native()->SetExtendModeY((D2D1_EXTEND_MODE)extendModeY);
            }

            void MP_STDCALL SetInterpolationMode(GmpiDrawing_API::MP1_BITMAP_INTERPOLATION_MODE interpolationMode) override
            {
  //              native()->SetInterpolationMode((D2D1_BITMAP_INTERPOLATION_MODE)interpolationMode);
            }

            void MP_STDCALL GetFactory(GmpiDrawing_API::IMpFactory **factory) override
            {
                *factory = factory_;
            }
            
            GMPI_REFCOUNT;
            GMPI_QUERYINTERFACE1(GmpiDrawing_API::SE_IID_BITMAPBRUSH_MPGUI, GmpiDrawing_API::IMpBitmapBrush);
        };

		class GeometrySink : public gmpi::generic_graphics::GeometrySink
		{
			NSBezierPath* geometry_;

		public:
			GeometrySink(NSBezierPath* geometry) : geometry_(geometry)
            {}

			void MP_STDCALL SetFillMode(GmpiDrawing_API::MP1_FILL_MODE fillMode) override
			{
				switch (fillMode)
				{
				case GmpiDrawing_API::MP1_FILL_MODE_ALTERNATE:
                case GmpiDrawing_API::MP1_FILL_MODE_FORCE_DWORD:
					[geometry_ setWindingRule : NSEvenOddWindingRule];
					break;

				case GmpiDrawing_API::MP1_FILL_MODE_WINDING:
					[geometry_ setWindingRule : NSNonZeroWindingRule];
					break;
				}
			}
#if 0
			void MP_STDCALL SetSegmentFlags(GmpiDrawing_API::MP1_PATH_SEGMENT vertexFlags)
			{
				//		geometrysink_->SetSegmentFlags((D2D1_PATH_SEGMENT)vertexFlags);
			}
#endif
			void MP_STDCALL BeginFigure(GmpiDrawing_API::MP1_POINT startPoint, GmpiDrawing_API::MP1_FIGURE_BEGIN figureBegin) override
			{
				[geometry_ moveToPoint : NSMakePoint(startPoint.x, startPoint.y)];

				lastPoint = startPoint;
			}

			void MP_STDCALL EndFigure(GmpiDrawing_API::MP1_FIGURE_END figureEnd) override
			{
				if (figureEnd == GmpiDrawing_API::MP1_FIGURE_END_CLOSED)
				{
					[geometry_ closePath];
				}
			}

			void MP_STDCALL AddLine(GmpiDrawing_API::MP1_POINT point) override
			{
				[geometry_ lineToPoint : NSMakePoint(point.x, point.y)];

				lastPoint = point;
			}

			void MP_STDCALL AddBezier(const GmpiDrawing_API::MP1_BEZIER_SEGMENT* bezier) override
			{
				[geometry_ curveToPoint : NSMakePoint(bezier->point3.x, bezier->point3.y)
					controlPoint1 : NSMakePoint(bezier->point1.x, bezier->point1.y)
					controlPoint2 : NSMakePoint(bezier->point2.x, bezier->point2.y)];

				lastPoint = bezier->point3;
			}

			int32_t MP_STDCALL queryInterface(const gmpi::MpGuid& iid, void** returnInterface) override
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


		class PathGeometry final : public GmpiDrawing_API::IMpPathGeometry
		{
			NSBezierPath* geometry_ = {};
            
            GmpiDrawing_API::MP1_DASH_STYLE currentDashStyle = GmpiDrawing_API::MP1_DASH_STYLE_SOLID;
            std::vector<float> currentCustomDashStyle;
            float currentDashPhase = {};
            
		public:
			PathGeometry()
			{
#if !__has_feature(objc_arc)
//                [geometry_ retain];
#endif
            }

			~PathGeometry()
			{
				//	auto release pool handles it?
#if !__has_feature(objc_arc)
//                [geometry_ release];
#endif
                if(geometry_)
                {
                    [geometry_ release];
                }
            }

			inline NSBezierPath* native()
			{
				return geometry_;
			}

			int32_t MP_STDCALL Open(GmpiDrawing_API::IMpGeometrySink** geometrySink) override
			{
                if(geometry_)
                {
                    [geometry_ release];
                }
				geometry_ = [NSBezierPath bezierPath];
                [geometry_ retain];
              
				gmpi_sdk::mp_shared_ptr<gmpi::IMpUnknown> b2;
				b2.Attach(new GeometrySink(geometry_));

				return b2->queryInterface(GmpiDrawing_API::SE_IID_GEOMETRYSINK_MPGUI, reinterpret_cast<void**>(geometrySink));
			}

			void MP_STDCALL GetFactory(GmpiDrawing_API::IMpFactory** factory) override
			{
				//		native_->GetFactory((ID2D1Factory**)factory);
			}
            
			int32_t MP_STDCALL StrokeContainsPoint(GmpiDrawing_API::MP1_POINT point, float strokeWidth, GmpiDrawing_API::IMpStrokeStyle* strokeStyle, const GmpiDrawing_API::MP1_MATRIX_3X2* worldTransform, bool* returnContains) override
			{
                auto cgPath2 = gmpi::cocoa::NsToCGPath(geometry_);
                
                CGPathRef hitTargetPath = CGPathCreateCopyByStrokingPath(cgPath2, NULL, (CGFloat) strokeWidth, (CGLineCap) [geometry_ lineCapStyle], (CGLineJoin)[geometry_ lineJoinStyle], [geometry_ miterLimit] );
                
                CGPathRelease(cgPath2);
                
                CGPoint cgpoint = CGPointMake(point.x, point.y);
                *returnContains = (bool) CGPathContainsPoint(hitTargetPath, NULL, cgpoint, (bool)[geometry_ windingRule]);
                
                CGPathRelease(hitTargetPath);
				return gmpi::MP_OK;
			}
            
			int32_t MP_STDCALL FillContainsPoint(GmpiDrawing_API::MP1_POINT point, const GmpiDrawing_API::MP1_MATRIX_3X2* worldTransform, bool* returnContains) override
			{
                *returnContains = [geometry_ containsPoint:NSMakePoint(point.x, point.y)];
				return gmpi::MP_OK;
			}
            
			int32_t MP_STDCALL GetWidenedBounds(float strokeWidth, GmpiDrawing_API::IMpStrokeStyle* strokeStyle, const GmpiDrawing_API::MP1_MATRIX_3X2* worldTransform, GmpiDrawing_API::MP1_RECT* returnBounds) override
			{
                const float radius = ceilf(strokeWidth * 0.5f);
                auto nativeRect = [geometry_ bounds];
                returnBounds->left = nativeRect.origin.x - radius;
                returnBounds->top = nativeRect.origin.y - radius;
                returnBounds->right = nativeRect.origin.x + nativeRect.size.width + radius;
                returnBounds->bottom = nativeRect.origin.y + nativeRect.size.height + radius;

				return gmpi::MP_OK;
			}
            
            void applyDashStyle(const GmpiDrawing_API::IMpStrokeStyle* strokeStyleIm, float strokeWidth)
            {
                auto strokeStyle = const_cast<GmpiDrawing_API::IMpStrokeStyle*>(strokeStyleIm); // work arround const-correctness issues.
                
                const auto dashStyle = strokeStyle ? strokeStyle->GetDashStyle() : GmpiDrawing_API::MP1_DASH_STYLE_SOLID;
                const auto phase = strokeStyle ? strokeStyle->GetDashOffset() : 0.0f;
                
                bool changed = currentDashStyle != dashStyle;
                currentDashStyle = dashStyle;
                
                if(currentDashStyle == GmpiDrawing_API::MP1_DASH_STYLE_CUSTOM)
                {
                    const auto customDashesCount = strokeStyle->GetDashesCount();
                    std::vector<float> dashesf;
                    dashesf.resize(customDashesCount);
                    strokeStyle->GetDashes(dashesf.data(), static_cast<int>(dashesf.size()));
                    
                    changed |= customDashesCount != currentCustomDashStyle.size();
                    currentCustomDashStyle.resize(customDashesCount);
                    
                    for(int i = 0 ; i < customDashesCount ; ++i)
                    {
                        changed |= currentCustomDashStyle[i] != dashesf[i];
                        currentCustomDashStyle[i] = dashesf[i];
                    }
                }
                                
                if(currentDashStyle != GmpiDrawing_API::MP1_DASH_STYLE_SOLID) // i.e. 'none'
                {
                    changed |= phase != currentDashPhase;
                    currentDashPhase = phase;
                }
                
                if(!changed)
                {
                    return;
                }
                
                applyDashStyleToPath(geometry_, strokeStyle, strokeWidth);
            }
            
			GMPI_QUERYINTERFACE1(GmpiDrawing_API::SE_IID_PATHGEOMETRY_MPGUI, GmpiDrawing_API::IMpPathGeometry);
			GMPI_REFCOUNT;
		};

		class GraphicsContext : public GmpiDrawing_API::IMpDeviceContext
		{
		protected:
            se::cocoa::DrawingFactory* factory{};
            gmpi::cocoa::ContextInfo& info;
            gmpi::IMpUnknown* fallback{};
			std::wstring_convert<std::codecvt_utf8<wchar_t>>* stringConverter; // cached, as constructor is super-slow.
            
		public:
            inline static int logicProFix = -1;
            
			GraphicsContext(
                            NSView* pview
                            , se::cocoa::DrawingFactory* pfactory
                            , gmpi::cocoa::ContextInfo& pinfo
                            , gmpi::IMpUnknown* pfallback
                            ) :
				factory(pfactory)
                , info(pinfo)
                , fallback(pfallback)
			{
                info.view = pview;
                info.currentTransform = [NSAffineTransform transform];
                
#if 0
                // no idea what the real cause is
                if(logicProFix == -1) // -1 = not-set
                {
                    char path[1024];
                    uint32_t size{std::size(path)};
                    _NSGetExecutablePath(path, &size);
                    
                    std::string pathstr{path, size};
                    logicProFix = pathstr.find("arrow.xpc") != std::string::npos;
                }
#endif
			}

			~GraphicsContext()
			{
			}

			void MP_STDCALL GetFactory(GmpiDrawing_API::IMpFactory** pfactory) override
			{
				*pfactory = factory;
			}

			void MP_STDCALL DrawRectangle(const GmpiDrawing_API::MP1_RECT* rect, const GmpiDrawing_API::IMpBrush* brush, float strokeWidth, const GmpiDrawing_API::IMpStrokeStyle* strokeStyle) override
			{
                /*
				auto scb = dynamic_cast<const SolidColorBrush*>(brush);
				if (scb)
				{
					[scb->nativeColor() set];
				}
				NSBezierPath *bp = [NSBezierPath bezierPathWithRect : se::cocoa::NSRectFromRect(*rect)];
				[bp stroke];
                */
                
                NSBezierPath* path = [NSBezierPath bezierPathWithRect : se::cocoa::NSRectFromRect(*rect)];
                applyDashStyleToPath(path, strokeStyle, strokeWidth);
                auto cocoabrush = dynamic_cast<const CocoaBrushBase*>(brush);
                if (cocoabrush)
                {
                    cocoabrush->StrokePath(path, strokeWidth, strokeStyle);
                }
			}

			void MP_STDCALL FillRectangle(const GmpiDrawing_API::MP1_RECT* rect, const GmpiDrawing_API::IMpBrush* brush) override
			{
				NSBezierPath* rectPath = [NSBezierPath bezierPathWithRect : se::cocoa::NSRectFromRect(*rect)];

				auto cocoabrush = dynamic_cast<const CocoaBrushBase*>(brush);
				if (cocoabrush)
				{
					cocoabrush->FillPath(this, rectPath);
				}
			}

			void MP_STDCALL Clear(const GmpiDrawing_API::MP1_COLOR* clearColor) override
			{
                SolidColorBrush brush(clearColor, factory);
                GmpiDrawing::Rect r;
                GetAxisAlignedClip(&r);
                NSBezierPath* rectPath = [NSBezierPath bezierPathWithRect : NSRectFromRect(r)];
                brush.FillPath(this, rectPath);
			}
            
            void MP_STDCALL DrawLine(GmpiDrawing_API::MP1_POINT point0, GmpiDrawing_API::MP1_POINT point1, const GmpiDrawing_API::IMpBrush* brush, float strokeWidth, const GmpiDrawing_API::IMpStrokeStyle* strokeStyle) override
			{
                NSBezierPath* path = [NSBezierPath bezierPath];
                [path moveToPoint : NSMakePoint(point0.x, point0.y)];
                [path lineToPoint : NSMakePoint(point1.x, point1.y)];

                auto cocoabrush = dynamic_cast<const CocoaBrushBase*>(brush);
                if (cocoabrush)
                {
                    applyDashStyleToPath(path, const_cast<GmpiDrawing_API::IMpStrokeStyle*>(strokeStyle), strokeWidth);
                    cocoabrush->StrokePath(path, strokeWidth, strokeStyle);
                }
			}

			void MP_STDCALL DrawGeometry(const GmpiDrawing_API::IMpPathGeometry* geometry, const GmpiDrawing_API::IMpBrush* brush, float strokeWidth = 1.0f, const GmpiDrawing_API::IMpStrokeStyle* strokeStyle = 0) override
			{
                auto pg = (PathGeometry*)geometry;
                pg->applyDashStyle(strokeStyle, strokeWidth);

				auto cocoabrush = dynamic_cast<const CocoaBrushBase*>(brush);
				if (cocoabrush)
				{
					cocoabrush->StrokePath(pg->native(), strokeWidth, strokeStyle);
				}
			}

			void MP_STDCALL FillGeometry(const GmpiDrawing_API::IMpPathGeometry* geometry, const GmpiDrawing_API::IMpBrush* brush, const GmpiDrawing_API::IMpBrush* opacityBrush) override
			{
				auto nsPath = ((PathGeometry*)geometry)->native();

				auto cocoabrush = dynamic_cast<const CocoaBrushBase*>(brush);
				if (cocoabrush)
				{
					cocoabrush->FillPath(this, nsPath);
				}
			}
            
            inline float truncatePixel(float y, float stepSize)
            {
                return floor(y / stepSize) * stepSize;
            }
            
            inline float roundPixel(float y, float stepSize)
            {
                return floor(y / stepSize + 0.5f) * stepSize;
            }
            
			void MP_STDCALL DrawTextU(const char* utf8String, int32_t stringLength, const GmpiDrawing_API::IMpTextFormat* textFormat, const GmpiDrawing_API::MP1_RECT* layoutRect, const GmpiDrawing_API::IMpBrush* brush, int32_t flags) override
			{
				auto textformat = reinterpret_cast<const TextFormat*>(textFormat);

				auto scb = dynamic_cast<const SolidColorBrush*>(brush);

				CGRect bounds = CGRectMake(layoutRect->left, layoutRect->top, layoutRect->right - layoutRect->left, layoutRect->bottom - layoutRect->top);

                GmpiDrawing::Size textSize {};
                if (textformat->paragraphAlignment != (int)GmpiDrawing::TextAlignment::Leading
                    || flags != GmpiDrawing_API::MP1_DRAW_TEXT_OPTIONS_CLIP)
                {
                    const_cast<GmpiDrawing_API::IMpTextFormat*>(textFormat)->GetTextExtentU(utf8String, (int32_t)strlen(utf8String), &textSize);
                }
                
				if (textformat->paragraphAlignment != (int)GmpiDrawing::TextAlignment::Leading)
				{
					// Vertical text alignment.
					switch (textformat->paragraphAlignment)
					{
					case (int)GmpiDrawing::TextAlignment::Trailing:    // Bottom
						bounds.origin.y += bounds.size.height - textSize.height;
						bounds.size.height = textSize.height;
						break;

					case (int)GmpiDrawing::TextAlignment::Center:
						bounds.origin.y += (bounds.size.height - textSize.height) / 2;
						bounds.size.height = textSize.height;
						break;

					default:
						break;
					}
				}

				NSString* str = [NSString stringWithCString : utf8String encoding : NSUTF8StringEncoding];
                
                [textformat->native2 setObject:scb->nativeColor() forKey:NSForegroundColorAttributeName];

                const bool clipToRect = flags & GmpiDrawing_API::MP1_DRAW_TEXT_OPTIONS_CLIP;

                if (textformat->wordWrapping == GmpiDrawing_API::MP1_WORD_WRAPPING_NO_WRAP)
                {
                    // Mac will always clip to rect, so we turn 'off' clipping by extending rect to the right.
                    if(!clipToRect)
                    {
                        const auto extendWidth = static_cast<double>(textSize.width) - bounds.size.width;
                        if(extendWidth > 0.0)
                        {
                            bounds.size.width += extendWidth;
                            
                            // fake no-clip by extending text box.
                            switch(textformat->textAlignment)
                            {
                                case GmpiDrawing_API::MP1_TEXT_ALIGNMENT_CENTER:
                                    bounds.origin.x -= 0.5 * extendWidth;
                                    break;
                                case GmpiDrawing_API::MP1_TEXT_ALIGNMENT_TRAILING: // Right
                                    bounds.origin.x -= extendWidth;
                                case GmpiDrawing_API::MP1_TEXT_ALIGNMENT_LEADING: // Left
                                default:
                                    break;
                            }
                        }
                    }
                    
                    [textformat->native2[NSParagraphStyleAttributeName] setLineBreakMode:NSLineBreakByClipping];
                }
                else
                {
                    // 'wrap'.
                    [textformat->native2[NSParagraphStyleAttributeName] setLineBreakMode:NSLineBreakByWordWrapping];
                }
                
                // maximumLineHeight minimumLineHeight
// no diff                [textformat->native2[NSParagraphStyleAttributeName] setLineHeightMultiple:1.0];

                // macOS draws extra padding at top of text bounds. Compensate for it.
				if (!textformat->getUseLegacyBaseLineSnapping())
				{
					bounds.origin.y -= textformat->topAdjustment;
					bounds.size.height += textformat->topAdjustment;

					// Adjust text baseline vertically to match Windows.
#if 1
                    const float scale = 0.5f; // Hi DPI x2

                    float winBaseline {};
                    {
                        // Windows baseline
                        /*
                        const float offsetHinted = -0.25f;
                        const float offsetVertAA = -0.125f;
                        const float useHintingThreshold = 10.0f;
                        const float offset =
                            fontMetrics.bodyHeight() < useHintingThreshold ? offsetHinted : offsetVertAA;
                        */
                        const float offset = -0.25f;;
                        winBaseline = layoutRect->top + textformat->ascent;
                        winBaseline = floorf((offset + winBaseline) / scale) * scale;
                    }
 #if 0
                    float macBaseline {};
                    {
                        // Mac baseline
                        // Correct, but given the assumption we intend to snap baseline o pixel grid - we
                        // can use the simpler version below.
                        float predictedBt = layoutRect->top + fontMetrics.ascent + fontMetrics.descent;
                        predictedBt = truncatePixel(predictedBt, scale);
                        macBaseline = predictedBt - fontMetrics.descent;
                        macBaseline = truncatePixel(macBaseline, scale);
                    
                        if((fontMetrics.descent - floor(fontMetrics.descent)) <= 0.5f)
                        {
                            macBaseline -= 0.5f;
                        }
                    }
                    
                    float macBaselineCorrection = roundPixel(winBaseline - macBaseline, scale);
#else
                    const float baseline = layoutRect->top + textformat->ascent;
                    float macBaselineCorrection{};
                    
                    // adjust text vertical alignment for Logix Pro issue
                    if(logicProFix)
                    {
                        macBaselineCorrection = winBaseline - baseline - textformat->baselineCorrection + 1;
                    }
                    else
                    {
                        macBaselineCorrection = winBaseline - baseline + textformat->baselineCorrection;
                    }
                    
 //                   _RPT3(0, "baseline %f, winBL %f, BLCorctn %f\n", baseline, winBaseline, textformat->baselineCorrection);
#endif
                    
                    bounds.origin.y += macBaselineCorrection;
#endif
#if 0
                    if(false) // Visualize Mac Baseline prediction.
                    {
                        GmpiDrawing::Graphics g(this);
                        static auto brush = g.CreateSolidColorBrush(GmpiDrawing::Color::Lime);
                         g.DrawLine(GmpiDrawing::Point(layoutRect->left,macBaseline + 0.25), GmpiDrawing::Point(layoutRect->left + 2,macBaseline + 0.25),brush, 0.25f);
                    }
#endif
                }
                
                #if 0
                    if(true) // Visualize adjusted bounds.
                    {
                        GmpiDrawing::Graphics g(this);
                        static auto brush = g.CreateSolidColorBrush(GmpiDrawing::Color::Lime);
                        g.DrawRectangle(GmpiDrawing::Rect(bounds.origin.x, bounds.origin.y, bounds.origin.x+bounds.size.width, bounds.origin.y+bounds.size.height), brush) ;
                    }
                #endif

                // do last so don't affect font metrics.
  //              [textformat->native2[NSParagraphStyleAttributeName] setLineHeightMultiple:testLineHeightMultiplier];
 //               textformat->native2[NSBaselineOffsetAttributeName] = [NSNumber numberWithFloat:shiftUp];
#if USE_BACKING_BUFFER
                // Create a flipped coordinate system
                [[NSGraphicsContext currentContext] saveGraphicsState];
                NSAffineTransform *transform = [NSAffineTransform transform];
                [transform scaleXBy:1 yBy:-1];
                [transform translateXBy:0 yBy:-2 * bounds.origin.y - bounds.size.height];

                [transform concat];

                // Draw in the flipped coordinate system
                [str drawInRect : bounds withAttributes : textformat->native2];

                // Restore the original graphics state
                [[NSGraphicsContext currentContext] restoreGraphicsState];
#else
                [str drawInRect : bounds withAttributes : textformat->native2];
#endif
                
 //               [textformat->native2[NSParagraphStyleAttributeName] setLineHeightMultiple:1.0f];

#if 0
                {
                    GmpiDrawing::Graphics g(this);
                    GmpiDrawing_API::MP1_FONT_METRICS fontMetrics;
                    ((GmpiDrawing_API::IMpTextFormat*) textFormat)->GetFontMetrics(&fontMetrics);
                    float pixelScale=2.0; // DPI ish
                
                    if(true)
                    {
                        // nailed it.
                        float predictedBt = layoutRect->top + fontMetrics.ascent + fontMetrics.descent;
                        predictedBt = floor(predictedBt * pixelScale) / pixelScale;
                        float predictedBaseline = predictedBt - fontMetrics.descent;
                        predictedBaseline = floor(predictedBaseline * pixelScale) / pixelScale;
                        
                        if((fontMetrics.descent - floor(fontMetrics.descent)) < 0.5f)
                        {
                            predictedBaseline -= 0.5f;
                        }

                        static auto brush = g.CreateSolidColorBrush(GmpiDrawing::Color::Red);
                        g.DrawLine(GmpiDrawing::Point(layoutRect->left,predictedBaseline + 0.25), GmpiDrawing::Point(layoutRect->left + 2,predictedBaseline+0.25),brush, 0.25f);
                    }
                    
                    // Absolute vertical position does not matter, because entire row has same error,
                    // despite different vertical offsets. Therefore error comes from font metrics.
                    if(false)
                    {
                        float topGap = textformat->topAdjustment;
                        float renderTop = layoutRect->top - topGap;
                        
                        renderTop = truncatePixel(renderTop, 0.5f);
         
                        float lineHeight = fontMetrics.ascent + fontMetrics.descent;
                        
                        lineHeight = truncatePixel(lineHeight, 0.5f);
                        
                        float predictedBt = renderTop + topGap + lineHeight;
                        
                        //predictedBt = floor(predictedBt * pixelScale) / pixelScale;
                        float predictedBaseline = truncatePixel(predictedBt - fontMetrics.descent, 0.5f);
                    
                        predictedBaseline = truncatePixel(predictedBaseline, 0.5f);
                        
                        static auto brush = g.CreateSolidColorBrush(GmpiDrawing::Color::Red);
                        g.DrawLine(GmpiDrawing::Point(layoutRect->left +2,predictedBaseline + 0.25), GmpiDrawing::Point(layoutRect->left + 4,predictedBaseline+0.25),brush, 0.25f);
                    }
                    
                    {
                        static auto brush1 = g.CreateSolidColorBrush(GmpiDrawing::Color::Red);
                        static auto brush2 = g.CreateSolidColorBrush(GmpiDrawing::Color::Lime);
                        static auto brush3 = g.CreateSolidColorBrush(GmpiDrawing::Color::Blue);

                        float v = fontMetrics.descent;
                        float x = layoutRect->left + (v - floor(v)) * 10.0f;
                        float y = layoutRect->top;
                        g.DrawLine(GmpiDrawing::Point(x,y), GmpiDrawing::Point(x,y+5),brush1, 0.25f);
                        
                         v = fontMetrics.descent + fontMetrics.ascent;
                         x = layoutRect->left + (v - floor(v)) * 10.0f;
                         y = layoutRect->top;

                        g.DrawLine(GmpiDrawing::Point(x,y), GmpiDrawing::Point(x,y+5),brush2, 0.25f);

                        v = fontMetrics.capHeight;
                         x = layoutRect->left + (v - floor(v)) * 10.0f;
                         y = layoutRect->top;

                        g.DrawLine(GmpiDrawing::Point(x,y), GmpiDrawing::Point(x,y+5),brush3, 0.25f);

                    }
                    
/*
                    // clip baseline, then bottom. Not close
                    float predictedBaseline = layoutRect->top + fontMetrics.ascent;
                    predictedBaseline = floor(predictedBaseline * pixelScale) / pixelScale;
                    
                    float predictedBt = predictedBaseline + fontMetrics.descent;
                    predictedBt = floor(predictedBt * pixelScale) / pixelScale;
                    predictedBaseline = predictedBt - fontMetrics.descent;
*/
                    
                 }
#endif
			}

			void MP_STDCALL DrawBitmap(const GmpiDrawing_API::IMpBitmap* mpBitmap, const GmpiDrawing_API::MP1_RECT* destinationRectangle, float opacity, /* MP1_BITMAP_INTERPOLATION_MODE*/ int32_t interpolationMode, const GmpiDrawing_API::MP1_RECT* sourceRectangle) override
			{
				auto bm = ((Bitmap*)mpBitmap);
				auto bitmap = bm->GetNativeBitmap();
    
                GmpiDrawing_API::MP1_SIZE_U imageSize;
                bm->GetSize(&imageSize);
                
                auto destRect = se::cocoa::NSRectFromRect(*destinationRectangle);
                
#if USE_BACKING_BUFFER
                auto sourceRect = se::cocoa::NSRectFromRect(*sourceRectangle);
                
                // mirror source rectangle
                sourceRect.origin.y = imageSize.height - (sourceRect.origin.y + sourceRect.size.height);
                
                // Create a flipped coordinate system
                [[NSGraphicsContext currentContext] saveGraphicsState];
                NSAffineTransform *transform = [NSAffineTransform transform];
                [transform scaleXBy:1 yBy:-1];
                [transform translateXBy:0 yBy:-2 * destRect.origin.y - destRect.size.height];

                [transform concat];

                // Draw in the flipped coordinate system
#else
                GmpiDrawing::Rect sourceRectangleFlipped(*sourceRectangle);
                sourceRectangleFlipped.bottom = imageSize.height - sourceRectangle->top;
                sourceRectangleFlipped.top = imageSize.height - sourceRectangle->bottom;
                
                auto sourceRect = se::cocoa::NSRectFromRect(sourceRectangleFlipped);

#endif
				if (bitmap)
				{
                    [bitmap drawInRect : destRect fromRect : sourceRect operation : NSCompositingOperationSourceOver fraction : opacity respectFlipped : TRUE hints : nil];
                }
                if(bm->additiveBitmap_)
                {
                #if 1
                    [bm->additiveBitmap_ drawInRect : destRect fromRect : sourceRect operation : NSCompositingOperationPlusLighter fraction : opacity respectFlipped : TRUE hints : nil];

                #else // imagerep (don't work due to flip
                    auto rect = se::cocoa::NSRectFromRect(*destinationRectangle);
                    
                    // Create a flipped coordinate system (imagerep don't understand flipped)
                    [[NSGraphicsContext currentContext] saveGraphicsState];
                    NSAffineTransform *transform = [NSAffineTransform transform];
                    [transform translateXBy:0 yBy:rect.size.height];
                    [transform scaleXBy:1 yBy:-1];
                    [transform concat];

                    // Draw the image in the flipped coordinate system
                    [bm->additiveBitmap_ drawInRect : rect fromRect : sourceRect operation : NSCompositingOperationPlusLighter fraction : opacity respectFlipped : TRUE hints : nil];
                    
                    // Restore the original graphics state
                    [[NSGraphicsContext currentContext] restoreGraphicsState];
                    #endif
                }
#if USE_BACKING_BUFFER
                // Restore the original graphics state
                [[NSGraphicsContext currentContext] restoreGraphicsState];
#endif
			}

			void MP_STDCALL SetTransform(const GmpiDrawing_API::MP1_MATRIX_3X2* transform) override
			{
                // Remove the current transformations by applying the inverse transform.
                try
                {
                    [info.currentTransform invert];
                    [info.currentTransform concat];
                }
                catch(...)
                {
                    // some transforms are not reversible. e.g. scaling everything down to a point.
                    // int test = 9;
                };
                
                NSAffineTransformStruct transformStruct {
                    transform->_11,
                    transform->_12,
                    transform->_21,
                    transform->_22,
                    transform->_31,
                    transform->_32
                };

				[info.currentTransform setTransformStruct : transformStruct];

				[info.currentTransform concat];
			}

			void MP_STDCALL GetTransform(GmpiDrawing_API::MP1_MATRIX_3X2* transform) override
			{
				NSAffineTransformStruct
					transformStruct = [info.currentTransform transformStruct];

				transform->_11 = transformStruct.m11;
				transform->_12 = transformStruct.m12;
				transform->_21 = transformStruct.m21;
				transform->_22 = transformStruct.m22;
				transform->_31 = transformStruct.tX;
				transform->_32 = transformStruct.tY;
			}

			int32_t MP_STDCALL CreateSolidColorBrush(const GmpiDrawing_API::MP1_COLOR* color, GmpiDrawing_API::IMpSolidColorBrush **solidColorBrush) override
			{
				gmpi_sdk::mp_shared_ptr<gmpi::IMpUnknown> b2;
				b2.Attach(new SolidColorBrush(color, factory));

				return b2->queryInterface(GmpiDrawing_API::SE_IID_SOLIDCOLORBRUSH_MPGUI, reinterpret_cast<void **>(solidColorBrush));
			}

			int32_t MP_STDCALL CreateGradientStopCollection(const GmpiDrawing_API::MP1_GRADIENT_STOP* gradientStops, uint32_t gradientStopsCount, /* GmpiDrawing_API::MP1_GAMMA colorInterpolationGamma, GmpiDrawing_API::MP1_EXTEND_MODE extendMode,*/ GmpiDrawing_API::IMpGradientStopCollection** gradientStopCollection) override
			{
				gmpi_sdk::mp_shared_ptr<gmpi::IMpUnknown> b2;
				b2.Attach(new GradientStopCollection(factory, gradientStops, gradientStopsCount));

				return b2->queryInterface(GmpiDrawing_API::SE_IID_GRADIENTSTOPCOLLECTION_MPGUI, reinterpret_cast<void **>(gradientStopCollection));
			}

			int32_t MP_STDCALL CreateLinearGradientBrush(const GmpiDrawing_API::MP1_LINEAR_GRADIENT_BRUSH_PROPERTIES* linearGradientBrushProperties, const GmpiDrawing_API::MP1_BRUSH_PROPERTIES* brushProperties, const  GmpiDrawing_API::IMpGradientStopCollection* gradientStopCollection, GmpiDrawing_API::IMpLinearGradientBrush** linearGradientBrush) override
			{
				gmpi_sdk::mp_shared_ptr<gmpi::IMpUnknown> b2;
				b2.Attach(new LinearGradientBrush(factory, linearGradientBrushProperties, brushProperties, gradientStopCollection));

				return b2->queryInterface(GmpiDrawing_API::SE_IID_LINEARGRADIENTBRUSH_MPGUI, reinterpret_cast<void **>(linearGradientBrush));
			}

			int32_t MP_STDCALL CreateBitmapBrush(const GmpiDrawing_API::IMpBitmap* bitmap, const GmpiDrawing_API::MP1_BITMAP_BRUSH_PROPERTIES* bitmapBrushProperties, const GmpiDrawing_API::MP1_BRUSH_PROPERTIES* brushProperties, GmpiDrawing_API::IMpBitmapBrush** returnBrush) override
			{
                *returnBrush = nullptr;
                gmpi_sdk::mp_shared_ptr<gmpi::IMpUnknown> b2;
                b2.Attach(new BitmapBrush(factory, bitmap, bitmapBrushProperties, brushProperties));
                return b2->queryInterface(GmpiDrawing_API::SE_IID_BITMAPBRUSH_MPGUI, reinterpret_cast<void **>(returnBrush));
			}

			int32_t MP_STDCALL CreateRadialGradientBrush(const GmpiDrawing_API::MP1_RADIAL_GRADIENT_BRUSH_PROPERTIES* radialGradientBrushProperties, const GmpiDrawing_API::MP1_BRUSH_PROPERTIES* brushProperties, const GmpiDrawing_API::IMpGradientStopCollection* gradientStopCollection, GmpiDrawing_API::IMpRadialGradientBrush** radialGradientBrush) override
			{
				gmpi_sdk::mp_shared_ptr<gmpi::IMpUnknown> b2;
				b2.Attach(new RadialGradientBrush(factory, radialGradientBrushProperties, brushProperties, gradientStopCollection ));

				return b2->queryInterface(GmpiDrawing_API::SE_IID_RADIALGRADIENTBRUSH_MPGUI, reinterpret_cast<void **>(radialGradientBrush));
			}

			int32_t MP_STDCALL CreateCompatibleRenderTarget(const GmpiDrawing_API::MP1_SIZE* desiredSize, GmpiDrawing_API::IMpBitmapRenderTarget** bitmapRenderTarget) override;

			void MP_STDCALL DrawRoundedRectangle(const GmpiDrawing_API::MP1_ROUNDED_RECT* roundedRect, const GmpiDrawing_API::IMpBrush* brush, float strokeWidth, const GmpiDrawing_API::IMpStrokeStyle* strokeStyle) override
			{
				NSRect r = se::cocoa::NSRectFromRect(roundedRect->rect);
				NSBezierPath* path = [NSBezierPath bezierPathWithRoundedRect : r xRadius : roundedRect->radiusX yRadius : roundedRect->radiusY];
                applyDashStyleToPath(path, strokeStyle, strokeWidth);
                
                auto cocoabrush = dynamic_cast<const CocoaBrushBase*>(brush);
                if (cocoabrush)
                {
                    cocoabrush->StrokePath(path, strokeWidth, strokeStyle);
                }
            }

			void MP_STDCALL FillRoundedRectangle(const GmpiDrawing_API::MP1_ROUNDED_RECT* roundedRect, const GmpiDrawing_API::IMpBrush* brush) override
			{
				NSRect r = se::cocoa::NSRectFromRect(roundedRect->rect);
				NSBezierPath* rectPath = [NSBezierPath bezierPathWithRoundedRect : r xRadius:roundedRect->radiusX yRadius: roundedRect->radiusY];

				auto cocoabrush = dynamic_cast<const CocoaBrushBase*>(brush);
				if (cocoabrush)
				{
					cocoabrush->FillPath(this, rectPath);
				}
			}

			void MP_STDCALL DrawEllipse(const GmpiDrawing_API::MP1_ELLIPSE* ellipse, const GmpiDrawing_API::IMpBrush* brush, float strokeWidth, const GmpiDrawing_API::IMpStrokeStyle* strokeStyle) override
			{
				NSRect r = NSMakeRect(ellipse->point.x - ellipse->radiusX, ellipse->point.y - ellipse->radiusY, ellipse->radiusX * 2.0f, ellipse->radiusY * 2.0f);

				NSBezierPath* path = [NSBezierPath bezierPathWithOvalInRect : r];
                applyDashStyleToPath(path, strokeStyle, strokeWidth);

				auto cocoabrush = dynamic_cast<const CocoaBrushBase*>(brush);
				if (cocoabrush)
				{
                    cocoabrush->StrokePath(path, strokeWidth, strokeStyle);
				}
			}

			void MP_STDCALL FillEllipse(const GmpiDrawing_API::MP1_ELLIPSE* ellipse, const GmpiDrawing_API::IMpBrush* brush) override
			{
                NSRect r = NSMakeRect(ellipse->point.x - ellipse->radiusX, ellipse->point.y - ellipse->radiusY, ellipse->radiusX * 2.0f, ellipse->radiusY * 2.0f);

				NSBezierPath* rectPath = [NSBezierPath bezierPathWithOvalInRect : r];

				auto cocoabrush = dynamic_cast<const CocoaBrushBase*>(brush);
				if (cocoabrush)
				{
					cocoabrush->FillPath(this, rectPath);
				}
			}

			void MP_STDCALL PushAxisAlignedClip(const GmpiDrawing_API::MP1_RECT* clipRect/*, GmpiDrawing_API::MP1_ANTIALIAS_MODE antialiasMode*/) override
			{
                GmpiDrawing::Matrix3x2 currentTransform;
                GetTransform(&currentTransform);
				auto absClipRect = currentTransform.TransformRect(*clipRect);
                
                if(!info.clipRectStack.empty())
                    absClipRect = GmpiDrawing::Intersect(absClipRect, *(GmpiDrawing::Rect*) &info.clipRectStack.back());
                    
				info.clipRectStack.push_back(*(gmpi::drawing::Rect*) &absClipRect);

				// Save the current clipping region
				[NSGraphicsContext saveGraphicsState];

				NSRectClip(NSRectFromRect(*clipRect));
			}

			void MP_STDCALL PopAxisAlignedClip() override
			{
				info.clipRectStack.pop_back();

				// Restore the clipping region for further drawing
				[NSGraphicsContext restoreGraphicsState];
			}

			void MP_STDCALL GetAxisAlignedClip(GmpiDrawing_API::MP1_RECT* returnClipRect) override
			{
                GmpiDrawing::Matrix3x2 currentTransform;
                GetTransform(&currentTransform);
                currentTransform.Invert();
				*returnClipRect = currentTransform.TransformRect(*(GmpiDrawing_API::MP1_RECT*) &info.clipRectStack.back());
			}

			void MP_STDCALL BeginDraw() override
			{
			}

			int32_t MP_STDCALL EndDraw() override
			{
				return gmpi::MP_OK;
			}
   
            NSView* getNativeView()
            {
                return info.view;
            }
/*
            int getQuartzYorigin()
            {
                const auto frameSize = [view_ frame];
                return frameSize.size.height;
            }

            int32_t MP_STDCALL CreateMesh(GmpiDrawing_API::IMpMesh** returnObject) override
            {
                *returnObject = nullptr;
                return gmpi::MP_FAIL;
            }
            
            void MP_STDCALL FillMesh(const GmpiDrawing_API::IMpMesh* mesh, const GmpiDrawing_API::IMpBrush* brush) override
            {
                
            }
*/
			//	void MP_STDCALL InsetNewMethodHere(){}

			// GMPI_QUERYINTERFACE1(GmpiDrawing_API::SE_IID_DEVICECONTEXT_MPGUI, GmpiDrawing_API::IMpDeviceContext);
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

        class GraphicsContext2 : public GraphicsContext, public GmpiDrawing_API::IMpDeviceContextExt
        {
        public:
            GraphicsContext2(
                             gmpi::IMpUnknown* pfallback
                             , NSView* pview
                             , gmpi::cocoa::ContextInfo& info
                             , se::cocoa::DrawingFactory* pfactory
                             ) : GraphicsContext(pview, pfactory, info, pfallback){}

            int32_t MP_STDCALL CreateBitmapRenderTarget(GmpiDrawing_API::MP1_SIZE_L desiredSize, bool enableLockPixels, GmpiDrawing_API::IMpBitmapRenderTarget** returnObject) override;

            int32_t MP_STDCALL queryInterface(const gmpi::MpGuid& iid, void** returnInterface) override
            {
                *returnInterface = 0;
                if (iid == GmpiDrawing_API::IMpDeviceContextExt::guid)
                {
                    *returnInterface = static_cast<GmpiDrawing_API::IMpDeviceContextExt*>(this);
                    addRef();
                    return gmpi::MP_OK;
                }
                return GraphicsContext::queryInterface(iid, returnInterface);
            }

            GMPI_REFCOUNT_NO_DELETE;
        };

        struct UniversalGraphicsContext : public gmpi::cocoa::GraphicsContext
        {
            GraphicsContext2 sdk3Context;

            UniversalGraphicsContext(NSView* pview, gmpi::cocoa::Factory* gmpiFactory, se::cocoa::DrawingFactory* sdk3Factory) :
                gmpi::cocoa::GraphicsContext(pview, gmpiFactory),
                sdk3Context((gmpi::IMpUnknown*) static_cast<gmpi::api::IUnknown*>(this), pview, info, sdk3Factory)
            {
            }

            gmpi::ReturnCode queryInterface(const gmpi::api::Guid* iid, void** returnInterface) override
            {
                *returnInterface = {};
                if (*iid == gmpi::drawing::api::IDeviceContext::guid || *iid == gmpi::drawing::api::IResource::guid || *iid == gmpi::api::IUnknown::guid)
                {
                    return gmpi::cocoa::GraphicsContext::queryInterface(iid, returnInterface);
                }
                if (*iid == *reinterpret_cast<const gmpi::api::Guid*>(&GmpiDrawing_API::SE_IID_DEVICECONTEXT_MPGUI))
                {
                    return (gmpi::ReturnCode)sdk3Context.queryInterface(*reinterpret_cast<const gmpi::MpGuid*>(iid), returnInterface);
                }
                return gmpi::ReturnCode::NoSupport;
            }
            GMPI_REFCOUNT_NO_DELETE;
        };

		// https://stackoverflow.com/questions/10627557/mac-os-x-drawing-into-an-offscreen-nsgraphicscontext-using-cgcontextref-c-funct
		class bitmapRenderTarget : public GraphicsContext // emulated by carefull layout public GmpiDrawing_API::IMpBitmapRenderTarget
		{
			NSImage* image = {};
            gmpi::cocoa::ContextInfo info;

		public:
			bitmapRenderTarget(se::cocoa::DrawingFactory* pfactory, const GmpiDrawing_API::MP1_SIZE* desiredSize) :
				GraphicsContext(nullptr, pfactory, info, nullptr)
			{
				NSRect r = NSMakeRect(0.0, 0.0, desiredSize->width, desiredSize->height);
                image = [[NSImage alloc] initWithSize:r.size];

                info.clipRectStack.push_back({ 0, 0, desiredSize->width, desiredSize->height });
			}

            void MP_STDCALL BeginDraw() override
            {
#if USE_BACKING_BUFFER
                [image lockFocus];

                // draw upside down
                NSAffineTransform* transform = [NSAffineTransform transform];
                [transform scaleXBy:1 yBy:-1];
                [transform translateXBy:0 yBy:-[image size].height];
                [transform concat];
#else
                // To match Flipped View, Flip Bitmap Context too.
                // (Alternative is [image setFlipped:TRUE] in constructor, but that method is deprected).
                [image lockFocus Flipped:TRUE];
#endif
                GraphicsContext::BeginDraw();
            }
            
            int32_t MP_STDCALL EndDraw() override
            {
                auto r = GraphicsContext::EndDraw();
                [image unlockFocus];
                return r;
            }
            
#if !__has_feature(objc_arc)
            ~bitmapRenderTarget()
			{
				[image release];
			}
#endif
            // MUST BE FIRST VIRTUAL FUNCTION!
			virtual int32_t MP_STDCALL GetBitmap(GmpiDrawing_API::IMpBitmap** returnBitmap)
			{
				gmpi_sdk::mp_shared_ptr<gmpi::IMpUnknown> b;
				b.Attach(new Bitmap(factory, image));
				return b->queryInterface(GmpiDrawing_API::SE_IID_BITMAP_MPGUI, reinterpret_cast<void**>(returnBitmap));
			}

			int32_t MP_STDCALL queryInterface(const gmpi::MpGuid& iid, void** returnInterface) override
			{
				*returnInterface = 0;
				if (iid == GmpiDrawing_API::SE_IID_BITMAP_RENDERTARGET_MPGUI)
				{
					// non-standard. Forcing this class (which has the correct vtable) to pretend it's the emulated interface.
					*returnInterface = reinterpret_cast<GmpiDrawing_API::IMpBitmapRenderTarget*>(this);
					addRef();
					return gmpi::MP_OK;
				}
				else
				{
					return GraphicsContext::queryInterface(iid, returnInterface);
				}
			}

			GMPI_REFCOUNT;
		};

		inline int32_t MP_STDCALL GraphicsContext::CreateCompatibleRenderTarget(const GmpiDrawing_API::MP1_SIZE* desiredSize, GmpiDrawing_API::IMpBitmapRenderTarget** bitmapRenderTarget)
		{
			gmpi_sdk::mp_shared_ptr<gmpi::IMpUnknown> b2;
			b2.Attach(new class bitmapRenderTarget(factory, desiredSize));

			return b2->queryInterface(GmpiDrawing_API::SE_IID_BITMAP_RENDERTARGET_MPGUI, reinterpret_cast<void **>(bitmapRenderTarget));
		}
    inline int32_t GraphicsContext2::CreateBitmapRenderTarget(GmpiDrawing_API::MP1_SIZE_L desiredSize, bool enableLockPixels, GmpiDrawing_API::IMpBitmapRenderTarget** returnObject)
        {
            GmpiDrawing_API::MP1_SIZE sizef{ static_cast<float>(desiredSize.width), static_cast<float>(desiredSize.height) };
            return CreateCompatibleRenderTarget(&sizef, returnObject);
        }
    
        inline int32_t MP_STDCALL DrawingFactory::CreateTextFormat(const char* fontFamilyName, void* unused /* fontCollection */, GmpiDrawing_API::MP1_FONT_WEIGHT fontWeight, GmpiDrawing_API::MP1_FONT_STYLE fontStyle, GmpiDrawing_API::MP1_FONT_STRETCH fontStretch, float fontSize, void* unused2 /* localeName */, GmpiDrawing_API::IMpTextFormat** textFormat)
        {
            gmpi_sdk::mp_shared_ptr<gmpi::IMpUnknown> b2;
            b2.Attach(new TextFormat(&info.stringConverter, fontFamilyName, fontWeight, fontStyle, fontStretch, fontSize));

            return b2->queryInterface(GmpiDrawing_API::SE_IID_TEXTFORMAT_MPGUI, reinterpret_cast<void **>(textFormat));
        }
    
        inline int32_t MP_STDCALL DrawingFactory::CreatePathGeometry(GmpiDrawing_API::IMpPathGeometry** pathGeometry)
        {
            gmpi_sdk::mp_shared_ptr<gmpi::IMpUnknown> b2;
            b2.Attach(new PathGeometry());

            return b2->queryInterface(GmpiDrawing_API::SE_IID_PATHGEOMETRY_MPGUI, reinterpret_cast<void **>(pathGeometry));
        }
    
        inline int32_t MP_STDCALL DrawingFactory::CreateImage(int32_t width, int32_t height, GmpiDrawing_API::IMpBitmap** returnDiBitmap)
        {
            *returnDiBitmap = nullptr;
            
			gmpi_sdk::mp_shared_ptr<gmpi::IMpUnknown> bm;
			bm.Attach(new Bitmap(this, width, height));

            return bm->queryInterface(GmpiDrawing_API::SE_IID_BITMAP_MPGUI, (void**)returnDiBitmap);
        }

        inline int32_t MP_STDCALL DrawingFactory::LoadImageU(const char* utf8Uri, GmpiDrawing_API::IMpBitmap** returnDiBitmap)
        {
            *returnDiBitmap = nullptr;

  			gmpi_sdk::mp_shared_ptr<gmpi::IMpUnknown> bm;
            auto temp = new Bitmap(this, utf8Uri);
			bm.Attach(temp);
            
            if (temp->isLoaded())
            {
                return bm->queryInterface(GmpiDrawing_API::SE_IID_BITMAP_MPGUI, (void**)returnDiBitmap);
            }

            return gmpi::MP_FAIL;
        }

        inline void BitmapBrush::FillPath(GraphicsContext* context, NSBezierPath* nsPath) const
        {
            [NSGraphicsContext saveGraphicsState];

            auto view = context->getNativeView();
            
#if USE_BACKING_BUFFER
            // Adjust offset to be relative to the top (Windows) not bottom (mac)
            CGFloat yOffset = view.bounds.size.height - const_cast<Bitmap&>(bitmap_).GetSizeF().height;
#else
            // convert to Core Grapics co-ords
            CGFloat yOffset = NSMaxY([view convertRect:view.bounds toView:nil]);
#endif
            GmpiDrawing::Matrix3x2 moduleTransform;
            context->GetTransform(&moduleTransform);
            auto offset = GmpiDrawing::TransformPoint(moduleTransform, {0.0f, 0.0f});
            // apply brushes transfer. we support only translation on mac
            offset = GmpiDrawing::TransformPoint(brushProperties_.transform, offset);
            
            // also need to apply current drawing transform for modules not at [0,0]
            
            [[NSGraphicsContext currentContext] setPatternPhase:NSMakePoint(offset.x, yOffset - offset.y)];
            [[NSColor colorWithPatternImage:bitmap_.nativeBitmap_] set];
            [nsPath fill];
            
            [NSGraphicsContext restoreGraphicsState];
        }

        inline bitmapPixels::bitmapPixels(Bitmap* sebitmap /*NSImage** inBitmap*/, bool _alphaPremultiplied, int32_t pflags) :
                inBitmap_(&sebitmap->nativeBitmap_ /*inBitmap*/)
                ,flags(pflags)
                ,seBitmap(sebitmap)
        {
            NSSize s = [*inBitmap_ size];
            bytesPerRow = s.width * 4;
            
            constexpr int bitsPerSample = 8;
            constexpr int samplesPerPixel = 4;
            constexpr int bitsPerPixel = bitsPerSample * samplesPerPixel;

            auto initial_bitmap = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:nil
                pixelsWide : s.width
                pixelsHigh : s.height
                bitsPerSample : bitsPerSample
                samplesPerPixel : samplesPerPixel
                hasAlpha : YES
                isPlanar : NO
                colorSpaceName: NSCalibratedRGBColorSpace
                bitmapFormat : 0
                bytesPerRow : bytesPerRow
                bitsPerPixel : bitsPerPixel];
                
            bitmap2 = [initial_bitmap bitmapImageRepByRetaggingWithColorSpace:NSColorSpace.sRGBColorSpace];
            [bitmap2 retain];

            // Copy the image to the new imageRep (effectivly converts it to correct pixel format/brightness etc)
            if (0 != (flags & GmpiDrawing_API::MP1_BITMAP_LOCK_READ))
            {
                NSGraphicsContext * context;
                context = [NSGraphicsContext graphicsContextWithBitmapImageRep : bitmap2];
                [NSGraphicsContext saveGraphicsState];
                [NSGraphicsContext setCurrentContext : context];
                [*inBitmap_ drawAtPoint: NSZeroPoint fromRect: NSZeroRect operation: NSCompositingOperationCopy fraction: 1.0];
                
                [NSGraphicsContext restoreGraphicsState];
            }
        }
        
   		inline bitmapPixels::~bitmapPixels()
        {
            if (0 != (flags & GmpiDrawing_API::MP1_BITMAP_LOCK_WRITE))
            {
                // scan for overbright pixels
                bool hasOverbright = false;
                {
                    GmpiDrawing_API::MP1_SIZE_U imageSize{};
                    seBitmap->GetSize(&imageSize);
                    const int totalPixels = imageSize.height * getBytesPerRow() / sizeof(uint32_t);

                    uint32_t* destPixels = (uint32_t*) getAddress();

                    for (int i = 0; i < totalPixels; ++i)
                    {
                        uint8_t* p = (uint8_t*) destPixels;
                        auto& alpha = p[3];
                        if(alpha != 255 && *destPixels != 0) // skip solid or blank pixels.
                        {
                            if(p[0] > alpha || p[1] > alpha || p[2] > alpha)
                            {
                                hasOverbright = true;
                                break;
                            }
                        }

                        ++destPixels;
                    }
                }
                
                if(hasOverbright)
                {
                    // create and populate the additive bitmap
                    NSSize s = [bitmap2 size];
                    
                    constexpr int bitsPerSample = 8;
                    constexpr int samplesPerPixel = 4;
                    constexpr int bitsPerPixel = bitsPerSample * samplesPerPixel;

                    NSBitmapImageRep* initial_bitmap = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:nil
                        pixelsWide : s.width
                        pixelsHigh : s.height
                        bitsPerSample : bitsPerSample
                        samplesPerPixel : samplesPerPixel
                        hasAlpha : YES
                        isPlanar : NO
                        colorSpaceName: NSCalibratedRGBColorSpace
                        bitmapFormat : 0
                        bytesPerRow : bytesPerRow
                        bitsPerPixel : bitsPerPixel];
                                                          
                    auto source = (uint32_t*)([bitmap2 bitmapData]);
                    auto dest = (uint32_t*)([initial_bitmap bitmapData]);
                    
                    const int totalPixels = s.height * bytesPerRow / sizeof(uint32_t);

                    for (int i = 0; i < totalPixels; ++i)
                    {
                        // Retain Alpha of bitmap2, but black-out RGB. Copy RGB to additiveBitmap_ and set alpha to 255.
                        *dest = 0xFF000000 | *source; //0x88008888;
                        *source &= 0xFF000000;
                        
                        ++source;
                        ++dest;
                    }
                    seBitmap->additiveBitmap_ = [initial_bitmap bitmapImageRepByRetaggingWithColorSpace:NSColorSpace.sRGBColorSpace];
                    
                    [seBitmap->additiveBitmap_ retain];
                }
                
                // replace bitmap with a fresh one, and add pixels to it.
                *inBitmap_ = [[NSImage alloc] init];
                [*inBitmap_ addRepresentation:bitmap2];
            }
            else
            {
                [bitmap2 release];
            }
        }
	} // namespace
} // namespace
