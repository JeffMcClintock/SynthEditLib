#pragma once
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

#include <memory>
#include "helpers/GmpiPluginEditor.h"
#include "mp_sdk_gui2.h"
#include "Drawing.h"
#include "GraphicsRedrawClient.h"
#include "GmpiUiToSDK3.h"

namespace gmpi_gui_api
{
	class IMpGraphics4;
}

class SDK3Adaptor;

// provide for running SynthEdit modules in a host that uses GMPI
class SDK3AdaptorClient : public gmpi_gui::IMpGraphicsHost, public gmpi::IMpUserInterfaceHost2, public legacy::IGraphicsRedrawClient
{
	friend class SDK3Adaptor;

public:
	SDK3Adaptor& gmpiEditor;

	gmpi_sdk::mp_shared_ptr<gmpi_gui_api::IMpGraphics4> gmpi_gui_client; // usually a ContainerView at the topmost level
	gmpi_sdk::mp_shared_ptr<gmpi_gui_api::IMpKeyClient> gmpi_key_client;
	gmpi_sdk::mp_shared_ptr<legacy::IGraphicsRedrawClient> frameUpdateClient;
	gmpi_sdk::mp_shared_ptr<gmpi::IMpUserInterface2B> pluginParameters2B;
	GmpiDrawing_API::MP1_RECT bounds{};

	gmpi_sdk::mp_shared_ptr<GmpiDrawing_API::IMpFactory2> sdk3Factory; // for SynthEdit.exe, use the hosts factory
	std::unique_ptr<se::GmpiToSDK3Factory> factoryAdaptor;  // for GMPI-UI, use a factory adaptor.

	GmpiDrawing_API::IMpFactory2* getSdk3Factory()
	{
		assert(sdk3Factory.get() || factoryAdaptor.get());

		if (sdk3Factory)
			return sdk3Factory.get();

		return factoryAdaptor.get();
	}

	SDK3AdaptorClient(SDK3Adaptor& editor) : gmpiEditor(editor) {}

	void setFactory(gmpi::api::IUnknown* nativeFactory)
	{
		// SynthEdit has a universal factory, if it's available use it.
		// !!! ERROR, synthedit will fail to return the sdk3 factory. factories now have no fallback behaviour. !!!
		// see mitigation in SDK3Adaptor::render()
		nativeFactory->queryInterface((const gmpi::api::Guid*)&GmpiDrawing_API::SE_IID_FACTORY2_MPGUI, sdk3Factory.asIMpUnknownPtr());

		// otherwise (in pure GMPI-UI), init the Factory adaptor instead.
		if (sdk3Factory.isNull())
		{
			gmpi::shared_ptr<gmpi::drawing::api::IFactory> gmpiUiFactory;
			nativeFactory->queryInterface(&gmpi::drawing::api::IFactory::guid, gmpiUiFactory.put_void());

			assert(!gmpiUiFactory.isNull()); // factory does not support GMPI-UI interface
			factoryAdaptor = std::make_unique<se::GmpiToSDK3Factory>(gmpiUiFactory);
		}

		/* no, done durring attach
		if (pluginParameters2B) // is already attached
		{
			pluginParameters2B->setHost(sdk3Factory.isNull() ? factoryAdaptor.get() : sdk3Factory.get());
		}
		*/
	}

	void attach(gmpi::IMpUnknown* pclient)
	{
// no, later.		assert(sdk3Factory.get() || factoryAdaptor.get()); // call setFactory() first.

		pclient->queryInterface(gmpi_gui_api::IMpGraphics4::guid, gmpi_gui_client.asIMpUnknownPtr());
		pclient->queryInterface(gmpi_gui_api::IMpKeyClient::guid, gmpi_key_client.asIMpUnknownPtr());
		pclient->queryInterface(legacy::IGraphicsRedrawClient::guid, frameUpdateClient.asIMpUnknownPtr());
		[[maybe_unused]] auto r = pclient->queryInterface(gmpi::MP_IID_GUI_PLUGIN2B, pluginParameters2B.asIMpUnknownPtr());

		gmpi_sdk::mp_shared_ptr<gmpi::IMpUserInterface2> pinHost;
		gmpi_gui_client->queryInterface(gmpi::MP_IID_GUI_PLUGIN2, pinHost.asIMpUnknownPtr());

		if (pinHost)
		{
			pinHost->setHost(static_cast<gmpi_gui::IMpGraphicsHost*>(this));
			pinHost->initialize();
		}
	}

	void detach()
	{
		frameUpdateClient = {};
		gmpi_key_client = {};
		gmpi_gui_client = {};
		pluginParameters2B = {};
	}

	// IGraphicsRedrawClient
	void preGraphicsRedraw() override
	{
		if (frameUpdateClient)
			frameUpdateClient->preGraphicsRedraw();
	}

	int32_t measure(float availableWidth, float availableHeight, float& returnWidth, float& returnHeight)
	{
		GmpiDrawing_API::MP1_SIZE returnDesiredSize{availableWidth, availableHeight};

		if (gmpi_gui_client)
			gmpi_gui_client->measure({ availableWidth, availableHeight }, &returnDesiredSize);

		returnWidth = returnDesiredSize.width;
		returnHeight = returnDesiredSize.height;

		return gmpi::MP_OK;
	}

	int32_t arrange(float left, float top, float right, float bottom)
	{
		bounds = { left, top, right, bottom };

		if (gmpi_gui_client)
			return gmpi_gui_client->arrange({ left, top, right, bottom });

		return gmpi::MP_OK;
	}

	int32_t getClipArea(float& left, float& top, float& right, float& bottom)
	{
		int32_t res = gmpi::MP_OK;

		GmpiDrawing_API::MP1_RECT clipRect{ bounds };

		if(gmpi_gui_client)
		{
			res = gmpi_gui_client->getClipArea(&clipRect);
		}

		left = clipRect.left;
		top = clipRect.top;
		right = clipRect.right;
		bottom = clipRect.bottom;

		return res;
	}
	int32_t render(gmpi::api::IUnknown* unknown);
	int32_t onPointerDown(float x, float y, int32_t flags)
	{
		if (!gmpi_gui_client)
			return gmpi::MP_UNHANDLED;

		return gmpi_gui_client->onPointerDown(flags, { x, y });
	}
	int32_t onPointerMove(float x, float y, int32_t flags)
	{
		if(!gmpi_gui_client)
			return gmpi::MP_UNHANDLED;

		return gmpi_gui_client->onPointerMove(flags, { x, y });
	}
	int32_t onPointerUp(float x, float y, int32_t flags)
	{
		if (!gmpi_gui_client)
			return gmpi::MP_UNHANDLED;

		return gmpi_gui_client->onPointerUp(flags, { x, y });
	}

	// IMpGraphicsHostBase
	int32_t MP_STDCALL createFileDialog(int32_t dialogType, gmpi_gui::IMpFileDialog** returnFileDialog) override;

	// IMpGraphicsHost
	int32_t MP_STDCALL GetDrawingFactory(GmpiDrawing_API::IMpFactory** returnFactory) override;
	void MP_STDCALL invalidateRect(const GmpiDrawing_API::MP1_RECT* invalidRect) override;
	void MP_STDCALL invalidateMeasure() override;
	int32_t MP_STDCALL setCapture() override;
	int32_t MP_STDCALL getCapture(int32_t& returnValue) override;
	int32_t MP_STDCALL releaseCapture() override;
	int32_t MP_STDCALL createPlatformMenu(GmpiDrawing_API::MP1_RECT* rect, gmpi_gui::IMpPlatformMenu** returnMenu) override;
	int32_t MP_STDCALL createPlatformTextEdit(GmpiDrawing_API::MP1_RECT* rect, gmpi_gui::IMpPlatformText** returnTextEdit) override;
	int32_t MP_STDCALL createOkCancelDialog(int32_t dialogType, gmpi_gui::IMpOkCancelDialog** returnDialog) override;

	// IMpUserInterfaceHost2
	int32_t MP_STDCALL pinTransmit(int32_t pinId, int32_t size, const void* data, int32_t voice = 0) override { return gmpi::MP_OK; }
	int32_t MP_STDCALL createPinIterator(gmpi::IMpPinIterator** returnIterator) override { return gmpi::MP_OK; }
	int32_t MP_STDCALL getHandle(int32_t& returnValue) override { return gmpi::MP_OK; }
	int32_t MP_STDCALL sendMessageToAudio(int32_t id, int32_t size, const void* messageData) override { return gmpi::MP_OK; }
	int32_t MP_STDCALL ClearResourceUris() override { return gmpi::MP_OK; }
	int32_t MP_STDCALL RegisterResourceUri(const char* resourceName, const char* resourceType, gmpi::IString* returnString) override { return gmpi::MP_OK; }
	int32_t MP_STDCALL OpenUri(const char* fullUri, gmpi::IProtectedFile2** returnStream) override { return gmpi::MP_OK; }
	int32_t MP_STDCALL FindResourceU(const char* resourceName, const char* resourceType, gmpi::IString* returnString) override { return gmpi::MP_OK; }
	int32_t MP_STDCALL LoadPresetFile_DEPRECATED(const char* presetFilePath) override { return gmpi::MP_OK; }

	int32_t queryInterface(const gmpi::MpGuid& iid, void** returnInterface) override;
	GMPI_REFCOUNT_NO_DELETE;
};

// provide for running SynthEdit modules in a host that uses GMPI
class SDK3Adaptor : public gmpi::editor::PluginEditor, public gmpi::api::IGraphicsRedrawClient
{
public:
	SDK3AdaptorClient client;

	SDK3Adaptor() : client(*this){}
	~SDK3Adaptor();
	gmpi::ReturnCode setHost(gmpi::api::IUnknown* phost) override;

	void attachClient(gmpi::IMpUnknown* pcontainerView);

	// IDrawingClient
	gmpi::ReturnCode open(gmpi::api::IUnknown* host) override;
	gmpi::ReturnCode measure(const gmpi::drawing::Size* availableSize, gmpi::drawing::Size* returnDesiredSize) override;
	gmpi::ReturnCode arrange(const gmpi::drawing::Rect* finalRect) override;
	gmpi::ReturnCode render(gmpi::drawing::api::IDeviceContext* drawingContext) override;
	gmpi::ReturnCode getClipArea(gmpi::drawing::Rect* returnRect) override;

	// IGraphicsRedrawClient
	void preGraphicsRedraw() override;

	////////////////////////////////////
	gmpi::ReturnCode onPointerDown(gmpi::drawing::Point point, int32_t flags) override;
	gmpi::ReturnCode onPointerMove(gmpi::drawing::Point point, int32_t flags) override;
	gmpi::ReturnCode onPointerUp(gmpi::drawing::Point point, int32_t flags) override;
	gmpi::ReturnCode onMouseWheel(gmpi::drawing::Point point, int32_t flags, int32_t delta) override;

	gmpi::ReturnCode onKeyPress(wchar_t c) override;

	// right-click menu
	gmpi::ReturnCode populateContextMenu(gmpi::drawing::Point point, gmpi::api::IUnknown* contextMenuItemsSink) override;
	gmpi::ReturnCode onContextMenu(int32_t idx) override;
#if 0
	// gmpi_gui::IMpGraphicsHostBase
	int32_t GetDrawingFactory(GmpiDrawing_API::IMpFactory** returnFactory) override
	{
		return getGuiHost()->GetDrawingFactory(returnFactory);
	}

	void invalidateRect(const GmpiDrawing_API::MP1_RECT* invalidRect) override
	{
		getGuiHost()->invalidateRect(invalidRect);
}
	void invalidateMeasure() override
	{
	}

	int32_t setCapture() override
	{
		return getGuiHost()->setCapture();
	}
	int32_t getCapture(int32_t& returnValue) override
	{
		return getGuiHost()->getCapture(returnValue);
	}
	int32_t releaseCapture() override
	{
		return getGuiHost()->releaseCapture();
	}
	int32_t createPlatformMenu(GmpiDrawing_API::MP1_RECT* rect, gmpi_gui::IMpPlatformMenu** returnMenu) override
	{
		return gmpi::MP_UNHANDLED;
	}
	int32_t createPlatformTextEdit(GmpiDrawing_API::MP1_RECT* rect, gmpi_gui::IMpPlatformText** returnTextEdit) override
	{
		return gmpi::MP_UNHANDLED;
	}
	int32_t createOkCancelDialog(int32_t dialogType, gmpi_gui::IMpOkCancelDialog** returnDialog)
	{
		return gmpi::MP_UNHANDLED;
	}
	int32_t createFileDialog(int32_t dialogType, gmpi_gui::IMpFileDialog** returnFileDialog) override
	{
		return gmpi::MP_UNHANDLED;
	}

	//	int32_t queryInterface(const gmpi::MpGuid& iid, void** returnInterface) override
	ReturnCode queryInterface(const gmpi::api::Guid* iid, void** returnInterface) override
	{
		*returnInterface = nullptr;

		if (iid == IMpGraphicsHost::IID() || iid == gmpi_gui::SE_IID_GRAPHICS_HOST_BASE)
		{
			*returnInterface = static_cast<IMpGraphicsHost*>(this);
			addRef();
			return ReturnCode::Ok;
		}

		return PluginEditor::queryInterface(iid, returnInterface);
	}
	int32_t addRef() override
	{
		return PluginEditor::addRef();
	}
	int32_t release() override
	{
		return PluginEditor::release();
	}
#endif

	gmpi::ReturnCode queryInterface(const gmpi::api::Guid* iid, void** returnInterface) override
	{
		if ((*iid) == gmpi::api::IGraphicsRedrawClient::guid || (*iid) == gmpi::api::IUnknown::guid)
		{
			*returnInterface = static_cast<gmpi::api::IGraphicsRedrawClient*>(this);
			gmpi::editor::PluginEditor::addRef();
			return gmpi::ReturnCode::Ok;
		}

		return gmpi::editor::PluginEditor::queryInterface(iid, returnInterface);
	}
	int32_t addRef() override
	{
		return gmpi::editor::PluginEditor::addRef();
	}
	int32_t release() override
	{
		return gmpi::editor::PluginEditor::release();
	}
};
