#pragma once
#include <vector>
#include "Drawing.h"
#include "../se_sdk3_hosting/GraphicsRedrawClient.h"
#include "SDK3Adaptor.h"

// see also GmpiUiHelper (gmpi->moduleView)  SDK3Adaptor (wraps an SKD3 plugin in a GMPI API)

namespace SE2
{
struct hasGmpiUiChildren
{
	// GMPI child plugins
	std::vector<gmpi::shared_ptr<gmpi::api::IDrawingClient>> graphics_gmpi;
	std::vector<gmpi::shared_ptr<gmpi::api::IInputClient>> editors_gmpi;

	void addChild(gmpi::api::IUnknown* child, gmpi::api::IUnknown* host)
	{
		gmpi::shared_ptr<gmpi::api::IUnknown> unknown;
		unknown = (gmpi::api::IUnknown*)child;

		if (auto graphic = unknown.as<gmpi::api::IDrawingClient>(); graphic)
		{
			graphics_gmpi.push_back(graphic);
			graphic->open(host);
		}

		if (auto editor = unknown.as<gmpi::api::IInputClient>(); editor)
			editors_gmpi.push_back(editor);
	}
};

struct GmpiUiLayerHost :
	  public gmpi::api::IDrawingHost
	, public gmpi::api::IInputHost
	, public gmpi::api::IDialogHost
{
	struct GmpiUiLayer* owner{};
	gmpi::drawing::Size offset{};

	// IDrawingHost
	gmpi::ReturnCode getDrawingFactory(gmpi::api::IUnknown** returnFactory);
	void invalidateRect(const gmpi::drawing::Rect* invalidRect) override;
	void invalidateMeasure() override;
	float getRasterizationScale() override;

	// IInputHost
	gmpi::ReturnCode setCapture() override;
	gmpi::ReturnCode getCapture(bool& returnValue) override;
	gmpi::ReturnCode releaseCapture() override;
//	gmpi::ReturnCode getFocus() override;
//	gmpi::ReturnCode releaseFocus() override;

	// IDialogHost
	gmpi::ReturnCode createTextEdit(const gmpi::drawing::Rect* r, gmpi::api::IUnknown** returnTextEdit);
	gmpi::ReturnCode createPopupMenu(const gmpi::drawing::Rect* r, gmpi::api::IUnknown** returnPopupMenu);
	gmpi::ReturnCode createKeyListener(const gmpi::drawing::Rect* r, gmpi::api::IUnknown** returnKeyListener);
	gmpi::ReturnCode createFileDialog(int32_t dialogType, gmpi::api::IUnknown** returnDialog) override;
	gmpi::ReturnCode createStockDialog(int32_t dialogType, gmpi::api::IUnknown** returnDialog) override;

	gmpi::ReturnCode queryInterface(const gmpi::api::Guid* iid, void** returnInterface)
	{
		GMPI_QUERYINTERFACE(IDrawingHost);
		GMPI_QUERYINTERFACE(IDialogHost);
		GMPI_QUERYINTERFACE(IInputHost);
		return gmpi::ReturnCode::NoSupport;
	}

	GMPI_REFCOUNT_NO_DELETE
};

// test adorner layer. using pure GMPI-UI APIs
// next: make this forward all calls to the GMPI-UI children.
struct GmpiUiLayer :
	  public gmpi::api::IDrawingClient
	, public gmpi::api::IInputClient
{
	struct childInfo
	{
		GmpiUiLayerHost host;
		gmpi::drawing::Rect bounds;
		gmpi::shared_ptr<gmpi::api::IDrawingClient> graphic;
		gmpi::shared_ptr<gmpi::api::IInputClient> editor;

		childInfo(
			  GmpiUiLayer* powner
			, gmpi::drawing::Rect pbounds
			, gmpi::shared_ptr<gmpi::api::IDrawingClient> pgraphic
			, gmpi::shared_ptr<gmpi::api::IInputClient> peditor
			) :
			  bounds( pbounds )
			, graphic( pgraphic )
			, editor( peditor )
		{
			host.owner = powner;
		}
	};

	gmpi::shared_ptr<gmpi::api::IDrawingHost> drawingHost;
	gmpi::shared_ptr<gmpi::api::IInputHost> inputHost;
	gmpi::shared_ptr<gmpi::api::IDialogHost> dialogHost;

	std::vector<std::unique_ptr<childInfo>> children;

	std::function<gmpi::ReturnCode(wchar_t)> keyHandler;
	bool isMeasured{ false };

	GmpiUiLayer()
	{
	}
	//~GmpiUiLayer()
	//{
	//	int x = 9;
	//}
	void addChild(gmpi::api::IUnknown* newchild)
	{
		gmpi::shared_ptr<gmpi::api::IUnknown> unknown;
		unknown = (gmpi::api::IUnknown*) newchild;

		auto graphic = unknown.as<gmpi::api::IDrawingClient>();
		auto editor = unknown.as<gmpi::api::IInputClient>();

		auto info = std::make_unique<childInfo>(
			  this
			, gmpi::drawing::Rect{100,100,200,200}
			, graphic
			, editor
		);

		children.push_back( std::move(info) );

		if (graphic)
		{
			graphic->open(static_cast<gmpi::api::IDrawingHost*>(&children.back()->host));

			if (isMeasured)
			{
				auto child = children.back().get();
				const auto size = measureChild(child);
				child->bounds.right = child->bounds.left + size.width;
				child->bounds.bottom = child->bounds.top + size.height;
				arrangeChild(child);

				drawingHost->invalidateRect(&child->bounds);
			}
		}
	}

	void removeChild(gmpi::api::IUnknown* oldchild)
	{
		std::erase_if(children, [&](const std::unique_ptr<childInfo>& c)
			{
				return c->graphic.get() == oldchild || c->editor.get() == oldchild;
			}
		);
	}

	// IInputClient
	gmpi::ReturnCode setHover(bool isMouseOverMe) override
	{
		(void)isMouseOverMe;
		return gmpi::ReturnCode::Ok;
	}

	gmpi::ReturnCode hitTest(gmpi::drawing::Point point, int32_t flags) override
	{
		for (auto& it : children)
		{
			if (!pointInRect(point, (*it).bounds))
				continue;

			if ((*it).editor.get())
				return gmpi::ReturnCode::Ok;
		}

		return gmpi::ReturnCode::Unhandled;
	}

	gmpi::ReturnCode onPointerDown(gmpi::drawing::Point point, int32_t flags) override
	{
		(void)point; (void)flags;
		return gmpi::ReturnCode::Unhandled;
	}

	gmpi::ReturnCode onPointerMove(gmpi::drawing::Point point, int32_t flags) override
	{
		for (auto& it : children)
		{
			if (!pointInRect(point, (*it).bounds))
				continue;

			auto child = (*it).editor.get();
			if (!child)
				continue;

			gmpi::drawing::Point childPoint{ point.x - (*it).bounds.left, point.y - (*it).bounds.top };
			return child->onPointerMove(childPoint, flags);
		}

		return gmpi::ReturnCode::Unhandled;
	}

	gmpi::ReturnCode onPointerUp(gmpi::drawing::Point point, int32_t flags) override
	{
		(void)point; (void)flags;
		return gmpi::ReturnCode::Unhandled;
	}

	gmpi::ReturnCode onMouseWheel(gmpi::drawing::Point point, int32_t flags, int32_t delta) override
	{
		(void)point; (void)flags; (void)delta;
		return gmpi::ReturnCode::Unhandled;
	}

	gmpi::ReturnCode populateContextMenu(gmpi::drawing::Point point, gmpi::api::IUnknown* contextMenuItemsSink) override
	{
		(void)point; (void)contextMenuItemsSink;
		return gmpi::ReturnCode::Unhandled;
	}

	gmpi::ReturnCode onContextMenu(int32_t idx) override
	{
		(void)idx;
		return gmpi::ReturnCode::Unhandled;
	}

	gmpi::ReturnCode OnKeyPress(wchar_t c) override
	{
		if(keyHandler)
			return keyHandler(c);

		return gmpi::ReturnCode::Unhandled;
	}

	// IDrawingClient
	gmpi::ReturnCode open(gmpi::api::IUnknown* phost) override
	{
		gmpi::shared_ptr<gmpi::api::IUnknown> unknown;
		unknown = (gmpi::api::IUnknown*) phost;

		drawingHost = unknown.as<gmpi::api::IDrawingHost>();
		inputHost = unknown.as<gmpi::api::IInputHost>();
		dialogHost = unknown.as<gmpi::api::IDialogHost>();

		return gmpi::ReturnCode::Ok;
	}

	gmpi::drawing::Size measureChild(childInfo* child)
	{
		if (!child->graphic)
			return {};

		const gmpi::drawing::Size availableSize{10000,10000};
		gmpi::drawing::Size desiredSize{};
		child->graphic->measure(&availableSize, &desiredSize);

		return desiredSize;
	}

	void arrangeChild(childInfo* child)
	{
		if (!child->graphic)
			return;

		child->host.offset = { child->bounds.left, child->bounds.top };
		child->graphic->arrange(&child->bounds);
	}

	gmpi::ReturnCode measure(const gmpi::drawing::Size* availableSize, gmpi::drawing::Size* returnDesiredSize) override
	{
		(void)availableSize;

		assert(returnDesiredSize);

		// Minimal adornment area.
		returnDesiredSize->width = 100.0f;
		returnDesiredSize->height = 100.0f;

		for (auto& child : children)
		{
			measureChild(child.get());
		}

		isMeasured = true;

		return gmpi::ReturnCode::Ok;
	}

	gmpi::ReturnCode arrange(const gmpi::drawing::Rect* finalRect) override
	{
		for (auto& it : children)
		{
			auto& child = *it.get();

			const auto size = measureChild(&child);
			child.bounds.left = finalRect->left;
			child.bounds.top = finalRect->top;
			child.bounds.right = child.bounds.left + size.width;
			child.bounds.bottom = child.bounds.top + size.height;
			arrangeChild(&child);
		}

		return gmpi::ReturnCode::Ok;
	}

	gmpi::ReturnCode render(gmpi::drawing::api::IDeviceContext* drawingContext) override
	{
		assert(isMeasured);

		gmpi::drawing::Graphics g(drawingContext);

		// Restrict drawing only to overall clip-rect.
		const auto cliprect = g.getAxisAlignedClip();
		const auto originalTransform = g.getTransform();

		gmpi::drawing::Rect childClipRect;
		for (auto& it : children)
		{
			auto& child = *it.get();

			if (!child.graphic)
				continue;

			child.graphic->getClipArea(&childClipRect);
			if (empty(intersectRect(childClipRect, cliprect)))
				continue;

			auto adjustedTransform = gmpi::drawing::makeTranslation(child.bounds.left, child.bounds.top) * originalTransform;
			g.setTransform(adjustedTransform);

			child.graphic->render(drawingContext);
		}

		g.setTransform(originalTransform);
		return gmpi::ReturnCode::Ok;
	}

	gmpi::ReturnCode getClipArea(gmpi::drawing::Rect* returnRect) override
	{
		if (returnRect)
		{
			*returnRect = gmpi::drawing::Rect{ 0.0f, 0.0f, 100.0f, 100.0f };
		}
		return gmpi::ReturnCode::Ok;
	}

	gmpi::ReturnCode queryInterface(const gmpi::api::Guid* iid, void** returnInterface)
	{
		*returnInterface = {};
		GMPI_QUERYINTERFACE(IInputClient);
		GMPI_QUERYINTERFACE(IDrawingClient);
		return gmpi::ReturnCode::NoSupport;
	}

	GMPI_REFCOUNT
};


inline gmpi::ReturnCode GmpiUiLayerHost::getDrawingFactory(gmpi::api::IUnknown** returnFactory)
{
	return owner->drawingHost->getDrawingFactory(returnFactory);
}

inline void GmpiUiLayerHost::invalidateRect(const gmpi::drawing::Rect* invalidRect)
{
	// map to owners coord space.
	const auto r = offsetRect(*invalidRect, offset);
	owner->drawingHost->invalidateRect(&r);
}

inline void GmpiUiLayerHost::invalidateMeasure()
{
	return;
}

inline float GmpiUiLayerHost::getRasterizationScale()
{
	return 1.0f;
}

// IInputHost
inline gmpi::ReturnCode GmpiUiLayerHost::setCapture()
{
	return owner->inputHost->setCapture();
}

inline gmpi::ReturnCode GmpiUiLayerHost::getCapture(bool& returnValue)
{
	returnValue = false;
	return gmpi::ReturnCode::Ok;
}

inline gmpi::ReturnCode GmpiUiLayerHost::releaseCapture()
{
	return owner->inputHost->releaseCapture();
}

//inline gmpi::ReturnCode GmpiUiLayerHost::getFocus() // ???
//{
//	return gmpi::ReturnCode::Ok;
//}
//
//inline gmpi::ReturnCode GmpiUiLayerHost::releaseFocus()
//{
//	return gmpi::ReturnCode::Ok;
//}

// IDialogHost
inline gmpi::ReturnCode GmpiUiLayerHost::createTextEdit(const gmpi::drawing::Rect* r, gmpi::api::IUnknown** returnTextEdit)
{
	return owner->dialogHost->createTextEdit(r, returnTextEdit);
}
inline gmpi::ReturnCode GmpiUiLayerHost::createPopupMenu(const gmpi::drawing::Rect* r, gmpi::api::IUnknown** returnPopupMenu)
{
	return owner->dialogHost->createPopupMenu(r, returnPopupMenu);
}
inline gmpi::ReturnCode GmpiUiLayerHost::createKeyListener(const gmpi::drawing::Rect* r, gmpi::api::IUnknown** returnKeyListener)
{
	return owner->dialogHost->createKeyListener(r, returnKeyListener);
}
inline gmpi::ReturnCode GmpiUiLayerHost::createFileDialog(int32_t dialogType, gmpi::api::IUnknown** returnDialog)
{
	return owner->dialogHost->createFileDialog(dialogType, returnDialog);
}
inline gmpi::ReturnCode GmpiUiLayerHost::createStockDialog(int32_t dialogType, gmpi::api::IUnknown** returnDialog)
{
	return owner->dialogHost->createStockDialog(dialogType, returnDialog);
}

#if 0
// PileChildHost holds refcount of child plugins hosts. need to keep refcount seperate from Pile, else it never gets deleted.
struct PileChildHost :
															public gmpi_gui::IMpGraphicsHost
														, public gmpi::IMpUserInterfaceHost2
														, public gmpi::api::IDialogHost

{
	struct Pile* parent = nullptr;

	// PARENTING
	// IMpGraphicsHost
	int32_t MP_STDCALL createFileDialog(int32_t dialogType, gmpi_gui::IMpFileDialog** returnFileDialog) override { return 0; }
	int32_t MP_STDCALL GetDrawingFactory(GmpiDrawing_API::IMpFactory** returnFactory) override;
	void MP_STDCALL invalidateRect(const GmpiDrawing_API::MP1_RECT* invalidRect) override;
	void MP_STDCALL invalidateMeasure() override { return; };
	int32_t MP_STDCALL setCapture() override;
	int32_t MP_STDCALL getCapture(int32_t& returnValue) override;
	int32_t MP_STDCALL releaseCapture() override;
	int32_t MP_STDCALL createPlatformMenu(GmpiDrawing_API::MP1_RECT* rect, gmpi_gui::IMpPlatformMenu** returnMenu) override { return 0; }
	int32_t MP_STDCALL createPlatformTextEdit(GmpiDrawing_API::MP1_RECT* rect, gmpi_gui::IMpPlatformText** returnTextEdit) override { return 0; }
	int32_t MP_STDCALL createOkCancelDialog(int32_t dialogType, gmpi_gui::IMpOkCancelDialog** returnDialog) override { return 0; }

	// IMpUserInterfaceHost2
	int32_t MP_STDCALL pinTransmit(int32_t pinId, int32_t size, const void* data, int32_t voice = 0) override { return 0; }
	int32_t MP_STDCALL createPinIterator(gmpi::IMpPinIterator** returnIterator) override { return 0; }
	int32_t MP_STDCALL getHandle(int32_t& returnValue) override { return 0; }
	int32_t MP_STDCALL sendMessageToAudio(int32_t id, int32_t size, const void* messageData) override { return 0; }
	int32_t MP_STDCALL ClearResourceUris() override { return 0; }
	int32_t MP_STDCALL RegisterResourceUri(const char* resourceName, const char* resourceType, gmpi::IString* returnString) override { return 0; }
	int32_t MP_STDCALL OpenUri(const char* fullUri, gmpi::IProtectedFile2** returnStream) override { return 0; }
	int32_t MP_STDCALL FindResourceU(const char* resourceName, const char* resourceType, gmpi::IString* returnString) override { return 0; }
	int32_t MP_STDCALL LoadPresetFile_DEPRECATED(const char* presetFilePath) override { return 0; }

	// IDialogHost
	gmpi::ReturnCode createTextEdit(const gmpi::drawing::Rect* r, gmpi::api::IUnknown** returnTextEdit) override { return gmpi::ReturnCode::Ok; }
	gmpi::ReturnCode createPopupMenu(const gmpi::drawing::Rect* r, gmpi::api::IUnknown** returnPopupMenu) override { return gmpi::ReturnCode::Ok; }
	gmpi::ReturnCode createKeyListener(const gmpi::drawing::Rect* r, gmpi::api::IUnknown** returnKeyListener);
	gmpi::ReturnCode createFileDialog(int32_t dialogType, gmpi::api::IUnknown** returnDialog) override { return gmpi::ReturnCode::Ok; }
	gmpi::ReturnCode createStockDialog(int32_t dialogType, gmpi::api::IUnknown** returnDialog) override { return gmpi::ReturnCode::Ok; }


	int32_t queryInterface(const gmpi::MpGuid& iid, void** returnInterface) override
	{
		*returnInterface = nullptr;

		if (iid == gmpi_gui::IMpGraphicsHost::IID() || iid == gmpi::MP_IID_UNKNOWN)
		{
			*returnInterface = static_cast<gmpi_gui::IMpGraphicsHost*>(this);
			addRef();
			return gmpi::MP_OK;
		}

		if (iid == gmpi::MP_IID_UI_HOST2)
		{
			*returnInterface = static_cast<gmpi::IMpUserInterfaceHost2*>(this);
			addRef();
			return gmpi::MP_OK;
		}

		if (iid == *(gmpi::MpGuid*)&IDialogHost::guid)
		{
			*returnInterface = static_cast<IDialogHost*>(this);
			addRef();
			return gmpi::MP_OK;
		}

		return gmpi::MP_NOSUPPORT;
	}

	gmpi::ReturnCode queryInterface(const gmpi::api::Guid* iid, void** returnInterface)
	{
		GMPI_QUERYINTERFACE(IDialogHost);

		const gmpi::MpGuid& iid2 = *(const gmpi::MpGuid*)iid;
		return (gmpi::ReturnCode)queryInterface(iid2, returnInterface);
	}

	GMPI_REFCOUNT_NO_DELETE
};
#endif

struct PileChildHost2 :
	  public gmpi::api::IDialogHost
	, public gmpi::api::IDrawingHost
{
	struct Pile* parent = nullptr;

	// IDrawingHost
	gmpi::ReturnCode getDrawingFactory(gmpi::api::IUnknown** returnFactory) override;
	void invalidateRect(const gmpi::drawing::Rect* invalidRect) override;
	void invalidateMeasure() override;
	float getRasterizationScale() override { return 1.0f; } // DPI scaling

	// IDialogHost
	gmpi::ReturnCode createTextEdit(const gmpi::drawing::Rect* r, gmpi::api::IUnknown** returnTextEdit) override;
	gmpi::ReturnCode createPopupMenu(const gmpi::drawing::Rect* r, gmpi::api::IUnknown** returnPopupMenu) override;
	gmpi::ReturnCode createKeyListener(const gmpi::drawing::Rect* r, gmpi::api::IUnknown** returnKeyListener) override;
	gmpi::ReturnCode createFileDialog(int32_t dialogType, gmpi::api::IUnknown** returnDialog) override;
	gmpi::ReturnCode createStockDialog(int32_t dialogType, gmpi::api::IUnknown** returnDialog) override;

	gmpi::ReturnCode queryInterface(const gmpi::api::Guid* iid, void** returnInterface)
	{
		*returnInterface = {};
		GMPI_QUERYINTERFACE(IDialogHost);
		GMPI_QUERYINTERFACE(IDrawingHost);
		return gmpi::ReturnCode::NoSupport;
	}

	GMPI_REFCOUNT_NO_DELETE
};

// stacks a set of child elements on top of each other.
struct Pile :
	  public gmpi::api::IDrawingClient
	, public gmpi::api::IGraphicsRedrawClient
	, public gmpi::api::IInputClient
{
	gmpi::shared_ptr<gmpi::api::IInputHost> inputHost;
	gmpi::shared_ptr<gmpi::api::IDialogHost> dialogHost;
	gmpi::shared_ptr<gmpi::api::IDrawingHost> drawingHost;

	PileChildHost2 childhost_gmpi;

	// child plugins.
	std::vector<gmpi::shared_ptr<gmpi::api::IDrawingClient>> graphics_gmpi;
	std::vector<gmpi::shared_ptr<gmpi::api::IInputClient>> editors_gmpi;
	std::vector<gmpi::shared_ptr<IGraphicsRedrawClient>> redrawers_gmpi;

	gmpi::drawing::Rect bounds{};
	gmpi::drawing::Point lastPoint{};
	int32_t currentMouseLayer = -1;
	int32_t capturedLayer = -2;
	
	Pile()
	{
		childhost_gmpi.parent = this;
	}

	void addChild(gmpi::api::IUnknown* child)
	{
		// SDK3 child plugins.
		auto maybeSdk3 = (gmpi::IMpUnknown*) child;
		gmpi_sdk::mp_shared_ptr<gmpi::IMpUserInterface2> sdk3_editor;
		gmpi_sdk::mp_shared_ptr<gmpi_gui_api::IMpGraphics3> sdk3_graphic;
		gmpi_sdk::mp_shared_ptr<IGraphicsRedrawClient> sdk3_redraw;

		maybeSdk3->queryInterface(gmpi_gui_api::IMpGraphics3::guid, sdk3_graphic.asIMpUnknownPtr());
		maybeSdk3->queryInterface(gmpi::MP_IID_GUI_PLUGIN2, sdk3_editor.asIMpUnknownPtr());
		maybeSdk3->queryInterface(legacy::IGraphicsRedrawClient::guid, sdk3_redraw.asIMpUnknownPtr());

		gmpi::shared_ptr<gmpi::api::IUnknown> unknown;

		if (sdk3_editor || sdk3_graphic || sdk3_redraw)
		{
			auto wrapper = new SDK3Adaptor();
			unknown.attach(static_cast<gmpi::api::IDrawingClient*>(wrapper));

			wrapper->attachClient(maybeSdk3);

			child = static_cast<gmpi::api::IDrawingClient*>(wrapper);
		}
		else
		{
			unknown = child;
		}

		if (auto graphic = unknown.as<gmpi::api::IDrawingClient>(); graphic)
		{
			graphics_gmpi.push_back(graphic);

			if(drawingHost)
				graphic->open(drawingHost.get());
		}

		if (auto editor = unknown.as<gmpi::api::IInputClient>(); editor)
			editors_gmpi.push_back(editor);

		if (auto redraw = unknown.as<IGraphicsRedrawClient>(); redraw)
			redrawers_gmpi.push_back(redraw);
	}

	// IGraphicsRedrawClient
	void preGraphicsRedraw() override
	{
		for (auto& graphic : redrawers_gmpi)
		{
			graphic->preGraphicsRedraw();
		}		
	}

	// IDrawingClient
	gmpi::ReturnCode open(gmpi::api::IUnknown* phost) override
	{
		gmpi::shared_ptr<gmpi::api::IUnknown> unknown(phost);

		inputHost = unknown.as<gmpi::api::IInputHost>();
		drawingHost = unknown.as<gmpi::api::IDrawingHost>();
		dialogHost = unknown.as<gmpi::api::IDialogHost>();

		for(auto& graphic : graphics_gmpi)
		{
			graphic->open(drawingHost.get());
		}

		return gmpi::ReturnCode::Ok;
	}

	gmpi::ReturnCode measure(const gmpi::drawing::Size* availableSize, gmpi::drawing::Size* returnDesiredSize) override
	{
		bool first = true;

		for (auto& child : graphics_gmpi)
		{
			gmpi::drawing::Size s{};
			child->measure(availableSize, &s);

			if (first)
			{
				*returnDesiredSize = s;
				first = false;
			}
			else
			{
				returnDesiredSize->width = (std::max(returnDesiredSize->width, s.width));
				returnDesiredSize->height = (std::max(returnDesiredSize->height, s.height));
			}
		}
		return gmpi::ReturnCode::Ok;
	}

	gmpi::ReturnCode arrange(const gmpi::drawing::Rect* finalRect) override
	{
		bounds = *finalRect;

		for (auto& child : graphics_gmpi)
			child->arrange(finalRect);

		return gmpi::ReturnCode::Ok;
	}
	gmpi::ReturnCode render(gmpi::drawing::api::IDeviceContext* drawingContext) override
	{
		for (auto& graphic : graphics_gmpi)
		{
			graphic->render(drawingContext);
		}
		return gmpi::ReturnCode::Ok;
	}
	gmpi::ReturnCode getClipArea(gmpi::drawing::Rect* returnRect) override
	{
		*returnRect = bounds;
		return gmpi::ReturnCode::Ok;
	}

	// IInputClient
	gmpi::ReturnCode setHover(bool isMouseOverMe) override
	{
		(void)isMouseOverMe;
		return gmpi::ReturnCode::Ok;
	}

	gmpi::ReturnCode hitTest(gmpi::drawing::Point point, int32_t flags) override
	{
		currentMouseLayer = -1;
		if (!pointInRect(point, bounds))
			return gmpi::ReturnCode::Unhandled;

		for (int32_t i = (int32_t)editors_gmpi.size() - 1; i >= 0; --i)
		{
			auto editor = editors_gmpi[i];
			if (!editor)
				continue;

			if (editor->hitTest(point, flags) == gmpi::ReturnCode::Ok)
			{
				currentMouseLayer = i;
				return gmpi::ReturnCode::Ok;
			}
		}
		return gmpi::ReturnCode::Unhandled;
	}

	gmpi::api::IInputClient* getEditor()
	{
		if (capturedLayer >= 0)
		{
			return editors_gmpi[capturedLayer].get();
		}
		else if (currentMouseLayer >= 0 && currentMouseLayer < (int32_t)editors_gmpi.size())
		{
			return editors_gmpi[currentMouseLayer].get();
		}
		return nullptr;
	}

	gmpi::ReturnCode onPointerDown(gmpi::drawing::Point point, int32_t flags) override
	{
		lastPoint = point;

		if (auto editor = getEditor(); editor)
			return editor->onPointerDown(point, flags);

		return gmpi::ReturnCode::Unhandled;
	}

	gmpi::ReturnCode onPointerMove(gmpi::drawing::Point point, int32_t flags) override
	{
		lastPoint = point;

		if (capturedLayer >= 0)
			return editors_gmpi[capturedLayer]->onPointerMove(point, flags);

		// hit test each layer, topmost first.
		for (currentMouseLayer = static_cast<int>(editors_gmpi.size()) - 1; currentMouseLayer >= 0; --currentMouseLayer)
		{
			auto& child = editors_gmpi[currentMouseLayer];

			if (child->hitTest(point, flags) == gmpi::ReturnCode::Ok)
				return child->onPointerMove(point, flags);
		}

		currentMouseLayer = -1;

		return gmpi::ReturnCode::Unhandled;
	}

	gmpi::ReturnCode onPointerUp(gmpi::drawing::Point point, int32_t flags) override
	{
		lastPoint = point;

		if (auto editor = getEditor(); editor)
			return editor->onPointerUp(point, flags);

		return gmpi::ReturnCode::Unhandled;
	}

	gmpi::ReturnCode onMouseWheel(gmpi::drawing::Point point, int32_t flags, int32_t delta) override
	{
		lastPoint = point;

		if (auto editor = getEditor(); editor)
			return editor->onMouseWheel(point, flags, delta);

		return gmpi::ReturnCode::Unhandled;
	}

	gmpi::ReturnCode populateContextMenu(gmpi::drawing::Point point, gmpi::api::IUnknown* contextMenuItemsSink) override
	{
		if (auto editor = getEditor(); editor)
			return editor->populateContextMenu(point, contextMenuItemsSink);
		return gmpi::ReturnCode::Unhandled;
	}

	gmpi::ReturnCode onContextMenu(int32_t idx) override
	{
		if (auto editor = getEditor(); editor)
			return editor->onContextMenu(idx);
		return gmpi::ReturnCode::Unhandled;
	}

	gmpi::ReturnCode OnKeyPress(wchar_t c) override
	{
		if (auto editor = getEditor(); editor)
			return editor->OnKeyPress(c);
		return gmpi::ReturnCode::Unhandled;
	}



#if 0
	friend class ResizeAdorner;
	friend class ViewChild;

	GmpiDrawing::Point pointPrev;
	GmpiDrawing_API::MP1_POINT lastMovePoint = { -1, -1 };

protected:
	bool isIteratingChildren = false;
	std::string draggingNewModuleId;
	std::unique_ptr<GmpiSdk::ContextMenuHelper::ContextMenuCallbacks> contextMenuCallbacks;
	bool isArranged = false;
	bool childrenDirty = false;
	std::vector< std::unique_ptr<IViewChild> > children;
	std::vector<ModuleView*> children_monodirectional;
	std::unique_ptr<IPresenter> presenter;

	GmpiDrawing::Rect drawingBounds;
	IViewChild* mouseCaptureObject = {};
	IViewChild* mouseOverObject = {};
	IViewChild* modulePicker = {};

	bool isDraggingModules = false;
	GmpiDrawing::Size DraggingModulesOffset = {};
	GmpiDrawing::Point DraggingModulesInitialTopLeft = {};

#ifdef _WIN32
	DrawingFrameBase2* frameWindow = {};
#endif
	class ModuleViewPanel* patchAutomatorWrapper_ = {};

	void ConnectModules(const Json::Value& element, std::map<int, class ModuleView*>& guiObjectMap);// , ModuleView* patchAutomatorWrapper);
	class ModuleViewPanel* getPatchAutomator(std::map<int, class ModuleView*>& guiObjectMap);
	void PreGraphicsRedraw() override;
	void processUnidirectionalModules();

public:
	EditorViewFrame(GmpiDrawing::Size size);
	virtual ~EditorViewFrame() { mouseOverObject = {}; }

	void setDocument(SE2::IPresenter* presenter);
	int32_t setHost(gmpi::IMpUnknown* host) override;

	void Init(class IPresenter* ppresentor);
	void BuildPatchCableNotifier(std::map<int, class ModuleView*>& guiObjectMap);
	virtual void BuildModules(Json::Value* context, std::map<int, class ModuleView*>& guiObjectMap) = 0;
	void initMonoDirectionalModules(std::map<int, SE2::ModuleView*>& guiObjectMap);

	virtual int getViewType() = 0;

	void OnChildResize(IViewChild* child);
	void RemoveChild(IViewChild* child);
	virtual void markDirtyChild(IViewChild* child);

	int32_t measure(GmpiDrawing_API::MP1_SIZE availableSize, GmpiDrawing_API::MP1_SIZE* returnDesiredSize) override;
	int32_t arrange(GmpiDrawing_API::MP1_RECT finalRect) override;

	int32_t onPointerDown(int32_t flags, GmpiDrawing_API::MP1_POINT point) override;
	int32_t onPointerMove(int32_t flags, GmpiDrawing_API::MP1_POINT point) override;
	int32_t onPointerUp(int32_t flags, GmpiDrawing_API::MP1_POINT point) override;
	int32_t onMouseWheel(int32_t flags, int32_t delta, GmpiDrawing_API::MP1_POINT point) override;
	int32_t OnRender(GmpiDrawing_API::IMpDeviceContext* drawingContext) override;
	int32_t setHover(bool isMouseOverMe) override;

	void calcMouseOverObject(int32_t flags);
	void OnChildDeleted(IViewChild* childObject);
	void onSubPanelMadeVisible();

	int32_t populateContextMenu(float /*x*/, float /*y*/, gmpi::IMpUnknown* /*contextMenuItemsSink*/) override;
	int32_t onContextMenu(int32_t idx) override;

	GmpiDrawing_API::IMpFactory* GetDrawingFactory()
	{
		GmpiDrawing_API::IMpFactory* temp = nullptr;
		getGuiHost()->GetDrawingFactory(&temp);
		return temp;
	}
	gmpi::ReturnCode getDrawingFactory(gmpi::api::IUnknown** returnFactory);

	//		std::string getToolTip(GmpiDrawing_API::MP1_POINT point);
	//		int32_t getToolTip(float x, float y, gmpi::IMpUnknown* returnToolTipString) override;
	int32_t getToolTip(GmpiDrawing_API::MP1_POINT point, gmpi::IString* returnString) override;

	virtual std::string getSkinName() = 0;

	virtual int32_t setCapture(IViewChild* module);
	bool isCaptured(IViewChild* module)
	{
		return mouseCaptureObject == module;
	}
	int32_t releaseCapture();
	//bool isMouseCaptured()
	//{
	//	return mouseCaptureObject != nullptr;
	//}

	virtual int32_t StartCableDrag(IViewChild* fromModule, int fromPin, GmpiDrawing::Point dragStartPoint, bool isHeldAlt, CableType type = CableType::PatchCable);
	void OnCableMove(ConnectorViewBase* dragline);
	int32_t EndCableDrag(GmpiDrawing_API::MP1_POINT point, ConnectorViewBase* dragline);
	void OnPatchCablesUpdate(RawView patchCablesRaw);
	void UpdateCablesBounds();
	void RemoveCables(ConnectorViewBase* cable);
	void RemoveModule(int32_t handle);

	void OnChangedChildHighlight(int phandle, int flags);

	void OnChildDspMessage(void* msg);

	void MoveToFront(IViewChild* child);
	void MoveToBack(IViewChild* child);

	SE2::IPresenter* Presenter()
	{
		return presenter.get();
	}

	void OnChangedChildSelected(int handle, bool selected);
	void OnChangedChildPosition(int phandle, GmpiDrawing::Rect& newRect);
	void OnChangedChildNodes(int phandle, std::vector<GmpiDrawing::Point>& nodes);

	void OnDragSelectionBox(int32_t flags, GmpiDrawing::Rect selectionRect);

	// not to be confused with MpGuiGfxBase::invalidateRect
	virtual void ChildInvalidateRect(const GmpiDrawing_API::MP1_RECT& invalidRect)
	{
		getGuiHost()->invalidateRect(&invalidRect);
	}
	virtual void OnChildMoved() {}
	virtual int32_t ChildCreatePlatformTextEdit(const GmpiDrawing_API::MP1_RECT* rect, gmpi_gui::IMpPlatformText** returnTextEdit)
	{
		return getGuiHost()->createPlatformTextEdit(const_cast<GmpiDrawing_API::MP1_RECT*>(rect), returnTextEdit);
	}
	virtual int32_t ChildCreatePlatformMenu(const GmpiDrawing_API::MP1_RECT* rect, gmpi_gui::IMpPlatformMenu** returnMenu)
	{
		return getGuiHost()->createPlatformMenu(const_cast<GmpiDrawing_API::MP1_RECT*>(rect), returnMenu);
	}

	void autoScrollStart();
	void autoScrollStop();
	void DoClose();

	IViewChild* Find(GmpiDrawing::Point& p);
	void Unload();
	virtual void Refresh(Json::Value* context, std::map<int, SE2::ModuleView*>& guiObjectMap_);

	virtual GmpiDrawing::Point MapPointToView(EditorViewFrame* parentView, GmpiDrawing::Point p)
	{
		return p;
	}

	virtual bool isShown()
	{
		return true;
	}

	virtual void OnPatchCablesVisibilityUpdate();

	// gmpi_gui_api::IMpKeyClient
	int32_t OnKeyPress(wchar_t c) override;

	gmpi::ReturnCode onKey(int32_t key, gmpi::drawing::Point* pointerPosOrNull);

	bool DoModulePicker(gmpi::drawing::Point currentPointerPos);
	void DismissModulePicker();
	void DragNewModule(const char* id);
	virtual ConnectorViewBase* createCable(CableType type, int32_t handleFrom, int32_t fromPin) = 0;
	// MpGuiGfxBase
	int32_t MP_STDCALL measure(gmpi_gui::MP1_SIZE availableSize, gmpi_gui::MP1_SIZE* returnDesiredSize)  override
	{
		bool first = true;
		for (auto& child : graphics)
		{
			gmpi_gui::MP1_SIZE s{};
			child->measure(availableSize, &s);

			if (first)
			{
				*returnDesiredSize = s;
				first = false;
			}
			else
			{
				returnDesiredSize->width = (std::max(returnDesiredSize->width, s.width));
				returnDesiredSize->height = (std::max(returnDesiredSize->height, s.height));
			}
		}

		const gmpi::drawing::Size availableSize2 = legacy_converters::convert(availableSize);

		for (auto& child : graphics_gmpi)
		{
			gmpi::drawing::Size s{};
			child->measure(&availableSize2, &s);

			if (first)
			{
				returnDesiredSize->width = s.width;
				returnDesiredSize->height = s.height;
				first = false;
			}
			else
			{
				returnDesiredSize->width = (std::max(returnDesiredSize->width, s.width));
				returnDesiredSize->height = (std::max(returnDesiredSize->height, s.height));
			}
		}
		return gmpi::MP_OK;
	}

	int32_t MP_STDCALL arrange(gmpi_gui::MP1_RECT finalRect)  override
	{
		for (auto& child : graphics)
			child->arrange(finalRect);

		return gmpi::MP_OK;
	}

	int32_t initialize() override
	{
		for (auto& child : editors)
			child->initialize();
		
		return gmpi_gui::MpGuiGfxBase::initialize();
	}

	int32_t OnRender(GmpiDrawing_API::IMpDeviceContext* drawingContext_sdk3) override
	{
		for(auto& child : graphics)
			child->OnRender(drawingContext_sdk3);

		gmpi::shared_ptr<gmpi::drawing::api::IDeviceContext> dc;
		drawingContext_sdk3->queryInterface(*(gmpi::MpGuid*)&gmpi::drawing::api::IDeviceContext::guid, dc.put_void());

		for (auto& child : graphics_gmpi)
			child->render(dc.get());
			
		return gmpi::MP_OK;
	}

	int32_t MP_STDCALL hitTest2(int32_t flags, GmpiDrawing_API::MP1_POINT point)
	{
		return gmpi::MP_OK; // not called
	}

	gmpi_gui_api::IMpGraphics3* graphicsChild()
	{
		auto layer = capturedLayer >= 0 ? capturedLayer : currentMouseLayer;

		return layer >= 0 ? graphics[layer].get() : nullptr;
	}

	int32_t onMouseWheel(int32_t flags, int32_t delta, GmpiDrawing_API::MP1_POINT point) override
	{
		if(auto child = graphicsChild(); child)
			return child->onMouseWheel(flags, delta, point);

		return gmpi::MP_UNHANDLED;
	}

	int32_t MP_STDCALL onPointerDown(int32_t flags, GmpiDrawing_API::MP1_POINT point) override
	{
		if (auto child = graphicsChild(); child)
			child->onPointerDown(flags, point);

		return gmpi::MP_OK;
	}
	int32_t MP_STDCALL onPointerMove(int32_t flags, GmpiDrawing_API::MP1_POINT point) override
	{
		lastPoint = point;

		// TODO !! capturedLayer don't work now that GMPI-UI children in different vector to SDK3 children.

		if(capturedLayer >= 0)
			return graphics[capturedLayer]->onPointerMove(flags, point);

		for (currentMouseLayer = static_cast<int>(editors_gmpi.size()) - 1; currentMouseLayer >= 0; --currentMouseLayer)
		{
			auto& child = editors_gmpi[currentMouseLayer];

			if (child->hitTest(legacy_converters::convert(point), flags) == gmpi::ReturnCode::Ok)
				return (int32_t) child->onPointerMove(legacy_converters::convert(point), flags);
		}

		for (currentMouseLayer = static_cast<int>(graphics.size()) - 1; currentMouseLayer >= 0 ; --currentMouseLayer)
		{
			auto& child = graphics[currentMouseLayer];

			if (child->hitTest2(flags, point) == gmpi::MP_OK)
				return child->onPointerMove(flags, point);
		}

		currentMouseLayer = -1;

		return gmpi::MP_UNHANDLED;
	}
	int32_t MP_STDCALL onPointerUp(int32_t flags, GmpiDrawing_API::MP1_POINT point) override
	{
		if (auto child = graphicsChild(); child)
			child->onPointerUp(flags, point);

		return gmpi::MP_OK;
	}

	// IGraphicsRedrawClient
	void preGraphicsRedraw() override
	{
		for (auto& child : redraws)
			child->PreGraphicsRedraw();
	}

	// IMpKeyClient
	int32_t MP_STDCALL OnKeyPress(wchar_t c) override
	{
		// GMPI
		for(auto& child : editors_gmpi)
		{
			if (gmpi::ReturnCode::Ok == child->OnKeyPress(c))
			{
				return gmpi::MP_OK;
			}
		}
		// SDK3
		if (auto child = graphicsChild(); child)
		{
			gmpi_gui_api::IMpKeyClient* keyClient{};
			if (child->queryInterface(gmpi_gui_api::IMpKeyClient::guid, reinterpret_cast<void**>(&keyClient)) == gmpi::MP_OK)
			{
				keyClient->OnKeyPress(c);
				keyClient->release();
				return gmpi::MP_OK;
			}
		}
		return gmpi::MP_UNHANDLED;
	}
#endif

#if 0
	friend class ResizeAdorner;
	friend class ViewChild;
		
	GmpiDrawing::Point pointPrev;
	GmpiDrawing_API::MP1_POINT lastMovePoint = { -1, -1 };

protected:
bool isIteratingChildren = false;
	std::string draggingNewModuleId;
	std::unique_ptr<GmpiSdk::ContextMenuHelper::ContextMenuCallbacks> contextMenuCallbacks;
	bool isArranged = false;
	bool childrenDirty = false;
	std::vector< std::unique_ptr<IViewChild> > children;
	std::vector<ModuleView*> children_monodirectional;
	std::unique_ptr<IPresenter> presenter;

	GmpiDrawing::Rect drawingBounds;
	IViewChild* mouseCaptureObject = {};
	IViewChild* mouseOverObject = {};
	IViewChild* modulePicker = {};

	bool isDraggingModules = false;
	GmpiDrawing::Size DraggingModulesOffset = {};
	GmpiDrawing::Point DraggingModulesInitialTopLeft = {};

#ifdef _WIN32
	DrawingFrameBase2* frameWindow = {};
#endif
	class ModuleViewPanel* patchAutomatorWrapper_ = {};

	void ConnectModules(const Json::Value& element, std::map<int, class ModuleView*>& guiObjectMap);// , ModuleView* patchAutomatorWrapper);
	class ModuleViewPanel* getPatchAutomator(std::map<int, class ModuleView*>& guiObjectMap);
	void preGraphicsRedraw() override;
	void processUnidirectionalModules();

public:
	EditorViewFrame(GmpiDrawing::Size size);
	virtual ~EditorViewFrame() { mouseOverObject = {}; }

	void setDocument(SE2::IPresenter* presenter);
	int32_t setHost(gmpi::IMpUnknown* host) override;

	void Init(class IPresenter* ppresentor);
	void BuildPatchCableNotifier(std::map<int, class ModuleView*>& guiObjectMap);
	virtual void BuildModules(Json::Value* context, std::map<int, class ModuleView*>& guiObjectMap) = 0;
	void initMonoDirectionalModules(std::map<int, SE2::ModuleView*>& guiObjectMap);

	virtual int getViewType() = 0;

	void OnChildResize(IViewChild* child);
	void RemoveChild(IViewChild* child);
	virtual void markDirtyChild(IViewChild* child);

	int32_t measure(GmpiDrawing_API::MP1_SIZE availableSize, GmpiDrawing_API::MP1_SIZE * returnDesiredSize) override;
	int32_t arrange(GmpiDrawing_API::MP1_RECT finalRect) override;

	int32_t onPointerDown(int32_t flags, GmpiDrawing_API::MP1_POINT point) override;
	int32_t onPointerMove(int32_t flags, GmpiDrawing_API::MP1_POINT point) override;
	int32_t onPointerUp(int32_t flags, GmpiDrawing_API::MP1_POINT point) override;
	int32_t onMouseWheel(int32_t flags, int32_t delta, GmpiDrawing_API::MP1_POINT point) override;
	int32_t OnRender(GmpiDrawing_API::IMpDeviceContext* drawingContext) override;
	int32_t setHover(bool isMouseOverMe) override;

	void calcMouseOverObject(int32_t flags);
	void OnChildDeleted(IViewChild* childObject);
	void onSubPanelMadeVisible();

	int32_t populateContextMenu(float /*x*/, float /*y*/, gmpi::IMpUnknown* /*contextMenuItemsSink*/) override;
	int32_t onContextMenu(int32_t idx) override;

	GmpiDrawing_API::IMpFactory* GetDrawingFactory()
	{
		GmpiDrawing_API::IMpFactory* temp = nullptr;
		getGuiHost()->GetDrawingFactory(&temp);
		return temp;
	}
	gmpi::ReturnCode getDrawingFactory(gmpi::api::IUnknown** returnFactory);

//		std::string getToolTip(GmpiDrawing_API::MP1_POINT point);
//		int32_t getToolTip(float x, float y, gmpi::IMpUnknown* returnToolTipString) override;
	int32_t getToolTip(GmpiDrawing_API::MP1_POINT point, gmpi::IString* returnString) override;

	virtual std::string getSkinName() = 0;

	virtual int32_t setCapture(IViewChild* module);
	bool isCaptured(IViewChild* module)
	{
		return mouseCaptureObject == module;
	}
	int32_t releaseCapture();
	//bool isMouseCaptured()
	//{
	//	return mouseCaptureObject != nullptr;
	//}

	virtual int32_t StartCableDrag(IViewChild* fromModule, int fromPin, GmpiDrawing::Point dragStartPoint, bool isHeldAlt, CableType type = CableType::PatchCable);
	void OnCableMove(ConnectorViewBase * dragline);
	int32_t EndCableDrag(GmpiDrawing_API::MP1_POINT point, ConnectorViewBase* dragline);
	void OnPatchCablesUpdate(RawView patchCablesRaw);
	void UpdateCablesBounds();
	void RemoveCables(ConnectorViewBase* cable);
	void RemoveModule(int32_t handle);

	void OnChangedChildHighlight(int phandle, int flags);

	void OnChildDspMessage(void * msg);

	void MoveToFront(IViewChild* child);
	void MoveToBack(IViewChild* child);

	SE2::IPresenter* Presenter()
	{
		return presenter.get();
	}

	void OnChangedChildSelected(int handle, bool selected);
	void OnChangedChildPosition(int phandle, GmpiDrawing::Rect& newRect);
	void OnChangedChildNodes(int phandle, std::vector<GmpiDrawing::Point>& nodes);

	void OnDragSelectionBox(int32_t flags, GmpiDrawing::Rect selectionRect);

	// not to be confused with MpGuiGfxBase::invalidateRect
	virtual void ChildInvalidateRect(const GmpiDrawing_API::MP1_RECT& invalidRect)
	{
		getGuiHost()->invalidateRect(&invalidRect);
	}
	virtual void OnChildMoved() {}
	virtual int32_t ChildCreatePlatformTextEdit(const GmpiDrawing_API::MP1_RECT* rect, gmpi_gui::IMpPlatformText** returnTextEdit)
	{
		return getGuiHost()->createPlatformTextEdit(const_cast<GmpiDrawing_API::MP1_RECT*>(rect), returnTextEdit);
	}
	virtual int32_t ChildCreatePlatformMenu(const GmpiDrawing_API::MP1_RECT* rect, gmpi_gui::IMpPlatformMenu** returnMenu)
	{
		return getGuiHost()->createPlatformMenu(const_cast<GmpiDrawing_API::MP1_RECT*>(rect), returnMenu);
	}

	void autoScrollStart();
	void autoScrollStop();
	void DoClose();

	IViewChild* Find(GmpiDrawing::Point& p);
	void Unload();
	virtual void Refresh(Json::Value* context, std::map<int, SE2::ModuleView*>& guiObjectMap_);

	virtual GmpiDrawing::Point MapPointToView(EditorViewFrame* parentView, GmpiDrawing::Point p)
	{
		return p;
	}

	virtual bool isShown()
	{
		return true;
	}

	virtual void OnPatchCablesVisibilityUpdate();

	// gmpi_gui_api::IMpKeyClient
	int32_t OnKeyPress(wchar_t c) override;

	gmpi::ReturnCode onKey(int32_t key, gmpi::drawing::Point* pointerPosOrNull);

	bool DoModulePicker(gmpi::drawing::Point currentPointerPos);
	void DismissModulePicker();
	void DragNewModule(const char* id);
	virtual ConnectorViewBase* createCable(CableType type, int32_t handleFrom, int32_t fromPin) = 0;
#endif

	gmpi::ReturnCode queryInterface(const gmpi::api::Guid* iid, void** returnInterface)
	{
		*returnInterface = {};
		GMPI_QUERYINTERFACE(IInputClient);
		GMPI_QUERYINTERFACE(IDrawingClient);
		GMPI_QUERYINTERFACE(gmpi::api::IGraphicsRedrawClient);
		
		return gmpi::ReturnCode::NoSupport;
	}
#if 0
	int32_t queryInterface(const gmpi::MpGuid& iid, void** returnInterface) override
	{
		*returnInterface = nullptr;

		if (iid == IGraphicsRedrawClient::guid)
		{
			*returnInterface = static_cast<IGraphicsRedrawClient*>(this);
			addRef();
			return gmpi::MP_OK;
		}

		if (iid == gmpi_gui_api::IMpKeyClient::guid)
		{
			*returnInterface = static_cast<gmpi_gui_api::IMpKeyClient*>(this);
			addRef();
			return gmpi::MP_OK;
		}
		
		return gmpi_gui::MpGuiGfxBase::queryInterface(iid, returnInterface);
	}
#endif
	GMPI_REFCOUNT
};

#if 0
inline int32_t MP_STDCALL PileChildHost::GetDrawingFactory(GmpiDrawing_API::IMpFactory** returnFactory)
{
	return parent->getGuiHost()->GetDrawingFactory(returnFactory);
}
inline void MP_STDCALL PileChildHost::invalidateRect(const GmpiDrawing_API::MP1_RECT* invalidRect)
{
	return parent->getGuiHost()->invalidateRect(invalidRect);
}
inline int32_t MP_STDCALL PileChildHost::setCapture()
{
	parent->capturedLayer = parent->currentMouseLayer;
	parent->setCapture(); // tell parent.

	return gmpi::MP_OK;
}
inline int32_t MP_STDCALL PileChildHost::getCapture(int32_t& returnValue)
{
	return parent->capturedLayer == parent->currentMouseLayer ? gmpi::MP_OK : gmpi::MP_UNHANDLED;
}
inline int32_t MP_STDCALL PileChildHost::releaseCapture()
{
	parent->capturedLayer = -2;

	parent->releaseCapture(); // tell parent.

	const int32_t flags = gmpi_gui_api::GG_POINTER_FLAG_INCONTACT | gmpi_gui_api::GG_POINTER_FLAG_PRIMARY | gmpi_gui_api::GG_POINTER_FLAG_CONFIDENCE;
	return parent->onPointerMove(flags, parent->lastPoint); // recalc current layer
}

inline gmpi::ReturnCode PileChildHost::createKeyListener(const gmpi::drawing::Rect* r, gmpi::api::IUnknown** returnKeyListener)
{
	gmpi::shared_ptr<gmpi::api::IDialogHost> host;
	parent->getGuiHost()->queryInterface(*(const gmpi::MpGuid*)&gmpi::api::IDialogHost::guid, host.put_void());
	return host->createKeyListener(r, returnKeyListener);
}
#endif

inline gmpi::ReturnCode PileChildHost2::getDrawingFactory(gmpi::api::IUnknown** returnFactory)
{
	gmpi_sdk::mp_shared_ptr<gmpi_gui::IMpGraphicsHost> sdk3DrawingHost;
	parent->drawingHost->queryInterface((const gmpi::api::Guid*) &gmpi_gui::SE_IID_GRAPHICS_HOST, sdk3DrawingHost.asIMpUnknownPtr());

	sdk3DrawingHost->GetDrawingFactory((GmpiDrawing_API::IMpFactory**) returnFactory);
	return gmpi::ReturnCode::Ok;
}
inline void PileChildHost2::invalidateRect(const gmpi::drawing::Rect* invalidRect) { parent->drawingHost->invalidateRect(invalidRect); }
inline void PileChildHost2::invalidateMeasure() { parent->drawingHost->invalidateMeasure(); };
inline gmpi::ReturnCode PileChildHost2::createTextEdit(const gmpi::drawing::Rect* r, gmpi::api::IUnknown** returnTextEdit) { return parent->dialogHost->createTextEdit(r, returnTextEdit); }
inline gmpi::ReturnCode PileChildHost2::createPopupMenu(const gmpi::drawing::Rect* r, gmpi::api::IUnknown** returnPopupMenu) { return parent->dialogHost->createPopupMenu(r, returnPopupMenu); }
inline gmpi::ReturnCode PileChildHost2::createKeyListener(const gmpi::drawing::Rect* r, gmpi::api::IUnknown** returnKeyListener)
{
	//gmpi::shared_ptr<gmpi::api::IDialogHost> host;
	//parent->getGuiHost()->queryInterface(*(const gmpi::MpGuid*)&gmpi::api::IDialogHost::guid, host.put_void());
	return parent->dialogHost->createKeyListener(r, returnKeyListener);
}
inline gmpi::ReturnCode PileChildHost2::createFileDialog(int32_t dialogType, gmpi::api::IUnknown** returnDialog) { return parent->dialogHost->createFileDialog(dialogType, returnDialog); }
inline gmpi::ReturnCode PileChildHost2::createStockDialog(int32_t dialogType, gmpi::api::IUnknown** returnDialog) { return parent->dialogHost->createStockDialog(dialogType, returnDialog); }

} //namespace SE2

