#pragma once
#include <list>
#include "../se_sdk3/mp_sdk_stdint.h"
#include "../se_sdk3/Drawing_API.h"
#include "../shared/PatchCables.h"

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
		virtual void RefreshView() = 0;
		virtual bool editEnabled() = 0;
		virtual IPresenter* CreateSubPresenter(int32_t containerHandle) = 0;
		virtual void SetViewPosition(GmpiDrawing_API::MP1_RECT_L positionRect) = 0;
		virtual void GetViewScroll(int32_t& returnX, int32_t& returnY) = 0;
		virtual void SetViewScroll(int32_t x, int32_t y) = 0;
		virtual int GetSnapSize() = 0;
		virtual SE2::ModuleView* HandleToObject(int handle) = 0; // Seems out-of-place, because can have two objects w same handle (module + adorner).
		virtual void InitializeGuiObjects() = 0;

		virtual void ObjectClicked(int handle, int heldKeys) = 0;
		virtual void ObjectSelect(int handle) = 0;

		virtual void populateContextMenu(gmpi::IMpContextItemSink* menuItemList, GmpiDrawing_API::MP1_POINT p, int32_t moduleHandle, int32_t nodeIndex = -1) = 0;
		virtual int32_t onContextMenu(int32_t idx) = 0;

		virtual void AddModule(const wchar_t* uniqueid, GmpiDrawing_API::MP1_POINT point) = 0;
		virtual bool CanConnect(CableType cabletype, int32_t fromModule, int fromPin, int32_t toModule, int toPin) = 0;
		virtual bool AddConnector(int32_t fromModule, int fromPin, int32_t toModule, int toPin, bool placeAtBack) = 0;
		virtual void HighlightConnector(int32_t moduleHandle, int pin) = 0;
		virtual bool AddPatchCable(int32_t fromModule, int fromPin, int32_t toModule, int toPin, int colorIndex, bool placeAtBack = false) = 0;
		virtual void RemovePatchCable(int32_t fromModule, int fromPin, int32_t toModule, int toPin) = 0;
		virtual void DragSelection(GmpiDrawing_API::MP1_SIZE offset) = 0;
		virtual void DragNode(int32_t fromModule, int32_t nodeIdx, GmpiDrawing_API::MP1_POINT point) = 0;
		virtual void InsertNode(int32_t fromLine, int32_t nodeInsertIdx, GmpiDrawing_API::MP1_POINT point) = 0;
		virtual void ResizeModule(int handle, int dragNodeX, int dragNodeY, GmpiDrawing_API::MP1_SIZE) = 0;
		virtual int32_t OnCommand(PresenterCommand c, int32_t moduleHandle = -1) = 0;
		virtual void OnFrameGotFocus() = 0;
		virtual IGuiHost2* GetPatchManager() = 0;
		virtual int32_t GenerateTemporaryHandle() = 0;
		virtual int32_t MP_STDCALL LoadPresetFile_DEPRECATED(const char* presetFilePath) = 0;
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
