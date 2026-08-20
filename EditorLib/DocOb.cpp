#include "DocOb.h"
#include "Application.h"
#include "SynthEditDocBase.h"
#include "CContainer.h"
#include "resource.h"
#include "Module_Info3.h"
#include "Notify_msg.h"
#include "UgDatabase.h"
#include "se_file_format_version.h"
#include "SuspendDSP.h"
#include <set>
#include <sstream>
#include "SerializationHelper_XML.h"
#include "BundleInfo.h"
#ifdef _WIN32
#include "HtmlHelp.h"
#endif
#include "ModuleFactory_Editor.h"
#include "Hosting/message_queues.h"

using namespace gmpi::hosting;
using namespace std;

int CDocOb::m_loading_version = FILE_FORMAT_VERSION_NUM;

bool CDocOb::serialise_copy_mode = false;
bool CDocOb::serialise_all_mode = false;
CContainer* CDocOb::m_copy_container;

CDocOb::CDocOb( Module_Info* p_type ) :
	m_selected(false)
	,m_type(p_type)
	,m_container(0)
	,m_error_flags(0)
{
}

bool CDocOb::OnAdded(CContainer* container)
{
	if( getType()->UniqueId() == L"SE Patch Automator" && Container() == nullptr) // special case for "Main' container. It already has PS so won't automatically update views
	{
		container->OnHasPatchSelectionChanged();
	}
	return true;
}

void CDocOb::OnRemoved(CContainer* container)
{
	if (getType()->UniqueId() == L"SE Patch Automator" && Container() == nullptr)// special case for "Main' container. It already has PS so won't automatically update views
	{
		container->OnHasPatchSelectionChanged();
	}
}

//why?, why not just delete it?, because Container() was stored in CUG(), not accessable from destructor (fixed now)
void CDocOb::OnDelete()
{
	Application()->NotifyFast(OM_SHOW_PROPERTIES, nullptr);

	const auto handle = Handle();
	auto safeDocument = Document(); // need pointer to doc *after* delete this
	safeDocument->SetModified();

	if( Container() )
		Container()->Remove(this);

	delete this;

	safeDocument->uniqueIdDatabase.Unregister(handle);
}

// provides the currently loading file version to a serialise routine
// needs to be sta-tic so can be accessed by serialise without relying on pointers to containers
int CDocOb::LoadingVersion()
{
	return m_loading_version;
}

void CDocOb::SetModifiedFlag()
{
	if( Document() ) // is null during doc creation
	{
		Document()->SetModified();
	}
}

bool CDocOb::EditEnabled()
{
	return Container()->EditEnabled();
}

void CDocOb::Locate()
{
	SetSelected(true, true);
	auto r = getViewObRect(CF_STRUCTURE_VIEW);
	{
		// Center the view on the object.
		const gmpi::drawing::Point newCenter = {
			(r.left + r.right) * 0.5f,
			(r.top  + r.bottom) * 0.5f
		};
		Container()->SetViewCenter(CF_STRUCTURE_VIEW, newCenter);
	}

	Document()->OpenView(Container(), CF_STRUCTURE_VIEW);

	OnProperty(); // update the properties pane
}

void CDocOb::SetSelected(bool p_state, bool clear_selection )
{
	// first clear other selected objects
	// important to do this first for control_obs, as if we un-select them, we may indirectly un-select this
	if( clear_selection && Container() ) // main view has null container
	{
		Container()->setAllSelected(false, this );
	}

	// 2nd select this object
	if( p_state != GetSelected() ) // prevent infinite notify loops between panel and struct obs
	{
		m_selected = p_state;

		// Notify Presenters.
		if (Container()) // main view has null container
		{
			Container()->VO_Notify(OM_ONCHANGE_CHILD_SELECTED, (void*)this);
		}
	}
}

// this still relies on Type !!!
// should use a pointer to a description object, pointer would be init during contruction/serialisation
// (could store type desc string "osc" (not name, might change). !!!
int CDocOb::GetFlags()
{
	assert( getType() );
	return getType()->GetFlags();
}

// objects are included in copy/paste if tagged, or if container is tagged.
// serialise_all_mode forces every descendant of m_copy_container to be tagged (whole-document prefab save).
bool CDocOb::IsCopyTagged()
{
	if( Container() == m_copy_container && (GetSelected() || serialise_all_mode) )
		return true;

	assert( Container() != nullptr );
	return Container()->IsCopyTagged();
}

void CDocOb::OnDspMsg(int p_msg_id, gmpi::hosting::my_input_stream& p_stream)
{
}

void CDocOb::SetViewPos(int p_type, gmpi::drawing::RectL p_pos)
{
}

gmpi::drawing::RectL CDocOb::GetViewPos(int p_type)
{
	return {}; // empty, signifies size to child object
}

void CDocOb::OnMenuCommand( int p_view_type, uint32_t p_command_id, gmpi::drawing::PointL mouse_pos )
{
	switch( p_command_id )
	{
	case ID_DELETE_MODULE:
	{
		SuspendDSP x(Document()->Application());
		OnDelete();
	}
	break;

	case POPUP_MENU_UG_HELP:
		OnHelp();
		break;

	case POPUP_MENU_TO_PREFAB:
		Container()->OnEditToPrefab();
		break;

	case POPUP_MENU_ABOUT:
		DoAboutBox();
		break;
	};
}

void CDocOb::MoveTo(gmpi::drawing::PointL p_point, int p_view_type)
{
	auto r = getViewObRect( p_view_type );
	const auto dx = p_point.x - r.left;
	const auto dy = p_point.y - r.top;
	r.left += dx;
	r.top += dy;
	r.right += dx;
	r.bottom += dy;
	setViewObRect( p_view_type, r );
}

void CDocOb::OnProperty()
{
	Document()->Application()->NotifyFast(OM_SHOW_PROPERTIES, this);
}

void CDocOb::Export(Json::Value& module_json, ExportFormatType targetType)
{
	module_json["type"] = WStringToUtf8(getType()->UniqueId());
	module_json["handle"] = Handle();

	if (SAT_SYNTHEDIT_GUI_PANEL == targetType || SAT_SYNTHEDIT_GUI_STRUCT == targetType)
	{
		module_json["selected"] = GetSelected();
	}
}

void CDocOb::Export(tinyxml2::XMLElement* moduleElement, ExportFormatType targetType)
{
	moduleElement->SetAttribute("type", WStringToUtf8(getType()->UniqueId()).c_str());

	XmlSaveHelper helper(moduleElement);
	SerialiseC(helper); // handle

	// Do this with flags
	if (SAT_SYNTHEDIT_GUI_PANEL == targetType || SAT_SYNTHEDIT_GUI_STRUCT == targetType)
	{
		moduleElement->SetAttribute("selected", GetSelected());
	}
}

void CDocOb::Import(std::map<int32_t, CUG*>& uniqueIds, tinyxml2::XMLElement* moduleElement, ExportFormatType targetType)
{
	// about to overwrite handle. ensure it's not set.
	assert(!handleIsSet());

	XmlLoadHelper helper(moduleElement);
	SerialiseC(helper); // handle

//Document()->uniqueIdDatabase.Register(this);

	if (getType() == nullptr) // caller is usually able to setType() already.
	{
		const char* typeString = nullptr;
		moduleElement->QueryStringAttribute("type", &typeString);

		auto typeStringW = Utf8ToWstring(typeString);
		auto type = ModuleFactory()->GetById(typeStringW);
		assert(type);
		setType(type);
	}
}

void CDocOb::OnHelp()
{
	std::wstring help_url;
	auto sdk3mod = dynamic_cast<Module_Info3_base*>(getType());

	if( sdk3mod )
	{
		// show either help-file or SDK info (would be nice to have both available)
		help_url = getType()->GetHelpUrl();

		if( !help_url.empty() )
		{
			if( Lowercase(Left( help_url, 4 )) != L"http" )	// local file?
			{
				// cope with internal links in chm
				std::wstring baseFilename = help_url;
				std::wstring internalLink;
				auto p = help_url.find( L"::" ); // chm page?

				if( p == string::npos )
				{
					p = help_url.find( L"#" ); // internal anchor?
				}

				if( p != string::npos )
				{
					internalLink = Right( baseFilename, baseFilename.size() - p );
					baseFilename = Left(baseFilename, p );
				}

				if( baseFilename == L"synthedit.chm" )
				{
					baseFilename = GetHomeDir() + baseFilename;
				}
				else
				{
					auto factoryModulesHelpPath = BundleInfo::instance()->getSemFolder() + L"help\\";
					auto helpFilename = combinePathAndFile(factoryModulesHelpPath, baseFilename);

					if (!FileExists(helpFilename))
					{
						// 3rd-party SEM folder.
						helpFilename = GetApp()->ResolveFilename(L"help\\" + baseFilename, L"sem");
					}

					baseFilename = helpFilename;
				}

				if( !FileExists(baseFilename) )
				{
					Application()->SeMessageBox( L"No Help Available.", L"", 0x30 /*MB_OK|MB_ICONINFORMATION*/ );
					return;
				}

				help_url = baseFilename + internalLink;
			}

			OpenWebPage(help_url);
		}
	}
	else
	{
		std::unordered_map<std::wstring, std::wstring> corrections = {
			{L"Feedback - Volts", L"Feedback delayed"},
			{L"Feedback - Float", L"Feedback delayed"},
			{L"Feedback - Int",   L"Feedback delayed"},
			{L"Feedback - Text",  L"Feedback delayed"},
			{L"Feedback - Double", L"Feedback delayed"},
			{L"Feedback - Bool",  L"Feedback delayed"},
			{L"Feedback - Int64", L"Feedback delayed"},
			{L"Feedback - Blob",  L"Feedback delayed"},
			{L"Feedback - MIDI",  L"Feedback delayed"},
		};

		std::wstring clean_name = ::GetName(getType());

		auto it = corrections.find(clean_name);
		if(it != corrections.end())
		{
			clean_name = (*it).second;
		}

		replacein( clean_name,  (L" "), (L"_") );
		replacein( clean_name,  (L">"), (L"_") );
		replacein( clean_name,  (L"("), (L"") );
		replacein( clean_name,  (L")"), (L"") );
		help_url = (L"mdl_") + clean_name + (L".htm");
#ifdef _WIN32
		GetApp()->DoHelp(help_url, HH_DISPLAY_TOC);
#endif
	}

	// deliberatly not passing window to avoid bug on help close
	//	HWND h = Html Help( nullptr, help_url, HH_DISPLAY_TOPIC, 0);
}

void CDocOb::DoAboutBox()
{
	const wchar_t* sdk = L"SDK2 (internal)";

	if (dynamic_cast<Module_Info3_base*>(getType()))
		sdk = L"SDK V3 (internal)";

	auto sdk3 = dynamic_cast<Module_Info3*>(getType());
	if (sdk3)
	{
		sdk = L"SDK3";

		if(sdk3->Filename().find(L".gmpi") != std::wstring::npos || sdk3->Filename().find(L".GMPI") != std::wstring::npos)
			sdk = L"GMPI (SDK4)";

//		if(sdk3->Filename().find(L".sem") != std::wstring::npos || sdk3->Filename().find(L".sem") != std::wstring::npos)
//			sdk = L"SDK3";
	}

	std::wostringstream oss;

	oss << L"Type: " << getType()->UniqueId() << L"\n";
	oss << L"Handle: " << Handle() << L"\n";
	oss << sdk;

	if(sdk3)
		oss << L"\nFile: " << sdk3->Filename();

	Application()->SeMessageBox( oss.str().c_str(), L"", 0x30 /*MB_OK|MB_ICONINFORMATION*/ );
}

void CDocOb::Highlight(int highlightType)
{
	const auto before = m_error_flags;

	if(highlightType < 0)
		m_error_flags &= highlightType; // clear the flag
	else
		m_error_flags |= highlightType; // set the flag

	if(before == m_error_flags) // avoid expensive redraws.
		return; // no change

	Container()->VO_Notify(OM_ONCHANGE_CHILD_COLOUR, (void*)this);
}

void CDocOb::MoveFront()
{
	Container()->MoveChildToFront(this);
}

CSynthEditDocBase* CDocOb::Document()
{
	if( Container() )
	{
		return Container()->Document();
	}

	return {};
}

ApplicationBase* CDocOb::GetApp() { return Document()->Application(); }

CPatchManager* CDocOb::get_patch_manager()
{
	return Container()->get_patch_manager();
}

void CDocOb::SetParentPointersRecursive( CContainer* p_container )
{
	SetContainer(p_container); // allow CContainer to override
}

bool CDocOb::IsChildOf(CContainer* p_container)
{
	if( Container() == p_container )
		return true;

	if( Container() )
	{
		return Container()->IsChildOf(p_container);
	}

	return false;
}

void CDocOb::RegisterHandles( bool pasteMode )
{
	Document()->uniqueIdDatabase.Register(this);
}

void CDocOb::Initialise(bool loaded_from_file)
{
	assert( ( Container() != 0 || dynamic_cast<CContainer*>(this) != 0 ) && "Must call SetContainer() before Initialise()" ); // so can access document.

	if( loaded_from_file )
	{
		if (getType()->m_incompatible_with_current_module)
		{
			Document()->m_upgrade_replace_modules.push_back(dynamic_cast<CUG*>(this));
		}
		else
		{
			// called right after constructor (except during load from disk)
			// When loaded from file, Module Info will not come from main module factory, but is loaded from file (in case module not avail locally).
			// check we are pointed to correct module info
			assert(getType()->getLoadedIntoDatabase() || ModuleFactory()->GetById(getType()->UniqueId()) == 0);
		}
		assert( Document()->uniqueIdDatabase.HandleToObject( Handle() ) == this );
	}
	else
	{
		if (handleIsSet())
		{
			const auto lhandle = Handle();

			Document()->uniqueIdDatabase.Register(this);

			assert(lhandle == Handle()); // assuming handle did not clash.
		}
		else
		{
			Document()->uniqueIdDatabase.setHandleAutoGenerated(this);
		}
	}
}

void CDocOb::AdjustModuleTypePointer()
{
	// on loading, module type information is a cached copy from the project file.
	// update my pointer to the 'live' module data from local filesystem.
	auto liveModuleInfo = ModuleFactory()->GetById( getType()->UniqueId() );
	assert(liveModuleInfo);
	if (!liveModuleInfo)
		return; // module vanished from the database (e.g. re-scan without its library); keep the cached info.

//	if (liveModuleInfo->OnDemandLoad()) // Ensure VST2 Wrapper has full info. MIstake. If VST plugin not avail, still need to use database info.
	liveModuleInfo->OnDemandLoad();

	assert( liveModuleInfo->getLoadedIntoDatabase() ); // flag should be set if it's in database.
	setType( liveModuleInfo );
}

// either left-of right click. Adjust selection.
void CDocOb::OnClicked(int32_t flags)
{
	//bool new_state = GetKeyState(VK_CONTROL) >= 0 || !GetSelected();
	const bool new_state = 0 == (flags & gmpi_gui_api::GG_POINTER_KEY_CONTROL) || !GetSelected();

	// multiple selection won't be cleared if user drags selection, otherwise it will
//	clear_sel_on_key_up = GetKeyState(VK_SHIFT) >= 0 && GetKeyState(VK_CONTROL) >= 0;
	clear_sel_on_key_up = 0 == (flags & (gmpi_gui_api::GG_POINTER_KEY_SHIFT | gmpi_gui_api::GG_POINTER_KEY_CONTROL));

	// selection not cleared immediate if user making multiple selections
	bool clear_selection_immediate = !GetSelected() && clear_sel_on_key_up;

	// no need to clear selection later, if doing it now
	if( clear_selection_immediate )
		clear_sel_on_key_up = false;

	SetSelected( new_state, clear_selection_immediate );

	if( new_state )
	{
		OnProperty();
	}
}

ApplicationBase* CDocOb::Application() { return Document()->Application(); }

bool CDocOb::FlagRequiredModuleForExport(const std::wstring& moduleTypeId)
{
	if (auto* moduleType = ModuleFactory()->GetById(moduleTypeId); moduleType)
	{
		moduleType->SetSerialiseFlag();
		return true;
	}

	// A stock module every export depends on is missing from the module database, so the
	// plugin scan was incomplete - e.g. a cache rebuilt without libControls.gmpi. The
	// broken database is the problem to repair; the crash this used to be was only the
	// symptom. Report once per module, not per export - the panel re-exports on every
	// refresh.
	static std::set<std::wstring> reported;
	if (reported.insert(moduleTypeId).second)
	{
		// Document() is null on the detached container used for copy/paste, and
		// CDocOb::Application() dereferences it without checking.
		if (auto* doc = Document(); doc && doc->Application())
			doc->Application()->SeMessageBoxAsync(
				(L"Export failed: required module is missing from the module database:\n" + moduleTypeId +
					L"\nThe module scan is incomplete - this installation is broken. "
					L"Re-scan modules (or repair the modules folder), then export again.").c_str(),
				L"Export failed", MB_OK | MB_ICONSTOP);
	}

	return false;
}
