#pragma once
#include <list>
#include "../shared/PatchCables.h"
#include "GmpiUiDrawing.h"
#include "helpers/ContextMenuHelper.h"

class IGuiHost2;
enum class PresenterCommand;

namespace SE2
{
class ViewBase;
class IViewChild;
class ModuleView;
enum class CableType;

	// The presenter class mediates between the view and the model.
	//
	//                 [Model]<->[Presentor]<->[View]
	//
	// Currently we're assuming a fairly "passive view" design. i.e. the view has no direct link to the model, to allow undo/redo to be inserted and to
	// support both MFC and json versions of the model.

	// Currently the View is NOT represented by an abstraction, to save pissing about.
	struct feedbackPinUi
	{
		int32_t moduleHandle;
		int32_t pinIndex;
		std::string debugModuleName;
	};

	class IPresenter
	{
	public:
		virtual ~IPresenter() {}
		virtual void setView(SE2::ViewBase* pview) = 0;
		virtual void DirtyView() = 0; // async RefreshView(), editor only.
		virtual void RefreshView() = 0;
		virtual bool editEnabled() = 0;
		virtual IPresenter* CreateSubPresenter(int32_t containerHandle) = 0;
		virtual void SetViewPosition(gmpi::drawing::RectL positionRect) = 0;
		virtual gmpi::drawing::Point GetViewCenter() = 0;
		virtual void SetViewCenter(gmpi::drawing::Point center) = 0;
		virtual void SetZoomFactor(float zoomFactor) = 0;
		virtual void SetPanZoom(gmpi::drawing::Point center, float zoomFactor) = 0;
		virtual float GetZoomFactor() = 0;
		virtual int GetSnapSize() = 0;
		virtual SE2::ModuleView* HandleToObject(int handle) = 0; // Seems out-of-place, because can have two objects w same handle (module + adorner).
		virtual void InitializeGuiObjects() = 0;

		virtual void ObjectClicked(int handle, int heldKeys) = 0;
		virtual void ObjectSelect(int handle) = 0;

		virtual void populateContextMenu(gmpi::api::IContextItemSink* menu, gmpi::drawing::Point p, int32_t moduleHandle, int32_t nodeIndex = -1) = 0;
//		virtual int32_t onContextMenu(int32_t idx) = 0;

		virtual void AddModule(const wchar_t* uniqueid, gmpi::drawing::Point point) = 0;
		virtual bool CanConnect(CableType cabletype, int32_t fromModule, int fromPin, int32_t toModule, int toPin) = 0;
		virtual bool AddConnector(int32_t fromModule, int fromPin, int32_t toModule, int toPin, bool placeAtBack) = 0;
		virtual void HighlightConnector(int32_t moduleHandle, int pin, int highlightType) = 0;
		virtual bool AddPatchCable(int32_t fromModule, int fromPin, int32_t toModule, int toPin, int colorIndex, bool placeAtBack = false) = 0;
		virtual void RemovePatchCable(int32_t fromModule, int fromPin, int32_t toModule, int toPin) = 0;
		virtual void DragSelection(gmpi::drawing::Size offset) = 0;
		virtual void NotDragging() = 0;
		virtual void DragNode(int32_t fromModule, int32_t nodeIdx, gmpi::drawing::Point point) = 0;
		virtual void InsertNode(int32_t fromLine, int32_t nodeInsertIdx, gmpi::drawing::Point point) = 0;
		virtual void ResizeModule(int handle, int dragNodeX, int dragNodeY, gmpi::drawing::Size) = 0;
		virtual int32_t OnCommand(PresenterCommand c, int32_t moduleHandle = -1) = 0;
		virtual void OnFrameGotFocus() = 0;
		virtual IGuiHost2* GetPatchManager() = 0;
		virtual int32_t GenerateTemporaryHandle() = 0;
		virtual int32_t LoadPresetFile_DEPRECATED(const char* presetFilePath) = 0;
		virtual void OnChildDspMessage(void* msg) = 0;
        virtual void OnControllerDeleted() = 0;
		virtual void InsertRackModule(const std::wstring& prefabFilePath) = 0;		
		virtual void setHoverScopePin(int32_t moduleHandle, int pin) = 0;
		virtual void HighlightFeedback(std::list< std::pair<feedbackPinUi, feedbackPinUi> >& feedbackConnectors) = 0;
		virtual void ClearFeedbackHighlights() = 0;
		virtual SE2::IViewChild* createModulePicker(SE2::ViewBase*) = 0;
		// - HERE --
	};
}

class PresenterBase : public SE2::IPresenter
{
public:
	bool CanConnect(SE2::CableType cabletype, int32_t fromModule, int fromPin, int32_t toModule, int toPin) override;
};