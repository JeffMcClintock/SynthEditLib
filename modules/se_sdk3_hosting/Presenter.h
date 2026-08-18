#pragma once
#include <list>
#include <vector>
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

	// "Rack mode": the view is an empty Eurorack case that modules bolt into.
	// Modules occupy whole rows one rack-unit tall and a whole number of HP
	// (the standard horizontal pitch) wide. Proportions follow VCV Rack.
	struct RackLayout
	{
		bool enabled = false;
		gmpi::drawing::Point origin{}; // document coords of row 0, column 0
		float hpWidth = 15.0f;         // one unit of horizontal pitch
		float rowHeight = 380.0f;      // one rack row, rail to rail
		float railHeight = 15.0f;      // mounting rail along each row's top and bottom edge
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
		// Disabled everywhere except a rack-mode project's top-level panel view.
		virtual RackLayout getRackLayout() { return {}; }
		virtual SE2::ModuleView* HandleToObject(int handle) = 0; // Seems out-of-place, because can have two objects w same handle (module + adorner).
		virtual void InitializeGuiObjects() = 0;

		virtual void ObjectClicked(int handle, int heldKeys) = 0;
		virtual void ObjectSelect(int handle) = 0;

		virtual void populateContextMenu(gmpi::api::IContextItemSink* menu, gmpi::drawing::Point p, int32_t moduleHandle, int32_t nodeIndex = -1) = 0;
//		virtual int32_t onContextMenu(int32_t idx) = 0;

		// Returns the unique handle of the inserted module, or -1 if none was
		// created (e.g. prefab paths that spawn multiple modules, or stub
		// presenters that don't actually mutate the document).
		virtual int AddModule(const wchar_t* uniqueid, gmpi::drawing::Point point) = 0;
		// Insert a prefab ("*P=<file>") and return the handles of every
		// top-level module it created, in insertion order. Empty means nothing
		// was created -- a missing or unreadable prefab file, or a stub
		// presenter. This exists because AddModule CANNOT answer: a prefab may
		// hold several top-level modules, so there is no single handle to
		// return, which is what its -1 above means. Callers previously had to
		// snapshot the container's handles and diff them; two of them did, and
		// each invented its own idiom.
		//
		// Defaulted rather than pure so the several stub and read-only
		// presenters implementing this interface need no change, the same way
		// getRackLayout/SetModuleRect/GetModuleRect are defaulted above.
		virtual std::vector<int32_t> AddPrefab(const wchar_t* /*uniqueid*/, gmpi::drawing::Point /*point*/) { return {}; }
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
		// Set the module's persisted view-rect absolutely, bypassing
		// ResizeModule's drag-node-relative delta semantics. Used by isNull
		// centering in ViewBase::arrange to set both position and size in
		// one shot — the delta math compounds badly when the persisted rect
		// has drifted from bounds_/JSON (e.g. across view re-opens).
		virtual void SetModuleRect(int handle, gmpi::drawing::Rect rect) {}
		// Read the module's persisted view-rect. Used by isNull centering to
		// detect when the data model says the user has already positioned the
		// module — the JSON snapshot used to seed bounds_ may be stale.
		virtual gmpi::drawing::Rect GetModuleRect(int handle) { return {}; }
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