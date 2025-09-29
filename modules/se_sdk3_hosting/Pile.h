#pragma once
#include <vector>
#include "Drawing.h"
#include "UgDatabase.h"
#include "mp_gui.h"
#include "../se_sdk3_hosting/GraphicsRedrawClient.h"
#include "GmpiApiDrawing.h"

namespace SE2
{
// test adorner layer
struct AdornerLayer : public gmpi_gui::MpGuiGfxBase
{
	int32_t OnRender(GmpiDrawing_API::IMpDeviceContext* drawingContext) override
	{
		GmpiDrawing::Graphics g(drawingContext);

		auto r = getRect();

		auto textFormat = GetGraphicsFactory().CreateTextFormat();
		auto brush = g.CreateSolidColorBrush(GmpiDrawing::Color::Green);

		g.DrawTextU("Adorner Layer", textFormat, r, brush);

		return gmpi::MP_OK;
	}
};


// PileChildHost holds refcount of child plugins hosts. need to keep refcount seperate from Pile, else it never gets deleted.
struct PileChildHost :
	  public gmpi_gui::IMpGraphicsHost
	, public gmpi::IMpUserInterfaceHost2
{
	struct Pile* parent = nullptr;

	// PARENTING
	// IMpGraphicsHost
	int32_t MP_STDCALL createFileDialog(int32_t dialogType, gmpi_gui::IMpFileDialog** returnFileDialog) override { return 0; }
	int32_t MP_STDCALL GetDrawingFactory(GmpiDrawing_API::IMpFactory** returnFactory) override;
	void MP_STDCALL invalidateRect(const GmpiDrawing_API::MP1_RECT* invalidRect) override { return; }
	void MP_STDCALL invalidateMeasure() override { return; };
	int32_t MP_STDCALL setCapture() override { return 0; }
	int32_t MP_STDCALL getCapture(int32_t& returnValue) override { return 0; }
	int32_t MP_STDCALL releaseCapture() override { return 0; }
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

		return gmpi::MP_NOSUPPORT;
	}
	GMPI_REFCOUNT
};

// stacks a set of child elements on top of each other.
struct Pile :
	  public gmpi_gui::MpGuiGfxBase
	, public IGraphicsRedrawClient
	, public gmpi_gui_api::IMpKeyClient

{
	struct PileChild
	{
		gmpi_sdk::mp_shared_ptr<gmpi_gui_api::IMpGraphics3> gfx;
		gmpi_sdk::mp_shared_ptr<gmpi::IMpUserInterface2> pinHost;
	};

	PileChildHost childhost;
	std::vector<PileChild> children;

	Pile()
	{
		childhost.parent = this;
	}

	void addChild(gmpi::IMpUnknown* child)
	{
		PileChild pc;

		child->queryInterface(gmpi_gui_api::IMpGraphics3::guid, pc.gfx.asIMpUnknownPtr());
		child->queryInterface(gmpi::MP_IID_GUI_PLUGIN2, pc.pinHost.asIMpUnknownPtr());

		if(!pc.gfx && !pc.pinHost)
		{
			assert(false);
			return;
		}

		if (pc.pinHost)
			pc.pinHost->setHost(static_cast<gmpi_gui::IMpGraphicsHost*>(&childhost));

		children.push_back(pc);
	}

	// MpGuiGfxBase
	int32_t MP_STDCALL measure(gmpi_gui::MP1_SIZE availableSize, gmpi_gui::MP1_SIZE* returnDesiredSize)  override
	{
		bool first = true;
		for (auto& child : children)
		{
			if (!child.gfx)
				continue;

			gmpi_gui::MP1_SIZE s{};
			child.gfx->measure(availableSize, &s);

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
		for (auto& child : children)
		{
			if (!child.gfx)
				continue;

			child.gfx->arrange(finalRect);
		}
		return gmpi::MP_OK;
	}

	int32_t initialize() override
	{
		for (auto& child : children)
		{
			if (!child.pinHost)
				continue;

			child.pinHost->initialize();
		}
		
		return gmpi_gui::MpGuiGfxBase::initialize();
	}

	int32_t OnRender(GmpiDrawing_API::IMpDeviceContext* drawingContext) override
	{
		for(auto& child : children)
		{
			if(child.gfx)
			{
				child.gfx->OnRender(drawingContext);
			}
		}

		return gmpi::MP_OK;
	}

	// IGraphicsRedrawClient
	void PreGraphicsRedraw() override {};

	// IMpKeyClient
	int32_t MP_STDCALL OnKeyPress(wchar_t c) override
	{
		for (auto& child : children)
		{
			if (child.gfx)
			{
				gmpi_gui_api::IMpKeyClient* keyClient = nullptr;
				if (child.gfx->queryInterface(gmpi_gui_api::IMpKeyClient::guid, reinterpret_cast<void**>(&keyClient)) == gmpi::MP_OK)
				{
					keyClient->OnKeyPress(c);
					keyClient->release();
				}
			}
		}
		return gmpi::MP_OK;
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

} //namespace SE2

