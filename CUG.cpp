#if defined( _WIN32 )
#undef  WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#undef  NOMINMAX
#define NOMINMAX
#include <Windows.h>
#include "Shlobj.h"
#include <shellapi.h>
#endif

#include <sstream>
#include <fstream>
#include <filesystem>
#include <assert.h>
#include <algorithm>
#include <chrono>

#include "resource.h"
#include "Dialogs_editor.h"
#include "CUG.h"
#include "SynthEditDocBase.h"
#include "Application.h"

#include "Notify_msg.h"
#include "Plug_decorator_sdk2.h"
#include "Module_Info3.h"

#include "UgDatabase.h"
#include "Plug4.h"
#include "PlugIO4.h"
#include "PatchManager.h"
#include "InterfaceObject.h"
#include "PatchParameter.h"
#include "SeAudioMaster.h"
#include "SuspendDSP.h"
#include "SkinMgr.h"
#include "../se_sdk3_hosting/GmpiResourceManager.h"
#include "it_plug_destinations.h"
#include "UgDebugInfo.h"
#include "./ISEAppManaged.h"
#include "modules/shared/voice_allocation_modes.h"

#include "tinyxml/tinyxml.h"
#include "HostControls.h"
#include "CContainer.h"
#include "SerializationHelper_XML.h"
#include "ModuleFactory_Editor.h"
#include "SafeMessageBox.h"
#include "mfc_emulation.h"
#include "BundleInfo.h"
#include "Hosting/message_queues.h"

using namespace gmpi::hosting;
using namespace std;

#ifdef _DEBUG
bool CUG::debugUpgradeInProgress = false;
#endif

std::wstring datatypeToString(int dt)
{
	const wchar_t* datatype = L"";

	switch( dt )
	{
	case DT_TEXT:
		datatype = L"string";
		break;

	case DT_STRING_UTF8:
		datatype = L"string_utf8";
		break;

	case DT_MIDI2:
		datatype = L"midi";
		break;

	case DT_DOUBLE:
		datatype = L"double";
		break;

	case DT_ENUM:
		datatype = L"enum";
		break;

	case DT_FSAMPLE:
	case DT_FLOAT:
		datatype = L"float";
		break;

	case DT_BOOL:
		datatype = L"bool";
		break;

	case DT_INT:
		datatype = L"int";
		break;

	case DT_BLOB:
		datatype = L"Blob";
		break;

	case DT_OBJECT:
		datatype = L"Object";
		break;

	default:
		assert(false); // unsupported type (add it here).
	};

	return std::wstring(datatype);
}

std::wstring datatypeToString2(int dt)
{
	switch (dt)
	{
	case DT_TEXT:
		return L"std::wstring";
		break;

	case DT_STRING_UTF8:
		return L"std::string";
		break;

	case DT_MIDI2:
		return L"midi";
		break;

	case DT_DOUBLE:
		return L"double";
		break;

	case DT_ENUM:
		return L"enum";
		break;

	case DT_FSAMPLE:
	case DT_FLOAT:
		return L"float";
		break;

	case DT_BOOL:
		return L"bool";
		break;

	case DT_INT:
		return L"int32_t";
		break;

	case DT_BLOB:
		return L"blob";
		break;

	case DT_OBJECT:
		return L"object";
		break;

	default:
		assert(false); // unsupported type (add it here).
	};

	return {};
}

CUG::CUG( Module_Info* p_type ) : CDocOb( p_type )
	, mute(false)
{
	if( p_type ) // 0 implies create from serialize
	{
		name = WStringToUtf8(::GetName(p_type));
		AddStandardPlugs();
	}

	name.addObserver([this]()
		{
			SetModifiedFlag();

			if (Container())
				Container()->NotifyFast(OM_REFRESH_PRESENTERS);

			// Fires for both CUG::SetName and direct property-browser edits, since
			// both paths assign to `name` which triggers this observer. Observers
			// of this CUG (e.g. EditorWindowHelper for open container tabs) can
			// react without subscribing to the coarse OM_REFRESH_PRESENTERS above.
			VO_Notify(OM_OBJECT_NAME_CHANGED);
		});
}

void CUG::AddStandardPlugs()
{
	// add gui object plugs
	for( auto it = getType()->gui_plugs.begin() ; it != getType()->gui_plugs.end() ; ++it )
	{
		InsertPlugSorted( MakePlug( ((*it).second )) ); // use CPlug4
	}

	// add plugs from audio ug
	for( auto it = getType()->plugs.begin() ; it != getType()->plugs.end() ; ++it )
	{
		InsertPlugSorted( MakePlug(((*it).second )) );
	}
}

void CUG::OnDelete()
{
	RemoveAllLines();

	// CLose the WPF control, before each plug deletion sends a layout-change event.
	NotifySafe( OM_DELETE );

//	assert( !GetApp()->SynthRunning() && "got to stop engine first!" );
	CDocOb::OnDelete(); // deletes this.
}

CUG::~CUG()
{
	//	delete ui ob;
	NotifySafe(OM_DELETE);

	// now delete remaining plugs
	while( !Plugs.empty() )
	{
		RemovePlug( Plugs.back() );
	}

	// back in VST because messia crashing on exit due to parameters holding pointer to deleted module.
	// Also in removeplug(), but it's possible to specify parameters without pins (not much use, but still need to delete them)
	if( Container()  )// master container can't accesss container. also during paste, temp_container don't have container
	{
		get_patch_manager()->UnRegister( this ); //Handle() );
	}
}

#ifdef _DEBUG
struct paramUpdate
{
	std::wstring name;
	std::string paramValues;
	std::string rangeHi;
	std::string rangeLo;
};
static std::map< int, paramUpdate > paramNames;
#endif

void CUG::Initialise(bool loaded_from_file )
{
	CDocOb::Initialise(loaded_from_file);
	// Initialize parameters
	parameter_description* pd;
	int i = 0;

	do
	{
		// ids may not be seqential due to some SDK2 modules having bogus patch-store flags on outputs.
		//pd = getType()->getParameterById( i );
		pd = getType()->getParameterByPosition( i ); // not efficient, mayby get iterator directly.

		if( pd )
		{
			get_patch_manager()->RegisterParameter(	this, *pd );
		}

		++i;
	}
	while( pd );

	for( auto it = Plugs.begin() ; it != Plugs.end() ; ++it )
	{
		(*it)->Initialise(loaded_from_file);
	}

	// Loaded (or pasted) files can lack the trailing unused 'spare' plug on autoduplicating modules:
	// CLine2::Import connects via AddConnectorQuiet, bypassing the OnNewConnection top-up.
	// see also: CUG::Import(), CContainer::Import()
	if( loaded_from_file && !Plugs.empty() )
	{
		const auto last = Plugs.back();
		if( last->autoDuplicate() && !last->isUnusedSpare() )
		{
			auto p = last->Duplicate();
			AddPlug( p );
			p->Initialise();
		}
	}
}

std::map<std::wstring, std::wstring> CUG::moduleUpgrades = {
	{ L"Inverter",				L"SynthEdit Inverter2" },
	{ L"Sample Player obsolete",L"Soundfont Player" },
	{ L"TexttoFloat2",			L"SE TextToFloat GUI" },
	{ L"SynthEdit Waveshaper3",	L"SE Waveshaper3 XP" },
	{ L"SynthEdit Scope3",		L"SE Scope3 XP" },
	{ L"Panel Group2",			L"SE Panel Group" },
	{ L"TextEntry3",			L"SE Text Entry4" },
	{ L"ListEntry3",			L"SE List Entry4" },
	{ L"Image2",				L"Image3" },
	{ L"ImageTinted",			L"SE ImageTinted XP" },
	{ L"SynthEdit Keyboard2",	L"SE Keyboard2" },
	{ L"SE Popup Menu",			L"SE Popup Menu XP" },
	{ L"SE Rectangle",			L"SE Rectangle XP" },
	{ L"Volt Meter",			L"SE Volt Meter" },
	{ L"SE Impulse Response",	L"SE Impulse Response2" },
	{ L"KeyBoard",				L"SE Keyboard (MIDI)" },
	{ L"SE Structure Group",	L"SE Structure Group2" },
};

void CUG::Upgrade(int from_version)
{
	CPatchManager* patchManager{};

	if( Container() )
	{
		patchManager = get_patch_manager();
	}

	// When replacing an internal module with a external one, need to explicitly replace it.
	if(const auto it = moduleUpgrades.find(getType()->UniqueId()) ; it != moduleUpgrades.end())
	{
		Document()->CancelCantLoad(getType()->UniqueId());

		auto replacement = (CUG*) Container()->AddReplacementUg(this, CreateDocObject(it->second));
		replacement->UpgradeFrom(this);
		OnDelete();
		return;
	}

	// replace old-style CPlug2 with new CPlug4 etc
	assert(from_version > 110000); // I have removed code from here for upgrading very old projects.

	// add gui object plugs
	InterfaceObject* i;

	for( auto it = getType()->gui_plugs.begin() ; it != getType()->gui_plugs.end() ; ++it )
	{
		assert( getType()->ModuleTechnology() != MT_SDK2 );
		i = (*it).second;
		int id = i->getPlugDescID();

		// is this plug already present?
		for( auto it2 = Plugs.begin() ; it2 != Plugs.end() ; it2++ )
		{
			if( (*it2)->getPlugDescID() == id && (*it2)->isUiPlug() )
			{
				i = 0;
				break;
			}
		}

		if( i )
		{
			// _RPT2(_CRT_WARN, " Adding new plug %d - '%s'\n", i->getPlugDescID(), DebugCStringToAscii( i->GetName() ) );
			InsertPlugSorted( MakePlug((i) ) );
		}
	}

	// All types. DSP pins. Changed to fix bug where Container not getting it's plugs fixed up.
	{
		// add plugs from audio ug
		for( auto it = getType()->plugs.begin() ; it != getType()->plugs.end() ; ++it )
		{
			i = (*it).second;
			int id = i->getPlugDescID();
//			assert( i->isUiPlug(0) == false ); // true only for SDK2+.

			// is this plug already present?
			for(auto it2 = Plugs.begin() ; it2 != Plugs.end() ; it2++ )
			{
//				if( (*it2)->getPlugDescID() == id && (*it2)->isUiPlug() == false )
				if( (*it2)->getPlugDescID() == id && (*it2)->isUiPlug() == i->isUiPlug() ) // new - attempt to fixup both types of plugs. (pre SDK3 gui plugs stored in same array as DSP plugs).
				{
					i = 0;
					break;
				}
			}

			if( i )
			{
				_RPTW2(_CRT_WARN, L" Adding new plug %d - '%s'\n", i->getPlugDescID(), i->GetName().c_str() );
				InsertPlugSorted( MakePlug( (i) ) );
			}
		}
	}

	// Since extra plugs added after Container I/O plug, need to re-sort older projects to move I/O plugs after that point.
	assert(from_version >= /*44*/ 119081);
	//{
	//	SortPlugs();
	//}

	// Transfer Polyphony setting to Patch Automator
	if( (GetFlags() & CF_NOTESOURCE) != 0 )
	{
		// Convert voice allocation over to Patch-Automator (provided no Poly-Mode or Mono-Mode pins in use).
		if( ::GetName(getType()) == L"MIDI to CV" )
		{
			if( GetPlug(L"Polyphony Mode")->GetDefault() != L"-1" && GetPlug(L"Polyphony Mode")->GetNumConnections() == 0 && GetPlug(L"Mono Note Priority")->GetNumConnections() == 0 && GetPlug(L"Retrigger")->GetNumConnections() == 0 )
			{
				int allocationMode = VA_POLY_SOFT;

				IPlug* p = GetPlug(L"Mono Mode");
				if( p && p->GetDefault() != L"1" ) // Poly?
				{
					p = GetPlug(L"Polyphony Mode");
					if( p )
					{
						allocationMode = StringToInt( p->GetDefault() );
					}
				}
				else
				{
					allocationMode = VA_MONO_DEPRECATED;
				}

				p = GetPlug(L"Mono Note Priority");
				if( p )
				{
					allocationMode += ( StringToInt( p->GetDefault() ) << 8 );
				}

				Container()->setVoiceAllocationMode( allocationMode );

				GetPlug(L"Polyphony Mode")->SetDefault(L"-1"); // ignore in future.
			}
		}
	}
}

CPlug4* CUG::MakePlug(InterfaceObject* i )
{
	if( i->isContainerIoPlug() )
	{
		return new CPlugIO4( this, i );
	}
	else
	{
		auto p = new CPlug4( this, i );

		if( p->isOldStyleGuiPlug() )
		{
			p->AddDecorator( new Plug_decorator_sdk2() );
		}

		return p;
	}
}

void CUG::AddPlug(CPlug4* p)
{
	InsertPlugSorted( p );

	VO_Notify( OM_PLUGS_CHANGE, this );
	if( Container() )
	{
		Container()->VO_Notify( OM_LAYOUT_CHANGE2, this );
	}
}

void CUG::RemovePlug(IPlug* p)
{
//	_RPTN(_CRT_WARN, "CUG[%d]::Remove Plug (%S.%S).\n", Handle(), GetName().c_str(), p->getName().c_str());

	if( Container() && // master container can't accesss container. also during paste, temp_container don't have container
	      p->isParameterPlug() )
	{
		get_patch_manager()->UnRegister( this, p->getParameterId());
	}

	p->OnRemove();

	assert( find( Plugs.begin(), Plugs.end(), p ) != Plugs.end() );// ensure deleting line hasn't indirectly deleted plug

	Plugs.erase( find( Plugs.begin(), Plugs.end(), p ) );

	// re-number autoduplicate plugs (SDK3).
	if( p->autoDuplicate() && getType()->ModuleTechnology() >= MT_SDK3 )
	{
		const int erasedId = p->getPlugDescID();

		for( auto it =  Plugs.rbegin() ; it !=  Plugs.rend() ; ++it )
		{
			IPlug* p2 = *it;

			if( p2->autoDuplicate() )
			{
				int id = p2->getPlugDescID();

				if( id <= erasedId )
				{
					break;
				}

				p2->setPlugDescID( id - 1 );
			}
		}
	}

	delete p;

	UpdatePlugEnumLists();

	VO_Notify( OM_PLUGS_CHANGE, this );
}

void CUG::OnPlugSetName(IPlug* p)
{
	/* no, plug binding to use affectsmeasure for this.
		if( Container() )
		{
			Container()->VO_N otify( OM_LAYOUT_CHANGE2, this ); // resize object
		}
	*/
	UpdatePlugEnumLists();
}

#if defined( _DEBUG )
void CUG::DebugPrintPlugName( IPlug* p )
{
	_RPTW2(_CRT_WARN, L"%s.%s", p->UG()->GetName().c_str(), p->getName().c_str() );
}
#endif

void CUG::OnDownstreamPlugChange(IPlug* p_my_plug, IPlug* p_downstream_plug, int p_msg_id)
{
	switch( p_msg_id )
	{
	case OM_DOWNSTREAM_PLUG_CONNECT2:
	{
		if( p_my_plug->autoConfigureParameter() )
		{
			// ignore connections to un-connected IO plugs.
			CPlugIO4* ioPlug = dynamic_cast<CPlugIO4*>( p_downstream_plug );

			if( ioPlug )
			{
				if( ioPlug->GetTiedTo() == 0 )
				{
					return;
				}

				// check out IO plug's destinations.
				it_plug_destinations it( ioPlug->GetTiedTo() );
				it.First();

				if( it.IsDone() )
					return;
			}

			// determin if plug making first connection (not counting un-connected IO plugs).
			// will fail if connection finds more than 1 downstream plug.
			IPlug* firstTimeConnection = 0;
			// iterate destination plugs
			it_plug_destinations it( p_my_plug );

			for( it.First(); !it.IsDone() ; it.Next() )
			{
				// if more than one connected plug, this aint the first connection.
				if( firstTimeConnection )
				{
					firstTimeConnection = 0;
					break;
				}

				firstTimeConnection = it.CurrentItem();
			}

			if( firstTimeConnection )
			{
				doAutoConfigureParameter( p_my_plug, firstTimeConnection );
			}
		}
	}
	break;

	case OM_UPSTREAM_PLUG_CONNECT:
	{
		// FOR GUI plugs...
		// NOTE: this is an input, therefore there is ultimatly only one upstream plug.
		it_plug_destinations it( p_my_plug );
		it.First();

		if( it.IsDone() ) // connected only to IO Mod.  Nothing to do.
		{
			return;
		}

#if defined( _DEBUG )
		IPlug* other_plug = it.CurrentItem();

		assert(!p_my_plug->isOldStyleGuiPlug()); // all this shit should be gone.
		assert(!other_plug->isOldStyleGuiPlug());
		assert( p_my_plug->isUiPlug() && p_my_plug->GetDirection() == DR_CNTRL && dynamic_cast<CPlugIO4*>(p_my_plug) == 0 && dynamic_cast<CPlugIO4*>(other_plug) == 0 ); // control (INPUT) has only one possible connection. easy to disconnect
		assert( other_plug != p_my_plug );
		
		// new-style GUI connections.  See also CContainer::OnViewCreate()
		++it;
		assert( (it.IsDone() || debugUpgradeInProgress) && "only sposed to be one destination. (Unless Containerizing or Replacing module during upgrade)." ); // assuming only one possible source plug.
#endif
	}
	break;

	case OM_UPSTREAM_PLUG_DISCONNECT:
	{
		if( p_my_plug->isUiPlug() )
		{
			/*
			#if defined( _DEBUG )
				DebugPrintPlugName( p_my_plug );
				_RPTW0(_CRT_WARN, L" - OM_UPSTREAM_PLUG_DISCONNECT\n" );
			#endif
			*/
			p_my_plug->OnUiDisconnect();
		}
	}
	break;

	case OM_DOWNSTREAM_PLUG_ENUM_CHANGE:
	{
		// see also onnewconnection.
		if( p_my_plug->autoConfigureParameter() )
		{
			IPlug* to = p_downstream_plug;

			if( to == 0 )
				return;

			// get first patch parameter
			IPlug* firstParameterPlug = 0;

			for( auto it = Plugs.begin() ; it != Plugs.end() ; ++it )
			{
				IPlug* p = *it;

				if( p->isParameterPlug() )
				{
					firstParameterPlug = p;
					break;
				}
			}

			if( firstParameterPlug == 0 )
				return;

			PatchParameter_base* patch_param = Container()->get_patch_manager()->GetParameter( this, firstParameterPlug->getParameterId() );
			if( patch_param )
			{
				if (p_my_plug->getDatatype() == DT_ENUM) // We may be connected via an implicit converter.
				{
					// Enum List
					std::wstring enumList = (to->getDefaultEnumList());
					patch_param->SetValue(RawView(enumList), FT_ENUM_LIST);
					patch_param->UpdateGui(); // reflect change to current patch.
				}
			}
			else
			{
				// Almost ALWAYS exists, except when replacing a List-Entry with a snapshot list entry.
				// Parameter should still end up initialised OK.
				_RPTW0(_CRT_WARN, L"!!!OM_DOWNSTREAM_PLUG_ENUM_CHANGE patch_param NULL!!!!\n" );
			}
		}
	}
	break;
	};
}

PatchParameter_base* CUG::getFirstPatchParam()
{
	// get first patch parameter
	for( auto it = Plugs.begin() ; it != Plugs.end() ; ++it )
	{
		IPlug* p = *it;

		if( p->isParameterPlug() )
		{
			return Container()->get_patch_manager()->GetParameter( this, p->getParameterId() );
		}
	}

	return 0;
}

void CUG::doAutoConfigureParameter( IPlug* from, IPlug* to )
{
	PatchParameter_base* patch_param = getFirstPatchParam();

	if( patch_param == 0 ) // should only ever be zero when upgrading ui_param_float_in.
		return;

	// only reset the parameter if this is the first connection ever, otherwise we wipe out all presets anytime we temporarily disconect/reconnect a wire.
	const auto pd = getType()->getParameterById(patch_param->ModuleParameterId());
	if (!pd)
		return;

	int parameterDatatype = -1;
	patch_param->GetDatatype( FT_VALUE, &parameterDatatype );

	// name or default already set? A rough indication that this patch-mem has already been configured to some pin.
	const auto rawDefault = patch_param->GetValue(FT_DEFAULT);
	const auto rawDefaultString = uniformDefaultString(Utf8ToWstring(RawToUtf8B(parameterDatatype, rawDefault.data(), rawDefault.size())), (EPlugDataType) parameterDatatype);
	if (
		pd->name != patch_param->GetName() ||
		pd->defaultValue != rawDefaultString
		)
		return;

	// Name.
	std::wstring name = to->UG()->GetName() + std::wstring(L" ") + to->getName();
	patch_param->SetValue(RawView(name), FT_SHORT_NAME);

	// Default and Metadata.
	patch_param->setDefault( to->GetDefault() );

	switch(parameterDatatype)
	{
	case DT_FSAMPLE:
	case DT_FLOAT:
	{
		// Range.
		sRange r = to->GetDefaultRange();
		float rangeHi = (float) r.MaxVal;
		float rangeLo = (float) r.MinVal;
		patch_param->SetValue(RawView(rangeHi), FT_RANGE_HI);
		patch_param->SetValue(RawView(rangeLo), FT_RANGE_LO);
	}
	break;

	case DT_TEXT:
	case DT_STRING_UTF8:
	{
		// Default.
		// File extension.
		std::wstring fileExtension = ( to->getFileExt() );
		patch_param->SetValue(RawView(fileExtension), FT_FILE_EXTENSION);
	}
	break;

	case DT_ENUM:
	{
		// Enum List
		std::wstring enumList = ( to->getDefaultEnumList() );
		patch_param->SetValue(RawView(enumList), FT_ENUM_LIST);
	}
	break;

	case DT_INT:
	{
		int metadatatype{};
		patch_param->GetDatatype(FT_ENUM_LIST, &metadatatype);
		// Can identify ENUM parameters by checking what type of metadata they have.
		if (metadatatype == DT_TEXT)
		{
			const auto enumlist = to->getDefaultEnumList();
			patch_param->SetValue(RawView(enumlist), FT_ENUM_LIST);
		}
		else
		{
			// Range.
			sRange r = to->GetDefaultRange();
			int rangeHi = (int)r.MaxVal;
			int rangeLo = (int)r.MinVal;
			patch_param->SetValue(RawView(rangeHi), FT_RANGE_HI);
			patch_param->SetValue(RawView(rangeLo), FT_RANGE_LO);
		}
	}
	break;

	case DT_BOOL:
	case DT_BLOB:
		break;

	default:
		assert( false && "TODO" );
	};

	patch_param->setIgnoreProgramChange( to->info()->connectedControlsIgnorePatchChange() );

	patch_param->InitializePatchMemory(true);

	patch_param->UpdateGui(); // reflect change to current patch.

	patch_param->UpdateGui( FT_NORMALIZED ); // and normalized.
}

void CUG::UpdatePlugEnumLists()
{
	// update any enum list relying on plug names
	for( auto it = Plugs.begin() ; it != Plugs.end() ; ++it )
	{
		IPlug* p = *it;

		if( p->UsesAutoEnumList() )
		{
			// plug has changed name
			// need to update enum list
			if( !p->isUiPlug()  )
			{
				if( p->Connectors().empty() )
				{
					// Update PlugViewModel.
					p->Notify_GUI( OM_ENUM_LIST_CHANGE, 0 );
				}
				else
				{
					p->PropogateBack( p, OM_DOWNSTREAM_PLUG_ENUM_CHANGE );
				}
			}
		}
	}
}

void CUG::Export(Json::Value& module_element, ExportFormatType targetType)
{
	assert(
		targetType == SAT_CADMIUM_VIEW ||
//		targetType == SAT_SYNTHEDIT_DOCUMENT ||
		targetType == SAT_SUBCONTROLS_GUI ||
		targetType == SAT_SYNTHEDIT_GUI_STRUCT ||
		targetType == SAT_SYNTHEDIT_GUI_PANEL
	); // else update if conditions.

	bool hasGui = (GetFlags() & CF_PANEL_VIEW) != 0;
	
	if ((SAT_SYNTHEDIT_GUI_PANEL == targetType || SAT_SUBCONTROLS_GUI == targetType) && !hasGui)
	{
		return;
	}

	auto moduleInfo = getType();
	auto oldType = moduleInfo->UniqueId();

	bool hasGuiSubstitute = oldType == L"List Entry" || oldType == L"Slider" || oldType == L"Text Entry"; // special cases.

	if(!moduleInfo)
	{
		return;
	}

	bool isImbeddedView = false;
	if (moduleInfo->UniqueId() == L"Container")
	{
		auto container = dynamic_cast<CContainer*>(this);
		isImbeddedView = container->IsImbeddedView(CF_PANEL_VIEW);
	}

	// A module has a drawable panel presence if it's an imbedded (container) view, a
	// registered GUI module (legacy XP window OR a modern SDK3/GMPI drawing client that
	// has no SDK3 window), or one of the legacy substitutes. Computed once so the export
	// gate here and the panel-rect serialisation further down stay in sync: previously the
	// rect test (isVisible) omitted the SDK3/GMPI-with-no-window case, so GMPI controls that
	// didn't declare graphicsApi (window type NONE) were exported with no pl/pt/pr/pb rect,
	// collapsing to the 100x100 default in hosts with no live data model (e.g. VST3).
	const bool hasDxGui = isImbeddedView
		|| (moduleInfo->m_gui_registered && (moduleInfo->getWindowType() == MP_WINDOW_TYPE_XP || (moduleInfo->getWindowType() == MP_WINDOW_TYPE_NONE && moduleInfo->ModuleTechnology() >= MT_SDK3)))
		|| hasGuiSubstitute;

	if (targetType == SAT_SUBCONTROLS_GUI || targetType == SAT_SYNTHEDIT_GUI_PANEL)
	{
		if (!hasDxGui)
		{
			return;
		}
	}

	if (targetType == SAT_CADMIUM_VIEW)
	{
		bool hasCadmiumGui = /* ?isImbeddedView || */ moduleInfo->m_gui_registered && (moduleInfo->getWindowType() == MP_WINDOW_TYPE_CADMIUM);

		if (!hasCadmiumGui)
		{
			return;
		}
	}

	CDocOb::Export(module_element, targetType);

	// Flag Module-info as needing serializing to XML too.
	// For now, flag them all. (there is no built-in modules in SE Universal).
	if(targetType == SAT_SYNTHEDIT_DOCUMENT || moduleInfo->ModuleTechnology() >= MT_SDK3 ) // and EXTERNAL?
	{
		moduleInfo->SetSerialiseFlag();
	}

	// Structure Position
	if (targetType == SAT_SYNTHEDIT_DOCUMENT || targetType == SAT_SYNTHEDIT_GUI_STRUCT )
	{
		module_element["sl"] = m_struct_rect.left;
		module_element["st"] = m_struct_rect.top;
		module_element["sr"] = m_struct_rect.right;
		module_element["sb"] = m_struct_rect.bottom;
	}

	bool isStructureView = targetType == SAT_SYNTHEDIT_GUI_STRUCT;

	{
		// Emit the panel rect for anything with a drawable panel presence (see hasDxGui
		// above). Was gated on a narrower test that dropped SDK3/GMPI controls.
		if (hasDxGui)
		{
			auto r = getViewObRect(CF_PANEL_VIEW);

			module_element["pl"] = r.left;
			module_element["pt"] = r.top;
			module_element["pr"] = r.right;
			module_element["pb"] = r.bottom;

			// Really need a 3rd component of plugins - Design-time Editor so each plugin can handle export XML custom attributes itself.
			// or new concept "properties", like plugs but non-binable. fixed at runtime. rect would be one.

			// need title?
			if( false ) // don't really want to dirty the format with hacks like this.
			{
				IPlug* p = GetPlug(L"Show Title On Panel");

				bool has_header = false;
				bool has_title = false;

				if (p)
				{
					// show header area, even if blank' to preserve vertical spacing.
					has_header = true;
					// Show text if wanted.
					has_title = p->GetDefault().compare(L"1") == 0;
				}
				else
				{
					// Depends on each module.
					has_header = has_title = show_title_on_panel();
				}

				if (has_header)
				{
					if (has_title)
					{
						isStructureView = true;
					}
				}
			}
		}
	}

	if (isStructureView)
	{
		module_element["title"] = WStringToUtf8(GetName());
		module_element["muted"] = GetMute();
	}

	// Plugs.
	Json::Value pins_element(Json::arrayValue);
	bool hasAutocopyTotalPins = false;
	int totalPins = 0;

	int pinUniqueIdexpected = 0;
	int voiceContainerHandle = -1;
	for (auto p : Plugs)
	{
		// Also skip unused spares (some in scwartze have default?????).
		if (p->isUnusedSpare())
		{
			continue;
		}

		bool isDspPlug = !p->isUiPlug();

		// Skip non-relevant plugs.
		if ((targetType == SAT_SUBCONTROLS_GUI || targetType == SAT_SYNTHEDIT_GUI_PANEL) && isDspPlug)
		{
			continue;
		}

		bool isGuiPlug = p->isUiPlug();

		++totalPins;

		// Get adorners to provide plug input value, or nullptr if plug using defaults.
		Json::Value pin_element(Json::objectValue);

		// Plug decorators if any can contribute.
		p->Export(pin_element, targetType);

		const auto hostControl = p->getHostConnect();
		if (AttachesToVoiceContainer(hostControl))
		{
			if (voiceContainerHandle == -1)
			{
				// cache it to save the expense of recalculating it.
				voiceContainerHandle = Container()->getVoiceControlContainer()->Handle();
				module_element["VoiceContainer"] = voiceContainerHandle;
			}
		}

		// Enum output pins (need to know what the enum list is).
		if (p->getDatatype() == DT_ENUM && p->GetDirection() == DR_OUT)
		{
			auto enumlist = p->getDefaultEnumList();
			if (!enumlist.empty())
			{
				pin_element["EnumList"] = WStringToUtf8(enumlist);
			}
		}

		// I/O Plugs, Autoduplicating plugs.
#if _DEBUG
		if (p->isCustomisable())
		{
			// Theory: IO_CUSTOMISABLE are *always* IO_AUTODUPLICATE
			assert(p->autoDuplicate());
		}
#endif				
		//if (p->isRenamable() && !p->isUnusedSpare())
		//{
		//	pin_element["Name"] = WStringToUtf8(p->getName());
		//}
		
		if (p->autoDuplicate() && !p->isUnusedSpare())
		{
			CPlugIO4* pio = dynamic_cast<CPlugIO4*> (p);

			if (pio) //  Container or I/O Mod pins.
			{
				if(pio->isUiPlug() && (SAT_SUBCONTROLS_GUI == targetType || SAT_SYNTHEDIT_GUI_PANEL == targetType))
				{
					// Skip I/O plugs in subcontrols.
					// otherwise they all get assigned Id=0, then their default value is applied to pin 0 ("Show on Panel" legacy)
					// very confusing.

					// NOTE: This means that setting a default value on a I/O plug in a subcontrol will not work.
					// The fix would be to better identify these pins, perhaps via an index (but don't confuse with DSP pins which use "Idx" to identify them)
					continue;
				}

				pin_element["Direction"] = (int)p->GetDirection();
				pin_element["Datatype"] = (int)p->getDatatype();
				if(p->isUiPlug())
					pin_element["GuiPin"] = 1;

				// Serialise tied-to information. Which I/O plugs links to what Container plug.
				if (GetFlags() & CF_IO_MOD)
				{
					if (!pio->isUiPlug())
					{
						IPlug* tie_to_plug = pio->GetTiedTo();

						if (tie_to_plug != nullptr)
						{
							pin_element["TiedTo"] = tie_to_plug->UG()->Handle();
							pin_element["TiedToPinIdx"] = tie_to_plug->UG()->getRuntimePinIndex(tie_to_plug, targetType != SAT_SUBCONTROLS_GUI && targetType != SAT_SYNTHEDIT_GUI_PANEL);
						}
					}
				}

#if defined( _DEBUG )
				// comment = new TiXmlComment("I/O Plug");
#endif
			}
			else
			{
#if defined( _DEBUG )
				// comment = new TiXmlComment("Autoduplicating Plug");
#endif
				hasAutocopyTotalPins = true;

				// Need to know which plug we are duplicating. SKip the decorator that overrides the ID.
				IPlugDescriptionDecorator* i = dynamic_cast<IPlugDescriptionDecorator*>(p)->getPlugDescription();

				while (i->getPlugDescription())
				{
					i = i->getPlugDescription();
				}
				pin_element["AutoCopy"] = i->getPlugDescID(0);
				pin_element["Id"] = p->getPlugDescID();

				/*
				if (targetType == SAT_SYNTHEDIT_GUI_STRUCT)
				{
					if (p->isRenamable())
					{
						pin_element["Name"] = WStringToUtf8(p->getName());
					}
				}
				*/
			}
		}

		// Special-case: GUI input pin connected to IO/Mod, where the OUTER pin sets a default value.
		if (p->GetDirection() == DR_IN && p->isUiPlug() && p->GetNumConnections() != 0)
		{
			IPlug* defaultPin = p;

			// search backwards up signal path.
			do
			{
				auto from = dynamic_cast<CPlugIO4*>(defaultPin->ConnectedTo());
				defaultPin = {};

				if (!from)
					break;

				defaultPin = from->GetTiedTo();

				if (defaultPin->GetNumConnections() == 0)
				{
					// Found outer container input that sets default value.
					assert(defaultPin != p);
					assert(defaultPin->GetDirection() == DR_IN);
					assert(dynamic_cast<CPlugIO4*>(defaultPin));
					assert(defaultPin->GetNumConnections() == 0);

					pin_element["default"] = WStringToUtf8(defaultPin->GetDefault());

					// _RPTN(0, "Propogating Container pin default value '%S' to %S\n", defaultPin->GetDefault().c_str(), p->getName().c_str());

					break;
				}
			}while (true);
		}

		if (!pin_element.empty())
		{
//			if (targetType == SAT_SUBCONTROLS_GUI || targetType == SAT_SYNTHEDIT_GUI_PANEL)
			if(isGuiPlug)
			{
				// Attributes like Default value are accesed by unique-ID on GUI.
				int pinUniqueId = p->getPlugDescID();

				if (pinUniqueIdexpected != pinUniqueId) // pinUniqueId != 0) // default is zero.
				{
					pin_element["Id"] = pinUniqueId;
				}

				pinUniqueIdexpected = pinUniqueId;
			}
			else
			{
				// DSP pins are exported to draw module on structure view
				// Attributes like Default value are accessed by Index on DSP.
				int PinIdx = getRuntimePinIndex(p, targetType != SAT_SUBCONTROLS_GUI && targetType != SAT_SYNTHEDIT_GUI_PANEL);

// NO. need this to Identify DSP pin vs GUI, else can crash gui.				if (PinIdx != 0) // default is zero.
				{
					pin_element["Idx"] = PinIdx;
				}
			}

			pins_element.append(pin_element);

			++pinUniqueIdexpected;
		}
	}

	// Don't create plug element unless it has non-default settings.
	if (!pins_element.empty())
	{
		module_element["Pins"] = pins_element;

		if (hasAutocopyTotalPins)
		{
			module_element["PinCount"] = totalPins;
		}
	}
}

void CUG::Export(tinyxml2::XMLElement* moduleElement, ExportFormatType targetType)
{
	CDocOb::Export(moduleElement, targetType);

	auto doc = moduleElement->GetDocument();

	// plugs
	if (!Plugs.empty())
	{
		auto plugsX = doc->NewElement("plugs");
		int i = 0;
		int prevPinIndex = -1;
		auto xml = doc->NewElement("plug"); // create plug element in advance.
		for(auto p : Plugs)
		{
			p->Export(xml, targetType);
			if( xml->FirstAttribute() || p->isUnusedSpare()) // skip if xml is empty. Exception is Container 'spare' plug, which needs to be included regardless
			{
				if (prevPinIndex + 1 != i)
				{
					xml->SetAttribute("idx", i);
				}

				plugsX->LinkEndChild(xml);
				xml = doc->NewElement("plug"); // create next plug element in anticipation.
				prevPinIndex = i;
			}
			++i;
		}
		if(plugsX->FirstChild()) // skip if xml is empty.
		{
			moduleElement->LinkEndChild(plugsX);
		}
		else
		{
			doc->DeleteNode(plugsX);
		}

		// remove leftover plug element.
		if(xml)
		{
			doc->DeleteNode(xml);
		}
	}

	XmlSaveHelper helper(moduleElement);
	SerialiseC(helper);
}

void CUG::Import(std::map<int32_t, CUG*>& uniqueIds, tinyxml2::XMLElement* moduleElement, ExportFormatType targetType)
{
	CDocOb::Import(uniqueIds, moduleElement, targetType);

	uniqueIds[Handle()] = this;
	
	XmlLoadHelper helper(moduleElement);
	SerialiseC(helper);

	if (auto plugsElement = moduleElement->FirstChildElement("plugs"); plugsElement)
	{
		int i = 0;
		for (auto plugElement = plugsElement->FirstChildElement(); plugElement; plugElement = plugElement->NextSiblingElement())
		{
			assert(strcmp(plugElement->Value(), "plug") == 0);

			plugElement->QueryIntAttribute("idx", &i);

			// Autoduplicating pins.
			while(i >= Plugs.size())
			{
				assert( Plugs.back()->autoDuplicate() );

				auto p = Plugs.back()->Duplicate();
				p->SetUG(this);
				Plugs.push_back(p);
			}

			assert(i >= 0 && i < Plugs.size());

			Plugs[i]->Import(plugElement, targetType);
			++i;
		}
	}

	if (GetFlags() & CF_IO_MOD)
	{
		// some early files are missing the last 'spare' pin in the XML. If so add it.
		// see also: CUG::Import()
		auto last = dynamic_cast<CPlugIO4*>(Plugs.back());
		if (!last || last->GetTiedTo() != nullptr)
		{
			_RPT0(0, "IO Module MISSING Spare pin!!! Fixed Up.\n");
			Plugs.push_back(MakePlug((getType()->getPinDescriptionById(0)))); // 'spare' plug
			assert(Plugs.back()->isUnusedSpare());
		}
	}
}

TiXmlElement* CUG::ExportXml(TiXmlElement* XmlParent, ExportFormatType targetType )
{
	if(!doExport() || targetType == SAT_VST3_CONTROLERS)
		return 0;

	if (targetType == SAT_SUBCONTROLS_GUI)
	{
		// skip VST3 GUI XML if no GUI module.
		// Older types register GUI plugs from DSP. So also check hasGuiObject.
		bool hasGui = (GetFlags() & CF_PANEL_VIEW) != 0 || getType()->m_gui_registered;
		if (!hasGui)
		{
			return 0;
		}
	}
	else
	{
		// skip DSP XML if no DSP module.
		if (!getType()->hasDspModule() )
		{
			//_RPTW1(_CRT_WARN, L"XML skipped %s\n", getType()->GetName().c_str() );
			return 0;
		}
	}

	// Flag Module-info as needing serializing to XML too.
	if( getType()->ModuleTechnology() >= MT_SDK3 ) // and EXTERNAL?
	{
		getType()->SetSerialiseFlag();
	}

	TiXmlElement* module_element = new TiXmlElement( "Module" );

	if(cpu_meter && targetType == SAT_SYNTHEDIT_DSP)
	{
		module_element->SetAttribute( "Debugger", 1 );
	}

	module_element->SetAttribute( "Id", Handle() );
	auto moduleId = getType()->UniqueId();

	module_element->SetAttribute("Type", WStringToUtf8(moduleId).c_str());
/* compacter XML
#if defined( _DEBUG )

	if( GetName() != getType()->GetName() )
	{
		module_element->SetAttribute( "DebugName", WStringToUtf8( GetName() ) );
	}

#endif
*/
	XmlParent->LinkEndChild( module_element );

	// GUI position.
	if( targetType == SAT_SUBCONTROLS_GUI )
	{
		std::wstring uniqueId = getType( )->UniqueId( );
		bool isImage = uniqueId == L"Image2" || uniqueId == L"ImageTinted";
		bool hasRect = (getType()->getWindowType() != MP_WINDOW_TYPE_NONE) || isImage || uniqueId == L"TextEntry3" || uniqueId == L"Text Entry" || uniqueId == L"List Entry" || uniqueId == L"ListEntry3" || uniqueId == L"SE Popup Menu" || uniqueId == L"Slider";
		if (hasRect)
		{
			auto r = GetAbsolutePanelRect();

			int rectInset = 0;

			if (uniqueId == L"ListEntry3" || uniqueId == L"SE Popup Menu" || uniqueId == L"TextEntry3")
			{
				rectInset = 3; // pixels.
			}

			if (r.left != 0)
			{
				module_element->SetDoubleAttribute("Canvas.Left", r.left + rectInset);
			}
			if (r.top != 0)
			{
				module_element->SetDoubleAttribute("Canvas.Top", r.top + rectInset);
			}

			// Really need a 3rd component of plugins - Design-time Editor so each plugin can handle export XML custom attributes itself.
			// or new concept "properties", like plugs but non-binable. fixed at runtime. rect would be one.

			module_element->SetAttribute("Width", gmpi::drawing::getWidth(r) - 2 * rectInset);
			module_element->SetAttribute("Height", gmpi::drawing::getHeight(r) - 2 * rectInset);

			// need title?
			{
				IPlug* p = GetPlug(L"Show Title On Panel");

				bool has_header = false;
				bool has_title = false;

				if (p)
				{
					// show header area, even if blank' to preserve vertial spacing.
					has_header = true;
					// Show text if wanted.
					has_title = p->GetDefault().compare(L"1") == 0;
				}
				else
				{
					// Depends on each module.
					has_header = has_title = show_title_on_panel();
				}

				if (has_header)
				{
					string title;
					if (has_title)
					{
						title = WStringToUtf8(GetName());
					}
					module_element->SetAttribute("Title", title);
				}
			}
/*
			if (isImage)
			{
				// knob image.
				IPlug* p = GetPlug( _T( "Filename" ) );
				wstring imageNameW = p->getValue();
				( (CSynthEditDoc*) Document() )->vst3AddBitmap( imageNameW );
			}
*/
		}
	}


	// Plugs.
	TiXmlElement* plugsElement = 0;
	int pinUniqueIdexpected = 0;
	for( auto it = Plugs.begin(); it != Plugs.end() ; ++it )
	{
		IPlug* p = *it;

		// Also skip unused spares (some in scwartze have default?????).
		if( p->isUnusedSpare() )
		{
			continue;
		}

		// Skip non-relevant plugs.
		if( ( p->isUiPlug( ) != ( targetType == SAT_SUBCONTROLS_GUI ) ) )
		{
			continue;
		}

		// Get adorners to provide plug input value, or nullptr if plug using defaults.
		TiXmlElement* plugElement = 0;
		// Plug decorators if any can contribute.
		plugElement = p->ExportXml();

		// Enum output pins (need to know what the enum list is).
		if( p->getDatatype() == DT_ENUM && p->GetDirection() == DR_OUT )
		{
			auto enumlist = p->getDefaultEnumList();
			if (!enumlist.empty())
			{
				if( plugElement == 0 )
				{
					plugElement = new TiXmlElement( "Plug" );
				}

				plugElement->SetAttribute("EnumList", WStringToUtf8(enumlist));
			}
		}

		if( plugElement )
		{
			if (targetType == SAT_SUBCONTROLS_GUI)
			{
				// Attributes like Default value are accesed by unique-ID on GUI.
				int pinUniqueId = p->getPlugDescID();

				if (pinUniqueIdexpected != pinUniqueId ) // pinUniqueId != 0) // default is zero.
				{
					plugElement->SetAttribute("Id", pinUniqueId);
				}

				pinUniqueIdexpected = pinUniqueId;
			}
			else
			{
				// Attribute Default value is accesed by Index on DSP.
				int PinIdx = getRuntimePinIndex( p, targetType != SAT_SUBCONTROLS_GUI );

				if( PinIdx != 0 ) // default is zero.
				{
					plugElement->SetAttribute( "Idx", PinIdx );
				}
			}
		}

#if defined( _DEBUG )
		TiXmlComment* comment = 0;
#endif

		
		// I/O Plugs, Autoduplicating plugs.
#if defined( _DEBUG )
		if (p->isCustomisable())
		{
			// Theory: IO_CUSTOMISABLE is a synonym for IO_AUTODUPLICATE
			assert(p->autoDuplicate());
		}
#endif
		if( (/*p->isCustom isable() ||*/ p->autoDuplicate()) && !p->isUnusedSpare())
		{
			if( plugElement == 0 )
			{
				plugElement = new TiXmlElement( "Plug" );
			}

			CPlugIO4* pio = dynamic_cast<CPlugIO4*> ( p );

			if( pio ) //  Container or I/O Mod pins.
			{
				plugElement->SetAttribute( "Direction", (int) p->GetDirection() );
				plugElement->SetAttribute( "Datatype",  (int) p->getDatatype() );

				// Serialise tied-to information. Which I/O plugs links to what Container plug.
				if( GetFlags() & CF_IO_MOD )
				{
					if( !pio->isUiPlug() )
					{
						IPlug* tie_to_plug = pio->GetTiedTo();

						if( tie_to_plug )
						{
							plugElement->SetAttribute( "TiedTo", tie_to_plug->UG()->Handle() );
							plugElement->SetAttribute( "TiedToPinIdx", tie_to_plug->UG( )->getRuntimePinIndex( tie_to_plug, targetType != SAT_SUBCONTROLS_GUI ) );
						}
					}
				}
				else
				{
					// Containers
					// 
					// IO plug prefixed with an asterix ("*input") are Control Voltage pins and need less filtering in an oversampler.
					const auto name = p->getName();
					if (!name.empty() && name[0] == L'*')
					{
						plugElement->SetAttribute("CV", true);
					}
				}

#if defined( _DEBUG )
				// comment = new TiXmlComment("I/O Plug");
#endif
			}
			else
			{
#if defined( _DEBUG )
				// comment = new TiXmlComment("Autoduplicating Plug");
#endif
				// Need to know which plug we are duplicating. SKip the decorator that overrides the ID.
				auto i = dynamic_cast<IPlugDescriptionDecorator*>(p)->getPlugDescription();

				while( i->getPlugDescription() )
				{
					i = i->getPlugDescription();
				}
				plugElement->SetAttribute( "AutoCopy", i->getPlugDescID(0) );
				
				// !!! probably completely redundant.
				if (targetType != SAT_SYNTHEDIT_DSP)
				{
					plugElement->SetAttribute("Id", p->getPlugDescID());
				}
			}
		}

		if( plugElement )
		{
			if( plugsElement == 0 )
			{
				plugsElement = new TiXmlElement( "Plugs" );
			}

#if defined( _DEBUG )

			if(comment)
			{
				plugsElement->LinkEndChild( comment );
				comment = 0;
			}

#endif
			plugsElement->LinkEndChild( plugElement );

			++pinUniqueIdexpected;
		}
	}

	// Don't create plug element unless it has non-default settings.
	if( plugsElement )
	{
		module_element->LinkEndChild( plugsElement );
	}

	return module_element;
}

void CUG::AttachDebugger( void )
{
	if(cpu_meter)
	{
		cpu_meter = nullptr;
	}
	else
	{
		cpu_meter = std::make_unique<cpu_accumulator>();
	}

	// Notify Processor
	Document()->Application()->invalidateDsp();

	// Notify GUI
	Container()->VO_Notify(OM_CPU_UPDATE, this);
}

// Return the last plug in this UG
// only relevant for UG's that have a 'Spare' plug
CPlugIO4* CUG::GetSparePlug()
{
	for( auto it = Plugs.rbegin() ; !(it == Plugs.rend()) ; ++it )
	{
		IPlug* p = *it;

		if( p->isUnusedSpare() )
			return dynamic_cast<CPlugIO4*>(p);
	}

	assert(false);
	return nullptr;
}

std::wstring CUG::GetName()
{
	return Utf8ToWstring(name.get());
}

void CUG::SetName(const std::wstring& new_name)
{
	name = WStringToUtf8(new_name);
/*
	SetModifiedFlag();

	if(Container())
		Container()->NotifyFast(OM_REFRESH_PRESENTERS);
*/
}

std::wstring CUG::getPath()
{
	std::wstring fullpath;
	CContainer* c = Container();

	while( c ) //->Container() )
	{
		std::wstring temp( L"/" );
		temp.append( c->GetName() );
		fullpath.insert( 0, temp );
		c = c->Container();
	}

	fullpath.append( L"/" );
	fullpath.append( GetName() );
	return fullpath;
}

// add debug info to ug title,
// indicates which UGs are polyphonic, or for drums, which voice they are allocated to
void CUG::debugpoly()
{
#if 0
	if( generator == nullptr )
	{
		return;
	}

	std::wostringstream oss;

	if( generator->GetPolyphonic() )
	{
		if( generator->isPolyLast() )
		{
			//			new_title = L"*" + new_title;
			oss << L"*";
		}
		else
		{
			//new_title = L"+" + new_title;
			oss << L"+";
		}
	}

	//	std::wstring sort_order((L"?"));
	int so = generator->GetSortOrder();

	if( so >= 0 )
	{
		//		sort_order = IntToString(so);
		oss << so;
	}
	else
	{
		oss << L"?";
	}

	//	std::wstring new_title = GetName(); //Name;
	if( generator->pp_voice_num >= 0 )
	{
		//std::wstring voice_num;
		//voice_num.Format((L"v%d "), generator->pp_voice_num );
		//new_title = voice_num + new_title;
		oss << L"v" << generator->pp_voice_num << L" ";
	}

	oss << GetName();
	//	new_title = sort_order + new_title;
	wstring t = oss.str();
	VO_Notify( OM_NAME_CHANGE, &t );
#endif
}

IPlug* CUG::GetPlug(std::wstring name)
{
	for( auto it = Plugs.begin() ; it != Plugs.end() ; ++it )
	{
		if( (*it)->getName().compare(name) == 0 )
			return *it;
	}

	/*
	#ifdef _DEBUG

		_RPT1(_CRT_WARN, "\nCUG::GetPlug(%s) CAN'T FIND IT\n\nplugs...\n", name );
		pos = Plugs.GetHeadPosition();
		while( pos != nullptr )
		{
			_RPT1(_CRT_WARN, "%s\n", Plugs.GetNext(pos)->GetName() );
		}
	#endif*/
	//	assert(false); //can't find this plug by name
	return nullptr;
}

IPlug* CUG::GetPlug(int i)
{
	// assert() flags the bad index for the developer, but it compiles out under
	// NDEBUG (release builds are RelWithDebInfo -DNDEBUG), so it can't be the only
	// guard. Return nullptr rather than reading past the end of Plugs: callers that
	// already null-check recover cleanly, and the rest fault at a known point
	// instead of dereferencing whatever the out-of-range read produced.
	assert( i >= 0 && (int) Plugs.size() > i );

	if( i < 0 || i >= (int) Plugs.size() )
		return nullptr;

	return Plugs[i];
}

// get plug, creating autoduplicate plugs as nesc.
IPlug* CUG::GetPlugForSerialize(int i)
{
	// i comes straight out of a file, so it can be anything. The assert compiles
	// out under NDEBUG, and the size check below is signed-compared, which passes
	// for every negative i - so without this the release build would read Plugs[-1].
	assert( i >= 0 );

	if( i < 0 )
		return nullptr; // caller drops the connector, see CLine2::isValid().

	if(static_cast<int>(Plugs.size()) > i)
	{
		return Plugs[i];
	}

	// maybe plug is autoduplicate
	const auto last_description_id = getType()->PlugCount() - 1;
	if(last_description_id < 0)
	{
		assert(false);
		return nullptr;
	}

	auto new_plug_descriptor = getType()->getPinDescriptionById(last_description_id);

	if(!new_plug_descriptor->autoDuplicate())
	{
		assert(false);
		return nullptr;
	}

	while(Plugs.size() <= i)
	{
		auto new_plug = MakePlug((new_plug_descriptor) );
		InsertPlugSorted(new_plug);
	}

	return Plugs[i];
}

// todo !!!, plugs in map
// !! how does this discriminate between audio and gui plugs with same id????
#if 0
IPlug* CUG::GetPlug ById(int i)
{
	for( auto it = Plugs.begin() ; it != Plugs.end() ; ++it )
	{
		if( (*it)->getPlugDescID() == i )
			return *it;
	}

	return 0;
}
#endif

// Return plug by type and count (within type)
IPlug* CUG::GetPlug(EDirection p_direction, EPlugDataType p_datatype, int p_index)
{
	int count = 0;

	for( auto it = Plugs.begin() ; it != Plugs.end() ; ++it )
	{
		IPlug* p = *it;

		if( p->GetDirection() == p_direction && p->getDatatype() == p_datatype )
		{
			if( count++ == p_index )
				return p;
		}
	}

	// ok not to find in some cases (vst)	assert(false); //can't find this plug
	_RPT0(_CRT_WARN, "CUG::GetPlug - Can't find plug!!!!\n" );
	return nullptr;
}

void CUG::ToggleMute()
{
	SetMute(!mute);
}

void CUG::SetMute(bool m)
{
	mute = m;
	VO_Notify( OM_MUTE_CHANGE, &mute );

	Container()->NotifyFast(OM_REFRESH_PRESENTERS);

	GetApp()->invalidateDsp();

	Document()->SetModified();
}

void CUG::setHoverScopePin(int pinIdx) // visible pin index
{
	// https://www.youtube.com/watch?v=NcfmTZkA_kY&ab_channel=AlphaForeverModular

	hoverScopePin = pinIdx;
	int32_t dspHoverModule = Handle();
	int32_t dspHoverPinIdx = -1;

	std::string text;
	if (pinIdx >= 0 && pinIdx < Plugs.size())
	{
		auto& p = Plugs[pinIdx];

		if(!p->isUnusedSpare())
		{
			auto masterContainer = Application()->Document()->MasterContainer;
			const auto offlineParam = masterContainer->get_patch_manager()->GetHostGeneratedParameter(HC_PROCESSOR_OFFLINE, true, masterContainer);
			const auto processorOffline = (bool)offlineParam->GetValue();

			if(!processorOffline && !p->isUiPlug() && (p->GetDirection() != DR_IN || p->HasActiveConnections()))
			{
				text.clear();

				if(p->isIoPlug()) // can't hover IO pin, get nothing back.
				{
					if(auto destPin = p->GetUltimateDest2(DR_IN); destPin)
					{
						dspHoverModule = destPin->UG()->Handle();
						dspHoverPinIdx = destPin->UG()->getRuntimePinIndex(destPin, true);
					}
				}
				else
				{
					dspHoverPinIdx = getRuntimePinIndex(p, true);
				}
			}
			else
			{
				if(p->GetDirection() == DR_IN && !p->HasActiveConnections())
				{
					if(p->getDatatype() == DT_ENUM)
					{
						const auto enum_list = WStringToUtf8(p->getDefaultEnumList());
						text = enum_list_lookup_id(enum_list, StringToInt(p->GetDefault())).text;
					}
					else
					{
						text = NiceFormatted(p->GetDefault(), p->getDatatype());
					}
				}
			}
		}
	}
	else
	{
		text = "Invalid IDX";
	}

	if (pinIdx != -1) // -1 = moduleview is handing the test itself (GUI pins).
	{
		handleAndString parms
		{
			Handle(),
			text.c_str()
		};

		Container()->NotifyFast(OM_HOVER_SCOPE_VALUE, (void*)&parms);
	}

	// inform Processor
	Application()->setHoverScopePin(dspHoverModule, Handle(), dspHoverPinIdx);
}

void CUG::OnPlugDefaultChange(IPlug* plug)
{
	// inform generator of change
	if( // Application()->SynthRunning() &&
		!plug->isUiPlug()
		&& (plug->isSettableOutput()
			|| (!plug->HasActiveConnections() && plug->GetDirection() == DR_IN))
		)
	{
		if (auto queue = Application()->MessageQueToDspOrNull(); queue)
		{
			const auto defaultValue = plug->GetDefault();
			my_msg_que_output_stream strm(queue, Handle(), "setd"); // Set-Default.
			strm << (int) ( sizeof(wchar_t) * defaultValue.size() + sizeof(int32_t) * 2); // message length. (string has int32 size then actual bytes.)
			strm << (int32_t) getRuntimePinIndex(plug, true);
			strm << defaultValue;
			strm.Send();
		}
	}

	// redraw if imbedded control changes
	if( plug->RedrawOnChange() && Container() )
	{
		Container()->VO_Notify( OM_LAYOUT_CHANGE2, this );
	}
}

// ideal template function (once plugs use STL)
int CUG::getPlugIdx(IPlug* p_plug)
{
	int plug_num = 0;

	for( auto it = Plugs.begin() ; it != Plugs.end() ; ++it )
	{
		if( (*it)  == p_plug )
		{
			return plug_num;
		}

		plug_num++;
	}

	_RPT0(_CRT_WARN, "getPlugIdx FAILED !!!\n" );
	return -1;
}

int CUG::getRuntimePinIndex( IPlug* p_plug, bool isDspPin )
{
	// Not valid on unused spares.
	assert( !p_plug->isUnusedSpare() );
	int plug_num = 0;

	for( auto it = Plugs.begin() ; it != Plugs.end() ; ++it )
	{
		// Based on active cords count , ie not going to muted objects
		IPlug* p = *it;

		if( p == p_plug )
		{
			return plug_num;
		}

		if( !p->isUnusedSpare() ) // skip unused spares, don't even count them, the ug don't have them
		{
			if( isDspPin == !p->isUiPlug( ) ) // skip unused spares, don't even count them, the ug don't have them
			{
				++plug_num;
			}
		}
	}

	_RPT0(_CRT_WARN, "getRuntimePinIndex FAILED !!!\n" );
	return -1;
}

// get index of plug out of all plugs, DSP and GUI combined (GUI first).
int CUG::getPinNumber(IPlug* p_plug)
{
	// Not valid on unused spares.
	assert(!p_plug->isUnusedSpare());

	int plug_num = 0;
	// GUI.
	for( auto p : Plugs )
	{
		if( p == p_plug )
		{
			return plug_num;
		}
		++plug_num;
	}

	_RPT0(_CRT_WARN, "getPinNumber FAILED !!!\n");
	return -1;
}

// Given a plug, determine what DSP plug it represents.
// returns an index counting ONLY parameter pins
int CUG::getParameterIdx(IPlug* p_plug)
{
	int plug_num = 0;

	for( auto it = Plugs.begin() ; it != Plugs.end() ; ++it )
	{
		// counts registered parameter plugs (not dsp only parameter plugs)
		IPlug* p = *it;

		if( p->isParameterPlug() && p->isUiPlug() )
		{
			if( p == p_plug )
			{
				return plug_num;
			}

			plug_num++;
		}
	}

	_RPT0(_CRT_WARN, "getGeneratorParameterIdx FAILED !!!\n" );
	return -1;
}

void CUG::OnMenuCommand( int p_view_type, uint32_t p_command_id, gmpi::drawing::PointL mouse_pos )
{
	switch( p_command_id )
	{
	case POPUP_MENU_MUTE:
		ToggleMute();
		break;

		// Display WPF Dialog.
	case POPUP_MENU_REPLACE:
		Document()->Application()->VO_Notify( OM_WPF_REPLACE_DIALOG, (void*) static_cast<intptr_t>(Handle()));
		break;

	case POPUP_MENU_DEBUG_TOGGLE:
		AttachDebugger();
		break;

	case POPUP_MENU_DEBUG_CODE:
	{
		//auto moduleType = getType()->UniqueId();
		Document()->Application()->VO_Notify(OM_SHOW_CODE_SKELETON_DIALOG, (void*) static_cast<intptr_t>(Handle()));
	}
	break;

	case POPUP_MENU_PARAMETER_DETAIL:
	{
		//auto moduleType = getType()->UniqueId();
		Document()->Application()->VO_Notify(OM_SHOW_PARAMETERS_DIALOG, (void*) static_cast<intptr_t>(Handle()));
	}
	break;

	case POPUP_MENU_EXCLUDE_FROM_VST:
		excludeFromVst = !excludeFromVst;
		break;
		
	case POPUP_MENU_EXCLUDE_FROM_JUCE:
		excludeFromJUCE = !excludeFromJUCE;
		break;

	case POPUP_MENU_DELETE_BYPASS:
		DeleteBypass();
		break;
		
	case POPUP_MENU_CONNECT:
		OnConnectDialog();
		break;

	case POPUP_MENU_LOCATE_CONTAINER:
		Locate();
		break;


	default:
	{
		CDocOb::OnMenuCommand(p_view_type, p_command_id, mouse_pos);
	}
	}
}

// "Delete Retain Wires" menu.
void CUG::DeleteBypass()
{
	SuspendDSP x( GetApp() );

	std::vector<IPlug*> fromPlugs;
	std::vector<IPlug*> toPlugs;

	for( auto it = Plugs.begin() ; it != Plugs.end() ; ++it )
	{
		IPlug* p = *it;

		for( auto line : p->Connectors() )
		{
			if( p->GetDirection() == DR_IN )
			{
				fromPlugs.push_back(line->FromPlug);
			}
			else
			{
				toPlugs.push_back(line->ToPlug);
			}
		}
	}

	for( auto fp : fromPlugs )
	{
		for( auto tp : toPlugs )
		{
			if( !fp->IsConnectedTo(tp) && fp->isUiPlug() == tp->isUiPlug() )
			{
				Container()->AddLine( fp, tp );
			}
		}
	}

	OnDelete();
}

void CUG::DoReplaceDialog()
{
	VO_Notify( OM_WPF_REPLACE_DIALOG, this );
}

std::vector< std::pair<bool, std::string> > CUG::GetPlugsText()
{
	std::vector< std::pair<bool, std::string> > pluginfo;

	for (auto p : Plugs)
	{
		pluginfo.push_back(std::pair<bool, std::string>(p->GetDirection() == DR_IN, WStringToUtf8(p->getName())));
	}

	return pluginfo;
}

void CUG::OnReplace(CUG* oldModule) // Can be replacing ANY module with any other. Not same as upgrade.
{
#if defined( _DEBUG )
	debugUpgradeInProgress = true;
#endif

#if 0
	// generate a unique handle. pRevents crash in Plug_decorator_default::CopyDefaultToVariable(). from OnReplace().
	oldModule->Document()->uniqueIdDatabase.setHandleAutoGenerated(this);
#else
	{
		// Attempt to retain old handle.
		auto& db = oldModule->Document()->uniqueIdDatabase;
		auto h = oldModule->Handle();
		db.Unregister(h);
		db.setHandleAutoGenerated(oldModule, true);

		setHandle(h);
		db.Register(this);
	}
#endif

	MoveConnectorsFrom( oldModule );

	// copy all default vals (must be done after ALL lines connected because lines can set defaults)
	for( auto it = oldModule->Plugs.rbegin() ; it != oldModule->Plugs.rend() ; ++it )
	{
		IPlug* old_p = *it;

		for (auto it2 = Plugs.begin(); it2 != Plugs.end(); ++it2)
		{
			IPlug* p = *it2;

			// OK to initialize a float with an int etc if pin datatype has changed.
			int dt1 = p->getDatatype();
			int dt2 = old_p->getDatatype();
			bool datatypeIsNumeric1 = dt1 != DT_TEXT && dt1 != DT_MIDI && dt1 != DT_BLOB && dt1 != DT_OBJECT;
			bool datatypeIsNumeric2 = dt2 != DT_TEXT && dt2 != DT_MIDI && dt2 != DT_BLOB && dt1 != DT_OBJECT;

			if( p->GetDirection() == old_p->GetDirection() && datatypeIsNumeric1 == datatypeIsNumeric2 && p->isUiPlug() == old_p->isUiPlug() && p->getName() == old_p->getName() )
			{
				if (p->can_set_value())
				{
					p->SetDefaultQuiet(old_p->GetDefault());
				}
				break;
			}
		}
//		_RPTW1(_CRT_WARN, L"%s \n", old_p->getName().c_str());
	}

	// copy placement
	{
		auto r = oldModule->getViewObRect(CF_PANEL_VIEW);
		setViewObRect(CF_PANEL_VIEW, r);
	}
	{
		auto r = oldModule->getViewObRect(CF_STRUCTURE_VIEW);
		setViewObRect(CF_STRUCTURE_VIEW, r);
	}

	// copy name (only if user botherd to change it).
	if( ::GetName(oldModule->getType()) != oldModule->GetName() )
	{
		// bypass CControl::SetName which causes layout update on inconsistant object.
		CUG::SetName(oldModule->GetName());
	}

	mute = oldModule->mute;
	SetSelected(oldModule->GetSelected());

#if defined( _DEBUG )
	debugUpgradeInProgress = false;
#endif
}

// this virtual function allows CUGs to customise an enum list being set to a control
// (used by switch)
std::wstring CUG::CustomiseEnumList(IPlug* plug, std::wstring default_list)
{
	// is "default_list == (L"{AUTO}")" redundant??
	assert( default_list != (L"{AUTO}") || plug->UsesAutoEnumList() );

	// SDK2 GUI plugs don't return default (AUTO), need to call UsesAutoEnumList() for them.  e.g. ui_enum_to_bools
	if( /*default_list == (L"{AUTO}") ||*/ plug->UsesAutoEnumList() )
	{
		// create a list from the auto-duplicate plug names
		std::wstring new_list;

		for( auto it = Plugs.begin() ; it != Plugs.end() ; ++it )
		{
			IPlug* p = *it;

			//			if( (p->GetFlags()& IO_AUTODUPLICATE) != 0 && p->GetNumConnections() != 0 )
			if( p->autoDuplicate() && p->GetNumConnections() != 0 )
			{
				if( !new_list.empty() )
				{
					new_list = new_list + (L",");
				}

				new_list = new_list + p->getName();
			}
		}

		if( new_list.empty() )
		{
			new_list = (L"<none>");
		}

		return new_list;
	}

	return default_list;
}

void CUG::UpgradeFrom(CUG* old)
{
	// generate a unique handle.
	old->Document()->uniqueIdDatabase.setHandleAutoGenerated(this);

	MoveConnectorsFrom( old );

	// copy all default vals (must be done after ALL lines connected because lines can set defaults)
	for( auto it = old->Plugs.rbegin() ; it != old->Plugs.rend() ; ++it )
	{
		IPlug* old_p = *it;
		IPlug* new_p = GetPlug( old_p->getName() );

		if( new_p != nullptr && new_p->can_set_value() )
		{
			new_p->SetDefault( old_p->GetDefault() );
		}
	}

	// copy placement
	{
		auto r = old->getViewObRect(CF_PANEL_VIEW);
		setViewObRect(CF_PANEL_VIEW, r);
	}
	{
		auto r = old->getViewObRect(CF_STRUCTURE_VIEW);
		setViewObRect(CF_STRUCTURE_VIEW, r);
	}

	// copy name (only if user botherd to change it.
	if( ::GetName(old->getType()) != old->GetName() )
	{
		SetName( old->GetName() );
	}

	mute = old->mute;
}

// Levenshtein Distance Algorithm. Compares two strings for similarity.
unsigned int levenshtein_distance(const wstring& s1, const wstring& s2)
{
	const size_t len1 = s1.size(), len2 = s2.size();
	vector<unsigned int> col(len2+1), prevCol(len2+1);

	for (unsigned int i = 0; i < prevCol.size(); i++)
		prevCol[i] = i;

	for (unsigned int i = 0; i < len1; i++)
	{
		col[0] = i+1;

		for (unsigned int j = 0; j < len2; j++)
			col[j+1] = min( min( 1 + col[j], 1 + prevCol[1 + j]),
			                prevCol[j] + (s1[i]==s2[j] ? 0 : 1) );

		col.swap(prevCol);
	}

	return prevCol[len2];
}

void CUG::MoveConnectorsFrom(CUG* old_module)
{
	// sound out was screwing up
	const bool ignorePinNames = getType()->UniqueId() == L"Sound Out" || getType()->UniqueId() == L"Sound In";

	vector<int> replacementPlugs;

	// crash on autoduplicating (it invalidated)	for( auto it = old_module->Plugs.begin() ; it != old_module->Plugs.end() ; ++it )
	// 'twisted' switches by reversing autoduplicated 	for (int i = old_module->Plugs.size() - 1; i >= 0; --i)
	for (int i = 0 ; i < old_module->Plugs.size(); ++i)
	{
		IPlug* oldPlug = old_module->Plugs[i];

		if (oldPlug->Connectors().empty())
			continue;

		// Find best match on new module for lines.
		// First what is the plugs index, relative to others of the same direction and datatype.
		float old_index = 0;

		for(auto newPlug : old_module->Plugs)
		{
			/* hmm
			bool isCompatible = newPlug->GetDirection() == oldPlug->GetDirection();

			// Allow upgrade on plugs converted from sample -> float (all else being equal). (for Feedback Delay pin 2).
			bool exception = newPlug->GetDirection() == DR_OUT &&
				newPlug->getDatatype() == DT_FLOAT && oldPlug->getDatatype() == DT_FSAMPLE &&
				newPlug->getName() == oldPlug->getName();

			isCompatible &= newPlug->getDatatype() == oldPlug->getDatatype() || exception;
			*/

			if(newPlug->GetDirection() == oldPlug->GetDirection() && newPlug->getDatatype() == oldPlug->getDatatype())
			{
				++old_index;
			}

			if( newPlug == oldPlug)
				break;
		}

		// Prevent Sliders obsolete 'MIDI' pin resurfacing during 'replace'
		const auto uniqueId = getType()->UniqueId();
		const bool neverConnectHiddenPins =
			uniqueId.find(L"List Entry") != std::string::npos
			|| uniqueId.find(L"Slider") != std::string::npos
			|| uniqueId.find(L"Text Entry") != std::string::npos;

		float best_score = 99999.0f;
		IPlug* best_match = 0;
		float new_index = 0;

		for( auto newPlug : Plugs)
		{
			float score = 99999.0f;

			if (oldPlug->autoDuplicate() && newPlug->autoDuplicate() && newPlug->GetNumConnections() == 0)
			{
				score = 0.0f; // Winner.
			}
			else
			{
				if (!newPlug->isMinimised() // allow IO_MINIMISED, for Osc PM Depth dummy
					&& !(neverConnectHiddenPins && newPlug->DisableIfNotConnected())
					&& newPlug->canAcceptConnection()
					&& newPlug->GetDirection() == oldPlug->GetDirection()
					// Allow upgrade on plugs converted from sample -> float (all else being equal). (for Feedback Delay pin 2).
					&& (newPlug->getDatatype() == oldPlug->getDatatype() || (!newPlug->isUiPlug() && !oldPlug->isUiPlug() && AreCompatible(newPlug->getDatatype(), oldPlug->getDatatype())) )
					&& newPlug->isUiPlug() == oldPlug->isUiPlug()
					&& !newPlug->isMinimised()
					)
				{
					++new_index;
					float score1 = ignorePinNames ? 0.0f : (float)levenshtein_distance(newPlug->getName(), oldPlug->getName());
					float score2 = fabsf(new_index - old_index) * 0.1f;
					score = score1 + score2;
//					_RPTW5(_CRT_WARN, L"%s vs %s  D=%f I=%f score = %f\n", oldPlug->getName().c_str(), newPlug->getName().c_str(), score1, score2, score);
				}
			}

			if (score < best_score)
			{
				best_score = score;
				best_match = newPlug;
			}
		}

		if( best_match )
		{
			// Copy connectors to cope with iterator getting invalidated by operations.
			connectors_t tempConnectors( oldPlug->Connectors().begin(), oldPlug->Connectors().end() );

			// process connectors in REVERSE order (as we are adding new ones to tail)
			for( auto it = tempConnectors.rbegin() ; it != tempConnectors.rend() ; ++it )
			{
				CLine2* line = *it;
				IPlug* p1 = line->FromPlug;
				IPlug* p2 = best_match;

				if( oldPlug->GetDirection() == DR_OUT )
				{
					p1 = best_match;
					p2 = line->ToPlug;
				}

				// Already connected?
				auto already = false;
				for( auto line2 : p1->Connectors( ) )
				{
					if( line2->FromPlug == p2 || line2->ToPlug == p2 )
					{
						already = true;
						break;
					}
				}

				if( !already )
				{
					Container()->AddLine( p1, p2 );
				}
			}
		}
	}
}


void CUG::RemoveAllLines()
{
	// bit tricky, On Containers with self-connections, removing a line might cause TWO
	// IO plugs to delete themseves - thereby invalidating the iterator.
	auto plugCount = Plugs.size();

	for( auto it = Plugs.rbegin() ; !(it == Plugs.rend()) ; )
	{
		(*it)->RemoveLines();

		if( plugCount == Plugs.size() )
		{
			++it; // next plug.
		}
		else
		{
			it = Plugs.rbegin(); // iterator (potentially) invalid. restart.
			plugCount = Plugs.size();
		}
	}
}

void CUG::HighlightLines(int pinIdx, int highlightType)
{
	// The view can show pins the document hasn't materialised (e.g. an autoduplicate 'spare'). Nothing to highlight.
	if( pinIdx < 0 || (int) Plugs.size() <= pinIdx )
		return;

	auto pin = Plugs[pinIdx];
	for (auto line : pin->Connectors())
	{
		// Set the highlight color of the line.
		line->Highlight(highlightType);

		auto otherEndPin = pin->GetDirection() == DR_IN ? line->FromPlug : line->ToPlug;

		otherEndPin->highlightOutsideLines2(highlightType);
	}
}

// if user has created feedback path. need to highlight it in red as warning
bool CUG::HighlightLineTo(CUG* p_ug, int32_t flags)
{
	Highlight(flags);
	bool found = false;

	for( auto& p : Plugs)
	{
		if( p->GetDirection() == DR_OUT ) // no need to do from both ends
		{
			for( auto line : p->Connectors() )
			{
				if( line->HighlightLineTo(p_ug, flags) )
				{
					found = true;
				}
			}
		}
	}

	return found;
}

void CUG::UnHighlight()
{
	const auto flags = ~PinHighlightFlag_Emphasise;

	CDocOb::Highlight(flags);

	for( auto p : Plugs)
	{
		if( p->GetDirection() == DR_OUT ) // no need to do from both ends
		{
			for(auto line : p->Connectors())
				line->Highlight(flags);
		}
	}
}

gmpi::drawing::RectL CUG::GetAbsolutePanelRect()
{
	/*
	todo stop recursion at...
	IsImbeddedView(CF_PANEL_VIEW)
	*/

	auto r = getViewObRect(CF_PANEL_VIEW);
	if( !gmpi::drawing::empty(r) )
	{
		CContainer* parent = Container();

		while( parent )
		{
			if( parent->Container() && parent->Container()->Container() ) // Only Offset as far as main Synth Container. Not 'Main'.
			{
				if (parent->IsImbeddedView(CF_PANEL_VIEW))
				{
					return r;
				}

				IPlug* controlOnParentPin = parent->GetPlug(2);
				assert( controlOnParentPin->getName() == (L"Visible") );

				bool showOnParent = controlOnParentPin->GetDefault().compare((L"1")) == 0 || controlOnParentPin->GetNumConnections() > 0;

				if( !showOnParent )
				{
					r.bottom = r.top = r.left = r.right = 0;
					break;
				}

				const auto offset = parent->getPanelWndOffset();
				r.left += offset.width;
				r.top += offset.height;
				r.right += offset.width;
				r.bottom += offset.height;
			}
			parent = parent->Container();
		}
	}

	return r;
}

gmpi::drawing::RectL CUG::getViewObRect(int p_view_type)
{
	if( p_view_type == CF_STRUCTURE_VIEW )
		return m_struct_rect;
	else
		return CDocOb::getViewObRect(p_view_type);
}

void CUG::setViewObRect(int p_view_type, gmpi::drawing::RectL& p_rect)
{
	if( p_view_type == CF_STRUCTURE_VIEW )
	{
		if( m_struct_rect != p_rect )
		{
//			_RPT4(_CRT_WARN, "CUG::setViewObRect Struct(%d, %d, %d, %d)\n", p_rect.left, p_rect.top, p_rect.right, p_rect.bottom);
			m_struct_rect = p_rect;

			if(Document())
				Document()->SetModified();

			if (Container()) // main view has null container
			{
				Container()->VO_Notify(OM_ONCHANGE_CHILD_POSITION_STRUCT, (void*)this);
			}
		}
	}
}

void CUG::offsetViewObRect( int p_view_type, int dx, int dy )
{
	auto r = getViewObRect( p_view_type );
	r.left += dx;
	r.top += dy;
	r.right += dx;
	r.bottom += dy;
	setViewObRect( p_view_type, r );
}

CPlug4* CUG::InsertPlugSorted(CPlug4* p_plug)
{
	// below needed?
	assert( p_plug );
	assert( p_plug->UG() == this );
	assert(std::find(Plugs.begin(), Plugs.end(), p_plug) == Plugs.end());
	p_plug->SetUG(this);

	// insert sorted by plug desc id, GUI plugs first. Crucial for VSTs
	// new.  Plugs are almost always added in order, so check that case first
	if( !Plugs.empty() && !PlugSortOrderOK( Plugs.back(), p_plug) )
	{
		for( auto it = Plugs.begin() ; it != Plugs.end() ; ++it )
		{
			if( PlugSortOrderOK( p_plug, *it)  )
			{
				Plugs.insert( it, p_plug );
				return p_plug;
			}
		}
	}

	Plugs.push_back( p_plug );
	return p_plug;
}

bool CUG::show_title_on_panel()
{
	// Put title on all Older Panel objects by default.
	if( getType()->ModuleTechnology() == MT_SDK2 )
	{
		return true;
	}

	IPlug* p = GetPlug( (L"Show Title On Panel") );
	return p && p->GetDefault().compare(L"1") == 0;
}

void CUG::OnConnectDialog()
{
	doDialogConnectUg(this);
}

void CUG::GetTimingRequirements( int& p_flags )
{
	if( GetMute() )
		return;

	std::wstring module_type_id = getType()->UniqueId();

	if( module_type_id == (L"Sound In") )
	{
		p_flags |= SER_SOUNDCARD_IN;
	}

	assert(module_type_id != (L"Audio Out"));
#if 0
	{
		if( GetPlug((L"Mode"))->GetDefault().compare(L"1") == 0 ) // mode = Soundcard
		{
			p_flags |= SER_SOUNDCARD_OUT;
		}
		else
		{
			p_flags |= SER_FILE;
		}
	}
#endif

	if( module_type_id == (L"Sound Out") )
	{
		p_flags |= SER_SOUNDCARD_OUT;
	}

	if( module_type_id == (L"MIDI Out") || module_type_id == (L"MIDI In") )
	{
		p_flags |= SER_REALTIME;
	}

	if( module_type_id == L"Wave Recorder" )
	{
		p_flags |= SER_FILE;
	}

}

bool CUG::GetMute()
{
	if( !getType()->isDllAvailable() )
	{
		return true;
	}

	return mute || (Container() && Container()->GetMute());
}

bool CUG::doExport()
{
	const auto isJuce = 0 != (CDocOb::exportFlags & EXP_JUCE);
	const auto isPlugin = 0 != (CDocOb::exportFlags & EXP_PLUGIN);
	return !GetMute() && !(excludeFromVst && isPlugin) && !(excludeFromJUCE && isJuce);
}

void CUG::OnDspMsg(int p_msg_id, gmpi::hosting::my_input_stream& p_stream)
{
#if 0
	if( p_msg_id == id_to_long("pat2") ) // patch change from DSP thread.
	{
		int patch;
		p_stream >> patch;
		get_patch_manager()->SetProgram(patch);
			return;
	}
#endif
	if( p_msg_id == code_to_long('c', 'p', 'u', 0)) // gmpi::hosting::id_to_long("cpu") )
	{
		cpu_accumulator::staticUpdate(cpu_meter.get(), p_stream);
		assert(Container());
		Container()->VO_Notify(OM_CPU_UPDATE, this);
		return;
	}

	if (p_msg_id == code_to_long('h', 'v', 's', 'v')) // "hvsv" Hover-Scope value from Processor as text
	{
		std::string text;
		p_stream >> text;

		handleAndString parms
		{
			Handle(),
			text.c_str()
		};

		// inform the ModuleViewStruct
		Container()->NotifyFast(OM_HOVER_SCOPE_VALUE, (void*)&parms);
		return;
	}

	if (p_msg_id == code_to_long('h', 'v', 's', 'e')) // "hvse" Hover-Scope ENUM value from Processor as int32
	{
		int32_t value{};
		p_stream >> value;

		std::string text = std::to_string(value);

		// extra step: lookup enum text
		if (hoverScopePin >= 0 && hoverScopePin < Plugs.size())
		{
			auto& p = Plugs[hoverScopePin];

			if (p->getDatatype() == DT_ENUM)
			{
				const auto enum_list = WStringToUtf8(p->getDefaultEnumList());
				text = enum_list_lookup_id(enum_list, value).text;
			}
		}

		handleAndString parms
		{
			Handle(),
			text.c_str()
		};

		// inform the ModuleViewStruct
		Container()->NotifyFast(OM_HOVER_SCOPE_VALUE, (void*)&parms);
		return;
	}

	if (p_msg_id == code_to_long('h', 'v', 's', 'w')) // "hvsw" Hover-Scope waveform
	{
		int32_t count;
		p_stream >> count;

		handleAndWaveform parms;
		parms.handle = Handle();

		parms.data = std::make_unique<std::vector<float>>(count);
		p_stream.Read(parms.data->data(), sizeof(float) * count);

		Container()->NotifyFast(OM_HOVER_SCOPE_WAVEFORM, (void*)&parms);
		return;
	}
	CDocOb::OnDspMsg(p_msg_id, p_stream);
}

std::wstring MakePN( std::wstring& n ) // PN_SOME_PLUG
{
	std::wstring t;
	std::wstring p_name = (n);
	MakeLegalVariableName(p_name);
	p_name = Uppercase(p_name); //.MakeUpper();
	std::wstring s = (L"PN_") + p_name;
	return s;
}

std::wstring MakeFuncName( std::wstring& n ) // SomePlug
{
	std::wstring temp = RemoveSpaces((n));
	MakeLegalVariableName(temp);
	return temp;
}

// Temporary variable used in process loop.
std::wstring MakeVarName( std::wstring& n ) // somePlug
{
	std::wstring temp = RemoveSpaces( (n) );
	MakeLegalVariableName(temp);
	temp[0] = static_cast<wchar_t>(tolower( temp[0] ));
	return temp;
}

std::wstring MakePinVarName( std::wstring& n ) // pinInput1
{
	std::wstring temp(RemoveSpaces((n)));
	MakeLegalVariableName(temp);
	return (L"pin") + temp;
}

std::wstring MakeModuleId( std::wstring& n ) // Oscillator
{
	if(n.size() > 3 && ( n.substr(3) == L" " || n.substr(3) == L"_") )
	{
		n = Right( n,  n.size() - 3 );
	}

#if defined( _DEBUG )
	return (L"SE ") + n;
#else
	return (L"My ") + n;
#endif
}

void CopyFileWithReplace(std::wstring sourceFileNameW, std::wstring destinationFileNameW, string searchString, string replaceString)
{
    const auto sourceFileName = WStringToUtf8(sourceFileNameW);
    const auto destinationFileName = WStringToUtf8(destinationFileNameW);

	FILE* f1, *f2;
	f1 = fopen(sourceFileName.c_str(), "r");

	if( nullptr == f1 )
	{
		wstring errmsg = L"File open error : " + sourceFileNameW;
		SafeMessagebox(0, errmsg.c_str(), L"", MB_OK);
		return;
	}

	f2 = fopen(destinationFileName.c_str(), "w");

	if( nullptr == f2 )
	{
		wstring errmsg = L"File open error : " + destinationFileNameW;
		SafeMessagebox(0, errmsg.c_str(), L"", MB_OK);
		return;
	}

	char buffer[512];

	while( !feof(f1) )
	{
		char* ret = fgets(buffer, 512, f1);

		if( ret != 0 )
		{
			string oldLine(buffer);
			// replace "gain.xml" with "whatever.xml"
			auto p = oldLine.find(searchString);

			if( p != std::string::npos )
			{
				//string newLine = oldLine.substr(p, searchString.size() ) + replaceString;
				string newLine = oldLine.substr(0, p ) + replaceString + oldLine.substr(p + searchString.size(), oldLine.size()- p - searchString.size());
				fputs(newLine.c_str(), f2);
			}
			else
			{
				fputs(buffer, f2);
			}
		}
	}

	fclose(f1);
	fclose(f2);
}

void CopyFileWithReplace(wstring sourceFileName, wstring destinationFileName, wstring searchString, wstring replaceString)
{
    CopyFileWithReplace(sourceFileName, destinationFileName, WStringToUtf8(searchString), WStringToUtf8(replaceString));
}

void WriteFileFromString(const std::wstring& destinationFileName, const std::wstring& s)
{
	// convert to plain text, not unicode.
	string stringascii = WStringToUtf8(s);

	ofstream myfile;
	myfile.open(std::filesystem::path{ destinationFileName });
	myfile << stringascii;
	myfile.close();
}

void TpCopyFile(wstring sourceProjectFolder, wstring destinationProjectFolder, wstring filename)
{
	wstring sourceFileName = combine_path_and_file(sourceProjectFolder, filename);
	wstring destinationFileName = combine_path_and_file(destinationProjectFolder, filename);

    std::filesystem::copy_file(sourceFileName, destinationFileName, std::filesystem::copy_options::overwrite_existing);
}

std::wstring ModuleIdToFilename(std::wstring uniqueId)
{
	auto base_filename = uniqueId;

	// Sanitise File Name.
	for (auto it = base_filename.begin(); it != base_filename.end(); )
	{
		auto c = *it;
		if ((c >= L'0' && c <= L'9') || (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z') || c == L'_' || c == L' ')
		{
			++it;
		}
		else
		{
			it = base_filename.erase(it);
		}
	}

	// capitalise first letter each word.
	wchar_t prev = (L' ');

	for (size_t i = 0; i < base_filename.size(); i++)
	{
		wchar_t letter = base_filename[i];

		if (prev == (L' '))
		{
			letter = (wchar_t) toupper(letter);
		}
		else
		{
			letter = letter;// _totlower(letter); // preserve CamelCase names
		}

		base_filename[i] = letter;
		prev = letter;
	}

	replacein(base_filename, L" ", L"");

	return base_filename;
}


// Build Code Skeleton.
void CUG::BuildSkeletonCode(std::wstring plugin_name)
{
#ifdef _DEBUG
const auto currentYear = []()
{
	const auto today = std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now());
	return int(std::chrono::year_month_day{ today }.year());
}();
const std::wstring synthEditCopyRightNotice =
L"// SPDX-License-Identifier: ISC\n// Copyright 2007-" + std::to_wstring(currentYear) + L" Jeff McClintock.\n";

#endif
	const std::wstring base_filename = ModuleIdToFilename(plugin_name);
	const std::filesystem::path myDocuments = BundleInfo::instance()->getUserDocumentFolder();
	const std::filesystem::path destinationFolder = myDocuments / L"new_module";
	const std::filesystem::path destinationProjectFolder = destinationFolder / base_filename;
	const std::filesystem::path applicationFolder = GetHomeDir();

	std::filesystem::create_directory(destinationFolder);
	std::filesystem::create_directory(destinationProjectFolder);

	std::vector<std::wstring> sourceFilenames;

	bool useHeader = false;

	bool hasGuiObject = ( GetFlags() & CF_PANEL_VIEW ) != 0;
	// older types register GUI plugs from DSP. So also check hasGuiObject.
	bool hasGui = hasGuiObject || getType()->scanned_xml_gui;
	// SDK2 has DSP regardless of needed or not.  Set hasDsp false (will be corrected in pin loop if DSP pins exist)
	bool hasDsp = getType()->hasDspModule() && getType()->ModuleTechnology() != MT_SDK2;
	// determin type of GUI.
	enum {NO_GUI_OBJECT, NON_VISIBLE_GUI_OBJECT, HWND_GUI_OBJECT, COMPOSITED_GUI_OBJECT };
	int gui_type = NO_GUI_OBJECT;

	if( hasGui )
	{
		if( getType()->gui_object_non_visible() )
		{
			gui_type = NON_VISIBLE_GUI_OBJECT;
		}
		else
		{
			gui_type = HWND_GUI_OBJECT;

			Module_Info3* mi3 = dynamic_cast<Module_Info3*>( getType() );

			if( mi3 )
			{
				if( mi3->getWindowType() == MP_WINDOW_TYPE_COMPOSITED )
				{
					gui_type = COMPOSITED_GUI_OBJECT;
				}
			}
		}
	}

	// generate list of plug names for plin members, ensuring all are unique
	vector< pair<IPlug*, std::wstring> > pin_vars;
	vector< pair<IPlug*, std::wstring> > pin_vars_dsp;
	bool hasGuiPins = false;
	bool hasDspPins = false;
	int pass;

	for( auto it = Plugs.begin() ; it != Plugs.end() ; ++it )
	{
		bool is_gui_plug = (*it)->isUiPlug();

		if( is_gui_plug )
			hasGuiPins = hasGui = true;
		else
			hasDspPins = hasDsp = true;

		{
			std::wstring n = (*it)->getName();
			pass = 0;

			vector< pair<IPlug*, std::wstring> >* nameList;
			if (is_gui_plug)
				nameList = &pin_vars;
			else
				nameList = &pin_vars_dsp;

			// ensure it's unique
			for (auto it2 = nameList->begin(); it2 != nameList->end();)
			{
				if( (*it2).first->isUiPlug() == is_gui_plug && RemoveSpaces((*it2).second) == RemoveSpaces(n) ) // pin function names need to be unique even with spaces removed
				{
					if( pass == 0 && (*it)->GetDirection() != (*it2).first->GetDirection() )
					{
						if( (*it)->GetDirection() == DR_IN )
						{
							n += L" In";
						}
						else
						{
							n += L" Out";
						}
					}
					else
					{
						n = (*it)->getName() + L" " + IntToString(pass + 2);
					}

//					if( pass < 3 )
					{
						pass++;
						it2 = nameList->begin();
					} // else failed
				}
				else
				{
					++it2;
				}
			}

			nameList->push_back(pair<IPlug*, std::wstring>(*it, (n)));

			_RPTW1(_CRT_WARN, L"%s\n", n.c_str() );
		}
	}

	// copy project files to destination folder.

	std::wstring destinationClassName(base_filename);
	std::wstring destinationFileName;
#if 0 // gonna use inline XML?
	// Copy File "Gain.rc" replacing "gain.xml" with "whatever.xml"
	std::wstring sourceClassName( (L"XX_TEMPLATE_XX") );

	// resource file Gain.rc -> module.rc (using generic name to reduce differences between modules)
	auto sourceFileName = sourceProjectFolder / (sourceClassName + L".rc");
	destinationFileName = destinationProjectFolder / (destinationClassName + L".rc");
    string searchString = WStringToUtf8(sourceClassName);
	string replaceString = WStringToUtf8(destinationClassName);
	CopyFileWithReplace(sourceFileName, destinationFileName, searchString, replaceString);

	// filter file XX_TEMPLATE_XX.vcxproj.filters
	// !! TODO put SDK gui headers into "sdk" folder.
	//sourceFileName = combine_path_and_file(sourceProjectFolder, sourceClassName + L".vcxproj.filters" );
	//destinationFileName = combine_path_and_file(destinationProjectFolder, destinationClassName + L".vcxproj.filters" );
	//CopyFileWithReplace(sourceFileName, destinationFileName, searchString, replaceString);

	// resource.h
	TpCopyFile(sourceProjectFolder, destinationProjectFolder, L"resource.h");
#endif
	//-------------GUI HEADER-------------------------------------------------
	std::wstring ui_module_name = base_filename + (L"Gui");
	std::wstring t,s;

	//----------------XML----------------------------------------------------------------
	std::string pluginXML;
	{
		s.clear();
		tinyxml2::XMLDocument doc;
		doc.LinkEndChild(doc.NewDeclaration());

		tinyxml2::XMLNode* ModulesElement = &doc;
		ExportModuleInfo(getType(), ModulesElement, SAT_CODE_SKELETON);

		auto moduleE = ModulesElement->FirstChildElement();
		moduleE->SetAttribute("id", WStringToUtf8(MakeModuleId(plugin_name)).c_str());
		moduleE->SetAttribute("name", WStringToUtf8(plugin_name).c_str());

		tinyxml2::XMLPrinter printer;
		doc.Accept(&printer);

		// write XML to string
		pluginXML = printer.CStr();

		// write XML to file (not using atm)
#if 0
		auto destFile = destinationProjectFolder / (destinationClassName + L".xml");
		{
			ofstream factory(destFile.generic_string());
			factory << printer.CStr();
		}
#endif
	}

#if 0 // ???
				p = oldLine.find("_HEADER_FILES_");

				if( p != std::string::npos )
				{
					newLine = "";

					if( hasDsp )
						fputs("    <ClInclude Include=\"..\\se_sdk3\\mp_sdk_audio.h\" />\n", f2);

					if( hasGui )
						fputs("    <ClInclude Include=\"..\\se_sdk3\\mp_sdk_gui2.h\" />\n", f2);
				}
#endif

	s.clear();
	if( hasGui )
	{
#define USE_GMPI_GUI
#define USE_GMPI2

		int i = 0;

		std::wstring baseClass;
#if 0
	// .vcxproj.filters file.
	{
		stringstream o2;
		const auto mname = WStringToUtf8(destinationClassName);

		o2 <<
R"XML(<?xml version="1.0" encoding="utf-8"?>
<Project ToolsVersion="4.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <ItemGroup>
)XML";
		// *.cpp
		if (hasDsp)
			o2 << "    <ClCompile Include=\"" << mname + ".cpp" << "\"/>\n";

		if (hasGui)
			o2 << "    <ClCompile Include=\"" << mname + "Gui.cpp" << "\"/>";

		if(hasDsp)
			o2 <<
R"XML(
    <ClCompile Include="..\se_sdk3\mp_sdk_audio.cpp">
      <Filter>sdk</Filter>
    </ClCompile>)XML";

		if(hasGui)
			o2 <<
R"XML(
    <ClCompile Include="..\se_sdk3\mp_sdk_gui.cpp">
      <Filter>sdk</Filter>
    </ClCompile>)XML";

		o2 <<
R"XML(
    <ClCompile Include="..\se_sdk3\mp_sdk_common.cpp">
      <Filter>sdk</Filter>
    </ClCompile>
  </ItemGroup>
  <ItemGroup>)XML";

		// *.h
		if(hasDsp)
			o2 <<
R"XML(
    <ClInclude Include="..\se_sdk3\mp_sdk_audio.h">
      <Filter>sdk</Filter>
    </ClInclude>)XML";

		if(hasGui)
			o2 <<
R"XML(
    <ClInclude Include="..\se_sdk3\mp_sdk_gui2.h">
      <Filter>sdk</Filter>
    </ClInclude>)XML";

		o2 <<
R"XML(
    <ClInclude Include="..\se_sdk3\mp_sdk_common.h">
      <Filter>sdk</Filter>
    </ClInclude>)XML";

	// resources
		o2 <<
R"XML(
  </ItemGroup>
  <ItemGroup>
    <ClInclude Include="resource.h">
      <Filter>Resources</Filter>
    </ClInclude>
  </ItemGroup>
  <ItemGroup>
    <Filter Include="sdk">
      <UniqueIdentifier>{c70fc6f8-3abc-4fff-a436-8fb639a3e7fb}</UniqueIdentifier>
    </Filter>
    <Filter Include="Resources">
      <UniqueIdentifier>{15b5f461-63f5-4b31-88be-8130b6ec8068}</UniqueIdentifier>
    </Filter>
  </ItemGroup>
  <ItemGroup>
    <ResourceCompile Include=")XML" << mname << R"XML(.rc">
      <Filter>Resources</Filter>
    </ResourceCompile>
  </ItemGroup>
</Project>
)XML";

		destinationFileName = (destinationProjectFolder / (destinationClassName + L".vcxproj.filters")).generic_wstring();

		const auto outstring = Utf8ToWstring(o2.str());
		WriteFileFromString(destinationFileName, outstring);
	}
#endif

#ifdef USE_GMPI2
		if (gui_type == NON_VISIBLE_GUI_OBJECT)
		{
			baseClass = L"PluginEditorNoGui";
		}
		else
		{
			baseClass = L"PluginEditor";
		}
#else
		if (gui_type == NON_VISIBLE_GUI_OBJECT)
		{
			baseClass = L"SeGuiInvisibleBase";
		}
		else
		{
			baseClass = L"gmpi_gui::MpGuiGfxBase";
		}
#endif

#ifdef _DEBUG
		{
			s += synthEditCopyRightNotice;
		}
#endif
		// include-guard
		if (useHeader)
		{
			s += L"#pragma once\n\n";
		}

#ifdef USE_GMPI2
		s += L"#include \"helpers/GmpiPluginEditor.h\"\n";
#else
		s += L"#include \"mp_sdk_gui2.h\"\n";
#endif

#ifndef USE_GMPI2 // Drawing.h is included transitivly. omit it to avoid accidentally dragging in SDK3 Drawing.h
		if (gui_type != NON_VISIBLE_GUI_OBJECT)
		{
			s += L"#include \"Drawing.h\"\n";
		}
#endif

		s += L"\nusing namespace gmpi;";
		s += L"\nusing namespace gmpi::editor;\n";

#ifdef USE_GMPI2
		if (gui_type != NON_VISIBLE_GUI_OBJECT)
			s += L"using namespace gmpi::editor;\nusing namespace gmpi::drawing;\n";
#else
		if (gui_type != NON_VISIBLE_GUI_OBJECT)
			s += L"using namespace GmpiDrawing;\n";
#endif

		s += L"\nclass " + ui_module_name + L" final : public " + baseClass + L"\n";
		s += L"{\n";

		i = 0;
		for (auto it = Plugs.begin(); it != Plugs.end(); ++it)
		{
			if ((*it)->isUiPlug())
			{
				s += L" 	void onSet" + MakeFuncName(pin_vars[i].second) + L"()";

				if (useHeader)
				{
					s += L";";
				}
				else
				{
					//					s += L"\n{\n}";
					s += L"\n\t{\n"
						L"\t\t// " + MakePinVarName(pin_vars[i].second) + L" changed\n"
						L"\t}";
				}
				s += L"\n\n";

				i++;
			}
		}

		// 	Gui Pins.
		// IntGuiPin menuSelection;
		i = 0;
		for (auto pin : Plugs)
		{
			if (pin->isUiPlug())
			{
				auto dt = pin->getDatatype();
				// No enum pins in SDK3
				if (dt == DT_ENUM)
				{
					dt = DT_INT;
				}
#ifdef USE_GMPI2
				s += L" 	" L"Pin<" + datatypeToString2(dt) + L"> " + MakePinVarName(pin_vars[i].second) + L";\n";
				i++;
#else
				std::wstring datatype = MakeCamelCase(datatypeToString(dt));
				s += L" 	" + datatype + L"GuiPin " + MakePinVarName(pin_vars[i].second) + L";\n";
				i++;
#endif

			}
		}

		s += L"\npublic:\n";

		// Constructor
		s += L"\t" + ui_module_name + L"() = default;";

#if 0
		if (useHeader)
		{
			s +=L";";
		}
		else
		{
			s += L"\n\t{\n";

			i = 0;

#ifndef USE_GMPI2
			for (auto it = Plugs.begin(); it != Plugs.end(); ++it)
			{
				if ((*it)->isUiPlug())
				{
					std::wstring plug_id;
					plug_id = IntToString(i);

					t = L"\t\tinitializePin( " + MakePinVarName(pin_vars[i].second) + L", static_cast<MpGuiBaseMemberPtr2";
					t += L">(&" + ui_module_name + L"::onSet" + MakeFuncName(pin_vars[i].second) + L") );\n";
					s += t;
					i++;
				}
			}
#endif
			s += L"\t}";
		}
#endif

		s += L"\n\n"; //	// overrides.\n";

		if (gui_type != NON_VISIBLE_GUI_OBJECT)
		{
#ifdef USE_GMPI2
			s += L"\tReturnCode render(gmpi::drawing::api::IDeviceContext *drawingContext) override";
#else
			s += L"\tint32_t MP_STDCALL OnRender(GmpiDrawing_API::IMpDeviceContext* drawingContext ) override";
#endif

			if (useHeader)
			{
				s += L";";
			}
			else
			{
//				s += L"\n{\n}";
				s += L"\n\t{";

#ifdef USE_GMPI2
				s += LR"XML(
		Graphics g(drawingContext);

		auto textFormat = g.getFactory().createTextFormat();
		auto brush = g.createSolidColorBrush(Colors::Red);

		g.drawTextU("Hello World!", textFormat, bounds, brush);

		return ReturnCode::Ok;)XML";
#else


				s += L"\t\tGraphics g(drawingContext);\n\n";

				s += L"\t\tauto textFormat = GetGraphicsFactory().CreateTextFormat();\n";
				s += L"\t\tauto brush = g.CreateSolidColorBrush(Color::Red);\n\n";

				s += L"\t\tg.DrawTextU(\"Hello World!\", textFormat, 0.0f, 0.0f, brush);\n";

				s += L"\n\t\treturn gmpi::MP_OK;}";

#endif
			}
			s += L"\n\t}\n"; // close method
		}

		s += L"};\n\n"; // close class

		if(!useHeader)
		{
			s += L"namespace\n";
			s += L"{\n";
#ifdef USE_GMPI2
			if(hasDsp)
			{
				s += L"\tauto r = gmpi::Register<" + destinationClassName + L"Gui>::withId(\"" + MakeModuleId(plugin_name) + L"\");\n";
			}
			else
			{
				// with XML inline in the cpp
				s += L"auto r = Register<" + destinationClassName + L"Gui>::withXml(R\"XML(\n";
				s += ToWstring(pluginXML);
				s += L")XML\");\n";

				s += L"}\n";
			}
#else
			s += L"\tauto r = sesdk::Register<" + destinationClassName + L"Gui>::withId(L\"" + MakeModuleId(plugin_name) + L"\");\n";
#endif
			s += L"}\n";
		}

		if (useHeader)
		{
            destinationFileName = (destinationProjectFolder / (destinationClassName + L"Gui.h")).wstring();
			WriteFileFromString(destinationFileName, s);

			s.clear();
			//s +=  L"-------------GUI CPP---------------\n\n" ; //---------------------------------------
			// include headers
			t = (L"#include \"./") + ui_module_name + (L".h\"\n");
			s += t;
			if (gui_type != NON_VISIBLE_GUI_OBJECT)
			{
				s += L"#include \"Drawing.h\"\n";
				s += L"\nusing namespace GmpiDrawing;\n";
			}

			s += L"using namespace gmpi;\n";


#ifdef USE_GMPI2
			s += L"auto r = Register<" + ui_module_name + L">::withId(\"" + MakeModuleId(plugin_name) + L"\");";
#else

#ifdef USE_GMPI_GUI
			s += L"GMPI_REGISTER_GUI(MP_SUB_TYPE_GUI2, " + ui_module_name + L", L\"" + MakeModuleId(plugin_name) + L"\" );\n";
#else
			s += L"REGISTER_GUI_PLUGIN";
			s += L"(" + ui_module_name + L", L\"" + MakeModuleId(plugin_name) + L"\" );\n";
#endif
#endif
			s += L"\n";

			// CONSTRUCTOR...
			t = ui_module_name + (L"::") + ui_module_name;
#ifdef USE_GMPI_GUI
			t += L"()\n";
#else
			t += L"( IMpUnknown* host ) : " + baseClass + (L"(host)\n");
#endif
			s += t;
			s += L"{\n";

#ifndef USE_GMPI2
			s += L"	// initialise pins.\n";
			i = 0;

			for (auto it = Plugs.begin(); it != Plugs.end(); ++it)
			{
				if ((*it)->isUiPlug())
				{
					std::wstring plug_id;
					plug_id = IntToString(i);

					t = L"	initializePin( " + MakePinVarName(pin_vars[i].second) + L", static_cast<MpGuiBaseMemberPtr2";
					t += L">(&" + ui_module_name + L"::onSet" + MakeFuncName(pin_vars[i].second) + L") );\n";
					s += t;
					i++;
				}
			}
#endif
			s += L"}\n"
				L"\n"
				L"// handle pin updates.\n";
			// void ui_patch_automator::OnSetPIN_NAME()
			i = 0;

			for (auto it = Plugs.begin(); it != Plugs.end(); ++it)
			{
				if ((*it)->isUiPlug())
				{
					t = (L"void ") + ui_module_name + (L"::onSet") + MakeFuncName(pin_vars[i].second) + (L"()\n");
					s += t;
					s += L"{\n"
						L"	// " + MakePinVarName(pin_vars[i].second) + L" changed\n"
						L"}\n"
						L"\n";
					i++;
				}
			}

			if (gui_type != NON_VISIBLE_GUI_OBJECT)
			{
				s += L"int32_t " + ui_module_name + L"::OnRender(GmpiDrawing_API::IMpDeviceContext* drawingContext )\n";
				s += L"{\n";
				s += L"\tGraphics g(drawingContext);\n\n";

				s += L"\tauto textFormat = GetGraphicsFactory().CreateTextFormat();\n";
				s += L"\tauto brush = g.CreateSolidColorBrush(Color::Red);\n\n";

				s += L"\tg.DrawTextU(\"Hello World!\", textFormat, 0.0f, 0.0f, brush);\n";

				s += L"\n\treturn gmpi::MP_OK;\n}\n\n";
			}
		}
		//else
		//{
		//	destinationFileName = destinationProjectFolder + destinationClassName + (L"Gui.h");
		//	wstring empty(L"// not used\n");
		//	WriteFileFromString(destinationFileName, empty);
		//}

		const auto filename = destinationClassName + L"Gui.cpp";
        destinationFileName = (destinationProjectFolder / filename).wstring();
		WriteFileFromString(destinationFileName, s);

		sourceFilenames.push_back(filename);
	}

	s.clear();

	if( hasDsp )
	{
		// AUDIO class name ================================================================
		std::wstring dsp_module_name = base_filename;

		int i = 0;

#ifdef _DEBUG
		{
			s += synthEditCopyRightNotice;
		}
#endif

		if (useHeader)
		{
			s += L"#pragma once\n\n";
		}

		// include headers
		t = (L"#include \"Processor.h\"\n");
		s += t;
		s += L"\nusing namespace gmpi;\n";
		s +=  L"\nstruct " + dsp_module_name + L" final : public Processor\n{\n" ;

		// Pins
		i = 0;

		for (auto it = Plugs.begin(); it != Plugs.end(); ++it)
		{
			if (!(*it)->isUiPlug())
			{
				std::wstring name = (*it)->getName();
				std::wstring datatype;
				std::wstring directionString = L"In";

				if ((*it)->GetDirection() == DR_OUT)
				{
					directionString = L"Out";
				}

				switch ((*it)->getDatatype())
				{
				case DT_MIDI2:
					datatype = L"Midi";
					break;

				case DT_TEXT:
				case DT_STRING_UTF8:
					datatype = L"String";
					break;

				case DT_DOUBLE:
					datatype = L"Double";
					break;

				case DT_BOOL:
					datatype = L"Bool";
					break;

				case DT_ENUM:

					// deliberate fall-thru.
				case DT_INT:
					datatype = L"Int";
					break;

				case DT_FLOAT:
					datatype = L"Float";
					break;

				case DT_FSAMPLE:
					datatype = L"Audio";
					break;

				case DT_BLOB:
					datatype = L"Blob";
					break;

				case DT_OBJECT:
					datatype = L"Object";
					break;

				default:
					assert(false); // unsupported type (add it here + destructor)
				};

				s += L"	" + datatype + directionString + L"Pin " + MakePinVarName(pin_vars_dsp[i].second) + L";\n";

				i++;
			}
		}


		//		s +=  L"\npublic:\n" ;
		s +=  L"\n" ;

		// Constructor
		s +=  L"\t" + dsp_module_name + L"()";
		if (useHeader)
		{
			s += L";\n";
		}
		else
		{
			s += L"\n\t{\n";

#ifndef USE_GMPI2
			i = 0;

			for (auto it = Plugs.begin(); it != Plugs.end(); ++it)
			{
				if (!(*it)->isUiPlug())
				{
					std::wstring name = (*it)->getName();
					s += L"\t\tinit( ";
					s += MakePinVarName(pin_vars_dsp[i].second) + L" );\n";
					i++;
				}
			}
#endif
			s += L"\t}\n\n";
		}

		const bool hasStreamingPins = [this]()->bool
			{
				for (auto it = Plugs.begin(); it != Plugs.end(); ++it)
				{
					if (!(*it)->isUiPlug() && (*it)->getDatatype() == DT_FSAMPLE)
					{
						return true;
					}
				}

				return false;
			}();

		if (hasStreamingPins)
		{
			s += L"\tvoid subProcess( int sampleFrames )";
			if (useHeader)
			{
				s += L";";
			}
			else
			{
				s += L"\n\t{\n\t\t// get pointers to in/output buffers.\n";
				i = 0;

				for (auto p : Plugs)
				{
					if (!p->isUiPlug())
					{
						if (p->getDatatype() == DT_FSAMPLE)
						{
							s += L"\t\t";
							//if (p->GetDirection() == DR_IN)
							//{
							//	s += L"const ";
							//}

							t = (L"auto " + MakeVarName(pin_vars_dsp[i].second) + L" = getBuffer(") + MakePinVarName(pin_vars_dsp[i].second) + (L");");
							s += t;
							s += L"\n";
						}

						i++;
					}
				}

				s += L"\n";
				s += L"\t\tfor( int s = sampleFrames; s > 0; --s )\n";
				s += L"\t\t{\n";
				s += L"\t\t\t// TODO: Signal processing goes here.\n";
				s += L"\n\t\t\t// Increment buffer pointers.\n";
				// increment pointers.
				i = 0;

				for (auto it = Plugs.begin(); it != Plugs.end(); ++it)
				{
					// ++in1;
					if (!(*it)->isUiPlug())
					{
						if ((*it)->getDatatype() == DT_FSAMPLE)
						{
							t = (L"\t\t\t++") + MakeVarName(pin_vars_dsp[i].second) + (L";\n");
							s += t;
						}

						++i;
					}
				}
				s += L"\t\t}\n";
				s += L"\t}\n\n";
			}
		}

		s +=  L"\tvoid onSetPins() override";
		if (useHeader)
		{
			s += L";";
		}
		else
		{
			s += L"\n\t{\n";

			i = 0;
			bool commentInserted = false;

			for (auto it = Plugs.begin(); it != Plugs.end(); ++it)
			{
				// ++in1;
				if (!(*it)->isUiPlug())
				{
					if ((*it)->GetDirection() == DR_IN && (*it)->getDatatype() != DT_MIDI)
					{
						if (!commentInserted)
						{
							s += L"\t\t// Check which pins are updated.\n";
							commentInserted = true;
						}

						if ((*it)->getDatatype() == DT_FSAMPLE)
						{
							t = (L"\t\tif( ") + MakePinVarName(pin_vars_dsp[i].second) + (L".isStreaming() )\n");
						}
						else
						{
							t = (L"\t\tif( ") + MakePinVarName(pin_vars_dsp[i].second) + (L".isUpdated() )\n");
						}

						s += t;
						s += L"\t\t{\n\t\t}\n";
					}

					++i;
				}
			}

			// pinSignalOut.setStreaming(true);
			i = 0;
			commentInserted = false;

			for (auto it = Plugs.begin(); it != Plugs.end(); ++it)
			{
				// ++in1;
				if (!(*it)->isUiPlug())
				{
					if ((*it)->GetDirection() == DR_OUT && (*it)->getDatatype() == DT_FSAMPLE)
					{
						if (!commentInserted)
						{
							s += L"\n";
							s += L"\t\t// Set state of output audio pins.\n";
							commentInserted = true;
						}

						t = (L"\t\t") + MakePinVarName(pin_vars_dsp[i].second) + (L".setStreaming(true);\n");
						s += t;
					}

					++i;
				}
			}

			if (hasStreamingPins)
			{
				s += L"\n\t\t// Set processing method.\n";
				s += L"\t\tsetSubProcess(&" + dsp_module_name + L"::subProcess);\n";
			}

//			s += L"\n\t\t// Set sleep mode (optional).\n";
//			s += L"\t\t// setSleep(false);\n";
			s += L"\t}\n";
		}


		s +=  L"};\n\n" ;

		if (!useHeader)
		{
			s += L"namespace\n";
			s += L"{\n";
			
			// with XML in resources.
			// s += L"\tauto r = Register<" + destinationClassName + L">::withId(L\"" + MakeModuleId(plugin_name) + L"\");\n";

			// with XML inline in the cpp
			// auto r = Register<Gain>::withXml(R"XML(
			s += L"auto r = Register<" + destinationClassName + L">::withXml(R\"XML(\n";
			s += ToWstring(pluginXML);
			s += L")XML\");\n";

			s += L"}\n";
		}

		if (useHeader)
		{
			destinationFileName = (destinationProjectFolder / (destinationClassName + L".h")).wstring();
			WriteFileFromString(destinationFileName, s);

			s.clear();
			//--------AUDIO PROCESSING -------------------------------------------------------------
			// include headers
			t = (L"#include \"./") + dsp_module_name + (L".h\"\n\n");
			s += t;
			/*
			// define pin index macros
			i = 0;
			for( auto it = Plugs.begin() ; it != Plugs.end() ; ++it ) {
				if( (*it)->isUiPlug() )
				{
					s += (L"#define ") + MakePN( pin_vars[i].second ) + (L"				");
					t.Format((L"%d\n"), i ); s += t;
					i++;
				}
			}

			s +=  L"\n\n" ;
			*/
			//REGISTER_PLUGIN( AGain, L"AGain" );
			s += L"REGISTER_PLUGIN2 ( " + dsp_module_name + L", L\"" + MakeModuleId(plugin_name) + L"\" );\n";
			s += L"\n";
			// Contructor
			s += dsp_module_name + L"::" + dsp_module_name + L"( )\n";
			s += L"{\n";
			s += L"	// Register pins.\n";
			i = 0;

			for (auto it = Plugs.begin(); it != Plugs.end(); ++it)
			{
				if (!(*it)->isUiPlug())
				{
					std::wstring name = (*it)->getName();

					s += L"	initializePin( ";
					s += MakePinVarName(pin_vars_dsp[i].second) + L" );\n";
					i++;
				}
			}

			s += L"}\n\n";
			// SUB-PROCESS
			s += L"void " + dsp_module_name + L"::subProcess( int sampleFrames )\n{\n";
			s += L"	// get pointers to in/output buffers.\n";
			i = 0;

			for (auto p : Plugs)
			{
				if (!p->isUiPlug())
				{
					if (p->getDatatype() == DT_FSAMPLE)
					{
						s += L"\t";
						if (p->GetDirection() == DR_IN)
						{
							s += L"const ";
						}

						t = (L"float* " + MakeVarName(pin_vars_dsp[i].second) + L" = getBuffer(") + MakePinVarName(pin_vars_dsp[i].second) + (L");");
						s += t;
						s += L"\n";
					}

					i++;
				}
			}

			s += L"\n";
			s += L"	for( int s = sampleFrames; s > 0; --s )\n";
			s += L"	{\n";
			s += L"		// TODO: Signal processing goes here.\n";
			s += L"\n		// Increment buffer pointers.\n";
			// increment pointers.
			i = 0;

			for (auto it = Plugs.begin(); it != Plugs.end(); ++it)
			{
				// ++in1;
				if (!(*it)->isUiPlug())
				{
					if ((*it)->getDatatype() == DT_FSAMPLE)
					{
						t = (L"		++") + MakeVarName(pin_vars_dsp[i].second) + (L";\n");
						s += t;
					}

					++i;
				}
			}

			s += L"	}\n";
			s += L"}\n\n";
			// OnStreamingChange
			s += L"void " + dsp_module_name + L"::onSetPins()\n{\n";
			i = 0;
			bool commentInserted = false;

			for (auto it = Plugs.begin(); it != Plugs.end(); ++it)
			{
				// ++in1;
				if (!(*it)->isUiPlug())
				{
					if ((*it)->GetDirection() == DR_IN && (*it)->getDatatype() != DT_MIDI)
					{
						if (!commentInserted)
						{
							s += L"	// Check which pins are updated.\n";
							commentInserted = true;
						}

						if ((*it)->getDatatype() == DT_FSAMPLE)
						{
							t = (L"	if( ") + MakePinVarName(pin_vars_dsp[i].second) + (L".isStreaming() )\n");
						}
						else
						{
							t = (L"	if( ") + MakePinVarName(pin_vars_dsp[i].second) + (L".isUpdated() )\n");
						}

						s += t;
						s += L"	{\n	}\n";
					}

					++i;
				}
			}

			// pinSignalOut.setStreaming(true);
			i = 0;
			commentInserted = false;

			for (auto it = Plugs.begin(); it != Plugs.end(); ++it)
			{
				// ++in1;
				if (!(*it)->isUiPlug())
				{
					if ((*it)->GetDirection() == DR_OUT && (*it)->getDatatype() == DT_FSAMPLE)
					{
						if (!commentInserted)
						{
							s += L"\n	// Set state of output audio pins.\n";
							commentInserted = true;
						}

						t = (L"	") + MakePinVarName(pin_vars_dsp[i].second) + (L".setStreaming(true);\n");
						s += t;
					}

					++i;
				}
			}

			s += L"\n	// Set processing method.\n";
			s += L"	setSubProcess(&" + dsp_module_name + L"::subProcess);\n";
			s += L"\n	// Set sleep mode (optional).\n";
			s += L"	// setSleep(false);\n";
			s += L"}\n\n";
		}

		const auto filename = destinationClassName + L".cpp";
        destinationFileName = (destinationProjectFolder / filename).wstring();
		WriteFileFromString(destinationFileName, s);

		sourceFilenames.push_back(filename);
	}


	//------------- HELP FILE -------------------------------------------------
	// HTML document header.
	s.clear();
	s += L"<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Transitional//EN\" \"http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd\">\n" ;
	s += L"<link rel=\"stylesheet\" type=\"text/css\" href=\"se_topic.css\"/>\n";
	s += L"<html xmlns=\"http://www.w3.org/1999/xhtml\" >\n";
	s += L"<head>\n";
	s += L"    <title>" + GetName() + L"</title>\n";
	s += L"</head>\n";
	s += L"<body>\n";
	// MODULE NAME
	s += L"<h2>" + GetName() + L"</h2>\n";
	// MODULE DESCRIPTION
	s += L"<p>Does whatever.</p>\n";
	// PIN DESCRIPTIONS
	s += L"<h2>Pins</h2>\n";
	s += L"<ul>\n";
	int i = 0;
	int k = 0;

	for( auto it = Plugs.begin() ; it != Plugs.end() ; ++it )
	{
		if( (*it)->isUiPlug() )
		{
			s += L"	<li><b>" + (pin_vars[i].second) + L"</b></li>\n";
			++i;
		}
		else
		{
			s += L"	<li><b>" + (pin_vars_dsp[k].second) + L"</b></li>\n";
			++k;
		}
	}

	s += L"</ul>\n";
	s += L"</body>\n";
	s += L"</html>\n";
	// WRITE FILE TO DISK.
    destinationFileName = (destinationProjectFolder / (destinationClassName + L".htm")).wstring();
	WriteFileFromString(destinationFileName, s);
	s.clear();

	// CMakeLists.txt
	{
		wstringstream o;
		o <<
			L"cmake_minimum_required(VERSION 3.30)"  L"\n\n"
			L"project(" << base_filename << L")"     L"\n\n"

			L"gmpi_plugin(\n"
			L"	PROJECT_NAME ${PROJECT_NAME}\n";

		if (hasDsp)
		{
			o << L"	HAS_DSP\n";
		}
		if (hasGui)
		{
			o << L"	HAS_GUI\n";
		}
		o << // L" FORMATS_LIST GMPI\n"
		     L"	SOURCE_FILES\n";

		if (hasDsp)
			o << L"	  " << base_filename << L".cpp\n";
		if (hasGui)
			o << L"	  " << base_filename << L"Gui.cpp\n";
		o << L")\n";

		// WRITE FILE TO DISK.
        destinationFileName = (destinationProjectFolder / L"CMakeLists.txt").wstring();
		{
			const auto contents = o.str();
			WriteFileFromString(destinationFileName, contents);
		}

		// Overall CMakeLists.txt
		wstringstream o2;
		o2 <<

LR"XML(cmake_minimum_required(VERSION 3.30)

set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "") 
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum OS X deployment version")
set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "Build architectures for Mac OS X")
set(SE_LOCAL_BUILD FALSE CACHE BOOL "Execute extra build steps for developers machine")

# This is for macOS commandline only, because it uses a single-target generator. for other targets, ref: target_compile_definitions
# this point of this is to ensure NDEBUG macro is set
if(NOT GENERATOR_IS_MULTI_CONFIG)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
endif()

include(FetchContent)

# Download the SynthEdit SDKs
#note: SOURCE_SUBDIR is a subfolder with NO cmake file (so we don't needlessly include the GMPI examples)
FetchContent_Declare(
  gmpi
  GIT_REPOSITORY https://github.com/JeffMcClintock/GMPI
  GIT_TAG origin/main
  SOURCE_SUBDIR Core
)

FetchContent_MakeAvailable(gmpi)

FetchContent_Declare(
  gmpi_ui
  GIT_REPOSITORY https://github.com/JeffMcClintock/gmpi_ui
  GIT_TAG origin/main
  SOURCE_SUBDIR helpers
)
FetchContent_MakeAvailable(gmpi_ui)
set(GMPI_UI_SDK ${gmpi_ui_SOURCE_DIR})

FetchContent_Declare(
  syntheditsdk
  GIT_REPOSITORY https://github.com/JeffMcClintock/SynthEdit_SDK
  GIT_TAG origin/master
  SOURCE_SUBDIR se_sdk3
)

FetchContent_MakeAvailable(syntheditsdk)

project(MyModules)

enable_testing()

set(se_sdk_folder
	${syntheditsdk_SOURCE_DIR}/se_sdk3
    )
set(se_shared_folder
    ${syntheditsdk_SOURCE_DIR}/shared
    )

set(sdk_folder ${se_sdk_folder})

set(GMPI_SDK
	${gmpi_SOURCE_DIR}
    )
	
include_directories(
    ${se_sdk_folder}
    ${se_shared_folder}
    )

set(CMAKE_CXX_STANDARD 20)

add_definitions(-D_UNICODE)
add_definitions(-DSE_TARGET_SEM)

if (MSVC)
    # Floating Point Model: Fast (/fp:fast)
    # Buffer Security Check: No (/GS-)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /fp:fast /GS-")
endif()

if(APPLE)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ffast-math")
endif()

include(${syntheditsdk_SOURCE_DIR}/plugin_helper.cmake)
include(${gmpi_SOURCE_DIR}/gmpi_plugin.cmake)

# Add you own modules here. One folder per module.
add_subdirectory(")XML" << base_filename << L"\")\n";

		// write parent CMakeLists.
        destinationFileName = (destinationFolder / L"CMakeLists.txt").wstring();
		if (!FileExists(destinationFileName))
		{
			WriteFileFromString(destinationFileName, o2.str());
		}
	}

	std::wostringstream oss;
	oss << L"Project Written to :" << destinationProjectFolder;
	Application()->SeMessageBox( oss.str().c_str(), L"", MB_OK|MB_ICONINFORMATION );
#ifdef _WIN32
	ShellExecute(0, L"open", destinationProjectFolder.c_str(), nullptr, nullptr, SW_SHOWDEFAULT);
#endif
}

#if defined( _DEBUG )
int CUG::DebugGetPinIndex(IPlug* pin)
{
	int i = 0;

	for( auto it = Plugs.begin() ; it != Plugs.end() ; ++it )
	{
		if( (*it) == pin )
			return i;

		++i;
	}

	return -1;
}

void CUG::DebugIdentify()
{
	_RPTW1(_CRT_WARN, L"%s", GetName().c_str() );
	CContainer* c = Container();

	while( c )
	{
		_RPTW1(_CRT_WARN, L" in %s", c->GetName().c_str() );
		c = c->Container();
	}

	_RPTW0(_CRT_WARN, L"\n" );
}
#endif

void CUG::AdjustModuleTypePointer()
{
	// on loading, module type information is a cached copy from the project file.
	// update my pointer to the 'live' module data from local filesystem.
	CDocOb::AdjustModuleTypePointer();

	// likewise adjust pin description pointers.
	for( auto& p : Plugs )
	{
		dynamic_cast<CPlug4*>( p )->AdjustDecoratorPointer();
	}
}
