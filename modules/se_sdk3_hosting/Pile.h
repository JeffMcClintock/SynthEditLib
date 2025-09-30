#pragma once
#include <vector>
#include "Drawing.h"
#include "../se_sdk3_hosting/GraphicsRedrawClient.h"
#include "ModulePicker.h"

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
	// these point to my *parent*
	gmpi::shared_ptr<gmpi::api::IDrawingHost> drawingHost;
	gmpi::shared_ptr<gmpi::api::IInputHost> inputHost;
	gmpi::shared_ptr<gmpi::api::IDialogHost> dialogHost;

	// IDrawingHost
	gmpi::ReturnCode getDrawingFactory(gmpi::api::IUnknown** returnFactory) override
	{
		return drawingHost->getDrawingFactory(returnFactory);
	}

	void invalidateRect(const gmpi::drawing::Rect* invalidRect) override
	{
		(void)invalidRect;
	}

	void invalidateMeasure() override
	{
		return;
	}

	float getRasterizationScale() override
	{
		return 1.0f;
	}

	// IInputHost
	gmpi::ReturnCode setCapture() override
	{
		return gmpi::ReturnCode::Ok;
	}

	gmpi::ReturnCode getCapture(bool& returnValue) override
	{
		returnValue = false;
		return gmpi::ReturnCode::Ok;
	}

	gmpi::ReturnCode releaseCapture() override
	{
		return gmpi::ReturnCode::Ok;
	}

	gmpi::ReturnCode getFocus() override
	{
		return gmpi::ReturnCode::Ok;
	}

	gmpi::ReturnCode releaseFocus() override
	{
		return gmpi::ReturnCode::Ok;
	}

	// IDialogHost
	gmpi::ReturnCode createTextEdit(const gmpi::drawing::Rect* r, gmpi::api::IUnknown** returnTextEdit) override { return gmpi::ReturnCode::NoSupport;}
	gmpi::ReturnCode createPopupMenu(const gmpi::drawing::Rect* r, gmpi::api::IUnknown** returnPopupMenu) override { return gmpi::ReturnCode::NoSupport;}
	gmpi::ReturnCode createKeyListener(const gmpi::drawing::Rect* r, gmpi::api::IUnknown** returnKeyListener) { return gmpi::ReturnCode::NoSupport;}
	gmpi::ReturnCode createFileDialog(int32_t dialogType, gmpi::api::IUnknown** returnDialog) override { return gmpi::ReturnCode::NoSupport;}
	gmpi::ReturnCode createStockDialog(int32_t dialogType, gmpi::api::IUnknown** returnDialog) override { return gmpi::ReturnCode::NoSupport; }


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
		gmpi::drawing::Rect bounds;
		gmpi::shared_ptr<gmpi::api::IDrawingClient> graphic;
		gmpi::shared_ptr<gmpi::api::IInputClient> editor;
	};

	gmpi::shared_ptr<gmpi::api::IDrawingHost> drawingHost;
	gmpi::shared_ptr<gmpi::api::IInputHost> inputHost;
	gmpi::shared_ptr<gmpi::api::IDialogHost> dialogHost;

	GmpiUiLayerHost host;
	std::vector<childInfo> children;

	std::function<gmpi::ReturnCode(wchar_t)> keyHandler;

	GmpiUiLayer()
	{ 
		//gmpi::shared_ptr<gmpi::api::IDrawingClient> unknown;
		//unknown.attach(new ModulePicker2());
		//addChild(unknown.get(), nullptr); // test child.
	}
	~GmpiUiLayer()
	{
		int x = 9;
	}
	void addChild(gmpi::api::IUnknown* child) //, gmpi::api::IUnknown* host)
	{
		gmpi::shared_ptr<gmpi::api::IUnknown> unknown;
		unknown = (gmpi::api::IUnknown*)child;

		auto graphic = unknown.as<gmpi::api::IDrawingClient>();
		auto editor = unknown.as<gmpi::api::IInputClient>();

		children.push_back(
			{
				{100,100,200,200}
				, graphic
				, editor
			}
		);

		if (graphic)
		{
			graphic->open(static_cast<gmpi::api::IDrawingHost*>(&host));
		}
	}
	
	// IInputClient
	gmpi::ReturnCode setHover(bool isMouseOverMe) override
	{
		(void)isMouseOverMe;
		return gmpi::ReturnCode::Ok;
	}

	gmpi::ReturnCode hitTest(gmpi::drawing::Point point, int32_t flags) override
	{
		(void)flags;

		if (point.x < 100 && point.y < 100)
			return gmpi::ReturnCode::Ok;

		return gmpi::ReturnCode::Unhandled;
	}

	gmpi::ReturnCode onPointerDown(gmpi::drawing::Point point, int32_t flags) override
	{
		(void)point; (void)flags;
		return gmpi::ReturnCode::Unhandled;
	}

	gmpi::ReturnCode onPointerMove(gmpi::drawing::Point point, int32_t flags) override
	{
		(void)point; (void)flags;
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

		host.drawingHost = drawingHost;
		host.inputHost = inputHost;
		host.dialogHost = dialogHost;

		return gmpi::ReturnCode::Ok;
	}

	gmpi::ReturnCode measure(const gmpi::drawing::Size* availableSize, gmpi::drawing::Size* returnDesiredSize) override
	{
		(void)availableSize;

		if (returnDesiredSize)
		{
			// Minimal adornment area.
			returnDesiredSize->width = 100.0f;
			returnDesiredSize->height = 100.0f;
		}
		return gmpi::ReturnCode::Ok;
	}

	gmpi::ReturnCode arrange(const gmpi::drawing::Rect* finalRect) override
	{
		(void)finalRect;
		return gmpi::ReturnCode::Ok;
	}

	gmpi::ReturnCode render(gmpi::drawing::api::IDeviceContext* drawingContext) override
	{
		gmpi::drawing::Graphics g(drawingContext);

		// Restrict drawing only to overall clip-rect.
		const auto cliprect = g.getAxisAlignedClip();
		const auto originalTransform = g.getTransform();

		gmpi::drawing::Rect childClipRect;
		for (auto& child : children)
		{
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
		GMPI_QUERYINTERFACE(IInputClient);
		GMPI_QUERYINTERFACE(IDrawingClient);
		return gmpi::ReturnCode::NoSupport;
	}

	GMPI_REFCOUNT
};


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

struct PileChildHost2 :
	  public gmpi::api::IDialogHost
	, public gmpi::api::IDrawingHost
{
	struct Pile* parent = nullptr;

	// IDrawingHost
	gmpi::ReturnCode getDrawingFactory(gmpi::api::IUnknown** returnFactory) override { return gmpi::ReturnCode::Ok; }
	void invalidateRect(const gmpi::drawing::Rect* invalidRect) override {}
	void invalidateMeasure() override { return; };
	float getRasterizationScale() override { return 1.0f; } // DPI scaling

	// IDialogHost
	gmpi::ReturnCode createTextEdit(const gmpi::drawing::Rect* r, gmpi::api::IUnknown** returnTextEdit) override { return gmpi::ReturnCode::Ok; }
	gmpi::ReturnCode createPopupMenu(const gmpi::drawing::Rect* r, gmpi::api::IUnknown** returnPopupMenu) override { return gmpi::ReturnCode::Ok; }
	gmpi::ReturnCode createKeyListener(const gmpi::drawing::Rect* r, gmpi::api::IUnknown** returnKeyListener) override { return gmpi::ReturnCode::Ok; }
	gmpi::ReturnCode createFileDialog(int32_t dialogType, gmpi::api::IUnknown** returnDialog) override { return gmpi::ReturnCode::Ok; }
	gmpi::ReturnCode createStockDialog(int32_t dialogType, gmpi::api::IUnknown** returnDialog) override { return gmpi::ReturnCode::Ok; }

	gmpi::ReturnCode queryInterface(const gmpi::api::Guid* iid, void** returnInterface)
	{
		GMPI_QUERYINTERFACE(IDialogHost);
		GMPI_QUERYINTERFACE(IDrawingHost);
		return gmpi::ReturnCode::NoSupport;
	}

	GMPI_REFCOUNT_NO_DELETE
};

// stacks a set of child elements on top of each other.
struct Pile :
	  public gmpi_gui::MpGuiGfxBase
	, public gmpi_gui_api::IMpKeyClient
	, public IGraphicsRedrawClient
	, public hasGmpiUiChildren
{
	PileChildHost childhost_sdk3;
	PileChildHost2 childhost_gmpi;

	// SDK3 child plugins.
	std::vector<gmpi_sdk::mp_shared_ptr<gmpi_gui_api::IMpGraphics3>> graphics;
	std::vector<gmpi_sdk::mp_shared_ptr<gmpi::IMpUserInterface2>> editors;
	std::vector<gmpi_sdk::mp_shared_ptr<IGraphicsRedrawClient>> redraws;
	
	GmpiDrawing_API::MP1_POINT lastPoint{};
	int32_t currentMouseLayer = -1;
	int32_t capturedLayer = -2;
	
	Pile()
	{
		childhost_sdk3.parent = this;
	}
	~Pile()
	{
		int x = 9;;
	}
	void addChild(gmpi::IMpUnknown* child)
	{
		// SDK3 child plugins.
		{
			gmpi_sdk::mp_shared_ptr<gmpi::IMpUserInterface2> editor;
			gmpi_sdk::mp_shared_ptr<gmpi_gui_api::IMpGraphics3> graphic;
			gmpi_sdk::mp_shared_ptr<IGraphicsRedrawClient> redraw;

			child->queryInterface(gmpi_gui_api::IMpGraphics3::guid, graphic.asIMpUnknownPtr());
			child->queryInterface(gmpi::MP_IID_GUI_PLUGIN2, editor.asIMpUnknownPtr());
			child->queryInterface(IGraphicsRedrawClient::guid, redraw.asIMpUnknownPtr());

			if (graphic)
			{
				graphics.push_back(graphic);
			}
			if (editor)
			{
				editors.push_back(editor);
				editor->setHost(static_cast<gmpi_gui::IMpGraphicsHost*>(&childhost_sdk3));
			}
			if (redraw)
			{
				redraws.push_back(redraw);
			}
		}

		// GMPI child plugins
		hasGmpiUiChildren::addChild((gmpi::api::IUnknown*) child, static_cast<gmpi::api::IDialogHost*>(&childhost_gmpi));
	}

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

		if(capturedLayer >= 0)
			return graphics[capturedLayer]->onPointerMove(flags, point);

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
	void PreGraphicsRedraw() override
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

	GMPI_REFCOUNT
};

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


} //namespace SE2

