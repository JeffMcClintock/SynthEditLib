#include "pch.h"

#include <Windowsx.h>
#include "DrawingFrame2_win.h"
#include <dxgi1_6.h>
#include "shlobj.h"
#include "conversion.h"
#include "Drawing.h"

// Windows 32
#include "modules/se_sdk3_hosting/gmpi_gui_hosting.h"

DrawingFrameBase2::DrawingFrameBase2()
{
    DrawingFactory = std::make_unique<UniversalFactory>();
}

void DrawingFrameBase2::attachClient(gmpi_sdk::mp_shared_ptr<gmpi_gui_api::IMpGraphics3> gfx)
{
    detachClient();

    gmpi_gui_client = gfx;

    gfx->queryInterface(IGraphicsRedrawClient::guid, frameUpdateClient.asIMpUnknownPtr());
    //    gfx->queryInterface(gmpi_gui_api::IMpKeyClient::guid, gmpi_key_client.asIMpUnknownPtr());
    [[maybe_unused]] auto r = gfx->queryInterface(gmpi::MP_IID_GUI_PLUGIN2B, pluginParameters2B.asIMpUnknownPtr());

    gmpi_sdk::mp_shared_ptr<gmpi::IMpUserInterface2> pinHost;
    gmpi_gui_client->queryInterface(gmpi::MP_IID_GUI_PLUGIN2, pinHost.asIMpUnknownPtr());

    if (pinHost)
        pinHost->setHost(static_cast<gmpi_gui::legacy::IMpGraphicsHost*>(this)); // static_cast<gmpi_gui::IMpGraphicsHost*>(this));

    if(swapChain)
    {
        const auto scale = 1.0 / getRasterizationScale();

        sizeClientDips(
            static_cast<float>(swapChainSize.width) * scale,
            static_cast<float>(swapChainSize.height) * scale
        );
    }
}

void DrawingFrameBase2::detachClient()
{
    gmpi_gui_client = {};
    frameUpdateClient = {};
    pluginParameters2B = {};
}

void DrawingFrameBase2::detachAndRecreate()
{
    assert(!reentrant); // do this async please.

    detachClient();
    CreateSwapPanel(DrawingFactory->gmpiFactory.getD2dFactory());
}

void DrawingFrameBase2::OnScrolled(double x, double y, double zoom)
{
    scrollPos = { -static_cast<float>(x), -static_cast<float>(y) };
    zoomFactor = static_cast<float>(zoom);

    calcViewTransform();
}

void DrawingFrameBase2::calcViewTransform()
{
    viewTransform = gmpi::drawing::makeScale({zoomFactor, zoomFactor});
    viewTransform *= gmpi::drawing::makeTranslation({scrollPos.width, scrollPos.height});

    static_cast<gmpi::api::IDrawingHost*>(this)->invalidateRect(nullptr);
}

gmpi::ReturnCode DrawingFrameBase2::createKeyListener(const gmpi::drawing::Rect* r, gmpi::api::IUnknown** returnKeyListener)
{
    *returnKeyListener = new gmpi::hosting::win32::PlatformKeyListener(getWindowHandle(), r);
    return gmpi::ReturnCode::Ok;
}

void DrawingFrameHwndBase::invalidateRect(const gmpi::drawing::Rect* invalidRect)
{
    invalidateRect((const GmpiDrawing_API::MP1_RECT*)invalidRect);
}

float DrawingFrameHwndBase::getRasterizationScale()
{
#if 0
    int dpiX(96), dpiY(96);
    {
        HDC hdc = ::GetDC(getWindowHandle());
        dpiX = GetDeviceCaps(hdc, LOGPIXELSX);
        dpiY = GetDeviceCaps(hdc, LOGPIXELSY);
        ::ReleaseDC(getWindowHandle(), hdc);
    }

    return dpiX / 96.f;
#else
    // This is not recommended. Instead, DisplayProperties::LogicalDpi should be used for packaged Microsoft Store apps and GetDpiForWindow should be used for Win32 apps.
    /*
    float dpiX{ 96.f }, dpiY{ 96.f };
    DrawingFactory->getD2dFactory()->GetDesktopDpi(&dpiX, &dpiY);
    */

    const auto dpiX = GetDpiForWindow(getWindowHandle());
    return dpiX / 96.f;
#endif
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

void DrawingFrameHwndBase::initTooltip()
{
    if (tooltipWindow == nullptr && getWindowHandle())
    {
        auto instanceHandle = getDllHandle();
        {
            TOOLINFO ti{};

            // Create the ToolTip control.
            HWND hwndTT = CreateWindow(TOOLTIPS_CLASS, TEXT(""),
                WS_POPUP,
                CW_USEDEFAULT, CW_USEDEFAULT,
                CW_USEDEFAULT, CW_USEDEFAULT,
                NULL, (HMENU)NULL, instanceHandle,
                NULL);

            // Prepare TOOLINFO structure for use as tracking ToolTip.
            ti.cbSize = TTTOOLINFO_V1_SIZE; // win 7 compatible. sizeof(TOOLINFO);
            ti.uFlags = TTF_SUBCLASS;
            ti.hwnd = (HWND)getWindowHandle();
            ti.uId = (UINT)0;
            ti.hinst = instanceHandle;
            ti.lpszText = const_cast<TCHAR*> (TEXT("This is a tooltip"));
            ti.rect.left = ti.rect.top = ti.rect.bottom = ti.rect.right = 0;

            // Add the tool to the control
            if (!SendMessage(hwndTT, TTM_ADDTOOL, 0, (LPARAM)&ti))
            {
                DestroyWindow(hwndTT);
                return;
            }

            tooltipWindow = hwndTT;
        }
    }
}

void DrawingFrameHwndBase::ShowToolTip()
{
    //	_RPT0(_CRT_WARN, "YEAH!\n");

        //UTF8StringHelper tooltipText(tooltip);
        //if (platformObject)
    {
        auto platformObject = tooltipWindow;

        RECT rc;
        rc.left = (LONG)0;
        rc.top = (LONG)0;
        rc.right = (LONG)100000;
        rc.bottom = (LONG)100000;
        TOOLINFO ti = { 0 };
        ti.cbSize = TTTOOLINFO_V1_SIZE; // win 7 compatible. sizeof(TOOLINFO);
        ti.hwnd = (HWND)getWindowHandle(); // frame->getSystemWindow();
        ti.uId = 0;
        ti.rect = rc;
        ti.lpszText = (TCHAR*)(const TCHAR*)toolTipText.c_str();
        SendMessage((HWND)platformObject, TTM_UPDATETIPTEXT, 0, (LPARAM)&ti);
        SendMessage((HWND)platformObject, TTM_NEWTOOLRECT, 0, (LPARAM)&ti);
        SendMessage((HWND)platformObject, TTM_POPUP, 0, 0);
    }

    toolTipShown = true;
}

void DrawingFrameHwndBase::HideToolTip()
{
    toolTipShown = false;
    //	_RPT0(_CRT_WARN, "NUH!\n");

    if (tooltipWindow)
    {
        TOOLINFO ti = { 0 };
        ti.cbSize = TTTOOLINFO_V1_SIZE; // win 7 compatible. sizeof(TOOLINFO);
        ti.hwnd = (HWND)getWindowHandle(); // frame->getSystemWindow();
        ti.uId = 0;
        ti.lpszText = 0;
        SendMessage((HWND)tooltipWindow, TTM_UPDATETIPTEXT, 0, (LPARAM)&ti);
        SendMessage((HWND)tooltipWindow, TTM_POP, 0, 0);
    }
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
    if (!gmpi_gui_client)
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
        gmpi::drawing::Point point{ static_cast<float>(GET_X_LPARAM(lParam)), static_cast<float>(GET_Y_LPARAM(lParam)) };
        point *= WindowToDips;

        // Cubase sends spurious mouse move messages when transport running.
        // This prevents tooltips working.
        if (message == WM_MOUSEMOVE)
        {
            if (cubaseBugPreviousMouseMove == point)
            {
                return TRUE;
            }
            cubaseBugPreviousMouseMove = point;
        }
        else
        {
            cubaseBugPreviousMouseMove = { -1, -1 };
        }

        TooltipOnMouseActivity();

        int32_t flags = gmpi_gui_api::GG_POINTER_FLAG_INCONTACT | gmpi_gui_api::GG_POINTER_FLAG_PRIMARY | gmpi_gui_api::GG_POINTER_FLAG_CONFIDENCE;

        switch (message)
        {
        case WM_MBUTTONDOWN:
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
            flags |= gmpi_gui_api::GG_POINTER_FLAG_NEW;
            break;
        }

        switch (message)
        {
        case WM_LBUTTONUP:
        case WM_LBUTTONDOWN:
            flags |= gmpi_gui_api::GG_POINTER_FLAG_FIRSTBUTTON;
            break;
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
            flags |= gmpi_gui_api::GG_POINTER_FLAG_SECONDBUTTON;
            break;
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
            flags |= gmpi_gui_api::GG_POINTER_FLAG_THIRDBUTTON;
            break;
        }

        if (GetKeyState(VK_SHIFT) < 0)
        {
            flags |= gmpi_gui_api::GG_POINTER_KEY_SHIFT;
        }
        if (GetKeyState(VK_CONTROL) < 0)
        {
            flags |= gmpi_gui_api::GG_POINTER_KEY_CONTROL;
        }
        if (GetKeyState(VK_MENU) < 0)
        {
            flags |= gmpi_gui_api::GG_POINTER_KEY_ALT;
        }

        int32_t r;
        switch (message)
        {
        case WM_MOUSEMOVE:
        {
            r = gmpi_gui_client->onPointerMove(flags, {point.x, point.y});

            // get notified when mouse leaves window
            if (!isTrackingMouse)
            {
                TRACKMOUSEEVENT tme{};
                tme.cbSize = sizeof(TRACKMOUSEEVENT);
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = hwnd;

                if (::TrackMouseEvent(&tme))
                {
                    isTrackingMouse = true;
                }
                gmpi_gui_client->setHover(true);
            }
        }
        break;

        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
            {
                r = gmpi_gui_client->onPointerDown(flags, { point.x, point.y });
                ::SetFocus(hwnd);

                // Handle right-click context menu.
                if (r == gmpi::MP_UNHANDLED && (flags & gmpi_gui_api::GG_POINTER_FLAG_SECONDBUTTON) != 0 && pluginParameters2B)
                {
                    contextMenu.setNull();

                    GmpiDrawing::Rect rect(point.x, point.y, point.x + 120, point.y + 20);
                    createPlatformMenu(&rect, contextMenu.GetAddressOf());

                    GmpiGui::ContextItemsSinkAdaptor sink(contextMenu);

                    r = pluginParameters2B->populateContextMenu(point.x, point.y, &sink);

                    contextMenu.ShowAsync(
                        [this](int32_t res) -> int32_t
                        {
                            if (res == gmpi::MP_OK)
                            {
                                const auto commandId = contextMenu.GetSelectedId();
                                res = pluginParameters2B->onContextMenu(commandId);
                            }
                            contextMenu = {};
                            return res;
                        }
                    );
                }

            }
            break;

        case WM_MBUTTONUP:
        case WM_RBUTTONUP:
        case WM_LBUTTONUP:
            r = gmpi_gui_client->onPointerUp(flags, { point.x, point.y });
            break;
        }
    }
    break;

    case WM_MOUSELEAVE:
        isTrackingMouse = false;
        gmpi_gui_client->setHover(false);
        break;

    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
    {
        // supplied point is relative to *screen* not window.
        POINT pos = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        MapWindowPoints(NULL, getWindowHandle(), &pos, 1); // !!! ::ScreenToClient() might be more correct. ref MyFrameWndDirectX::OnMouseWheel

        gmpi::drawing::Point p(static_cast<float>(pos.x), static_cast<float>(pos.y));
        p *= WindowToDips;

        //The wheel rotation will be a multiple of WHEEL_DELTA, which is set at 120. This is the threshold for action to be taken, and one such action (for example, scrolling one increment) should occur for each delta.
        const auto zDelta = GET_WHEEL_DELTA_WPARAM(wParam);

        int32_t flags = gmpi_gui_api::GG_POINTER_FLAG_PRIMARY | gmpi_gui_api::GG_POINTER_FLAG_CONFIDENCE;

        if (WM_MOUSEHWHEEL == message)
            flags |= gmpi_gui_api::GG_POINTER_SCROLL_HORIZ;

        const auto fwKeys = GET_KEYSTATE_WPARAM(wParam);
        if (MK_SHIFT & fwKeys)
        {
            flags |= gmpi_gui_api::GG_POINTER_KEY_SHIFT;
        }
        if (MK_CONTROL & fwKeys)
        {
            flags |= gmpi_gui_api::GG_POINTER_KEY_CONTROL;
        }
        //if (GetKeyState(VK_MENU) < 0)
        //{
        //	flags |= gmpi_gui_api::GG_POINTER_KEY_ALT;
        //}

        /*auto r =*/ gmpi_gui_client->onMouseWheel(flags, zDelta, { p.x, p.y });
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

        calcViewTransform();

        initTooltip();

        if (gmpi_gui_client)
        {
            const auto scale = 1.0 / getRasterizationScale();

            sizeClientDips(
                static_cast<float>((r.right - r.left) * scale),
                static_cast<float>((r.bottom - r.top) * scale)
            );
        }

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

// see also DrawingFrame::calcWhiteLevel()
float DrawingFrameHwndBase::calcWhiteLevel()
{
    return gmpi::hosting::calcWhiteLevelForHwnd(getWindowHandle());
}

// Ideally this is called at 60Hz so we can draw as fast as practical, but without blocking to wait for Vsync all the time (makes host unresponsive).
bool DrawingFrameHwndBase::onTimer()
{
    auto hwnd = getWindowHandle();
    if (hwnd == nullptr || gmpi_gui_client == nullptr)
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
    if (toolTiptimer-- == 0 && !toolTipShown)
    {
        POINT P;
        GetCursorPos(&P);

        // Check mouse in window and not captured.
        if (WindowFromPoint(P) == hwnd && GetCapture() != hwnd)
        {
            ScreenToClient(hwnd, &P);

            const auto point = gmpi::drawing::transformPoint(WindowToDips, { static_cast<float>(P.x), static_cast<float>(P.y) });

            gmpi_sdk::MpString text;
            gmpi_gui_client->getToolTip({point.x, point.y}, & text);
            if (!text.str().empty())
            {
                toolTipText = JmUnicodeConversions::Utf8ToWstring(text.str());
                ShowToolTip();
            }
        }
    }

    if (frameUpdateClient)
    {
        frameUpdateClient->PreGraphicsRedraw();
    }

    // Queue pending drawing updates to backbuffer.
    const BOOL bErase = FALSE;

    for (auto& invalidRect : backBufferDirtyRects)
    {
        ::InvalidateRect(hwnd, reinterpret_cast<RECT*>(&invalidRect), bErase);
    }
    backBufferDirtyRects.clear();

    return true;
}

void DrawingFrameHwndBase::TooltipOnMouseActivity()
{
    if (toolTipShown)
    {
        if (toolTiptimer < -20) // ignore spurious MouseMove when Tooltip shows
        {
            HideToolTip();
            toolTiptimer = toolTiptimerInit;
        }
    }
    else
        toolTiptimer = toolTiptimerInit;
}

void DrawingFrameHwndBase::OnPaint()
{
    // First clear update region (else windows will pound on this repeatedly).
    updateRegion_native.copyDirtyRects(getWindowHandle(), { static_cast<int32_t>(swapChainSize.width) , static_cast<int32_t>(swapChainSize.height) });
    ValidateRect(getWindowHandle(), NULL); // Clear invalid region for next frame.

    auto& dirtyRects = updateRegion_native.getUpdateRects();

    Paint(dirtyRects);
}

void DrawingFrameBase2::Paint(const std::span<const gmpi::drawing::RectL> dirtyRects)
{
    // prevent infinite assert dialog boxes when assert happens during painting.
    if (!isInit.load(std::memory_order_relaxed) || reentrant || !gmpi_gui_client || dirtyRects.empty())
    {
        return;
    }

	// Detect switching on/off HDR mode.
    gmpi::directx::ComPtr<::IDXGIFactory2> dxgiFactory;
    swapChain->GetParent(__uuidof(dxgiFactory), dxgiFactory.put_void());
    if (!dxgiFactory->IsCurrent())
    {
        // _RPT0(0, "dxgiFactory is NOT Current!\n");
        recreateSwapChainAndClientAsync();
    }

    // if app dragged to new monitor or HDR changed we need to stop drawing until swapchain is recreated.
    if (monitorChanged)
        return;

    reentrant = true;

	//	_RPT1(_CRT_WARN, "OnPaint(); %d dirtyRects\n", dirtyRects.size() );

	if (!d2dDeviceContext) // not quite right, also need to re-create any resources (brushes etc) else most object draw blank. Could refresh the view in this case.
	{
		CreateSwapPanel(DrawingFactory->gmpiFactory.getD2dFactory());
	}

	assert(d2dDeviceContext);
	if (!d2dDeviceContext)
	{
		reentrant = false;
		return;
	}

    gmpi::directx::ComPtr <ID2D1DeviceContext> deviceContext;

	if (hdrRenderTarget) // draw onto intermediate buffer, then pass that through an effect to scale white.
    {
        d2dDeviceContext->BeginDraw();
        deviceContext = hdrRenderTargetDC;

//		_RPTN(0, "%s: hdrRenderTarget %x deviceContext %x\n", this->debugName.c_str(), hdrRenderTarget.get(), deviceContext.get());
    }
    else // draw directly on the swapchain bitmap.
    {
        deviceContext = d2dDeviceContext;
    }

    // draw onto the intermediate buffer.
	{
        se::directx::UniversalGraphicsContext context(DrawingFactory.get(), deviceContext);

		auto legacyContext = static_cast<GmpiDrawing_API::IMpDeviceContext*>(&context.sdk3Context);
		GmpiDrawing::Graphics graphics(legacyContext);

		graphics.BeginDraw();
		const auto viewTransformL = toLegacy(viewTransform);
		graphics.SetTransform(viewTransformL);

auto reverseTransform = gmpi::drawing::invert(viewTransform);

		// clip and draw each rect individually (causes some objects to redraw several times)
		for (auto& r : dirtyRects)
		{
            const gmpi::drawing::Rect dirtyRectPixels{ (float)r.left, (float)r.top, (float)r.right, (float)r.bottom };
            const auto dirtyRectDips = dirtyRectPixels * WindowToDips;
            const auto dirtyRectDipsPanZoomed = dirtyRectDips * reverseTransform; // Apply Pan and Zoom

            graphics.PushAxisAlignedClip(toLegacy(dirtyRectDipsPanZoomed));

			gmpi_gui_client->OnRender(legacyContext);
			graphics.PopAxisAlignedClip();
		}

		graphics.EndDraw();
	}

    // draw the intermediate buffer onto the swapchain.
    if(hdrRenderTarget)
	{
		// Draw the bitmap to the screen
		const D2D1_RECT_F destRect(0, 0, static_cast<float>(swapChainSize.width), static_cast<float>(swapChainSize.height));
        d2dDeviceContext->DrawImage(
              hdrWhiteScaleEffect.get()
            , D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR
            , D2D1_COMPOSITE_MODE_SOURCE_COPY
        );

        d2dDeviceContext->EndDraw();
    }

	// Present the backbuffer (if it has some new content)
	if (firstPresent)
	{
		firstPresent = false;
		const auto hr = swapChain->Present(1, 0);
		if (S_OK != hr && DXGI_STATUS_OCCLUDED != hr)
		{
			// DXGI_ERROR_INVALID_CALL 0x887A0001L
			ReleaseDevice();
		}
	}
	else
	{
		HRESULT hr = S_OK;
		{
			assert(!dirtyRects.empty());
			DXGI_PRESENT_PARAMETERS presetParameters{ (UINT)dirtyRects.size(), (RECT*)dirtyRects.data(), nullptr, nullptr, };

			hr = swapChain->Present1(1, 0, &presetParameters);
		}

		if (S_OK != hr && DXGI_STATUS_OCCLUDED != hr)
		{
			// DXGI_ERROR_INVALID_CALL 0x887A0001L
			ReleaseDevice();
		}
	}

    reentrant = false;
}

void DrawingFrameBase2::sizeClientDips(float width, float height)
{
    GmpiDrawing_API::MP1_SIZE available{ width, height };
    GmpiDrawing_API::MP1_SIZE desired{};

    gmpi_gui_client->measure(available, &desired);
    gmpi_gui_client->arrange({ 0, 0, width, height });
}

void DrawingFrameHwndBase::invalidateRect(const GmpiDrawing_API::MP1_RECT* invalidRect)
{
    GmpiDrawing::RectL r;
    if (invalidRect)
    {
        //_RPT4(_CRT_WARN, "invalidateRect r[ %d %d %d %d]\n", (int)invalidRect->left, (int)invalidRect->top, (int)invalidRect->right, (int)invalidRect->bottom);
        //r = RectToIntegerLarger(DipsToWindow.TransformRect(*invalidRect));
		const auto actualRect = fromLegacy(*invalidRect) * DipsToWindow;
        r.left   = static_cast<int32_t>(floorf(actualRect.left));
        r.top    = static_cast<int32_t>(floorf(actualRect.top));
        r.right  = static_cast<int32_t>( ceilf(actualRect.right));
        r.bottom = static_cast<int32_t>( ceilf(actualRect.bottom));
    }
    else
    {
        GetClientRect(getWindowHandle(), reinterpret_cast<RECT*>(&r));
    }

    auto area1 = r.getWidth() * r.getHeight();

    for (auto& dirtyRect : backBufferDirtyRects)
    {
        auto area2 = dirtyRect.getWidth() * dirtyRect.getHeight();

        GmpiDrawing::RectL unionrect(dirtyRect);

        unionrect.top = (std::min)(unionrect.top, r.top);
        unionrect.bottom = (std::max)(unionrect.bottom, r.bottom);
        unionrect.left = (std::min)(unionrect.left, r.left);
        unionrect.right = (std::max)(unionrect.right, r.right);

        auto unionarea = unionrect.getWidth() * unionrect.getHeight();

        if (unionarea <= area1 + area2)
        {
            // replace existing rect with combined rect
            dirtyRect = unionrect;
            return;
            break;
        }
    }

    // no optimisation found, add new rect.
    backBufferDirtyRects.push_back(r);
}

int32_t DrawingFrameHwndBase::setCapture()
{
    ::SetCapture(getWindowHandle());
    return gmpi::MP_OK;
}

int32_t DrawingFrameHwndBase::getCapture(int32_t& returnValue)
{
    returnValue = ::GetCapture() == getWindowHandle();
    return gmpi::MP_OK;
}

int32_t DrawingFrameHwndBase::releaseCapture()
{
    ::ReleaseCapture();

    return gmpi::MP_OK;
}

int32_t DrawingFrameHwndBase::createPlatformMenu(GmpiDrawing_API::MP1_RECT* rect, gmpi_gui::IMpPlatformMenu** returnMenu)
{
    auto nativeRect = *rect * DipsToWindow;
    *returnMenu = new GmpiGuiHosting::PGCC_PlatformMenu(getWindowHandle(), &nativeRect, DipsToWindow._22);
    return gmpi::MP_OK;
}

int32_t DrawingFrameHwndBase::createPlatformTextEdit(GmpiDrawing_API::MP1_RECT* rect, gmpi_gui::IMpPlatformText** returnTextEdit)
{
    auto nativeRect = *rect * DipsToWindow;
    *returnTextEdit = new GmpiGuiHosting::PGCC_PlatformTextEntry(getWindowHandle(), &nativeRect, DipsToWindow._22);

    return gmpi::MP_OK;
}

int32_t DrawingFrameHwndBase::createFileDialog(int32_t dialogType, gmpi_gui::IMpFileDialog** returnFileDialog)
{
    *returnFileDialog = new GmpiGuiHosting::Gmpi_Win_FileDialog(dialogType, getWindowHandle());
    return gmpi::MP_OK;
}

int32_t DrawingFrameHwndBase::createOkCancelDialog(int32_t dialogType, gmpi_gui::IMpOkCancelDialog** returnDialog)
{
    *returnDialog = new GmpiGuiHosting::Gmpi_Win_OkCancelDialog(dialogType, getWindowHandle());
    return gmpi::MP_OK;
}