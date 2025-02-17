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

// GMPI-UI to replace custom code.
#include "backends/DrawingFrameWin.h"

namespace SE2
{
    class IPresenter;
}

class CSynthEditDocBase;

// utilities for working with legacy graphics api.
inline GmpiDrawing::Rect toLegacy(gmpi::drawing::Rect r)
{
    return { r.left, r.top, r.right, r.bottom };
}
inline GmpiDrawing::Matrix3x2 toLegacy(gmpi::drawing::Matrix3x2 m)
{
    return { m._11, m._12, m._21, m._22, m._31, m._32 };
}
inline gmpi::drawing::Rect fromLegacy(GmpiDrawing_API::MP1_RECT r)
{
    return { r.left, r.top, r.right, r.bottom };
}
// mixing new matrix with old rect (for convinience)
inline GmpiDrawing_API::MP1_RECT operator*(GmpiDrawing_API::MP1_RECT rect, gmpi::drawing::Matrix3x2 transform)
{
    return {
        rect.left * transform._11 + rect.top * transform._21 + transform._31,
        rect.left * transform._12 + rect.top * transform._22 + transform._32,
        rect.right * transform._11 + rect.bottom * transform._21 + transform._31,
        rect.right * transform._12 + rect.bottom * transform._22 + transform._32
    };
}


class UniversalFactory : public gmpi::directx::Factory
{
    se::directx::Factory_base sdk3Factory;

public:

	UniversalFactory() : gmpi::directx::Factory(nullptr), sdk3Factory(info, (gmpi::IMpUnknown*) static_cast<gmpi::api::IUnknown*>(this))
	{
	}

    gmpi::ReturnCode queryInterface(const gmpi::api::Guid* iid, void** returnInterface) override
    {
        *returnInterface = {};
        if (*iid == gmpi::drawing::api::IFactory::guid)
        {
            return gmpi::directx::Factory_base::queryInterface(iid, returnInterface);
        }
        if (
            *iid == *reinterpret_cast<const gmpi::api::Guid*>(&GmpiDrawing_API::SE_IID_FACTORY2_MPGUI) ||
            *iid == *reinterpret_cast<const gmpi::api::Guid*>(&GmpiDrawing_API::SE_IID_FACTORY_MPGUI)
            )
        {
            return (gmpi::ReturnCode)sdk3Factory.queryInterface(*reinterpret_cast<const gmpi::MpGuid*>(iid), returnInterface);
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

// SDK3 Graphics support on Direct2D. Used by SE2JUCE, VST3 and SynthEdit2::HostedView
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
    float zoomFactor = {};

    GmpiDrawing_API::MP1_POINT currentPointerPos = {-1, -1};
//    std::chrono::time_point<std::chrono::steady_clock> frameCountTime;
    GmpiGui::PopupMenu contextMenu;

    bool isMouseOver() const { return currentPointerPos.x >= 0 && currentPointerPos.y >= 0; }

    // override these please.
    virtual float getRasterizationScale() = 0; // DPI scaling
    virtual HRESULT createNativeSwapChain
    (
        IDXGIFactory2* factory,
        ID3D11Device* d3dDevice,
        DXGI_SWAP_CHAIN_DESC1* desc,
        IDXGISwapChain1** returnSwapChain
    ) = 0;

    virtual void autoScrollStart() {}
    virtual void autoScrollStop() {}

    void CreateSwapPanel();

    void attachClient(gmpi_sdk::mp_shared_ptr<gmpi_gui_api::IMpGraphics3> gfx);
    void detachClient();
    void calcViewTransform();

    virtual void OnPaint() = 0; // Derived should call Paint with the dirty area
    void Paint(const std::span<const gmpi::drawing::RectL> dirtyRects);

    virtual void Closed()
    {
        detachClient();

        DrawingFactory = {};
        d2dDeviceContext = {};
        swapChain = {};
    }

    // gmpi::api::IDialogHost
    gmpi::ReturnCode createTextEdit(gmpi::api::IUnknown** returnTextEdit) override
    {
		return gmpi::ReturnCode::NoSupport;
	}
    gmpi::ReturnCode createKeyListener(gmpi::api::IUnknown** returnKeyListener) override;
    gmpi::ReturnCode createPopupMenu(gmpi::api::IUnknown** returnPopupMenu) override
    {
        return gmpi::ReturnCode::NoSupport;
    }
    gmpi::ReturnCode createFileDialog(int32_t dialogType, gmpi::api::IUnknown** returnDialog) override
    {
        return gmpi::ReturnCode::NoSupport;
    }
    gmpi::ReturnCode createStockDialog(int32_t dialogType, gmpi::api::IUnknown** returnDialog) override
    {
        return gmpi::ReturnCode::NoSupport;
    }

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
        return (int32_t) DrawingFactory->queryInterface(reinterpret_cast<const gmpi::api::Guid*>(&GmpiDrawing_API::SE_IID_FACTORY2_MPGUI), (void**)returnFactory);
    }

    void MP_STDCALL invalidateMeasure() override {}

    void OnScrolled(double x, double y, double zoom);

    int32_t MP_STDCALL queryInterface(const gmpi::MpGuid& iid, void** returnInterface) override
    {
        if (gmpi::MpGuidEqual(&iid, (const gmpi::MpGuid*) &gmpi::api::IDialogHost::guid))
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
        GMPI_QUERYINTERFACE(gmpi::api::IDialogHost);
        GMPI_QUERYINTERFACE(gmpi_gui::legacy::IMpGraphicsHost);
        GMPI_QUERYINTERFACE(gmpi::legacy::IMpUserInterfaceHost2);
        return gmpi::ReturnCode::NoSupport;
    }

    GMPI_REFCOUNT_NO_DELETE;
};

// Used in SE2JUCE
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

    void initTooltip();
    void HideToolTip();
    void ShowToolTip();
    void TooltipOnMouseActivity();
    void OnSize(UINT width, UINT height);

public:
    void open(void* pParentWnd, const GmpiDrawing_API::MP1_SIZE_L* overrideSize = {});
	virtual void setWindowHandle(HWND hwnd) = 0; // privides the new hwnd to the derived class

    HRESULT createNativeSwapChain
    (
		IDXGIFactory2* factory,
        ID3D11Device* d3dDevice,
        DXGI_SWAP_CHAIN_DESC1* desc,
        IDXGISwapChain1** returnSwapChain
    ) override;

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
