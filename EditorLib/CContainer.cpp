#include <assert.h>
#include <algorithm>
#include <sstream>
#include <map>
#include <filesystem>

#include "StandardCommandIds.h"

#include "Application.h"
#include "CContainer.h"

#include "ug_container.h"
#include "module_info.h"
#include "InterfaceObject.h"
#include "CLine2.h"
#include "SynthEditDocBase.h"
#include "HostControls.h"
#include "SkinMgr.h"
#include "se_file_format_version.h"
#include "it_doc_ob.h"
#include "Control.h"
#include "resource.h"
#include "UgDatabase.h"

#include "MpString.h"
#include "../se_sdk3_hosting/GmpiResourceManager.h"
#include "./SuspendDSP.h"

#include "it_plug_destinations.h"
#include "it_doc_ob_recursive.h"
#include "PatchManager.h"
#include "PlugIO4.h"
#include "tinyxml/tinyxml.h"
#include "SerializationHelper_XML.h"
#include "PatchParameter.h"
#include "../tinyXml2/tinyxml2.h"
#include "Shared/VstPreset.h"
#include "Shared/AuPreset.h"
#include "Ctl_Slider.h"
#include "Notify_msg.h"
#include "ModuleFactory_Editor.h"
#include "ViewBase.h"

#ifdef _WIN32
#include "legacyExternalApp.h"
#endif

using namespace std;

#define PN_IGNORE_PC 3

tinyxml2::XMLDocument CContainer::PasteBuffer;

gmpi::drawing::PointL snap_to_grid_struct(gmpi::drawing::PointL p)
{
	constexpr int snapSize = 12;

	p.x = ((p.x + snapSize / 2) / snapSize) * snapSize;
	p.y = ((p.y + snapSize / 2) / snapSize) * snapSize;
	return p;
}
gmpi::drawing::PointL snap_to_grid_panel(gmpi::drawing::PointL p)
{
	constexpr int snapSize = 8;

	p.x = ((p.x + snapSize / 2) / snapSize) * snapSize;
	p.y = ((p.y + snapSize / 2) / snapSize) * snapSize;
	return p;
}

CDocOb* CContainer::Make( Module_Info* p_type )
{
	return new CContainer(p_type);
}

CContainer::CContainer( Module_Info* p_type ) : CUG_with_patches( p_type )
	,StructLocation(0,0,500,400)
	,PanelLocation(30,30,530,330)
	,PanelWndPosition(0,0,0,0)
	,PanelScroll(0,0)
	,StructScroll(0,0)
	,PanelLocationCenter(3984.0f, 3984.0f)
	,StructLocationCenter(3984.0f, 3984.0f)
	,PanelLocationZoom(1.0f)
	,StructLocationZoom(1.0f)
	,ViewOpenFlags(0)
	,m_skin(0)
	,m_document(0)
	,m_locked(false)
	,m_show_controls_on_panel_legacy(false)
	,m_patch_manager(0)
	,PanelWndOffset(-99999,-99999)
{
	if(p_type) // -1 indicates creation by serialisation
		m_skin = SkinMgr::Instance()->getSkin(L"default3");

	// panel initial rect dimensions (for use in plugins).
	constexpr auto center = SE2::viewDimensions / 2 - 12;
	constexpr auto size = 12 * 20; // 20 grid squares
	m_panel_rect = { center - size,center - size,center + size, center + size };
}

CContainer::~CContainer()
{
	// close any views on this( yes, base class does it, but's too late then)
	NotifySafe(OM_DELETE);
	DeleteAll();
	if (m_patch_manager)
	{
		delete m_patch_manager;
	}
}

// adding patch select may require adding patch controls to top of panel view
void CContainer::OnHasPatchSelectionChanged()
{
	bool has_ps = hasPatchSelector();
	bool update_view = false; // try not to if possible
	CPatchManager* temp = 0;

	if( Container() == 0 || has_ps )
	{
		if( m_patch_manager == 0 )
		{
			m_patch_manager = new CPatchManager(this);

			if( Container() != 0 ) // transfer patches from above
			{
				Container()->get_patch_manager()->TransferPatchData();
				Container()->NotifyParameterChange( OM_REFRESH_PARAMETERS, 0 ); // get upper containers to erase these parameters.
			}

			NotifyParameterChange( OM_REFRESH_PARAMETERS, 0 ); // update immediate container's parameters.
			update_view = true;
		}
	}
	else
	{
		if( m_patch_manager != 0 ) // patch manager no longer needed (save it's patch data)
		{
			temp = m_patch_manager;
			m_patch_manager = 0; // set zero so transfer goes to parent patch manager
			temp->TransferPatchData();
			update_view = true;

			temp->DeregisterAllGuiPatchAutomators();

			// Some host-parameters may remain, notify ContainerViewModel to delete any reference to them.
			// handled below..temp->NotifyLostVisibility(this);
			// This container and any parent gain visibility of parameters. Quickest to completly refresh all.
			NotifyParameterChange( OM_REFRESH_PARAMETERS, 0 );
		}
	}

	// DURING UPGRADE (when loading file), DON'T access views and don't call Sdk2UpdatePatchConnections() (will crash)

	if( !Document()->isGraphInitialised() )
		update_view = false;

	// notify views
	if( update_view )
	{
		// notify ctl_wnd to redraw container (imbedded patch display will be added/removed)
		if( Container() )
		{
			Container()->VO_Notify( OM_LAYOUT_CHANGE2, this );
		}
		// reload all views, including sub-views (which will have invalid hanging IGuiHost pointers), adding patch controls if nesc.
		// avoid updating structure view, causes screwed up GFX for patch-automator.
		//Document()->NotifyAllViews( nullptr, OM_PATCH_AUTOMATOR_CHANGE, this ); // Does not notify presentors.
	}

	// remove old patch manager after view recreated (else crash)
	delete temp;

	// this will crash if no patch_manager available (when loading old files) so, moved last to allow creation of PM first
	// When user deletes patch select (and patches not synced to parent), need to set patch back to zero.
	// otherwise controls will detect no patch support and only serialise patch number zero
	// (even though user is affecting patch 5 (whatever))
	if( !has_ps && !hasPatches() )
	{
		SetProgram( 0 );
	}
}

CLine2* CContainer::AddLine(IPlug* from, IPlug* to, int32_t handle)
{
	assert(from);
	assert(to);
	assert(from->UG()->Container() == this);
	assert(to->UG()->Container() == this);

	if(handle < 0 )
	{
		// auto-generate handle
		handle = Document()->uniqueIdDatabase.GenerateUniqueHandleValue();
	}

	auto l = new CLine2(from, to);
	l->setHandle(handle);

	addToContainer(l);

	return l;
}

CLine2* CContainer::AddLineQuiet(IPlug* from, IPlug* to)
{
	assert(from);
	assert(to);
	assert(from->UG()->Container() == this);
	assert(to->UG()->Container() == this);

	auto l = new CLine2();
	l->FromPlug = from;
	l->ToPlug = to;

	l->setType(ModuleFactory()->GetById(L"Line"));

	addToContainer(l);

	from->AddConnectorQuiet(l);
	to->AddConnectorQuiet(l);

	return l;
}

// Arrange UG list so that this ug is drawn on top
// (drawn last)
void CContainer::MoveChildToFront(CDocOb* ug)
{
	auto it = find(BaseList.begin(),BaseList.end(),ug);
	assert( it != BaseList.end() );
	BaseList.erase(it);
	AddSorted( ug );
	VO_Notify( OM_CHILD_TO_FRONT, ug);
}

void CContainer::MoveChildToBack(CDocOb* doc_ob)
{
	auto it2 = find( BaseList.begin(), BaseList.end(), doc_ob );
	assert( it2 != BaseList.end() );
	BaseList.erase(it2);

	// insert CUGs after other CUGs, but in front of lines.
	if( dynamic_cast<CLine2*>( doc_ob ) == 0 )
	{
		for( auto it = BaseList.begin() ; it != BaseList.end() ; ++it )
		{
			if( dynamic_cast<CLine2*>((*it) ) != 0 )
			{
				BaseList.insert( it, doc_ob );
				VO_Notify( OM_CHILD_TO_BACK, doc_ob);
				return;
			}
		}
	}

	BaseList.push_back( doc_ob );
	VO_Notify( OM_CHILD_TO_BACK, doc_ob);
}

void CContainer::setLocked(bool p_locked)
{
	m_locked = p_locked;
	Document()->SetModified();
};

void CContainer::toggleLocked()
{
	setLocked(!getLocked());
	VO_Notify( OM_LOCKED_CHANGE, &m_locked );
//	((CSynthEditDoc*)Document())->UpdateAllViews( 0, OM_LOCKED_CHANGE,this );
}

CDocOb* CContainer::Remove(CDocOb* o)
{
	NotifyFast(OM_REMOVE_CHILD, (void*) o);

	// if module has any patch parameters, put them in holding bin
	//	o->get_patch_manager()->PutAside(o);
	// no it may not be being deleted, just moved	o->VO_Notify( OM _DELETE );
	o->NotifySafe(OM_DELETE);

	NotifyFast(OM_REFRESH_PRESENTERS);

	auto it = find( BaseList.begin(), BaseList.end(), o );

	if( it != BaseList.end() )
	{
		BaseList.erase(it);
	}

	assert( find( BaseList.begin(), BaseList.end(), o ) == BaseList.end() );

	// special case, removing patch select may require removing patch controls to top of panel view
	o->OnRemoved( this );

	return o;
}

// ugs are included in copy/paste if tagged, or if container is tagged (but not 'above' current container).
// serialise_all_mode forces every descendant of m_copy_container to be tagged (whole-document prefab save).
bool CContainer::IsCopyTagged()
{
	/*
		// if it's not inside copy container, ignore it
		if( ! IsChildOf(m_copy_container) ) // NOTHING 'above' this can be copied
			return false;
	*/
	// if my direct parent is copy container, and i'm selected (or we're saving everything), then copy
	if( Container() == m_copy_container && (GetSelected() || serialise_all_mode) )
		return true;

	// else copy me if parent is being copied
	return Container() && Container()->IsCopyTagged();
}
// Normal 'Copy' command

bool CContainer::OnEditCopy()
{
	// Ensure user isn't 'copy'ing an IO_UG (not allowed)
	for (auto& vo : BaseList)
	{
		if (vo->GetSelected() && vo->GetFlags() & CF_IO_MOD)
		{
			Application()->SeMessageBox(L"Can't Copy (or Cut) an 'IO Module'. Please un-select it and try again", L"", MB_OK | MB_ICONEXCLAMATION);
			return false;
		}
	}
	
	SerialiseSelectedModules(PasteBuffer, false);

	return true;
}

void CContainer::SerialiseSelectedModules(tinyxml2::XMLDocument& doc, bool andModuleInfo)
{
	// Archive list to temp file
	doc.Clear();

	// Create empty XML Document.
	doc.LinkEndChild(doc.NewDeclaration());

	auto document_element = doc.NewElement("Document");
	document_element->SetAttribute("file_format", (int)XML_FILE_FORMAT_VERSION_NUM);
	doc.LinkEndChild(document_element);

	// objects are copied if selected, or parent container is (recursive)
	// need to know outermost container to stop at
	m_copy_container = this;
	serialise_copy_mode = true; // global indicator that we are in 'copy mode'
	// save any parameters stored at a higher level
	//	my_patch_manager()->OnEditCopyPaste(ar, this);
	// if this container don't have own patch manager, we need to borrow the one from parent
	// to ensure any patch data gets serialised
	CPatchManager* t_original_patch_mgr = m_patch_manager;
	m_patch_manager = get_patch_manager();

	ModuleFactory()->ClearSerialiseFlags();
	CSynthEditDocBase::serializingMode = SERT_UNSET;
	
	m_patch_manager->Export(document_element, SAT_SYNTHEDIT_DOCUMENT);

	{
		const auto targetType = SAT_SYNTHEDIT_DOCUMENT;

		// First modules.
		auto modules_json = document_element->GetDocument()->NewElement("modules");
		document_element->LinkEndChild(modules_json);
		for (auto m : BaseList)
		{
			if (m->IsCopyTagged() && dynamic_cast<CLine2*>(m) == nullptr)
			{
				auto module_json = doc.NewElement("module");
				modules_json->LinkEndChild(module_json);

				Archive2 ar{ true , module_json, targetType };
				m->Serialize2(ar);
				m->getType()->SetSerialiseFlag();
			}
		}

		// Second lines.
		modules_json = doc.NewElement("lines");
		document_element->LinkEndChild(modules_json);
		for (auto m : BaseList)
		{
			auto line = dynamic_cast<CLine2*>(m);
			if (line == nullptr || !m->IsCopyTagged())
				continue;

			if (!reportIfUnserializable(line))
				continue;

			auto module_json = doc.NewElement("line");
			line->Export(module_json, targetType);
			modules_json->LinkEndChild(module_json);
		}
	}

	if(andModuleInfo)
		ExportModuleInfo(document_element, SAT_SYNTHEDIT_DOCUMENT);
	
	m_patch_manager = t_original_patch_mgr;
	serialise_copy_mode = false;
	CSynthEditDocBase::serializingMode = SERT_UNSET;

//	tinyxml2::XMLPrinter printer;
//	PasteBuffer.Accept(&printer);
//	_RPTN(0, "%s\n", printer.CStr());
}

void CContainer::OnEditPaste(gmpi::drawing::PointL point, int p_view_type, tinyxml2::XMLDocument* pasteDoc)
{
	if (!pasteDoc)
		pasteDoc = &PasteBuffer;

	auto document_xml = pasteDoc->FirstChildElement("Document");
	if (!document_xml)
		return;

	SuspendDSP x(Document()->Application());

	int fileFormatVersion = XML_FILE_FORMAT_VERSION_NUM;
	document_xml->QueryIntAttribute("file_format", &fileFormatVersion);

	gmpi::drawing::PointL paste_pos_struct{-1, -1};
	gmpi::drawing::PointL paste_pos_panel{-1, -1};
	setAllSelected(false);

	// if position passed in, overide default
	if (p_view_type == CF_STRUCTURE_VIEW)
		paste_pos_struct = point;
	else
		paste_pos_panel = point;

	serialise_copy_mode = true; // global indicator that we are in 'copy mode'
	Document()->serializingMode = SERT_SE1_DOC;

	ImportModuleInfo(document_xml, SAT_SYNTHEDIT_DOCUMENT, fileFormatVersion);
		
	// create a temp container
	auto temp_container = dynamic_cast<CContainer*>(CreateDocObject(L"Container")); // adds default plugs.
	temp_container->m_document = Document();

	// no gets overwritten in IMport() : Document()->uniqueIdDatabase.setHandleAutoGenerated(temp_container); // prevent assertion during destruction
//	temp_container->Initialise(true);
//	temp_container->m_patch_manager->SetContainer(temp_container);

	auto topLevel = document_xml;

	// when loading prebab from .synthedit, need to ignore mastercontainer
	if (auto masterContainer = document_xml->FirstChildElement("master_container"))
		topLevel = masterContainer;

	{
		std::map<int32_t, CUG*> uniqueIds;
		temp_container->Import(uniqueIds, topLevel, SAT_SYNTHEDIT_DOCUMENT);
	}

	temp_container->AdjustModuleTypePointer();

	constexpr bool loading_prefab = false;
	temp_container->RegisterHandles(loading_prefab);

	temp_container->UpgradeAll(CDocOb::m_loading_version);

	temp_container->Initialise(true);

	serialise_copy_mode = false;

	CDocOb::m_loading_version = FILE_FORMAT_VERSION_NUM; // back to current.

	if (temp_container->BaseList.empty()) // pasted nothing
		return;

	// work out approx center of selected objects
	int count_struct = 0;
	int count_panel = 0;
	gmpi::drawing::PointL struct_center{};
	gmpi::drawing::PointL panel_center{};
	gmpi::drawing::PointL struct_topleft{INT_MAX, INT_MAX};
	gmpi::drawing::PointL panel_topleft{INT_MAX, INT_MAX};

	for( auto& db : temp_container->BaseList)
	{
		const int object_flags = db->getType()->GetFlags();
		gmpi::drawing::RectL temp = db->getViewObRect(CF_STRUCTURE_VIEW);

		if( (object_flags & CF_STRUCTURE_VIEW) != 0 && !gmpi::drawing::empty(temp) ) // ignore lines (nullptr rect during load prefab)
		{
			struct_center.x += temp.left;
			struct_center.y += temp.top;
			struct_topleft.x = min( struct_topleft.x, temp.left );
			struct_topleft.y = min( struct_topleft.y, temp.top );
			count_struct++;
		}

		temp = db->getViewObRect(CF_PANEL_VIEW);

		//		if( (object_flags & CF_PANEL_VIEW) != 0 && ! temp.IsRectNull() ) // ignore lines
		// can't use flags, prefabs like LED are containers with "show-on-panel"
// no - CUG2 object have a panel rect, even if they don't show on panel		if( temp.left > 0 || temp.top > 0 ) // Structure-only objects have rect all zeros, panl object not yet sized have empty-rect with a valid offset.
		
		if ((object_flags & CF_PANEL_VIEW) != 0)
		{
			assert( temp.top >= -50 && temp.bottom < 10000 ); // check for crap rects

			panel_center.x += temp.left;
			panel_center.y += temp.top;
			panel_topleft.x = min( panel_topleft.x, temp.left );
			panel_topleft.y = min( panel_topleft.y, temp.top );
			count_panel++;
		}

		AddSorted(db); // put in my list

		// bit hacky, need to detect addition of patch selector via paste			db->OnAdded( this );
	}

	// all objects now copied into my baselist (also still in temp_container list)
	// set all the parent pointers
	SetParentPointersRecursive(Container());
	// now copy any patch parameters from temp container
	assert( temp_container->has_own_patch_mgr() );
	assert( temp_container->Container() == 0 ); // else temp_container.my_patch_manager() returns nULL
	// temp_container->m_document = Document();
	temp_container->RegisterHandles( true ); // regenerate and register unique IDs.

	// prevent divide by zero
	if( count_panel < 1 )
	{
		count_panel = 1;
		panel_topleft.x = panel_topleft.y = 100;
	}

	if( count_struct < 1 )
	{
		count_struct = 1;
		struct_center.x = struct_center.y = 100;
	}

	panel_center.x /= count_panel;
	panel_center.y /= count_panel;
	if (paste_pos_panel != gmpi::drawing::PointL{-1, -1})
	{
		panel_center = {paste_pos_panel.x - panel_center.x, paste_pos_panel.y - panel_center.y};
	}
	else
	{
		paste_pos_panel = {panel_center.x + 36, panel_center.y + 36}; // Down and right a bit.

		// Constrain to visible area (document coords).
		{
			auto r = getVisibleRect(CF_PANEL_VIEW);
			const int shrink = (std::min)(100, (std::min)(gmpi::drawing::getWidth(r) / 2, gmpi::drawing::getHeight(r) / 2));
			r.left += shrink;
			r.top += shrink;
			r.right -= shrink;
			r.bottom -= shrink;

			paste_pos_panel.x = (std::max)(paste_pos_panel.x, r.left);
			paste_pos_panel.x = (std::min)(paste_pos_panel.x, r.right);
			paste_pos_panel.y = (std::max)(paste_pos_panel.y, r.top);
			paste_pos_panel.y = (std::min)(paste_pos_panel.y, r.bottom);
		}

		paste_pos_panel.x += (rand() & 0x07) * 8; // A little random.
		paste_pos_panel.y += (rand() & 0x07) * 8;

		panel_center = {paste_pos_panel.x - panel_center.x, paste_pos_panel.y - panel_center.y}; // convert to offset.
	}

	struct_center.x /= count_struct;
	struct_center.y /= count_struct;
	if (paste_pos_struct != gmpi::drawing::PointL{-1, -1})
	{
		struct_center = {paste_pos_struct.x - struct_center.x, paste_pos_struct.y - struct_center.y};
	}
	else
	{
		paste_pos_struct = {struct_center.x + 36, struct_center.y + 36}; // Down and right a bit.

		// Constrain to visible area (document coords).
		{
			auto r = getVisibleRect(CF_STRUCTURE_VIEW);
			const int shrink = (std::min)(100, (std::min)(gmpi::drawing::getWidth(r) / 2, gmpi::drawing::getHeight(r) / 2));
			r.left += shrink;
			r.top += shrink;
			r.right -= shrink;
			r.bottom -= shrink;

			paste_pos_struct.x = (std::max)(paste_pos_struct.x, r.left);
			paste_pos_struct.x = (std::min)(paste_pos_struct.x, r.right);
			paste_pos_struct.y = (std::max)(paste_pos_struct.y, r.top);
			paste_pos_struct.y = (std::min)(paste_pos_struct.y, r.bottom);
		}

		paste_pos_struct.x += (rand() & 0x07) * 8; // A little random.
		paste_pos_struct.y += (rand() & 0x07) * 8;

		struct_center = {paste_pos_struct.x - struct_center.x, paste_pos_struct.y - struct_center.y}; // convert to offset.
	}

	// snap to grid.
	struct_center = snap_to_grid_struct(struct_center);
	panel_center = snap_to_grid_panel(panel_center);

	// can't do because new CUGs are not registered with UniquesnOWFLAKE OWNER
	// hence looking up their handle returns pointer to other object??
	temp_container->my_patch_manager()->TransferPatchData(get_patch_manager());

	// add objects to current list
	// center on paste point
	for( auto ug : temp_container->BaseList)
	{
		// Offset
		ug->offsetViewObRect( CF_STRUCTURE_VIEW, struct_center.x, struct_center.y );
		ug->offsetViewObRect( CF_PANEL_VIEW, panel_center.x, panel_center.y );
		ug->SetSelected(true);

		// on Paste, there is posibility of unused auto-duplicate plugs
		// because the module was cut/copied, but not always it's lines
		// Remove these unused plugs, (except for last one)
		if( auto cug = dynamic_cast<CUG*>(ug); cug )
		{
			// old, removed all but last spare, crashed on DH modules with > 1 autoduplicate
			bool found_spare = false;

			for(auto it2 =  cug->Plugs.rbegin() ; it2 !=  cug->Plugs.rend() ; )
			{
				IPlug* p = *it2;

				if( p->isUnusedSpare() )
				{
					if( found_spare )
					{
						p->Disconnect_pt1(0); // remove it
						p->Disconnect_pt2(0);
						// removal invalidates iterator, restart.
						it2 =  cug->Plugs.rbegin();
						found_spare = false;
					}
					else
					{
						found_spare = true;
						++it2;
					}
				}
				else
				{
					++it2;
				}
			}
		}
	}

	Document()->UpGradeIncompatibleModules();
	DeleteTemporaryModuleDescriptions();

    // update properties browser with the new module
    if(!temp_container->BaseList.empty())
        Document()->Application()->NotifyFast(OM_SHOW_PROPERTIES, BaseList.front());

	temp_container->BaseList.clear();

	const auto handle = temp_container->Handle();
	delete temp_container;
	Document()->uniqueIdDatabase.Unregister(handle);

	NotifyFast(OM_REFRESH_PRESENTERS);
}

void CContainer::OnEditClone()
{
	// clones controns will need thier own unique parameters.
	// therfore probly need to paste copy of entire contents, then link each child to it's 'clone'
	// then any editting command on a child needs to be repeated on it's clones.
}

void CContainer::debugpoly()
{
	for( auto it = BaseList.begin() ; it != BaseList.end() ; ++it )
	{
		CUG* cug = dynamic_cast<CUG*>(*it);

		if( cug != 0 )
			cug->debugpoly();
	}

	CUG::debugpoly();
}

void CContainer::ExportMain(Json::Value& object_json, ExportFormatType targetType)
{
	// top-level container needs handle for attached host-controls like Oversampling-rate.
	// This will be skipped by CUG::Export() if Container is not visible ("Show on Panel" = false).
	// CUG::Export(object_json, targetType);
	object_json["handle"] = Handle(); // this is all we need.

	ExportChildren(object_json, targetType);

	if (targetType == SAT_SUBCONTROLS_GUI ) // export to VST3, save skin bitmap.
	{
		// Skin.
		auto skinName = WStringToUtf8(getSkin()->Name);

		// Background Image.
		gmpi_sdk::MpString returnUri;
		GmpiResourceManager::Instance()->RegisterResourceUri(Handle(), skinName, "background", "Image", &returnUri); // resource leak, but OK for a hack.

		if (targetType == SAT_SUBCONTROLS_GUI)
		{
			// Nag Screen.
			{
				object_json["vst_about_text"] = WStringToUtf8(Document()->m_vst_about_text);
				object_json["vst_nag_user"] = Document()->m_vst_unregistered;
			}
		}
	}

	{
		object_json["skin"] = (targetType == SAT_SYNTHEDIT_GUI_STRUCT) ? "default3" : WStringToUtf8(getSkin()->Name);
	}

	// In rack mode the top-level panel's backdrop is the rack the view draws;
	// a skin background image would sit on top and hide it.
	const bool rackIsTheBackground =
		targetType == SAT_SYNTHEDIT_GUI_PANEL && !Container() && Document() && Document()->rackMode;

	if ((targetType == SAT_SUBCONTROLS_GUI || targetType == SAT_SYNTHEDIT_GUI_PANEL) && !rackIsTheBackground)
	{
		// inject background image object.
		if (FlagRequiredModuleForExport(L"SE Background Image"))
		{
			const char* moduleTypeId = "SE Background Image";
			Json::Value background_j;
			background_j["type"] = moduleTypeId;

			background_j["handle"] = Document()->uniqueIdDatabase.GenerateUniqueHandleValue();
			background_j["ignoremouse"] = "true";
			Json::Value pin_element(Json::objectValue);
			pin_element["Id"] = 0;
			pin_element["default"] = "background";
			background_j["Pins"].append(pin_element);

			pin_element["Id"] = 1;
			pin_element["default"] = "1"; // tiled
			background_j["Pins"].append(pin_element);

			auto r = GetPanelRect();
			if (targetType == SAT_SUBCONTROLS_GUI)
			{
				// backgrond is shifted to top-left in VST3.
				background_j["pt"] = 0;
				background_j["pl"] = 0;
				background_j["pr"] = gmpi::drawing::getWidth(r);
				background_j["pb"] = gmpi::drawing::getHeight(r);
			}
			else
			{
				background_j["pt"] = r.top;
				background_j["pl"] = r.left;
				background_j["pr"] = r.right;
				background_j["pb"] = r.bottom;
			}

			object_json["modules"].append(background_j);
		}
	}

	if(SAT_SYNTHEDIT_GUI_STRUCT == targetType && draggingLineFromMod > -1)
	{
		object_json["draggingLineFromMod"] = draggingLineFromMod;
		object_json["draggingLineFromPin"] = draggingLineFromPin;
		object_json["draggingLineToMod"] = draggingLineToMod;
		object_json["draggingLineToPin"] = draggingLineToPin;
	}
}

// export an embedded sub-view
void CContainer::Export(Json::Value& object_json, ExportFormatType targetType)
{
	bool isCadmiumView = false;
#ifdef _DEBUG
	// TODO!! Cadmium Views are identified by the container name for now
	if ((targetType == SAT_SYNTHEDIT_GUI_PANEL || targetType == SAT_SYNTHEDIT_GUI_STRUCT) && name.get() == "View")
	{
		object_json["isCadmiumView"] = true;
		isCadmiumView = true;
#if 0
//		ExportCadmiumView(object_json);

		// substitute a Renderer module
		const char* moduleTypeId = "SE CadmiumRenderer";
		object_json["type"] = moduleTypeId;
		object_json["handle"] = Handle();
		ModuleFactory()->GetById(Utf8ToWstring(moduleTypeId))->SetSerialiseFlag();

		// export children as usual, except JSON gets set on the Renderer xml pin.
		Json::Value cadmium_json;
		ExportChildren(cadmium_json, SAT_SYNTHEDIT_GUI_PANEL);

		const auto jsonString = cadmium_json.toStyledString();

		Json::Value pin_element(Json::objectValue);
		pin_element["Id"] = 0;
		pin_element["default"] = jsonString;

		Json::Value pins_element(Json::arrayValue);
		pins_element.append(pin_element);

		object_json["Pins"] = pins_element;
		return;
#endif
	}
#endif

	ExportFormatType childrenTargetType = targetType;

	// On GUI, containers only show if "Visible" enabled.
	if (targetType == SAT_SUBCONTROLS_GUI || targetType == SAT_SYNTHEDIT_GUI_PANEL )
	{
		if (!IsImbeddedView(CF_PANEL_VIEW))
		{
			return;
		}

		if(isRackModule())
		{
			object_json["isRackModule"] = true;
		}
	}

	// On the Structure view, imbedded view is a Panel view (no wires).
	if (targetType == SAT_SYNTHEDIT_GUI_STRUCT)
	{
		if (IsImbeddedView(CF_STRUCTURE_VIEW))
		{
			childrenTargetType = SAT_SYNTHEDIT_GUI_PANEL;
		}
	}

	if (isCadmiumView)
	{
		childrenTargetType = SAT_CADMIUM_VIEW;
	}

	getType()->SetSerialiseFlag(); // always needed, even for GUI.

	CUG::Export(object_json, targetType);

	// Don't bother exporting children for regular container on structure view.
	if (childrenTargetType != SAT_SYNTHEDIT_GUI_STRUCT)
	{
		ExportChildren(object_json, childrenTargetType);
	}
}

void CContainer::Import(std::map<int32_t, CUG*>& uniqueIds, tinyxml2::XMLElement* moduleElement, ExportFormatType targetType)
{
	ExportFormatType childrenTargetType = targetType;

	// On GUI, contains only show if "Visible" enabled.
	if (targetType == SAT_SUBCONTROLS_GUI || targetType == SAT_SYNTHEDIT_GUI_PANEL)
	{
		if (!IsImbeddedView(CF_PANEL_VIEW))
		{
			return;
		}
	}

	// On the Structure view, imbedded view is a Panel view (no wires).
	if (targetType == SAT_SYNTHEDIT_GUI_STRUCT && IsImbeddedView(CF_STRUCTURE_VIEW))
	{
		childrenTargetType = SAT_SYNTHEDIT_GUI_PANEL;
	}

	getType()->SetSerialiseFlag(); // always needed, even for GUI.

	CUG::Import(uniqueIds, moduleElement, targetType);

	if (childrenTargetType == SAT_SYNTHEDIT_DOCUMENT)
	{
		XmlLoadHelper helper(moduleElement);
		Serialise2(helper);

		// If zoom data is missing (old format document), compute center from legacy values.
		// center = (viewSize/2 - scrollPos) / zoom  (scrollPos stored as raw ViewBase scrollPos).
		if (PanelLocationZoom == 0.0f)
		{
			PanelLocationZoom = 1.0f;
			PanelLocationCenter.x = (gmpi::drawing::getWidth(PanelLocation)  / 2.0f + PanelScroll.x) / PanelLocationZoom;
			PanelLocationCenter.y = (gmpi::drawing::getHeight(PanelLocation) / 2.0f + PanelScroll.y) / PanelLocationZoom;
		}
		if (StructLocationZoom == 0.0f)
		{
			StructLocationZoom = 1.0f;
			StructLocationCenter.x = (gmpi::drawing::getWidth(StructLocation)  / 2.0f + StructScroll.x) / StructLocationZoom;
			StructLocationCenter.y = (gmpi::drawing::getHeight(StructLocation) / 2.0f + StructScroll.y) / StructLocationZoom;
		}

		auto skinName = moduleElement->Attribute("skin");
		std::wstring skinNameW = skinName ? Utf8ToWstring(skinName) : L"default3";
		m_skin = SkinMgr::Instance()->getSkin(skinNameW);
	}

	// Don't bother exporting children for regular container on structure view.
	//if (childrenTargetType != SAT_SYNTHEDIT_GUI_STRUCT)
	ImportChildren(uniqueIds, moduleElement, childrenTargetType);

	// some early files are missing the last 'spare' pin in the XML. If so add it.
	// see also: CUG::Import()
	if (!Plugs.back()->isUnusedSpare())
	{
//		_RPT0(0, "Container MISSING Spare pin!!! Fixed Up.\n");
		Plugs.push_back(MakePlug((getType()->getPinDescriptionById(0)))); // 'spare' plug
		assert(Plugs.back()->isUnusedSpare());
	}
}

void CContainer::Export(tinyxml2::XMLElement* moduleElement, ExportFormatType targetType)
{
	ExportFormatType childrenTargetType = targetType;

	// On GUI, containers only show if "Visible" enabled.
	if (targetType == SAT_SUBCONTROLS_GUI || targetType == SAT_SYNTHEDIT_GUI_PANEL)
	{
		if (!IsImbeddedView(CF_PANEL_VIEW))
		{
			return;
		}
	}

	// On the Structure view, imbedded view is a Panel view (no wires).
	if (targetType == SAT_SYNTHEDIT_GUI_STRUCT)
	{
		if (IsImbeddedView(CF_STRUCTURE_VIEW))
		{
			childrenTargetType = SAT_SYNTHEDIT_GUI_PANEL;
		}
	}

	getType()->SetSerialiseFlag(); // always needed, even for GUI.

	CUG::Export(moduleElement, targetType);

	// Don't bother exporting children for regular container on structure view.
	if(childrenTargetType != SAT_SYNTHEDIT_GUI_STRUCT)
	{
		if(my_patch_manager())
		{
			my_patch_manager()->Export(moduleElement, targetType);
		}

		ExportChildren(moduleElement, childrenTargetType);
	}

	if (childrenTargetType == SAT_SYNTHEDIT_DOCUMENT)
	{
		XmlSaveHelper helper(moduleElement);
		Serialise2(helper); // handle

		moduleElement->SetAttribute("skin", WStringToUtf8(m_skin->Name).c_str());
	}
}

void CContainer::ExportChildren(tinyxml2::XMLElement* object_xml, ExportFormatType targetType)
{
	// First modules.
	auto modules_json = object_xml->GetDocument()->NewElement("modules");
	object_xml->LinkEndChild(modules_json);
	for (auto m : BaseList)
	{
		if (dynamic_cast<CLine2*>(m) == nullptr)
		{
			auto module_json = object_xml->GetDocument()->NewElement("module");
			modules_json->LinkEndChild(module_json);

			Archive2 ar{ true , module_json, targetType };
			m->Serialize2(ar);
			m->getType()->SetSerialiseFlag();
		}
	}

	// Second lines.
	modules_json = object_xml->GetDocument()->NewElement("lines");
	object_xml->LinkEndChild(modules_json);
	for (auto m : BaseList)
	{
		auto line = dynamic_cast<CLine2*>(m);
		if (line == nullptr)
			continue;

		if (!reportIfUnserializable(line))
			continue;

		auto module_json = object_xml->GetDocument()->NewElement("line");
		line->Export(module_json, targetType);
		modules_json->LinkEndChild(module_json);
	}
}

bool CContainer::reportIfUnserializable(CLine2* line)
{
	std::wstring whyNot;
	if (line->canSerialize(whyNot))
		return true;

	// Document() is null on the detached container used for copy/paste, and
	// CDocOb::Application() dereferences it without checking.
	if (auto* doc = Document(); doc && doc->Application())
		doc->Application()->SeMessageBox(whyNot.c_str(), L"Connector not saved", MB_OK | MB_ICONWARNING);

	return false;
}

namespace
{
// Describes a connector the loader could not rebuild, from what the file said plus
// what actually resolved, e.g.
//    "Osc1" pin 3 to "MoogFilter" pin 7 (no such pin)
// Enough to identify the cable in the project and to see which end went missing.
std::wstring describeDroppedLine(const std::map<int32_t, CUG*>& uniqueIds, tinyxml2::XMLElement* lineElement, CLine2* line)
{
	int fModule = 0, tModule = 0, fPlug = 0, tPlug = 0;
	lineElement->QueryIntAttribute("fMod", &fModule);
	lineElement->QueryIntAttribute("tMod", &tModule);
	lineElement->QueryIntAttribute("fPlg", &fPlug);
	lineElement->QueryIntAttribute("tPlg", &tPlug);

	const auto describeEnd = [&uniqueIds](int handle, int pin, IPlug* resolved) -> std::wstring
	{
		std::wstring r;

		if (auto it = uniqueIds.find(handle); it != uniqueIds.end())
			r = L"\"" + it->second->GetName() + L"\"";
		else
			r = L"<no module with handle " + std::to_wstring(handle) + L">";

		r += L" pin " + std::to_wstring(pin);

		if (!resolved)
			r += pin < 0 ? L" (invalid pin index)" : L" (no such pin)";

		return r;
	};

	return describeEnd(fModule, fPlug, line->FromPlug) + L" to " + describeEnd(tModule, tPlug, line->ToPlug);
}
}

void CContainer::ImportChildren(std::map<int32_t, CUG*>& uniqueIds, tinyxml2::XMLElement* containerX, ExportFormatType targetType)
{
	std::vector< std::pair<PatchParameter_base*, int>> parameterModuleHandles;

	auto patchmanager_xml = containerX->FirstChildElement("PatchManager");
	if (patchmanager_xml)
	{
		if (m_patch_manager)
		{
			delete m_patch_manager;
		}
		m_patch_manager = new CPatchManager(this);
		m_patch_manager->Import(patchmanager_xml, targetType, parameterModuleHandles);
		m_patch_manager->RegisterHandles(&(Document()->uniqueIdDatabase), false);
	}
	
	auto modulesX = containerX->FirstChildElement("modules");
	for (auto module_json = modulesX->FirstChildElement("module") ; module_json ; module_json = module_json->NextSiblingElement("module"))
	{
		const char* typeString = nullptr;
		module_json->QueryStringAttribute("type", &typeString);
		auto typeStringW = Utf8ToWstring(typeString);

		auto mi = GetByIdSerializing(typeStringW); // loading module may differ from local module info
		assert(mi);
		if (!mi)
		{
			// A module that resolves nowhere - not even the in-use-old-module list. Skip it
			// (its connectors are reported as dropped below) rather than crash in CreateDocObject.
			// Document() is null on the detached container used for copy/paste, and
			// CDocOb::Application() dereferences it without checking.
			if (auto* doc = Document(); doc && doc->Application())
				doc->Application()->SeMessageBoxAsync(
					(L"Module not found in factory: " + typeStringW).c_str(), L"", MB_OK | MB_ICONSTOP);
			continue;
		}

		auto docob = CreateDocObject(mi); // adds default plugs.

		if (docob)
		{
			docob->SetContainer(this);

			Archive2 ar{ false, module_json , targetType, &uniqueIds };
			docob->Serialize2(ar);
			BaseList.push_back(docob);
		}
	}

	auto linesX = containerX->FirstChildElement("lines");
	if (linesX)
	{
		std::vector<std::wstring> droppedLines;

		for (auto moduleElement = linesX->FirstChildElement("line"); moduleElement; moduleElement = moduleElement->NextSiblingElement("line"))
		{
			auto l = new CLine2();
			l->CDocOb::SetContainer(this); // required by initialise (to find unique ID database).
			l->Import(uniqueIds, moduleElement, targetType);

			if (l->isValid())
			{
				BaseList.push_back(l);
			}
			else
			{
				// Legitimate when a module lost a pin between SynthEdit versions, and a
				// symptom of a corrupt file otherwise. Either way the user loses a wire
				// they drew, so say so instead of deleting it behind their back.
				droppedLines.push_back(describeDroppedLine(uniqueIds, moduleElement, l));

				Document()->uniqueIdDatabase.Unregister(l);
				delete l;
			}
		}

		if (auto* app = Document() ? Document()->Application() : nullptr; !droppedLines.empty() && app)
		{
			// One message per container, not one per connector: a module that lost a pin
			// can strand every cable that was plugged into it.
			std::wstring msg = std::to_wstring(droppedLines.size())
				+ L" connector(s) in \"" + GetName() + L"\" could not be restored and have been removed:";

			for (const auto& d : droppedLines)
				msg += L"\n    " + d;

			app->SeMessageBox(msg.c_str(), L"Connectors lost while loading", MB_OK | MB_ICONWARNING);
		}
	}
	
	if (m_patch_manager)
	{
		m_patch_manager->InitModulePointers(uniqueIds, parameterModuleHandles);
	}
}

void CContainer::ExportChildren(Json::Value& object_json, ExportFormatType targetType)
{
	assert(targetType == SAT_SUBCONTROLS_GUI
		|| targetType == SAT_SYNTHEDIT_GUI_PANEL
		|| targetType == SAT_SYNTHEDIT_GUI_STRUCT
		|| targetType == SAT_CADMIUM_VIEW
	);

	Json::Value modules_json(Json::arrayValue);
	for( auto m : BaseList )
	{
		Json::Value module_json(Json::objectValue);
		m->Export(module_json, targetType);
		if (!module_json.empty())
		{
			modules_json.append(module_json);
		}
	}
	object_json["modules"] = modules_json;

	// GUI connections.
	{
		Json::Value childConnections(Json::arrayValue);

		for (auto& docob : BaseList)
		{
			CUG* ug = dynamic_cast<CUG*>(docob);

			if (ug == 0)
				continue;

			for (auto it = ug->Plugs.begin(); it != ug->Plugs.end(); ++it)
			{
				IPlug* FromP = *it;

				if (FromP->GetDirection() == DR_OUT)
				{
					if (targetType == SAT_SUBCONTROLS_GUI || targetType == SAT_SYNTHEDIT_GUI_PANEL || targetType == SAT_SYNTHEDIT_GUI_STRUCT || targetType == SAT_CADMIUM_VIEW)
					{
						if (FromP->isUiPlug() && !FromP->isIoPlug())
						{
							it_plug_destinations it2(*it); // looks 'thru' container IO connections.

							for (it2.First(); !it2.IsDone(); it2.Next())
							{
								IPlug* ToP = it2.CurrentItem();

								// No point connecting to unused io input plugs, in fact will cause crash. Except wired leaving main container via IO-MOD, which has wires disconnected during save-as-vst.
								if (!ToP->isUnconnectedIOPlug() || dynamic_cast<CPlugIO4*>(ToP)->GetTiedTo()->Container()->Container() == 0)
								{
									CUG* m1 = FromP->UG();
									CUG* m2 = ToP->UG();

									// IO outputs linked to container inputs
									// are handled by automatic default parameter connection
									Json::Value line_element(Json::objectValue);

									line_element["fMod"] = m1->Handle();
									line_element["tMod"] = m2->Handle();

									int fromIdentifier = FromP->getPlugDescID();
									int toIdentifier = ToP->getPlugDescID();

									if (fromIdentifier != 0)
									{
										line_element["fPin"] = fromIdentifier;
									}
									if (toIdentifier != 0)
									{
										line_element["tPin"] = toIdentifier;
									}
									assert(m2->Handle() != 2094399057 || toIdentifier != 3);

									childConnections.append(line_element);
								}
							}
						}
					}
				}
			}
		}
		object_json["connections"] = childConnections;
	}
}

TiXmlElement* CContainer::ExportXml(TiXmlElement* XmlParent, ExportFormatType targetType )
{
	// Child VST Plugins.
	if (targetType == SAT_VST3_CONTROLERS)
	{
		for (auto it = BaseList.begin(); it != BaseList.end(); ++it)
		{
			auto ug = dynamic_cast<CUG*>(*it);

			if (ug)
			{
				ug->ExportXml(XmlParent, targetType);
			}
		}

		return 0;
	}

	CContainer* topLevelSynthContainer = 0;
	TiXmlElement* containerElement = 0;

	if( Container() == 0 ) // indicates 'main' container.
	{
		for( auto it = BaseList.begin(); it != BaseList.end(); ++it )
		{
			topLevelSynthContainer = dynamic_cast<CContainer*>( *it );
			if( topLevelSynthContainer != 0 )
			{
				break;
			}
		}
	}

	if( topLevelSynthContainer != 0 )
	{
		if( targetType == SAT_VST3_PARAMETERS )
		{
			topLevelSynthContainer->get_patch_manager()->ExportAssignParamIndexes(targetType);

			if( topLevelSynthContainer->my_patch_manager() )
			{
				topLevelSynthContainer->my_patch_manager()->ExportXml(XmlParent, targetType );
			}
			return 0;
		}

		if( targetType == SAT_SUBCONTROLS_GUI )
		{
			// hack: out-of-band pass skin name for legacy crap
			CControl::currentVst3SkinName = WStringToUtf8(topLevelSynthContainer->getSkin()->Name);

			// VST GUI Main panel.
			TiXmlElement* viewTemplate = XmlParent;

			auto r = topLevelSynthContainer->GetPanelRect();

			viewTemplate = new TiXmlElement( "Canvas" );
			XmlParent->LinkEndChild( viewTemplate );
			viewTemplate->SetAttribute( "Width", gmpi::drawing::getWidth(r) );
			viewTemplate->SetAttribute( "Height", gmpi::drawing::getHeight(r) );
			viewTemplate->SetAttribute("Skin", WStringToUtf8(topLevelSynthContainer->getSkin()->Name));

			containerElement = viewTemplate;

//			( (CSynthEditDoc*) Document() )->vst3AddBitmap( L"background" );

			// Export Canvas like it was a Container with child modules.
			topLevelSynthContainer->ExportXml_Pt2( viewTemplate, targetType );

			return 0;
		}
/* not needed at present (was used in experimental fix for ug_vst_out)
		if (targetType == SAT_VST3)
		{
			// ensure that these host controls are always available to VST3, even when not explicitly referenced by any module.
			for (auto hc : { HC_CLEAR_TAILS })
			{
				get_patch_manager()->GetHostGeneratedParameter(hc, true, this);
			}
		}
*/
	}

	// Avoid exporting a bunch of empty Container elements to the VST3 GUI.
	if (targetType == SAT_SUBCONTROLS_GUI && !IsImbeddedView(CF_PANEL_VIEW))
	{
		return 0;
	}

	containerElement = CUG::ExportXml(XmlParent, targetType );

	if( containerElement == 0 ) // Muted?
		return 0;

	if (targetType == SAT_SUBCONTROLS_GUI)
	{
		// Substitute VSTGUI version.
		containerElement->SetAttribute("Type", "ContainerV");
	}
	else
	{
		bool expandInline = /*GetOversamplingRate() == 0 &&*/ ExpandInline();

		// It's possible user saved a VST Effect with no patch-automator.
		// In this case we must not-expand the top-level container.
		// It should be the only container having a patch-manager.
		if( my_patch_manager() )
		{
			expandInline = false;
		}
/*
		if (GetOversamplingRate() != 0)
		{
			containerElement->SetAttribute("Oversample", GetOversamplingRate());
			containerElement->SetAttribute("OversampleFilter", GetOversamplingFilterPoles());
		}
*/
		if( !expandInline )
		{
			containerElement->SetAttribute( "ExpandInline", 0 );

			if( GetNoteSource() )
			{
				containerElement->SetAttribute( "Polyphonic", 1 );

				// we need to ensure that HC_POLYPHONY etc are always exported (they are not created unless a Polyphony-Control module is present)
				// "SE VoiceParameterWatcher" assumes they exist. 
				for (auto hc : { HC_POLYPHONY, HC_POLYPHONY_VOICE_RESERVE, HC_VOICE_ALLOCATION_MODE, HC_PORTAMENTO })
				{
					get_patch_manager()->GetHostGeneratedParameter(hc, true, this);
				}
			}
		}

		// Polyphony.
		//containerElement->SetAttribute("VoiceCount", getPolyphony());
		//containerElement->SetAttribute("VoiceReserveCount", getPolyphonyReserve());
	}

	ExportXml_Pt2( containerElement, targetType );
	return 0;
}

void CContainer::ExportXml_Pt2( TiXmlElement* containerElement, ExportFormatType targetType )
{
	if (targetType == SAT_SUBCONTROLS_GUI)
	{
		if (IsImbeddedView(CF_PANEL_VIEW) || Container()->Container() == 0)
		{
			assert(false); // what?? GUI exported via JSON.
			//containerElement->SetAttribute("OffsetX", Panel WndOffset.width);
			//containerElement->SetAttribute("OffsetY", Panel WndOffset.height);
		}
		else
		{
			return;
		}
	}

	/*
	moved later becuase child containers may add additional parameters (e.g. HC_POLYPHONY)

	// Will have patch-manager if user inserted one, or if user didn't - in main container, or in VST mode
	// in VST plugin container.
	if( my_patch_manager() )
	{
		if( targetType == SAT_SYNTHEDIT_DSP )
		{
			my_patch_manager()->ExportAssignParamIndexes(targetType);
		}

		my_patch_manager()->ExportXml(containerElement, targetType );
	}
	*/

	// Modules.
	TiXmlElement* childModules = new TiXmlElement( "Modules" );
	containerElement->LinkEndChild( childModules );

	// save XML for use in a plugin. Excludes "Sound Out" and "MIDI In" modules.
	// In top-level container, we disregard "Sound Out" etc. Only Serialize first container we find.
	if( Container() == 0 && targetType != SAT_SYNTHEDIT_DSP)
	{
		for( auto it = BaseList.begin() ; it != BaseList.end() ; ++it )
		{
			CContainer* topLevelSynthContainer = dynamic_cast<CContainer*>(*it);

			if( topLevelSynthContainer != 0 )
			{
				topLevelSynthContainer->ExportXml(childModules, targetType );
				break;
			}
		}
	}
	else
	{
		// Add contained modules as child elements.
		for( auto it = BaseList.rbegin() ; it != BaseList.rend() ; ++it )
		{
			CUG* ug = dynamic_cast<CUG*>(*it);

			if( ug != 0 )
			{
				ug->ExportXml(childModules, targetType );
			}
		}
	}

	// connections
	TiXmlElement* childConnections = new TiXmlElement( "Lines" );
	containerElement->LinkEndChild( childConnections );

	for (auto& docob : BaseList)
	{
		CUG* ug = dynamic_cast<CUG*>(docob);

		if( ug == 0 )
			continue;

		if ((CDocOb::exportFlags & EXP_DSP) != 0 && !ug->doExport())
			continue;

		for (auto& FromP : ug->Plugs)
			{
			if (FromP->GetDirection() != DR_OUT)
				continue;

			if (FromP->isUiPlug())
				continue;

			assert(targetType != SAT_SUBCONTROLS_GUI);

			for (auto& line : FromP->Connectors())
									{
							IPlug* ToP = line->ToPlug;

				if ((CDocOb::exportFlags & EXP_DSP) != 0 && !ToP->UG()->doExport())
					continue;

				// No point connecting to unused io input plugs, in fact will cause crash. Except wire leaving main container via IO-MOD, which has wires disconnected during save-as-vst.
							if (!ToP->isUnconnectedIOPlug() || dynamic_cast<CPlugIO4*>(ToP)->GetTiedTo()->Container()->Container() == 0)
							{
								CUG* m1 = FromP->UG();
								CUG* m2 = ToP->UG();

								// IO outputs linked to container inputs
								// are handled by automatic default parameter connection
								TiXmlElement* module_element = new TiXmlElement("Line");
								int fromIdx = m1->getRuntimePinIndex(FromP, targetType != SAT_SUBCONTROLS_GUI);
								if (fromIdx != 0)
								{
									module_element->SetAttribute("FromPin", fromIdx);
								}
								module_element->SetAttribute("From", m1->Handle());
								int toIdx = m2->getRuntimePinIndex(ToP, targetType != SAT_SUBCONTROLS_GUI);
								if (toIdx != 0)
								{
									module_element->SetAttribute("ToPin", toIdx);
								}
								module_element->SetAttribute("To", m2->Handle());
								childConnections->LinkEndChild(module_element);
							}
						}
					}
				}

	// Will have patch-manager if user inserted one, or if user didn't - in main container, or in VST mode
	// in VST plugin container.
	if (my_patch_manager())
	{
		if (targetType == SAT_SYNTHEDIT_DSP)
		{
			my_patch_manager()->ExportAssignParamIndexes(targetType);
		}

		my_patch_manager()->ExportXml(containerElement, targetType);
	}
}

// Ensure there at least two IO-Mods, return best one for specified pin direction.
CUG* CContainer::GetIoModule(EDirection direction)
{
	std::vector<CUG*> ioModules;

	for(auto* docob : BaseList)
	{
		if((docob->GetFlags() & CF_IO_MOD) != 0)
		{
			if(auto* io = dynamic_cast<CUG*>(docob); io)
				ioModules.push_back(io);
		}
	}

	auto sortLeftToRight = [&ioModules]()
		{
			std::sort(ioModules.begin(), ioModules.end(), [](CUG* a, CUG* b)
				{
					return a->getViewObRect(CF_STRUCTURE_VIEW).left < b->getViewObRect(CF_STRUCTURE_VIEW).left;
				});
		};

	const gmpi::drawing::PointL viewCenter{ 7968 / 2, 7968 / 2 };
	constexpr int offsetX = 500;
	int insertx = viewCenter.x - offsetX;

	while(ioModules.size() < 2)
	{
		auto* io = static_cast<CUG*>(CreateDocObject(L"IO Mod"));
		addToContainer(io);

		auto io_pos = io->getViewObRect(CF_STRUCTURE_VIEW);
		io_pos = offsetRect(io_pos, { insertx, viewCenter.y });
		io->setViewObRect(CF_STRUCTURE_VIEW, io_pos);

		ioModules.push_back(io);
		insertx += offsetX * 2;
	}

	sortLeftToRight();

	const auto leftmost = ioModules.front();
	const auto rightmost = ioModules.back();

	return direction == DR_IN ? rightmost : leftmost;
}

void CContainer::DeleteAll()
{
	while( !BaseList.empty() )
	{
		CDocOb* d = BaseList.back(); //.RemoveTail();
		BaseList.pop_back();

		// CUG::Removeplug needs handle. So don't unregister till after deleted.
		int uniqueId = d->Handle();
		delete d;
		Document()->uniqueIdDatabase.Unregister(uniqueId);
	}
}

// after loading, all objected need their parent container pointer set.
// however, we don't want to call any overides of Set Container(), as objects havent
// had PostLoad() or Upgrade() called yet
void CContainer::SetParentPointersRecursive( CContainer* p_container )
{
	CDocOb::SetParentPointersRecursive( p_container );

	if( m_patch_manager )
		m_patch_manager->SetContainer(this);

	for( auto it = BaseList.rbegin() ; it != BaseList.rend() ; ++it )
	{
		(*it)->SetParentPointersRecursive(this);
	}
}

void CContainer::preSaveState()
{
	CUG::preSaveState();

	// Synthesize legacy serialization values from view center.
	// Assume zoom=1.0 and view size 500x500 for backward compatibility.
	// Legacy readers interpret PanelScroll as raw scrollPos, where
	// scrollPos = refSize/2 - center * zoom  (with refSize=500, zoom=1).
	constexpr float assumedSize = 500.0f;
	constexpr float halfSize = assumedSize / 2.0f;

	PanelScroll.x = static_cast<int32_t>(PanelLocationCenter.x - halfSize);
	PanelScroll.y = static_cast<int32_t>(PanelLocationCenter.y - halfSize);
	PanelLocation = { 0, 0, static_cast<int32_t>(assumedSize), static_cast<int32_t>(assumedSize) };

	StructScroll.x = static_cast<int32_t>(StructLocationCenter.x - halfSize);
	StructScroll.y = static_cast<int32_t>(StructLocationCenter.y - halfSize);
	StructLocation = { 0, 0, static_cast<int32_t>(assumedSize), static_cast<int32_t>(assumedSize) };

	for (auto m : BaseList)
		m->preSaveState();
}

void CContainer::UpgradeAll(int version)
{
	if (BaseList.empty())
		return;

	// Careful to iterate in reverse and to decrement iterator BEFORE upgrading module. Reverse iterator didn't work, it keeps a pointer to 'next' object.
	auto it = BaseList.end();
	--it;
	bool done = BaseList.empty();
	while (!done)
	{
		auto ug = *it;
		done = it == BaseList.begin(); // have to test before ug (potentially) deleted and iterator possibly invalidated.
		if (!done)
		{
			--it;
		}
		ug->Upgrade(version);
	}
}

void CContainer::Upgrade(int from_version)
{
	CUG::Upgrade(from_version);

	OnHasPatchSelectionChanged();

	if(IPlug* p = GetPlug(L"Polyphony"); p )
	{
		p->SetDefault(L"6"); // prevent DSP XML trying to set this pin on oversamplers (crashes becuase container not set yet)
	}

	UpgradeAll(from_version);
}

bool CContainer::getIgnoreProgramChange()
{
	return GetPlug(PN_IGNORE_PC)->GetDefault().compare(L"1") == 0 || (Container() && Container()->getIgnoreProgramChange());
}

void CContainer::OnMenuCommand(int p_view_type, uint32_t p_command_id, gmpi::drawing::PointL mouse_pos )
{
    switch( p_command_id )
	{
	case POPUP_MENU_TOGGLE_LOCKED:
		toggleLocked();
		break;
	
	case POPUP_MENU_TOGGLE_RACKMODULE:
		m_is_rack_module = !m_is_rack_module;
		/* something like
		if( Container() ) // main has none
			Container()->VO_Notify( OM_LAYOUT_CHANGE2, this );
		*/

		break;

	case POPUP_MENU_STRUCTURE_WPF:
		Document()->OpenView(this, CF_STRUCTURE_VIEW); //, GetName() );
		break;

	//case POPUP_MENU_DIRECTX_STRUCTURE_VIEW:
	//	Document()->OpenView(this, CF_STRUCTURE_VIEW, L"!");
	//	break;

	case POPUP_MENU_CONTROLS:
		Document()->OpenView(this, CF_PANEL_VIEW); //, GetName());
		break;

	case POPUP_MENU_SCREENSHOT:
		VO_Notify( OM_SCREENSHOT );
		break;

	case ID_EDIT_DELETE:
		OnEditDelete();
		break;

	case ID_EDIT_COPY:
		OnEditCopy();
		break;

	case ID_EDIT_PASTE:
		OnEditPaste({ -1,-1 }, p_view_type);
		break;

	case POPUP_MENU_PASTE:
		OnEditPaste( mouse_pos, p_view_type );
		break;

	case ID_EDIT_CUT:
		if( OnEditCopy() )
		{
			OnEditDelete();
		}

		break;

	case ID_EDIT_CONTAIN:
		OnEditContain();
		break;

	case ID_EDIT_UNCONTAIN:
		OnEditUnContain();
		break;

	case ID_EDIT_SELECT_ALL:
		OnEditSelectAll();
		break;

	case ID_EDIT_MOVEFRONT:
		OnEditMoveFront();
		break;

	case ID_EDIT_MOVEBACK:
		OnEditMoveBack();
		break;

	case ID_INS_PREFAB:
	{
		std::wstring newName;
		auto* app = GetApp();
		const auto containerHandle = Handle();

		// Match the single-extension overload's default (.syntheditprefab + .synthedit),
		// then offer the legacy formats when SE 1.5 is on-disk to auto-upgrade them.
		std::vector<std::wstring> prefabExtensions = { L"syntheditprefab", L"synthedit" };
#ifdef _WIN32
		if (legacyExternalApp::create())
		{
			prefabExtensions.push_back(L"seprefab");
			prefabExtensions.push_back(L"se1");
		}
#endif

		app->FileDialogAsync(true, std::move(prefabExtensions), newName,
			[app, containerHandle, p_view_type, mouse_pos](int result, std::wstring selectedName)
			{
				if (result != IDOK)
					return;

				auto* document = app->Document();
				if (!document)
					return;

				auto* container = dynamic_cast<CContainer*>(document->uniqueIdDatabase.HandleToObjectWithNull(containerHandle));
				if (container)
				{
					container->LoadPrefab(p_view_type, std::move(selectedName), mouse_pos);
				}
			}
		);
	}
	break;

#if defined( _DEBUG )
	case POPUP_MENU_USER_DEF+33:
	{
		//Document()->NotifyAllViews( nullptr, OM_RELOAD_VIEWS, (void*)(/*CF_AUTOMATION_VIEW|*/CF_STRUCTURE_VIEW) );
	}
	break;
#endif

	default:
		CUG_with_patches::OnMenuCommand(p_view_type, p_command_id,mouse_pos);
		break;
	}

}

void CContainer::DoHostCommand(int p_command_id)
{
	switch (p_command_id)
	{
	case HC_LoadSubPreset:
		LoadSubPreset(true);
		break;

	case HC_LoadSubPresetRelaxedMatching:
		LoadSubPreset(false);
		break;

	case HC_SaveSubPreset:
		SaveSubPreset();
		break;

	default:
		CUG_with_patches::DoHostCommand(p_command_id);
	};
}

void CContainer::LoadSubPreset( bool strictMatching)
{
	std::wstring ext = L"xmlpreset";

	auto* app = GetApp();
	const auto containerHandle = Handle();

	app->FileDialogAsync(true, ext, std::wstring{},
		[app, containerHandle, strictMatching](int result, std::wstring filename)
		{
			if (result != IDOK)
				return;

			auto* document = app->Document();
			if (!document)
				return;

			auto* container = dynamic_cast<CContainer*>(document->uniqueIdDatabase.HandleToObjectWithNull(containerHandle));
			if (!container)
				return;

			TiXmlDocument doc;
			doc.LoadFile(WStringToUtf8(app->ResolveFilename(filename, L"")));

			if (doc.Error())
			{
				assert(false);
				return;
			}

			TiXmlElement* pElem = TiXmlHandle(&doc).FirstChildElement().Element();
			if (!pElem)
				return;

#ifdef _DEBUG
			const char* pKey = pElem->Value();
			assert(strcmp(pKey, "Presets") == 0);
#endif

			container->my_patch_manager()->ImportSubPresetXml(pElem, container, strictMatching);
		}
	);
}

void CContainer::SaveSubPreset()
{
	std::wstring ext = L"xmlpreset";

	auto* app = GetApp();
	const auto containerHandle = Handle();

	app->FileDialogAsync(false, ext, std::wstring{},
		[app, containerHandle](int result, std::wstring filename)
		{
			if (result != IDOK)
				return;

			auto* document = app->Document();
			if (!document)
				return;

			auto* container = dynamic_cast<CContainer*>(document->uniqueIdDatabase.HandleToObjectWithNull(containerHandle));
			if (!container)
				return;

			TiXmlDocument doc;
			TiXmlDeclaration* decl = new TiXmlDeclaration("1.0", "", "");
			doc.LinkEndChild(decl);

			TiXmlElement* element = new TiXmlElement("Presets");
			doc.LinkEndChild(element);

			container->my_patch_manager()->ExportSubPresetXml(element, container);

			doc.SaveFile(WStringToUtf8(app->ResolveFilename(filename, L"")));
		}
	);
}

void CContainer::GetSelection(DO_LIST& p_list)
{
	for( auto it = BaseList.begin() ; it != BaseList.end() ; ++it )
	{
		CDocOb* vo = *it;

		if( vo->GetSelected() )
		{
			p_list.push_back(vo);
		}
	}
}

void CContainer::OnEditMoveFront()
{
	// moving things in multiple selection can lead to infinite loops as things get moved up/down list.
	// solution, copy selection to 2ndary list.
	DO_LIST selection;
	GetSelection(selection);

	for( auto it = selection.begin() ; it != selection.end() ; ++it )
	{
		MoveChildToFront( *it );
	}
}

void CContainer::OnEditMoveBack()
{
	DO_LIST selection;
	GetSelection(selection);

	for( auto it = selection.begin() ; it != selection.end() ; ++it )
	{
		MoveChildToBack( *it );
	}
}

void CContainer::OnEditToPrefab()
{
	const std::wstring ext{ L"syntheditprefab" };
	wstring fname = L"myprefab." + ext;

	for(auto& vo : BaseList)
	{
		if(auto cont = dynamic_cast<CContainer*>(vo); cont)
		{
			auto name = SanitizeFileName(cont->name.get());
			if(name != "Container")
			{
				fname = Utf8ToWstring(name) + L"." + ext;
				break;
			}
		}
	}

	auto* app = GetApp();
	const auto containerHandle = Handle();

	app->FileDialogAsync(false, ext, fname,
		[app, containerHandle, ext](int result, std::wstring selectedFname)
		{
			if (result != IDOK)
				return;

			auto* document = app->Document();
			if (!document)
				return;

			auto* container = dynamic_cast<CContainer*>(document->uniqueIdDatabase.HandleToObjectWithNull(containerHandle));
			if (!container)
				return;

			const std::wstring l_filename = app->ResolveFilename(selectedFname, ext);

			std::filesystem::path prefabPath(l_filename);

			// Force correct extension, else won't load.
			prefabPath.replace_extension(ext);

			tinyxml2::XMLDocument buffer;
			container->SerialiseSelectedModules(buffer, true);

			buffer.SaveFile((const char*) prefabPath.generic_u8string().c_str());

			app->RefreshModuleData(false, false, true);
		}
	);
}

// Containerize (Containerise) Selection.
CContainer* CContainer::OnEditContain()
{
	SuspendDSP x(Document()->Application());

	// prevent crash in properties pane as module is removed.
	Document()->Application()->NotifyFast(OM_SHOW_PROPERTIES, nullptr);

	gmpi::drawing::RectL r{};  // calc position of new container
	bool emptySelection = true;
	for( auto vo : BaseList)
	{
		if (auto line = dynamic_cast<CLine2*>(vo) ; line)
		{
			// lines are included when the modules at both ends are selected
			line->SetSelected(line->FromPlug->UG()->GetSelected() && line->ToPlug->UG()->GetSelected());
		}
		else
		{
			if (auto ug = dynamic_cast<CUG*>(vo) ; ug && vo->GetSelected())
			{
				emptySelection = false;

				// take postion into account
				auto ug_rect = ug->getViewObRect(CF_STRUCTURE_VIEW);
				ug_rect.right = max( ug_rect.right, ug_rect.left + 60 ); // ensure rect not empty else UnionRect fails.
				ug_rect.bottom = max( ug_rect.bottom, ug_rect.top + 60 ); // ensure rect not empty else UnionRect fails.

				if( gmpi::drawing::empty(r))
					r = ug_rect;
				else
					r = gmpi::drawing::unionRect(r, ug_rect);

				if ((vo->GetFlags() & CF_IO_MOD) != 0)
				{
					Application()->SeMessageBox((L"Can't Containerize an 'IO Module'. Please un-select it and try again"), L"", MB_OK | MB_ICONEXCLAMATION);
					return {};
				}
			}
		}
	}

	if (emptySelection)
		return {};

	// Add 'placeholder IO-Mod (Container)
	auto placeHolderIoMod = (CContainer*)CreateDocObject(L"Container");
	placeHolderIoMod->SetSelected(true);

	addToContainer(placeHolderIoMod);

	const int firstDummyConnection = static_cast<int>(placeHolderIoMod->Plugs.size()) - 1;
	const auto placeHolderHandle = placeHolderIoMod->Handle();

	// create new container
	auto new_cont = (CContainer*)CreateDocObject(L"Container");
	new_cont->SetSelected(false);

	addToContainer(new_cont);

	// get IO Mods
	auto input_io_mod = new_cont->GetIoModule();
	auto output_io_mod = new_cont->GetIoModule(DR_OUT);

	// route outer wires to container
	CUG::is_containerizing = true;
	// first inputs.
	{
		std::vector<CLine2*> lines;
		for (auto vo : BaseList)
		{
			if (auto line = dynamic_cast<CLine2*>(vo); line)
			{
				if (!line->FromPlug->UG()->GetSelected() && line->ToPlug->UG()->GetSelected())
					lines.push_back(line);
			}
		}

		// rough sort on plugs estimated y-position
		std::sort(lines.begin(), lines.end(),
			[](const CLine2* a, const CLine2* b)
			{
				const auto a_rect = a->FromPlug->UG()->getViewObRect(CF_STRUCTURE_VIEW);
				const auto b_rect = b->FromPlug->UG()->getViewObRect(CF_STRUCTURE_VIEW);

				const auto a_plugindex = a->FromPlug->getPlugDescID();
				const auto b_plugindex = b->FromPlug->getPlugDescID();

				const auto plugHeight = 12;
				return a_rect.top + a_plugindex * plugHeight < b_rect.top + b_plugindex * plugHeight;
			}
		);

		// calc how many connections each plug has. Plugs with the most connects gets to be the proxy.
		std::unordered_map<IPlug*, int> plugConnectionCount;
		for (auto& line : lines)
		{
			plugConnectionCount[line->FromPlug]++;
			plugConnectionCount[line->ToPlug]++;
		}

		while (!lines.empty())
		{
			auto line = lines.front();

			// whichever pin (inner/outer) has the most connections gets to be proxied through the IO Mod.
			if (plugConnectionCount[line->FromPlug] > plugConnectionCount[line->ToPlug])
			{
				auto outerPlug = line->FromPlug;
				auto l2 = AddLine(outerPlug, new_cont->GetSparePlug());
				auto innerPlug = placeHolderIoMod->GetSparePlug();

				for (auto it = lines.begin(); it != lines.end(); )
				{
					if( (*it)->FromPlug == outerPlug)
					{
						AddLine(innerPlug, (*it)->ToPlug);
						it = lines.erase(it);
					}
					else
					{
						++it;
					}
				}

				l2->ToPlug->setName(outerPlug->getName());
			}
			else
			{
				auto innerPlug = line->ToPlug;
				AddLine(placeHolderIoMod->GetSparePlug(), innerPlug);
				auto outerPlug = new_cont->GetSparePlug();

				for (auto it = lines.begin(); it != lines.end(); )
				{
					if ((*it)->ToPlug == innerPlug)
					{
						AddLine((*it)->FromPlug, outerPlug);
						it = lines.erase(it);
					}
					else
					{
						++it;
					}
				}

				outerPlug->setName(innerPlug->getName());
			}
		}
	}

	// second outputs.
	{
		std::vector<CLine2*> lines;
		for (auto vo : BaseList)
		{
			if (auto line = dynamic_cast<CLine2*>(vo); line)
			{
				if (line->FromPlug->UG()->GetSelected() && !line->ToPlug->UG()->GetSelected())
					lines.push_back(line);
			}
		}

		// rough sort on plugs estimated y-position
		std::sort(lines.begin(), lines.end(),
			[](const CLine2* a, const CLine2* b)
			{
				auto& plugA = a->ToPlug;
				auto& plugB = b->ToPlug;

				const auto a_rect = plugA->UG()->getViewObRect(CF_STRUCTURE_VIEW);
				const auto b_rect = plugB->UG()->getViewObRect(CF_STRUCTURE_VIEW);

				const auto a_plugindex = plugA->getPlugDescID();
				const auto b_plugindex = plugB->getPlugDescID();

				const auto plugHeight = 12;
				return a_rect.top + a_plugindex * plugHeight < b_rect.top + b_plugindex * plugHeight;
			}
		);

		// calc how many connections each plug has. Plugs with the most connects gets to be the proxy.
		std::unordered_map<IPlug*, int> plugConnectionCount;
		for (auto& line : lines)
		{
			plugConnectionCount[line->FromPlug]++;
			plugConnectionCount[line->ToPlug]++;
		}

		while (!lines.empty())
		{
			auto line = lines.front();

			// whichever pin has the most relevant connects gets to be proxied through the IO Mod.
			if (plugConnectionCount[line->FromPlug] >= plugConnectionCount[line->ToPlug])
			{
				auto innerPlug = line->FromPlug;
				AddLine(innerPlug, placeHolderIoMod->GetSparePlug());
				auto outerPlug = new_cont->GetSparePlug();

				for (auto it = lines.begin(); it != lines.end(); )
				{
					if ((*it)->FromPlug == innerPlug)
					{
						AddLine(outerPlug, (*it)->ToPlug);
						it = lines.erase(it);
					}
					else
					{
						++it;
					}
				}
				outerPlug->setName(innerPlug->getName());
			}
			else
			{
				auto outerPlug = line->ToPlug;
				AddLine(new_cont->GetSparePlug(), outerPlug);
				auto innerPlug = placeHolderIoMod->GetSparePlug();

				for (auto it = lines.begin(); it != lines.end(); )
				{
					if ((*it)->ToPlug == outerPlug)
					{
						AddLine((*it)->FromPlug, innerPlug);
						it = lines.erase(it);
					}
					else
					{
						++it;
					}
				}
				innerPlug->setName(outerPlug->getName());
			}
		}


		// avoid double io-pins to same destination < outside, proxy >
		std::vector< std::pair<IPlug*, IPlug*> > proxyPins;

		for (auto& line : lines)
		{
			IPlug* proxy = {};

			for (auto& it : proxyPins)
			{
				// existing line to destination?
				if (it.first == line->ToPlug)
				{
					proxy = it.second;
					break;
				}
			}

			if (proxy)
			{
				AddLine(line->FromPlug, proxy);
			}
			else
			{
				AddLine(new_cont->GetSparePlug(), line->ToPlug);
				auto l = AddLine(line->FromPlug, placeHolderIoMod->GetSparePlug());
				l->SetSelected(true);

				proxyPins.push_back({ line->ToPlug, l->ToPlug });
			}
		}
	}

	// 'Cut'
	OnEditCopy();
	DeleteSelection();

	// 'Paste'
	new_cont->OnEditPaste({ 0,0 }, CF_STRUCTURE_VIEW);

	// Preserve relative positions of modules on the panel-view while
	// repositioning the group to be centered. Calculate the bounding box of
	// all pasted modules with panel positions, then offset them as a group
	// so their center aligns with the sub-container's center. For modules
	// without panel positions (structure-view insertion), empty() check skips
	// them, leaving null so ViewBase::arrange auto-centers them.
	{
		gmpi::drawing::RectL pasted_bounds{};
		bool first = true;

		for (auto vo : new_cont->BaseList)
		{
			if (auto ug = dynamic_cast<CUG*>(vo); ug && vo->GetSelected())
			{
				const auto rect = ug->getViewObRect(CF_PANEL_VIEW);
				if (!gmpi::drawing::empty(rect))
				{
					if (first)
					{
						pasted_bounds = rect;
						first = false;
					}
					else
					{
						pasted_bounds = gmpi::drawing::unionRect(pasted_bounds, rect);
					}
				}
			}
		}

		// Only center the group if at least one module has a panel position.
		if (!gmpi::drawing::empty(pasted_bounds))
		{
			// Center of the pasted group
			const auto pasted_center_x = (pasted_bounds.left + pasted_bounds.right) / 2;
			const auto pasted_center_y = (pasted_bounds.top + pasted_bounds.bottom) / 2;

			// Center of the sub-container's panel
			const auto container_rect = new_cont->GetPanelRect();
			const auto container_center_x = (container_rect.left + container_rect.right) / 2;
			const auto container_center_y = (container_rect.top + container_rect.bottom) / 2;

			// Offset to move the group's center to the container's center
			const auto dx = container_center_x - pasted_center_x;
			const auto dy = container_center_y - pasted_center_y;

			// Apply the offset to all modules, preserving their relative positions
			for (auto vo : new_cont->BaseList)
			{
				if (auto ug = dynamic_cast<CUG*>(vo); ug && vo->GetSelected())
				{
					auto rect = ug->getViewObRect(CF_PANEL_VIEW);
					if (!gmpi::drawing::empty(rect))
					{
						rect.left += dx;
						rect.right += dx;
						rect.top += dy;
						rect.bottom += dy;
						ug->setViewObRect(CF_PANEL_VIEW, rect);
					}
				}
			}
		}
		else
		{
			// No modules had panel positions. Clear rects to null so they
			// auto-center when the sub-container's panel view is first opened.
			gmpi::drawing::RectL nullRect{};
			for (auto vo : new_cont->BaseList)
			{
				if (auto ug = dynamic_cast<CUG*>(vo); ug && vo->GetSelected())
				{
					ug->setViewObRect(CF_PANEL_VIEW, nullRect);
				}
			}
		}
	}

	// move connections from dummy IO-Mod
	{
		placeHolderIoMod = dynamic_cast<CContainer*>( Document()->uniqueIdDatabase.HandleToObject(placeHolderHandle));

		int inIoModIndex = 0;
		int outIoModIndex = 0;

		for (int i = firstDummyConnection; i < placeHolderIoMod->Plugs.size(); ++i)
		{
			auto& p = placeHolderIoMod->Plugs[i];
			for (auto& c : p->Connectors())
			{
				// using AddLineQuiet to avoid resetting Sliders etc (due to on-first-connection logic
				if (c->FromPlug->UG() == placeHolderIoMod)
					new_cont->AddLineQuiet(output_io_mod->Plugs[outIoModIndex], c->ToPlug);
				else
					new_cont->AddLineQuiet(c->FromPlug, input_io_mod->Plugs[inIoModIndex]);
			}

			if (p->GetDirection() == DR_OUT)
				outIoModIndex++;
			else
				inIoModIndex++;
		}
	}

	// remove dummy IO Mod
	placeHolderIoMod->OnDelete();

	CUG::is_containerizing = false; // IMPORTANT.

	// Position new container in center of original selection
	{
		constexpr int viewType = CF_STRUCTURE_VIEW;

		auto ug_rect = new_cont->getViewObRect(CF_STRUCTURE_VIEW);
		{
			const auto dx = (r.left + r.right) / 2 - (ug_rect.left + ug_rect.right) / 2;
			const auto dy = (r.top + r.bottom) / 2 - (ug_rect.top + ug_rect.bottom) / 2;
			ug_rect.left += dx;
			ug_rect.top += dy;
			ug_rect.right += dx;
			ug_rect.bottom += dy;
		}

		// Constrain to visible portion of window.
		auto visibleRect = getVisibleRect(viewType);
		const int shrink = (std::min)(100, (std::min)(gmpi::drawing::getWidth(visibleRect) / 2, gmpi::drawing::getHeight(visibleRect) / 2));
		visibleRect.left += shrink;
		visibleRect.top += shrink;
		visibleRect.right -= shrink;
		visibleRect.bottom -= shrink;

		if(ug_rect.top < visibleRect.top)
		{
			const auto dy = visibleRect.top - ug_rect.top;
			ug_rect.top += dy;
			ug_rect.bottom += dy;
		}
		if (ug_rect.left < visibleRect.left)
		{
			const auto dx = visibleRect.left - ug_rect.left;
			ug_rect.left += dx;
			ug_rect.right += dx;
		}
		if (ug_rect.bottom > visibleRect.bottom)
		{
			const auto dy = visibleRect.bottom - ug_rect.bottom;
			ug_rect.top += dy;
			ug_rect.bottom += dy;
		}
		if (ug_rect.right > visibleRect.right)
		{
			const auto dx = visibleRect.right - ug_rect.right;
			ug_rect.left += dx;
			ug_rect.right += dx;
		}

		new_cont->setViewObRect(viewType, ug_rect );
	}

	// Position pasted modules centered on the document canvas.
	{
		constexpr int canvasMidpoint = 7968 / 2; // 3984

		gmpi::drawing::RectL bounds{};

		for (auto vo : new_cont->BaseList)
		{
			if (auto ug = dynamic_cast<CUG*>(vo); ug && vo->GetSelected())
			{
				// take position into account
				auto ug_rect2 = ug->getViewObRect(CF_STRUCTURE_VIEW);
				ug_rect2.right = max(ug_rect2.right, ug_rect2.left + 60); // ensure rect not empty else UnionRect fails.
				ug_rect2.bottom = max(ug_rect2.bottom, ug_rect2.top + 60); // ensure rect not empty else UnionRect fails.

				if (gmpi::drawing::empty(bounds))
					bounds = ug_rect2;
				else
					bounds = gmpi::drawing::unionRect(bounds, ug_rect2);
			}
		}

		// Center the selection on the canvas
		const int boundsCenterX = (bounds.left + bounds.right) / 2;
		const int boundsCenterY = (bounds.top + bounds.bottom) / 2;
		new_cont->DragSelection(CF_STRUCTURE_VIEW, canvasMidpoint - boundsCenterX, canvasMidpoint - boundsCenterY);

#if 0 // moving the *in* iomod
		// move output IO Mod to the right of centered modules.
		const int modulesHalfWidth = gmpi::drawing::getWidth(bounds) / 2;
		const int iox = canvasMidpoint + modulesHalfWidth + 70;
		gmpi::drawing::RectL temp{iox, canvasMidpoint, iox, canvasMidpoint};
		output_io_mod->setViewObRect(CF_STRUCTURE_VIEW, temp);
#endif
	}

	// update properties browser with the new container
	new_cont->SetSelected(true, false);
	new_cont->OnProperty();

	Document()->Application()->CommandManager()->DoCheckpoint(L"Containerize");

	return new_cont;
}

// UnContainerize (UnContainerise) Selection.
void CContainer::OnEditUnContain()
{
	SuspendDSP x(Document()->Application());

	// select everything except IO-Mods
	for (auto vo : BaseList)
		vo->SetSelected((vo->GetFlags() & CF_IO_MOD) == 0);

	// Add 'placeholder IO-Mod (Container)
	auto placeHolderIoMod = (CContainer*)CreateDocObject(L"Container");
	placeHolderIoMod->SetSelected(true);

	addToContainer(placeHolderIoMod);

	const auto placeHolderHandle = placeHolderIoMod->Handle();

	for (auto& pin : Plugs)
	{
		auto outsidePin = dynamic_cast<CPlugIO4*>( pin );
		if (!outsidePin || outsidePin->GetNumConnections() == 0)
			continue;

		auto inner_io_plug = outsidePin->GetTiedTo();
		if (!inner_io_plug || inner_io_plug->GetNumConnections() == 0)
			continue;

		auto dummyPlaceholderPin = placeHolderIoMod->GetSparePlug();
		std::vector<CLine2*> copyOfConnectors(inner_io_plug->Connectors().begin(), inner_io_plug->Connectors().end()); // else we modify connectors by adding to it in infinite loop.

		for (auto& c : copyOfConnectors)
		{
			if(inner_io_plug->GetDirection() == DR_OUT)
				AddLine(dummyPlaceholderPin, c->ToPlug);
			else
				AddLine(dummyPlaceholderPin, c->FromPlug);
		}
	}

	CUG::is_containerizing = true;

	// 'Cut'
	OnEditCopy();
	DeleteSelection();

	// 'Paste'
	const gmpi::drawing::RectL rStruct = getViewObRect(CF_STRUCTURE_VIEW);
	const gmpi::drawing::RectL rPanel = GetPanelRect();
	Container()->OnEditPaste({rStruct.left, rStruct.top}, CF_STRUCTURE_VIEW);

	// place holder is now a new module, pasted outside.
	placeHolderIoMod = dynamic_cast<CContainer*>( Document()->uniqueIdDatabase.HandleToObject(placeHolderHandle));

	// Position pasted modules so their bounding-box top-left aligns with this
	// container's top-left in each view. When this container shows its contents
	// as an embedded sub-view ("Visible" / "Controls on Module"), the SubView
	// renders children at container.topLeft + (child.pos - bbox.topLeft); aligning
	// bbox to container.topLeft puts the modules where they appeared visually.
	// The placeholder IO-Mod is excluded — it has a default canvas-centered
	// m_panel_rect that would otherwise dominate the bbox.
	auto alignBboxToTopLeft = [this, placeHolderIoMod](int viewType, const gmpi::drawing::RectL& containerRect)
	{
		gmpi::drawing::PointL bbox_tl{INT_MAX, INT_MAX};
		bool found = false;
		for (auto vo : Container()->BaseList)
		{
			if (!vo->GetSelected() || vo == placeHolderIoMod) continue;
			const auto r = vo->getViewObRect(viewType);
			if (!gmpi::drawing::empty(r))
			{
				bbox_tl.x = (std::min)(bbox_tl.x, r.left);
				bbox_tl.y = (std::min)(bbox_tl.y, r.top);
				found = true;
			}
		}
		if (!found)
			return;

		const auto dx = containerRect.left - bbox_tl.x;
		const auto dy = containerRect.top - bbox_tl.y;
		if (dx == 0 && dy == 0)
			return;

		for (auto vo : Container()->BaseList)
		{
			if (!vo->GetSelected() || vo == placeHolderIoMod) continue;
			auto ug = dynamic_cast<CUG*>(vo);
			if (!ug) continue;
			auto rect = ug->getViewObRect(viewType);
			if (!gmpi::drawing::empty(rect))
			{
				rect.left += dx; rect.right += dx;
				rect.top += dy; rect.bottom += dy;
				ug->setViewObRect(viewType, rect);
			}
		}
	};

	alignBboxToTopLeft(CF_STRUCTURE_VIEW, rStruct);
	alignBboxToTopLeft(CF_PANEL_VIEW, rPanel);

	// move connections from dummy IO-Mod
	{

		for (int i = 0 ; i < Plugs.size(); ++i)
		{
			auto outsidePin = dynamic_cast<CPlugIO4*>( Plugs[i]);
			if (!outsidePin || outsidePin->GetNumConnections() == 0)
				continue;

			auto insidePin = dynamic_cast<CPlugIO4*>( placeHolderIoMod->Plugs[i]);

			for (auto& c1 : outsidePin->Connectors()) // for all outside connectors
			{
				for (auto& c2 : insidePin->Connectors()) // for all inside connectors
				{
					if (outsidePin->GetDirection() == DR_OUT)
						Container()->AddLine(c1->ToPlug, c2->FromPlug);
					else
						Container()->AddLine(c1->FromPlug, c2->ToPlug);
				}
			}
		}
	}

	// remove dummy IO Mod
	placeHolderIoMod->OnDelete();

	// remove me
	OnDelete();

	CUG::is_containerizing = false;
}

// return a containers note generating ug (usually a midi to cv) if any
bool CContainer::GetNoteSource()
{
	for( auto d : BaseList)
	{
		if( (d->GetFlags() & CF_NOTESOURCE) != 0)
		{
			return true;
		}
	}

	return false;
}

// Either a polyphony control module (MIDI-CV etc) or oversampled, or has a patch-automator.
bool CContainer::ExpandInline()
{
	// Not correct, if OS Rate matches rate of parent oversampler, container DOES expand inline (unless it has MIDI-CV).

	for( auto it = BaseList.begin() ; it != BaseList.end() ; ++it )
	{
		CUG* cug = dynamic_cast<CUG*>(*it);

		if( cug && (cug->GetFlags() & CF_DONT_EXPAND_CONTAINER) != 0 && !cug->GetMute() )
		{
			return false;
		}
	}

	return true;
}

void CContainer::CopyPatch( int to_patch_lo, int to_patch_hi)
{
	// copy patch name
	const auto originalName = getProgramName();
	int c = 1;
	for( int i = to_patch_lo; i <= to_patch_hi; i++ )
	{
		// To prevent bank-export saving unwanted presets, leave name blank when original name is blank.
		if (originalName.empty())
		{
			SetProgramNameIndexed(i, L"");
		}
		else
		{
			std::wostringstream oss;
			oss << L"(Copy " << c++ << L") " << originalName;
			SetProgramNameIndexed(i, oss.str());
		}
	}

	get_patch_manager()->CopyPatch( to_patch_lo, to_patch_hi);
}

// open whatever views were open when doc last saved
void CContainer::OpenViews()
{
#if 0 // def _DEBUG
	static int totalCoverted = 0;
	if (true)
	{
		// replace PM Texts misused as fixed-valu modules
		std::vector<CUG*> safelist;

		for (auto it = BaseList.begin(); it != BaseList.end(); ++it)
		{
			auto cug = dynamic_cast<CUG*>(*it);
			if (!cug)
				continue;

			if (cug->getType()->UniqueId() != L"SE PatchMemory Text2" && cug->getType()->UniqueId() != L"SE PatchMemory Int" && cug->getType()->UniqueId() != L"SE PatchMemory Float")
				continue;

			bool hasConnections = false;
			for (auto& p : cug->Plugs)
			{
				if (p->GetNumConnections() > 0 && p->getPlugDescID() != 6)//1)
				{
					hasConnections = true;
					break;
				}
			}

			if (hasConnections)
				continue;

			safelist.push_back(cug);
		}

		for (auto cug : safelist)
		{
			std::wstring replacementModuleType;
			if (cug->getType()->UniqueId() == L"SE PatchMemory Text2")
			{
				replacementModuleType = L"SE:GUIFixedValues_text";
			}
			else if (cug->getType()->UniqueId() == L"SE PatchMemory Int")
			{
				replacementModuleType = L"SE:GUIFixedValues_int";
			}
			else if (cug->getType()->UniqueId() == L"SE PatchMemory Float")
			{
				replacementModuleType = L"SE:GUIFixedValues_float";
			}
			else
			{
				continue;
			}

			// get param value
			std::wstring outputValue;

			{
				PatchParameter_base* parameter = get_patch_manager()->GetParameter(cug, 0);
				outputValue = parameter->GetValueString(FT_VALUE);
			}

			// Replace, explicitly: this path dereferences the result immediately, and
			// SetPinDefaults returns null by design.
			CUG* new_module = ReplaceModule(cug, replacementModuleType, ReplaceModuleAction::Replace);

			IPlug* p = new_module->GetPlug(0);

			if (p->GetNumConnections() > 0)
			{
				if(outputValue.size() < 15)
					p->setName(outputValue);
				p->SetDefault(outputValue);
			}
		}

		totalCoverted += safelist.size();
		_RPTN(_CRT_WARN, "Converted PatchMemory Text to GUIFixedValues_text %d\n", totalCoverted);
	}
#endif

	CSynthEditDocBase* d = Document();

	if( ViewOpenFlags & CF_STRUCTURE_VIEW || Container() == nullptr)
	{
		d->OpenView( this, CF_STRUCTURE_VIEW); //, GetName() );
	}

	if( ViewOpenFlags & CF_PANEL_VIEW)
	{
		d->OpenView( this, CF_PANEL_VIEW); //, GetName() );
	}

	for( auto it = BaseList.begin() ; it != BaseList.end() ; ++it )
	{
		CContainer* child_container = dynamic_cast<CContainer*>( *it );

		if( child_container != nullptr )
		{
			child_container->OpenViews();
		}
	}
}

void CContainer::onViewClosed(int32_t viewType)
{
	ViewOpenFlags &= (~viewType);
}

gmpi::drawing::RectL CContainer::GetViewPos(int view_type)
{
	if( view_type == CF_PANEL_VIEW )
		return PanelLocation;
	else if( view_type == CF_STRUCTURE_VIEW )
		return StructLocation;
	else
		return CUG::GetViewPos(view_type);
}

void CContainer::SetViewPos(int view_type, gmpi::drawing::RectL pos)
{
	// Track the current view size (used to derive scroll from center/zoom).
	// The view center remains unchanged when the view is resized.
	if( view_type == CF_PANEL_VIEW )
		PanelLocation = pos;
	else if( view_type == CF_STRUCTURE_VIEW )
		StructLocation = pos;
}

gmpi::drawing::RectL CContainer::GetPanelRect()
{
	return { m_panel_rect.left, m_panel_rect.top, m_panel_rect.right, m_panel_rect.bottom };
}

void CContainer::SetPanelRect(gmpi::drawing::RectL pos)
{
	m_panel_rect = { pos.left, pos.top, pos.right, pos.bottom };
}

gmpi::drawing::RectL CContainer::getVisibleRect(int view_type) const
{
	const auto center = GetViewCenter(view_type);
	const float zoom  = GetZoom(view_type);

	// Use a default visible size when no view is open (matches assumed size in preSaveState).
	constexpr float defaultHalfW = 250.0f;
	constexpr float defaultHalfH = 250.0f;

	const float halfW = (zoom > 0.0f) ? defaultHalfW / zoom : defaultHalfW;
	const float halfH = (zoom > 0.0f) ? defaultHalfH / zoom : defaultHalfH;

	return {
		static_cast<int32_t>(center.x - halfW),
		static_cast<int32_t>(center.y - halfH),
		static_cast<int32_t>(center.x + halfW),
		static_cast<int32_t>(center.y + halfH)
	};
}

gmpi::drawing::Point CContainer::GetViewCenter(int view_type) const
{
	if (view_type == CF_PANEL_VIEW)
		return PanelLocationCenter;
	else
		return StructLocationCenter;
}

void CContainer::SetViewCenter(int view_type, gmpi::drawing::Point center)
{
//	_RPTN(0, "CContainer::SetViewCenter %.2f, %.2f\n", center.x, center.y);
	if(view_type == CF_PANEL_VIEW)
		PanelLocationCenter = center;
	else
		StructLocationCenter = center;

	VO_Notify(OM_VIEW_PANORZOOM_CHANGED);
}

void CContainer::SetPanZoom(int view_type, gmpi::drawing::Point center, float zoomFactor)
{
	if (view_type == CF_PANEL_VIEW)
	{
		PanelLocationCenter = center;
		PanelLocationZoom = zoomFactor;
	}
	else
	{
		StructLocationCenter = center;
		StructLocationZoom = zoomFactor;
	}

	VO_Notify(OM_VIEW_PANORZOOM_CHANGED);
}

void CContainer::SetZoom(int view_type, float zoomFactor)
{
//	_RPTN(0, "CContainer::SetZoom %.2f\n", zoomFactor);
	if (view_type == CF_PANEL_VIEW)
		PanelLocationZoom = zoomFactor;
	else
		StructLocationZoom = zoomFactor;

	VO_Notify(OM_VIEW_PANORZOOM_CHANGED);
}

float CContainer::GetZoom(int view_type) const
{
	if (view_type == CF_PANEL_VIEW)
		return PanelLocationZoom;

	return StructLocationZoom;
}


// create new ug
CDocOb* CContainer::OnNewUG(int p_view_type, const std::wstring& module_id, int32_t handle, gmpi::drawing::PointL p_point )
{
	SuspendDSP x( Document()->Application() );
	std::wstring p_unique_id = module_id;
	gmpi::drawing::PointL p = p_point;
	CDocOb* new_thing = nullptr;

	if( Left( module_id, 3) == (L"*P=") ) // indicates a prefab or vst plugin
	{
		// !!!!this is all crap. should be..
		// ModuleFactory()->Create DisplayObj(p_unique_id)
		// ...thats it. Prefabs should not be handled differently.
		std::wstring FileName = Right( module_id,  module_id.size() - 3 );

		// remove lone leading slash (if no sub dir specified) — accept either separator (ScanFolder writes the platform-native one).
		if( !FileName.empty() && (FileName.front() == L'\\' || FileName.front() == L'/') )
		{
			FileName = Right( FileName,  FileName.size() - 1 );
		}

		std::wstring FileExtension = GetExtension(FileName);

		if (FileExtension == L"synthedit" || FileExtension == L"syntheditprefab" || FileExtension == L"seprefab")
		{
			LoadPrefab( p_view_type, FileName, p );
			return nullptr;
		}
	}
	else
	{
		// Module ID must be in the factory. Used to deref blindly here:
		//     ModuleFactory()->GetById(id)->GetFlags()
		// which short-circuit-evaluated `Container() == nullptr && …` first,
		// then crashed on a null GetById return when adding to the master
		// container. Now we surface it as a clean error so the caller (test
		// harness, CLI script, GUI insert path) can handle it.
		auto* moduleType = ModuleFactory()->GetById(p_unique_id);
		if (!moduleType)
		{
			Application()->SeMessageBox(
				(L"Module not found in factory: " + p_unique_id).c_str(),
				L"AddModule failed", MB_OK);
			return nullptr;
		}

		// can't put io mod in master container
		if (Container() == nullptr && (moduleType->GetFlags() & CF_IO_MOD))
		{
			Application()->SeMessageBox( (L"IO Modules only go inside a Container"), L"", MB_OK );
			return nullptr;
		}

		new_thing = CreateDocObject(p_unique_id);
		//moved		Add Ug( new_thing );
	}

	// move to correct position
	if( new_thing )
	{
		if( p.x < 0 ) // indicates center of view
		{
			p = getViewVisibleCenter(p_view_type);
		}

		// Only position the module on the view the user clicked. Leave the
		// other view's rect null (0,0,0,0); ViewBase::arrange's isNull
		// centering will place it at *that* view's current visible center
		// (TopView::getCenter()) when the view first displays the module.
		// Seeding a position here (formerly via getViewVisibleCenter) gave
		// the persisted rect a phantom zero-size location that conflicted
		// with arrange's post-measure ResizeModule delta math.
		if (p_view_type == CF_STRUCTURE_VIEW)
		{
			gmpi::drawing::PointL structPos = p;
			structPos.x = ((structPos.x + 6) / 12) * 12;
			structPos.y = ((structPos.y + 6) / 12) * 12;
			new_thing->MoveTo(structPos, CF_STRUCTURE_VIEW);
		}
		else
		{
			gmpi::drawing::PointL panelPos = p;
			panelPos.x = ((panelPos.x + 4) / 8) * 8;
			panelPos.y = ((panelPos.y + 4) / 8) * 8;
			new_thing->MoveTo(panelPos, CF_PANEL_VIEW);
		}

		// added to container AFTER position set, makes initial drawing cleaner (as position is correct)
#if 0
		new_thing->CDocOb::SetContainer(this); // required by initialise (to find patch mgr and unique ID database)
		new_thing->Initialise();

		AddUg( new_thing );
#endif
		new_thing->setHandle(handle);

		addToContainer(new_thing);

		setAllSelected(false, new_thing);
		new_thing->SetSelected(true);
		//new_thing->VO_Notify( OM _MOVE_FRONT );
		VO_Notify( OM_CHILD_TO_FRONT, new_thing);
		Document()->SetModified();
	}

	return new_thing;
}

// combine old Initialise() and AddUg()
// except Initialise() is done after adding object to containers list (so that Initialise can recognise voice-containers correctly when you insert a MIDI-CV into an empty sub-container for the first time)
// otherwise Initialise() wrongly assumes *Main* container is voice container
void CContainer::addToContainer(CDocOb* o)
{
	assert(find(BaseList.begin(), BaseList.end(), o) == BaseList.end());
	// required by initialise (to find patch mgr and unique ID database)
	o->SetContainer(this);
	AddSorted(o);

	o->Initialise();

	assert(o->handleIsSet() && "Must call Initialize() first");

	// open in all relevant views
	// Fix bug whereby Patch-Automator gets added twice (and crashes on close).
	VO_Notify(OM_ADD_CHILD, o);
	o->OnAdded(this);
}

void CContainer::LoadPrefab(int p_view_type, std::wstring filename, gmpi::drawing::PointL p)
{
	filename = GetApp()->ResolveFilename(filename, L"syntheditprefab");

	const auto ext = GetExtension(filename);
	if(ext != L"synthedit" && ext != L"syntheditprefab")
	{
#ifdef _WIN32
		// Mirror the File > Open path: SE 1.5 upgrades .se1 → .synthedit and
		// .seprefab → .syntheditprefab. Either result is loadable as prefab payload.
		if (ext == L"se1" || ext == L"seprefab")
		{
			if (auto synthEdit15App = legacyExternalApp::create(); synthEdit15App)
			{
				synthEdit15App->UpgradeProjectFile(filename);

				std::wstring file, path, originalExt;
				decompose_filename(filename, file, path, originalExt);
				const std::wstring newExt = L".synthedit";
				const auto newPathName = combinePathAndFile(path, file + newExt);

				LoadPrefab(p_view_type, newPathName, p);
				return;
			}
		}
#endif

		const wstring errmsg = L"Unsupported prefab file format ( ." + ext + L" ) .You may be able to upgrade using SynthEdit 1.5";
		Application()->SeMessageBox(errmsg.c_str(), L"", MB_OK);
		return;
	}

	tinyxml2::XMLDocument doc;
	doc.LoadFile(WStringToUtf8(filename).c_str());

	if (doc.Error())
	{
		std::wostringstream oss;
		oss << L"Can't open file " << filename << L", error = " << doc.ErrorName() << L"\n";
		GetApp()->SeMessageBox(oss.str().c_str(), L"", MB_OK | MB_ICONSTOP);

		assert(false);
		return;
	}

	OnEditPaste(p, p_view_type, &doc);
	Document()->SetModified();
}

// remove selected modules with no undo checkpoint.
bool CContainer::DeleteSelection()
{
	// Make temp list, as can't delete from main list without invalidating iterator.
	std::vector<CDocOb*> selection;
	std::copy_if(BaseList.begin(), BaseList.end(), std::back_inserter(selection), [](CDocOb* u) {
		return u->GetSelected();
		});

	// sort lines to the front, so they get deleted first. otherwise they leave hanging pointer when their module is deleted first.
	std::sort(selection.begin(), selection.end(), [](CDocOb* a, CDocOb* b) {
		return dynamic_cast<CLine2*>(a) != nullptr && dynamic_cast<CLine2*>(b) == nullptr;
		});

	// Cheap pre-check (per Module_Info) so we only run the host-control sweep when a deleted
	// module actually referenced a module-owned host-control. Deleting plain modules or wires
	// can't orphan a parameter, so they skip it.
	auto referencesModuleOwnedHostControl = [](Module_Info* info) {
		auto any = [](module_info_pins_t& pins) {
			for (auto& it : pins) {
				auto pin = it.second;
				if (pin->isHostControlledPlug() && isModuleOwnedHostControl(pin->getHostConnect()))
					return true;
			}
			return false;
		};
		return any(info->gui_plugs) || any(info->plugs) || any(info->controller_plugs);
	};

	bool mayHaveOrphanedHostControl = false;
	for (auto m : selection)
	{
		if (auto module = dynamic_cast<CUG*>(m))
			mayHaveOrphanedHostControl |= referencesModuleOwnedHostControl(module->getType());

		m->OnDelete();
	}

	// Sweep document-wide (host-controls can be attached above the editing container).
	if (mayHaveOrphanedHostControl)
		Document()->MasterContainer->RemoveOrphanedHostControls();

	return !selection.empty();
}

// remove selected modules *with* undo checkpoint.
void CContainer::OnEditDelete()
{
	SuspendDSP x( Document()->Application() );

	if (DeleteSelection())
	{
		Document()->Application()->CommandManager()->DoCheckpoint(L"Delete");
	}
}

// get a point in center of view, suitable for pasting/inserting new objects
gmpi::drawing::PointL CContainer::getViewVisibleCenter(int p_view_type)
{
	gmpi::drawing::Point center;
	if(p_view_type == CF_PANEL_VIEW)
		center = PanelLocationCenter;
	else
		center = StructLocationCenter;

	// add a small random distance, so objects don't all paste on top of eachother
	const int rand1 = (rand() & 0x3f) - (0x3f / 2);
	const int rand2 = (rand() & 0x3f) - (0x3f / 2);

	return { static_cast<int32_t>(center.x) + rand1, static_cast<int32_t>(center.y) + rand2 };
}

void CContainer::OnEditSelectAll()
{
	/* create massive number of lines to test gfx
		gmpi::drawing::PointL center(600,600);
		CUG *center_cont = (CContainer *) CreateDocObject(L"Oscillator");
		center_cont->SetContainer(this); // prevents it thinkin it's 'Main' and creating ait's own patch manager
		center_cont->Initialise();
		gmpi::drawing::RectL r(center.x,center.y,0,0);
		center_cont->setViewObRect(CF_STRUCTURE_VIEW, r );

		AddUg( center_cont );

		const double length = 450;
		for( double angle = 0 ; angle < 6.28 ; angle += 0.2 )
		{
			double x = length * sin(angle);
			double y = length * cos(angle);

			CContainer *new_cont = (CContainer *) CreateDocObject(L"Container");
			new_cont->SetContainer(this); // prevents it thinkin it's 'Main' and creating ait's own patch manager
			new_cont->Initialise();
			gmpi::drawing::RectL r(center.x + x,center.y+y,0,0);
			new_cont->setViewObRect(CF_STRUCTURE_VIEW, r );
			AddUg( new_cont );
			for( int i = 7 ; i < 47 ; ++i )
			{
				AddLine( center_cont->GetPlug(0), new_cont->GetPlug(i) );
			}
		}

	*/
	setAllSelected(true);
}

// the exception parameter allows caller to unselect all but one module (without screen flicker)
void CContainer::setAllSelected(bool p_selected, CDocOb* p_exception)
{
	for( auto d : BaseList )
	{
		if( d != p_exception )
		{
			d->SetSelected(p_selected);
		}

		// un-highlight lines only during "un-select all", so user can navigate and right-click modules without erasing highlight trace.
		if (!p_selected && p_exception == nullptr)
			d->Highlight(~PinHighlightFlag_Emphasise);
	}
}

// replace a CUG retaining Z-Order.
CDocOb* CContainer::AddReplacementUg( CDocOb* oldUg, CDocOb* newUg )
{
	auto it = find(BaseList.begin(),BaseList.end(), oldUg);

	if( it != BaseList.end() )
	{
		BaseList.insert( it, newUg );
	}

	newUg->SetContainer(this);
	return newUg;
}

#if 0
CDocOb* CContainer::AddUg( CDocOb* o )
{
	assert( o->handleIsSet() && "Must call Initialize() first" );
	assert( find(BaseList.begin(),BaseList.end(), o) == BaseList.end() );
	AddSorted(o);
	o->SetContainer(this);
	// open in all relevant views

//?? not sure, crashes in a different way.		if (!rebuiltCanvas)

	// Fix bug whereby Patch-Automator gets added twice (and crashes on close).
	{
		VO_Notify( OM_ADD_CHILD, o);
	}

	o->OnAdded(this);

	return o;
}
#endif

// Important to add lines to tail of list, ugs to head (eases deleting ugs)
// objects are sorted by z-order, frontmost objects first
void CContainer::AddSorted(CDocOb* doc_ob)
{
	// this can be restored? TODO
	//	doc_ob->SetContainer(this);
	// if module has any patch parameters, get them from holding bin
	//assert( BaseList.Find( doc_ob ) == 0 );
	assert( find(BaseList.begin(),BaseList.end(), doc_ob) == BaseList.end() );

	if( dynamic_cast<CLine2*>( doc_ob ) )
	{
		// BaseList.AddTail( doc_ob );
		BaseList.push_back( doc_ob );
	}
	else
	{
		//BaseList.AddHead( doc_ob );
		BaseList.push_front( doc_ob );
	}
}

void CContainer::PickupLine(int32_t moduleHandle, bool isFromEnd)
{
	// used to delete a line that is picked up via ALT-click
	auto line = dynamic_cast<CLine2*>(Document()->uniqueIdDatabase.HandleToObjectWithNull(moduleHandle));
	if(!line)
		return;

	// pin that we are dragging from.
	{
		auto pin = isFromEnd ? line->FromPlug : line->ToPlug;

		const auto& pins = pin->UG()->Plugs;

		int pinIndex = 0;
		for(; pinIndex < pins.size(); pinIndex++)
		{
			if(pin == pins[pinIndex])
				break;
		}

		draggingLineFromMod = pin->UG()->Handle();
		draggingLineFromPin = pinIndex;
	}

	// pin that line was picked-up from.
	{
		auto pin = isFromEnd ? line->ToPlug : line->FromPlug;

		const auto& pins = pin->UG()->Plugs;

		int pinIndex = 0;
		for(; pinIndex < pins.size(); pinIndex++)
		{
			if(pin == pins[pinIndex])
				break;
		}

		draggingLineToMod = pin->UG()->Handle();
		draggingLineToPin = pinIndex;
	}

	// remove the line, also refreshes the view. Causing the drag line to be added.
	line->OnDelete();
}

void CContainer::ClearDragLine()
{
	draggingLineFromMod = draggingLineFromPin = -1;
	draggingLineToMod = draggingLineToPin = -1;
}

SkinInfo* CContainer::getSkin()
{
	return m_skin;
}

void CContainer::setSkin(SkinInfo* p_skin)
{
	SkinInfo* old_skin = m_skin;
	m_skin = p_skin;

	if( old_skin )
	{
//		old_skin->UnloadUnusedImages();
		// reload all views.
		//Document()->NotifyAllViews( nullptr, OM_RELOAD_VIEWS, (void*)-1 );
	}
}

void CContainer::OnPlugDefaultChange(IPlug* plug)
{
	// Call base class
	CUG_with_patches::OnPlugDefaultChange(plug);

	// don't access plugs while initialising or while loading/upgrading file.
	if( !m_initialising && LoadingVersion() == FILE_FORMAT_VERSION_NUM )
	{
		if( plug == GetPlug(PN_IGNORE_PC) && my_patch_manager() )
		{
			SuspendDSP x( Document()->Application() );
			// no. don't affect parent's patch manager. get_patch_manager()->onContainerIgnoreProgramChangeUpdate();
			my_patch_manager()->onContainerIgnoreProgramChangeUpdate();
		}
	}

	// When 'Visible' previously not used, no GUI object created, so need to refresh GUI when situation changes.
	assert(GetPlug(2)->getName() == (L"Visible"));
	if (plug == GetPlug(2) && plug->GetNumConnections() == 0 && plug->GetDefault() == L"1" )
	{
		Container()->VO_Notify(OM_LAYOUT_CHANGE2, this);
	}
}

void CContainer::OnNewConnection(CLine2* p_line)
{
	CUG_with_patches::OnNewConnection( p_line );

	// When 'Visible' previously not used, no GUI object created, so need to refresh GUI when situation changes.
	assert( GetPlug(2)->getName() == L"Visible" || GetPlug(2)->getName() == L"Controls on Parent"); // will be "Controls on Parent" while upgrading from 1.4
	if( p_line->ToPlug->UG() == this && p_line->ToPlug->getPlugDescID() == 2 && Container() )
	{
		Container()->VO_Notify( OM_LAYOUT_CHANGE2, this );
	}
}

bool CContainer::IsImbeddedView(int view_type)
{
	assert(GetPlug(1)->getName() == L"Controls on Module");
	assert(GetPlug(2)->getName() == L"Visible");
	
	// Add imbedded control/s to module
	// There's now TWO 'Controls on Parent' / 'Visible' plugs, get SE 1.1 one.
	IPlug* showControls = GetPlug(view_type == CF_STRUCTURE_VIEW ? 1 : 2);

	return showControls->GetDefault() == L"1" || showControls->GetNumConnections() > 0;
}

gmpi::drawing::RectL CContainer::getViewObRect(int p_view_type)
{
	if( p_view_type == CF_PANEL_VIEW )
		return PanelWndPosition;
	else
		return CUG_with_patches::getViewObRect(p_view_type);
}

void CContainer::setViewObRect(int p_view_type, gmpi::drawing::RectL& p_rect)
{
	if( p_view_type == CF_PANEL_VIEW )
	{
		if( PanelWndPosition != p_rect )
		{
//			_RPTW4(_CRT_WARN, L"setViewObRect [%d,%d,%d,%d] -> ", PanelWndPosition.left, PanelWndPosition.top, PanelWndPosition.right, PanelWndPosition.bottom);
//			_RPTW4(_CRT_WARN, L"[%d,%d,%d,%d]\n", p_rect.left, p_rect.top, p_rect.right, p_rect.bottom);
			assert( PanelWndPosition.top < 0x10000000 || PanelWndPosition.top > -0x10000000 );
			PanelWndPosition = p_rect;

			if (Container()) // main view has null container
			{
				Container()->VO_Notify(OM_ONCHANGE_CHILD_POSITION_PANEL, (void*)this);
			}
		}
	}
	else
		CUG_with_patches::setViewObRect( p_view_type, p_rect );
}

void CContainer::offsetViewObRect( int p_view_type, int dx, int dy )
{
	// Special handling for controls-on-parent windows.
	// caused bug in MFC view: objects moved by arrow keys 'jump' when GUI re-opened. Due to double-increment of PanelWndOffset. Movement handled by base class now?
	// Revived for DX-View.
	if (p_view_type == CF_PANEL_VIEW)
	{
		if (PanelWndOffset.width > -99999) // un-initialised
		{
			PanelWndOffset.width += dx;
			PanelWndOffset.height += dy;
//			_RPTW2(_CRT_WARN, L"offsetViewObRect(%d,%d) ", dx, dy);
//			_RPTW4(_CRT_WARN, L"-> [%d,%d]\n", PanelWndOffset.width, PanelWndOffset.height);
		}
		else
		{
//			_RPTW2(_CRT_WARN, L"UNINITIALISED offsetViewObRect(%d,%d)\n", dx, dy);
		}
	}

	CUG_with_patches::offsetViewObRect( p_view_type, dx, dy );
}

// used only when running as VST Plugin
// counts number of AUDIO ins/outs
class plug_is_audio
{
	EDirection m_direction;
public:
	plug_is_audio(EDirection p_direction) : m_direction(p_direction) {};
	bool operator()(IPlug* p)
	{
		return p->GetDirection() == m_direction && !p->isUnusedSpare() && p->getDatatype() == DT_FSAMPLE;
	};
};

int CContainer::CountPlugs(EDirection p_direction)
{
	return (int) count_if( Plugs.begin() ,Plugs.end(), plug_is_audio(p_direction) );
}

CSynthEditDocBase* CContainer::Document()
{
	if( m_document ) // only master container knows it
	{
		return m_document;
	}
	else
	{
		if( Container() )
		{
			return Container()->Document();
		}

		return 0;
	}
}

bool CContainer::hasPatchSelector()
{
	// no. may exist two copies during load. Module_Info* patch_sel_type = ModuleFactory()->GetById((L"SE Patch Automator"));
	const wstring patch_sel_type( L"SE Patch Automator" );

	it_doc_ob itr(this);
	for( itr.First() ; !itr.IsDone() ; itr.Next() )
	{
		if( itr.CurrentItem()->getType()->UniqueId() == patch_sel_type )
		{
			return true;
		}
	}

	return false;
}

bool CContainer::hasPatches()
{
	// container has patch control if either:
	// * it has a patch select, or..
	// * it's embedded and it's parent has a patches
	if( hasPatchSelector() )
	{
		return true;
	}

	if( Container() && PatchSlavedToParent() ) // durin cut/paste container may be nullptr
	{
		return Container()->hasPatches();
	}

	return false;
}

int32_t CContainer::VstUniqueID(bool vstWrapperMode)
{
	if (vstWrapperMode) // SynthEdit running as a VST3 plugin.
	{
		// generate an VST2 ID from VST3 GUIID.
		// see also VST2 wrapper createEffectInstance()
		//Steinberg::FUID guid;
		//guid.fromRegistryString(Document()->m_vst_processor_id.c_str());
		//Steinberg::TUID tuid;
		//guid.toTUID(tuid);

		// note this util only works correctly on win ('COM compatible' GUID layout). Steinberg use a different alg on macOS.
		SE_UUID_Util::TUID processorGuid{};
		SE_UUID_Util::fromRegistryString(Document()->m_vst_processor_id.c_str(), processorGuid);

		unsigned char vst2ID[4] = { 'A', 'A', 'A', 'A' };
		int i2 = 0;
		for (int i = 0; i < sizeof(processorGuid); ++i)
		{
			vst2ID[i2] = vst2ID[i2] + processorGuid[i];
			i2 = (i2 + 1) & 0x03;
		}

		return *((int32_t*)vst2ID);
	}

	// relies on 4-char-id being set, but no huge drama if not.
	return id_to_long( Document()->m_vst_4_Char_id); // Compatibility with Cubase-saved banks
}

int CContainer::GetChunk(void** chunk_ptr, bool isPreset)
{
#if 0
	preSaveState();

	if( chunk_storage )
	{
		free( chunk_storage );
		chunk_storage = nullptr;
	}

	assert(isPreset); // no point storing banks in a fxb file. Plugin will ignore all but the first.
	{
		// 64-bit compatible XML format.
		TiXmlDocument doc;
		doc.LinkEndChild(new TiXmlDeclaration("1.0", "", ""));

		my_patch_manager()->ExportPresetXml(VstUniqueID(), &doc, isPreset, -1);

		TiXmlPrinter printer;
		printer.SetIndent(" ");
		doc.Accept(&printer);
		const auto xmlPreset = printer.Str();

		Vst2PresetUtil::wrapperHeader header2 = {};
		header2.sizeA = xmlPreset.size() + sizeof(header2.sizeC); // total size
		header2.sizeC = static_cast<int32_t>(xmlPreset.size()); // XML size

		const auto header_size2 = sizeof(header2.sizeA) + sizeof(header2.sizeB) + sizeof(header2.sizeC); // minus psudo-member.

		// Header then xml into chunk.
		std::string chunk((const char*) &header2, header_size2);
		chunk += xmlPreset;

		chunk_storage = (uint8_t*) malloc(chunk.size());
		memcpy(chunk_storage, chunk.c_str(), chunk.size());
		*chunk_ptr = chunk_storage;

		return static_cast<int>(chunk.size());
	}
#endif
	return 0;
}

int CContainer::SetChunk(void* data, int byteSize, bool isPreset)
{
	return 0;
}

void CContainer::ImportPreset(const std::string& filename, bool isPreset)
{
	TiXmlDocument doc;

	auto fileExtension = GetExtension(filename);
	std::string overridingCategory;

	if (fileExtension == "vstpreset")
	{
		auto xml = VstPresetUtil::ReadPreset( Utf8ToWstring(filename), &overridingCategory);
		doc.Parse(xml.c_str());
	}
	else
	{
		if (fileExtension == "aupreset")
		{
			auto xml = AuPresetUtil::ReadPreset(Utf8ToWstring(filename));
			doc.Parse(xml.c_str());
		}
		else
		{
			doc.LoadFile(WStringToUtf8(GetApp()->ResolveFilename(Utf8ToWstring(filename), L"")));
		}
	}

	if (doc.Error())
	{
		assert(false);
		return;
	}

	my_patch_manager()->ImportPresetXml(&doc, isPreset, -1, overridingCategory);
}

void CContainer::ExportPreset(const std::string& filename, bool isPreset, int presetIndex)
{
	auto fileExtension = GetExtension(filename);
	if (fileExtension == "xmlpreset" || !isPreset)
	{
		TiXmlDocument doc;

		TiXmlDeclaration* decl = new TiXmlDeclaration("1.0", "", "");
		doc.LinkEndChild(decl);

		TiXmlNode* element;
		if (isPreset)
		{
			element = &doc;
		}
		else
		{
			element = new TiXmlElement("Presets");
			doc.LinkEndChild(element);
		}

		my_patch_manager()->ExportPresetXml(VstUniqueID(), element, isPreset, presetIndex);

		std::string fullFilename = filename;

		if (!isPreset)
		{
			// FileDialog() will have assumed default directory
			fullFilename = WStringToUtf8(GetApp()->ResolveFilename(Utf8ToWstring(filename), L"xmlbank"));
		}

		// doc.SaveFile(fullFilename); // wrong indent. VST2 fails to compare presets correctly.
		{
			TiXmlPrinter printer;
			printer.SetIndent(" ");
			doc.Accept(&printer);

			std::ofstream out(fullFilename);
			out << printer.Str();
			out.close();
		}

		return;
	}
	if (fileExtension == "vstpreset")
	{
		std::wstring user_email, serial;
		Document()->Application()->GetRegistrationInfo(user_email, serial);
		string vendorName = WStringToUtf8(user_email);
		if (vendorName.empty())
		{
			vendorName = "SynthEdit"; // important for storing presets that it is not empty.
		}

		std::string categoryName; // TODO !!!
		VstPresetUtil::WritePreset(
			Utf8ToWstring(filename),
			categoryName,
			vendorName,
			WStringToUtf8(Document()->m_vst_product),
			Document()->m_vst_processor_id.c_str(),
			my_patch_manager()->exportVst3Preset(VstUniqueID(), presetIndex)
		);
		return;
	}
	if (fileExtension == "aupreset")
	{
		std::wstring user_email, serial;
		Document()->Application()->GetRegistrationInfo(user_email, serial);
		string vendorName = WStringToUtf8(user_email);
		if (vendorName.empty())
		{
			vendorName = "SynthEdit"; // important for storing presets that it is not empty.
		}

		std::string presetName;
		{
			std::wstring r_file, r_path, r_extension;
			decompose_filename(Utf8ToWstring(filename), r_file, r_path, r_extension);
			presetName = WStringToUtf8(r_file);
		}

		auto unique_manufacturer_id = Document()->Application()->getVendor4charCode();

		const char* macPluginType = nullptr;
		if ((Document()->m_vst_flags & VST_ISSYNTH) != 0)
		{
			macPluginType = "aumu"; // 0x61756D75
		}
		else
		{
			CContainer* prototype = Document()->GetFirstContainer();
			const bool hasMidiInput = prototype && prototype->GetPlug(DR_IN, DT_MIDI, 0) != nullptr;

			if (hasMidiInput)
			{
				macPluginType = "aumf";
			}
			else
			{
				macPluginType = "aufx"; // 0x61756678
			}
		}

//		const auto* pluginType = (Document()->m_vst_flags & VST_ISSYNTH) != 0 ? "aumu" : "aufx";
		bool res = AuPresetUtil::WritePreset(Utf8ToWstring(filename), presetName, macPluginType, unique_manufacturer_id, WStringToUtf8(Document()->m_vst_4_Char_id), my_patch_manager()->exportVst3Preset(VstUniqueID(), presetIndex));
		if (!res)
		{
			char buffer[100];
#ifdef _WIN32
            _strerror_s(buffer, sizeof(buffer), nullptr);
#else
            strerror_r(errno, buffer, sizeof(buffer));
#endif
            
			string systemErrorMsg(buffer); // _strerror_s(nullptr));
			wstring errmsg = L"File open error (" + Utf8ToWstring(systemErrorMsg) + L") : " + Utf8ToWstring(filename);
			Application()->SeMessageBox(errmsg.c_str(), L"", MB_OK);
		}

// ExportPresetXml vs exportVst3Preset !!??
		return;
	}
}

// is container a sub-container, controlled by parent
bool CContainer::PatchSlavedToParent()
{
	// During upgrade, Patch sel will attempt to set patch in
	// sub-containers that havn't been upgraded yet (so don't have "Show Controls" plug)
	// in this case, assume show-controls = <none>
	//	IPlug *p = GetPlug((L"Show Con trols") );
	//	return p != 0 && 0 < StringToInt( p->GetDefault() );
	//	return m_show _controls_on_panel.getValue() /*|| m_show_controls_on_module.getValue()*/;
	// main container needs own patch management (will also return false during serialise because Container is nullptr)
	if( Container() == 0 ) // no, prefab knobs in top level are slaved ....|| Container()->Container() == 0 )
		return false;

	return true; //will not be needed here?...!m_ignore_program_change.getValue();
}

void CContainer::NotifyParameterChange( int action, IGuiHostParameter* parameter )
{
	// All Parent containers need notification. Visible up tree till and including one with a patch-manager.
	CContainer* c = this;
	do
	{
		c->VO_Notify( action, parameter );
		if( c->has_own_patch_mgr() )
		{
			break;
		}
		c = c->Container();
	}while( c );
}


void CContainer::GetTimingRequirements( int& p_flags )
{
	for( auto it = BaseList.begin() ; it != BaseList.end() ; ++it )
	{
		CDocOb* vo = *it;
		vo->GetTimingRequirements( p_flags );
	}
}

// when saving as VST, the panel scroll decorator is not saved
// so move every object on it to top left to compensate
// (only matters if user had scrolled view over)
void CContainer::OffsetChildren(int p_view_type, gmpi::drawing::SizeL p_offset)
{
	gmpi::drawing::RectL r;

	for( auto it = BaseList.begin() ; it != BaseList.end() ; ++it )
	{
		CDocOb* d = *it;
		r = d->getViewObRect(p_view_type);

		if( !gmpi::drawing::empty(r) )
		{
			r.left += p_offset.width;
			r.top += p_offset.height;
			r.right += p_offset.width;
			r.bottom += p_offset.height;
			d->setViewObRect(p_view_type,r);
		}
	}
}

void CContainer::DragSelection(int p_view_type, int dx, int dy)
{
	// Lines first so nodes get detected within bounds correctly.
	if (p_view_type == CF_STRUCTURE_VIEW)
	{
		for (auto d : BaseList)
		{
			if ( dynamic_cast<CLine2*>(d) != nullptr)
			{
				d->Drag(p_view_type, dx, dy);
			}
		}
	}

	for (auto d : BaseList)
	{
		if( d->GetSelected() && dynamic_cast<CLine2*>(d) == 0)
		{
			d->Drag( p_view_type, dx, dy );
		}
	}
	Document()->SetModified();
}

void CContainer::RegisterHandles( bool pasteMode )
{
	if( m_patch_manager != 0 )
	{
		//m_patch_manager->Initialise( &(Document()->uniqueIdDatabase), loaded_from_file );
		m_patch_manager->RegisterHandles( &(Document()->uniqueIdDatabase), pasteMode );
	}

	CUG_with_patches::RegisterHandles( pasteMode );

	for( auto it = BaseList.rbegin() ; it != BaseList.rend() ; ++it )
	{
		(*it)->RegisterHandles(pasteMode);
	}
}

void CContainer::Initialise( bool loaded_from_file ) // called on first create, not serialise
{
	// new main containers, and older files need patch manager contructed
	if( Container() == 0 || hasPatchSelector() )
	{
		if( m_patch_manager == 0 )
		{
			m_patch_manager = new CPatchManager(this);
		}
	}

	m_initialising = true;
	CUG::Initialise(loaded_from_file);
	m_initialising = false;

	if( loaded_from_file )
	{
		// moved from serialise so Ctl_WaveShape::Set Container() don't trigger while graph half loaded
		// (caused it to attempt to register paramter with patch manager (not avail during serialise)
		for( auto it = BaseList.rbegin() ; it != BaseList.rend() ; ++it )
		{
			(*it)->Initialise(loaded_from_file);
		}
	}
	else
	{
		if( Container() != 0 )
		{
			// add io ug
			GetIoModule();
		}
	}
}

void CContainer::OnSaveVST(int targetType)
{
	// VST Container needs a patch manager
	if( Container()->Container() == 0 && m_patch_manager == 0 )
	{
		m_patch_manager = new CPatchManager(this);
		Container()->get_patch_manager()->TransferPatchData(m_patch_manager);
	}

	CUG_with_patches::OnSaveVST(targetType);

	for( auto it = BaseList.begin() ; it != BaseList.end() ; ++it )
	{
		CDocOb* d = *it;
		d->OnSaveVST(targetType);
	}
}

// return this container's patch manager, or nullptr if this container doesn't manage it's own patches
CPatchManager* CContainer::my_patch_manager()
{
	/* moved
	// !!not correct, non PatchSlavedToParent still have one patch, controlled by parent
	// !!really should find either topmost container, or one with patch selctr.
	//	if( !Patch SlavedToParent() )
	//!!slow?
	if( Container() == 0 || / *Container()->Container() == 0 || * /has PatchSelector() )
	{
		if( m_patch_manager == 0 )
		{
			m_patch_manager = new CPatch Manager(this);
		}
	}
	else
	{
		if( m_patch_manager != 0 ) // patch manager no longer needed (may need to save it's patch data?)
		{
			delete m_patch_manager;
			m_patch_manager = 0;
		}
	}
	*/
	return m_patch_manager;
}

// return this container's patch manager, or defers to parent if this container doesn't manage it's own patches
CPatchManager* CContainer::get_patch_manager()
{
	CPatchManager* pm = my_patch_manager();

	if( pm )
	{
		return pm;
	}

	return CUG_with_patches::get_patch_manager();
}

/*
std::wstring CContainer::getProgramNameList()
{
	std::wstring temp;
	int programs = GetnumPrograms();
	for( int i = 0 ; i < programs ; i++ )
	{
		std::wstring t;
		t.Format((L"%d:%s,"),i + 1, GetProgramNameIndexed(i) );
		temp += t;
	}
	return ( temp );
}
*/
std::wstring CContainer::GetProgramNameIndexed(int p_index )
{
	return ( my_patch_manager()->getProgramNameIndexed(p_index) );
}

void CContainer::SetProgramNameIndexed(int p_index, const std::wstring& p_name)
{
	//	program_names[p_index] = p_name;
	my_patch_manager()->setProgramNameIndexed( (p_name), p_index );
	/*
		if(	GetProgram() == p_index )
		{
			get Parameter((L"Program"), false)->OnValueChanged();
			get Parameter((L"Program Name"), false)->OnValueChanged();
		}
	*/
}

std::wstring CContainer::getProgramName()
{
	//	return program_names[GetProgram()];
	return ( my_patch_manager()->getProgramNameIndexed() );
}

void CContainer::setProgramName(const std::wstring& p_name)
{
	//	program_names[GetProgram()] = p_name;
	my_patch_manager()->setProgramNameIndexed( (p_name) );
}

int CContainer::GetProgram()
{
	return get_patch_manager()->GetProgram();
}

void CContainer::SetProgram(int p_program)
{
	assert( p_program >= 0 && p_program < 128 );

	// fails (becuas it asks patch mgr)	if(	GetProgram() != p_program )
	// new way
	if( my_patch_manager() )
		my_patch_manager()->SetProgram(p_program);
}

bool CContainer::canSetPinDefaultsInstead(CUG* old_module, const wstring& replacementModuleType)
{
	if( replacementModuleType != L"Fixed Values" )
		return false;

	if( !dynamic_cast<Ctl_Slider*>( old_module ) )
		return false;

	IPlug* p = old_module->GetPlug(L"Signal Out");
	if( !p )
		return false;

	// only offer it when the slider is the sole source: a destination fed by
	// anything else, or an auto-duplicating pin, must keep its connection
	it_plug_destinations it( p );
	for( it.First(); !it.IsDone() ; it.Next() )
	{
		IPlug* ToP = it.CurrentItem();
		if( ToP->GetNumConnections() > 1 || ToP->autoDuplicate() )
			return false;
	}

	return true;
}

void CContainer::ReplaceModuleAsync( CUG* old_module, const wstring& replacementModuleType,
                                     std::function<void(CUG*)> onComplete )
{
	if( !canSetPinDefaultsInstead( old_module, replacementModuleType ) )
	{
		auto* m = ReplaceModule( old_module, replacementModuleType, ReplaceModuleAction::Replace );
		if( onComplete )
			onComplete( m );
		return;
	}

	Document()->Application()->SeMessageBoxAsync(
		L"Want to just set destination pins default instead?", L"", MB_YESNO | MB_ICONWARNING,
		[this, old_module, replacementModuleType, onComplete = std::move(onComplete)](int32_t answer)
		{
			const auto action = (answer == IDYES) ? ReplaceModuleAction::SetPinDefaults
			                                      : ReplaceModuleAction::Replace;
			auto* m = ReplaceModule( old_module, replacementModuleType, action );
			if( onComplete )
				onComplete( m );
		});
}

CUG* CContainer::ReplaceModule( CUG* old_module, const wstring& replacementModuleType,
                                ReplaceModuleAction action )
{
	SuspendDSP x( Document()->Application() );

	// Slider replaced by fixed-values: the caller may have chosen to write the
	// slider's value into the destination pins' defaults rather than substitute a
	// module. canSetPinDefaultsInstead() decides whether that was even on offer.
	if( action == ReplaceModuleAction::SetPinDefaults
	    && canSetPinDefaultsInstead( old_module, replacementModuleType ) )
	{
		Ctl_Slider* slider = dynamic_cast<Ctl_Slider*>( old_module );
		IPlug* p = old_module->GetPlug(L"Signal Out");

		if( slider && p )
		{
			PatchParameter_base* parameter = get_patch_manager()->GetParameter( slider, 0 );
			wstring outputValue = DoubleToString( parameter->GetValueReal(FT_VALUE) );

			it_plug_destinations it( p );
			for( it.First(); !it.IsDone() ; it.Next() )
			{
				IPlug* ToP = it.CurrentItem();
				ToP->SetDefault( outputValue );
			}

			// Copy connectors to cope with iterator getting invalidated by operations.
			connectors_t tempConnectors( p->Connectors().begin(), p->Connectors().end() );

			// process connectors in REVERSE order (as we are adding new ones to tail)
			for( auto it2 = tempConnectors.rbegin() ; it2 != tempConnectors.rend() ; ++it2 )
			{
				CLine2* line = *it2;
				// Remove original line from this UG
				Remove(line);
				delete line;
			}

			old_module->OnDelete();
			return {};
		}
	}

	// create new module.
	CUG* new_module = 0;

	if( Left( replacementModuleType, 3) == (L"*P=") ) // indicates a prefab or vst plugin
	{
/* possible enhancement, would need to return 'main' module in prefab for hooking up wires.

		// !!!!this is all crap. should be..
		// ModuleFactory()->Create DisplayObj(p_unique_id)
		// ...thats it. Prefabs should not be handled differently.
		std::wstring FileName = Right( replacementModuleType,  replacementModuleType.size() - 3 );

		// remove lone leading slash (if no sub dir specified)
		if( Left( FileName,  1 ) == L"\ \" )
		{
			FileName = Right( FileName,  FileName.size() - 1 );
		}

		std::wstring FileExtension = GetExtension(FileName);

		if( FileExtension == L"se1" )
		{
			new_module = LoadPrefab( CF_STRUCTURE_VIEW, FileName, {} );
//			return 0;
		}
		*/
	}
	else
	{
		// Module ID must be in the factory. Same hardening as OnNewUG: GetById was
		// dereferenced blindly and crashed when the replacement type was missing.
		auto* moduleType = ModuleFactory()->GetById(replacementModuleType);
		if (!moduleType)
		{
			Application()->SeMessageBox(
				(L"Module not found in factory: " + replacementModuleType).c_str(),
				L"ReplaceModule failed", MB_OK);
			return new_module;
		}

		// can't put io mod in master container
		if( Container() == nullptr && (moduleType->GetFlags() & CF_IO_MOD) )
		{
			Application()->SeMessageBox( (L"IO Modules only go inside a Container"), L"", MB_OK );
			return new_module;
		}

		new_module = dynamic_cast<CUG*>( CreateDocObject(replacementModuleType) );
	}

	if( new_module == 0 ) // prefab etc.
		return new_module;

	AddReplacementUg( old_module, new_module );
	new_module->OnReplace(old_module);
	new_module->Initialise(true); // pretend loaded from file so don't generate new handle and corrupt unique_id database (it should already be registered in OnReplace() ).

	vector< pair<int, PatchParameter_base*> > parameterHandles;

	// Copy Parameters.
	for(int idx = 0;; ++idx)
	{
		PatchParameter_base* fromParameter = get_patch_manager()->GetParameter( old_module, idx );
		if( fromParameter )
		{
			PatchParameter_base* toParameter = get_patch_manager()->GetParameter( new_module, idx );

#if defined(_DEBUG)
			if (L"chunk" == fromParameter->GetName())
			{
				int idx2 = 0;
				while (true)
				{
					auto param = get_patch_manager()->GetParameter(new_module, idx2);
					if (!param)
						break;

					if (L"chunk" == param->GetName())
					{
						toParameter = param;
						break;
					}
					++idx2;
				}
			}
#endif

			if( toParameter )
			{
				int fromDt, toDt;
				fromParameter->GetDatatype(FT_VALUE, &fromDt);
				toParameter->GetDatatype(FT_VALUE, &toDt);
				
				if( fromDt == toDt /* && fromParameter->GetName() == toParameter->GetName() && fromParameter->getPatchCount() == toParameter->getPatchCount()*/ )
				{
					parameterHandles.push_back( pair<int, PatchParameter_base*>(fromParameter->Handle(), toParameter));
					const int voice = 0;

					// Metadata.
					const ParameterFieldType ft[] = { FT_SHORT_NAME, FT_IGNORE_PROGRAM_CHANGE, FT_PRIVATE, FT_AUTOMATION, FT_AUTOMATION_SYSEX, FT_DEFAULT, FT_VST_PARAMETER_INDEX, FT_VST_DISPLAY_TYPE, FT_VST_DISPLAY_MIN, FT_VST_DISPLAY_MAX };
					for (auto field : ft)
					{
						const int patch = 0;
						toParameter->SetValue(fromParameter->GetValue(field, voice, patch), field, voice, patch);
					}

					switch (fromDt)
					{
					case DT_ENUM:
					{
						assert(false); // they are INT now.
						const ParameterFieldType field = FT_ENUM_LIST;
						const int patch = 0;
						toParameter->SetValue(fromParameter->GetValue(field, voice, patch), field, voice, patch);
					}
						break;

					case DT_TEXT:
					{
						const ParameterFieldType field = FT_FILE_EXTENSION;
						const int patch = 0;
						toParameter->SetValue(fromParameter->GetValue(field, voice, patch), field, voice, patch);
					}
						break;

					case DT_INT:
					{
						int metadatatype;
						fromParameter->GetDatatype(FT_ENUM_LIST, &metadatatype);
						// Can identify ENUM parameters by checking what type of metadata they have.
						if( metadatatype == DT_TEXT )
						{
							const ParameterFieldType field = FT_ENUM_LIST;
							const int patch = 0;
							toParameter->SetValue(fromParameter->GetValue(field, voice, patch), field, voice, patch);
							break; // non need to fall though to Max and Min settings.
						}

					}
					// deliberate fall-through.
					[[fallthrough]];

					case DT_DOUBLE:
					case DT_FLOAT:
					case DT_INT64:
					{
						const int patch = 0;
						toParameter->SetValue(fromParameter->GetValue(FT_RANGE_LO, voice, patch), FT_RANGE_LO, voice, patch);
						toParameter->SetValue(fromParameter->GetValue(FT_RANGE_HI, voice, patch), FT_RANGE_HI, voice, patch);
					}
					}

					// Presets
					if (toParameter->is_stateful())
					{
						int fromPatch = 0;
						int fromPatchInc = 0;
						if (fromParameter->getPatchCount() != 1)
						{
							fromPatchInc = 1;
						}

						for (int toPatch = 0; toPatch < toParameter->getPatchCount(); ++toPatch)
						{
							toParameter->SetValue(fromParameter->GetValue(FT_VALUE, voice, fromPatch), FT_VALUE, voice, toPatch);

							fromPatch += fromPatchInc;
						}
					}
				}
			}
		}
		else
		{
			break;
		}
	}

	if( replacementModuleType == L"Fixed Values" )
	{
		// Special case for slider when replaced by fixed-values.
		Ctl_Slider* slider = dynamic_cast<Ctl_Slider*>( old_module );

		if( slider )
		{
			IPlug* p = new_module->GetPlug(0);

			if( p->GetNumConnections() > 0 )
			{
				PatchParameter_base* parameter = get_patch_manager()->GetParameter( slider, 0 );
				double outputValue = parameter->GetValueReal(FT_VALUE);
				wstring plugName = DoubleToString(outputValue);
				p->setName(plugName);
				p->SetDefault( DoubleToString(outputValue) );
			}
		}
	}

	// if we contained a patch manager, may need to transfer it's patch data
	// TODO get_patch_manager()->TransferPatchData();
	// WPF Hack. Adding lines to new container causes it to redraw (layout change) repeatedly,
	// this can leave wires screwed up (going to screen top-left).
	// this forces them to re-draw correctly.
//	VO_Notify( OM_LAYOUT_CHANGE2, new_module ); 

	old_module->OnDelete();

	if (!parameterHandles.empty())
	{
		for (auto pp : parameterHandles)
		{
			Document()->uniqueIdDatabase.Unregister(pp.second);
			pp.second->setHandle(pp.first);
			Document()->uniqueIdDatabase.Register(pp.second);
		}
	}

	// Show module on Structure view.
	VO_Notify(OM_ADD_CHILD, new_module);

	// Show lines on Structure view.
	for (auto it = new_module->Plugs.begin(); it != new_module->Plugs.end(); ++it)
	{
		IPlug* p = *it;

		for (auto it2 = p->Connectors().begin(); it2 != p->Connectors().end(); ++it2)
		{
			CLine2* line = *it2;
			VO_Notify(OM_LAYOUT_CHANGE2, line);
		}
	}

	SetModifiedFlag();

	return new_module;
}

bool DocObgreater ( CDocOb* d1, CDocOb* d2 )
{
	const int rowSize = 50;
	gmpi::drawing::RectL r = d1->getViewObRect( CF_STRUCTURE_VIEW );
	int row1 = r.top / rowSize;
	int x1 = r.left;
	r = d2->getViewObRect( CF_STRUCTURE_VIEW );
	int row2 = r.top / rowSize;
	int x2 = r.left;

	// sort first by rows. top->bottom.
	if( row1 != row2 )
	{
		return row1 < row2;
	}

	// sort by collumns. left->right.
	if( x1 != x2 )
	{
		return x1 < x2;
	}

	// if all else fails sort by handle.
	return d1->Handle() < d2->Handle();
}

bool DocObless ( CDocOb* d1, CDocOb* d2 )
{
	return !DocObgreater(d1, d2);
}

CUG* CContainer::FindNextModule( CUG* old_module, bool downward )
{
	// first go down into container.
	CContainer*c = dynamic_cast<CContainer*>(old_module);
	if( c )
	{
		if( downward )
		{
			return c->FindNextModule(0);
		}
	}

	std::list<CUG*> sortedList2;

	it_doc_ob it(this);
	for( it.First() ; !it.IsDone() ; it.Next() )
	{
		CUG* u = dynamic_cast<CUG*>(it.CurrentItem());
		if( u )
		{
			sortedList2.push_back(u);
		}
	}

	sortedList2.sort( DocObgreater );

	// If no module specified. Get first.
	if( old_module == 0 )
	{
		if( !sortedList2.empty() )
		{
			return sortedList2.front();
		}
	}
	else
	{
		auto it2 = find( sortedList2.begin(), sortedList2.end(), old_module );
		if( it2 != sortedList2.end() )
		{
			it2++;
			if( it2 != sortedList2.end() )
			{
				return *it2;
			}
		}
	}

	if( Container() )
	{
		return Container()->FindNextModule( this, false );
	}

	return 0;
}

CUG* CContainer::FindPrevModule( CUG* old_module, bool downward )
{
	// first go down into container.
	CContainer*c = dynamic_cast<CContainer*>(old_module);
	if( c )
	{
		if( downward )
		{
			return c->FindPrevModule(0);
		}
	}

	std::list<CUG*> sortedList2;

	it_doc_ob it(this);
	for( it.First() ; !it.IsDone() ; it.Next() )
	{
		CUG* u = dynamic_cast<CUG*>(it.CurrentItem());
		if( u )
		{
			sortedList2.push_back(u);
		}
	}

	sortedList2.sort( DocObless );

	// If no module specified. Get first.
	if( old_module == 0 )
	{
		if( !sortedList2.empty() )
		{
			return sortedList2.front();
		}
	}
	else
	{
		auto it2 = find( sortedList2.begin(), sortedList2.end(), old_module );
		if( it2 != sortedList2.end() )
		{
			it2++;
			if( it2 != sortedList2.end() )
			{
				return *it2;
			}
		}
	}

	if( Container() )
	{
		return Container()->FindPrevModule( this, false );
	}

	return 0;
}


void CContainer::AdjustModuleTypePointer()
{
	CUG::AdjustModuleTypePointer();
	// point modules to module database (dropping temporary module_info s)
	// needed before upgrade as container->upgrade->onpatchselchanged() searches for patch select
	// by comparing module_info of ugs to one in database.
	it_doc_ob_recursive it(this);

	for( it.First() ; !it.IsDone() ; it.Next() )
	{
		CDocOb* d = it.CurrentItem();

		// When loaded from file, Module Info will not come from main module factory, but is loaded from file (in case module not avail locally).
		// switch module info pointer to module factory so temporary module info can be deleted.
		if( !d->getType()->getLoadedIntoDatabase() )
		{
			if( ! d->getType()->m_incompatible_with_current_module )
			{
				d->AdjustModuleTypePointer();
			}
		}
	}
}

void CContainer::setPanelWndOffset(gmpi::drawing::SizeL s)
{
	assert( s.width >= -99999 );
//	_RPTW4(_CRT_WARN, L"setPanelWndOffset %d,%d -> %d,%d\n", PanelWndOffset.width, PanelWndOffset.height, s.width, s.height );
	PanelWndOffset = s;
}

void CContainer::SendIntValueToDsp( const char* messageId, int value )
{
#ifdef _WIN32 // ignore this on mac, bigger fish
	assert(false); // ever used? (only refed by set midi channel)
#endif
    
#if 0
	auto& synthRuntime = SynthRuntime();

	if( synthRuntime.SynthRunning() )
	{
		// Inform DSP.
		my_msg_que_output_stream strm( synthRuntime.MessageQueToDsp(), Handle(), messageId);
		strm << (int) sizeof(value); // message length.
		strm << value;
		strm.Send();
	}
#endif
}

void CContainer::NotifyAllViews2( int lHint, void* pHint )
{
	VO_Notify( lHint, pHint );
}

int CContainer::GetOversamplingRate()
{
	return GetHostControlInt(HC_OVERSAMPLING_RATE);
}

int CContainer::GetHostControlInt(HostControls hostControl)
{
	auto p = get_patch_manager()->GetHostGeneratedParameter(hostControl, false, this);

	if (p == 0)
	{
		return 0;
	}

	return p->GetValueInt();
}

int CContainer::GetOversamplingFilterPoles()
{
	return (std::max)(5, GetHostControlInt(HC_OVERSAMPLING_FILTER));
}

void CContainer::setVoiceAllocationMode( int v )
{
	PatchParameter_base* p = get_patch_manager()->GetHostGeneratedParameter(HC_VOICE_ALLOCATION_MODE, true, this);
	p->SetValue(RawView(v));
}

int CContainer::getPolyphony()
{
	for( CContainer* c = this; c ; c = c->Container() )
	{
		PatchParameter_base* p = c->get_patch_manager()->GetHostGeneratedParameter( HC_POLYPHONY, false, c );
		if( p )
		{
			return p->GetValueInt();
		}
	}

	return defaultPolyphony;
}

int CContainer::getPolyphonyReserve()
{
	for( CContainer* c = this; c ; c = c->Container() )
	{
		PatchParameter_base* p = get_patch_manager()->GetHostGeneratedParameter( HC_POLYPHONY_VOICE_RESERVE, false, c );
		if( p )
		{
			return p->GetValueInt();
		}
	}

	return defaultPolyphonyReserve;
}

// forward declared; defined in UG2.cpp.
PatchParameter_base* getPinParameter(CContainer* container, InterfaceObject* p);

// A module-owned host-control parameter (oversampling, user shared-parameters) exists only
// because some module references it via a pin. When that module is deleted the parameter is
// orphaned - it used to linger in the Parameter Details window (still working) and only get
// dropped after a save + reload + save.
//
// Cull them in one document-wide pass: gather every module-owned host-control still wired to a
// live pin (matching the *specific parameter object* each pin resolves to, so two parameters of
// the same id can't be confused), then let each patch manager delete the rest. Call this on the
// document's top container so the whole module tree and every patch manager is covered.
//
// Structural host-controls (polyphony, voice-allocation, presets, patch-cables, ...) are owned
// by the container/document, not a module, so isModuleOwnedHostControl() excludes them.
void CContainer::RemoveOrphanedHostControls()
{
	// Two phases are required (not one fused walk): a module anywhere in the tree can reference a
	// shared-parameter held by an ancestor's patch manager, so the referenced set must be complete
	// before anything is deleted. One recursive pass gathers it (plus the managers to cull).
	std::set<PatchParameter_base*> referenced;
	std::vector<CPatchManager*> managers;
	gatherModuleOwnedHostControls(referenced, managers);

	for (auto pm : managers)
		pm->RemoveUnreferencedModuleOwnedHostControls(referenced);
}

void CContainer::gatherModuleOwnedHostControls(std::set<PatchParameter_base*>& referenced, std::vector<CPatchManager*>& managers)
{
	if (auto pm = my_patch_manager()) // only containers that own their patches hold parameters
		managers.push_back(pm);

	auto collect = [this, &referenced](module_info_pins_t& pins)
	{
		for (auto& it : pins)
		{
			auto pin = it.second;
			if (pin->isHostControlledPlug() && isModuleOwnedHostControl(pin->getHostConnect()))
			{
				if (auto param = getPinParameter(this, pin)) // resolves relative to this container
					referenced.insert(param);
			}
		}
	};

	for (auto docOb : BaseList)
	{
		auto module = dynamic_cast<CUG*>(docOb);
		if (!module)
			continue;

		collect(module->getType()->gui_plugs);
		collect(module->getType()->plugs);
		collect(module->getType()->controller_plugs);

		if (auto child = dynamic_cast<CContainer*>(module))
			child->gatherModuleOwnedHostControls(referenced, managers);
	}
}

CContainer* CContainer::getVoiceControlContainer()
{
	auto fromVoiceContainer = this;

	while (true)
	{
		if (fromVoiceContainer->GetNoteSource())
			break;

		auto parent = fromVoiceContainer->Container();

		if (parent)
			fromVoiceContainer = parent;
		else
			break;
	}

	return fromVoiceContainer;
}
