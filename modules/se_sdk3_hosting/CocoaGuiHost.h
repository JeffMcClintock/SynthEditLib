#ifndef CocoaGuiHost_h
#define CocoaGuiHost_h

#import "./Cocoa_Gfx.h"
#import "./EventHelper.h"
#include <algorithm>
#include <string>
#include <utility>
#include <vector>
#include "helpers/NativeUi.h"
#include "legacy_sdk_gui2.h"
#include "LegacyMenuAdapter.h"

namespace GmpiGuiHosting
{
	// Cocoa don't allow this to be class variable.
	static NSTextField* textField = nullptr;

    inline NSRect gmpiRectToViewRect(NSRect viewbounds, GmpiDrawing::Rect rect)
    {
        #if USE_BACKING_BUFFER
            // flip co-ords
            return NSMakeRect(
              rect.left,
              viewbounds.origin.y + viewbounds.size.height - rect.bottom,
              rect.right - rect.left,
              rect.bottom - rect.top
              );
        #else
            return NSMakeRect(rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top);
        #endif
    }

	class PlatformMenu : public gmpi::api::IPopupMenu, public EventHelperClient
	{
		int32_t selectedId{};
		NSView* view;
		std::vector<int32_t> menuIds;
		std::vector<gmpi::shared_ptr<gmpi::api::IUnknown>> itemCallbacks;
		SYNTHEDIT_EVENT_HELPER_CLASSNAME* eventhelper;
		gmpi::shared_ptr<gmpi::api::IUnknown> returnCallback;
        NSPopUpButton* button;
        GmpiDrawing::Rect rect;
        std::vector<NSMenu*> menuStack;

		void showButton()
		{
			[[button cell] setAltersStateOfSelectedItem:NO];
			[[button cell] attachPopUpWithFrame:NSMakeRect(0,0,1,1) inView:view];
			[[button cell] performClickWithFrame:gmpiRectToViewRect(view.bounds, rect) inView:view];
		}

	public:

		PlatformMenu(NSView* pview, GmpiDrawing_API::MP1_RECT* prect)
		{
			view = pview;
            rect = *prect;
            eventhelper = [SYNTHEDIT_EVENT_HELPER_CLASSNAME alloc];
            [eventhelper initWithClient : this];
            button = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(10,1000,30,30)];
            menuStack.push_back([button menu]);
		}

		~PlatformMenu()
		{
			if (button != nil)
			{
				[button removeFromSuperview];
				button = nil;
			}
		}

		void CallbackFromCocoa(NSObject* sender) override
		{
            const int i = static_cast<int>([((NSMenuItem*) sender) tag]) - 1;
			const bool validIndex = (i >= 0 && i < static_cast<int>(menuIds.size()));
			if (validIndex)
				selectedId = menuIds[i];

			[button removeFromSuperview];

			if (returnCallback)
			{
				// Clear before calling so that onComplete's adapter->release() doesn't
				// leave a dangling ref when returnCallback's dtor runs.
				auto cb = returnCallback.as<gmpi::api::IPopupMenuCallback>();
				returnCallback = {};
				if (cb)
					cb->onComplete(validIndex ? gmpi::ReturnCode::Ok : gmpi::ReturnCode::Cancel, selectedId);
			}

			if (validIndex && i < static_cast<int>(itemCallbacks.size()) && itemCallbacks[i])
			{
				if (auto cb = itemCallbacks[i].as<gmpi::api::IPopupMenuCallback>(); cb)
					cb->onComplete(gmpi::ReturnCode::Ok, selectedId);
			}
		}

		gmpi::ReturnCode addItem(const char* text, int32_t id, int32_t flags, gmpi::api::IUnknown* callback) override
		{
			menuIds.push_back(id);
			itemCallbacks.emplace_back();

            if ((flags & gmpi_gui::MP_PLATFORM_MENU_SEPARATOR) != 0)
            {
                [menuStack.back() addItem:[NSMenuItem separatorItem] ];
            }
            else
            {
                NSString* nsstr = [NSString stringWithCString : (text ? text : "") encoding : NSUTF8StringEncoding];

                if ((flags & (gmpi_gui::MP_PLATFORM_SUB_MENU_BEGIN | gmpi_gui::MP_PLATFORM_SUB_MENU_END)) != 0)
                {
                    if ((flags & gmpi_gui::MP_PLATFORM_SUB_MENU_BEGIN) != 0)
                    {
                        auto menuItem = [menuStack.back() addItemWithTitle:nsstr action : nil keyEquivalent:@""];
                        NSMenu* subMenu = [[NSMenu alloc] init];
                        [menuItem setSubmenu:subMenu];
                        menuStack.push_back(subMenu);
                    }
                    if ((flags & gmpi_gui::MP_PLATFORM_SUB_MENU_END) != 0)
                        menuStack.pop_back();
                }
                else
                {
                    NSMenuItem* menuItem;
                    if ((flags & gmpi_gui::MP_PLATFORM_MENU_GRAYED) != 0)
                        menuItem = [menuStack.back() addItemWithTitle:nsstr action : nil keyEquivalent:@""];
                    else
                        menuItem = [menuStack.back() addItemWithTitle:nsstr action : @selector(menuItemSelected : ) keyEquivalent:@""];

                    [menuItem setTarget : eventhelper];
                    [menuItem setTag: menuIds.size()];

                    if ((flags & gmpi_gui::MP_PLATFORM_MENU_TICKED) != 0)
                        [menuItem setState:NSOnState];
                }
            }

			if (callback && !itemCallbacks.empty())
			{
				gmpi::shared_ptr<gmpi::api::IUnknown> cb;
				cb = callback;
				itemCallbacks.back() = cb;
			}
			return gmpi::ReturnCode::Ok;
		}

		gmpi::ReturnCode setAlignment(int32_t /*alignment*/) override
		{
			return gmpi::ReturnCode::Ok;
		}

		gmpi::ReturnCode showAsync(gmpi::api::IUnknown* pcallback) override
		{
			// Use assignment (not attach) so bridge_'s delegated refcount is incremented.
			returnCallback = pcallback;
			showButton();
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

    struct PlatformTextEntryObserver
    {
        virtual void onTextEditRemoved() = 0;
    };

	// Dual-API text edit: implements both legacy gmpi_gui::IMpPlatformText
	// and the new gmpi::api::ITextEdit, sharing the same NSTextField.
	class PlatformTextEntry : public gmpi_gui::IMpPlatformText, public gmpi::api::ITextEdit, public EventHelperClient
	{
        NSView* view;
		float textHeight;
        int align = 0;
        bool multiline = false;
		GmpiDrawing::Rect rect;
        gmpi_gui::ICompletionCallback* completionHandler{};         // legacy completion
        gmpi::shared_ptr<gmpi::api::IUnknown> newCallback;          // new-API completion
        SYNTHEDIT_EVENT_HELPER_CLASSNAME* eventhelper;
        PlatformTextEntryObserver* drawingFrame;

	public:
		std::string text_;

		PlatformTextEntry(PlatformTextEntryObserver* pdrawingFrame, NSView* pview, GmpiDrawing_API::MP1_RECT* prect) :
			view(pview)
            ,textHeight(12)
			,rect(*prect)
            ,drawingFrame(pdrawingFrame)
		{
            eventhelper = [SYNTHEDIT_EVENT_HELPER_CLASSNAME alloc];
            [eventhelper initWithClient : this];
        }

		~PlatformTextEntry()
		{
			if (textField != nil)
			{
				[textField removeFromSuperview];
				textField = nil;
			}

            if (drawingFrame)
                drawingFrame->onTextEditRemoved();
		}

		// Shared routine used by both legacy and new-API showAsync.
		void showField()
		{
			if (textField != nil)
			{
				[textField removeFromSuperview];
				textField = nil;
			}

			textField = [[NSTextField alloc] initWithFrame:gmpiRectToViewRect(view.bounds, rect)];

			[textField setFont:[NSFont systemFontOfSize:textHeight]];

			NSString* nsstr = [NSString stringWithCString : text_.c_str() encoding: NSUTF8StringEncoding];
			[textField setStringValue:nsstr];

			textField.bezeled = false;

			switch(align)
			{
				case GmpiDrawing_API::MP1_TEXT_ALIGNMENT_LEADING:
					break;
				case GmpiDrawing_API::MP1_TEXT_ALIGNMENT_CENTER:
					textField.alignment = NSTextAlignmentCenter;
					break;
				case GmpiDrawing_API::MP1_TEXT_ALIGNMENT_TRAILING:
					textField.alignment = NSTextAlignmentRight;
					break;
			}

			textField.usesSingleLineMode = !multiline;
			textField.drawsBackground = true;
			[textField setBackgroundColor:[NSColor textBackgroundColor]];

			[textField setTarget:eventhelper];
			[textField setAction: @selector(endEditing:)];

			[view addSubview : textField];
			[textField becomeFirstResponder];
		}

		int32_t MP_STDCALL SetText(const char* text) override
		{
			text_ = text ? text : "";
			return gmpi::MP_OK;
		}

		int32_t MP_STDCALL GetText(IMpUnknown* returnString) override
		{
			gmpi::IString* returnValue = 0;

			if (gmpi::MP_OK != returnString->queryInterface(gmpi::MP_IID_RETURNSTRING, reinterpret_cast<void**>(&returnValue)))
			{
				return gmpi::MP_NOSUPPORT;
			}

			returnValue->setData(text_.data(), (int32_t)text_.size());
			return gmpi::MP_OK;
		}

		int32_t MP_STDCALL ShowAsync(gmpi_gui::ICompletionCallback* pCompletionHandler) override
		{
            completionHandler = pCompletionHandler;
            showField();
			return gmpi::MP_OK;
		}

		int32_t MP_STDCALL SetAlignment(int32_t alignment) override
		{
            align = (alignment & 0x03);
            multiline = (alignment > 16) == 1;
			return gmpi::MP_OK;
		}

		int32_t MP_STDCALL SetTextSize(float height) override
		{
			textHeight = height;
			return gmpi::MP_OK;
		}

		// new-API ITextEdit methods:
		gmpi::ReturnCode setText(const char* text) override
		{
			(void) SetText(text);
			return gmpi::ReturnCode::Ok;
		}

		gmpi::ReturnCode setAlignment(int32_t alignment) override
		{
			(void) SetAlignment(alignment);
			return gmpi::ReturnCode::Ok;
		}

		gmpi::ReturnCode setTextSize(float height) override
		{
			(void) SetTextSize(height);
			return gmpi::ReturnCode::Ok;
		}

		gmpi::ReturnCode showAsync(gmpi::api::IUnknown* pcallback) override
		{
			// Caller transfers ownership: `new Callback(...)` with refcount=1. attach() steals it.
			newCallback.attach(pcallback);
			showField();
			return gmpi::ReturnCode::Ok;
		}

        void CallbackFromCocoa(NSObject* sender) override
        {
            if (textField)
                text_ = [[textField stringValue] UTF8String];

            [textField removeFromSuperview];

            if (completionHandler)
                completionHandler->OnComplete(gmpi::MP_OK);

            if (newCallback)
            {
                if (auto cb = newCallback.as<gmpi::api::ITextEditCallback>(); cb)
                {
                    cb->onChanged(text_.c_str());
                    cb->onComplete(gmpi::ReturnCode::Ok);
                }
            }
        }

		// legacy queryInterface (gmpi::MpGuid&): responds to legacy IID and bridges to new API
		int32_t MP_STDCALL queryInterface(const gmpi::MpGuid& iid, void** returnInterface) override
		{
			*returnInterface = nullptr;
			if (iid == gmpi_gui::SE_IID_GRAPHICS_PLATFORM_TEXT || iid == gmpi::MP_IID_UNKNOWN)
			{
				*returnInterface = static_cast<gmpi_gui::IMpPlatformText*>(this);
				addRef();
				return gmpi::MP_OK;
			}
			if (iid == *reinterpret_cast<const gmpi::MpGuid*>(&gmpi::api::ITextEdit::guid))
			{
				*returnInterface = static_cast<gmpi::api::ITextEdit*>(this);
				addRef();
				return gmpi::MP_OK;
			}
			return gmpi::MP_NOSUPPORT;
		}

		// new-API queryInterface (gmpi::api::Guid*): responds to new IIDs and bridges to legacy
		gmpi::ReturnCode queryInterface(const gmpi::api::Guid* iid, void** returnInterface) override
		{
			*returnInterface = nullptr;
			if (*iid == gmpi::api::ITextEdit::guid || *iid == gmpi::api::IUnknown::guid)
			{
				*returnInterface = static_cast<gmpi::api::ITextEdit*>(this);
				addRef();
				return gmpi::ReturnCode::Ok;
			}
			if (*iid == gmpi_gui::legacy::IMpPlatformText::guid)
			{
				*returnInterface = static_cast<gmpi_gui::IMpPlatformText*>(this);
				addRef();
				return gmpi::ReturnCode::Ok;
			}
			return gmpi::ReturnCode::NoSupport;
		}

		GMPI_REFCOUNT
	};

	// Dual-API file dialog. Legacy IMpFileDialog and new IFileDialog both define
	// `setInitialDirectory(const char*)` differing only in return type, which C++
	// won't let us resolve via multiple inheritance. Instead, the outer class
	// implements the new API, and a nested LegacyAdapter class exposes the legacy
	// interface and forwards to the outer's state. queryInterface cross-bridges.
	class PlatformFileDialog : public gmpi::api::IFileDialog
	{
		int32_t mode_;
		std::string initial_filename;
		std::string initial_folder;
		std::string selectedFilename;
        NSView* view;

		NSSavePanel* buildPanel()
		{
			NSSavePanel* dialog = nullptr;

			if (mode_ == static_cast<int32_t>(gmpi::api::FileDialogType::Save))
			{
				dialog = [NSSavePanel savePanel];
				dialog.title = @"Save file";
			}
			else
			{
				NSOpenPanel* openPanel = [NSOpenPanel openPanel];
				const bool folderMode = (mode_ == static_cast<int32_t>(gmpi::api::FileDialogType::Folder));
				openPanel.canChooseFiles = folderMode ? NO : YES;
				openPanel.canChooseDirectories = folderMode ? YES : NO;
				openPanel.allowsMultipleSelection = NO;
				dialog = openPanel;
				dialog.title = folderMode ? @"Choose folder" : @"Open file";
			}

			dialog.showsResizeIndicator = YES;
			dialog.showsHiddenFiles = NO;
			dialog.canCreateDirectories = YES;

			if (!initial_folder.empty())
				[dialog setDirectoryURL:[NSURL fileURLWithPath:[NSString stringWithUTF8String:initial_folder.c_str()]]];

			if (!initial_filename.empty() && mode_ == static_cast<int32_t>(gmpi::api::FileDialogType::Save))
				[dialog setNameFieldStringValue:[NSString stringWithUTF8String:initial_filename.c_str()]];

			if (mode_ != static_cast<int32_t>(gmpi::api::FileDialogType::Folder))
			{
				NSMutableArray* extensionsstring = [[NSMutableArray alloc] init];
				bool allowsOtherFileTypes = false;
				for (auto& e : extensions)
				{
					if (e.first == "*")
						allowsOtherFileTypes = true;
					else
						[extensionsstring addObject:[NSString stringWithUTF8String:e.first.c_str()]];
				}
				if (!extensions.empty() && [extensionsstring count] > 0)
				{
					dialog.allowedFileTypes = extensionsstring;
					if (allowsOtherFileTypes)
						dialog.allowsOtherFileTypes = YES;
				}
			}

			return dialog;
		}

		void addExtensionInternal(const char* extension, const char* description)
		{
			std::string ext(extension ? extension : "");
			std::string desc(description ? description : "");
			if (desc.empty())
			{
				if (ext == "*")
					desc = "All";
				else
					desc = ext;
				desc += " Files";
			}
			extensions.push_back(std::pair<std::string, std::string>(ext, desc));
		}

		// Nested adapter that presents the legacy gmpi_gui::IMpFileDialog vtable.
		// Reference-counted independently so queryInterface can hand it out while
		// keeping the outer alive via a strong back-reference.
		class LegacyAdapter : public gmpi_gui::IMpFileDialog
		{
			PlatformFileDialog* owner;
			gmpi_gui::ICompletionCallback* completionHandler{};
		public:
			LegacyAdapter(PlatformFileDialog* p) : owner(p) {}

			int32_t MP_STDCALL AddExtension(const char* extension, const char* description) override
			{
				owner->addExtensionInternal(extension, description);
				return gmpi::MP_OK;
			}
			int32_t MP_STDCALL SetInitialFilename(const char* text) override
			{
				owner->initial_filename = text ? text : "";
				return gmpi::MP_OK;
			}
			int32_t MP_STDCALL setInitialDirectory(const char* text) override
			{
				owner->initial_folder = text ? text : "";
				return gmpi::MP_OK;
			}
			int32_t MP_STDCALL ShowAsync(gmpi_gui::ICompletionCallback* pcompletionHandler) override
			{
				completionHandler = pcompletionHandler;

				NSSavePanel* dialog = owner->buildPanel();

				if ([dialog runModal] == NSModalResponseOK)
				{
					NSURL* selection = dialog.URL;
					NSString* path = [[selection path] stringByResolvingSymlinksInPath];
					owner->selectedFilename = [path UTF8String];
					completionHandler->OnComplete(gmpi::MP_OK);
				}
				else
				{
					completionHandler->OnComplete(gmpi::MP_FAIL);
				}
				return gmpi::MP_OK;
			}
			int32_t MP_STDCALL GetSelectedFilename(IMpUnknown* returnString) override
			{
				gmpi::IString* returnValue = 0;
				if (gmpi::MP_OK != returnString->queryInterface(gmpi::MP_IID_RETURNSTRING, reinterpret_cast<void**>(&returnValue)))
					return gmpi::MP_NOSUPPORT;
				returnValue->setData(owner->selectedFilename.data(), (int32_t)owner->selectedFilename.size());
				return gmpi::MP_OK;
			}

			// Delegate lifetime and QI to owner so a single refcount controls both vtables.
			int32_t MP_STDCALL queryInterface(const gmpi::MpGuid& iid, void** returnInterface) override
			{
				*returnInterface = nullptr;
				if (iid == gmpi_gui::SE_IID_GRAPHICS_PLATFORM_FILE_DIALOG || iid == gmpi::MP_IID_UNKNOWN)
				{
					*returnInterface = static_cast<gmpi_gui::IMpFileDialog*>(this);
					owner->addRef();
					return gmpi::MP_OK;
				}
				return gmpi::MP_NOSUPPORT;
			}
			int32_t MP_STDCALL addRef() override   { return owner->addRef(); }
			int32_t MP_STDCALL release() override  { return owner->release(); }
		};

		LegacyAdapter legacyAdapter{this};

	public:
		std::vector< std::pair< std::string, std::string> > extensions;

		PlatformFileDialog(int32_t mode, NSView* pview) :
			view(pview)
			,mode_(mode)
		{
		}

		// Accessor for the legacy vtable; refcount is shared with the outer.
		gmpi_gui::IMpFileDialog* asLegacy() { return &legacyAdapter; }

		// new-API IFileDialog methods:
		gmpi::ReturnCode addExtension(const char* extension, const char* description) override
		{
			addExtensionInternal(extension, description);
			return gmpi::ReturnCode::Ok;
		}

		gmpi::ReturnCode setInitialFilename(const char* text) override
		{
			initial_filename = text ? text : "";
			return gmpi::ReturnCode::Ok;
		}

		gmpi::ReturnCode setInitialDirectory(const char* text) override
		{
			initial_folder = text ? text : "";
			return gmpi::ReturnCode::Ok;
		}

		gmpi::ReturnCode showAsync(const gmpi::drawing::Rect* /*rect*/, gmpi::api::IUnknown* pcallback) override
		{
			gmpi::shared_ptr<gmpi::api::IUnknown> unknown;
			unknown.attach(pcallback);
			auto fileCallback = unknown.as<gmpi::api::IFileDialogCallback>();
			if (!fileCallback)
				return gmpi::ReturnCode::Fail;

			NSSavePanel* dialog = buildPanel();

			if (view)
			{
				// Sheet-modal async: retain the typed callback via block capture.
				auto prevent_release = fileCallback;
				[dialog beginSheetModalForWindow:[view window] completionHandler:^(NSModalResponse result) {
					if (result == NSModalResponseOK)
					{
						const char* path = [[dialog URL] fileSystemRepresentation];
						selectedFilename = path ? path : "";
						prevent_release->onComplete(gmpi::ReturnCode::Ok, selectedFilename.c_str());
					}
					else
					{
						prevent_release->onComplete(gmpi::ReturnCode::Cancel, "");
					}
				}];
			}
			else
			{
				// No view: run synchronously as an app-level modal dialog.
				if ([dialog runModal] == NSModalResponseOK)
				{
					const char* path = [[dialog URL] fileSystemRepresentation];
					selectedFilename = path ? path : "";
					fileCallback->onComplete(gmpi::ReturnCode::Ok, selectedFilename.c_str());
				}
				else
				{
					fileCallback->onComplete(gmpi::ReturnCode::Cancel, "");
				}
			}

			return gmpi::ReturnCode::Ok;
		}

		// new-API queryInterface: cross-bridges to legacy via the adapter.
		gmpi::ReturnCode queryInterface(const gmpi::api::Guid* iid, void** returnInterface) override
		{
			*returnInterface = nullptr;
			if (*iid == gmpi::api::IFileDialog::guid || *iid == gmpi::api::IUnknown::guid)
			{
				*returnInterface = static_cast<gmpi::api::IFileDialog*>(this);
				addRef();
				return gmpi::ReturnCode::Ok;
			}
			if (*iid == gmpi_gui::legacy::IMpFileDialog::guid)
			{
				*returnInterface = static_cast<gmpi_gui::IMpFileDialog*>(&legacyAdapter);
				addRef();
				return gmpi::ReturnCode::Ok;
			}
			return gmpi::ReturnCode::NoSupport;
		}

		GMPI_REFCOUNT
	};

	// Dual-API stock dialog. Implements both legacy gmpi_gui::IMpOkCancelDialog
	// and new gmpi::api::IStockDialog. dialogType selects buttons (Ok, OkCancel,
	// YesNo, YesNoCancel). Legacy path uses OkCancel with (MP_OK | MP_CANCEL).
	class PlatformOkCancelDialog : public gmpi_gui::IMpOkCancelDialog, public gmpi::api::IStockDialog
	{
		int32_t mode_;
		std::string title;
		std::string text;
		NSView* view;

		NSAlert* buildAlert()
		{
			NSAlert* alert = [[NSAlert alloc] init];
			[alert setMessageText:[NSString stringWithUTF8String:title.c_str()]];
			[alert setInformativeText:[NSString stringWithUTF8String:text.c_str()]];
			[alert setAlertStyle:NSAlertStyleWarning];

			switch (static_cast<gmpi::api::StockDialogType>(mode_))
			{
			case gmpi::api::StockDialogType::Ok:
				[alert addButtonWithTitle:@"OK"];
				break;
			case gmpi::api::StockDialogType::YesNo:
				[alert addButtonWithTitle:@"Yes"];
				[alert addButtonWithTitle:@"No"];
				break;
			case gmpi::api::StockDialogType::YesNoCancel:
				[alert addButtonWithTitle:@"Yes"];
				[alert addButtonWithTitle:@"No"];
				[alert addButtonWithTitle:@"Cancel"];
				break;
			case gmpi::api::StockDialogType::OkCancel:
			default:
				[alert addButtonWithTitle:@"OK"];
				[alert addButtonWithTitle:@"Cancel"];
				break;
			}
			return alert;
		}

	public:
		PlatformOkCancelDialog(int32_t mode, NSView* pview) :
			mode_(mode)
			,view(pview)
		{
		}

		PlatformOkCancelDialog(int32_t mode, NSView* pview, const char* ptitle, const char* ptext) :
			mode_(mode)
			,title(ptitle ? ptitle : "")
			,text(ptext ? ptext : "")
			,view(pview)
		{
		}

		int32_t MP_STDCALL SetTitle(const char* ptext) override
		{
			title = ptext ? ptext : "";
			return gmpi::MP_OK;
		}
		int32_t MP_STDCALL SetText(const char* ptext) override
		{
			text = ptext ? ptext : "";
			return gmpi::MP_OK;
		}

		int32_t MP_STDCALL ShowAsync(gmpi_gui::ICompletionCallback* returnCompletionHandler) override
		{
			NSAlert* alert = buildAlert();
			auto result = [alert runModal] == NSAlertFirstButtonReturn ? gmpi::MP_OK : gmpi::MP_CANCEL;
			returnCompletionHandler->OnComplete(result);
			return gmpi::MP_OK;
		}

		// new-API IStockDialog:
		gmpi::ReturnCode showAsync(gmpi::api::IUnknown* pcallback) override
		{
			gmpi::shared_ptr<gmpi::api::IUnknown> unknown;
			unknown.attach(pcallback);
			auto dialogCallback = unknown.as<gmpi::api::IStockDialogCallback>();
			if (!dialogCallback)
				return gmpi::ReturnCode::Fail;

			NSAlert* alert = buildAlert();

			auto prevent_release = dialogCallback;
			auto type = static_cast<gmpi::api::StockDialogType>(mode_);
			[alert beginSheetModalForWindow:[view window] completionHandler:^(NSModalResponse returnCode) {
				gmpi::api::StockDialogButton button{};
				switch (type)
				{
				case gmpi::api::StockDialogType::Ok:
					button = gmpi::api::StockDialogButton::Ok;
					break;
				case gmpi::api::StockDialogType::OkCancel:
					button = (returnCode == NSAlertFirstButtonReturn)
						? gmpi::api::StockDialogButton::Ok
						: gmpi::api::StockDialogButton::Cancel;
					break;
				case gmpi::api::StockDialogType::YesNo:
					button = (returnCode == NSAlertFirstButtonReturn)
						? gmpi::api::StockDialogButton::Yes
						: gmpi::api::StockDialogButton::No;
					break;
				case gmpi::api::StockDialogType::YesNoCancel:
					if (returnCode == NSAlertFirstButtonReturn)
						button = gmpi::api::StockDialogButton::Yes;
					else if (returnCode == NSAlertSecondButtonReturn)
						button = gmpi::api::StockDialogButton::No;
					else
						button = gmpi::api::StockDialogButton::Cancel;
					break;
				}
				prevent_release->onComplete(button);
			}];

			return gmpi::ReturnCode::Ok;
		}

		// legacy queryInterface (gmpi::MpGuid&)
		int32_t MP_STDCALL queryInterface(const gmpi::MpGuid& iid, void** returnInterface) override
		{
			*returnInterface = nullptr;
			if (iid == gmpi_gui::SE_IID_GRAPHICS_OK_CANCEL_DIALOG || iid == gmpi::MP_IID_UNKNOWN)
			{
				*returnInterface = static_cast<gmpi_gui::IMpOkCancelDialog*>(this);
				addRef();
				return gmpi::MP_OK;
			}
			if (iid == *reinterpret_cast<const gmpi::MpGuid*>(&gmpi::api::IStockDialog::guid))
			{
				*returnInterface = static_cast<gmpi::api::IStockDialog*>(this);
				addRef();
				return gmpi::MP_OK;
			}
			return gmpi::MP_NOSUPPORT;
		}

		// new-API queryInterface (gmpi::api::Guid*)
		gmpi::ReturnCode queryInterface(const gmpi::api::Guid* iid, void** returnInterface) override
		{
			*returnInterface = nullptr;
			if (*iid == gmpi::api::IStockDialog::guid || *iid == gmpi::api::IUnknown::guid)
			{
				*returnInterface = static_cast<gmpi::api::IStockDialog*>(this);
				addRef();
				return gmpi::ReturnCode::Ok;
			}
			if (*iid == gmpi_gui::legacy::IMpOkCancelDialog::guid)
			{
				*returnInterface = static_cast<gmpi_gui::IMpOkCancelDialog*>(this);
				addRef();
				return gmpi::ReturnCode::Ok;
			}
			return gmpi::ReturnCode::NoSupport;
		}

		GMPI_REFCOUNT
	};

#ifdef STANDALONE
	// C++ facade for plugin to interact with the Mac View
	class DrawingFrameCocoa : public gmpi_gui::IMpGraphicsHost, public gmpi::IMpUserInterfaceHost2
	{
		gmpi::cocoa::DrawingFactory DrawingFactory;
		gmpi_sdk::mp_shared_ptr<gmpi::IMpUserInterface> pluginParametersLegacy;
		gmpi_sdk::mp_shared_ptr<gmpi::IMpUserInterface2> pluginParameters;
		gmpi_sdk::mp_shared_ptr<gmpi_gui_api::IMpGraphics> pluginGraphics;

	public:
		NSView* view;
		DrawingFrameCocoa()
		{

		}
		/*
		void Init(Json::Value* context, class IGuiHost2* hostPatchManager, int pviewType)
		{
			controller = hostPatchManager;

			//        AddView(new SE2::Container View());
			containerView.Attach(new SE2::Container View());
			containerView->setHost(static_cast<gmpi_gui::IMpGraphicsHost*>(this));

			containerView->setDocument(context, hostPatchManager, pviewType);

			// remainder should mimic standard GUI module initialisation.
			containerView->initialize();

			GmpiDrawing::Size avail(20000, 20000);
			GmpiDrawing::Size desired;
			containerView->measure(avail, &desired);
			containerView->arrange(GmpiDrawing::Rect(0, 0, desired.width, desired.height));
		}
		*/
		void attachModule(gmpi::IMpUnknown* object)
		{
			auto r = object->queryInterface(gmpi::MP_IID_GUI_PLUGIN2, pluginParameters.asIMpUnknownPtr());
			r = object->queryInterface(gmpi_gui_api::SE_IID_GRAPHICS_MPGUI, pluginGraphics.asIMpUnknownPtr());

			if (pluginParameters.get() != nullptr)
				pluginParameters->setHost(static_cast<gmpi::IMpUserInterfaceHost2*>(this));

			if (pluginGraphics.get() != nullptr)
			{
				// future: pluginGraphics->setHost(static_cast<gmpi_gui::IMpGraphicsHost*>(this));
				dynamic_cast<GmpiTestGui*>(object)->setHost(static_cast<gmpi_gui::IMpGraphicsHost*>(this));
			}
		}
		/*
			inline SE2::Conta inerView* getView()
			{
				return containerView.get();
			}
		  */
		  // PARENT NSVIEW CALL PLUGIN
		void arrange(GmpiDrawing_API::MP1_RECT r)
		{

		}

		void OnRender(NSView* frame, GmpiDrawing_API::MP1_RECT* dirtyRect)
		{
			gmpi::cocoa::DrawingFactory cocoafactory;
			gmpi::cocoa::GraphicsContext2 context(frame, &cocoafactory);

			context.PushAxisAlignedClip(dirtyRect);

			pluginGraphics->OnRender(static_cast<GmpiDrawing_API::IMpDeviceContext*>(&context));

			context.PopAxisAlignedClip();
		}

		void onPointerDown(int32_t flags, GmpiDrawing_API::MP1_POINT point)
		{
			pluginGraphics->onPointerDown(flags, point);
		}

		void onPointerMove(int32_t flags, GmpiDrawing_API::MP1_POINT point)
		{
			pluginGraphics->onPointerMove(flags, point);
		}

		void onPointerUp(int32_t flags, GmpiDrawing_API::MP1_POINT point)
		{
			pluginGraphics->onPointerUp(flags, point);
		}


		// PLUGIN CALLING HOST.

		// Inherited via IMpUserInterfaceHost2
		int32_t MP_STDCALL pinTransmit(int32_t pinId, int32_t size, const void * data, int32_t voice = 0) override
		{
			//TODO         assert(false); // not implemented.
			return gmpi::MP_FAIL;
		}
		int32_t MP_STDCALL createPinIterator(gmpi::IMpPinIterator** returnIterator) override
		{
			//TODO         assert(false); // not implemented.
			return gmpi::MP_FAIL;
		}
		int32_t MP_STDCALL getHandle(int32_t & returnValue) override
		{
			//TODO        assert(false); // not implemented.
			return gmpi::MP_FAIL;
		}
		int32_t MP_STDCALL sendMessageToAudio(int32_t id, int32_t size, const void * messageData) override
		{
			//TODO        assert(false); // not implemented.
			return gmpi::MP_FAIL;
		}
		int32_t MP_STDCALL ClearResourceUris() override
		{
			//TODO         assert(false); // not implemented.
			return gmpi::MP_FAIL;
		}
		int32_t MP_STDCALL RegisterResourceUri(const char * resourceName, const char * resourceType, gmpi::IString* returnString) override
		{
			//TODO         assert(false); // not implemented.
			return gmpi::MP_FAIL;
		}
		int32_t MP_STDCALL OpenUri(const char * fullUri, gmpi::IProtectedFile2** returnStream) override
		{
			//TODO         assert(false); // not implemented.
			return gmpi::MP_FAIL;
		}
		int32_t MP_STDCALL FindResourceU(const char * resourceName, const char * resourceType, gmpi::IString* returnString) override
		{
			//TODO         assert(false); // not implemented.
			return gmpi::MP_FAIL;
		}

		// IMpGraphicsHost
		void MP_STDCALL invalidateRect(const GmpiDrawing_API::MP1_RECT * invalidRect) override
		{
			[view setNeedsDisplay : YES];
		}
		void MP_STDCALL invalidateMeasure() override
		{
			//TODO        assert(false); // not implemented.
		}
		int32_t MP_STDCALL setCapture() override
		{
			//TODO         assert(false); // not implemented.
			return gmpi::MP_FAIL;
		}
		int32_t MP_STDCALL getCapture(int32_t & returnValue) override
		{
			//TODO         assert(false); // not implemented.
			return gmpi::MP_FAIL;
		}
		int32_t MP_STDCALL releaseCapture() override
		{
			//TODO         assert(false); // not implemented.
			return gmpi::MP_FAIL;
		}

		int32_t MP_STDCALL GetDrawingFactory(GmpiDrawing_API::IMpFactory ** returnFactory) override
		{
			*returnFactory = &DrawingFactory;
			return gmpi::MP_OK;
		}

		int32_t MP_STDCALL createPlatformMenu(GmpiDrawing_API::MP1_RECT* rect, gmpi_gui::IMpPlatformMenu** returnMenu) override
		{
			auto newMenu = new PlatformMenu(view, rect);
			// Wrap new-API menu in adapter; cast is safe — vtable layout of both IMpPlatformMenu variants is identical.
			*returnMenu = reinterpret_cast<gmpi_gui::IMpPlatformMenu*>(new GmpiGuiHosting::LegacyMenuAdapter(newMenu));
			return gmpi::MP_OK;
		}

		int32_t MP_STDCALL createPlatformTextEdit(GmpiDrawing_API::MP1_RECT* rect, gmpi_gui::IMpPlatformText** returnTextEdit) override
		{
			*returnTextEdit = new PlatformTextEntry(/*observer*/ nullptr, view, rect);
			return gmpi::MP_OK;
		}

		int32_t MP_STDCALL createFileDialog(int32_t dialogType, gmpi_gui::IMpFileDialog** returnFileDialog) override
		{
			// PlatformFileDialog implements the new API; hand out its legacy adapter.
			*returnFileDialog = (new PlatformFileDialog(dialogType, view))->asLegacy();
			return gmpi::MP_OK;
		}

		int32_t MP_STDCALL createOkCancelDialog(int32_t dialogType, gmpi_gui::IMpOkCancelDialog** returnDialog) override
		{
			*returnDialog = new PlatformOkCancelDialog(dialogType, view);
			return gmpi::MP_OK;
		}

		// IUnknown methods
		int32_t MP_STDCALL queryInterface(const gmpi::MpGuid& iid, void** returnInterface) override
		{
			if (iid == gmpi::MP_IID_UI_HOST2)
			{
				// important to cast to correct vtable (ug_plugin3 has 2 vtables) before reinterpret cast
				*returnInterface = reinterpret_cast<void*>(static_cast<IMpUserInterfaceHost2*>(this));
				addRef();
				return gmpi::MP_OK;
			}

			if (iid == gmpi_gui::SE_IID_GRAPHICS_HOST || iid == gmpi_gui::SE_IID_GRAPHICS_HOST_BASE || iid == gmpi::MP_IID_UNKNOWN)
			{
				// important to cast to correct vtable (ug_plugin3 has 2 vtables) before reinterpret cast
				*returnInterface = reinterpret_cast<void*>(static_cast<IMpGraphicsHost*>(this));
				addRef();
				return gmpi::MP_OK;
			}

			*returnInterface = 0;
			return gmpi::MP_NOSUPPORT;
		}

		GMPI_REFCOUNT_NO_DELETE;
	};
#endif
} // namespace

#endif /* CocoaGuiHost_h */
