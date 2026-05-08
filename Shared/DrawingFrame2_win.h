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

// SDK3 Graphics support on Direct2D. Used by SE2JUCE, VST3 and SynthEdit2::HostedView
// also provides a universal drawing factory for nested GMPI-UI plugins
struct DrawingFrameBase2 :
      public gmpi::hosting::tempSharedD2DBase
    , public gmpi::api::IDialogHost
    // IInputHost inherited via tempSharedD2DBase (HWND-pointer-capture impls live there).
{
    std::unique_ptr<UniversalFactory> DrawingFactory;

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
    virtual gmpi::drawing::RectL getFullDirtyRect() = 0;
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
        ReleaseDevice();
    }

    bool isMouseOver() const { return currentPointerPos.x >= 0 && currentPointerPos.y >= 0; }

    // override these please.
    void OnSwapChainCreated() override;
    ID2D1Factory1* getD2dFactory() override
    {
        return DrawingFactory ? DrawingFactory->gmpiFactory.getD2dFactory() : nullptr;
    }
    bool canPaint(std::span<gmpi::drawing::RectL> dirtyRects) override;
    bool preparePaint(std::span<gmpi::drawing::RectL> dirtyRects) override;
    bool recreateDeviceOnPaint() const override
    {
        return true;
    }
    void renderFrame(ID2D1DeviceContext* deviceContext, std::span<gmpi::drawing::RectL> dirtyRects) override;

    virtual void autoScrollStart() {}
    virtual void autoScrollStop() {}

    void attachClient(gmpi::api::IUnknown* pclient);
    void attachClient(gmpi_sdk::mp_shared_ptr<gmpi_gui_api::IMpGraphics3> gfx);
    void detachClient();
    void detachAndRecreate();
    void sizeClientDips(float width, float height) override;

    virtual void OnPaint() = 0; // Derived should call Paint with the dirty area
    void Paint(std::span<gmpi::drawing::RectL> dirtyRects);

    virtual void Closed();

    // gmpi::api::IDrawingHost
    gmpi::ReturnCode getDrawingFactory(gmpi::api::IUnknown** returnFactory) override
    {
        *returnFactory = DrawingFactory.get();
        return gmpi::ReturnCode::Ok;
    }

    // IInputHost — implemented inline on tempSharedD2DBase.

	// gmpi::api::IDialogHost. All Win32 implementations live in DrawingFrame2_win.cpp
	// and delegate to the shared gmpi::hosting::win32::createPlatform* factories,
	// which are also used by gmpi_ui's DxDrawingFrameBase. HostedView (WinUI3)
	// overrides these for SwapChainPanel-native widgets.
    gmpi::ReturnCode createTextEdit(const gmpi::drawing::Rect* r, gmpi::api::IUnknown** returnTextEdit) override;
    gmpi::ReturnCode createPopupMenu(const gmpi::drawing::Rect* r, gmpi::api::IUnknown** returnPopupMenu) override;
    gmpi::ReturnCode createKeyListener(const gmpi::drawing::Rect* r, gmpi::api::IUnknown** returnKeyListener) override;
    // Both delegate to gmpi::hosting::win32::createPlatform{File,Stock}Dialog —
    // shared factories also used by gmpi_ui's DxDrawingFrameBase.
    gmpi::ReturnCode createFileDialog(int32_t dialogType, gmpi::api::IUnknown** returnDialog) override;
    // returns IStockDialog
    gmpi::ReturnCode createStockDialog(int32_t dialogType, const char* title, const char* text, gmpi::api::IUnknown** returnDialog) override;

    gmpi::ReturnCode queryInterface(const gmpi::api::Guid* iid, void** returnInterface) override
    {
        *returnInterface = {};
        GMPI_QUERYINTERFACE(gmpi::api::IDrawingHost);
        GMPI_QUERYINTERFACE(gmpi::api::IDialogHost);
        GMPI_QUERYINTERFACE(gmpi::api::IInputHost);
        return gmpi::ReturnCode::NoSupport;
    }

    GMPI_REFCOUNT_NO_DELETE;
};

// Used in SE2JUCE, SE VST3
class DrawingFrameHwndBase : public DrawingFrameBase2, public gmpi::TimerClient
{
protected:
    HWND parentWnd = {};
    gmpi::drawing::Point cubaseBugPreviousMouseMove = { -1,-1 };
    bool isTrackingMouse = false;
    // `tooltip` member + initTooltip/HideToolTip/ShowToolTip/TooltipOnMouseActivity
    // now live on tempSharedD2DBase.
    // Paint() uses Direct-2d which block on vsync. Therefore all invalid rects should be applied in one "hit", else windows message queue chokes calling WM_PAINT repeately and blocking on every rect.
	gmpi::hosting::UpdateRegionWinGdi updateRegion_native;
    int pollHdrChangesCount = 100;

    gmpi::drawing::RectL getFullDirtyRect() override;

public:
    void open(void* pParentWnd, const GmpiDrawing_API::MP1_SIZE_L* overrideSize = {});
	virtual void setWindowHandle(HWND hwnd) = 0; // provides the new hwnd to the derived class

    HRESULT createNativeSwapChain
    (
		IDXGIFactory2* factory,
        ID3D11Device* d3dDevice,
        DXGI_SWAP_CHAIN_DESC1* desc,
        IDXGISwapChain1** returnSwapChain
    ) override;

    // IMpGraphicsHost
    void invalidateRect(const gmpi::drawing::Rect* invalidRect) override;
    float getRasterizationScale() override;

    void ReSize(int left, int top, int right, int bottom);
    virtual void DoClose() {}
    bool onTimer() override;
    void OnPaint() override; // should call Paint with the dirty area

    // (createPopupMenu now lives on DrawingFrameBase2 — shared Win32 impl.)

    // provids a default message handler. Note that some clients provide their own. e.g. MyFrameWndDirectX
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
