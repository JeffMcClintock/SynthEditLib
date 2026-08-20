#pragma once

#include <functional>
#include <string>

class CSynthEditAppBase;

// Single source of truth for "user is dragging a new module from the browser into a view".
//
// Lifecycle:
//   1. User selects a module in the browser           -> ModuleBrowser calls BeginDrag(id)
//   2. Cross cursor is shown via the registered hook
//   3a. User clicks in the editor                     -> presenter inserts module, then calls EndDrag()
//   3b. User presses ESC                              -> browser calls EndDrag()
//
// On every state change the manager fires OM_DRAG_NEW_MODULE so existing observers
// (MfcDocPresenter, MainWindow, Mac SynthEditBridge) react as before. The notification
// payload is the module-id c-str when starting, nullptr when ending — same contract
// the legacy code already understands.
class ModuleDragAndDropManager
{
public:
	explicit ModuleDragAndDropManager(CSynthEditAppBase* app);

	// Platform layer (MainWindow on WinUI, SynthEditBridge on Mac) registers a callback
	// that shows the cross cursor when isCross==true and clears it when false.
	void SetCursorHandler(std::function<void(bool isCross)> handler);

	// Begin dragging a module. moduleId must be non-empty; an empty id is treated as EndDrag().
	void BeginDrag(const std::string& moduleId);

	// End or cancel the drag. Idempotent — safe to call when no drag is active.
	void EndDrag();

	// Insert a module immediately at the active view's centre — used for the
	// double-click-to-insert shortcut. Cancels any in-progress drag and fires
	// OM_INSERT_MODULE_AT_VIEW_CENTER for the platform layer to action.
	void InsertAtViewCenter(const std::string& moduleId);

	// --- Source of truth queries -------------------------------------------------
	bool IsDragging() const { return !draggedModuleId_.empty(); }
	bool IsCursorCross() const { return IsDragging(); }
	const std::string& GetDraggedModuleId() const { return draggedModuleId_; }

private:
	CSynthEditAppBase* app_;
	std::string draggedModuleId_;
	std::string pendingInsertModuleId_; // keeps the c_str alive across the OM_INSERT_MODULE_AT_VIEW_CENTER dispatch
	std::function<void(bool)> cursorHandler_;
};
