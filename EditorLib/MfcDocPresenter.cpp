#include "resource.h"
#include "ILicenseState.h"
#ifdef _WIN32
#include "afxres.h"
#endif
#include <fstream>
#include <vector>
#include <algorithm>
#include <set>
#include <filesystem>
#include "helpers/browseto.h"

#include "MfcDocPresenter.h"

#include "Application.h"
#include "SynthEditDocBase.h"

#include "CContainer.h"
#include "CLine2.h"
#include "it_doc_ob.h"
#include "notify.h"
#include "module_info.h"
#include "UgDatabase.h"
#include "PatchManager.h"
#include "modules/shared/xplatform_modifier_keys.h"
#include "SkinMgr.h"
#include "modules/se_sdk3_hosting/PresenterCommands.h"
#include "modules/se_sdk3_hosting/UgDatabase2.h"
#include "SuspendDSP.h"
#include "ModulePicker.h"
#include "ModuleDragAndDropManager.h"

MfcDocPresenter::MfcDocPresenter(class CContainer* pcontainer, int view_flag, std::function<void(class CContainer*)> onContainerClosing_) : MfcDocPresenterBase(pcontainer)
, viewDirty(true)
, viewType(view_flag)
, onContainerClosing(onContainerClosing_)
{
	pcontainer->Document()->setSelectedView(pcontainer->Handle(), view_flag);
}

void MfcDocPresenter::ObjectClicked(int handle, int heldKeys)
{
	if (handle >= 0)
	{
		auto o = dynamic_cast<CDocOb*>(container->Document()->uniqueIdDatabase.HandleToObjectWithNull(handle));
		if (o)
		{
			if(!container->getLocked() || viewType != CF_PANEL_VIEW || o->isRackModule())
			{
				o->OnClicked(heldKeys);
				return;
			}
		}
	}
	
	// click on view itself, or decorative object (like background bitmap).
	if (0 == (heldKeys & (gmpi_gui_api::GG_POINTER_KEY_SHIFT| gmpi_gui_api::GG_POINTER_KEY_CONTROL)))
		container->setAllSelected(false);
}

void MfcDocPresenter::ObjectSelect(int handle)
{
	if (handle >= 0)
	{
		auto o = dynamic_cast<CDocOb*>(container->Document()->uniqueIdDatabase.HandleToObjectWithNull(handle));
		if (o)
		{
			o->SetSelected(true, false);
			return;
		}
	}
}

int MfcDocPresenter::AddModule(const wchar_t* uniqueid, gmpi::drawing::Point point)
{
	const std::wstring moduleTypeId(uniqueid);
	const auto containerHandle = container->Handle();
	const auto handle = container->Document()->uniqueIdDatabase.GenerateUniqueHandleValue();
	const auto lViewType = viewType;

	auto redo = [containerHandle, moduleTypeId, handle, lViewType, point](CSynthEditDocBase* document)
		{
			auto container = dynamic_cast<CContainer*>(document->uniqueIdDatabase.HandleToObjectWithNull(containerHandle));
			auto docob = container->OnNewUG(lViewType, moduleTypeId, handle, { static_cast<int>(point.x), static_cast<int>(point.y) });
			if (docob) // Prefabs may be many modules so don't return any docob.
			{
				docob->OnProperty();
			}
		};

	redo(container->Document());

	// Support Undo.
	if (Left(uniqueid, 3) == (L"*P=")) // indicates a prefab or vst plugin
	{
		// multiple modules involved, needs full serialise-based undo.
		container->Document()->Application()->CommandManager()->DoCheckpoint(L"Insert Module");
	}
	else
	{
		auto undo = [this, handle](CSynthEditDocBase* document)
			{
				dynamic_cast<CDocOb*>(document->uniqueIdDatabase.HandleToObjectWithNull(handle))->OnDelete();
			};

		container->Document()->Application()->CommandManager()->DoCheckpoint(L"Insert Module", undo, redo);
	}

	// end drag (changes cursor back)
	if (auto m = container->Document()->Application()->getModuleDragAndDropManager())
		m->EndDrag();

	// Prefabs (uniqueid prefix "*P=") spawn multiple modules with their own
	// handles; the locally-generated handle isn't tied to a single module in
	// that case, so don't pretend it is.
	if (Left(uniqueid, 3) == L"*P=")
		return -1;

	return handle;
}

std::vector<int32_t> MfcDocPresenter::AddPrefab(const wchar_t* uniqueid, gmpi::drawing::Point point)
{
	// Deliberately built ON TOP of AddModule rather than beside it: the insert,
	// the undo checkpoint (prefabs need the full serialise-based one) and the
	// drag-cursor reset all stay in exactly one place, and this method adds only
	// the answer AddModule structurally cannot give.
	//
	// The handles come from a before/after diff of the container's top-level
	// modules, NOT from the selection. Paste does select what it inserted
	// (CContainer::OnEditPaste calls setAllSelected(false) then SetSelected(true)
	// on each pasted module), and reading that would be cheaper -- but selection
	// is a UI concern any later code is entitled to change, while the diff is
	// exact and answers the question actually being asked.
	//
	// dynamic_cast<CUG*> is the necessary filter, not decoration: a container's
	// child list holds CLine2 connections alongside modules.
	auto topLevelModules = [](CContainer* c)
		{
			std::vector<int32_t> handles;
			it_doc_ob it(c);
			for (it.First(); !it.IsDone(); it.Next())
			{
				if (auto* m = dynamic_cast<CUG*>(it.CurrentItem()); m)
					handles.push_back(m->Handle());
			}
			return handles;
		};

	const auto beforeList = topLevelModules(container);
	const std::set<int32_t> before(beforeList.begin(), beforeList.end());

	AddModule(uniqueid, point);

	std::vector<int32_t> created;
	for (const auto h : topLevelModules(container))
	{
		if (before.find(h) == before.end())
			created.push_back(h);
	}

	// CContainer::AddSorted PREPENDS modules (and appends lines), so iteration
	// is reverse-insertion order. Reverse it so a caller reading created[0] gets
	// the prefab's first module rather than its last -- which for a single-module
	// prefab is the same thing, and for a multi-module one is the difference
	// between a stable contract and a coin toss.
	std::reverse(created.begin(), created.end());

	return created;
}

bool MfcDocPresenterBase::CanConnect(SE2::CableType cabletype, int32_t fromModule, int fromPin, int32_t toModule, int toPin)
{
	if(cabletype == SE2::CableType::PatchCable)
		return PresenterBase::CanConnect(cabletype, fromModule, fromPin, toModule, toPin);

	auto fromUg = dynamic_cast<CUG*>(container->Document()->uniqueIdDatabase.HandleToObjectWithNull(fromModule));
	auto toUg = dynamic_cast<CUG*>(container->Document()->uniqueIdDatabase.HandleToObjectWithNull(toModule));

	if (!fromUg || !toUg)
		return false;

	// Checking two plugs. Pin indices come from the caller unvalidated; GetPlug
	// returns nullptr for an out-of-range one, and canConnectplugs dereferences
	// both of its arguments, so reject here rather than there.
	auto fromPlug = fromUg->GetPlug(fromPin);
	auto toPlug = toUg->GetPlug(toPin);

	if (!fromPlug || !toPlug)
		return false;

	std::wstring err;
	return canConnectplugs(fromPlug, toPlug, err);
}

bool MfcDocPresenterBase::AddConnector(int32_t fromModule, int fromPin, int32_t toModule, int toPin, bool placeAtBack)
{
    // Validate module handles and plugs before attempting to connect.
    {
        auto* fromObj = container->Document()->uniqueIdDatabase.HandleToObjectWithNull(fromModule);
        auto* toObj   = container->Document()->uniqueIdDatabase.HandleToObjectWithNull(toModule);

        // If either end is invalid, ignore this request (can happen under stress clicks).
        if (!fromObj || !toObj)
            return false;

        CUG* fromUg = dynamic_cast<CUG*>(fromObj);
        CUG* toUg   = dynamic_cast<CUG*>(toObj);

        if (!fromUg || !toUg)
            return false;

        // Out-of-range pin index yields nullptr; canConnectplugs would dereference it.
        IPlug* fromPlug = fromUg->GetPlug(fromPin);
        IPlug* toPlug   = toUg->GetPlug(toPin);

        if (!fromPlug || !toPlug)
            return false;

        std::wstring error_msg;
        if (!canConnectplugs(fromPlug, toPlug, error_msg))
        {
            if (!error_msg.empty()) // ignore accidental drag-to-myself
                container->Application()->SeMessageBox(error_msg.c_str(), L"", 0 /*MB_OK*/);

            return false;
        }
    }

    const auto containerHandle = container->Handle();
    const auto handle = container->Document()->uniqueIdDatabase.GenerateUniqueHandleValue();

    auto redo = [containerHandle, handle, fromModule, fromPin, toModule, toPin](CSynthEditDocBase* document)
        {
            auto* containerObj = document->uniqueIdDatabase.HandleToObjectWithNull(containerHandle);
            if (!containerObj)
                return;

            auto* fromObj = document->uniqueIdDatabase.HandleToObjectWithNull(fromModule);
            auto* toObj   = document->uniqueIdDatabase.HandleToObjectWithNull(toModule);
            if (!fromObj || !toObj)
                return;

            CUG* fromUg = dynamic_cast<CUG*>(fromObj);
            CUG* toUg   = dynamic_cast<CUG*>(toObj);
            if (!fromUg || !toUg)
                return;

            IPlug* fromPlug = fromUg->GetPlug(fromPin);
            IPlug* toPlug   = toUg->GetPlug(toPin);

            // Validated above before the checkpoint was recorded, but redo replays
            // against a document that has moved on since; ConnectPlugs dereferences
            // p_from unconditionally.
            if (!fromPlug || !toPlug)
                return;

        #ifdef _DEBUG
            std::wstring error_msg;
            assert(canConnectplugs(fromPlug, toPlug, error_msg));
        #endif

            ConnectPlugs(fromPlug, toPlug, handle);
        };

    auto undo = [handle](CSynthEditDocBase* document)
        {
            if (auto* obj = dynamic_cast<CDocOb*>(document->uniqueIdDatabase.HandleToObjectWithNull(handle)); obj)
                obj->OnDelete();
        };

    // Execute immediately
    redo(container->Document());

    // Support Undo.
    container->Document()->Application()->CommandManager()->DoCheckpoint(L"Connect", undo, redo);

    return true;
}

bool MfcDocPresenter::AddPatchCable(int32_t fromModule, int fromPin, int32_t toModule, int toPin, int colorIndex, bool placeAtBack)
{
	auto fromUg = HandleToObject(fromModule);
	auto toUg = HandleToObject(toModule);

	if (fromUg == nullptr || toUg == nullptr)
		return false;

	auto fromType = fromUg->getModuleType();
	auto toType = toUg->getModuleType();

	if (fromType == nullptr || toType == nullptr)
		return false;

	// Module_Info::plugs is a std::map keyed by pin id, so plugs[pin] on an unknown
	// pin inserts a null entry into the shared module description and then returns
	// it to be dereferenced. Look it up without the insert side effect. Note this is
	// the DSP-only pin list — a shorter, different index space to CUG::Plugs, which
	// also carries GUI pins, so an index valid there can still miss here.
	auto fromPinDesc = fromType->getPinDescriptionById(fromPin);
	auto toPinDesc = toType->getPinDescriptionById(toPin);

	if (fromPinDesc == nullptr || toPinDesc == nullptr)
		return false;

	int toPinDirection = toPinDesc->GetDirection();
	int fromPinDirection = fromPinDesc->GetDirection();

	if (fromPinDirection == toPinDirection)
		return false;

	if (fromPinDirection == DR_IN)
	{
		// Swap them round.
		std::swap(fromUg, toUg);
		std::swap(fromModule, toModule);
		std::swap(fromPin, toPin);
	}

	// Get "lowest" common PatchManager
	IGuiHost2* patchManager = nullptr;
	{
		auto pm1 = fromUg->Presenter()->GetPatchManager();
		auto pm2 = toUg->Presenter()->GetPatchManager();

		// If possible use nested patch manager, so cable is owned by instrument.
		if (pm1 == pm2)
		{
			patchManager = pm1;
		}
		else
		{
			// Else use "Main" patch container. Assume 'this' is the top-level presenter.
			patchManager = GetPatchManager();
		}
	}

	// get current cable list.
	const int parameterIdx = -1 - HC_PATCH_CABLES;
	auto parameterHandle = patchManager->getParameterHandle(-1, parameterIdx);
	assert(parameterHandle >= 0);

	SE2::PatchCables cableList(patchManager->getParameterValue(parameterHandle));

	// Check for existing identical cable. If so exit.
	for (auto& c : cableList.cables)
	{
		if (c.fromUgHandle == fromModule && c.fromUgPin == fromPin && c.toUgHandle == toModule && c.toUgPin == toPin)
			return false;
	}

	if(placeAtBack)
		cableList.insert(fromModule, fromPin, toModule, toPin, colorIndex);
	else
		cableList.push_back(fromModule, fromPin, toModule, toPin, colorIndex);

	// update module parameter holding cable list.
	auto localToPreventTrashedReturnValue = cableList.Serialise();
	patchManager->setParameterValue(localToPreventTrashedReturnValue, parameterHandle);

	return true;
}

void MfcDocPresenter::RemovePatchCable(int32_t fromModule, int fromPin, int32_t toModule, int toPin)
{
	// Get "lowest" common PatchManager
	IGuiHost2* patchManager = nullptr;
	{
		auto fromUg = HandleToObject(fromModule);
		auto toUg = HandleToObject(toModule);

		if (fromUg == nullptr || toUg == nullptr)
			return;

		auto pm1 = fromUg->Presenter()->GetPatchManager();
		auto pm2 = toUg->Presenter()->GetPatchManager();

		// If possible use nested patch manager, so cable is owned by instrument.
		if (pm1 == pm2)
		{
			patchManager = pm1;
		}
		else
		{
			// Else use "Main" patch container. Assume 'this' is the top-level presenter.
			patchManager = GetPatchManager();
		}
	}

	const int parameterIdx = -1 - HC_PATCH_CABLES;
	while (true)
	{
		auto parameterHandle = patchManager->getParameterHandle(-1, parameterIdx);
		SE2::PatchCables cableList(patchManager->getParameterValue(parameterHandle));

		for (auto it = cableList.cables.begin(); it != cableList.cables.end(); )
		{
			if ((*it).fromUgHandle == fromModule && (*it).toUgHandle == toModule && (*it).fromUgPin == fromPin && (*it).toUgPin == toPin)
			{
				it = cableList.cables.erase(it);

				// update module parameter holding cable list.
				auto localToPreventTrashedReturnValue = cableList.Serialise();
				patchManager->setParameterValue(localToPreventTrashedReturnValue, parameterHandle);
				return;
				break;
			}
			else
			{
				++it;
			}
		}

		// If we failed to find patch-cable on sub-view's patchmanger, fall-back to "Main" patchmanager. Should only happen on patches created befor SE 1.4 Build 215
		if (patchManager != GetPatchManager())
			patchManager = GetPatchManager();
		else
			return;
	}
}

SE2::IPresenter* MfcDocPresenter::CreateSubPresenter(int32_t containerHandle)
{
	auto presenter = new MfcDocSubPresenter( dynamic_cast<CContainer*>(container->Document()->uniqueIdDatabase.HandleToObjectWithNull(containerHandle)));
	presenter->parent = this;
	return presenter;
}

bool MfcDocPresenter::OnTimer()
{
	if (viewDirty)
	{
		RefreshView();
	}
	return true;
}

void MfcDocPresenter::UnloadView()
{
	view->Unload();
}

void MfcDocPresenter::RefreshView()
{
	ExportFormatType targetType = viewType == CF_STRUCTURE_VIEW ? SAT_SYNTHEDIT_GUI_STRUCT : SAT_SYNTHEDIT_GUI_PANEL;

    // Drama: When exporting plugins we set the serialise flag of those that we need to export.
    // if a template is missing or whatever we pop up a message box, this causes the timer loop to execute..
    // which may cause the view to refresh, which CLEARs the serialise flags, ruining the export.
    // not sure why this was here. during refreshview, nothing seems to query the flag.
//	CModuleFactory::Instance()->ClearSerialiseFlags();

	Json::Value gui_json;
	container->ExportMain(gui_json, targetType);

#if 0 //def _DEBUG
	{
		stringstream out(std::ofstream::out);
		out << gui_json;
		auto jsonString = out.str();
		while(!jsonString.empty())
		{
			const int maxDebugStringSize = 1023;
			if(jsonString.size() > 1023)
			{
				_RPT1(_CRT_WARN, "%s", jsonString.substr(0, maxDebugStringSize).c_str());
				jsonString = jsonString.substr(maxDebugStringSize);
			}
			else
			{
				_RPT1(_CRT_WARN, "%s", jsonString.c_str());
				jsonString.clear();
			}
			_RPT0(_CRT_WARN, "\n");
		}
	}
#endif

	// to avoid/delay if mouse captured.
	guiObjectMap_.clear();
	view->Refresh(&gui_json, guiObjectMap_);

	viewDirty = false;
}

void MfcDocPresenterBase::OnChangedChildSelected(CDocOb* module)
{
	if (view) // these can currently be created temporarily and discarded for GDI views.
	{
		view->OnChangedChildSelected(module->Handle(), module->GetSelected());
	}
}

void MfcDocPresenterBase::OnChangedChildPosition(CDocOb* module)
{
	if (view)
	{
		if (auto line = dynamic_cast<CLine2*>(module); line)
		{
			std::vector<gmpi::drawing::Point> nodesTemp;
			for (auto& n : line->nodes)
			{
				nodesTemp.push_back({ static_cast<float>(n.x), static_cast<float>(n.y) });
			}

			view->OnChangedChildNodes(module->Handle(), nodesTemp);
		}
		else
		{
			// Convert rectangle type.
			auto r = module->getViewObRect(view->getViewType());
			gmpi::drawing::Rect gr(
				static_cast<float>(r.left),
				static_cast<float>(r.top),
				static_cast<float>(r.right),
				static_cast<float>(r.bottom)
			);

			view->OnChangedChildPosition(module->Handle(), gr);
		}
	}
}

// user hase deleted an object.
void MfcDocPresenterBase::OnChildDeleted(CDocOb* module)
{
	if (view)
	{
		if (auto line = dynamic_cast<CLine2*>(module); line)
		{
			DirtyView(); // brute=force way of redrawing.
		}
		else
		{
			view->RemoveModule(module->Handle());
		}
	}
}

void MfcDocPresenterBase::OnCpuUpdate(CUG* cug)
{
	auto m = HandleToObject(cug->Handle());

	if (m)
	{
		m->OnCpuUpdate(cug->cpu_meter.get());
	}
}

void MfcDocPresenterBase::OnHoverScopeUpdate(void* params)
{
	auto info = static_cast<handleAndString*>(params);

	if (auto m = HandleToObject(info->handle); m)
	{
		m->SetHoverScopeText(info->text);
	}
}

void MfcDocPresenterBase::OnHoverScopeWaveformUpdate(void* params)
{
	auto info = static_cast<handleAndWaveform*>(params);

	if (auto m = HandleToObject(info->handle); m)
		m->SetHoverScopeWaveform(std::move(info->data));
}

void MfcDocPresenterBase::InsertRackModule(const std::wstring& prefabFilePath)
{
	std::wstring fullFilename = L"C:\\SE\\SE16\\TideModules\\" + prefabFilePath + L".syntheditprefab"; // TEMP!!!
	assert(false);
	// TODO container->LoadPrefab(CF_STRUCTURE_VIEW, fullFilename, { 10, 10 });
}

void MfcDocPresenterBase::setHoverScopePin(int32_t moduleHandle, int pin)
{
	dynamic_cast<CUG*>(container->Document()->uniqueIdDatabase.HandleToObjectWithNull(moduleHandle))->setHoverScopePin(pin);
}

void MfcDocPresenterBase::HighlightFeedback(std::list< std::pair<SE2::feedbackPinUi, SE2::feedbackPinUi> >& feedbackConnectors)
{
	container->Application()->reportFeedbackErrorUi(feedbackConnectors);
}

void MfcDocPresenterBase::ClearFeedbackHighlights()
{
	container->Application()->ApplyHighlights(~PinHighlightFlag_UiFeedback, nullptr);
}

SE2::IViewChild* MfcDocPresenterBase::createModulePicker(SE2::ViewBase* pparent)
{
	return new ModulePicker(pparent);
}

void MfcDocPresenter::DragSelection(gmpi::drawing::Size offset)
{
  const int dx = static_cast<int>(offset.width);
	const int dy = static_cast<int>(offset.height);
	if (!isDraggingModules)
	{
		isDraggingModules = true;
        DraggingModulesOffset.width = dx;
		DraggingModulesOffset.height = dy;
	}
	else
	{
        DraggingModulesOffset.width += dx;
		DraggingModulesOffset.height += dy;
	}

 container->DragSelection(view->getViewType(), dx, dy);

	view->UpdateCablesBounds();
}

void MfcDocPresenter::NotDragging()
{
	if (!isDraggingModules)
		return;

	isDraggingModules = false;
	const auto containerHandle = container->Handle();

	// collect selected modules.
	DO_LIST selectedModules;
	container->GetSelection(selectedModules);

	// get handles.
	std::vector<int32_t> selectedModulesHandles;
	for (auto& m : selectedModules)
	{
		selectedModulesHandles.push_back(m->Handle());
	}

	const auto lViewType = viewType;

  const int offsetX = DraggingModulesOffset.width;
	const int offsetY = DraggingModulesOffset.height;
	auto redo = [containerHandle, lViewType, selectedModulesHandles, offsetX, offsetY](CSynthEditDocBase* document)
		{
			auto container = dynamic_cast<CContainer*>(document->uniqueIdDatabase.HandleToObjectWithNull(containerHandle));
			container->setAllSelected(false);

			for(auto handle : selectedModulesHandles)
			{
				if (auto docob = dynamic_cast<CDocOb*>(document->uniqueIdDatabase.HandleToObjectWithNull(handle)); docob)
				{
					docob->SetSelected(true);
				}
			}

           container->DragSelection(lViewType, offsetX, offsetY);
		};

 auto undo = [containerHandle, lViewType, selectedModulesHandles, offsetX, offsetY](CSynthEditDocBase* document)
		{
			auto container = dynamic_cast<CContainer*>(document->uniqueIdDatabase.HandleToObjectWithNull(containerHandle));
			container->setAllSelected(false);

			for (auto handle : selectedModulesHandles)
			{
				if (auto docob = dynamic_cast<CDocOb*>(document->uniqueIdDatabase.HandleToObjectWithNull(handle)); docob)
				{
					docob->SetSelected(true);
				}
			}

         container->DragSelection(lViewType, -offsetX, -offsetY);
		};

	container->Document()->Application()->CommandManager()->DoCheckpoint(L"Drag", undo, redo);
}

int32_t MfcDocPresenter::OnCommand(PresenterCommand c, int32_t moduleHandle)
{
	switch (c)
	{
	case PresenterCommand::Delete:
		if(moduleHandle > -1)
		{
			// used to delete a line that is picked up via ALT-click
			auto docob = dynamic_cast<CDocOb*>(container->Document()->uniqueIdDatabase.HandleToObjectWithNull(moduleHandle));
			if(docob)
				docob->OnDelete();
		}
		else
		{
			container->OnEditDelete();
		}
		break;

	case PresenterCommand::SelectAll:
		container->OnEditSelectAll();
		break;

	case PresenterCommand::Undo:
		container->Document()->Application()->CommandManager()->Undo();
		break;

	case PresenterCommand::Redo:
		container->Document()->Application()->CommandManager()->Redo();
		break;

	case PresenterCommand::Cut:
		container->Document()->Application()->CommandManager()->OnMenuCommand(container, viewType, -1, -1, ID_EDIT_CUT, L"Cut");
		break;

	case PresenterCommand::Copy:
		container->OnEditCopy();
		break;

	case PresenterCommand::Paste:
		container->Document()->Application()->CommandManager()->OnMenuCommand(container, viewType, -1, -1, ID_EDIT_PASTE, L"Paste");
		break;

	case PresenterCommand::ToFront:
		container->OnEditMoveFront();
		break;

	case PresenterCommand::ToBack:
		container->OnEditMoveBack();
		break;

	case PresenterCommand::Lock:
		container->setLocked(true);
		break;

	case PresenterCommand::Contain:
		container->OnEditContain();
		break;

	case PresenterCommand::UnContain:
		container->OnEditUnContain();
		break;

	case PresenterCommand::Open:
	{
		auto childContainer = dynamic_cast<CContainer*>(container->Document()->uniqueIdDatabase.HandleToObjectWithNull(moduleHandle));
		if (childContainer)
		{
			childContainer->OnMenuCommand(CF_STRUCTURE_VIEW, POPUP_MENU_STRUCTURE_WPF);
		}
		else
		{
			return gmpi::MP_UNHANDLED;
		}
	}
	break;

	case PresenterCommand::UnloadView:
		UnloadView();
		break;

	case PresenterCommand::RefreshView:
		// immediate RefreshView();
		DirtyView(); // deferred RefreshView()
		break;

	case PresenterCommand::ImportPrefab:
		container->OnMenuCommand(CF_STRUCTURE_VIEW, ID_INS_PREFAB);
		break;

	case PresenterCommand::PickupLineFrom:
		container->PickupLine(moduleHandle, true);
		break;

	case PresenterCommand::PickupLineTo:
		container->PickupLine(moduleHandle, false);
		break;

	case PresenterCommand::CancelPickupLine:
		container->ClearDragLine();
		break;

	case PresenterCommand::Debug:
	{
		// Ctrl+D: toggle the debugger (CPU meter) on every selected module.
		DO_LIST selection;
		container->GetSelection(selection);
		for (auto vo : selection)
		{
			if (auto ug = dynamic_cast<CUG*>(vo); ug)
				ug->AttachDebugger();
		}
	}
	break;

	default:
		assert(false);
		break;
	}

	return gmpi::MP_HANDLED;
}

void MfcDocPresenterBase::HighlightConnector(int32_t moduleHandle, int pin, int highlightType)
{
	auto m = dynamic_cast<CUG*>(container->Document()->uniqueIdDatabase.HandleToObjectWithNull(moduleHandle));
	if(m)
		m->HighlightLines(pin, highlightType);
}

void MfcDocPresenterBase::DragNode(int32_t fromModule, int32_t nodeIdx, gmpi::drawing::Point point)
{
	auto line = dynamic_cast<CLine2*>(container->Document()->uniqueIdDatabase.HandleToObjectWithNull(fromModule));
	if (line)
	{
		line->UpdateNode(nodeIdx + 1, point.x, point.y);
//don't work		view->UpdateCablesBounds();
	}
}

void MfcDocPresenterBase::InsertNode(int32_t fromLine, int32_t nodeInsertIdx, gmpi::drawing::Point point)
{
	auto line = dynamic_cast<CLine2*>(container->Document()->uniqueIdDatabase.HandleToObjectWithNull(fromLine));
	if (!line)
		return;

	line->InsertNode(nodeInsertIdx, point.x, point.y);

	const auto handle = fromLine;
	const auto idx = nodeInsertIdx;
	const auto x = point.x;
	const auto y = point.y;

	auto redo = [handle, idx, x, y](CSynthEditDocBase* document)
	{
		if (auto l = dynamic_cast<CLine2*>(document->uniqueIdDatabase.HandleToObjectWithNull(handle)); l)
		{
			l->InsertNode(idx, x, y);
			if (l->Container())
				l->Container()->VO_Notify(OM_ONCHANGE_CHILD_POSITION_STRUCT, (void*)l);
		}
	};

	auto undo = [handle, idx](CSynthEditDocBase* document)
	{
		if (auto l = dynamic_cast<CLine2*>(document->uniqueIdDatabase.HandleToObjectWithNull(handle)); l)
		{
			l->DeleteNode(idx);
			if (l->Container())
				l->Container()->VO_Notify(OM_ONCHANGE_CHILD_POSITION_STRUCT, (void*)l);
		}
	};

	container->Document()->Application()->CommandManager()->DoCheckpoint(L"Insert Node", undo, redo);
}

void MfcDocPresenterBase::OnConnectorColorChanged(CDocOb* line)
{
	if (view)
		view->OnChangedChildHighlight(line->Handle(), line->getErrorFlags());
}

void MfcDocPresenterBase::SetModuleRect(int handle, gmpi::drawing::Rect rect)
{
	if (auto m = dynamic_cast<CUG*>(container->Document()->uniqueIdDatabase.HandleToObjectWithNull(handle)))
	{
		gmpi::drawing::RectL rl{
			static_cast<int32_t>(rect.left),
			static_cast<int32_t>(rect.top),
			static_cast<int32_t>(rect.right),
			static_cast<int32_t>(rect.bottom)
		};
		m->setViewObRect(view ? view->getViewType() : CF_PANEL_VIEW, rl);
	}
}

gmpi::drawing::Rect MfcDocPresenterBase::GetModuleRect(int handle)
{
	if (auto m = dynamic_cast<CUG*>(container->Document()->uniqueIdDatabase.HandleToObjectWithNull(handle)))
	{
		const auto rl = m->getViewObRect(view ? view->getViewType() : CF_PANEL_VIEW);
		return {
			static_cast<float>(rl.left),
			static_cast<float>(rl.top),
			static_cast<float>(rl.right),
			static_cast<float>(rl.bottom)
		};
	}
	return {};
}

void MfcDocPresenter::ResizeModule(int handle, int dragNodeX, int dragNodeY, gmpi::drawing::Size offset)
{
	auto m = dynamic_cast<CUG*>(container->Document()->uniqueIdDatabase.HandleToObjectWithNull(handle));

	gmpi::drawing::RectL r;
	if (m == nullptr) // fix for background image.
	{
		r = container->GetPanelRect();
	}
	else
	{
		auto mr = m->getViewObRect(view->getViewType());
		r = { mr.left, mr.top, mr.right, mr.bottom };
	}

	if (dragNodeX == 0)
	{
		r.left += static_cast<int>(offset.width);
		r.left = (std::min)(r.left, r.right - 1);
	}
	else
	{
		if (dragNodeX == 2)
		{
			r.right += static_cast<int>(offset.width);
			r.right = (std::max)(r.right, r.left + 1);
		}
	}
	if (dragNodeY == 0)
	{
		r.top += static_cast<int>(offset.height);
		r.top = (std::min)(r.top, r.bottom - 1);
	}
	else
	{
		if (dragNodeY == 2)
		{
			r.bottom += static_cast<int>(offset.height);
			r.bottom = (std::max)(r.bottom, r.top + 1);
		}
	}

	if (m == nullptr) // fix for background image.
	{
		container->SetPanelRect(r);
		gmpi::drawing::Rect temp(static_cast<float>(r.left), static_cast<float>(r.top), static_cast<float>(r.right), static_cast<float>(r.bottom));
		view->OnChangedChildPosition(handle, temp);
	}
	else
	{
		gmpi::drawing::RectL rm{ r.left, r.top, r.right, r.bottom };
		m->setViewObRect(view->getViewType(), rm);
	}
}

int32_t MfcDocPresenter::onContextMenuLine(CLine2* line, int32_t nodeIndex, int32_t idx)
{
	++nodeIndex; // convert.

	switch (idx)
	{
	case 1: // remove node.
	{
		const auto handle = line->Handle();
		const auto nodeIdx = nodeIndex;
		const auto x = line->nodes[nodeIdx - 1].x;
		const auto y = line->nodes[nodeIdx - 1].y;

		line->DeleteNode(nodeIdx);
		DirtyView();

		auto redo = [handle, nodeIdx](CSynthEditDocBase* document)
		{
			if (auto l = dynamic_cast<CLine2*>(document->uniqueIdDatabase.HandleToObjectWithNull(handle)); l)
			{
				l->DeleteNode(nodeIdx);
				if (l->Container())
					l->Container()->VO_Notify(OM_ONCHANGE_CHILD_POSITION_STRUCT, (void*)l);
			}
		};

		auto undo = [handle, nodeIdx, x, y](CSynthEditDocBase* document)
		{
			if (auto l = dynamic_cast<CLine2*>(document->uniqueIdDatabase.HandleToObjectWithNull(handle)); l)
			{
				l->InsertNode(nodeIdx, x, y);
				if (l->Container())
					l->Container()->VO_Notify(OM_ONCHANGE_CHILD_POSITION_STRUCT, (void*)l);
			}
		};

		container->Document()->Application()->CommandManager()->DoCheckpoint(L"Delete Node", undo, redo);
	}
	break;
	case 2: // straighten line.
	{
		const auto handle = line->Handle();
		auto savedNodes = line->nodes;

		line->Straighten();
		DirtyView();

		auto redo = [handle](CSynthEditDocBase* document)
		{
			if (auto l = dynamic_cast<CLine2*>(document->uniqueIdDatabase.HandleToObjectWithNull(handle)); l)
			{
				l->Straighten();
				if (l->Container())
					l->Container()->VO_Notify(OM_ONCHANGE_CHILD_POSITION_STRUCT, (void*)l);
			}
		};

		auto undo = [handle, savedNodes](CSynthEditDocBase* document)
		{
			if (auto l = dynamic_cast<CLine2*>(document->uniqueIdDatabase.HandleToObjectWithNull(handle)); l)
			{
				l->nodes = savedNodes;
				l->SetModifiedFlag();
				if (l->Container())
					l->Container()->VO_Notify(OM_ONCHANGE_CHILD_POSITION_STRUCT, (void*)l);
			}
		};

		container->Document()->Application()->CommandManager()->DoCheckpoint(L"Straighten Line", undo, redo);
	}
	break;

	case 3: // remove line.
		{
			SuspendDSP x(container->Document()->Application());
			line->OnDelete();
		}
		break;

	case 4:
		line->setLineType(CLine2::CURVEY);
		line->SetModifiedFlag();
		DirtyView();
		break;

	case 5:
		line->setLineType(CLine2::STRAIGHT);
		line->SetModifiedFlag();
		DirtyView();
		break;

	case 7:
		line->setLineType(CLine2::DEFAULT_STYLE);
		line->SetModifiedFlag();
		DirtyView();
		break;

	case 6: // Add node
		DirtyView();
		break;
	}

	return gmpi::MP_OK;
}

void MfcDocPresenter::populateContextMenu(gmpi::api::IContextItemSink* sink, gmpi::drawing::Point p, int32_t moduleHandle, int32_t nodeIndex)
{
	ContextMenuHelper menu(sink);

	auto obj = container->Document()->uniqueIdDatabase.HandleToObjectWithNull(moduleHandle);

	// line nodes.
	if (auto line = dynamic_cast<CLine2*>(obj); line)
	{
		menu.currentCallback = [this, line, nodeIndex](int32_t idx) { return onContextMenuLine(line, nodeIndex, idx); };
		if (nodeIndex > -1)
		{
			menu.addItem("Delete Node", 1);
		}
		menu.addItem("Delete Line", 3);
		menu.addSeparator();

		menu.addItem("Clear Nodes", 2);

		const int lineType = line->getLineType();
		menu.addItem("Default",  7, lineType == CLine2::DEFAULT_STYLE ? gmpi_gui::MP_PLATFORM_MENU_TICKED : 0);
		menu.addItem("Curvey",   4, lineType == CLine2::CURVEY        ? gmpi_gui::MP_PLATFORM_MENU_TICKED : 0);
		menu.addItem("Straight", 5, lineType == CLine2::STRAIGHT      ? gmpi_gui::MP_PLATFORM_MENU_TICKED : 0);
	}
	else
	{

	menu.currentCallback = [this](int32_t idx) { return onContextMenu(idx); };

	const bool editEnabled = container->EditEnabled() || viewType != CF_PANEL_VIEW;

	int flags = editEnabled ? 0 : gmpi_gui::MP_PLATFORM_MENU_GRAYED;
	menu.addItem("Cut", [this](int32_t result) -> int32_t
		{
			if (container->OnEditCopy())
				container->OnEditDelete();
			return gmpi::MP_OK;
		},
		flags
	);
	menu.addItem("Copy", [this](int32_t result) -> int32_t
		{
			container->OnEditCopy();
			return gmpi::MP_OK;
		},
		flags
	);
	menu.addItem("Paste", [this, p](int32_t result) -> int32_t
		{
			container->OnEditPaste({ static_cast<int32_t>(p.x), static_cast<int32_t>(p.y) }, view->getViewType());
			return gmpi::MP_OK;
		},
		flags
	);
	menu.addItem("Delete", [this](int32_t result) -> int32_t
		{
			container->OnEditDelete();
			return gmpi::MP_OK;
		},
		flags
	);
	if (auto o = dynamic_cast<CUG*>(obj); o)
	{
		menu.addItem("Delete (keep wires)", [this, o](int32_t result) -> int32_t { this->DoModuleCommand(POPUP_MENU_DELETE_BYPASS, o); return gmpi::MP_OK; });
	}

	menu.addSeparator();

	// ARRANGE
	menu.beginSubMenu("&Arrange");
	menu.addItem("Move to Front", [this](int32_t result) -> int32_t
		{
			container->OnEditMoveFront();
			return gmpi::MP_OK;
		},
		flags
	);
	menu.addItem("Move to Back", [this](int32_t result) -> int32_t
		{
			container->OnEditMoveBack();
			return gmpi::MP_OK;
		},
		flags
	);
	menu.endSubMenu();

	if (viewType == CF_PANEL_VIEW)
	{
		// SKINS
		//		menuItemList.addItem("Structure...", getContainer(), POPUP_MENU_STRUCTURE_WPF, MS_PROPERTIES, ContextMenuBuilder::MI_NO_UNDO);
		const int skinMenuStartId = 100;
		menu.beginSubMenu("&Skin");

		menu.currentCallback = [this, skinMenuStartId](int32_t idx)
			{
				int skinIdx = idx - skinMenuStartId;
				if (skinIdx >= -3 && skinIdx < 300)
				{
					switch (skinIdx)
					{
						/*
					case -1: // "Reload Skin"
						container->getSkin()->Reload();
						GmpiResourceManager::Instance()->ClearResourceUris();

						//container->Document()->Notify AllViews(nullptr, OM_RELOAD_VIEWS, (void*)-1);
						Refresh View();
						return 0; // views and objects now deleted.
						break;
						*/

					case -2: // Browse Global Skins..
						SkinMgr::Instance()->EditSkin(container->getSkin());
						break;

					case -3: // Browse Project Skin..
						gmpi::browse_to(GmpiResourceManager::Instance()->projectSkinFolder());
						break;

					default:
					{
						container->Document()->SetModified();
						container->setSkin(SkinMgr::Instance()->getSkin(skinIdx));
						RefreshView();
						return gmpi::MP_OK; // important. 'this' now deleted.
					}
					break;
					}
				}
				return gmpi::MP_OK;
			};

		for (int j = 0; j < SkinMgr::Instance()->getSkinCount(); j++)
		{
			SkinInfo* si = SkinMgr::Instance()->getSkin(j);

			if (si->Name != L"global" && si->Name != L"_fallback")
			{
				flags = editEnabled ? 0 : GmpiGui::Menu_Grayed;

				if (si == container->getSkin())
					flags = GmpiGui::Menu_Ticked;

				menu.addItem(WStringToUtf8(si->Name).c_str(), skinMenuStartId + j, flags);
			}
		}

		flags = editEnabled ? 0 : GmpiGui::Menu_Grayed;
		menu.addSeparator();
		// not supported with GMPI-UI menu.addItem("Reload Skin", skinMenuStartId - 1, flags);
		menu.addItem("Open Global Skins...", skinMenuStartId - 2, 0);
		if (const auto psf = GmpiResourceManager::Instance()->projectSkinFolder(); std::filesystem::exists(psf))
			menu.addItem("Open Project Skin...", skinMenuStartId - 3, 0);
		menu.endSubMenu();

		const int lockedFlags = container->getLocked() ? GmpiGui::Menu_Ticked : 0;
		menu.addItem("Locked", [this](int32_t result) -> int32_t
			{
				container->OnMenuCommand(viewType, POPUP_MENU_TOGGLE_LOCKED);
				return gmpi::MP_OK;
			},
			lockedFlags
		);

#if defined( _DEBUG )
		// Whole-project setting, and only the top-level panel becomes the rack.
		if (!container->Container())
		{
			auto document = container->Document();
			menu.addItem("Rack Mode", [this, document](int32_t result) -> int32_t
				{
					document->rackMode = !document->rackMode;
					document->SetModified();
					DirtyView();
					return gmpi::MP_OK;
				},
				document->rackMode ? GmpiGui::Menu_Ticked : 0
			);
		}
#endif

		// Navigate
		menu.addSeparator();
		menu.addItem("Goto Structure...", [this](int32_t result) -> int32_t
			{
				container->OnMenuCommand(viewType, POPUP_MENU_STRUCTURE_WPF);
				return gmpi::MP_OK;
			}
		);
	}
	else
	{
		// Module-specific menu.
		if (moduleHandle >= 0)
		{
			auto o = dynamic_cast<CUG*>(obj);
			if (o)
			{
				auto containerModule = dynamic_cast<CContainer*>(o);
				if (containerModule)
				{
					menu.addSeparator();
					menu.addItem("Pa&nel Edit...", [this, o](int32_t result) -> int32_t { this->DoModuleCommand(POPUP_MENU_CONTROLS, o); return gmpi::MP_OK; });
					menu.addItem("&Structure...", [this, o](int32_t result) -> int32_t { this->DoModuleCommand(POPUP_MENU_STRUCTURE_WPF, o); return gmpi::MP_OK; });
#ifdef _DEBUG // for now
					menu.addItem("Clone", [=](int32_t) -> int32_t { containerModule->OnEditClone(); return gmpi::MP_OK; });
#endif
					menu.addSeparator();
				}

				menu.beginSubMenu("More");
				{
					if (viewType == CF_STRUCTURE_VIEW)
					{
						menu.addItem("D&ebug", [this, o](int32_t result) -> int32_t { this->DoModuleCommand(POPUP_MENU_DEBUG_TOGGLE, o); return gmpi::MP_OK; });
						menu.addItem("Mute", [this, o](int32_t result) -> int32_t
							{
								const bool targetMute = !o->GetMute();
								DO_LIST selection;
								container->GetSelection(selection);
								bool clickedHandled = false;
								for (auto vo : selection)
								{
									if (auto ug = dynamic_cast<CUG*>(vo); ug)
									{
										ug->SetMute(targetMute);
										if (ug == o) clickedHandled = true;
									}
								}
								if (!clickedHandled)
									o->SetMute(targetMute);
								return gmpi::MP_OK;
							},
							o->GetMute() ? gmpi_gui::MP_PLATFORM_MENU_TICKED : 0);
						if(containerModule)
						{
							menu.addItem(
								"Panel Locked",
								[this, o](int32_t result) -> int32_t
									{
										this->DoModuleCommand(POPUP_MENU_TOGGLE_LOCKED, o);
										return gmpi::MP_OK;
									},
								containerModule->getLocked() ? gmpi_gui::MP_PLATFORM_MENU_TICKED : 0
							);

#ifdef _DEBUG // for now
							menu.addItem(
								"Rack-Module",
								[this, o](int32_t result) -> int32_t
									{
										this->DoModuleCommand(POPUP_MENU_TOGGLE_RACKMODULE, o);
										return gmpi::MP_OK;
									},
								containerModule->isRackModule() ? gmpi_gui::MP_PLATFORM_MENU_TICKED : 0);
#endif
						}

						menu.addItem("Containerize Selection", [this, o](int32_t result) -> int32_t { this->DoModuleCommand(ID_EDIT_CONTAIN, container); return gmpi::MP_OK; });

						if (containerModule)
							menu.addItem("Un-Containerize", [this, o](int32_t result) -> int32_t { this->DoModuleCommand(ID_EDIT_UNCONTAIN, o); return gmpi::MP_OK; });

						menu.addItem("Parameters...", [this, o](int32_t result) -> int32_t { this->DoModuleCommand(POPUP_MENU_PARAMETER_DETAIL, o); return gmpi::MP_OK; });
						{
							// Gray out "Selection to Prefab" when this app has gated features and the
							// current license doesn't cover them (it's effectively a save action).
							// Runtime check (not #ifdef) because this file lives in EditorLib, compiled
							// once for every app — GetLicenseState() resolves per-app at link time
							// (BACKLOG C11: narrowed from a direct SynthEditApp dependency to this
							// generic interface so the public repo doesn't need to know the private
							// app's type or its licensing method names).
							auto* license = GetLicenseState();
							int32_t prefabFlags = 0;
							if (license && license->hasGatedFeatures() && !license->isLicensed())
								prefabFlags = gmpi_gui::MP_PLATFORM_MENU_GRAYED;
							menu.addItem("Selection to Prefab", [this, o](int32_t result) -> int32_t { this->DoModuleCommand(POPUP_MENU_TO_PREFAB, o); return gmpi::MP_OK; }, prefabFlags);
						}
						menu.addItem("Replace...", [this, o](int32_t result) -> int32_t { this->DoModuleCommand(POPUP_MENU_REPLACE, o); return gmpi::MP_OK; });
						if (viewType == CF_STRUCTURE_VIEW)
						{
							menu.addItem("Connect...", [this, o](int32_t result) -> int32_t { this->DoModuleCommand(POPUP_MENU_CONNECT, o); return gmpi::MP_OK; });
						}

						flags = o->Container() ? 0 : gmpi_gui::MP_PLATFORM_MENU_GRAYED;
						menu.addItem("Goto Parent Container", [this, o](int32_t result) -> int32_t { this->DoModuleCommand(POPUP_MENU_LOCATE_CONTAINER, o); return gmpi::MP_OK; }, flags);
						menu.addItem("Build Code Skeleton...", [this, o](int32_t result) -> int32_t { this->DoModuleCommand(POPUP_MENU_DEBUG_CODE, o); return gmpi::MP_OK; });
//#ifdef _DEBUG
						menu.addItem("Don't Export", [this, o](int32_t result) -> int32_t { this->DoModuleCommand(POPUP_MENU_EXCLUDE_FROM_VST, o); return gmpi::MP_OK; },
							o->excludeFromVst ? gmpi_gui::MP_PLATFORM_MENU_TICKED : 0
						);
						menu.addItem("Don't Export JUCE", [this, o](int32_t result) -> int32_t { this->DoModuleCommand(POPUP_MENU_EXCLUDE_FROM_JUCE, o); return gmpi::MP_OK; },
							o->excludeFromJUCE ? gmpi_gui::MP_PLATFORM_MENU_TICKED : 0
						);
//#endif
					}

					menu.addItem("Help...", [this, o](int32_t result) -> int32_t { this->DoModuleCommand(POPUP_MENU_UG_HELP, o); return gmpi::MP_OK; });
					menu.addItem("About...", [this, o](int32_t result) -> int32_t { this->DoModuleCommand(POPUP_MENU_ABOUT, o); return gmpi::MP_OK; });
				}
				menu.endSubMenu();
			}
		}
		else
		{
			menu.addSeparator();
			// Didn't click any module, only background.
			menu.addItem("Panel Edit...", [this](int32_t result) -> int32_t { this->DoModuleCommand(POPUP_MENU_CONTROLS, container); return gmpi::MP_OK; });
			flags = container->Container() ? 0 : gmpi_gui::MP_PLATFORM_MENU_GRAYED;
			menu.addItem("Goto Parent...", [this](int32_t result) -> int32_t { this->DoModuleCommand(POPUP_MENU_LOCATE_CONTAINER, container); return gmpi::MP_OK; }, flags);

#if defined( _DEBUG )
			// mb->InsertDirect(container_, POPUP_MENU_SCREENSHOT, L"Screenshot", MS_PROPERTIES, ContextMenuBuilder::MI_NO_UNDO);
			menu.addItem("Screenshot", [this](int32_t result) -> int32_t { this->DoModuleCommand(POPUP_MENU_SCREENSHOT, container); return gmpi::MP_OK; });
#endif
		}
	}
	}
}

void MfcDocPresenter::DoModuleCommand(int32_t commandId, CUG* module)
{
	module->OnMenuCommand(CF_STRUCTURE_VIEW, commandId, { 0, 0 });
}

int32_t MfcDocPresenter::onContextMenu(int32_t idx)
{
//	return GmpiSdk::ContextMenuHelper::onContextMenu(contextMenuCallbacks, idx);
#if 0
	const int skinMenuStartId = 100;

	switch (idx)
	{
	case 0:
		if (container->OnEditCopy())
		{
			container->OnEditDelete();
		}
		break;

	case 1:
		container->OnEditCopy();
		break;

	case 2:
		{
		container->OnEditPaste({ static_cast<int>(point.x), static_cast<int>(point.y) }, view->getViewType());
		}
		break;

	case 3:
		container->OnEditDelete();
		break;

	case 4:
		container->OnEditMoveFront();
		break;

	case 5:
		container->OnEditMoveBack();
		break;

	case 6:
		container->OnMenuCommand(viewType, POPUP_MENU_TOGGLE_LOCKED);
		break;

	case 7:
		container->OnMenuCommand(viewType, POPUP_MENU_STRUCTURE_WPF);
		break;

	default:
		{
			int skinIdx = idx - skinMenuStartId;
			if (skinIdx >= -2 && skinIdx < 300)
			{
				switch (skinIdx)
				{
					/*
				case -1: // "Reload Skin"
					container->getSkin()->Reload();
					GmpiResourceManager::Instance()->ClearResourceUris();

					//container->Document()->Notify AllViews(nullptr, OM_RELOAD_VIEWS, (void*)-1);
					Refresh View();
					return 0; // views and objects now deleted.
					break;
					*/

				case -2: // "Edit Skin.."
					SkinMgr::Instance()->EditSkin(container->getSkin());
					break;

				default:
				{
					container->Document()->SetModified();
					container->setSkin(SkinMgr::Instance()->getSkin(skinIdx));
					RefreshView();
					return 0; // important. 'this' now deleted.
				}
				break;
				}
			}
		}
		break;
	}
#endif

	return 0;
}
/*
void MfcDocSubPresenter::ResizeSubView()
{
	auto module = dynamic_cast<SE2::ModuleView*>(view->getGuiHost());
	if(module)
		view->OnChild Resize(module); //  module's view is NOT my view, it's module->parent. !!! this is wrong view!!!

	// PROBLEMS: OnChildResize() only meant to handle changes to module's size, not handle sub-views (and it's resize adorner) changing size and/or position.
	// it relies on a hack in SubView::measure() to adjust module position top-left, however when the change is to bottom-right we get "smear" from
	// resize-adorner (which is completely unaware of size change).
	// Really need dedicated ->SubViewRecalcBounds() type method. or something better than measure/arrange.
	/ * e.g. (hmm what about resize-adorner?)
	module->GetClipRect();
	auto invalidRect = module->GetClipRect();

	module->SubViewRecalcBounds();

	invalidRect.Union(module->GetClipRect());
	view->invalidateRect(&invalidRect);
	* /
}
*/
SE2::RackLayout MfcDocPresenter::getRackLayout()
{
	SE2::RackLayout rack;

	// Rack mode is a whole-project setting, but only the top-level panel view is
	// the rack itself. Sub-panels and every structure view lay out as usual.
	if (viewType != CF_PANEL_VIEW || !container || container->Container() || !container->Document()->rackMode)
		return rack;

	rack.enabled = true;

	// Anchor row 0 / column 0 to the top-left of the plugin's panel, so a snapped
	// layout fills the exported window rather than an arbitrary point on the canvas.
	const auto panelRect = container->GetPanelRect();
	rack.origin = { static_cast<float>(panelRect.left), static_cast<float>(panelRect.top) };

	return rack;
}

int MfcDocPresenterBase::GetSnapSize()
{
	auto snapsize = container->Document()->Application()->SnapPixels(); // returns only panel snap setting.

	if (snapsize != 1 && view->getViewType() == CF_STRUCTURE_VIEW)
		return 12;

	return snapsize;
}

void MfcDocPresenterBase::OnFrameGotFocus()
{
	auto app = container->Document()->Application();
	app->VO_Notify(OM_WPF_UPDATE_PRESET_BROWSER, container->get_patch_manager() );

	app->VO_Notify(OM_WPF_UPDATE_EDIT_MENU, (void*) (!container->getLocked() || view->getViewType() != CF_PANEL_VIEW));
}
