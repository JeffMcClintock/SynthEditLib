#include "pch.h"

#include "DrawingFrame2_win.h"
#include "Core/GmpiApiEditor.h"
#include "SDK3Adaptor.h"

// Windows 32

using namespace legacy_converters;

DrawingFrameBase2::DrawingFrameBase2()
{
    universalFactory = std::make_unique<UniversalFactory>();
}

// Dirty-rect helpers (queueDirtyRect / replaceDirtyRects / invalidateAll /
// preGraphicsRedraw / Paint, plus the get/has/clear accessors) are now inline
// on gmpi_ui's DxDrawingFrameBase. Dead PaintQueuedDirtyRects helper removed.

// attachClient(IUnknown*) inherited from DxDrawingFrameBase — Phase 4c-2 promoted
// the swap-chain-size-on-attach block out of SE.

// SDK3 overload — wraps an IMpGraphics3 client in SDK3Adaptor and delegates.
void DrawingFrameBase2::attachClient(gmpi_sdk::mp_shared_ptr<gmpi_gui_api::IMpGraphics3> gfx)
{
	auto wrapper = new SDK3Adaptor();
    gmpi::shared_ptr<gmpi::api::IUnknown> adaptor;
    adaptor.attach(static_cast<gmpi::api::IDrawingClient*>(wrapper));

    wrapper->attachClient(gfx.get());

    attachClient(adaptor.get());
}

void DrawingFrameHwndBase::close()
{
    // Mirror of open(): disconnect from the window proc before tearing down
    // resources so any messages delivered during DestroyWindow (WM_DESTROY,
    // WM_NCDESTROY, etc.) hit the nullptr guard in DrawingFrame2WindowProc
    // rather than dispatching through a dangling this pointer.
    gmpi::hosting::DetachHostingWindow(getWindowHandle());
    setWindowHandle(nullptr);
    Closed();
}

void DrawingFrameBase2::Closed()
{
    /* TODO
    if(auto* viewBase = dynamic_cast<SE2::ViewBase*>(drawingClient.get()))
    {
        viewBase->Unload();
    }
    */

    detachClient();
    universalFactory = {};
    ReleaseDevice();
}

// launchContextMenu replaced by inherited DxDrawingFrameBase::doContextMenu.

// detachClient is now inherited from DxDrawingFrameBase — same body
// (setHost(nullptr) notification + null out the smart pointers).

void DrawingFrameBase2::detachAndRecreate()
{
    assert(!reentrant); // do this async please.

    detachClient();
    CreateSwapPanel(universalFactory->gmpiFactory.getD2dFactory());
}

// IDialogHost methods (createTextEdit / createPopupMenu / createKeyListener /
// createFileDialog / createStockDialog) are inherited from gmpi_ui's
// DxDrawingFrameBase — same body (delegates to the shared
// gmpi::hosting::win32::createPlatform* factories). HostedView (WinUI3) still
// overrides for SwapChainPanel-native widgets.
//
// IInputHost (setCapture / getCapture / releaseCapture) is on tempSharedD2DBase.

// queryInterface is inherited from DxDrawingFrameBase (same IDrawingHost /
// IInputHost / IDialogHost handling, plus the parameterHost fallback).

// getRasterizationScale inherited from DxDrawingFrameBase — Phase 4c-5
// promoted pluginUIScale into the gmpi_ui base.

// createNativeSwapChain is inherited from DxDrawingFrameHwnd (CreateSwapChainForHwnd).

void DrawingFrameBase2::OnSwapChainCreated()
{
    const auto dpiScale = lowDpiMode ? 1.0f : getRasterizationScale();

    // used to synchronize scaling of the DirectX swap chain with its associated SwapChainPanel element
    DXGI_MATRIX_3X2_F scale{};
    scale._22 = scale._11 = 1.f / dpiScale;
    [[maybe_unused]] auto hr = swapChain->SetMatrixTransform(&scale);
}

// createPopupMenu inherited from DxDrawingFrameBase (same factory delegate).
// Tooltip forwarders inline on tempSharedD2DBase.

LRESULT CALLBACK DrawingFrame2WindowProc(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam)
{
    auto drawingFrame = (DrawingFrameHwndBase*)(LONG_PTR)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (drawingFrame)
    {
        return drawingFrame->WindowProc(hwnd, message, wParam, lParam);
    }

    return DefWindowProc(hwnd, message, wParam, lParam);
}

// WindowProc inherited from DxDrawingFrameHwnd — Phase 4c-3 promoted SE's
// defensive `if (inputClient)` guards and the WM_MOUSELEAVE setHover(false)
// notification into the gmpi_ui base, and fixed gmpi_ui's doContextMenu so
// it no longer re-fires onPointerDown.

void DrawingFrameHwndBase::open(void* pParentWnd, const gmpi::drawing::SizeL* overrideSize)
{
    RECT r{};
    if (overrideSize)
    {
        // size to document
        r.right = overrideSize->width;
        r.bottom = overrideSize->height;
    }
    else
    {
        // auto size to parent
        GetClientRect(parentWnd, &r);
    }

    parentWnd = (HWND)pParentWnd;
    const auto windowClass = gmpi::hosting::RegisterWindowsClass(getDllHandle(), DrawingFrame2WindowProc);
    const auto windowHandle = gmpi::hosting::CreateHostingWindow(getDllHandle(), windowClass, parentWnd, r, (LONG_PTR)static_cast<DrawingFrameHwndBase*>(this));

    if (windowHandle)
    {
		setWindowHandle(windowHandle);

        CreateSwapPanel(universalFactory->gmpiFactory.getD2dFactory());

        initTooltip();

        const auto scale = 1.0 / getRasterizationScale();

        sizeClientDips(
            static_cast<float>((r.right - r.left) * scale),
            static_cast<float>((r.bottom - r.top) * scale)
        );

        // starting Timer last to avoid first event getting 'in-between' other init events.
        startTimer(15); // 16.66 = 60Hz. 16ms timer seems to miss v-sync. Faster timers offer no improvement to framerate.

		isInit = true; // prevent any painting happening before we're ready.
    }
}

void DrawingFrameHwndBase::ReSize(int left, int top, int right, int bottom)
{
    const auto width = right - left;
    const auto height = bottom - top;

    if (d2dDeviceContext && (swapChainSize.width != width || swapChainSize.height != height))
    {
        SetWindowPos(
            getWindowHandle()
            , nullptr
            , 0
            , 0
            , width
            , height
            , SWP_NOZORDER
        );

		OnSize(width, height);
    }
}

// onTimer inherited from DxDrawingFrameHwnd — Phase 4c-5 promoted SE's
// HDR-white-level poll up there. The disabled tooltip-readyToShow body that
// was here is preserved in git history if anyone wants to revive it.

// OnPaint forwarder defined inline in the header — DxDrawingFrameHwnd::OnPaint()
// runs the GDI dirty-region body.

bool DrawingFrameBase2::canPaint(std::span<gmpi::drawing::RectL> dirtyRects)
{
    return isInit.load(std::memory_order_relaxed);
}

// preparePaint (the IsCurrent adapter-changed check) is now the default impl on
// gmpi::hosting::tempSharedD2DBase — promoted up from here so gmpi_ui's
// DxDrawingFrameBase gains the same behaviour.

// renderFrame is inherited from DxDrawingFrameBase (a thin wrapper around
// renderInDeviceContext below).
void DrawingFrameBase2::renderInDeviceContext(ID2D1DeviceContext* deviceContext, std::span<gmpi::drawing::RectL> dirtyRects)
{
    // UniversalGraphicsContext dispatches both GMPI and SDK3 IIDs in
    // queryInterface — that's why SE overrides this hook. The dirty-rect loop
    // (tempSharedD2DBase::paintLoop) is shared with gmpi_ui.
    se::directx::UniversalGraphicsContext context(universalFactory.get(), deviceContext);
    gmpi::drawing::Graphics graphics(&context);

    graphics.beginDraw();
    paintLoop(&context, dirtyRects, drawingClient.get());
    if (graphics.endDraw() != gmpi::ReturnCode::Ok)
    {
        ReleaseDevice();
    }
}

void DrawingFrameBase2::sizeClientDips(float width, float height)
{
    if (drawingClient)
    {
        gmpi::drawing::Size available{ width, height };
        gmpi::drawing::Size desired{};

        drawingClient->measure(&available, &desired);

		gmpi::drawing::Rect finalRect{ 0, 0, width, height };
        drawingClient->arrange(&finalRect);
    }
}

// invalidateRect inherited from DxDrawingFrameBase — same body now that the
// dirty-rect helpers are promoted (Phase 4c-1).

// getFullDirtyRect is inherited from DxDrawingFrameHwnd (identical
// GetClientRect-based body). Single virtual subobject via the virtual base.

