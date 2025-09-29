#pragma once

#include <chrono>
#include <functional>
#include <d3d11_4.h>
#include <span>
#include "TimerManager.h"
#include "GraphicsRedrawClient.h"
#include "DirectXGfx.h"
#include "legacy_sdk_gui2.h"
#include "helpers/NativeUi.h"
#include "helpers/Timer.h"
#include "mp_gui.h"

// GMPI-UI to replace custom code.
#include "backends/DrawingFrameWin.h"

namespace SE2
{
    class IPresenter;
}

class CSynthEditDocBase;

struct UniversalFactory
{
    gmpi::directx::Factory gmpiFactory;
    se::directx::Factory_base sdk3Factory;

	UniversalFactory() : sdk3Factory(gmpiFactory.getInfo()) // SDK3 factory borrows the guts from the GMPI factory.
	{
	}
};

inline gmpi::drawing::Point fromLegacy(GmpiDrawing_API::MP1_POINT p)
{
    // MP1_POINT typically holds floats (or ints). Cast defensively.
    return { static_cast<float>(p.x), static_cast<float>(p.y) };
}
inline GmpiDrawing_API::MP1_POINT toLegacy(gmpi::drawing::Point p)
{
    return { p.x, p.y }; // Cast not needed if MP1_POINT fields are float. Add static_cast<type>(...) if they are int.
}

// SDK3 Graphics support on Direct2D. Used by SE2JUCE, VST3 and SynthEdit2::HostedView
// also provides a universal drawing factory for nested GMPI-UI plugins
struct DrawingFrameBase2 :
    public gmpi::hosting::tempSharedD2DBase,
    public gmpi_gui::legacy::IMpGraphicsHost,
    public gmpi::legacy::IMpUserInterfaceHost2,
    public gmpi::api::IDialogHost
{
    std::unique_ptr<UniversalFactory> DrawingFactory;

    gmpi_sdk::mp_shared_ptr<IGraphicsRedrawClient> frameUpdateClient;
    gmpi_sdk::mp_shared_ptr<gmpi_gui_api::IMpGraphics3> gmpi_gui_client; // usually a ContainerView at the topmost level
    gmpi_sdk::mp_shared_ptr<gmpi::IMpUserInterface2B> pluginParameters2B;

    std::atomic<bool> isInit;

    gmpi::drawing::Size scrollPos = {};
    float zoomFactor = 1.0f;

    GmpiDrawing_API::MP1_POINT currentPointerPos = { -1, -1 };
    //    std::chrono::time_point<std::chrono::steady_clock> frameCountTime;
    GmpiGui::PopupMenu contextMenu;

    DrawingFrameBase2();
    ~DrawingFrameBase2()
    {
        ReleaseDevice();
    }

    bool isMouseOver() const { return currentPointerPos.x >= 0 && currentPointerPos.y >= 0; }

    // override these please.
    void OnSwapChainCreated() override;

    virtual void autoScrollStart() {}
    virtual void autoScrollStop() {}

    void attachClient(gmpi_sdk::mp_shared_ptr<gmpi_gui_api::IMpGraphics3> gfx);
    void detachClient();
    void detachAndRecreate();
    void calcViewTransform();
    void sizeClientDips(float width, float height) override;

    virtual void OnPaint() = 0; // Derived should call Paint with the dirty area
    void Paint(const std::span<const gmpi::drawing::RectL> dirtyRects);

    virtual void Closed()
    {
        detachClient();

        DrawingFactory = {};
        ReleaseDevice();
    }

    // gmpi::api::IDrawingHost
    gmpi::ReturnCode getDrawingFactory(gmpi::api::IUnknown** returnFactory) override
    {
        *returnFactory = &DrawingFactory->gmpiFactory;
        return gmpi::ReturnCode::Ok;
    }

    // gmpi::api::IDialogHost
    gmpi::ReturnCode createTextEdit(const gmpi::drawing::Rect* r, gmpi::api::IUnknown** returnTextEdit) override
    {return gmpi::ReturnCode::NoSupport;}
    gmpi::ReturnCode createPopupMenu(const gmpi::drawing::Rect* r, gmpi::api::IUnknown** returnPopupMenu) override
    {return gmpi::ReturnCode::NoSupport;}
    gmpi::ReturnCode createKeyListener(const gmpi::drawing::Rect* r, gmpi::api::IUnknown** returnKeyListener) override;
    gmpi::ReturnCode createFileDialog(int32_t dialogType, gmpi::api::IUnknown** returnDialog) override
    {return gmpi::ReturnCode::NoSupport;}
    gmpi::ReturnCode createStockDialog(int32_t dialogType, gmpi::api::IUnknown** returnDialog) override
    {return gmpi::ReturnCode::NoSupport;}

    // IMpUserInterfaceHost2
    int32_t MP_STDCALL pinTransmit(int32_t pinId, int32_t size, const void* data, int32_t voice = 0) override
    {
        return gmpi::MP_FAIL;
    }
    int32_t MP_STDCALL createPinIterator(gmpi::IMpPinIterator** returnIterator) override
    {
        return gmpi::MP_FAIL;
    }
    int32_t MP_STDCALL getHandle(int32_t& returnValue) override
    {
        return gmpi::MP_FAIL;
    }
    int32_t MP_STDCALL sendMessageToAudio(int32_t id, int32_t size, const void* messageData) override
    {
        return gmpi::MP_FAIL;
    }
    int32_t MP_STDCALL ClearResourceUris() override
    {
        return gmpi::MP_FAIL;
    }
    int32_t MP_STDCALL RegisterResourceUri(const char* resourceName, const char* resourceType, gmpi::IString* returnString) override
    {
        return gmpi::MP_FAIL;
    }
    int32_t MP_STDCALL OpenUri(const char* fullUri, gmpi::IProtectedFile2** returnStream) override
    {
        return gmpi::MP_FAIL;
    }
    int32_t MP_STDCALL FindResourceU(const char* resourceName, const char* resourceType, gmpi::IString* returnString) override
    {
        return gmpi::MP_FAIL;
    }
    int32_t MP_STDCALL LoadPresetFile_DEPRECATED(const char* presetFilePath) override
    {
        return gmpi::MP_FAIL;
    }

    // IMpGraphicsHost
    int32_t MP_STDCALL GetDrawingFactory(GmpiDrawing_API::IMpFactory** returnFactory) override
    {
        return DrawingFactory->sdk3Factory.queryInterface(GmpiDrawing_API::SE_IID_FACTORY2_MPGUI, (void**)returnFactory);
    }

    void MP_STDCALL invalidateMeasure() override {}

    void OnScrolled(double x, double y, double zoom);

    int32_t MP_STDCALL queryInterface(const gmpi::MpGuid& iid, void** returnInterface) override
    {
        if (gmpi::MpGuidEqual(&iid, (const gmpi::MpGuid*)&gmpi::api::IDrawingHost::guid))
        {
            // important to cast to correct vtable (this has 2 vtables) before reinterpret cast
            *returnInterface = reinterpret_cast<void*>(static_cast<gmpi::api::IDrawingHost*>(this));
            addRef();
            return gmpi::MP_OK;
        }

        if (gmpi::MpGuidEqual(&iid, (const gmpi::MpGuid*)&gmpi::api::IDialogHost::guid))
        {
            // important to cast to correct vtable (this has 2 vtables) before reinterpret cast
            *returnInterface = reinterpret_cast<void*>(static_cast<gmpi::api::IDialogHost*>(this));
            addRef();
            return gmpi::MP_OK;
        }

        if (gmpi::MpGuidEqual(&iid, &gmpi::MP_IID_UI_HOST2))
        {
            // important to cast to correct vtable (this has 2 vtables) before reinterpret cast
            *returnInterface = reinterpret_cast<void*>(static_cast<gmpi::legacy::IMpUserInterfaceHost2*>(this));
            addRef();
            return gmpi::MP_OK;
        }

        if (gmpi::MpGuidEqual(&iid, &gmpi_gui::SE_IID_GRAPHICS_HOST) || gmpi::MpGuidEqual(&iid, &gmpi_gui::SE_IID_GRAPHICS_HOST_BASE) || gmpi::MpGuidEqual(&iid, &gmpi::MP_IID_UNKNOWN))
        {
            // important to cast to correct vtable (this has 2 vtables) before reinterpret cast
            *returnInterface = reinterpret_cast<void*>(static_cast<gmpi_gui::legacy::IMpGraphicsHost*>(this));
            addRef();
            return gmpi::MP_OK;
        }

        *returnInterface = 0;
        return gmpi::MP_NOSUPPORT;
    }
    gmpi::ReturnCode queryInterface(const gmpi::api::Guid* iid, void** returnInterface) override
    {
        *returnInterface = {};
        GMPI_QUERYINTERFACE(gmpi::api::IDrawingHost);
        GMPI_QUERYINTERFACE(gmpi::api::IDialogHost);
        GMPI_QUERYINTERFACE(gmpi_gui::legacy::IMpGraphicsHost);
        GMPI_QUERYINTERFACE(gmpi::legacy::IMpUserInterfaceHost2);
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
    bool toolTipShown = false;
    HWND tooltipWindow = {};
    static const int toolTiptimerInit = 40; // x/60 Hz
    int toolTiptimer = 0;
    std::wstring toolTipText;
    // Paint() uses Direct-2d which block on vsync. Therefore all invalid rects should be applied in one "hit", else windows message queue chokes calling WM_PAINT repeately and blocking on every rect.
	gmpi::hosting::UpdateRegionWinGdi updateRegion_native;
    std::vector<GmpiDrawing::RectL> backBufferDirtyRects;
    int pollHdrChangesCount = 100;

    void initTooltip();
    void HideToolTip();
    void ShowToolTip();
    void TooltipOnMouseActivity();

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

    // provids a default message handler. Note that some clients provide their own. e.g. MyFrameWndDirectX
    LRESULT WindowProc(
        HWND hwnd,
        UINT message,
        WPARAM wParam,
        LPARAM lParam);

    // IMpGraphicsHost (SDK3)
    void invalidateRect(const GmpiDrawing_API::MP1_RECT* invalidRect) override;

    int32_t setCapture() override;
    int32_t getCapture(int32_t& returnValue) override;
    int32_t releaseCapture() override;

    int32_t createPlatformMenu(GmpiDrawing_API::MP1_RECT* rect, gmpi_gui::IMpPlatformMenu** returnMenu) override;
    int32_t createPlatformTextEdit(GmpiDrawing_API::MP1_RECT* rect, gmpi_gui::IMpPlatformText** returnTextEdit) override;
    int32_t createFileDialog(int32_t dialogType, gmpi_gui::IMpFileDialog** returnFileDialog) override;
    int32_t createOkCancelDialog(int32_t dialogType, gmpi_gui::IMpOkCancelDialog** returnDialog) override;

    // tempSharedD2DBase
    float calcWhiteLevel() override;
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
