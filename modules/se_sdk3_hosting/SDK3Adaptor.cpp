/* Copyright (c) 2007-2023 SynthEdit Ltd

Permission to use, copy, modify, and /or distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THIS SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS.IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/

#include "SDK3Adaptor.h"
#include "GmpiUiToSDK3.h"
#include "mp_sdk_gui.h"

using namespace gmpi;
using namespace gmpi::editor;
using namespace gmpi::drawing;

SDK3Adaptor::~SDK3Adaptor()
{
	client.detach();
}

ReturnCode SDK3Adaptor::setHost(gmpi::api::IUnknown* phost)
{
    /* moved to open()

	gmpi::editor::PluginEditor::setHost(phost);

	gmpi::shared_ptr<gmpi::api::IUnknown> nativeFactory;
	drawingHost->getDrawingFactory(nativeFactory.put());

	client.setFactory(nativeFactory.get());
*/
	return ReturnCode::Ok;
}

void SDK3Adaptor::attachClient(gmpi::IMpUnknown* pclient)
{
	client.attach(/*factory.get(),*/ pclient);

	// have we been arranged already? bring client up to speed.
	if(bounds.right != 0)
	{
		float width{};
		float height{};
		client.measure(bounds.right - bounds.left, bounds.bottom - bounds.top, width, height);
		client.arrange(bounds.left, bounds.top, bounds.right, bounds.bottom);
	}
}

// IDrawingClient
ReturnCode SDK3Adaptor::open(gmpi::api::IUnknown* host)
{
    gmpi::editor::PluginEditor::setHost(host);

    gmpi::shared_ptr<gmpi::api::IUnknown> nativeFactory;
    drawingHost->getDrawingFactory(nativeFactory.put());

    client.setFactory(nativeFactory.get());
    
	return ReturnCode::Ok;
}

ReturnCode SDK3Adaptor::measure(const gmpi::drawing::Size* availableSize, gmpi::drawing::Size* returnDesiredSize)
{
	return (ReturnCode) client.measure(availableSize->width, availableSize->height, returnDesiredSize->width, returnDesiredSize->height);
}

ReturnCode SDK3Adaptor::arrange(const gmpi::drawing::Rect* finalRect)
{
	gmpi::editor::PluginEditor::arrange(finalRect);
	return (ReturnCode) client.arrange(finalRect->left, finalRect->top, finalRect->right, finalRect->bottom);
}

ReturnCode SDK3Adaptor::getClipArea(drawing::Rect* returnRect)
{
	return (ReturnCode) client.getClipArea(returnRect->left, returnRect->top, returnRect->right, returnRect->bottom);
}

void SDK3Adaptor::preGraphicsRedraw()
{
	client.preGraphicsRedraw();
}

gmpi::ReturnCode SDK3Adaptor::onPointerDown(gmpi::drawing::Point point, int32_t flags)
{
	return (ReturnCode) client.onPointerDown(point.x, point.y, flags);
}

gmpi::ReturnCode SDK3Adaptor::onPointerMove(gmpi::drawing::Point point, int32_t flags)
{
	return (ReturnCode)client.onPointerMove(point.x, point.y, flags);
}

gmpi::ReturnCode SDK3Adaptor::onPointerUp(gmpi::drawing::Point point, int32_t flags)
{
	return (ReturnCode)client.onPointerUp(point.x, point.y, flags);
}

gmpi::ReturnCode SDK3Adaptor::OnKeyPress(wchar_t c)
{
	return (ReturnCode)client.gmpi_key_client->OnKeyPress(c);
}

gmpi::ReturnCode SDK3Adaptor::onMouseWheel(gmpi::drawing::Point point, int32_t flags, int32_t delta)
{
	if (!client.gmpi_gui_client)
		return ReturnCode::Unhandled;

	return (ReturnCode) client.gmpi_gui_client->onMouseWheel(flags, delta, { point.x, point.y });
}

// right-click menu
gmpi::ReturnCode SDK3Adaptor::populateContextMenu(gmpi::drawing::Point point, gmpi::api::IUnknown* contextMenuItemsSink)
{
	return (ReturnCode)client.pluginParameters2B->populateContextMenu(point.x, point.y, (gmpi::IMpUnknown*) contextMenuItemsSink);
}

gmpi::ReturnCode SDK3Adaptor::onContextMenu(int32_t idx)
{
	return (ReturnCode)client.pluginParameters2B->onContextMenu(idx);
}

ReturnCode SDK3Adaptor::render(gmpi::drawing::api::IDeviceContext* drawingContext)
{
	Graphics g(drawingContext);
	ClipDrawingToBounds x(g, bounds);

	if(!client.gmpi_gui_client)
	{
		g.clear(Colors::Red);
		return ReturnCode::Ok;
	}

	// SynthEdit as a host support both GMPI-UI and SynthEdit SDK graphics contexts, just need to query it.
	// !! using the method below will fail becuase we will sucessfully get the SDK3 context, but will be using it with a GMPI-UI factory
	// due to the issue in setFactory() (in header)
	// kind of pointless complication, SE plugin is not going to be used much inside synthedit anyhow.
	if (client.sdk3Factory) //getSdk3Factory())
	{
		GmpiDrawing_API::IMpDeviceContext* sdk3DrawingContext{};
		((gmpi::IMpUnknown*)drawingContext)->queryInterface(GmpiDrawing_API::SE_IID_DEVICECONTEXT_MPGUI, (void**)&sdk3DrawingContext);

		if (sdk3DrawingContext)
		{
			return (ReturnCode)client.gmpi_gui_client->OnRender(sdk3DrawingContext);
		}
	}

	// The VST3 wrapper as a host does NOT support SynthEdit SDK graphics contexts, we need to create a universal context.
	assert(client.getSdk3Factory());
	se::UniversalGraphicsContext2 context(client.getSdk3Factory(), drawingContext);

	return (ReturnCode) client.gmpi_gui_client->OnRender(static_cast<GmpiDrawing_API::IMpDeviceContext*>(&context.sdk3Context));
}

// IMpGraphicsHost
int32_t MP_STDCALL SDK3AdaptorClient::GetDrawingFactory(GmpiDrawing_API::IMpFactory** returnFactory)
{
	return getSdk3Factory()->queryInterface(GmpiDrawing_API::SE_IID_FACTORY_MPGUI, (void**)returnFactory);
}

void MP_STDCALL SDK3AdaptorClient::invalidateRect(const GmpiDrawing_API::MP1_RECT* invalidRect)
{
	if(gmpiEditor.drawingHost)
		gmpiEditor.drawingHost->invalidateRect((const gmpi::drawing::Rect*) invalidRect);
}
void MP_STDCALL SDK3AdaptorClient::invalidateMeasure() {};
int32_t MP_STDCALL SDK3AdaptorClient::setCapture() { return (int32_t) gmpiEditor.inputHost->setCapture();}
int32_t MP_STDCALL SDK3AdaptorClient::getCapture(int32_t& returnValue) { bool ret{}; gmpiEditor.inputHost->getCapture(ret); return ret ? gmpi::MP_OK : gmpi::MP_FAIL;}
int32_t MP_STDCALL SDK3AdaptorClient::releaseCapture() { return (int32_t) gmpiEditor.inputHost->releaseCapture();}
int32_t MP_STDCALL SDK3AdaptorClient::createPlatformMenu(GmpiDrawing_API::MP1_RECT* rect, gmpi_gui::IMpPlatformMenu** returnMenu)
{
	gmpi::shared_ptr<gmpi::api::IUnknown> unk;
	gmpiEditor.dialogHost->createPopupMenu((gmpi::drawing::Rect*)rect, unk.put());

	return (int32_t)unk->queryInterface((const gmpi::api::Guid*)&gmpi_gui::SE_IID_GRAPHICS_PLATFORM_MENU, (void**)returnMenu);
}
int32_t MP_STDCALL SDK3AdaptorClient::createPlatformTextEdit(GmpiDrawing_API::MP1_RECT* rect, gmpi_gui::IMpPlatformText** returnTextEdit)
{
	gmpi::shared_ptr<gmpi::api::IUnknown> unk;
	gmpiEditor.dialogHost->createTextEdit((gmpi::drawing::Rect*)rect, unk.put());

	return (int32_t) unk->queryInterface((const gmpi::api::Guid*) &gmpi_gui::SE_IID_GRAPHICS_PLATFORM_TEXT, (void**)returnTextEdit);
}
int32_t MP_STDCALL SDK3AdaptorClient::createOkCancelDialog(int32_t dialogType, gmpi_gui::IMpOkCancelDialog** returnDialog)
{
	gmpi::shared_ptr<gmpi::api::IUnknown> unk;
	gmpiEditor.dialogHost->createStockDialog(0, unk.put());

	return (int32_t)unk->queryInterface((const gmpi::api::Guid*)&gmpi_gui::SE_IID_GRAPHICS_OK_CANCEL_DIALOG, (void**)returnDialog);
}

int32_t SDK3AdaptorClient::queryInterface(const gmpi::MpGuid& iid, void** returnInterface)
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

	if (iid == (const gmpi::MpGuid&)legacy::IGraphicsRedrawClient::guid)
	{
		//// cheat a bit, reply on GMPI-UI and SDK3 having exact same interface and guid for this.
		//return (int32_t) gmpiEditor.drawingHost->queryInterface((const gmpi::api::Guid*)&iid, returnInterface);

		*returnInterface = reinterpret_cast<void*>(static_cast<legacy::IGraphicsRedrawClient*>(this));
		addRef();
		return gmpi::MP_OK;
	}

	// calls looking for the GMPI-UI host, should be redirected to the gmpiEditor
	if (iid == (const gmpi::MpGuid&)gmpi::api::IDialogHost::guid)
	{
		return (int32_t)gmpiEditor.dialogHost->queryInterface((const gmpi::api::Guid*)&iid, returnInterface);
	}

	if (iid == (const gmpi::MpGuid&)gmpi::api::IDrawingHost::guid)
	{
		return (int32_t)gmpiEditor.drawingHost->queryInterface((const gmpi::api::Guid*)&iid, returnInterface);
	}

	*returnInterface = 0;
	return gmpi::MP_NOSUPPORT;
}