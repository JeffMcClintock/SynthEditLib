#pragma once

#include <d3d11_4.h>
#include <span>
#include "GraphicsRedrawClient.h"
#include "DirectXGfx.h"
#include "legacy_sdk_gui2.h"
#include "helpers/NativeUi.h"
#include "helpers/Timer.h"
#include "mp_gui.h"
#include "gmpi_drawing_conversions.h"

// GMPI-UI to replace custom code.
#include "backends/DrawingFrameWin.h"

using namespace legacy_converters;

namespace SE2
{
    class IPresenter;
}

class CSynthEditDocBase;

// Wraps a gmpi::directx::Factory so SDK3 plugins (which query for
// SE_IID_FACTORY*_MPGUI) can be served alongside modern GMPI plugins.
// Owns its gmpi factory by value — DrawingFrameBase2's inherited DrawingFactory
// from DxDrawingFrameBase is therefore unused on the SE path; SE methods route
// through universalFactory->gmpiFactory consistently. Phase 4b may collapse
// this to use the inherited factory once we're confident in the dispatch.
//
// ScreenshotFactory_win.cpp also creates these standalone (no enclosing frame),
// hence the default constructor.
struct UniversalFactory : public gmpi::api::IUnknown
{
    gmpi::directx::Factory gmpiFactory;
    se::directx::Factory_base sdk3Factory;

	UniversalFactory() : sdk3Factory(gmpiFactory.getInfo()) // SDK3 factory borrows the guts from the GMPI factory.
	{
	}

    // dispatch queries to correct factory
    gmpi::ReturnCode queryInterface(const gmpi::api::Guid* iid, void** returnInterface) override
    {
        if (*iid == *(const gmpi::api::Guid*)& GmpiDrawing_API::SE_IID_FACTORY2_MPGUI || *iid == *(const gmpi::api::Guid*)& GmpiDrawing_API::SE_IID_FACTORY_MPGUI)
        {
			return (gmpi::ReturnCode) sdk3Factory.queryInterface(* (const gmpi::MpGuid*) iid, returnInterface);
        }

		return gmpiFactory.queryInterface(iid, returnInterface);
    }

    GMPI_REFCOUNT_NO_DELETE;
};

// SDK3-aware editor host on top of gmpi_ui's DxDrawingFrameBase. Adds:
//  - UniversalFactory wrapping the inherited DrawingFactory so SDK3 plugins
//    that QI for SE_IID_FACTORY*_MPGUI work alongside modern GMPI plugins.
//  - SDK3-flavoured renderInDeviceContext (UniversalGraphicsContext).
//  - The legacy attachClient(IMpGraphics3) overload (live caller path —
//    SE2JUCE_Editor.cpp, Pile.h, SynthEditCocoaView.mm).
//  - pluginUIScale (HC_PLUGIN_UI_SCALE plugin-only multiplier).
//  - Closed() lifecycle for SE-specific teardown.
//  - isInit gating for canPaint.
//
// Virtual inheritance of DxDrawingFrameBase resolves the diamond formed by
// DrawingFrameHwndBase below (which also inherits DxDrawingFrameHwnd, itself
// virtually deriving DxDrawingFrameBase). Used by SE2JUCE, VST3 and HostedView.
//
// Phase 4a: graphics_gmpi/editor_gmpi/dirtyRects members deliberately kept
// alongside the inherited drawingClient/inputClient/backBufferDirtyRects.
// State collapse comes in Phase 4b.
struct DrawingFrameBase2 :
      public virtual gmpi::hosting::DxDrawingFrameBase
    // IDialogHost reaches us via DxDrawingFrameBase. Listing it again here would
    // create a duplicate base subobject (warning C4584).
{
    std::unique_ptr<UniversalFactory> universalFactory; // wraps inherited DrawingFactory; serves SDK3 IIDs

    gmpi::shared_ptr<gmpi::api::IDrawingClient> graphics_gmpi;
    gmpi::shared_ptr<gmpi::api::IInputClient> editor_gmpi;
    gmpi::shared_ptr<gmpi::api::IGraphicsRedrawClient> frameUpdateClient;

    std::atomic<bool> isInit;

    float pluginUIScale = 1.0f; // HC_PLUGIN_UI_SCALE multiplier (plugin builds only, not the editor)

    gmpi::drawing::Point currentPointerPos{ -1, -1 };
    GmpiGui::PopupMenu contextMenu;
    gmpi::shared_ptr<gmpi::api::IPopupMenu> popupMenu;

protected:
    gmpi::hosting::DirtyRectQueue dirtyRects;

    void queueDirtyRect(gmpi::drawing::RectL rect);
    void queueDirtyRect(const gmpi::drawing::Rect* invalidRect);
    void replaceDirtyRects(gmpi::drawing::RectL rect);
    void invalidateAll();
    // getFullDirtyRect already declared pure virtual on tempSharedD2DBase
    // (via DxDrawingFrameBase). Re-declaring here would split the virtual into
    // two override slots and make the diamond ambiguous (C2250).
    static int32_t makePointerFlags()
    {
        return static_cast<int32_t>(gmpi::api::PointerFlags::InContact)
             | static_cast<int32_t>(gmpi::api::PointerFlags::Primary)
             | static_cast<int32_t>(gmpi::api::PointerFlags::Confidence);
    }
    static void addPointerButtonFlags(int32_t& flags, bool firstButton, bool secondButton, bool thirdButton = false)
    {
        if (firstButton)
            flags |= static_cast<int32_t>(gmpi::api::PointerFlags::FirstButton);
        if (secondButton)
            flags |= static_cast<int32_t>(gmpi::api::PointerFlags::SecondButton);
        if (thirdButton)
            flags |= static_cast<int32_t>(gmpi::api::PointerFlags::ThirdButton);
    }
    static void addPointerKeyFlags(int32_t& flags, bool shift, bool control, bool alt)
    {
        if (shift)
            flags |= static_cast<int32_t>(gmpi::api::PointerFlags::KeyShift);
        if (control)
            flags |= static_cast<int32_t>(gmpi::api::PointerFlags::KeyControl);
        if (alt)
            flags |= static_cast<int32_t>(gmpi::api::PointerFlags::KeyAlt);
    }
    static void addPointerNewFlag(int32_t& flags, bool isNew)
    {
        if (isNew)
            flags |= static_cast<int32_t>(gmpi::api::PointerFlags::New);
    }
    std::span<gmpi::drawing::RectL> getDirtyRects() { return dirtyRects.get(); }
    std::span<const gmpi::drawing::RectL> getDirtyRects() const { return dirtyRects.get(); }
    bool hasDirtyRects() const { return !dirtyRects.empty(); }
    void clearDirtyRects() { dirtyRects.clear(); }
    bool preGraphicsRedraw();
    void PaintQueuedDirtyRects();
    gmpi::ReturnCode launchContextMenu(const gmpi::drawing::Point& point);

public:

    DrawingFrameBase2();
    ~DrawingFrameBase2()
    {
        // ReleaseDevice runs in DxDrawingFrameBase dtor too — dropping it here
        // (it's idempotent but redundant).
    }

    bool isMouseOver() const { return currentPointerPos.x >= 0 && currentPointerPos.y >= 0; }

    void OnSwapChainCreated() override;

    // Override gmpi_ui's getD2dFactory to use universalFactory's gmpi factory.
    // (The inherited DrawingFactory from DxDrawingFrameBase is unused on the SE
    // path — Phase 4b may collapse them to share one instance.)
    ID2D1Factory1* getD2dFactory() override
    {
        return universalFactory ? universalFactory->gmpiFactory.getD2dFactory() : nullptr;
    }
    // canPaint gates on isInit (set true at end of open() after startTimer)
    // rather than gmpi_ui's "drawingClient != nullptr" check, so the first
    // frame after open is suppressed until the timer is wired up.
    bool canPaint(std::span<gmpi::drawing::RectL> dirtyRects) override;
    // preparePaint inherits the tempSharedD2DBase default (IsCurrent adapter check).
    bool recreateDeviceOnPaint() const override
    {
        return true;
    }
    // Render-context customisation point: build a UniversalGraphicsContext
    // (which dispatches both GMPI and SDK3 IIDs) instead of gmpi_ui's plain
    // GraphicsContext, then run the shared paintLoop.
    void renderInDeviceContext(ID2D1DeviceContext* deviceContext, std::span<gmpi::drawing::RectL> dirtyRects) override;

    // attachClient(IUnknown*) inherits from DxDrawingFrameBase but is shadowed
    // by SE's override below, which writes to graphics_gmpi/editor_gmpi (Phase 4a
    // duplicate state) and adds the swap-chain-size-on-attach behaviour SE has
    // historically relied on.
    void attachClient(gmpi::api::IUnknown* pclient);
    // SDK3 overload — wraps an IMpGraphics3 client in SDK3Adaptor and delegates.
    // Live callers: SE2JUCE_Editor.cpp:106, Pile.h:577.
    void attachClient(gmpi_sdk::mp_shared_ptr<gmpi_gui_api::IMpGraphics3> gfx);
    void detachClient();
    void detachAndRecreate();
    void sizeClientDips(float width, float height) override;

    virtual void OnPaint() = 0; // Derived should call Paint with the dirty area
    void Paint(std::span<gmpi::drawing::RectL> dirtyRects);

    virtual void Closed();

    // gmpi::api::IDrawingHost — return the UniversalFactory so SDK3 plugins
    // querying for SE_IID_FACTORY*_MPGUI hit the SDK3 dispatch path.
    gmpi::ReturnCode getDrawingFactory(gmpi::api::IUnknown** returnFactory) override
    {
        *returnFactory = universalFactory.get();
        return gmpi::ReturnCode::Ok;
    }

    // IInputHost: inline impls on tempSharedD2DBase (via DxDrawingFrameBase).
    // IDialogHost: createTextEdit/PopupMenu/KeyListener/FileDialog/StockDialog
    //              all inherited from DxDrawingFrameBase — same body (delegate
    //              to gmpi::hosting::win32::createPlatform*). HostedView still
    //              overrides for SwapChainPanel-native widgets.
    // queryInterface: inherited from DxDrawingFrameBase (handles IDrawingHost,
    //                 IInputHost, IDialogHost, parameterHost fallback).
    // GMPI_REFCOUNT_NO_DELETE: inherited.
};

// HWND-coupled SE editor frame. Diamond: inherits HWND machinery from
// DxDrawingFrameHwnd (gmpi_ui) and SDK3 dispatch from DrawingFrameBase2.
// Both base classes virtually inherit DxDrawingFrameBase, so the diamond
// resolves to a single subobject.
//
// Phase 4a — many of SE's HWND-side methods are kept because their bodies
// genuinely differ from gmpi_ui's: SE's WindowProc has defensive
// "if (editor_gmpi)" guards at every dispatch site; OnPaint calls Paint()
// (which routes through SE's dirty-rect helpers); invalidateRect writes to
// SE's `dirtyRects` queue (still distinct from inherited backBufferDirtyRects
// in 4a); getRasterizationScale factors in pluginUIScale. Phase 4b unifies
// the dirty-rect queues, which lets several of these collapse.
//
// Used by SE2JUCE and SE VST3.
class DrawingFrameHwndBase :
      public gmpi::hosting::DxDrawingFrameHwnd
    , public DrawingFrameBase2
{
protected:
    HWND parentWnd = {};
    int pollHdrChangesCount = 100;

    // cubaseBugPreviousMouseMove, isTrackingMouse, updateRegion_native,
    // createNativeSwapChain, getFullDirtyRect — all inherited from
    // DxDrawingFrameHwnd (single subobject via virtual inheritance).

public:
    void open(void* pParentWnd, const GmpiDrawing_API::MP1_SIZE_L* overrideSize = {});
	virtual void setWindowHandle(HWND hwnd) = 0; // provides the new hwnd to the derived class

    // IMpGraphicsHost — SE's invalidateRect routes through SE's dirtyRects
    // queue (queueDirtyRect/invalidateAll). Different queue from gmpi_ui's
    // backBufferDirtyRects; collapses in Phase 4b.
    void invalidateRect(const gmpi::drawing::Rect* invalidRect) override;
    // Adds pluginUIScale on top of gmpi_ui's GetDpiForWindow result.
    float getRasterizationScale() override;

    void ReSize(int left, int top, int right, int bottom);
    virtual void DoClose() {}

    // Drains SE's dirtyRects queue + adds HDR-white-level poll for monitor
    // mode changes (e.g. HDR toggle).
    bool onTimer() override;

    // Pure virtual on DrawingFrameBase2; SE's body uses Paint() which routes
    // through the dirty-rect helpers.
    void OnPaint() override;

    // Default message handler. Note that some clients provide their own
    // (e.g. MyFrameWndDirectX wraps it). SE's body has defensive
    // "if (editor_gmpi)" guards at every dispatch site that gmpi_ui's lacks.
    LRESULT WindowProc(
        HWND hwnd,
        UINT message,
        WPARAM wParam,
        LPARAM lParam);
};

// This is used in VST3. Native HWND window frame, owned by this.
class DrawingFrame2 : public DrawingFrameHwndBase
{
    HWND windowHandle = {};

public:
    void setWindowHandle(HWND hwnd) override
    {
		windowHandle = hwnd;
    }
    HWND getWindowHandle() override
    {
        return windowHandle;
    }
};
