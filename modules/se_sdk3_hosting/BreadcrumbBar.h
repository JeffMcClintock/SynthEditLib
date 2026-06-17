#pragma once
// Cross-platform breadcrumb navigation bar, drawn with gmpi_ui. A gmpi
// IDrawingClient + IInputClient meant to sit in the top strip of the editor
// swapchain (see TopStripLayout). It owns the retained-trail navigation state
// and the thumbnail cache; the platform supplies two callbacks:
//   onNavigate(target)            - go to that container (e.g. retarget the tab)
//   renderThumbnail(container,vf) - render a container's view to a gmpi Bitmap on
//                                   the editor's device (so drawBitmap can draw it)
// No winrt / no Cocoa here.

#include <vector>
#include <map>
#include <string>
#include <utility>
#include <functional>
#include <algorithm>

#include "Drawing.h"
#include "GraphicsRedrawClient.h"
#include "helpers/NativeUi.h"
#include "experimental/theme.h"

#include "CContainer.h"
#include "SynthEditDocBase.h"

namespace SE2
{

struct BreadcrumbBar :
      public gmpi::api::IDrawingClient
    , public gmpi::api::IInputClient
{
    // ---- tunables ----
    static constexpr float kStripHeight   = 52.0f;
    static constexpr float kPadX          = 8.0f;
    static constexpr float kGap           = 4.0f;
    static constexpr float kChevronW      = 14.0f;
    static constexpr float kThumbPad      = 7.0f;   // vertical inset of thumbnail
    static constexpr float kThumbAspect   = 160.0f / 96.0f;
    static constexpr float kThumbRadius   = 4.0f;   // rounded-corner radius of the thumbnail clip
    static constexpr float kNameW         = 96.0f;
    static constexpr float kFontHeight    = 13.0f;

    // ---- wiring (platform supplies these) ----
    std::function<void(CContainer*)> onNavigate;
    std::function<gmpi::drawing::Bitmap(CContainer*, int /*view_flag*/)> renderThumbnail;

    // ---- state ----
    // Raw (not shared_ptr): hosts are GMPI_REFCOUNT_NO_DELETE and owned elsewhere
    // (the TopStripLayout's ChildHost). This bar is persistent across tab switches,
    // so a held shared_ptr would dangle when the old TopStripLayout is destroyed and
    // crash on release at the next setHost. A raw pointer is simply re-pointed.
    gmpi::api::IDrawingHost* drawingHost = nullptr;
    gmpi::api::IInputHost* inputHost = nullptr;
    gmpi::drawing::Rect bounds{};

    CContainer* currentContainer{};
    int viewFlag{};
    int deepestHandle = 0;
    std::vector<CContainer*> trail; // root -> deepest

    std::map<std::pair<int, int>, gmpi::drawing::Bitmap> thumbnailCache;

    struct CrumbHit { float x0; float x1; int handle; };
    std::vector<CrumbHit> crumbHits;

    gmpi::drawing::TextFormat textFormat;

    // ---- public API ----
    void setCurrent(CContainer* current, int view_flag)
    {
        currentContainer = current;
        viewFlag = view_flag;
        rebuildTrail();
        // Render thumbnails now (off the paint path) so render() only draws cached
        // bitmaps and the heavier offscreen render stays out of the paint loop.
        for (CContainer* c : trail)
            thumbnailFor(c);
        invalidate();
    }

    void invalidateThumbnail(CContainer* container)
    {
        if (!container)
            return;
        const int h = container->Handle();
        thumbnailCache.erase({ h, CF_STRUCTURE_VIEW });
        thumbnailCache.erase({ h, CF_PANEL_VIEW });
        invalidate();
    }

    void clearThumbnails() { thumbnailCache.clear(); }

    // Repaint (e.g. after a container rename — the label is read live in render()).
    void refresh() { invalidate(); }

private:
    void invalidate()
    {
        if (drawingHost)
            drawingHost->invalidateRect(nullptr);
    }

    CContainer* resolveHandle(int handle) const
    {
        if (handle == 0 || !currentContainer || !currentContainer->Document())
            return nullptr;
        return dynamic_cast<CContainer*>(
            currentContainer->Document()->uniqueIdDatabase.HandleToObjectWithNull(handle));
    }

    void rebuildTrail()
    {
        trail.clear();
        if (!currentContainer)
        {
            deepestHandle = 0;
            return;
        }

        // Retained-trail anchor: keep `deepest` while the current container is on
        // its ancestry (navigating up / back down the same branch); otherwise the
        // user branched elsewhere, so reset the anchor to current.
        CContainer* deepest = resolveHandle(deepestHandle);
        bool currentOnBranch = false;
        if (deepest)
            for (CContainer* c = deepest; c; c = c->Container())
                if (c == currentContainer) { currentOnBranch = true; break; }

        if (!deepest || !currentOnBranch)
        {
            deepest = currentContainer;
            deepestHandle = currentContainer->Handle();
        }

        for (CContainer* c = deepest; c; c = c->Container())
            trail.push_back(c);
        std::reverse(trail.begin(), trail.end());
    }

    gmpi::drawing::Bitmap thumbnailFor(CContainer* container)
    {
        const auto key = std::make_pair(container->Handle(), viewFlag);
        if (auto it = thumbnailCache.find(key); it != thumbnailCache.end())
            return it->second;

        gmpi::drawing::Bitmap bmp;
        if (renderThumbnail)
            bmp = renderThumbnail(container, viewFlag);
        if (bmp)
            thumbnailCache[key] = bmp;
        return bmp;
    }

    static std::string toUtf8(const std::wstring& w)
    {
        std::string out;
        out.reserve(w.size());
        for (wchar_t wc : w)
        {
            const unsigned c = static_cast<unsigned>(wc);
            if (c < 0x80) out += static_cast<char>(c);
            else if (c < 0x800) { out += static_cast<char>(0xC0 | (c >> 6)); out += static_cast<char>(0x80 | (c & 0x3F)); }
            else { out += static_cast<char>(0xE0 | (c >> 12)); out += static_cast<char>(0x80 | ((c >> 6) & 0x3F)); out += static_cast<char>(0x80 | (c & 0x3F)); }
        }
        return out;
    }

public:
    // ---- IDrawingClient ----
    gmpi::ReturnCode setHost(gmpi::api::IUnknown* phost) override
    {
        drawingHost = nullptr;
        inputHost = nullptr;
        if (phost)
        {
            gmpi::shared_ptr<gmpi::api::IUnknown> u;
            u = phost;
            drawingHost = u.as<gmpi::api::IDrawingHost>().get(); // raw; temp release is a NO_DELETE no-op
            inputHost = u.as<gmpi::api::IInputHost>().get();
        }
        return gmpi::ReturnCode::Ok;
    }

    gmpi::ReturnCode measure(const gmpi::drawing::Size* availableSize, gmpi::drawing::Size* returnDesiredSize) override
    {
        if (returnDesiredSize)
        {
            returnDesiredSize->width = availableSize->width;
            returnDesiredSize->height = kStripHeight;
        }
        return gmpi::ReturnCode::Ok;
    }

    gmpi::ReturnCode arrange(const gmpi::drawing::Rect* finalRect) override
    {
        bounds = *finalRect;
        return gmpi::ReturnCode::Ok;
    }

    gmpi::ReturnCode render(gmpi::drawing::api::IDeviceContext* dc) override
    {
        gmpi::drawing::Graphics g(dc);
        const auto& theme = gmpi::ui::currentTheme();

        // Background strip + bottom divider.
        {
            auto bg = g.createSolidColorBrush(theme.controlBackground);
            g.fillRectangle(bounds, bg);
        }

        if (!textFormat)
        {
            const std::array<std::string_view, 2> families{ "Segoe UI", "Arial" };
            textFormat = g.getFactory().createTextFormat(kFontHeight, families);
            textFormat.setWordWrapping(gmpi::drawing::WordWrapping::NoWrap);
            textFormat.setParagraphAlignment(gmpi::drawing::ParagraphAlignment::Center);
        }

        // The current crumb ("you are here") shows at full strength; the rest are
        // dimmed (the theme has no dedicated accent colour).
        gmpi::drawing::Color dimColor = theme.controlText;
        dimColor.a *= 0.55f;
        auto currentBrush = g.createSolidColorBrush(theme.controlText);
        auto dimBrush     = g.createSolidColorBrush(dimColor);
        auto& chevronBrush = dimBrush;

        const float thumbH = (bounds.bottom - bounds.top) - 2.0f * kThumbPad;
        const float thumbW = thumbH * kThumbAspect;
        const float midY = 0.5f * (bounds.top + bounds.bottom);

        crumbHits.clear();
        float x = bounds.left + kPadX;

        for (size_t i = 0; i < trail.size(); ++i)
        {
            CContainer* c = trail[i];
            const bool isCurrent = (c == currentContainer);

            if (i > 0)
            {
                const gmpi::drawing::Rect cr{ x, bounds.top, x + kChevronW, bounds.bottom };
                g.drawTextU("\xE2\x80\xBA", textFormat, cr, chevronBrush, gmpi::drawing::DrawTextOptions::Clip); // U+203A
                x += kChevronW;
            }

            const float x0 = x;

            if (auto thumb = thumbnailFor(c))
            {
                const auto sz = thumb.getSize();
                const gmpi::drawing::Rect dest{ x, midY - 0.5f * thumbH, x + thumbW, midY + 0.5f * thumbH };
                const gmpi::drawing::Rect src{ 0.0f, 0.0f, static_cast<float>(sz.width), static_cast<float>(sz.height) };

                // Soften the thumbnail corners by clipping the draw to a rounded rectangle.
                auto thumbClip = g.getFactory().createPathGeometry();
                {
                    auto sink = thumbClip.open();
                    sink.addRoundedRect({ dest, kThumbRadius, kThumbRadius });
                    sink.close();
                }
                {
                    gmpi::drawing::ClipDrawingToGeometry clip(g, thumbClip);
                    g.drawBitmap(thumb, dest, src);
                }

                x += thumbW + kGap;
            }

            const std::string name = toUtf8(c->GetName());
            const gmpi::drawing::Rect nr{ x, bounds.top, x + kNameW, bounds.bottom };
            g.drawTextU(name, textFormat, nr, isCurrent ? currentBrush : dimBrush, gmpi::drawing::DrawTextOptions::Clip);
            x += kNameW + kPadX;

            crumbHits.push_back({ x0, x, c->Handle() });
        }

        return gmpi::ReturnCode::Ok;
    }

    gmpi::ReturnCode getClipArea(gmpi::drawing::Rect* returnRect) override
    {
        if (returnRect)
            *returnRect = bounds;
        return gmpi::ReturnCode::Ok;
    }

    // ---- IInputClient ----
    gmpi::ReturnCode setHover(bool) override { return gmpi::ReturnCode::Ok; }

    gmpi::ReturnCode hitTest(gmpi::drawing::Point point, int32_t) override
    {
        for (const auto& h : crumbHits)
            if (point.x >= h.x0 && point.x < h.x1)
                return gmpi::ReturnCode::Ok;
        return gmpi::ReturnCode::Unhandled;
    }

    gmpi::ReturnCode onPointerDown(gmpi::drawing::Point point, int32_t) override
    {
        for (const auto& h : crumbHits)
        {
            if (point.x >= h.x0 && point.x < h.x1)
            {
                if (auto* target = resolveHandle(h.handle))
                {
                    if (target != currentContainer && onNavigate)
                        onNavigate(target);
                }
                else
                {
                    rebuildTrail(); // crumb's container is gone — refresh
                    invalidate();
                }
                return gmpi::ReturnCode::Ok;
            }
        }
        return gmpi::ReturnCode::Unhandled;
    }

    gmpi::ReturnCode onPointerMove(gmpi::drawing::Point, int32_t) override { return gmpi::ReturnCode::Unhandled; }
    gmpi::ReturnCode onPointerUp(gmpi::drawing::Point, int32_t) override   { return gmpi::ReturnCode::Unhandled; }
    gmpi::ReturnCode onMouseWheel(gmpi::drawing::Point, int32_t, int32_t) override { return gmpi::ReturnCode::Unhandled; }
    gmpi::ReturnCode populateContextMenu(gmpi::drawing::Point, gmpi::api::IUnknown*) override { return gmpi::ReturnCode::Unhandled; }
    gmpi::ReturnCode onKeyPress(wchar_t) override { return gmpi::ReturnCode::Unhandled; }
    gmpi::ReturnCode getToolTip(gmpi::drawing::Point, gmpi::api::IString*) override { return gmpi::ReturnCode::Unhandled; }

    gmpi::ReturnCode queryInterface(const gmpi::api::Guid* iid, void** returnInterface) override
    {
        *returnInterface = {};
        GMPI_QUERYINTERFACE(gmpi::api::IInputClient);
        GMPI_QUERYINTERFACE(gmpi::api::IDrawingClient);
        return gmpi::ReturnCode::NoSupport;
    }
    GMPI_REFCOUNT;
};

} // namespace SE2
