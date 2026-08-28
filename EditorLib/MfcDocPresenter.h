#pragma once
#include "Presenter.h"
#include "CContainer.h"
#include "notify.h"
#include "TimerManager.h"
#include "ModuleView.h"
#include "ViewBase.h"
#include "PatchManager.h"
#include "Notify_msg.h"

class MfcDocPresenterBase : public PresenterBase, public Notifiable
{
protected:
	class CContainer* container;
	SE2::ViewBase* view;

public:
	MfcDocPresenterBase(class CContainer* pcontainer) :
		container(pcontainer)
		, view(nullptr)
	{
		container->RegisterObserver(this);
	}

	~MfcDocPresenterBase() override
	{
		// Tab switching destroys/recreates presenters; without an explicit
		// unregister here the container keeps stale observer entries that
		// wedge teardown on shutdown. OM_DELETE handlers null `container`
		// first, so this is a no-op on the container-driven path.
		if (container)
			container->UnRegisterObserver(this);
	}

	class CContainer* getContainer() { return container; }

	void setView(SE2::ViewBase* pview) override
	{
		view = pview;
	}

	IGuiHost2* getPatchManager()
	{
		return container->get_patch_manager();
	}

	int GetSnapSize() override;

	void OnFrameGotFocus() override;

	bool CanConnect(SE2::CableType cabletype, int32_t fromModule, int fromPin, int32_t toModule, int toPin) override;
	bool AddConnector(int32_t fromModule, int fromPin, int32_t toModule, int toPin, bool placeAtBack) override;
	void HighlightConnector(int32_t moduleHandle, int pin, int highlightType) override;
	void DragNode(int32_t fromModule, int32_t nodeIdx, gmpi::drawing::Point point) override;
	void InsertNode(int32_t fromLine, int32_t nodeInsertIdx, gmpi::drawing::Point point) override;

	IGuiHost2* GetPatchManager() override
	{
		return container->get_patch_manager();
	}

	int32_t GenerateTemporaryHandle() override
	{
		static int nextTemporaryHandle = -100;
		return nextTemporaryHandle--;
	}

	int32_t MP_STDCALL LoadPresetFile_DEPRECATED(const char* presetFilePath) override
	{
		return gmpi::MP_FAIL;
	}
	void OnChangedChildSelected(CDocOb*);
	void OnConnectorColorChanged(CDocOb*);
	void OnChangedChildPosition(CDocOb*);
	void OnChildDeleted(CDocOb*);

	void OnCpuUpdate(CUG* module);
	void OnHoverScopeUpdate(void* param);
	void OnHoverScopeWaveformUpdate(void* param);

	void OnChildDspMessage(void* msg) override
	{
		if (view)
		{
			view->OnChildDspMessage(msg);
		}
	}

	void OnControllerDeleted() override {}
	void InsertRackModule(const std::wstring& prefabFilePath) override;
	void setHoverScopePin(int32_t moduleHandle, int pin) override;
	void HighlightFeedback(std::list< std::pair<SE2::feedbackPinUi, SE2::feedbackPinUi> >& feedbackConnectors) override;
	void ClearFeedbackHighlights() override;
	SE2::IViewChild* createModulePicker(SE2::ViewBase*) override;

	void OnNotify(Notifier* sender, int lHint, void* pHint = 0) override
	{
		switch (lHint)
		{
		case OM_REMOVE_CHILD:
			OnChildDeleted((CDocOb*)pHint);
			break;

		case OM_ON_DSP_MESSAGE:
			{
				OnChildDspMessage(pHint);
			}
			break;
		}
	}

	// Set module's persisted view-rect absolutely. Inherited by both
	// MfcDocSubPresenter and MfcDocPresenter; works regardless of which
	// presenter the call lands on.
	void SetModuleRect(int handle, gmpi::drawing::Rect rect) override;
	gmpi::drawing::Rect GetModuleRect(int handle) override;
};

class MfcDocSubPresenter : public MfcDocPresenterBase
{
public:
	MfcDocPresenterBase* parent;

	MfcDocSubPresenter(class CContainer* pcontainer) : MfcDocPresenterBase(pcontainer)
		, parent(nullptr)
	{}

	bool editEnabled() override
	{
		return false;
	}

	void ObjectClicked(int handle, int heldKeys) override
	{
		// As far as selection-logic goes, Clicking on object in sub-view translates to clicking sub-view container.
		// redundant: parent->ObjectClicked(container->Handle(), heldKeys);
	}

	void ObjectSelect(int handle) override
	{
		// As far as selection-logic goes, Clicking on object in sub-view translates to clicking sub-view container.
		parent->ObjectSelect(container->Handle());
	}

	bool AddPatchCable(int32_t fromModule, int fromPin, int32_t toModule, int toPin, int colorIndex, bool placeAtBack) override
	{
		return parent->AddPatchCable(fromModule, fromPin, toModule, toPin, colorIndex, placeAtBack);
	}
	void RemovePatchCable(int32_t fromModule, int fromPin, int32_t toModule, int toPin) override
	{
		parent->RemovePatchCable(fromModule, fromPin, toModule, toPin);
	}
	void DragSelection(gmpi::drawing::Size offset) override{}
	void NotDragging() override {}

	IPresenter* CreateSubPresenter(int32_t containerHandle) override
	{
		return parent->CreateSubPresenter(containerHandle);
	}

//	void ResizeSubView();

	void OnNotify(Notifier* sender, int lHint, void* pHint = 0) override
	{
		MfcDocPresenterBase::OnNotify(sender, lHint, pHint);

		switch (lHint)
		{
		case OM_DELETE:
			container->UnRegisterObserver(this);
			container = nullptr;
			break;

		case OM_ONCHANGE_CHILD_POSITION_PANEL:

			OnChangedChildPosition((CDocOb*)pHint); // informs and updates child position.
//			ResizeSubView();
			/*
				parent->DirtyView(); // brute=force way of redrawing, but need to re-calc boundaries.
				// Might be nice to have a "redraw on mouse up" for parent so dragging is not so jerky. Parent is low-priority in this case.
			*/
			break;

		case OM_GUI_PLUG_DEFAULT_CHANGE: // e.g. changing rectangle color in nested-view
		case OM_REFRESH_PRESENTERS:
		case OM_ADD_CHILD:
			parent->DirtyView(); // brute=force way of redrawing.
			break;
		}
	}

	void SetViewPosition(gmpi::drawing::RectL positionRect) override
	{
		// N/A
		assert(false);
	}
	SE2::ModuleView* HandleToObject(int handle) override
	{
		return parent->HandleToObject(handle);
	}
	void InitializeGuiObjects() override
	{
		// N/A
		assert(false);
	}
	void DirtyView() override
	{
		parent->DirtyView();
	}

	void RefreshView() override
	{
		// N/A
		assert(false);
	}
	void ResizeModule(int handle, int dragNodeX, int dragNodeY, gmpi::drawing::Size) override
	{
	}
	int32_t OnCommand(PresenterCommand, int32_t /* moduleHandle */ = -1) override
	{
		return gmpi::MP_UNHANDLED;
	}

	void populateContextMenu(gmpi::api::IContextItemSink* menu, gmpi::drawing::Point p, int32_t moduleHandle, int32_t nodeIndex = -1) override
	{
		parent->populateContextMenu(menu, p, moduleHandle, nodeIndex);
#if 0 //def _DEBUG // for now
		if (container->isRackModule())
		{
			contextMenu->currentCallback = [=](int32_t idx, GmpiDrawing_API::MP1_POINT point) { return onContextMenu(idx, point); };
			{
				contextMenu->BeginSubMenu("Add Rack Module");
				contextMenu->EndSubMenu();
			}

			contextMenu->AddItem("Remove Rack Module", 0);
		}
#endif
	}
	/* todo
	int32_t onContextMenu(int32_t idx) override
	{
		switch (idx)
		{
		case 0:
			container->OnDelete();
			break;
		}

		return 0;
	}
	*/

	gmpi::drawing::Point GetViewCenter() override
	{
		const auto s = container->getPanelWndOffset();
		return { static_cast<float>(s.width), static_cast<float>(s.height) };
	}
	void SetViewCenter(gmpi::drawing::Point center) override
	{
		container->setPanelWndOffset({ static_cast<int32_t>(center.x), static_cast<int32_t>(center.y) });
	}
	void SetPanZoom(gmpi::drawing::Point center, float) override
	{
		SetViewCenter(center);
	}
	void SetZoomFactor(float zoomFactor) override {}
	float GetZoomFactor() override { return 1.0f; }

	int AddModule(const wchar_t* uniqueid, gmpi::drawing::Point point) override
	{
		return -1;
	}
};


class MfcDocPresenter : public MfcDocPresenterBase, public se_sdk::TimerClient
{
	std::map<int, SE2::ModuleView*> guiObjectMap_;

	bool viewDirty;
	int viewType;
//	std::unique_ptr<GmpiSdk::ContextMenuHelper::ContextMenuCallbacks> contextMenuCallbacks;
	bool isDraggingModules{};
  gmpi::drawing::SizeL DraggingModulesOffset = {};
	std::function<void(class CContainer*)> onContainerClosing;

public:
	MfcDocPresenter(class CContainer* pcontainer, int view_flag, std::function<void(class CContainer*)> onContainerClosing_ = nullptr);

	~MfcDocPresenter()
	{
		StopTimer(); // safer to do here than in base-class (else might call partially destructed timer function).
	}

	// IPresenter //////////////////////////////////////
	SE2::IPresenter* CreateSubPresenter(int32_t containerHandle) override;

	void SetViewPosition(gmpi::drawing::RectL positionRect) override
	{
//		CRect r(positionRect.left, positionRect.top, positionRect.right, positionRect.bottom);
		container->SetViewPos(viewType, positionRect);
	}

	gmpi::drawing::Point GetViewCenter() override
	{
		return container->GetViewCenter(viewType);
	}

	void SetViewCenter(gmpi::drawing::Point center) override
	{
		container->SetViewCenter(viewType, center);
	}
	void SetPanZoom(gmpi::drawing::Point center, float zoomFactor) override
	{
		container->SetPanZoom(viewType, center, zoomFactor);
	}
	void SetZoomFactor(float zoomFactor) override
	{
		container->SetZoom(viewType, zoomFactor);
	}
	float GetZoomFactor() override
	{
		return container->GetZoom(viewType);
	}

	SE2::RackLayout getRackLayout() override;
	bool DeleteSelection() override;

	SE2::ModuleView* HandleToObject(int handle) override
	{
		auto it = guiObjectMap_.find(handle);
		if (it != guiObjectMap_.end())
		{
			return (*it).second;
		}
		return nullptr;
	}

	void InitializeGuiObjects() override
	{
		for (auto itn = guiObjectMap_.begin(); itn != guiObjectMap_.end(); ++itn)
		{
			(*itn).second->initialize();
		}
	}

	void setView(SE2::ViewBase* pview) override
	{
		view = pview;
		OnTimer(); // intial refresh.
		StartTimer(50);
	}

	bool editEnabled() override
	{
		return viewType != CF_PANEL_VIEW || container->EditEnabled();
	}

	int AddModule(const wchar_t* uniqueid, gmpi::drawing::Point point) override;
	std::vector<int32_t> AddPrefab(const wchar_t* uniqueid, gmpi::drawing::Point point) override;
	bool AddPatchCable(int32_t fromModule, int fromPin, int32_t toModule, int toPin, int colorIndex, bool placeAtBack) override;
	void RemovePatchCable(int32_t fromModule, int fromPin, int32_t toModule, int toPin) override;

	void ObjectClicked(int handle, int heldKeys) override;
	void ObjectSelect(int handle) override;

	void DragSelection(gmpi::drawing::Size) override;
	void NotDragging() override;
	void ResizeModule(int handle, int dragNodeX, int dragNodeY, gmpi::drawing::Size) override;
	int32_t OnCommand(PresenterCommand c, int32_t moduleHandle = -1) override;
		// - HERE --

	void populateContextMenu(gmpi::api::IContextItemSink* menu, gmpi::drawing::Point p, int32_t moduleHandle, int32_t nodeIndex = -1) override;
	int32_t onContextMenu(int32_t idx);

	void DoModuleCommand(int32_t commandId, CUG * module);
	int32_t onContextMenuLine(CLine2 * line, int32_t nodeIndex, int32_t idx);

	//void OpenPanel(int32_t result, CContainer* containerModule);
	// IPresenter //////////////////////////////////////

	// MFC -> View
	void OnNotify(Notifier* sender, int lHint, void* pHint = 0) override
	{
		MfcDocPresenterBase::OnNotify(sender, lHint, pHint);

		switch (lHint)
		{
		case OM_SCREENSHOT:
		case OM_SHOW_PROPERTIES:
		case OM_DOWNSTREAM_PLUG_CONNECT2:
		case OM_WPF_REPLACE_DIALOG:
		case OM_WPF_UPDATE_PRESET_BROWSER:
		case OM_UPDATE_PRESET_BROWSER_PATCH:
		case OM_REFRESH_PARAMETERS:
//		case OM_REFRESH_UI: // indicates Container wants to update it's little patch selector. Not entire view.
			break;

		case OM_DELETE_DOC:
		case OM_DELETE:
			StopTimer(); // otherwise it can crash trying to access the null container ptr.
			if (onContainerClosing)
				onContainerClosing(container);
			container->UnRegisterObserver(this);
			container = nullptr;
			// NOTE: this used to call view->DoClose() to WM_CLOSE the view's window, but the
			// window pointer was never wired up so it has been a no-op since the GMPI-UI
			// refactor - window teardown is handled elsewhere. Don't revive it: in the editor
			// the view's HWND is the MAIN window (HostedView), which this would close.
			break;

		case OM_ONCHANGE_CHILD_SELECTED:
			OnChangedChildSelected((CDocOb*)pHint);
			break;

		case OM_ONCHANGE_CHILD_POSITION_STRUCT:
			if (viewType == CF_STRUCTURE_VIEW)
				OnChangedChildPosition((CDocOb*)pHint);
			break;

		case OM_ONCHANGE_CHILD_POSITION_PANEL:
			if (viewType == CF_PANEL_VIEW)
				OnChangedChildPosition((CDocOb*)pHint);
			break;

		case OM_ONCHANGE_CHILD_COLOUR:
			OnConnectorColorChanged((CDocOb*)pHint);
			break;

		case OM_CPU_UPDATE:
			OnCpuUpdate((CUG*)pHint);
			break;

		case OM_HOVER_SCOPE_VALUE:
			OnHoverScopeUpdate(pHint);
			break;

		case OM_HOVER_SCOPE_WAVEFORM:
			OnHoverScopeWaveformUpdate(pHint);
			break;

		case OM_NAME_CHANGE: // plug name change
			if (viewType == CF_STRUCTURE_VIEW)
				DirtyView();
			break;

		case OM_VIEW_PANORZOOM_CHANGED:
			if (auto topView = dynamic_cast<SE2::TopView*>(view))
			{
				topView->setPanZoom(container->GetViewCenter(viewType), container->GetZoom(viewType));
			}
			break;

		case OM_REFRESH_PRESENTERS:
		case OM_GUI_PLUG_DEFAULT_CHANGE:
		default:
			DirtyView();
			break;
		}
	}

	void DirtyView() override
	{
//		_RPTN(_CRT_WARN, "DirtyView() %S : %s\n", container->GetName().c_str(), viewType == CF_PANEL_VIEW ? "Panel" :"Structure");
        // calls RefreshView() on timer
        viewDirty = true;
	}

	bool OnTimer() override;

	void UnloadView();

	void RefreshView() override;
};
