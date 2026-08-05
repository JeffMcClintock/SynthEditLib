#pragma once

/*
#include "GmpiCpuUniversalContext.h"
*/

// A device context that looks modern to gmpi_ui and legacy to SDK3 plugins.
//
// The live Windows editor gets this from se::directx::UniversalGraphicsContext
// (DirectXGfx.h) and the live Mac editor from se::cocoa::UniversalGraphicsContext,
// but both of those are welded to their platform's backend. Any host that drives
// a BACKEND-NEUTRAL gmpi::drawing::api::IDeviceContext — the screenshot CLI and
// the debug software-renderer option, both of which render through gmpi_ui's CPU
// backend — needs the same trick over an arbitrary context, which is what lives
// here.
//
// Pair it with se::GmpiToSDK3Factory (GmpiUiToSDK3.h) rather than the platform's
// native SDK3 factory: the bridge's CreateBitmapBrush / DrawBitmap / etc.
// dynamic_cast their resource arguments to the g3_X wrapper types, and native
// SDK3 resources fail that cast and dereference null. Context and factory have
// to come from the same side of the bridge.

#include <cstring>

#include "GmpiApiDrawing.h"
#include "GmpiSdkCommon.h"
#include "GmpiUiToSDK3.h"

namespace se
{

// Universal device context: looks like a modern gmpi::drawing::api::IDeviceContext
// to the host, but answers queryInterface for the legacy SDK3 GUIDs by handing
// back a separate object whose vtable actually matches the legacy
// IMpDeviceContext layout.
//
// The legacy and modern vtables disagree in arg counts and return types
// (CreateSolidColorBrush is 2 vs 3 args, CreateGradientStopCollection is 3 vs 4,
// most Draw*/Fill* are void vs ReturnCode, etc.), so a reinterpret-style cast
// would crash the moment a legacy plugin called any drawing method. The legacy
// sidecar (se::GmpiToSDK3Context) implements the legacy vtable and translates
// each call into the modern equivalent on the wrapped context.
//
// This mirrors the live editor's UniversalGraphicsContext (DirectXGfx.h):
// modern interface on the outer object, legacy interface served via a member
// adapter, queryInterface routes by GUID.
struct DeviceContextLegacyAdapter
    : public gmpi::drawing::api::IBitmapRenderTarget
{
    gmpi::drawing::api::IDeviceContext* modernContext{};
    gmpi::shared_ptr<GmpiDrawing_API::IMpFactory2> sdk3Factory;
    se::GmpiToSDK3Context sdk3Context;

    DeviceContextLegacyAdapter(
        gmpi::drawing::api::IDeviceContext* ctx,
        GmpiDrawing_API::IMpFactory2* legacyFactory)
        : modernContext(ctx)
        , sdk3Factory(legacyFactory) // hold a ref for the lifetime of sdk3Context
        , sdk3Context(legacyFactory, /*fallback*/ nullptr, ctx)
    {
        if (modernContext)
            modernContext->addRef();
    }

    ~DeviceContextLegacyAdapter()
    {
        if (modernContext)
            modernContext->release();
    }

    gmpi::ReturnCode queryInterface(const gmpi::api::Guid* iid, void** returnInterface) override
    {
        *returnInterface = nullptr;

        // Legacy SDK3 plugins reach this via Graphics(IMpUnknown*), which calls
        // queryInterface(SE_IID_DEVICECONTEXT_MPGUI, ...) through the IMpUnknown
        // vtable. They need an object whose vtable matches IMpDeviceContext —
        // sdk3Context, NOT this adapter (whose vtable matches the modern
        // IDeviceContext).
        if (std::memcmp(iid, &GmpiDrawing_API::SE_IID_DEVICECONTEXT_MPGUI, sizeof(*iid)) == 0
         || std::memcmp(iid, &GmpiDrawing_API::IMpDeviceContextExt::guid,   sizeof(*iid)) == 0)
        {
            return static_cast<gmpi::ReturnCode>(
                sdk3Context.queryInterface(
                    *reinterpret_cast<const gmpi::MpGuid*>(iid),
                    returnInterface));
        }

        // Modern IBitmapRenderTarget
        if (*iid == gmpi::drawing::api::IBitmapRenderTarget::guid)
        {
            *returnInterface = static_cast<gmpi::drawing::api::IBitmapRenderTarget*>(this);
            addRef();
            return gmpi::ReturnCode::Ok;
        }

        // Modern IDeviceContext
        if (*iid == gmpi::drawing::api::IDeviceContext::guid)
        {
            *returnInterface = static_cast<gmpi::drawing::api::IDeviceContext*>(this);
            addRef();
            return gmpi::ReturnCode::Ok;
        }

        return modernContext->queryInterface(iid, returnInterface);
    }

    int32_t addRef() override
    {
        return modernContext->addRef();
    }

    int32_t release() override
    {
        return modernContext->release();
    }

    // IResource
    gmpi::ReturnCode getFactory(gmpi::drawing::api::IFactory** factory) override
    {
        return modernContext->getFactory(factory);
    }

    // Forward all IDeviceContext methods to the wrapped modern context
    gmpi::ReturnCode createBitmapBrush(
        gmpi::drawing::api::IBitmap* bitmap,
        const gmpi::drawing::BrushProperties* brushProperties,
        gmpi::drawing::api::IBitmapBrush** bitmapBrush) override
    {
        return modernContext->createBitmapBrush(bitmap, brushProperties, bitmapBrush);
    }

    gmpi::ReturnCode createSolidColorBrush(
        const gmpi::drawing::Color* color,
        const gmpi::drawing::BrushProperties* brushProperties,
        gmpi::drawing::api::ISolidColorBrush** solidColorBrush) override
    {
        return modernContext->createSolidColorBrush(color, brushProperties, solidColorBrush);
    }

    gmpi::ReturnCode createGradientstopCollection(
        const gmpi::drawing::Gradientstop* gradientstops,
        uint32_t gradientstopsCount,
        gmpi::drawing::ExtendMode extendMode,
        gmpi::drawing::api::IGradientstopCollection** gradientstopCollection) override
    {
        return modernContext->createGradientstopCollection(
            gradientstops, gradientstopsCount, extendMode, gradientstopCollection);
    }

    gmpi::ReturnCode createLinearGradientBrush(
        const gmpi::drawing::LinearGradientBrushProperties* linearGradientBrushProperties,
        const gmpi::drawing::BrushProperties* brushProperties,
        gmpi::drawing::api::IGradientstopCollection* gradientstopCollection,
        gmpi::drawing::api::ILinearGradientBrush** linearGradientBrush) override
    {
        return modernContext->createLinearGradientBrush(
            linearGradientBrushProperties, brushProperties,
            gradientstopCollection, linearGradientBrush);
    }

    gmpi::ReturnCode createRadialGradientBrush(
        const gmpi::drawing::RadialGradientBrushProperties* radialGradientBrushProperties,
        const gmpi::drawing::BrushProperties* brushProperties,
        gmpi::drawing::api::IGradientstopCollection* gradientstopCollection,
        gmpi::drawing::api::IRadialGradientBrush** radialGradientBrush) override
    {
        return modernContext->createRadialGradientBrush(
            radialGradientBrushProperties, brushProperties,
            gradientstopCollection, radialGradientBrush);
    }

    gmpi::ReturnCode drawLine(
        gmpi::drawing::Point point0,
        gmpi::drawing::Point point1,
        gmpi::drawing::api::IBrush* brush,
        float strokeWidth,
        gmpi::drawing::api::IStrokeStyle* strokeStyle) override
    {
        return modernContext->drawLine(point0, point1, brush, strokeWidth, strokeStyle);
    }

    gmpi::ReturnCode drawRectangle(
        const gmpi::drawing::Rect* rect,
        gmpi::drawing::api::IBrush* brush,
        float strokeWidth,
        gmpi::drawing::api::IStrokeStyle* strokeStyle) override
    {
        return modernContext->drawRectangle(rect, brush, strokeWidth, strokeStyle);
    }

    gmpi::ReturnCode fillRectangle(
        const gmpi::drawing::Rect* rect,
        gmpi::drawing::api::IBrush* brush) override
    {
        return modernContext->fillRectangle(rect, brush);
    }

    gmpi::ReturnCode drawRoundedRectangle(
        const gmpi::drawing::RoundedRect* roundedRect,
        gmpi::drawing::api::IBrush* brush,
        float strokeWidth,
        gmpi::drawing::api::IStrokeStyle* strokeStyle) override
    {
        return modernContext->drawRoundedRectangle(roundedRect, brush, strokeWidth, strokeStyle);
    }

    gmpi::ReturnCode fillRoundedRectangle(
        const gmpi::drawing::RoundedRect* roundedRect,
        gmpi::drawing::api::IBrush* brush) override
    {
        return modernContext->fillRoundedRectangle(roundedRect, brush);
    }

    gmpi::ReturnCode drawEllipse(
        const gmpi::drawing::Ellipse* ellipse,
        gmpi::drawing::api::IBrush* brush,
        float strokeWidth,
        gmpi::drawing::api::IStrokeStyle* strokeStyle) override
    {
        return modernContext->drawEllipse(ellipse, brush, strokeWidth, strokeStyle);
    }

    gmpi::ReturnCode fillEllipse(
        const gmpi::drawing::Ellipse* ellipse,
        gmpi::drawing::api::IBrush* brush) override
    {
        return modernContext->fillEllipse(ellipse, brush);
    }

    gmpi::ReturnCode drawGeometry(
        gmpi::drawing::api::IPathGeometry* geometry,
        gmpi::drawing::api::IBrush* brush,
        float strokeWidth,
        gmpi::drawing::api::IStrokeStyle* strokeStyle) override
    {
        return modernContext->drawGeometry(geometry, brush, strokeWidth, strokeStyle);
    }

    gmpi::ReturnCode fillGeometry(
        gmpi::drawing::api::IPathGeometry* geometry,
        gmpi::drawing::api::IBrush* brush,
        gmpi::drawing::api::IBrush* opacityBrush) override
    {
        return modernContext->fillGeometry(geometry, brush, opacityBrush);
    }

    gmpi::ReturnCode drawBitmap(
        gmpi::drawing::api::IBitmap* bitmap,
        const gmpi::drawing::Rect* destinationRectangle,
        float opacity,
        gmpi::drawing::BitmapInterpolationMode interpolationMode,
        const gmpi::drawing::Rect* sourceRectangle) override
    {
        return modernContext->drawBitmap(
            bitmap, destinationRectangle, opacity, interpolationMode, sourceRectangle);
    }

    gmpi::ReturnCode drawTextU(
        const char* utf8String,
        uint32_t stringLength,
        gmpi::drawing::api::ITextFormat* textFormat,
        const gmpi::drawing::Rect* layoutRect,
        gmpi::drawing::api::IBrush* brush,
        int32_t flags) override
    {
        return modernContext->drawTextU(
            utf8String, stringLength, textFormat, layoutRect, brush, flags);
    }

    gmpi::ReturnCode drawRichTextU(
        gmpi::drawing::api::IRichTextFormat* richTextFormat,
        const gmpi::drawing::Rect* layoutRect,
        gmpi::drawing::api::IBrush* brush,
        int32_t flags) override
    {
        return modernContext->drawRichTextU(richTextFormat, layoutRect, brush, flags);
    }

    gmpi::ReturnCode setTransform(const gmpi::drawing::Matrix3x2* transform) override
    {
        return modernContext->setTransform(transform);
    }

    gmpi::ReturnCode getTransform(gmpi::drawing::Matrix3x2* transform) override
    {
        return modernContext->getTransform(transform);
    }

    gmpi::ReturnCode pushAxisAlignedClip(const gmpi::drawing::Rect* clipRect) override
    {
        return modernContext->pushAxisAlignedClip(clipRect);
    }

    gmpi::ReturnCode pushClipGeometry(gmpi::drawing::api::IPathGeometry* geometry) override
    {
        return modernContext->pushClipGeometry(geometry);
    }

    gmpi::ReturnCode popAxisAlignedClip() override
    {
        return modernContext->popAxisAlignedClip();
    }

    gmpi::ReturnCode getAxisAlignedClip(gmpi::drawing::Rect* returnClipRect) override
    {
        return modernContext->getAxisAlignedClip(returnClipRect);
    }

    gmpi::ReturnCode clear(const gmpi::drawing::Color* clearColor) override
    {
        return modernContext->clear(clearColor);
    }

    gmpi::ReturnCode beginDraw() override
    {
        return modernContext->beginDraw();
    }

    gmpi::ReturnCode endDraw() override
    {
        return modernContext->endDraw();
    }

    gmpi::ReturnCode createCompatibleRenderTarget(
        gmpi::drawing::Size desiredSize,
        int32_t flags,
        gmpi::drawing::api::IBitmapRenderTarget** bitmapRenderTarget) override
    {
        return modernContext->createCompatibleRenderTarget(desiredSize, flags, bitmapRenderTarget);
    }

    // IBitmapRenderTarget
    gmpi::ReturnCode getBitmap(gmpi::drawing::api::IBitmap** returnBitmap) override
    {
        gmpi::drawing::api::IBitmapRenderTarget* brt{};
        auto hr = modernContext->queryInterface(
            &gmpi::drawing::api::IBitmapRenderTarget::guid,
            reinterpret_cast<void**>(&brt));
        if (hr == gmpi::ReturnCode::Ok && brt)
        {
            hr = brt->getBitmap(returnBitmap);
            brt->release();
            return hr;
        }
        return gmpi::ReturnCode::Fail;
    }
};

} // namespace se
