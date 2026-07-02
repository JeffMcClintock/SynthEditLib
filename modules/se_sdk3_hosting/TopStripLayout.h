#pragma once
// A minimal two-region vertical layout for the gmpi drawing-client pipeline:
// a fixed-height TOP strip plus a FILL region below. Both children are gmpi
// IDrawingClient / IInputClient. Modeled on GmpiUiLayer (Pile.h) — per-child host
// that offsets invalidations into parent space and forwards the real
// rasterization scale (so the editor below keeps correct DPI) — but it splits the
// area into two stacked rectangles and routes pointer input by Y.
//
// Used to draw the breadcrumb bar across the top of the editor swapchain with the
// editor view filling the rest. Cross-platform (no winrt / no Cocoa).

#include <algorithm>
#include "Drawing.h"
#include "GraphicsRedrawClient.h"
#include "helpers/NativeUi.h"

namespace SE2
{

struct TopStripLayout :
      public gmpi::api::IDrawingClient
    , public gmpi::api::IInputClient
    , public gmpi::api::IGraphicsRedrawClient
{
    // Per-child host: forwards everything to the parent's host, mapping the
    // child's (0,0)-origin invalidations into parent coordinates via `offset`.
    struct ChildHost :
          public gmpi::api::IDrawingHost
        , public gmpi::api::IInputHost
        , public gmpi::api::IDialogHost
    {
        TopStripLayout* owner{};
        gmpi::drawing::Size offset{};

        // IDrawingHost
        gmpi::ReturnCode getDrawingFactory(gmpi::api::IUnknown** returnFactory) override
        {
            return owner->drawingHost ? owner->drawingHost->getDrawingFactory(returnFactory)
                                      : gmpi::ReturnCode::Fail;
        }
        void invalidateRect(const gmpi::drawing::Rect* invalidRect) override
        {
            if (!owner->drawingHost)
                return;
            if (invalidRect)
            {
                const auto r = gmpi::drawing::offsetRect(*invalidRect, offset);
                owner->drawingHost->invalidateRect(&r);
            }
            else
            {
                owner->drawingHost->invalidateRect(nullptr);
            }
        }
        void invalidateMeasure() override { if (owner->drawingHost) owner->drawingHost->invalidateMeasure(); }
        float getRasterizationScale() override { return owner->drawingHost ? owner->drawingHost->getRasterizationScale() : 1.0f; }

        // IInputHost
        gmpi::ReturnCode setCapture() override     { return owner->inputHost ? owner->inputHost->setCapture()     : gmpi::ReturnCode::Fail; }
        gmpi::ReturnCode getCapture(bool& v) override { if (owner->inputHost) return owner->inputHost->getCapture(v); v = false; return gmpi::ReturnCode::Ok; }
        gmpi::ReturnCode releaseCapture() override { return owner->inputHost ? owner->inputHost->releaseCapture() : gmpi::ReturnCode::Fail; }

        // IDialogHost — the child supplies these rects in its own (0,0)-origin
        // space; like invalidateRect, map them into parent space via `offset` so the
        // native text-edit / popup / key-listener lands at the child's on-screen
        // position. Without this the editor's (bottom child) text-edit appeared at
        // the top of the swap-chain, ignoring the breadcrumb strip above it (the
        // strip height is baked into offset.height).
        gmpi::ReturnCode createTextEdit(const gmpi::drawing::Rect* r, gmpi::api::IUnknown** o) override
        {
            if (!owner->dialogHost) return gmpi::ReturnCode::NoSupport;
            const auto pr = gmpi::drawing::offsetRect(*r, offset);
            return owner->dialogHost->createTextEdit(&pr, o);
        }
        gmpi::ReturnCode createPopupMenu(const gmpi::drawing::Rect* r, gmpi::api::IUnknown** o) override
        {
            if (!owner->dialogHost) return gmpi::ReturnCode::NoSupport;
            const auto pr = gmpi::drawing::offsetRect(*r, offset);
            return owner->dialogHost->createPopupMenu(&pr, o);
        }
        gmpi::ReturnCode createKeyListener(const gmpi::drawing::Rect* r, gmpi::api::IUnknown** o) override
        {
            if (!owner->dialogHost) return gmpi::ReturnCode::NoSupport;
            const auto pr = gmpi::drawing::offsetRect(*r, offset);
            return owner->dialogHost->createKeyListener(&pr, o);
        }
        gmpi::ReturnCode createFileDialog(int32_t t, gmpi::api::IUnknown** o) override                     { return owner->dialogHost ? owner->dialogHost->createFileDialog(t, o)  : gmpi::ReturnCode::NoSupport; }
        gmpi::ReturnCode createStockDialog(int32_t t, const char* a, const char* b, gmpi::api::IUnknown** o) override { return owner->dialogHost ? owner->dialogHost->createStockDialog(t, a, b, o) : gmpi::ReturnCode::NoSupport; }
        gmpi::ReturnCode createColorDialog(gmpi::drawing::Color initialColor, gmpi::api::IUnknown** o) override { return owner->dialogHost ? owner->dialogHost->createColorDialog(initialColor, o) : gmpi::ReturnCode::NoSupport; }

        gmpi::ReturnCode queryInterface(const gmpi::api::Guid* iid, void** returnInterface) override
        {
            *returnInterface = {};
            GMPI_QUERYINTERFACE(gmpi::api::IDrawingHost);
            GMPI_QUERYINTERFACE(gmpi::api::IInputHost);
            GMPI_QUERYINTERFACE(gmpi::api::IDialogHost);
            return gmpi::ReturnCode::NoSupport;
        }
        GMPI_REFCOUNT_NO_DELETE;
    };

    struct child
    {
        ChildHost host;
        gmpi::drawing::Rect pos{}; // position within the parent (this layout)
        gmpi::shared_ptr<gmpi::api::IDrawingClient> graphic;
        gmpi::shared_ptr<gmpi::api::IInputClient> editor;
        gmpi::shared_ptr<gmpi::api::IGraphicsRedrawClient> redraw;
    };

    gmpi::shared_ptr<gmpi::api::IDrawingHost> drawingHost;
    gmpi::shared_ptr<gmpi::api::IInputHost> inputHost;
    gmpi::shared_ptr<gmpi::api::IDialogHost> dialogHost;

    child topChild;
    child bottomChild;
    float topStripHeight = 52.0f;
    gmpi::drawing::Rect bounds{};
    child* capturedChild{}; // routes move/up to the child that took the down

    ~TopStripLayout()
    {
        // Sever children before this layout's ChildHosts are destroyed, so a
        // persistent child (the breadcrumb bar, reused across tab switches) drops
        // its host pointer rather than dangling onto a dead ChildHost.
        if (topChild.graphic)    topChild.graphic->setHost(nullptr);
        if (bottomChild.graphic) bottomChild.graphic->setHost(nullptr);
    }

    void setTop(gmpi::api::IUnknown* c)    { setChild(topChild, c); }
    void setBottom(gmpi::api::IUnknown* c) { setChild(bottomChild, c); }

    void setChild(child& ch, gmpi::api::IUnknown* c)
    {
        ch.host.owner = this;
        gmpi::shared_ptr<gmpi::api::IUnknown> u;
        u = c;
        ch.graphic = u.as<gmpi::api::IDrawingClient>();
        ch.editor = u.as<gmpi::api::IInputClient>();
        ch.redraw = u.as<gmpi::api::IGraphicsRedrawClient>();
        if (ch.graphic)
            ch.graphic->setHost(static_cast<gmpi::api::IDrawingHost*>(&ch.host));
    }

    // ---- IGraphicsRedrawClient ----
    // Transparent passthrough: forward the per-tick redraw heartbeat to both
    // children. Without this the editor view (bottomChild), buried under this
    // layout in the Pile, never receives preGraphicsRedraw and its DSP→GUI queue
    // (meters, scopes, value updates via ViewBase::serviceGuiQueue) goes stale.
    void preGraphicsRedraw() override
    {
        if (topChild.redraw)    topChild.redraw->preGraphicsRedraw();
        if (bottomChild.redraw) bottomChild.redraw->preGraphicsRedraw();
    }

    // ---- IDrawingClient ----
    gmpi::ReturnCode setHost(gmpi::api::IUnknown* phost) override
    {
        gmpi::shared_ptr<gmpi::api::IUnknown> u;
        u = phost;
        drawingHost = u.as<gmpi::api::IDrawingHost>();
        inputHost = u.as<gmpi::api::IInputHost>();
        dialogHost = u.as<gmpi::api::IDialogHost>();
        return gmpi::ReturnCode::Ok;
    }

    gmpi::ReturnCode measure(const gmpi::drawing::Size* availableSize, gmpi::drawing::Size* returnDesiredSize) override
    {
        if (returnDesiredSize)
            *returnDesiredSize = *availableSize;

        gmpi::drawing::Size d{};
        if (topChild.graphic)    topChild.graphic->measure(availableSize, &d);
        if (bottomChild.graphic) bottomChild.graphic->measure(availableSize, &d);
        return gmpi::ReturnCode::Ok;
    }

    gmpi::ReturnCode arrange(const gmpi::drawing::Rect* finalRect) override
    {
        bounds = *finalRect;
        const float strip = (std::min)(topStripHeight, finalRect->bottom - finalRect->top);

        topChild.pos    = { finalRect->left, finalRect->top,         finalRect->right, finalRect->top + strip };
        bottomChild.pos = { finalRect->left, finalRect->top + strip, finalRect->right, finalRect->bottom };

        arrangeChild(topChild);
        arrangeChild(bottomChild);
        return gmpi::ReturnCode::Ok;
    }

    void arrangeChild(child& ch)
    {
        if (!ch.graphic)
            return;
        ch.host.offset = { ch.pos.left, ch.pos.top };
        // Child works in (0,0)-origin local coordinates; the layout positions it.
        const gmpi::drawing::Rect local{ 0.0f, 0.0f, ch.pos.right - ch.pos.left, ch.pos.bottom - ch.pos.top };
        ch.graphic->arrange(&local);
    }

    gmpi::ReturnCode render(gmpi::drawing::api::IDeviceContext* dc) override
    {
        gmpi::drawing::Graphics g(dc);
        const auto base = g.getTransform();
        renderChild(g, dc, bottomChild, base);
        renderChild(g, dc, topChild, base);
        g.setTransform(base);
        return gmpi::ReturnCode::Ok;
    }

    void renderChild(gmpi::drawing::Graphics& g, gmpi::drawing::api::IDeviceContext* dc, child& ch, const gmpi::drawing::Matrix3x2& base)
    {
        if (!ch.graphic)
            return;
        g.setTransform(gmpi::drawing::makeTranslation(ch.pos.left, ch.pos.top) * base);
        const gmpi::drawing::Rect local{ 0.0f, 0.0f, ch.pos.right - ch.pos.left, ch.pos.bottom - ch.pos.top };
        g.pushAxisAlignedClip(local);
        ch.graphic->render(dc);
        g.popAxisAlignedClip();
    }

    gmpi::ReturnCode getClipArea(gmpi::drawing::Rect* returnRect) override
    {
        if (returnRect)
            *returnRect = bounds;
        return gmpi::ReturnCode::Ok;
    }

    // ---- IInputClient ----
    child* childAt(gmpi::drawing::Point p)
    {
        if (topChild.editor && pointInRect(p, topChild.pos))       return &topChild;
        if (bottomChild.editor && pointInRect(p, bottomChild.pos)) return &bottomChild;
        return nullptr;
    }
    static gmpi::drawing::Point toLocal(child* ch, gmpi::drawing::Point p) { return { p.x - ch->pos.left, p.y - ch->pos.top }; }

    gmpi::ReturnCode setHover(bool) override { return gmpi::ReturnCode::Ok; }

    gmpi::ReturnCode hitTest(gmpi::drawing::Point p, int32_t flags) override
    {
        auto ch = childAt(p);
        return (ch && ch->editor->hitTest(toLocal(ch, p), flags) == gmpi::ReturnCode::Ok) ? gmpi::ReturnCode::Ok : gmpi::ReturnCode::Unhandled;
    }
    gmpi::ReturnCode onPointerDown(gmpi::drawing::Point p, int32_t flags) override
    {
        auto ch = childAt(p);
        capturedChild = ch;
        return ch ? ch->editor->onPointerDown(toLocal(ch, p), flags) : gmpi::ReturnCode::Unhandled;
    }
    gmpi::ReturnCode onPointerMove(gmpi::drawing::Point p, int32_t flags) override
    {
        child* ch = capturedChild;
        if (!ch)
        {
            // A drag can retain LOGICAL capture on the editor (bottomChild) across a
            // mid-gesture button-up — e.g. a "pickup" cable drag: click a pin, RELEASE,
            // then the line follows the cursor with no button held until a second click.
            // capturedChild is only (re)set on pointer-DOWN, so once a button-up cleared
            // it we'd fall back to a position hit-test. But the OS-cursor poll that drives
            // auto-scroll synthesizes moves OUTSIDE the panel, where childAt() returns
            // null and the move would be dropped here — freezing auto-scroll. While the
            // host still reports capture, keep routing to the captured child (only the
            // editor takes capture in this chain) rather than hit-testing the position.
            bool held = false;
            if (inputHost) inputHost->getCapture(held);
            ch = held ? &bottomChild : childAt(p);
        }
        return (ch && ch->editor) ? ch->editor->onPointerMove(toLocal(ch, p), flags) : gmpi::ReturnCode::Unhandled;
    }
    gmpi::ReturnCode onPointerUp(gmpi::drawing::Point p, int32_t flags) override
    {
        child* ch = capturedChild ? capturedChild : childAt(p);
        const auto r = (ch && ch->editor) ? ch->editor->onPointerUp(toLocal(ch, p), flags) : gmpi::ReturnCode::Unhandled;
        // Keep our routing pointer in sync with the editor's LOGICAL capture rather than
        // clearing it on every raw button-up. A pickup drag sees a real button-up in the
        // middle of the gesture, yet the editor keeps capture across it, so we must too —
        // otherwise the next out-of-panel move can't find its child (see onPointerMove).
        // Checked AFTER forwarding, because the child releases capture from inside its own
        // onPointerUp on the genuine end-click.
        bool held = false;
        if (inputHost) inputHost->getCapture(held);
        if (!held)
            capturedChild = nullptr;
        return r;
    }
    gmpi::ReturnCode onMouseWheel(gmpi::drawing::Point p, int32_t flags, int32_t delta) override
    {
        auto ch = childAt(p);
        return (ch && ch->editor) ? ch->editor->onMouseWheel(toLocal(ch, p), flags, delta) : gmpi::ReturnCode::Unhandled;
    }
    gmpi::ReturnCode populateContextMenu(gmpi::drawing::Point p, gmpi::api::IUnknown* sink) override
    {
        auto ch = childAt(p);
        return (ch && ch->editor) ? ch->editor->populateContextMenu(toLocal(ch, p), sink) : gmpi::ReturnCode::Unhandled;
    }
    gmpi::ReturnCode onKeyPress(wchar_t c) override
    {
        return bottomChild.editor ? bottomChild.editor->onKeyPress(c) : gmpi::ReturnCode::Unhandled;
    }
    gmpi::ReturnCode getToolTip(gmpi::drawing::Point p, gmpi::api::IString* s) override
    {
        auto ch = childAt(p);
        return (ch && ch->editor) ? ch->editor->getToolTip(toLocal(ch, p), s) : gmpi::ReturnCode::Unhandled;
    }

    gmpi::ReturnCode queryInterface(const gmpi::api::Guid* iid, void** returnInterface) override
    {
        *returnInterface = {};
        GMPI_QUERYINTERFACE(gmpi::api::IInputClient);
        GMPI_QUERYINTERFACE(gmpi::api::IDrawingClient);
        GMPI_QUERYINTERFACE(gmpi::api::IGraphicsRedrawClient);
        return gmpi::ReturnCode::NoSupport;
    }
    GMPI_REFCOUNT;
};

} // namespace SE2
