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

#if 0
using namespace winrt;

void AddKeyStateFlags(Windows::System::VirtualKeyModifiers winUiKeyModifiers, int32_t& flags)
{
    if ((((int)winUiKeyModifiers) & (int)Windows::System::VirtualKeyModifiers::Shift) != 0)
    {
        flags |= gmpi_gui_api::GG_POINTER_KEY_SHIFT;
    }
    if ((((int)winUiKeyModifiers) & (int)Windows::System::VirtualKeyModifiers::Control) != 0)
    {
        flags |= gmpi_gui_api::GG_POINTER_KEY_CONTROL;
    }
    if ((((int)winUiKeyModifiers) & (int)Windows::System::VirtualKeyModifiers::Menu) != 0)
    {
        flags |= gmpi_gui_api::GG_POINTER_KEY_ALT;
    }
}

// issues: can't set initial folder
// mayby could use old Win32 picker instead?
class WINUI_PlatformFileDialog : public gmpi_gui::IMpFileDialog
{
    HWND mainWindowHandle{};
    std::string returnFilename;
    std::wstring initialFilename;
    std::wstring initialDirectory;
    std::wstring title;
    std::vector<std::pair<std::wstring, std::wstring>> extensions; // extension, description
    gmpi_gui::ICompletionCallback* callback{};

    Windows::Foundation::IAsyncAction showAsync()
    {
        winrt::apartment_context ui_thread; // Capture calling context (thread).

        auto picker = Windows::Storage::Pickers::FileSavePicker();
        picker.SuggestedFileName(initialFilename);
        picker.SuggestedStartLocation(Windows::Storage::Pickers::PickerLocationId::DocumentsLibrary);

        //		picker.ViewMode(Windows::Storage::Pickers::PickerViewMode::List);

        for (auto& it : extensions)
        {
            std::wstring ext = L'.' + it.first;
            picker.FileTypeChoices().Insert(it.second, winrt::single_threaded_vector<winrt::hstring>({ ext.c_str() }));
        }

        picker.as<IInitializeWithWindow>()->Initialize(getWindowHandle());

        auto file = co_await picker.PickSaveFileAsync();

        if (file == nullptr)
            co_return;

        returnFilename = JmUnicodeConversions::WStringToUtf8(file.Path().c_str());
        co_await ui_thread; // Switch back to calling context (thread). This allows any following statements to execute back on the UI thread.

        if (callback)
            callback->OnComplete(gmpi::MP_OK);
    }

public:
    WINUI_PlatformFileDialog(HWND mainWindow) : mainWindowHandle(mainWindow) {}

    // IMpFileDialog interface
    int32_t MP_STDCALL AddExtension(const char* extension, const char* description = "") override
    {
        extensions.push_back({ JmUnicodeConversions::Utf8ToWstring(extension), JmUnicodeConversions::Utf8ToWstring(description) });
        return gmpi::MP_OK;
    }
    int32_t MP_STDCALL SetInitialFilename(const char* text) override
    {
        initialFilename = Utf8ToWstring(text);
        return gmpi::MP_OK;
    }
    int32_t MP_STDCALL setInitialDirectory(const char* text) override
    {
        initialDirectory = Utf8ToWstring(text);
        return gmpi::MP_OK;
    }
    int32_t MP_STDCALL ShowAsync(gmpi_gui::ICompletionCallback* returnCompletionHandler) override
    {
        callback = returnCompletionHandler;
        showAsync();
        return gmpi::MP_OK;
    }
    int32_t MP_STDCALL GetSelectedFilename(IMpUnknown* returnString) override
    {
        gmpi::IString* returnValue{};

        if (gmpi::MP_OK != returnString->queryInterface(gmpi::MP_IID_RETURNSTRING, reinterpret_cast<void**>(&returnValue)))
        {
            return gmpi::MP_NOSUPPORT;
        }

        returnValue->setData(returnFilename.data(), (int32_t)returnFilename.size());
        return gmpi::MP_OK;        return gmpi::MP_OK;
    }

    GMPI_QUERYINTERFACE1(gmpi_gui::SE_IID_GRAPHICS_PLATFORM_FILE_DIALOG, gmpi_gui::IMpFileDialog);
    GMPI_REFCOUNT;
};

class WINUI_PlatformMenu : public gmpi_gui::IMpPlatformMenu
{
    //    winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher;
    DrawingFrameBase2* hostView{};
    Microsoft::UI::Xaml::FrameworkElement overobject;
    Microsoft::UI::Xaml::Controls::MenuFlyout menuFlyout;
    winrt::event_token loseFocusHandlerToken;
    gmpi_sdk::mp_shared_ptr<gmpi_gui::ICompletionCallback> callback;
    int32_t alignment = TPM_LEFTALIGN;
    GmpiDrawing::Rect editrect_s;
    int32_t result = -1;

    std::vector<Microsoft::UI::Xaml::Controls::MenuFlyoutSubItem> subMenus;

    void releaseMyselfAsync()
    {
        hostView->swapChainHost.DispatcherQueue().TryEnqueue(
            [this](auto&& ...)
            {
                // task to perform later.
                this->release();
            }
        );
    }

public:
    WINUI_PlatformMenu(DrawingFrameBase2* phostView, Microsoft::UI::Xaml::FrameworkElement poverobject, GmpiDrawing_API::MP1_RECT* editrect) :
        overobject(poverobject)
        , editrect_s(*editrect)
        , hostView(phostView)
    {
        _RPT0(_CRT_WARN, "WINUI_PlatformMenu::WINUI_PlatformMenu()\n");
        menuFlyout = Microsoft::UI::Xaml::Controls::MenuFlyout();
    }

    ~WINUI_PlatformMenu()
    {
        _RPT0(_CRT_WARN, "WINUI_PlatformMenu::~WINUI_PlatformMenu()\n");
    }

    // IMpPlatformMenu
    int32_t MP_STDCALL AddItem(const char* text, int32_t id, int32_t flags) override
    {
        auto wtitle = JmUnicodeConversions::Utf8ToWstring(text);
        wchar_t accelleratorKey{};
        {
            const auto p = wtitle.find(L'&');
            if (p != std::wstring::npos && p < wtitle.size() - 1)
            {
                accelleratorKey = wtitle[p + 1];
                wtitle.erase(p, 1);
            }
        }

        if ((flags & (gmpi_gui::MP_PLATFORM_SUB_MENU_BEGIN | gmpi_gui::MP_PLATFORM_SUB_MENU_END)) != 0)
        {
            if ((flags & gmpi_gui::MP_PLATFORM_SUB_MENU_BEGIN) != 0)
            {
                auto submenu = Microsoft::UI::Xaml::Controls::MenuFlyoutSubItem();
                submenu.Text(wtitle);
                if (accelleratorKey)
                {
                    Microsoft::UI::Xaml::Input::KeyboardAccelerator accellerator;
                    accellerator.Key(winrt::Windows::System::VirtualKey(toupper(accelleratorKey)));
                    submenu.KeyboardAccelerators().Append(accellerator);
                }

                subMenus.push_back(submenu);

                menuFlyout.Items().Append(submenu);
            }
            if ((flags & gmpi_gui::MP_PLATFORM_SUB_MENU_END) != 0 && !subMenus.empty())
            {
                subMenus.pop_back();
            }
        }
        else if ((flags & (gmpi_gui::MP_PLATFORM_MENU_SEPARATOR | gmpi_gui::MP_PLATFORM_MENU_BREAK)) != 0)
        {
            menuFlyout.Items().Append(Microsoft::UI::Xaml::Controls::MenuFlyoutSeparator());
        }
        else
        {
            Microsoft::UI::Xaml::Controls::MenuFlyoutItem item;

            if ((flags & gmpi_gui::MP_PLATFORM_MENU_TICKED) != 0)
            {
                auto toggleitem = Microsoft::UI::Xaml::Controls::ToggleMenuFlyoutItem();
                toggleitem.IsChecked(true);
                item = toggleitem;
            }
            else
            {
                item = Microsoft::UI::Xaml::Controls::MenuFlyoutItem();
            }

            item.Text(wtitle);
            item.Tag(winrt::box_value(id));
            if (accelleratorKey)
            {
                Microsoft::UI::Xaml::Input::KeyboardAccelerator accellerator;
                accellerator.Key(winrt::Windows::System::VirtualKey(toupper(accelleratorKey)));
                item.KeyboardAccelerators().Append(accellerator);
            }

            item.Click([this, id](auto&& sender, auto&& args)
                {
                    result = id;
                    if (callback)
                        callback->OnComplete(gmpi::MP_OK);
                }
            );

            if ((flags & gmpi_gui::MP_PLATFORM_MENU_GRAYED) != 0)
            {
                item.IsEnabled(false);
            }

            if (!subMenus.empty())
                subMenus.back().Items().Append(item);
            else
                menuFlyout.Items().Append(item);
        }

        return gmpi::MP_OK;
    }

    int32_t MP_STDCALL SetAlignment(int32_t palignment) override { alignment = palignment; return gmpi::MP_OK; }
    int32_t MP_STDCALL ShowAsync(gmpi_gui::ICompletionCallback* returnCompletionHandler) override
    {
        callback = returnCompletionHandler;
        addRef(); // take ownership of myself, because callers reference might be about to go out of scope.

        menuFlyout.ShowAt(overobject, { editrect_s.left, editrect_s.top });

        menuFlyout.Closed([this](auto&& ...)
            {
                releaseMyselfAsync();
            });

        return gmpi::MP_OK;
    }
    int32_t MP_STDCALL GetSelectedId() override
    {
        return result;
    }

    GMPI_QUERYINTERFACE1(gmpi_gui::SE_IID_GRAPHICS_PLATFORM_MENU, gmpi_gui::IMpPlatformMenu);
    GMPI_REFCOUNT;
};

class WINUI_PlatformTextEntry : public gmpi_gui::IMpPlatformText
{
    int align;
    float textHeight;
    GmpiDrawing::Rect editrect_s;
    Microsoft::UI::Xaml::FrameworkElement overobject;
    Microsoft::UI::Xaml::Controls::TextBox textBox;
    gmpi_sdk::mp_shared_ptr<gmpi_gui::ICompletionCallback> callback;    // legacy SDK
    gmpi::api::ITextEditCallback* callback2{};           // GMPI SDK
    winrt::event_token loseFocusHandlerToken;
    winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher;

public:
    std::string text_;
    bool multiline_ = false;

    // TODO: should editrect be scaled by dpi (rather than expect caller to do it?)
    WINUI_PlatformTextEntry(winrt::Microsoft::UI::Dispatching::DispatcherQueue pdispatcher, Microsoft::UI::Xaml::FrameworkElement poverobject, GmpiDrawing_API::MP1_RECT* editrect) :
        overobject(poverobject)
        , align(TPM_LEFTALIGN)
        , editrect_s(*editrect)
        , textHeight(12)
        , dispatcher(pdispatcher)
    {
    }

    ~WINUI_PlatformTextEntry()
    {
        // remove handler, so we don't trigger callback twice.
        textBox.LostFocus(loseFocusHandlerToken);

        auto parent = textBox.Parent();
        auto grid = parent.as<Microsoft::UI::Xaml::Controls::Grid>();
        if (grid)
        {
            uint32_t index{};
            if (grid.Children().IndexOf(textBox, index))
            {
                grid.Children().RemoveAt(index);
            }
        }
    }

    int32_t SetText(const char* text) override
    {
        text_ = text;
        textBox.Text(JmUnicodeConversions::Utf8ToWstring(text_));
        return gmpi::MP_OK;
    }

    // https://learn.microsoft.com/en-us/windows/windows-app-sdk/api/winrt/microsoft.ui?view=windows-app-sdk-1.5
    int32_t GetText(IMpUnknown* returnString) override
    {
        gmpi::IString* returnValue = 0;

        if (gmpi::MP_OK != returnString->queryInterface(gmpi::MP_IID_RETURNSTRING, reinterpret_cast<void**>(&returnValue)))
        {
            return gmpi::MP_NOSUPPORT;
        }

        // Q: How do i retrieve the text from a TextBox?
        // A: You can retrieve the text from a TextBox by using the Text property. The Text property returns a string that represents the text in the TextBox.
        text_ = JmUnicodeConversions::WStringToUtf8(textBox.Text().c_str());

        returnValue->setData(text_.data(), (int32_t)text_.size());
        return gmpi::MP_OK;
    }

    void releaseMyselfAsync()
    {
        dispatcher.TryEnqueue(
            [this](auto&& ...)
            {
                // task to perform later.
                this->release();
            }
        );
    }

    int32_t ShowAsync(gmpi_gui::ICompletionCallback* unknown) override
    {
        addRef(); // take ownership of myself, because callers pointer is about to go out of scope.

        // perform ungody hacks to accomodate both the old and new callback types.
        unknown->queryInterface(gmpi_gui::SE_IID_COMPLETION_CALLBACK, callback.asIMpUnknownPtr());
        unknown->queryInterface(*(gmpi::MpGuid*)&gmpi::api::ITextEditCallback::guid, (void**)&callback2);

        auto parent = overobject.Parent();
        auto grid = parent.as<Microsoft::UI::Xaml::Controls::Grid>();

        // Add a dynamic TextBox
        textBox = Microsoft::UI::Xaml::Controls::TextBox();
        textBox.Text(JmUnicodeConversions::Utf8ToWstring(text_));

        // position within cell
        textBox.HorizontalAlignment(Microsoft::UI::Xaml::HorizontalAlignment::Left);
        textBox.VerticalAlignment(Microsoft::UI::Xaml::VerticalAlignment::Top);
        const float heightHack = 8.f;
        const float widthHack = 6.f;
        const auto parentSize = overobject.ActualSize();
        textBox.Width(editrect_s.right - editrect_s.left + 2.f * widthHack);
        textBox.Height(editrect_s.bottom - editrect_s.top + 2.f * heightHack);
        textBox.Margin(Microsoft::UI::Xaml::Thickness(editrect_s.left - widthHack, editrect_s.top - heightHack, parentSize.x - editrect_s.right - widthHack, parentSize.y - editrect_s.bottom - heightHack));

        grid.Children().Append(textBox);
        grid.SetRow(textBox, grid.GetRow(overobject));
        grid.SetColumn(textBox, grid.GetColumn(overobject));

        //        callback = returnCompletionHandler;

        loseFocusHandlerToken = textBox.LostFocus([this](auto&& ...)
            {
                if (callback2)
                    callback2->onComplete(gmpi::ReturnCode::Ok);

                if (callback)
                    callback->OnComplete(gmpi::MP_OK);

                callback2 = nullptr;

                releaseMyselfAsync();
            });

        // <RETURN> dismisses textbox
        textBox.KeyUp([this](auto&& sender, auto&& args)
            {
                if (args.Key() == Windows::System::VirtualKey::Enter)
                {
                    //wrong, use token                    textBox.LostFocus(nullptr); // else that callback crashes

                    if (callback)
                    {
                        callback->OnComplete(gmpi::MP_OK);
                        releaseMyselfAsync();
                    }
                }
                else
                {
                    if (callback2)
                    {
                        gmpi_sdk::MpString s;
                        GetText(&s);
                        callback2->onChanged(s.c_str());
                    }
                }
            }
        );

        // setting focus immediatly seems to fail, as the framework steals the focus right after.
        // This works better for Waveshaper at least
        dispatcher.TryEnqueue(
            [this](auto&& ...)
            {
                textBox.Focus(Microsoft::UI::Xaml::FocusState::Programmatic);
            }
        );

        return gmpi::MP_OK;
    }

    int32_t SetAlignment(int32_t alignment) override
    {
        align = (alignment & 0x03);
        multiline_ = (alignment >> 16) == 1;
        return gmpi::MP_OK;
    }

    int32_t getAlignment()
    {
        return align;
    }

    int32_t SetTextSize(float height) override
    {
        textHeight = height;
        return gmpi::MP_OK;
    }

    GMPI_QUERYINTERFACE1(gmpi_gui::SE_IID_GRAPHICS_PLATFORM_TEXT, gmpi_gui::IMpPlatformText);
    GMPI_REFCOUNT;
};

class WINUI_PlatformKeyListener : public gmpi::api::IKeyListener
{
    winrt::Microsoft::UI::Xaml::FrameworkElement overobject;
    winrt::Microsoft::UI::Xaml::Shapes::Rectangle focusBox;
    gmpi::api::IKeyListenerCallback* callback2{};
    winrt::event_token loseFocusHandlerToken;
    winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher;
    uint8_t keyState[256];
    wchar_t keyDownChar[256]{}; // what was the state on key down (upper or lowercase for example)
    bool releasing{};

public:
    // TODO: should editrect be scaled by dpi (rather than expect caller to do it?)
    WINUI_PlatformKeyListener(winrt::Microsoft::UI::Dispatching::DispatcherQueue pdispatcher, winrt::Microsoft::UI::Xaml::FrameworkElement poverobject) :
        overobject(poverobject)
        , dispatcher(pdispatcher)
    {
    }

    ~WINUI_PlatformKeyListener()
    {
        // remove handler, so we don't trigger callback twice.
        focusBox.LostFocus(loseFocusHandlerToken);

        auto parent = focusBox.Parent();
        auto grid = parent.as<winrt::Microsoft::UI::Xaml::Controls::Grid>();
        if (grid)
        {
            uint32_t index{};
            if (grid.Children().IndexOf(focusBox, index))
            {
                grid.Children().RemoveAt(index);
            }
        }
    }

    void releaseMyselfAsync()
    {
        if (releasing) // handle double-release.
            return;

        releasing = true;

        dispatcher.TryEnqueue(
            [this](auto&& ...)
            {
                // task to perform later.
                delete this;
            }
        );
    }

    gmpi::ReturnCode showAsync(const gmpi::drawing::Rect* rect, gmpi::api::IUnknown* callback) override
    {
        callback->queryInterface(&gmpi::api::IKeyListenerCallback::guid, (void**)&callback2);

        auto parent = overobject.Parent();
        auto grid = parent.as<winrt::Microsoft::UI::Xaml::Controls::Grid>();

        // Add a dynamic TextBox
        focusBox = winrt::Microsoft::UI::Xaml::Shapes::Rectangle();
        focusBox.IsTabStop(true);

        // position within cell
        const auto parentSize = overobject.ActualSize();
        focusBox.HorizontalAlignment(winrt::Microsoft::UI::Xaml::HorizontalAlignment::Left);
        focusBox.VerticalAlignment(winrt::Microsoft::UI::Xaml::VerticalAlignment::Top);
        focusBox.Width(rect->right - rect->left);
        focusBox.Height(rect->bottom - rect->top);
        focusBox.Margin(winrt::Microsoft::UI::Xaml::Thickness(rect->left, rect->top, parentSize.x - rect->right, parentSize.y - rect->bottom));

#ifdef _DEBUG
        winrt::Microsoft::UI::Xaml::Media::SolidColorBrush fill(winrt::Windows::UI::Colors::Orange());
        fill.Opacity(0.1f);
        focusBox.Fill(fill);
#endif

        grid.Children().Append(focusBox);
        grid.SetRow(focusBox, grid.GetRow(overobject));
        grid.SetColumn(focusBox, grid.GetColumn(overobject));

        loseFocusHandlerToken = focusBox.LostFocus([this](auto&& ...)
            {
                auto temp = callback2;
                callback2 = nullptr;

                releaseMyselfAsync();

                if (temp)
                    temp->onLostFocus(gmpi::ReturnCode::Cancel);
            });

        focusBox.KeyDown([this](auto&& sender, auto&& args)
            {
                if (callback2)
                {
                    GetKeyboardState(keyState);
                    const int32_t vkey = 0xff & (int32_t)args.Key();

                    int32_t flags{};
                    if (keyState[VK_SHIFT] & 0x80)
                        flags |= gmpi_gui_api::GG_POINTER_KEY_SHIFT;
                    if (keyState[VK_CONTROL] & 0x80)
                        flags |= gmpi_gui_api::GG_POINTER_KEY_CONTROL;
                    if (keyState[VK_MENU] & 0x80)
                        flags |= gmpi_gui_api::GG_POINTER_KEY_ALT;

                    wchar_t keyBuffer[4];
                    auto r = ToUnicode((UINT)args.Key(), (UINT)args.OriginalKey(), keyState, keyBuffer, std::size(keyBuffer), 0);
                    if (r != 1)
                    {
                        // special key, pass raw virtual-key code.
                        keyBuffer[0] = static_cast<wchar_t>(vkey);
                    }

                    keyDownChar[vkey] = keyBuffer[0];
                    callback2->onKeyDown(keyBuffer[0], flags);

                    args.Handled(true);
                }
            }
        );

        focusBox.KeyUp([this](auto&& sender, auto&& args)
            {
                if (callback2)
                {
                    int32_t flags{}; // not bothering with flags for now.
                    const int vkey = 0xff & (int)args.Key();
                    callback2->onKeyUp(keyDownChar[vkey], flags);
                    args.Handled(true);
                }
            }
        );

        // textbox should absorb mouse clicks quietly, prevents focus being stolen by background.
        focusBox.PointerPressed([this](auto&& sender, auto&& args)
            {
                if (callback2)
                {
                    // TODO provide for module to move cursor in response to mouse
//					callback2->onPointerDown(args.GetCurrentPoint(textBox).Position().X, args.GetCurrentPoint(textBox).Position().Y);
                    args.Handled(true);
                }
            }
        );

        // setting focus immediatly seems to fail, as the framework steals the focus right after.
        // This works better
        dispatcher.TryEnqueue(
            [this](auto&& ...)
            {
                focusBox.Focus(winrt::Microsoft::UI::Xaml::FocusState::Programmatic);
            }
        );

        return gmpi::ReturnCode::Ok;
    }

    GMPI_QUERYINTERFACE_METHOD(gmpi::api::IKeyListener);
    int32_t refCount2_ = 1;
    int32_t addRef() override
    {
        return ++refCount2_;
    }
    int32_t release() override
    {
        if (--refCount2_ == 0)
        {
            // rather than delete immediate, do it async.
            releaseMyselfAsync();
            return 0;
        }
        return refCount2_;
    }
};

void DrawingFrameBase2::Init(
    winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel pSwapChainHost,
    winrt::Microsoft::UI::Xaml::Shapes::Rectangle pScrollSurface,
    HWND pMainWindowHandle
)
{
    swapChainHost = pSwapChainHost;
    scrollSurface = pScrollSurface;
    mainWindowHandle = pMainWindowHandle;

    scrollSurface.PointerPressed(
        [this](Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
        {
            // which button pressed?
            auto pt = args.GetCurrentPoint(scrollSurface);
            const int32_t flags = pt.Properties().IsRightButtonPressed() ? gmpi_gui_api::GG_POINTER_FLAG_SECONDBUTTON : gmpi_gui_api::GG_POINTER_FLAG_FIRSTBUTTON;

            OnPointer(true, flags, args);
        }
    );

    scrollSurface.PointerReleased(
        [this](Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
        {
            // which button pressed?
            auto pt = args.GetCurrentPoint(scrollSurface);
            const int32_t flags = pt.Properties().IsRightButtonPressed() ? gmpi_gui_api::GG_POINTER_FLAG_SECONDBUTTON : gmpi_gui_api::GG_POINTER_FLAG_FIRSTBUTTON;

            OnPointer(false, flags, args);
        }
    );
    scrollSurface.PointerMoved(
        [this](Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
        {
            if (!gmpi_gui_client)
                return;

            int32_t flags = gmpi_gui_api::GG_POINTER_FLAG_INCONTACT /*| gmpi_gui_api::GG_POINTER_FLAG_FIRSTBUTTON*/ | gmpi_gui_api::GG_POINTER_FLAG_PRIMARY | gmpi_gui_api::GG_POINTER_FLAG_CONFIDENCE;
            AddKeyStateFlags(args.KeyModifiers(), flags);

            const auto position = args.GetCurrentPoint(scrollSurface).Position();
            currentPointerPos = { position.X, position.Y };

            gmpi_gui_client->onPointerMove(flags, currentPointerPos);
        }
    );

    scrollSurface.PointerEntered(
        [this](Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
        {
            if (!gmpi_gui_client)
                return;

            gmpi_gui_client->setHover(true);
        }
    );
    scrollSurface.PointerExited(
        [this](Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
        {
            currentPointerPos = { -1, -1 };

            if (!gmpi_gui_client)
                return;

            gmpi_gui_client->setHover(false);
        }
    );

    scrollSurface.PointerWheelChanged(
        [this](Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
        {
            if (!gmpi_gui_client)
                return;

            const auto position = args.GetCurrentPoint(scrollSurface).Position();
            const GmpiDrawing_API::MP1_POINT point{ position.X, position.Y };

            int32_t flags = gmpi_gui_api::GG_POINTER_FLAG_PRIMARY | gmpi_gui_api::GG_POINTER_FLAG_INCONTACT | gmpi_gui_api::GG_POINTER_FLAG_CONFIDENCE;
            AddKeyStateFlags(args.KeyModifiers(), flags);

            const auto delta = args.GetCurrentPoint(scrollSurface).Properties().MouseWheelDelta();
            gmpi_gui_client->onMouseWheel(flags, delta, point);
        }
    );

    CreateSwapPanel();

    timer = std::make_unique<MyTimerHelper>([this]() { return OnTimer(); }, 24);
}

void DrawingFrameBase2::OnPointer(bool down, int32_t flags, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
{
    if (!gmpi_gui_client)
        return;

    currentPointerArgs = &args;

    if (down)
    {
        flags |= gmpi_gui_api::GG_POINTER_FLAG_NEW;
    }

    flags |= gmpi_gui_api::GG_POINTER_FLAG_PRIMARY | gmpi_gui_api::GG_POINTER_FLAG_INCONTACT | gmpi_gui_api::GG_POINTER_FLAG_CONFIDENCE;

    AddKeyStateFlags(args.KeyModifiers(), flags);

    const auto position = args.GetCurrentPoint(scrollSurface).Position();
    //_RPTN(_CRT_WARN, "Pointer %s %f %f\n", down ? "Down" : "Up", position.X, position.Y);
    const GmpiDrawing_API::MP1_POINT point{ position.X, position.Y };

    int32_t r{};
    if (down)
    {
        r = gmpi_gui_client->onPointerDown(flags, point);

        // Handle right-click on background. (right-click on objects is handled by object itself).
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
    else
    {
        r = gmpi_gui_client->onPointerUp(flags, point);
    }

    currentPointerArgs = {};
}
#endif

DrawingFrameBase2::DrawingFrameBase2()
{
    DrawingFactory = std::make_unique<UniversalFactory>();
}

#if 0
// TODO !!! complete merge with tempSharedD2DBase::CreateSwapPanel()
void DrawingFrameBase2::CreateSwapPanel(ID2D1Factory1* d2dFactory)
{
	// Create a Direct3D Device
    UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
    // you must explicity install DX debug support for this to work.
    creationFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL featureLevels[] =
    {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };

    D3D_FEATURE_LEVEL supportedFeatureLevel;
	gmpi::directx::ComPtr<ID3D11Device> d3dDevice;
	

    // Create Hardware device.
    HRESULT r = DXGI_ERROR_UNSUPPORTED;
    do {
        r = D3D11CreateDevice(nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            0,
            creationFlags,
            featureLevels,
            std::size(featureLevels),
            D3D11_SDK_VERSION,
            d3dDevice.put(),
            &supportedFeatureLevel,
            nullptr);

        // Clear D3D11_CREATE_DEVICE_DEBUG
        ((creationFlags) &= (0xffffffff ^ (D3D11_CREATE_DEVICE_DEBUG)));

    } while (r == 0x887a002d); // The application requested an operation that depends on an SDK component that is missing or mismatched. (no DEBUG LAYER).

    // Get the Direct3D device.
    auto dxgiDevice = d3dDevice.as<::IDXGIDevice>();

    // Get the DXGI adapter.
    gmpi::directx::ComPtr< ::IDXGIAdapter > dxgiAdapter;
    dxgiDevice->GetAdapter(dxgiAdapter.put());

    // Support for HDR displays.
    bool DX_support_sRGB{true};
    float whiteMult{ 1.0f };

    {
        UINT i = 0;
        gmpi::directx::ComPtr<IDXGIOutput> currentOutput;
        gmpi::directx::ComPtr<IDXGIOutput> bestOutput;
        int bestIntersectArea = -1;

        // get bounds of window having handle: getWindowHandle()
        RECT m_windowBounds;
        GetWindowRect(getWindowHandle(), &m_windowBounds);
		gmpi::drawing::RectL appWindowRect = { m_windowBounds.left, m_windowBounds.top, m_windowBounds.right, m_windowBounds.bottom };

        while (dxgiAdapter->EnumOutputs(i, currentOutput.put()) != DXGI_ERROR_NOT_FOUND)
        {
            // Get the rectangle bounds of current output
            DXGI_OUTPUT_DESC desc;
			/*auto hr =*/ currentOutput->GetDesc(&desc);
			RECT desktopRect = desc.DesktopCoordinates;
			gmpi::drawing::RectL outputRect = { desktopRect.left, desktopRect.top, desktopRect.right, desktopRect.bottom };

            // Compute the intersection
			const auto intersectRect = gmpi::drawing::intersectRect(appWindowRect, outputRect);
			const int intersectArea = getWidth(intersectRect) * getHeight(intersectRect);
            if (intersectArea > bestIntersectArea)
            {
                bestOutput = currentOutput;
                bestIntersectArea = intersectArea;
            }

            i++;
        }

        // Having determined the output (display) upon which the app is primarily being 
        // rendered, retrieve the HDR capabilities of that display by checking the color space.
		auto output6 = bestOutput.as<IDXGIOutput6>();

        if (output6)
        {
            DXGI_OUTPUT_DESC1 desc1;
            auto hr = output6->GetDesc1(&desc1);

            if (desc1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709)
            {
                _RPT0(_CRT_WARN, "SDR Display\n");
            }
            else
            {
                _RPT0(_CRT_WARN, "HDR Display\n");
            }

            uint32_t numPathArrayElements{};
            uint32_t numModeArrayElements{};

            GetDisplayConfigBufferSizes(
                QDC_ONLY_ACTIVE_PATHS,
                &numPathArrayElements,
                &numModeArrayElements
            );

            std::vector<DISPLAYCONFIG_PATH_INFO> pathInfo;
            std::vector<DISPLAYCONFIG_MODE_INFO> modeInfo;

            pathInfo.resize(numPathArrayElements);
            modeInfo.resize(numModeArrayElements);

            QueryDisplayConfig(
                QDC_ONLY_ACTIVE_PATHS,
                &numPathArrayElements,
                pathInfo.data(),
                &numModeArrayElements,
                modeInfo.data(),
                nullptr
            );

            DISPLAYCONFIG_SDR_WHITE_LEVEL white_level = {};
            white_level.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL;
            white_level.header.size = sizeof(white_level);
            for (uint32_t pathIdx = 0; pathIdx < numPathArrayElements; ++pathIdx)
            {
                white_level.header.adapterId = pathInfo[pathIdx].targetInfo.adapterId;
                white_level.header.id = pathInfo[pathIdx].targetInfo.id;

                if (DisplayConfigGetDeviceInfo(&white_level.header) == ERROR_SUCCESS)
                {
#if	ENABLE_HDR_SUPPORT // proper HDR rendering
                    {
                        // divide by 1000 to get nits, divide by reference nits (80) to get a factor
                        whiteMult = white_level.SDRWhiteLevel / 1000.f;
                    }
#else // fall back to 8-bit rendering and ignore HDR
                    {
                        const auto whiteMultiplier = white_level.SDRWhiteLevel / 1000.f;
                        DX_support_sRGB = DX_support_sRGB && whiteMultiplier == 1.0f; // workarround HDR issues by reverting to 8-bit colour
                    }
#endif
                }
            }
        }
    }
    
	if (m_disable_gpu)
	{
		// release hardware device
		d3dDevice = nullptr;
		r = DXGI_ERROR_UNSUPPORTED;
	}

	// fallback to software rendering.
	if (DXGI_ERROR_UNSUPPORTED == r)
	{
		do {
		r = D3D11CreateDevice(nullptr,
			D3D_DRIVER_TYPE_WARP,
			nullptr,
			creationFlags,
			nullptr, 0,
			D3D11_SDK_VERSION,
			d3dDevice.put(),
			&supportedFeatureLevel,
			nullptr);

			// Clear D3D11_CREATE_DEVICE_DEBUG
			((creationFlags) &= (0xffffffff ^ (D3D11_CREATE_DEVICE_DEBUG)));

		} while (r == 0x887a002d); // The application requested an operation that depends on an SDK component that is missing or mismatched. (no DEBUG LAYER).
	}
	
	// query adaptor memory. Assume small integrated graphics cards do not have the capacity for float pixels.
	// Software renderer has no device memory, yet does support float pixels anyhow.
	if (!m_disable_gpu)
	{
		DXGI_ADAPTER_DESC adapterDesc{};
		dxgiAdapter->GetDesc(&adapterDesc);

		const auto dedicatedRamMB = adapterDesc.DedicatedVideoMemory / 0x100000;

		// Intel HD Graphics on my Kogan has 128MB.
		DX_support_sRGB &= (dedicatedRamMB >= 512); // MB
	}
	

	// https://learn.microsoft.com/en-us/windows/win32/direct3darticles/high-dynamic-range
    const DXGI_FORMAT bestFormat = DXGI_FORMAT_R16G16B16A16_FLOAT; // Proper gamma-correct blending.
	const DXGI_FORMAT fallbackFormat = DXGI_FORMAT_B8G8R8A8_UNORM; // shitty linear blending.

    {
        UINT driverSrgbSupport = 0;
        auto hr = d3dDevice->CheckFormatSupport(bestFormat, &driverSrgbSupport);

        const UINT srgbflags = D3D11_FORMAT_SUPPORT_DISPLAY | D3D11_FORMAT_SUPPORT_TEXTURE2D | D3D11_FORMAT_SUPPORT_BLENDABLE;

        if (SUCCEEDED(hr))
        {
            DX_support_sRGB &= ((driverSrgbSupport & srgbflags) == srgbflags);
        }
    }

	DX_support_sRGB &= D3D_FEATURE_LEVEL_11_0 <= supportedFeatureLevel;

    // Get the DXGI factory.
    gmpi::directx::ComPtr<::IDXGIFactory2> dxgiFactory;
    dxgiAdapter->GetParent(__uuidof(dxgiFactory), dxgiFactory.put_void());

    DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
    swapChainDesc.Format = DX_support_sRGB ? bestFormat : fallbackFormat;
    swapChainDesc.SampleDesc.Count = 1; // Don't use multi-sampling.
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.BufferCount = 2;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
	swapChainDesc.Scaling = DXGI_SCALING_NONE; // prevents annoying stretching effect when resizing window.

	if (lowDpiMode)
	{
		RECT temprect;
		GetClientRect(getWindowHandle(), &temprect);

		swapChainDesc.Width = (temprect.right - temprect.left) / 2;
		swapChainDesc.Height = (temprect.bottom - temprect.top) / 2;
		swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
	}

    // customization point.
    gmpi::directx::ComPtr<::IDXGISwapChain1> swapChain1;
    auto swapchainresult = createNativeSwapChain(
		dxgiFactory.get(),
        d3dDevice.get(),
        &swapChainDesc,
        swapChain1.put()
    );

    if (FAILED(swapchainresult))
    {
        assert(false);

        // Handle the error appropriately
        if (swapchainresult == DXGI_ERROR_INVALID_CALL)
        {
            OutputDebugString(L"DXGI_ERROR_INVALID_CALL: The method call is invalid.\n");
        }
        else
        {
            // Handle other potential errors
            OutputDebugString(L"Failed to create swap chain.\n");
        }

        return;
    }

	swapChain1->QueryInterface(swapChain.getAddressOf());

    // Creating the Direct2D Device
    gmpi::directx::ComPtr<::ID2D1Device> d2dDevice;
    d2dFactory->CreateDevice(dxgiDevice.get(), d2dDevice.put());

    // and context.
    d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_ENABLE_MULTITHREADED_OPTIMIZATIONS, d2dDeviceContext.put());

	// disable DPI for testing.
	const float dpiScale = lowDpiMode ? 1.0f : getRasterizationScale();
    d2dDeviceContext->SetDpi(dpiScale * 96.f, dpiScale * 96.f);

	CreateDeviceSwapChainBitmap();

 	DipsToWindow = gmpi::drawing::makeScale(dpiScale, dpiScale);
	WindowToDips = gmpi::drawing::invert(DipsToWindow);

    // customisation point.
    OnSwapChainCreated(DX_support_sRGB, whiteMult);
}
#endif
#if 0
void DrawingFrameBase2::Paint()
{
	//DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
 //   swapChain->GetDesc1(&swapChainDesc);

    // draw one frame
//    const auto viewSize = getSwapChainSizePixels(); // swapChainHost.ActualSize();
    GmpiDrawing::Rect dirtyRect{ 0, 0, static_cast<float>(swapChainSize.width), static_cast<float>(swapChainSize.height) };

    {
        se::directx::UniversalGraphicsContext context(DrawingFactory->getInfo(), d2dDeviceContext.get());

        GmpiDrawing::Graphics graphics(static_cast<GmpiDrawing_API::IMpDeviceContext*>(&context.sdk3Context));

        graphics.BeginDraw();
        graphics.SetTransform(viewTransform);

        auto reverseTransform = viewTransform;
        reverseTransform.Invert();
        dirtyRect = reverseTransform.TransformRect(dirtyRect);

        graphics.PushAxisAlignedClip(dirtyRect);
        if (gmpi_gui_client)
        {
            gmpi_gui_client->OnRender(graphics.Get());
        }
        else
        {
            graphics.Clear(GmpiDrawing::Color::Orchid);
        }

        graphics.PopAxisAlignedClip();
        graphics.EndDraw();
    }

    swapChain->Present(1, 0);
}
#endif

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