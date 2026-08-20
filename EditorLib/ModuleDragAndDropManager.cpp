#include "ModuleDragAndDropManager.h"

#include "SynthEditAppBase.h"
#include "Notify_msg.h"

ModuleDragAndDropManager::ModuleDragAndDropManager(CSynthEditAppBase* app)
	: app_(app)
{
}

void ModuleDragAndDropManager::SetCursorHandler(std::function<void(bool)> handler)
{
	cursorHandler_ = std::move(handler);
}

void ModuleDragAndDropManager::BeginDrag(const std::string& moduleId)
{
	if (moduleId.empty())
	{
		EndDrag();
		return;
	}

	// Re-entrant BeginDrag (e.g. clicking a different module mid-drag) just swaps the id.
	draggedModuleId_ = moduleId;

	if (cursorHandler_)
		cursorHandler_(true);

	// Notify views so they can begin tracking the drag. Payload is the module id;
	// observers cast back to const char*.
	if (app_)
		app_->VO_Notify(OM_DRAG_NEW_MODULE, (void*)draggedModuleId_.c_str());
}

void ModuleDragAndDropManager::EndDrag()
{
	if (!IsDragging())
		return;

	draggedModuleId_.clear();

	if (cursorHandler_)
		cursorHandler_(false);

	if (app_)
		app_->VO_Notify(OM_DRAG_NEW_MODULE, nullptr);
}

void ModuleDragAndDropManager::InsertAtViewCenter(const std::string& moduleId)
{
	if (moduleId.empty() || !app_)
		return;

	// Drop any in-progress drag — double-click supersedes drag.
	EndDrag();

	// Stash the id so the notification payload survives until the platform
	// handler reads it (the caller's c_str may dangle right after this returns).
	pendingInsertModuleId_ = moduleId;
	app_->VO_Notify(OM_INSERT_MODULE_AT_VIEW_CENTER, (void*)pendingInsertModuleId_.c_str());
}
