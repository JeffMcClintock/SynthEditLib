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
