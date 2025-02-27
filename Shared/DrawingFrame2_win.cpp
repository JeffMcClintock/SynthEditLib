#include "pch.h"

#include <Windowsx.h>
#include "DrawingFrame2_win.h"
//#include <winrt/Windows.UI.h>
//#include <winrt/Microsoft.UI.Xaml.Controls.h>
//#include <winrt/Microsoft.UI.Xaml.Input.h>
//#include <winrt/Microsoft.UI.Input.h>
//#include <winrt/Windows.Storage.Pickers.h>
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
    //    cv->queryInterface(gmpi_gui_api::IMpKeyClient::guid, gmpi_key_client.asIMpUnknownPtr());
    [[maybe_unused]] auto r = gfx->queryInterface(gmpi::MP_IID_GUI_PLUGIN2B, pluginParameters2B.asIMpUnknownPtr());

    gmpi_sdk::mp_shared_ptr<gmpi::IMpUserInterface2> pinHost;
    gmpi_gui_client->queryInterface(gmpi::MP_IID_GUI_PLUGIN2, pinHost.asIMpUnknownPtr());

    if (pinHost)
        pinHost->setHost(static_cast<gmpi_gui::legacy::IMpGraphicsHost*>(this)); // static_cast<gmpi_gui::IMpGraphicsHost*>(this));

    if(swapChain)
    {
        const auto availablePt = gmpi::drawing::transformPoint( WindowToDips, { static_cast<float>(swapChainSize.width) , static_cast<float>(swapChainSize.height) });
		GmpiDrawing_API::MP1_SIZE availableDips{ availablePt.x, availablePt.y };
        GmpiDrawing_API::MP1_SIZE desired{};
        gmpi_gui_client->measure(availableDips, &desired);
        gmpi_gui_client->arrange({ 0, 0, availableDips.width, availableDips.height });
    }
}

void DrawingFrameBase2::detachClient()
{
    gmpi_gui_client = {};
    frameUpdateClient = {};
    pluginParameters2B = {};
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

//?    WindowToDips = gmpi::drawing::invert(DipsToWindow);

    invalidateRect(nullptr);
}

gmpi::ReturnCode DrawingFrameBase2::createKeyListener(gmpi::api::IUnknown** returnKeyListener)
{
#if 0 // TODO
    *returnKeyListener = new WINUI_PlatformKeyListener(swapChainHost.DispatcherQueue(), swapChainHost);
#endif
    return gmpi::ReturnCode::Ok;
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

void DrawingFrameBase2::OnSwapChainCreated(bool DX_support_sRGB, float whiteMult)
{
    DrawingFactory->setSrgbSupport(DX_support_sRGB, whiteMult);

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
        gmpi::drawing::Point p{ static_cast<float>(GET_X_LPARAM(lParam)), static_cast<float>(GET_Y_LPARAM(lParam)) };
        p *= WindowToDips;

        // Cubase sends spurious mouse move messages when transport running.
        // This prevents tooltips working.
        if (message == WM_MOUSEMOVE)
        {
            if (cubaseBugPreviousMouseMove == p)
            {
                return TRUE;
            }
            cubaseBugPreviousMouseMove = p;
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
            r = gmpi_gui_client->onPointerMove(flags, {p.x, p.y});

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
            r = gmpi_gui_client->onPointerDown(flags, { p.x, p.y });
            ::SetFocus(hwnd);
            break;

        case WM_MBUTTONUP:
        case WM_RBUTTONUP:
        case WM_LBUTTONUP:
            r = gmpi_gui_client->onPointerUp(flags, { p.x, p.y });
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

        CreateSwapPanel(DrawingFactory->getD2dFactory());

        calcViewTransform();

        initTooltip();

        if (gmpi_gui_client)
        {
            const auto scale = getRasterizationScale();

            const gmpi::drawing::Size available{
                static_cast<float>((r.right - r.left) * scale),
                static_cast<float>((r.bottom - r.top) * scale)
            };

            gmpi::drawing::Size desired{};
            gmpi_gui_client->measure(*(GmpiDrawing_API::MP1_SIZE*)&available, (GmpiDrawing_API::MP1_SIZE*)&desired);
            const gmpi::drawing::Rect finalRect{ 0, 0, available.width, available.height };
            gmpi_gui_client->arrange(*(GmpiDrawing_API::MP1_RECT*) &finalRect);
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

        // Note: This method can fail, but it's okay to ignore the
        // error here, because the error will be returned again
        // the next time EndDraw is called.
/*
        UINT Width = 0; // Auto size
        UINT Height = 0;

        if (lowDpiMode)
        {
            RECT r;
            GetClientRect(&r);

            Width = (r.right - r.left) / 2;
            Height = (r.bottom - r.top) / 2;
        }
*/
        d2dDeviceContext->SetTarget(nullptr);
        if (S_OK == swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0))
        {
            CreateSwapPanel(DrawingFactory->getD2dFactory());
        }
        else
        {
            ReleaseDevice();
        }
    }
}

// Ideally this is called at 60Hz so we can draw as fast as practical, but without blocking to wait for Vsync all the time (makes host unresponsive).
bool DrawingFrameHwndBase::onTimer()
{
    auto hwnd = getWindowHandle();
    if (hwnd == nullptr || gmpi_gui_client == nullptr)
        return true;

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

    reentrant = true;

	//	_RPT1(_CRT_WARN, "OnPaint(); %d dirtyRects\n", dirtyRects.size() );

	if (!d2dDeviceContext) // not quite right, also need to re-create any resources (brushes etc) else most object draw blank. Could refresh the view in this case.
	{
		CreateSwapPanel(DrawingFactory->getD2dFactory());
	}

	assert(d2dDeviceContext);
	if (!d2dDeviceContext)
	{
		reentrant = false;
		return;
	}

	{
		se::directx::UniversalGraphicsContext context(DrawingFactory->getInfo(), d2dDeviceContext.get());

		auto legacyContext = static_cast<GmpiDrawing_API::IMpDeviceContext*>(&context.sdk3Context);
		GmpiDrawing::Graphics graphics(legacyContext);

		graphics.BeginDraw();
		const auto viewTransformL = toLegacy(viewTransform);
		graphics.SetTransform(viewTransformL);

auto reverseTransform = gmpi::drawing::invert(viewTransform);

		{
			// clip and draw each rect individually (causes some objects to redraw several times)
			for (auto& r : dirtyRects)
			{
                const gmpi::drawing::Rect dirtyRectPixels{ (float)r.left, (float)r.top, (float)r.right, (float)r.bottom };
                const auto dirtyRectDips = dirtyRectPixels * WindowToDips;
                const auto dirtyRectDipsPanZoomed = dirtyRectDips * reverseTransform; // Apply Pan and Zoom

                graphics.PushAxisAlignedClip(toLegacy(dirtyRectDipsPanZoomed));

/*
				auto r2 = WindowToDips.TransformRect(GmpiDrawing::Rect(static_cast<float>(r.left), static_cast<float>(r.top), static_cast<float>(r.right), static_cast<float>(r.bottom)));

				// Snap to whole DIPs.
				GmpiDrawing::Rect temp;
				temp.left = floorf(r2.left);
				temp.top = floorf(r2.top);
				temp.right = ceilf(r2.right);
				temp.bottom = ceilf(r2.bottom);

				graphics.PushAxisAlignedClip(temp);
*/

				gmpi_gui_client->OnRender(legacyContext);
				graphics.PopAxisAlignedClip();
			}
		}

#if 0
		// Print Frame Rate
//		const bool displayFrameRate = true;
		constexpr bool displayFrameRate = false;
		if (displayFrameRate)
		{
			static int frameCount = 0;
			static char frameCountString[100] = "";
			if (++frameCount == 60)
			{
				auto timenow = std::chrono::steady_clock::now();
				auto elapsed = std::chrono::steady_clock::now() - frameCountTime;
				auto elapsedSeconds = 0.001f * (float)std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

				float frameRate = frameCount / elapsedSeconds;

				//				sprintf(frameCountString, "%3.1f FPS. %dms PT", frameRate, presentTimeMs);
				sprintf_s(frameCountString, sizeof(frameCountString), "%3.1f FPS", frameRate);
				frameCountTime = timenow;
				frameCount = 0;

				auto brush = graphics.CreateSolidColorBrush(GmpiDrawing::Color::Black);
				auto fpsRect = GmpiDrawing::Rect(0, 0, 50, 18);
				graphics.FillRectangle(fpsRect, brush);
				brush.SetColor(GmpiDrawing::Color::White);
				graphics.DrawTextU(frameCountString, graphics.GetFactory().CreateTextFormat(12), fpsRect, brush);

				dirtyRects.push_back(GmpiDrawing::RectL(0, 0, 100, 36));
			}
		}
#endif
		/*const auto r =*/ graphics.EndDraw();

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
			/*
							presetParameters.pScrollRect = nullptr;
							presetParameters.pScrollOffset = nullptr;
							presetParameters.DirtyRectsCount = (UINT) dirtyRects.size();
							presetParameters.pDirtyRects = reinterpret_cast<RECT*>(dirtyRects.data()); // should be exact same layout.
			*/
			// checkout DXGI_PRESENT_DO_NOT_WAIT
//				hr = swapChain->Present1(1, DXGI_PRESENT_TEST, &presetParameters);
//				_RPT1(_CRT_WARN, "Present1() test = %x\n", hr);
/* NEVER returns DXGI_ERROR_WAS_STILL_DRAWING
	//			_RPT1(_CRT_WARN, "Present1() DirtyRectsCount = %d\n", presetParameters.DirtyRectsCount);
				hr = swapChain->Present1(1, DXGI_PRESENT_DO_NOT_WAIT, &presetParameters);
				if (hr == DXGI_ERROR_WAS_STILL_DRAWING)
				{
					_RPT1(_CRT_WARN, "Present1() Blocked\n", hr);
*/
// Present(0... improves framerate only from 60 -> 64 FPS, so must be blocking a little with "1".
//				auto timeA = std::chrono::steady_clock::now();
			hr = swapChain->Present1(1, 0, &presetParameters);
			//auto elapsed = std::chrono::steady_clock::now() - timeA;
			//presentTimeMs = (float)std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
//				}
/* could put this in timer to reduce blocking, agregating dirty rects until call successful.
*/
		}

		if (S_OK != hr && DXGI_STATUS_OCCLUDED != hr)
		{
			// DXGI_ERROR_INVALID_CALL 0x887A0001L
			ReleaseDevice();
		}
	}

    reentrant = false;
}

void DrawingFrameHwndBase::OnSize(UINT width, UINT height)
{
    assert(swapChain);
    assert(d2dDeviceContext);

    d2dDeviceContext->SetTarget(nullptr);

    if (S_OK == swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0))
    {
        CreateSwapPanel(DrawingFactory->getD2dFactory());
    }
    else
    {
        ReleaseDevice();
    }

    int dpiX, dpiY;
    {
        HDC hdc = ::GetDC(getWindowHandle());
        dpiX = GetDeviceCaps(hdc, LOGPIXELSX);
        dpiY = GetDeviceCaps(hdc, LOGPIXELSY);
        ::ReleaseDC(getWindowHandle(), hdc);
    }

    const GmpiDrawing_API::MP1_SIZE available{
        static_cast<float>(((width) * 96) / dpiX),
        static_cast<float>(((height) * 96) / dpiY)
    };

    gmpi_gui_client->arrange({ 0, 0, available.width, available.height });
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