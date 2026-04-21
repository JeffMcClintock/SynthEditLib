#include "pch.h"

#include "DrawingFrame2_win.h"
#include "Core/GmpiApiEditor.h"
#include "SDK3Adaptor.h"

// Windows 32

using namespace legacy_converters;

DrawingFrameBase2::DrawingFrameBase2()
{
    DrawingFactory = std::make_unique<UniversalFactory>();
}

void DrawingFrameBase2::queueDirtyRect(gmpi::drawing::RectL rect)
{
    dirtyRects.add(rect);
}

void DrawingFrameBase2::queueDirtyRect(const gmpi::drawing::Rect* invalidRect)
{
    dirtyRects.add(invalidRect, DipsToWindow);
}

void DrawingFrameBase2::replaceDirtyRects(gmpi::drawing::RectL rect)
{
    dirtyRects.replace(rect);
}

void DrawingFrameBase2::invalidateAll()
{
    replaceDirtyRects(getFullDirtyRect());
}

bool DrawingFrameBase2::preGraphicsRedraw()
{
    if (frameUpdateClient)
    {
        frameUpdateClient->preGraphicsRedraw();
    }

    return hasDirtyRects();
}

void DrawingFrameBase2::PaintQueuedDirtyRects()
{
    if (!hasDirtyRects())
        return;

    Paint(getDirtyRects());
    clearDirtyRects();
}

// new
void DrawingFrameBase2::attachClient(gmpi::api::IUnknown* pclient)
{
    detachClient();

    gmpi::shared_ptr<gmpi::api::IUnknown> unknown; // no, does not increment refcount (pclient);
    unknown = pclient;

    editor_gmpi       = unknown.as<gmpi::api::IInputClient>();
    graphics_gmpi     = unknown.as<gmpi::api::IDrawingClient>();
	frameUpdateClient = unknown.as<gmpi::api::IGraphicsRedrawClient>();

#if 0 // TODO
    gmpi_gui_client = gfx;

    gfx->queryInterface(IGraphicsRedrawClient::guid, frameUpdateClient.asIMpUnknownPtr());
#endif

    auto ieditor = unknown.as<gmpi::api::IEditor>();
    if(ieditor)
    {
        ieditor->setHost(static_cast<gmpi::api::IDrawingHost*>(this));
        ieditor->initialize();
    }

    if (swapChain)
    {
        const auto scale = 1.0 / getRasterizationScale();

        sizeClientDips(
            static_cast<float>(swapChainSize.width) * scale,
            static_cast<float>(swapChainSize.height) * scale
        );
    }

    if (graphics_gmpi)
        graphics_gmpi->open(static_cast<gmpi::api::IDrawingHost*>(this));
}

// old
void DrawingFrameBase2::attachClient(gmpi_sdk::mp_shared_ptr<gmpi_gui_api::IMpGraphics3> gfx)
{
	auto wrapper = new SDK3Adaptor();
    gmpi::shared_ptr<gmpi::api::IUnknown> adaptor;
    adaptor.attach(static_cast<gmpi::api::IDrawingClient*>(wrapper));

    wrapper->attachClient(gfx.get());

    attachClient(adaptor.get());
}

void DrawingFrameBase2::Closed()
{
    /* TODO
    if(auto* viewBase = dynamic_cast<SE2::ViewBase*>(graphics_gmpi.get()))
    {
        viewBase->Unload();
    }
    */

    popupMenu = {};
    detachClient();
    DrawingFactory = {};
    ReleaseDevice();
}

gmpi::ReturnCode DrawingFrameBase2::launchContextMenu(const gmpi::drawing::Point& point)
{
    if (!editor_gmpi)
        return gmpi::ReturnCode::Unhandled;

    gmpi::drawing::Rect rect(point.x, point.y, point.x + 120, point.y + 20);

    gmpi::shared_ptr<gmpi::api::IUnknown> unknown;
    if (createPopupMenu(&rect, unknown.put()) != gmpi::ReturnCode::Ok)
        return gmpi::ReturnCode::NoSupport;

    popupMenu = unknown.as<gmpi::api::IPopupMenu>();
    if (!popupMenu)
        return gmpi::ReturnCode::NoSupport;

    const auto returnCode = editor_gmpi->populateContextMenu(point, popupMenu);
    popupMenu->showAsync(nullptr);
    return returnCode;
}

void DrawingFrameBase2::detachClient()
{
    // Notify the client that the host (drawing surface) is going away.
    // Without this, ViewBase::drawingHost holds a dangling pointer after the
    // window closes, and any in-flight DSP→GUI invalidation will crash.
    if (auto ieditor = graphics_gmpi.as<gmpi::api::IEditor>())
        ieditor->setHost(nullptr);

    graphics_gmpi = {};
    editor_gmpi = {};
    frameUpdateClient = {};
}

void DrawingFrameBase2::detachAndRecreate()
{
    assert(!reentrant); // do this async please.

    detachClient();
    CreateSwapPanel(DrawingFactory->gmpiFactory.getD2dFactory());
}

gmpi::ReturnCode DrawingFrameBase2::createKeyListener(const gmpi::drawing::Rect* r, gmpi::api::IUnknown** returnKeyListener)
{
    *returnKeyListener = new gmpi::hosting::win32::PlatformKeyListener(getWindowHandle(), r);
    return gmpi::ReturnCode::Ok;
}

// IInputHost
gmpi::ReturnCode DrawingFrameBase2::setCapture()
{
    ::SetCapture(getWindowHandle());
    return gmpi::ReturnCode::Ok;
}

gmpi::ReturnCode DrawingFrameBase2::getCapture(bool& returnValue)
{
    returnValue = ::GetCapture() == getWindowHandle();
    return gmpi::ReturnCode::Ok;
}

gmpi::ReturnCode DrawingFrameBase2::releaseCapture()
{
    ::ReleaseCapture();
    return gmpi::ReturnCode::Ok;
}

float DrawingFrameHwndBase::getRasterizationScale()
{
    const auto dpiX = GetDpiForWindow(getWindowHandle());
    return (dpiX / 96.f) * pluginUIScale;
}

HRESULT DrawingFrameHwndBase::createNativeSwapChain
(
    IDXGIFactory2* factory,
    ID3D11Device* d3dDevice,
    DXGI_SWAP_CHAIN_DESC1* desc,
    IDXGISwapChain1** returnSwapChain
)
{
    return factory->CreateSwapChainForHwnd(
        d3dDevice,
        getWindowHandle(),
        desc,
        nullptr,
        nullptr,
        returnSwapChain
    );
}

void DrawingFrameBase2::OnSwapChainCreated()
{
    const auto dpiScale = lowDpiMode ? 1.0f : getRasterizationScale();

    // used to synchronize scaling of the DirectX swap chain with its associated SwapChainPanel element
    DXGI_MATRIX_3X2_F scale{};
    scale._22 = scale._11 = 1.f / dpiScale;
    [[maybe_unused]] auto hr = swapChain->SetMatrixTransform(&scale);
}

class Win32PopupMenu : public gmpi::api::IPopupMenu
{
    HMENU hmenu;
    std::vector<HMENU> hmenus;
    HWND parentWnd;
    int align = TPM_LEFTALIGN;
    gmpi::drawing::Rect editrect{};
    int32_t selectedId = -1;
    std::vector<int32_t> menuIds;
    std::vector<gmpi::shared_ptr<gmpi::api::IUnknown>> itemCallbacks;

    // Shared HMENU logic. Flag values are binary-compatible between old and new API.
    void addItemCore(const char* text, int32_t id, int32_t flags)
    {
        UINT nativeFlags = MF_STRING;
        if (flags & gmpi_gui::legacy::MP_PLATFORM_MENU_TICKED)    nativeFlags |= MF_CHECKED;
        if (flags & gmpi_gui::legacy::MP_PLATFORM_MENU_GRAYED)    nativeFlags |= MF_GRAYED;
        if (flags & gmpi_gui::legacy::MP_PLATFORM_MENU_SEPARATOR) nativeFlags |= MF_SEPARATOR;
        if (flags & gmpi_gui::legacy::MP_PLATFORM_MENU_BREAK)     nativeFlags |= MF_MENUBREAK;

        const bool isSubStart = (flags & gmpi_gui::legacy::MP_PLATFORM_SUB_MENU_BEGIN) != 0;
        const bool isSubEnd   = (flags & gmpi_gui::legacy::MP_PLATFORM_SUB_MENU_END) != 0;

        if (isSubStart || isSubEnd)
        {
            if (isSubStart)
            {
                auto sub = CreatePopupMenu();
                AppendMenu(hmenus.back(), nativeFlags | MF_POPUP, (UINT_PTR)sub,
                    JmUnicodeConversions::Utf8ToWstring(text).c_str());
                hmenus.push_back(sub);
            }
            if (isSubEnd)
                hmenus.pop_back();
        }
        else
        {
            menuIds.push_back(id);
            itemCallbacks.emplace_back();
            AppendMenu(hmenus.back(), nativeFlags, menuIds.size(),
                JmUnicodeConversions::Utf8ToWstring(text).c_str());
        }
    }

    int trackMenu()
    {
        POINT pt{ (LONG)editrect.left, (LONG)editrect.top };
        ClientToScreen(parentWnd, &pt);
        const int flags = align | TPM_LEFTBUTTON | TPM_NONOTIFY | TPM_RETURNCMD;
        const auto index = TrackPopupMenu(hmenu, flags, pt.x, pt.y, 0, parentWnd, 0) - 1;
        selectedId = (index >= 0) ? menuIds[index] : 0;
        return index;
    }

public:
    Win32PopupMenu(HWND pParentWnd, const gmpi::drawing::Rect* r)
        : parentWnd(pParentWnd), editrect(*r)
    {
        hmenu = CreatePopupMenu();
        hmenus.push_back(hmenu);
    }

    ~Win32PopupMenu()
    {
        DestroyMenu(hmenu);
    }

    // === IPopupMenu ===
    gmpi::ReturnCode addItem(const char* text, int32_t id, int32_t flags, gmpi::api::IUnknown* callback) override
    {
        addItemCore(text ? text : "", id, flags);
        if (callback && !itemCallbacks.empty())
        {
            gmpi::shared_ptr<gmpi::api::IUnknown> cb;
            cb.attach(callback);
            itemCallbacks.back() = cb;
        }
        return gmpi::ReturnCode::Ok;
    }

    gmpi::ReturnCode setAlignment(int32_t alignment) override
    {
        switch (alignment)
        {
        case GmpiDrawing_API::MP1_TEXT_ALIGNMENT_LEADING:  align = TPM_LEFTALIGN;   break;
        case GmpiDrawing_API::MP1_TEXT_ALIGNMENT_CENTER:   align = TPM_CENTERALIGN; break;
        default:                                           align = TPM_RIGHTALIGN;  break;
        }
        return gmpi::ReturnCode::Ok;
    }

    gmpi::ReturnCode showAsync(gmpi::api::IUnknown* pcallback) override
    {
        const auto index = trackMenu();

        gmpi::shared_ptr<gmpi::api::IUnknown> unknown;
        unknown = pcallback;

        if (auto cb = unknown.as<gmpi::api::IPopupMenuCallback>(); cb)
            cb->onComplete(index >= 0 ? gmpi::ReturnCode::Ok : gmpi::ReturnCode::Cancel, selectedId);

        if (index >= 0 && index < (int32_t)itemCallbacks.size() && itemCallbacks[index])
        {
            gmpi::shared_ptr<gmpi::api::IPopupMenuCallback> itemCb;
            itemCallbacks[index]->queryInterface(&gmpi::api::IPopupMenuCallback::guid, itemCb.put_void());
            if (itemCb)
                itemCb->onComplete(gmpi::ReturnCode::Ok, menuIds[index]);
        }

        return gmpi::ReturnCode::Ok;
    }

    gmpi::ReturnCode queryInterface(const gmpi::api::Guid* iid, void** returnInterface) override
    {
        *returnInterface = {};
        GMPI_QUERYINTERFACE(gmpi::api::IPopupMenu);
        GMPI_QUERYINTERFACE(gmpi::api::IContextItemSink);
        return gmpi::ReturnCode::NoSupport;
    }

    GMPI_REFCOUNT
};

gmpi::ReturnCode DrawingFrameHwndBase::createPopupMenu(const gmpi::drawing::Rect* r, gmpi::api::IUnknown** returnPopupMenu)
{
    *returnPopupMenu = static_cast<gmpi::api::IPopupMenu*>(new Win32PopupMenu(getWindowHandle(), r));
    return gmpi::ReturnCode::Ok;
}

void DrawingFrameHwndBase::initTooltip()
{
    tooltip.init(getDllHandle(), getWindowHandle());
}

void DrawingFrameHwndBase::ShowToolTip()
{
    tooltip.show(getWindowHandle());
}

void DrawingFrameHwndBase::HideToolTip()
{
    tooltip.hide(getWindowHandle());
}

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

LRESULT DrawingFrameHwndBase::WindowProc(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam)
{
    if (!graphics_gmpi)
        return DefWindowProc(hwnd, message, wParam, lParam);

    switch (message)
    {
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    {
        const auto point = gmpi::hosting::win32::pointFromLParam(lParam, WindowToDips);
        if (gmpi::hosting::win32::isDuplicateMouseMove(message, point, cubaseBugPreviousMouseMove))
        {
            return TRUE;
        }

        TooltipOnMouseActivity();

        int32_t flags = gmpi::hosting::win32::makePointerFlags(message);

        gmpi::ReturnCode r{ gmpi::ReturnCode::Unhandled};
        switch (message)
        {
        case WM_MOUSEMOVE:
        {
            if (editor_gmpi)
                r = editor_gmpi->onPointerMove(point, flags);

            // get notified when mouse leaves window
            if (!isTrackingMouse)
            {
                gmpi::hosting::win32::beginMouseTracking(hwnd, isTrackingMouse);
                if (editor_gmpi)
                    editor_gmpi->setHover(true);
            }
        }
        break;

        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
            {
                if (editor_gmpi)
                    r = editor_gmpi->onPointerDown(point, flags);

                ::SetFocus(hwnd);

                // Handle right-click context menu.
                if (r == gmpi::ReturnCode::Unhandled && (flags & gmpi_gui_api::GG_POINTER_FLAG_SECONDBUTTON) != 0 && editor_gmpi)
                {
                    contextMenu.setNull();
                    r = launchContextMenu(point);
                }
            }
            break;

        case WM_MBUTTONUP:
        case WM_RBUTTONUP:
        case WM_LBUTTONUP:
            if (editor_gmpi)
                r = editor_gmpi->onPointerUp(point, flags);
            break;
        }
    }
    break;

    case WM_MOUSELEAVE:
        isTrackingMouse = false;
        if (editor_gmpi)
            editor_gmpi->setHover(false);
        break;

    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
    {
        const auto p = gmpi::hosting::win32::pointFromScreenLParam(getWindowHandle(), lParam, WindowToDips);

        //The wheel rotation will be a multiple of WHEEL_DELTA, which is set at 120. This is the threshold for action to be taken, and one such action (for example, scrolling one increment) should occur for each delta.
        const auto zDelta = GET_WHEEL_DELTA_WPARAM(wParam);

        int32_t flags = gmpi::hosting::win32::makeWheelFlags(message, wParam);

        if (editor_gmpi)
            /*auto r =*/ editor_gmpi->onMouseWheel(p, flags, zDelta);
    }
    break;

    case WM_CHAR:
#if 0 // TODO!!!
        if (gmpi_key_client)
            gmpi_key_client->OnKeyPress((wchar_t)wParam);
#endif
        break;

    case WM_PAINT:
    {
        OnPaint();
        //		return ::DefWindowProc(hwnd, message, wParam, lParam); // clear update rect.
    }
    break;

    case WM_SIZE:
    {
        UINT width = LOWORD(lParam);
        UINT height = HIWORD(lParam);

        OnSize(width, height);
        return ::DefWindowProc(hwnd, message, wParam, lParam); // clear update rect.
    }
    break;

    default:
        return DefWindowProc(hwnd, message, wParam, lParam);

    }
    return TRUE;
}

void DrawingFrameHwndBase::open(void* pParentWnd, const GmpiDrawing_API::MP1_SIZE_L* overrideSize)
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

        CreateSwapPanel(DrawingFactory->gmpiFactory.getD2dFactory());

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
            , NULL
            , 0
            , 0
            , width
            , height
            , SWP_NOZORDER
        );

		OnSize(width, height);
    }
}

// Ideally this is called at 60Hz so we can draw as fast as practical, but without blocking to wait for Vsync all the time (makes host unresponsive).
bool DrawingFrameHwndBase::onTimer()
{
    auto hwnd = getWindowHandle();
    if (hwnd == nullptr || graphics_gmpi == nullptr)
        return true;

    if (pollHdrChangesCount-- < 0)
    {
        pollHdrChangesCount = 100; // 1.5s

        if (windowWhiteLevel != calcWhiteLevel())
        {
            recreateSwapChainAndClientAsync();
        }
    }

    // Tooltips
    if (tooltip.readyToShow())
    {
        POINT P;
        GetCursorPos(&P);

        // Check mouse in window and not captured.
        if (WindowFromPoint(P) == hwnd && GetCapture() != hwnd)
        {
            ScreenToClient(hwnd, &P);

            const auto point = gmpi::drawing::transformPoint(WindowToDips, { static_cast<float>(P.x), static_cast<float>(P.y) });
/* TODO !!???
            gmpi_sdk::MpString text;
            editor_gmpi->getToolTip({point.x, point.y}, & text);
            if (!text.str().empty())
            {
              tooltip.getText() = JmUnicodeConversions::Utf8ToWstring(text.str());
                ShowToolTip();
            }
*/
        }
    }

    preGraphicsRedraw();

    // Queue pending drawing updates to backbuffer.
    const BOOL bErase = FALSE;

    for (const auto& invalidRect : getDirtyRects())
    {
        RECT rect{ invalidRect.left, invalidRect.top, invalidRect.right, invalidRect.bottom };
        ::InvalidateRect(hwnd, &rect, bErase);
    }
    clearDirtyRects();

    return true;
}

void DrawingFrameHwndBase::TooltipOnMouseActivity()
{
    tooltip.onMouseActivity(getWindowHandle());
}

void DrawingFrameHwndBase::OnPaint()
{
    // First clear update region (else windows will pound on this repeatedly).
    updateRegion_native.copyDirtyRects(getWindowHandle(), { static_cast<int32_t>(swapChainSize.width) , static_cast<int32_t>(swapChainSize.height) });
    ValidateRect(getWindowHandle(), NULL); // Clear invalid region for next frame.

    auto& dirtyRects = updateRegion_native.getUpdateRects();
    Paint({ dirtyRects.data(), dirtyRects.size() });
}

void DrawingFrameBase2::Paint(std::span<gmpi::drawing::RectL> dirtyRects)
{
    PaintFrame(dirtyRects);
}

bool DrawingFrameBase2::canPaint(std::span<gmpi::drawing::RectL> dirtyRects)
{
    return isInit.load(std::memory_order_relaxed);
}

bool DrawingFrameBase2::preparePaint(std::span<gmpi::drawing::RectL> dirtyRects)
{
    if (swapChain)
    {
        gmpi::directx::ComPtr<::IDXGIFactory2> dxgiFactory;
        swapChain->GetParent(__uuidof(dxgiFactory), dxgiFactory.put_void());
        if (!dxgiFactory->IsCurrent())
        {
            recreateSwapChainAndClientAsync();
            return false;
        }
    }

    return true;
}

void DrawingFrameBase2::renderFrame(ID2D1DeviceContext* deviceContext, std::span<gmpi::drawing::RectL> dirtyRects)
{
    se::directx::UniversalGraphicsContext context(DrawingFactory.get(), deviceContext);
    gmpi::drawing::Graphics graphics(&context);

    graphics.beginDraw();

    for (const auto& r : dirtyRects)
    {
        const gmpi::drawing::Rect dirtyRectPixels{ (float)r.left, (float)r.top, (float)r.right, (float)r.bottom };
        const auto dirtyRectDips = dirtyRectPixels * WindowToDips;

        graphics.pushAxisAlignedClip(dirtyRectDips);

        if (graphics_gmpi)
        {
            graphics_gmpi->render(&context);
        }
        else
        {
            gmpi::drawing::Color blankColor{ 0.f, 0.f, 0.f, 1.f };
            graphics.clear(blankColor);
        }

        if constexpr(false) // keywords: diagnose dirty rects debug dirty rects
        {
            static int32_t diagColor = 0;
            diagColor += 10;
            diagColor -= 0x1000;
            gmpi::drawing::Graphics g(static_cast<gmpi::drawing::api::IDeviceContext*>(&context));
            auto brush = g.createSolidColorBrush(gmpi::drawing::colorFromHex(diagColor, 0.3f));
            g.fillRectangle(dirtyRectDips, brush);
        }

        graphics.popAxisAlignedClip();
    }

    graphics.endDraw();
}

void DrawingFrameBase2::sizeClientDips(float width, float height)
{
    if (graphics_gmpi)
    {
        gmpi::drawing::Size available{ width, height };
        gmpi::drawing::Size desired{};

        graphics_gmpi->measure(&available, &desired);

		gmpi::drawing::Rect finalRect{ 0, 0, width, height };
        graphics_gmpi->arrange(&finalRect);
    }
}

void DrawingFrameHwndBase::invalidateRect(const gmpi::drawing::Rect* invalidRect)
{
    if (invalidRect)
        queueDirtyRect(invalidRect);
    else
        invalidateAll();
}

gmpi::drawing::RectL DrawingFrameHwndBase::getFullDirtyRect()
{
    RECT clientRect{};
    GetClientRect(getWindowHandle(), &clientRect);
    return { clientRect.left, clientRect.top, clientRect.right, clientRect.bottom };
}

