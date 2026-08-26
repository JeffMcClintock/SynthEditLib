#include "PatchManager.h"
// PatchManager.cpp : implementation file
//
/*
	with GIMPI, patch management will be handled by host.
	with VST, plugin needs to manage it.
	This class abstracts the Patch Managment.
*/


#include <algorithm>
#include "Notify_msg.h"

//#define _CRT_SECURE_NO_DEPRECATE // for affectx.h
#ifndef _CRT_SECURE_NO_DEPRECATE
#define _CRT_SECURE_NO_DEPRECATE
#endif
#ifndef _CRT_NONSTDC_NO_DEPRECATE
#define _CRT_NONSTDC_NO_DEPRECATE
#endif

#include <assert.h>
#include <algorithm>
#include <assert.h>
#include <sstream>
#include <fstream>
#include <iomanip>
#include "PatchManager.h"
#include "conversion.h"
#include "datatype_to_id.h"
#include "SeAudioMaster.h"
#include "SynthEditDocBase.h"
#include "Application.h"

#include "plug_description.h"
#include "midi_defs.h"
#include "PatchParameter.h"
#include "ug_base.h"
#include "./ug_container.h"
#include "./dsp_patch_manager.h"
#include "Module_Info3.h"
#include "./PatchParameter_host_generated.h"
#include "modules/se_sdk3/mp_api.h"
#include "modules/shared/voice_allocation_modes.h"
#include "HostControls.h"
#include "modules/se_sdk3/it_enum_list.h"
#include "module_info.h"
#include "tinyxml/tinyxml.h"
#include "SuspendDSP.h"
#include "UG2.h"
#include "../tinyXml2/tinyxml2.h"
#include "Ctl_Slider.h"
#include "PresetReader.h"
#include "modules/shared/PatchCables.h"
#include "mfc_emulation.h"

#ifdef _DEBUG
#include "it_doc_ob_recursive.h"
#endif

// for backward compat
#include "CContainer.h"
#include "Control.h"
#include "it_plug_destinations.h"
#include <regex> 
#include "../se_sdk3_hosting/GuiPatchAutomator3.h"
#include "PropertiesBrowser.h"   // PropertiesViewModel -- see ~CPatchManager()

using namespace std;

#define defaultVoiceAllocationMode 768

CPatchManager::CPatchManager(CContainer* p_container) :
	m_program(0)
	,m_container(p_container)
	,midiChannel(-1)
{
	init_controller_descriptions();
}

CPatchManager::~CPatchManager()
{
	// Registered GUIs must hear about this BEFORE the parameters (and the
	// properties they expose) disappear, or they are left holding a dangling
	// pointer. DeregisterAllGuiPatchAutomators() fixed exactly this shape once
	// already, for GuiPatchAutomator3 -- but it has to be called by hand before
	// each `delete m_patch_manager`, and only ONE of the three call sites in
	// CContainer.cpp remembers to. This destructor is not one of them.
	//
	// Measured 2026-08-25: TIDE-Rack-2026-08-25-165939.ips, EXC_BAD_ACCESS in
	// PmParameterIterator::First(). The container owning this patch manager
	// was deleted while its properties were showing; PropertiesViewModel::
	// currentPatchManager still pointed here, and the next repaint
	// dereferenced it.
	//
	// Fixed HERE rather than at each caller, so no future delete site can
	// forget it -- the class of bug DeregisterAllGuiPatchAutomators() already
	// exists to prevent, closed for its blind spot rather than papered over
	// with a fourth call site.
	//
	// NOT fixed by this: a LEAF module (no patch manager of its own) deleted
	// while its OWN properties are showing. currentPatchManager survives that
	// case untouched, but currentModule does not, and nothing here clears it.
	// Different mechanism, same failure shape -- filed on the same row rather
	// than assumed to be the same bug.
	for (auto g : m_guis2)
	{
		if (auto* props = dynamic_cast<PropertiesViewModel*>(g); props && props->currentPatchManager == this)
		{
			props->currentModule = nullptr;
			props->currentPatchManager = nullptr;
		}
	}

	for( auto& p : m_parameters)
	{
		Container()->Document()->uniqueIdDatabase.Unregister( p );
		delete p;
	}
}

void CPatchManager::TransferParameter( CUG* from, CUG* to )
{
	for(auto& p : m_parameters)
	{
		if(p->module() == from)
		{
			p->setModule( to );
		}
	}
}

PatchParameter_base* CPatchManager::RegisterParameter( CUG* module, parameter_description& descriptor )
{
	PatchParameter_base* patch_param = GetParameter(module, descriptor.id);

	bool isNewParameter = (patch_param == 0);

	if( isNewParameter )
	{
		switch( descriptor.datatype )
		{
		case DT_STRING_UTF8:
			patch_param = new PatchParameter<std::string, MetaData_filename8>(module, descriptor);
			break;

		case DT_TEXT: // wstring
			patch_param = new PatchParameter<std::wstring, MetaData_filename>(module, descriptor);
			break;

		case DT_ENUM:
			patch_param = new PatchParameter<int,MetaData_enum>( module, descriptor );
			break;

		case DT_FLOAT:
			// how does metadata get initialised? !!!!
			patch_param = new PatchParameter<float,MetaData_float>( module, descriptor );
			break;

		case DT_BOOL:
			patch_param = new PatchParameter<bool,MetaData_none>( module, descriptor );
			break;

		case DT_INT:
			patch_param = new PatchParameter<int,MetaData_int>( module, descriptor );
			break;

			// !!! todo remove variable and control from patch-parameter !!!
			// also need class MyBLob overridable contructor taking int, see variable()
		case DT_BLOB:
		case DT_OBJECT:
			patch_param = new PatchParameterBlob( module, descriptor );
			break;

		default:
			assert(!"datatype not supported yet"); // TODO
			break;
		};

		// This can be overriden by user, so only initialized once.
		patch_param->isPrivate = (descriptor.flags & IO_PAR_PRIVATE) != 0;

		patch_param->setModule( module ); // pre 1.1 did not serialise this.
	}

	// init these even if param already exists (module XML may have been updated).
	// hmm, exising modules must be set, else GetParameter( module->Handle()) at top would fail.
	assert( patch_param->module() == module );
	patch_param->setPolyphonic( (descriptor.flags & IO_PAR_POLYPHONIC) != 0 );
	patch_param->setPolyphonicGate( (descriptor.flags & IO_PAR_POLYPHONIC_GATE) != 0 );
	patch_param->setSdk2BackwardCompatibility(module && module->getType()->ModuleTechnology() == MT_SDK2);

	// On Patch-Mem modules, metadata is auto-set from the outgoing value pin.
	// the input pin connected to the patch-mem has blank metadata, if so don't overwrite patch mem metadata
	// when pin has blank metadata.
	if( !descriptor.metaData.empty() )
	{
		switch( descriptor.datatype )
		{
		case DT_STRING_UTF8:
		{
			auto p = dynamic_cast<PatchParameter<std::string, MetaData_filename8>*>(patch_param);
			assert(p);
			p->MetaData()->setFileExt(WStringToUtf8(descriptor.metaData));
		}
		break;

		case DT_TEXT:
			// redundant?
			if (dynamic_cast<PatchParameter<std::wstring, MetaData_filename>*>(patch_param)->MetaData()->getFileExt() != descriptor.metaData)
			{
				assert(false);
			}
			dynamic_cast< PatchParameter<std::wstring, MetaData_filename>* >(patch_param)->MetaData()->setFileExt( descriptor.metaData );
			break;

		case DT_ENUM:
			// redundant?
			if (dynamic_cast< PatchParameter<int, MetaData_enum>* >(patch_param)->MetaData()->getEnumList() != descriptor.metaData)
			{
				assert(false);
			}
			dynamic_cast< PatchParameter<int, MetaData_enum>* >(patch_param)->MetaData()->setEnumList( descriptor.metaData );
			break;
                
            default:
                break;
		};
	}

	if( isNewParameter )
	{
		int handle = Container()->Document()->uniqueIdDatabase.GenerateUniqueHandleValue();
		patch_param->m_automation = descriptor.automation;
		patch_param->setIgnoreProgramChange( (descriptor.flags & IO_IGNORE_PATCH_CHANGE) != 0 );

		patch_param->setHandle(handle);
		Container()->Document()->uniqueIdDatabase.Register( patch_param );
		AddParameter(patch_param);
		patch_param->InitializePatchMemory();

		CContainer* c = dynamic_cast<CContainer*>(module);
		if( c == 0 )
		{
			c = module->Container();
		}
			// All Parent containers need notification. Visible up tree till one has a patch-manager.
		c->NotifyParameterChange( OM_REFRESH_PARAMETERS, 0 );
	}

	if (module)
	{
		auto it = parameterUpgrades.find(module->Handle());

		if (it != parameterUpgrades.end())
		{
			auto info = (*it).second;

			patch_param->m_short_name = info.name;

			int lastPreset = (std::min)((int) info.paramValues.size(), patch_param->getPatchCount());
			for (int presetIdx = 0; presetIdx < lastPreset; ++presetIdx)
			{
				patch_param->SetValue(RawView(info.paramValues[presetIdx]), FT_VALUE, 0, presetIdx);
			}

			// done, delete it.
			parameterUpgrades.erase(it);
		}
	}

	assert( patch_param );
	return patch_param;
}

// A module-owned host-control parameter (oversampling rate/filter, user shared-parameters)
// only exists because a module references it via a pin. When that module is deleted the
// parameter is orphaned; historically it lingered (harmlessly) until the project was saved
// twice. CContainer::RemoveOrphanedHostControls() now culls them on delete and on save by
// passing the set of still-referenced parameters here.
//
// Structural host-controls owned by the container/document (polyphony, voice-allocation,
// presets, patch-cables, ...) are NOT module-owned, so isModuleOwnedHostControl() returns
// false for them and they are never culled.
bool isModuleOwnedHostControl(HostControls hostControlId)
{
	switch (hostControlId)
	{
	case HC_OVERSAMPLING_RATE:
	case HC_OVERSAMPLING_FILTER:
	case HC_USER_SHARED_PARAMETER_INT0:
	case HC_USER_SHARED_PARAMETER_INT1:
	case HC_USER_SHARED_PARAMETER_INT2:
	case HC_USER_SHARED_PARAMETER_INT3:
	case HC_USER_SHARED_PARAMETER_INT4:
		return true;
	default:
		return false;
	}
}

void CPatchManager::RemoveUnreferencedModuleOwnedHostControls(const std::set<PatchParameter_base*>& referenced)
{
	const bool reloadAutomationView = !Container()->Document()->isDeletingContents();

	for (auto it = m_parameters.begin(); it != m_parameters.end(); )
	{
		auto p = *it;
		if (p->is_stateful() && isModuleOwnedHostControl(p->hostControlId_) && referenced.find(p) == referenced.end())
		{
			Container()->Document()->uniqueIdDatabase.Unregister(p);
			it = m_parameters.erase(it);

			if (reloadAutomationView)
				Container()->NotifyParameterChange(OM_REFRESH_PARAMETERS, 0); // update Parameter Details window

			delete p;
		}
		else
		{
			++it;
		}
	}
}

void CPatchManager::UnRegister( CUG* module, int moduleParameterId )
{
	bool reloadAutomationView = !Container()->Document()->isDeletingContents();

	for(auto it = m_parameters.begin() ; it != m_parameters.end() ; )
	{
		auto p = *it;
		if(module == p->module() && (moduleParameterId == -1 || p->ModuleParameterId() == moduleParameterId)) // -1 = unregister all
		{
			module->Document()->uniqueIdDatabase.Unregister( p );
			it = m_parameters.erase(it);

			if( reloadAutomationView )
			{
				// Remove from module.
				module->VO_Notify( OM_REFRESH_PARAMETERS, 0 );

				// remove from container's list.
				module->Container()->NotifyParameterChange( OM_REFRESH_PARAMETERS, 0 );
			}

			delete p; // once WPF has removed references to it.
		}
		else
		{
			++it;
		}
	}
}

#if defined( _DEBUG )
void CPatchManager::DebugDump()
{
#ifdef _WIN32
	_RPT0(_CRT_WARN, "Patch Mem contents.\n" );

	for( auto&p : m_parameters)
	{
		if( p->module() ) // module* not serialised pre SE 1.1
			_RPTN(_CRT_WARN, "mod %d, param# %d Handle=%d\n", p->ModuleHandle(), p->ModuleParameterId(), p->Handle() );
		else
			_RPT1(_CRT_WARN, "mod [blank handle], param %d\n", p->ModuleParameterId() );
	}
#endif
}
#endif

int CPatchManager::getNumPrograms()
{
	return 128;
}

void CPatchManager::SetProgram(int p_program)
{
	if( m_program == p_program ) // clicking framewindow updates preset dropdown..updates this.. causing cascading pointless crap. avoid.
		return;

	m_program = p_program;

	for( auto&p : m_parameters)
	{
		p->SetProgram();

		if (!p->instansiateDsp()
			|| (p->hostControlId_ >= 0 && p->isPolyphonic()) // is a crude method of weeding out VOICE_ACTIVE which crashes if you try to update it from UI
			|| !p->is_stateful()
			)
			continue;

		int effectiveProgram = p_program;

		if (p->getPatchCount() < 128 || p->ignoreProgramChange())
			effectiveProgram = 0;

		for (int voice = 0; voice < p->getVoiceCount(); ++voice)
		{
			p->UpdateDspValue(effectiveProgram, voice);
		}
	}

	UpdateProgramCategory();

	setModified(false);

	Application()->NotifyFast(OM_UPDATE_PRESET_BROWSER_PATCH, (void*) this);
}

void CPatchManager::CopyPatch( int to_patch_lo, int to_patch_hi)
{
	for( auto& p : m_parameters)
	{
		p->CopyPatch( GetProgram(), to_patch_lo, to_patch_hi);
	}
}

int CPatchManager::GetProgram()
{
	return m_program;
}

// Return valid name or blank. This way caller can decide if preset is in use or not.
std::wstring CPatchManager::getProgramNameIndexed( int p_index )
{
	if( p_index == -1 )
		p_index = GetProgram();

	auto it = m_program_names.find(p_index);

	if (it != m_program_names.end())
		return (*it).second;

	return L"";
}

std::wstring CPatchManager::getProgramCategoryIndexed(int p_index)
{
	if (p_index == -1)
		p_index = GetProgram();

	auto pb = GetHostGeneratedParameter(HC_PROGRAM_CATEGORY, false, Container());
	if (pb)
	{
		return pb->GetValueString(FT_VALUE, 0, p_index);
	}

	return L"";
}

// update Preset Category from list
void CPatchManager::UpdateProgramCategory()
{
	auto pb = GetHostGeneratedParameter(HC_PROGRAM_CATEGORY, false, Container());

	if (pb)
	{
		pb->SetValue(RawView(getProgramCategoryIndexed()));
	}
}

// update list from Preset Category
void CPatchManager::UpdateProgramCategoryList()
{
	auto pb = GetHostGeneratedParameter(HC_PROGRAM_CATEGORY, false, Container());

	if (pb)
	{
		updateProgramCategoryListItem( pb->GetValueString() );
	}
}

void CPatchManager::updateProgramCategoryListItem(std::wstring p_category, int p_index)
{
	if (p_index == -1)
		p_index = GetProgram();

	bool autoCreateHostControl = !p_category.empty();

	auto pb = GetHostGeneratedParameter(HC_PROGRAM_CATEGORIES_LIST, autoCreateHostControl, Container());

	if (pb)
	{
		pb->UpdateGui();
	}
}

void CPatchManager::setProgramCategoryIndexed(std::wstring p_category, int p_index)
{
	if (p_index == -1)
		p_index = GetProgram();

	if (auto pb = GetHostGeneratedParameter(HC_PROGRAM_CATEGORY, false, Container()); pb)
	{
		pb->SetValue( RawView( p_category ), FT_VALUE, 0, p_index);

		if (p_index == GetProgram())
			pb->UpdateGui();
	}
}

void CPatchManager::setProgramNameIndexed( std::wstring p_program_name, int p_index)
{
	if( p_index == -1 )
		p_index = GetProgram();

	// When exporting presets to XML, blank names are replaced by default name 'Preset 1" etc. treat these as equal to blanks.
	if (getProgramNameIndexed(p_index) == p_program_name)
	{
		return;
	}

	// remove characters '=' and ',' which screw up patch-names enum list.
	auto pos = p_program_name.find(L"=");

	while( pos != string::npos )
	{
		p_program_name.replace(pos, 1, L"");
		pos = p_program_name.find(L"=");
	}

	pos = p_program_name.find(L",");

	while( pos != string::npos )
	{
		p_program_name.replace(pos, 1, L"");
		pos = p_program_name.find(L",");
	}

	auto it = m_program_names.find(p_index);

	if( it == m_program_names.end() )
	{
		m_program_names.insert( pair <int,std::wstring>( p_index, p_program_name ));
	}
	else
	{
		if( (*it).second.compare( p_program_name ) == 0 ) // Fix stack overflow Cubase SX3.
		{
			return;
		}

		(*it).second = p_program_name;
	}

	// update GUI objects needing Patch name list
	{
		auto pb = GetHostGeneratedParameter(HC_PROGRAM_NAMES_LIST, false, Container());

		if (pb) // is in use
		{
			pb->UpdateGui();
		}
	}

	// update GUI objects needing current Patch name
	if( p_index == GetProgram() )
	{
		auto pb = GetHostGeneratedParameter( HC_PROGRAM_NAME, false, Container() );

		if( pb ) // is in use
		{
			pb->UpdateGui();
		}
	}
}

#if defined( _DEBUG )

// double check that all parameters belong in this patch manager
void CPatchManager::Verify()
{
	for(auto& p : m_parameters)
	{
		//		assert( p->Gui Plug()->UG() );
		// can include?		assert( p->Gui Plug()->isParameterPlug() ); // ensure pin still flagged patch-store (module may have been updated)
		//		if( p->Gui Plug() ) // new-style don't store plug
		{
			//			if( p->Container()->get_patch_manager() != this )
			if( p->getPatchManager() != this )
			{
				_RPTW1(_CRT_WARN, L" p patch mgr = %s\n",  m_container->GetName().c_str() );
				//				_RPT1(_CRT_WARN, ".. should be %s\n", p->Container()->GetName()  );
				assert(false);
			}
		}
	}
}
#endif

// when un-containerizing. 'to' is parent of 'from'
void CPatchManager::ChangeParametersAttachedContainer(CContainer* from, CContainer* to)
{
	for (auto& p : m_parameters)
	{
		if (p->module() == from)
		{
			p->setModule(to);
		}
	}
}

// when modules are moved to another container, or patch selector is removed,
// need to transfer any affected parameters to new patch manager
void CPatchManager::TransferPatchData(CPatchManager* destPatchManager)
{
	for (auto it = m_parameters.begin(); it != m_parameters.end(); )
	{
		auto p = *it;
		CPatchManager* new_pm{};

		if (p->instansiateDsp()) // ignore host-generated (but not non-stateful like scope3).
		{
			// during export-plugins only: shift host-controls like MPE-mode if they are not already present in the plugin container. (else they get missed).
			if(p->hostControlId_ != HC_NONE)
			{
				// does destination already have this HC?
				auto dest_host_control = destPatchManager ? destPatchManager->GetHostGeneratedParameter(p->hostControlId_, false, destPatchManager->m_container) : nullptr;
				if(!dest_host_control)
				{
					// destination didn't have it, just shift it.
					new_pm = destPatchManager;
				}
				else
				{
					// destination did have it. might need to merge some host-controls.
					if(HC_PATCH_CABLES == p->hostControlId_)
					{
						auto& db = Container()->Document()->uniqueIdDatabase;
						const auto patchCount = p->getPatchCount();
						for(int patch = 0; patch < patchCount; ++patch)
						{
							// get current cable list.
							RawView raw = p->GetValue(FT_VALUE, 0, patch);
							SE2::PatchCables cableList(raw);

							SE2::PatchCables myCablesNew;
							bool updated = false;

							// make connections
							for(auto& c : cableList.cables)
							{
								auto from = dynamic_cast<CUG*>(db.HandleToObject(c.fromUgHandle));
								auto to = dynamic_cast<CUG*>(db.HandleToObject(c.toUgHandle));

								if(!from || !to)
									continue;

								// Patch cables belong to the most local patch manager
								if(from->get_patch_manager() != this && from->get_patch_manager() == to->get_patch_manager())
								{
									// move cable to it's correct patchmanager
									updated = true;

									auto destParam = from->get_patch_manager()->GetHostGeneratedParameter(HC_PATCH_CABLES, true, {});
									RawView raw2 = destParam->GetValue(FT_VALUE, 0, patch);
									SE2::PatchCables cableList2(raw2);
									cableList2.push_back(c);

									auto temp = cableList2.Serialise();
									destParam->SetValue(RawView(temp), FT_VALUE, 0, patch);
								}
								else
								{
									// keep cable, it's in the correct patchmanager
									myCablesNew.push_back(c);
								}
							}

							if(updated)
							{
								auto localToPreventTrashedReturnValue = myCablesNew.Serialise();
								p->SetValue(RawView(localToPreventTrashedReturnValue), FT_VALUE, 0, patch);
							}
						}
					}
				}
			}
			else
			{
				CUG* ob = p->module();
				if (ob) // host-controls don't have a module. (except those attached to container)
				{
					new_pm = ob->get_patch_manager();
				}
			}
		}

		if(new_pm && new_pm != this && p->hostControlId_ != HC_PATCH_CABLES)	// if wrong, transfer patch data. note: we already did patch-cables above.
		{
			new_pm->AddParameter(p);
			// and ensure it's set to correct program
			p->SetProgram();
			it = m_parameters.erase(it);
		}
		else
		{
			++it;
		}
	}
}

void CPatchManager::TransferOldPatchData(CUG* p_old, CUG* p_new)
{
	paramUpdate2 paramdata;
	auto patch_param = GetParameter(p_old, 0);
	if (patch_param)
	{
		paramdata.name = patch_param->GetName();
		for(int i = 0 ; i < patch_param->getPatchCount() ; ++i)
			paramdata.paramValues.push_back((std::string)patch_param->GetValue(FT_VALUE));

		int datatype;
		patch_param->GetDatatype(FT_VALUE, &datatype);
		if (datatype == DT_FLOAT || datatype == DT_INT)
		{
			paramdata.rangeHi = (std::string)patch_param->GetValue(FT_RANGE_HI);
			paramdata.rangeLo = (std::string)patch_param->GetValue(FT_RANGE_LO);
		}

		parameterUpgrades.insert({ p_new->Handle(), paramdata });
	}
}


void CPatchManager::AddParameter(PatchParameter_base* p_param )
{
	// check parameter has correct module handle.
	//assert( p_param->Gui Plug()->UG()->Handle() == p_param->ModuleHandle() );
	p_param->setPatchManager(this);
	bool handleInUse = Container()->Document()->uniqueIdDatabase.HandleInUse( p_param );

	// When pasting need to give paramter a new ID.
	if( handleInUse )
	{
//#if defined( _DEBUG )
//		int old_id = p_param->Handle();
//#endif
		Container()->Document()->uniqueIdDatabase.setHandleAutoGenerated( p_param );
		assert( p_param->Handle() >= 0 );
		assert( GetParameter(p_param->Handle()) == 0 );

//		_RPT3(_CRT_WARN, " %x Generated new parameter ID %x (was %x)\n", (intptr_t) p_param, p_param->Handle(), old_id );
	}

	m_parameters.push_back(p_param);

	assert( Container()->Document()->uniqueIdDatabase.HandleToObject( p_param->Handle() ) == p_param );
}

// find by unique ID (fast)
PatchParameter_base* CPatchManager::GetParameter(int p_handle  )
{
	// use global handle database to find parameter.
	return dynamic_cast<PatchParameter_base*>(Container()->Document()->uniqueIdDatabase.HandleToObjectWithNull(p_handle));
}

// find by module & pin id
PatchParameter_base* CPatchManager::GetParameter( CUG* module, int moduleParameterId )
{
	// prevent accedental return of 'Program' parameter which has id=-1.
	if( moduleParameterId < 0 )
	{
		return 0;
	}

	for(auto& p : m_parameters)
	{
		if( p->module() == module && p->ModuleParameterId() == moduleParameterId )
			return p;
	}

	return 0;
}

// host generated parameters don't belong to any UG, they are tagged with patch manager's container or in some cases parent container.
PatchParameter_base* CPatchManager::GetHostGeneratedParameter(HostControls hostControlId, bool auto_create, CContainer* parentContainer, const wchar_t* userDefinedName)
{
	CContainer* parameterContainer{};

	// Most host controls 'belong' to the Patch Automator, however a handfull apply to the voice-control container.
	if( AttachesToVoiceContainer(hostControlId) )
	{
		parameterContainer = parentContainer->getVoiceControlContainer();
	}
	else if (AttachesToParentContainer(hostControlId))
	{
		parameterContainer = parentContainer;
	}
	
	for(auto& p : m_parameters)
	{
		if (p->hostControlId_ == hostControlId && (parameterContainer == nullptr || p->module() == parameterContainer) )
		{
			return p;
		}
	}

	if( !auto_create )
		return {};

	// HOST_GENERATED_PARAMETERS
	PatchParameter_base* p = {};
	bool need_initialise = true;
	bool stateful = false; // prevent serialisation in most cases.

	switch( hostControlId )
	{
	case -1:
		return 0;
		break;

	case HC_USER_SHARED_PARAMETER_INT0:
	case HC_USER_SHARED_PARAMETER_INT1:
	case HC_USER_SHARED_PARAMETER_INT2:
	case HC_USER_SHARED_PARAMETER_INT3:
	case HC_USER_SHARED_PARAMETER_INT4:
//	case HC_MAX_LATENCY_COMPENSATION:
	{
			// Dynamically add parameters.
			parameter_description pd;
			pd.datatype = DT_INT;
			pd.name = userDefinedName; // not part of identity anymore. Didn't work in VST3.
			pd.defaultValue = L"0";
			pd.flags = IO_IGNORE_PATCH_CHANGE | IO_PARAMETER_PERSISTANT; // | IO_PAR_PRIVATE;
			pd.automation = GetHostControlAutomation(hostControlId);

			auto pp = new PatchParameter<int, MetaData_int>(Container(), pd);
//			pp->setPlugName(userDefinedName); // Long Name.

			p = pp;
			pp->MetaData()->setRangeLo(1);
			pp->MetaData()->setRangeHi(100);
			p->hostControlId_ = hostControlId;
			need_initialise = stateful = true;
		}
		break;

	case HC_VOICE_AFTERTOUCH:			// L"Voice/Aftertouch";ControllerType::PolyAftertouch << 24;
		need_initialise = true; // seems odd all the below are initialized to garbage? seemed to still result in garbage in DSP.
		// deliberate fall-thru.
		[[fallthrough]];
	case HC_CHANNEL_PRESSURE:
	case HC_VOICE_PITCH:				// L"Voice/Pitch";ControllerType::Pitch << 24;
	case HC_VOICE_VELOCITY_KEY_ON:		// L"Voice/VelOn";ControllerType::VelocityOn << 24;
	case HC_VOICE_VELOCITY_KEY_OFF:		// L"Voice/VelOff";ControllerType::VelocityOff << 24;
	case HC_VOICE_TRIGGER:				// L"Voice/Trigger";ControllerType::Trigger << 24;
	case HC_VOICE_ACTIVE:				// L"Voice/Active";ControllerType::Active << 24;
	case HC_HOLD_PEDAL:					// L"Hold Pedal";(ControllerType::CC << 24) | 64;
	case HC_TIME_QUARTER_NOTE_POSITION:	// L"Time/SongPosition";ControllerType::SongPosition << 24;
	case HC_TIME_BPM:					// L"Time/BPM"; //ControllerType::BPM << 24;
	case HC_VOICE_GATE:					// L"Voice/Gate";ControllerType::Gate << 24;
	case HC_PITCH_BENDER:				// L"Bender";ControllerType::Bender << 24;
	case HC_PLUGIN_UI_SCALE:
	case HC_TIME_BAR_START:				// 
	case HC_VOICE_VOLUME:				// 
	case HC_VOICE_PAN:					// 
	case HC_VOICE_PITCH_BEND:			// 
	case HC_VOICE_VIBRATO:				// 
	case HC_VOICE_EXPRESSION:			// 
	case HC_VOICE_BRIGHTNESS:			// 
	case HC_VOICE_USER_CONTROL0:		// 
	case HC_VOICE_USER_CONTROL1:		// 
	case HC_VOICE_USER_CONTROL2:		// 
	case HC_VOICE_PORTAMENTO_ENABLE:
	case HC_PORTAMENTO:
	case HC_GLIDE_START_PITCH:
	case HC_BENDER_RANGE:
	{
		PatchParameter<float,MetaData_float>* pp;
		p = pp = new PatchParameter<float,MetaData_float>();
		p->m_short_name = GetHostControlName(hostControlId);
		pp->MetaData()->setRangeHi( 10.0f );
		p->m_automation = GetHostControlAutomation(hostControlId);
	}
	break;

	case HC_PATCH_COMMANDS:
	case HC_SUB_PATCH_COMMANDS:
		p = new PatchParameter_host_PatchCommands();
		need_initialise = false;
		break;

	case HC_MIDI_CHANNEL:
		p = new PatchParameter_host_MidiChannelIn();
		need_initialise = false;
		break;

	case HC_PROGRAM_NAMES_LIST:
		p = new PatchParameter_host_ProgramNamesList();
		need_initialise = false;
		break;

	case HC_PROGRAM:
		p = new PatchParameter_host_Program();
		need_initialise = false;
		break;

	case HC_PROGRAM_NAME:
		p = new PatchParameter_host_ProgramName();
		need_initialise = false;
		break;

	case HC_PROGRAM_CATEGORIES_LIST:
		p = new PatchParameter_host_ProgramCategoriesList();
		need_initialise = false;
		break;

	case HC_PROGRAM_CATEGORY:
	{
		auto pp = new PatchParameter<std::wstring, MetaData_none>();
		p = pp;
		p->m_short_name = GetHostControlName(hostControlId);
		p->setIgnoreProgramChange(false);
		stateful = true;
	}
	break;

	case HC_VOICE_VIRTUAL_VOICE_ID:			//  L"Voice/VirtualVoiceId";ControllerType::VirtualVoiceId << 24;
	{
		assert(false); // not ever needed? (controlled by VoiceList).
	}
	break;

	// !!! Don't really need host control enumeration if they are all going to be tied uniquely to a controller too.
	case HC_TIME_NUMERATOR:
	case HC_TIME_DENOMINATOR:
	{
		PatchParameter<int, MetaData_int>* pp;
		p = pp = new PatchParameter<int, MetaData_int>();
		pp->MetaData()->setRangeHi(64);
		p->m_short_name = GetHostControlName(hostControlId);
		p->m_automation = GetHostControlAutomation(hostControlId);
	}
	break;

	case HC_DIAGNOSTIC_FLAGS: // compatibility w Waves
	{
		PatchParameter<int, MetaData_int>* pp;
		p = pp = new PatchParameter<int, MetaData_int>();
		p->m_short_name = GetHostControlName(hostControlId);
		p->m_automation = GetHostControlAutomation(hostControlId);
	}
	break;

	case HC_PROCESS_RENDERMODE:
	{
		PatchParameter<int, MetaData_int>* pp;
		p = pp = new PatchParameter<int, MetaData_int>();
		pp->MetaData()->setRangeHi(1);
		p->m_short_name = GetHostControlName(hostControlId);
		p->m_automation = GetHostControlAutomation(hostControlId);
	}
	break;

	case HC_CLEAR_TAILS:
	{
		PatchParameter<int, MetaData_int>* pp;
		p = pp = new PatchParameter<int, MetaData_int>();
		p->m_short_name = GetHostControlName(hostControlId);
		p->m_automation = GetHostControlAutomation(hostControlId);
	}
	break;

	case HC_SNAP_MODULATION__DEPRECATED:
	{
		PatchParameter<int, MetaData_int>* pp;
		p = pp = new PatchParameter<int, MetaData_int>();
		pp->MetaData()->setRangeHi(7);
		p->m_short_name = GetHostControlName(hostControlId);
		p->m_automation = GetHostControlAutomation(hostControlId);
	}
	break;

	case HC_VOICE_ALLOCATION_MODE:
		{
			// Dynamically add parameters.
			parameter_description pd;
			pd.datatype = DT_INT;
			pd.name = GetHostControlName(hostControlId);
			pd.defaultValue = IntToString(defaultVoiceAllocationMode);
			pd.flags = IO_IGNORE_PATCH_CHANGE|IO_PARAMETER_PERSISTANT|IO_PAR_PRIVATE;
			pd.automation = GetHostControlAutomation(hostControlId);

			//auto pp = new PatchParameter<int,MetaData_int>(Container(), pd);
			assert(parameterContainer);
			auto pp = new PatchParameter<int, MetaData_int>(parameterContainer, pd);

			p = pp;
			pp->MetaData()->setRangeLo( 1 );
			pp->MetaData()->setRangeHi( 128 );
			p->hostControlId_ = hostControlId;
			need_initialise = stateful = true;
		}
		break;

	case HC_POLYPHONY:
		{
			// Dynamically add parameters.
			parameter_description pd;
			pd.datatype = DT_INT;
			pd.name = GetHostControlName(hostControlId);
			pd.defaultValue = IntToString(defaultPolyphony);
			pd.flags = IO_IGNORE_PATCH_CHANGE|IO_PARAMETER_PERSISTANT|IO_PAR_PRIVATE;
			pd.automation = GetHostControlAutomation(hostControlId);

//			auto pp = new PatchParameter<int,MetaData_int>(Container(), pd);
			assert(parameterContainer);
			auto pp = new PatchParameter<int, MetaData_int>(parameterContainer, pd);

			p = pp;
			pp->MetaData()->setRangeLo( 1 );
			pp->MetaData()->setRangeHi( 128 );
			need_initialise = stateful = true;
		}
		break;

	case HC_POLYPHONY_VOICE_RESERVE:
		{
			// Dynamically add parameters.
			parameter_description pd;
			pd.datatype = DT_INT;
			pd.name = GetHostControlName(hostControlId);
			pd.defaultValue = IntToString(defaultPolyphonyReserve);
			pd.flags = IO_IGNORE_PATCH_CHANGE|IO_PARAMETER_PERSISTANT|IO_PAR_PRIVATE;
			pd.automation = GetHostControlAutomation(hostControlId);

//			auto pp = new PatchParameter<int, MetaData_int>(Container(), pd);
			assert(parameterContainer);
			auto pp = new PatchParameter<int, MetaData_int>(parameterContainer, pd);
			p = pp;
			pp->MetaData()->setRangeLo( 0 );
			pp->MetaData()->setRangeHi( 128 );
			need_initialise = stateful = true;
		}
		break;

	case HC_TIME_TRANSPORT_PLAYING:
	{
		PatchParameter<bool,MetaData_none>* pp;
		p = pp = new PatchParameter<bool,MetaData_none>();
		p->m_short_name = GetHostControlName(hostControlId);
		p->m_automation = GetHostControlAutomation(hostControlId);
	}
	break;

	case HC_SILENCE_OPTIMISATION:
	{
		parameter_description pd;
		pd.datatype = DT_BOOL;
		pd.name = GetHostControlName(hostControlId);
		pd.defaultValue = L"1";
		pd.flags = IO_IGNORE_PATCH_CHANGE | IO_PARAMETER_PERSISTANT | IO_PAR_PRIVATE;
		pd.automation = GetHostControlAutomation(hostControlId);

		p = new PatchParameter<bool, MetaData_none>(Container(), pd);
		need_initialise = stateful = true;
	}
	break;

	case HC_PROGRAM_MODIFIED:
	case HC_PROCESS_BYPASS:
	case HC_PROCESSOR_OFFLINE:
	{
		parameter_description pd;
		pd.datatype = DT_BOOL;
		pd.name = GetHostControlName(hostControlId);
		pd.flags = IO_IGNORE_PATCH_CHANGE | IO_PAR_PRIVATE;
		pd.automation = GetHostControlAutomation(hostControlId);

		p = new PatchParameter<bool, MetaData_none>(Container(), pd);
		need_initialise = true;
		stateful = false;
	}
	break;

	case HC_OVERSAMPLING_RATE:
		{
			parameter_description pd;
			pd.datatype = DT_ENUM;
			//pd.metaData = L"4x Under=-4,2x Under=-2,Off=0,2x=2,3x=3,4x=4,8x=8,16x=16,32x=32";
			if (parentContainer->Container() == nullptr)
			{
				pd.metaData = L"N/A";
			}
			else
			{
//				pd.metaData = L"Off=0,2x=2,3x=3,4x=4,8x=8,16x=16,32x=32"; // 3x seemed jittery buggy
				pd.metaData = L"Off=0,2x=2,4x=4,8x=8,16x=16,32x=32";
			}
			pd.name = GetHostControlName(hostControlId);
			pd.defaultValue = L"0";
			pd.flags = IO_IGNORE_PATCH_CHANGE|IO_PARAMETER_PERSISTANT|IO_PAR_PRIVATE;
			pd.automation = GetHostControlAutomation(hostControlId);

			auto pp = new PatchParameter<int,MetaData_enum>( parentContainer, pd); // * Note parentContainer, not patch-automator one *
			pp->MetaData()->setEnumList( pd.metaData );
			p = pp;
			need_initialise = stateful = true;
		}
		break;

	case HC_OVERSAMPLING_FILTER:
		{
			parameter_description pd;
			pd.datatype = DT_ENUM;
			pd.name = L"Oversampling Filter Poles";
			// poles < 10 = Elliptic IIR Filter.
			// Poles 10 - 19 = FIR Sinc - sample-rate independent quality setting.
			// Poles > 20 = literal number of taps.
			pd.metaData = L"3=3,5=5,7=7,9=9,FIR-Low=13,FIR-Med=14,FIR-Hi=15,FIR-Ultra=16"; // Elliptic ripple 0 at DC only for odd-number poles. (important for oversampled control signals).
			pd.defaultValue = L"5";
			pd.flags = IO_IGNORE_PATCH_CHANGE|IO_PARAMETER_PERSISTANT|IO_PAR_PRIVATE;
			pd.automation = ControllerType::None;

			//p = get_patch_manager()->Register Parameter(	this, pd );
			auto pp = new PatchParameter<int,MetaData_enum>( parentContainer, pd); // * Note parentContainer, not patch-automator one *
			pp->MetaData()->setEnumList( pd.metaData );
			p = pp;
			need_initialise = stateful = true;
		}
		break;

	case HC_PATCH_CABLES:
	{
		parameter_description pd;
		pd.datatype = DT_BLOB;
		pd.name = L"Patch Cables";
		pd.flags = IO_PARAMETER_PERSISTANT | IO_PAR_PRIVATE;
		pd.automation = ControllerType::None;

		auto pp = new PatchParameter<MpBlob, MetaData_none>();
		p = pp;
		/*need_initialise =*/ stateful = true;
		p->setIgnoreProgramChange(false);
	}
	break;
	
	case HC_MPE_MODE:
	{
		parameter_description pd{};
		pd.datatype = DT_ENUM;
		pd.name = L"MPE Mode";
		pd.metaData = L"Auto, MPE Off, MPE On";
		pd.defaultValue = L"0";
		pd.flags = IO_IGNORE_PATCH_CHANGE | IO_PARAMETER_PERSISTANT | IO_PAR_PRIVATE;
		pd.automation = ControllerType::None;

		auto pp = new PatchParameter<int, MetaData_enum>(Container(), pd);
		pp->MetaData()->setEnumList(pd.metaData);
		p = pp;
		need_initialise = stateful = true;
	}
	break;
	default:
		assert(false);
		return 0;
		break;
	};

	p->setPolyphonic(HostControlisPolyphonic(hostControlId));

	// A couple of exceptions to the rules.
	switch( hostControlId )
	{
		case HC_VOICE_GATE:
		{
			p->setPolyphonicGate( true );
		}
		break;

		case HC_CHANNEL_PRESSURE:
		{
			((PatchParameter<float,MetaData_float>*) p)->MetaData()->setRangeHi(  1.0f );
		}
		break;

		case HC_PITCH_BENDER:
		{
			((PatchParameter<float,MetaData_float>*) p)->MetaData()->setRangeHi(  1.0f );
			((PatchParameter<float,MetaData_float>*) p)->MetaData()->setRangeLo( -1.0f );
		}
		break;

		case HC_VOICE_PITCH_BEND:
		{
			((PatchParameter<float,MetaData_float>*) p)->MetaData()->setRangeHi(  10.0f );
			((PatchParameter<float,MetaData_float>*) p)->MetaData()->setRangeLo( -10.0f );
		}
		break;

		case HC_BENDER_RANGE:
		{
			constexpr float maxRPN = 0x3FFF;
			constexpr float maxPitchBend = maxRPN / (float) 0x80;

			((PatchParameter<float, MetaData_float>*) p)->MetaData()->setRangeHi(maxPitchBend); // 127.9921875f); // Semitones.
			float defaultval = 2.0f;
			p->SetValue(RawView(defaultval), FT_DEFAULT);
			stateful = true;
		}
		break;

		case HC_PLUGIN_UI_SCALE:
		{
			((PatchParameter<float, MetaData_float>*) p)->MetaData()->setRangeHi(400.0f);
			((PatchParameter<float, MetaData_float>*) p)->MetaData()->setRangeLo(10.0f);
			const float defaultval = 1.0f;
			p->SetValue(RawView(defaultval), FT_DEFAULT);
		}
		break;

		case HC_VOICE_PAN:
		{
			( ( PatchParameter<float, MetaData_float>* ) p )->MetaData()->setRangeHi(5.0f);
			( ( PatchParameter<float, MetaData_float>* ) p )->MetaData()->setRangeLo(-5.0f);
		}
		break;

		case HC_PORTAMENTO:
		{
			stateful = true;
		}
		break;

		case HC_PATCH_COMMANDS:
		{
			std::wstring t(L"Copy Patch=1,Load Preset,Save Preset,Load Bank,Save Bank,Load MIDI,Save MIDI");
			p->SetValue(RawView(t), FT_ENUM_LIST);
			p->m_short_name = GetHostControlName(hostControlId);
		}
		break;

		case HC_SUB_PATCH_COMMANDS:
		{
			std::wstring t(L"Save Sub-Preset=20,Load Sub-Preset"); // 22 = Load with relaxed parameter matching.
			p->SetValue(RawView(t), FT_ENUM_LIST);
			p->m_short_name = GetHostControlName(hostControlId);
		}
		break;

		case HC_PROCESSOR_OFFLINE:
		{
			const bool defaultval{ true };
			p->SetValue(RawView(defaultval), FT_DEFAULT);
		}
		break;

        default:
            break;
	};

	p->setModule(parameterContainer);
	
//	p->setPlugName( GetHostControlName(hostControlId) );

	p->setStateful(stateful);
	p->hostControlId_ = hostControlId;

	// Prevent ProgramNamesList etc showing on VST automation list.
	p->isPrivate = true;

	if( need_initialise )
	{
		p->InitializePatchMemory();
	}

	switch (hostControlId)
	{
		case HC_VOICE_PITCH:
		{
			// Initialise pitch to western default tuning.
			const int patch = 0;
			const float middleA = 69.0f;
			const float notesPerOctave = 12.0f;
			for (int key = 0; key < 128; ++key)
			{
				float pitch = 5.0f + ((static_cast<float>(key) - middleA) / notesPerOctave);

				p->patchMemory[key]->SetValue(&pitch, RawSize(pitch), patch);
			}
		}
		break;

		default:
			break;
	};

	Container()->Document()->uniqueIdDatabase.setHandleAutoGenerated(p, !stateful);
	AddParameter(p);

	if( parentContainer ) // When loading project, upgrade may call in here with parentContainer nullptr.
	{
		// All Parent containers need notification. Visible up tree till one has a patch-manager.
		parentContainer->NotifyParameterChange( OM_REFRESH_PARAMETERS, 0 );
	}

	return p;
}

bool ParametergreaterHandle(PatchParameter_base* p1, PatchParameter_base* p2)
{
	return p1->Handle() < p2->Handle();
}

void CPatchManager::ExportXml(TiXmlElement* XmlParent, ExportFormatType targetType )
{
	if( targetType == SAT_SUBCONTROLS_GUI )
		return;

	// Patchmanager settings.
	TiXmlElement* PatchManagerElement = new TiXmlElement( "PatchManager" );
	XmlParent->LinkEndChild( PatchManagerElement );
	{
//		int v = GetProgram();
//		if (v != 0 && targetType == SAT_SYNTHEDIT_DSP) //!savingForPlugin ) // On Waves only one program (as far as this is concerned).
//			PatchManagerElement->SetAttribute("Program", v);

		if (midiChannel != -1)
			PatchManagerElement->SetAttribute("MidiChannel", midiChannel);
	}

	TiXmlElement* parameters_xml = new TiXmlElement( "Parameters" );
	PatchManagerElement->LinkEndChild( parameters_xml );

	// Sort for export consistancy.
	list<PatchParameter_base*> sortedParameters;
	for(auto& p : m_parameters)
	{
		sortedParameters.push_back(p);
	}

	if (targetType == SAT_VST3_PARAMETERS)
	{
		// Order saved seems to determine DAW display order (despite ID).
		// Sort by VST Param ID, Name, Handle.
		sortedParameters.sort(
			[](const PatchParameter_base* a, const PatchParameter_base* b) -> bool
			{
				if (a->GetVstParameterNumber() != b->GetVstParameterNumber())
					return a->GetVstParameterNumber() < b->GetVstParameterNumber();

				if (a->GetName() != b->GetName())
					return a->GetName() < b->GetName();

				return a->Handle() > b->Handle();
		});
	}
	else
	{
		sortedParameters.sort(ParametergreaterHandle);
	}

	// Parameters settings and patch-memory.
	for(auto&p : sortedParameters)
	{
		p->ExportXml(parameters_xml, targetType);
	}
}

void CPatchManager::Export(tinyxml2::XMLElement* container_xml, ExportFormatType targetType)
{
	if (targetType == SAT_SUBCONTROLS_GUI || targetType == SAT_SYNTHEDIT_GUI_PANEL)
		return;

	// Patchmanager settings.
	auto patchManager_xml = container_xml->GetDocument()->NewElement("PatchManager");
	container_xml->LinkEndChild(patchManager_xml);

	{
		//int v = GetProgram();
		//if (v != 0 && targetType == SAT_SYNTHEDIT_DSP) // On Waves only one program (as far as this is concerned).
		//	patchManager_xml->SetAttribute("Program", v);

		if (midiChannel != -1)
			patchManager_xml->SetAttribute("MidiChannel", midiChannel);
	}

	// Program Names
	if (targetType == SAT_SYNTHEDIT_DOCUMENT)
	{
		auto programNamesE = container_xml->GetDocument()->NewElement("ProgramNames");
		patchManager_xml->LinkEndChild(programNamesE);
		for (auto& it : m_program_names)
		{
			auto nameE = container_xml->GetDocument()->NewElement("Name");
			nameE->SetAttribute("idx", it.first);
			nameE->SetAttribute("text", WStringToUtf8(it.second).c_str());

			programNamesE->LinkEndChild(nameE);
		}
	}

	auto parameters_xml = container_xml->GetDocument()->NewElement("Parameters");

	// Sort for export consistancy.
	std::vector<PatchParameter_base*> sortedParameters;
	for (auto& p : m_parameters)
	{
		sortedParameters.push_back(p);
	}

	std::sort(sortedParameters.begin(), sortedParameters.end(), ParametergreaterHandle);

	// Parameters settings and patch-memory.
	for (auto p : sortedParameters)
	{
		if (p->is_saved_in_project() && (!CDocOb::serialise_copy_mode || p->IsCopyTagged()))
			p->Export(parameters_xml, targetType);
	}

	if(parameters_xml->FirstChildElement())
	{
		patchManager_xml->LinkEndChild(parameters_xml);
	}
	else
	{
		container_xml->GetDocument()->DeleteNode(parameters_xml);
	}
}

void CPatchManager::Import(tinyxml2::XMLElement* patchmanager_xml, ExportFormatType targetType, std::vector< std::pair<PatchParameter_base*, int>>& parameterModuleHandles)
{
	// Program Names
	if (targetType == SAT_SYNTHEDIT_DOCUMENT)
	{
		m_program_names.clear();

		auto programNamesE = patchmanager_xml->FirstChildElement("ProgramNames");
		if (programNamesE)
		{
			for (auto nameE = programNamesE->FirstChildElement("Name"); nameE; nameE = nameE->NextSiblingElement("Name"))
			{
				int idx{};
				nameE->QueryIntAttribute("idx", &idx);
				m_program_names[idx] = Utf8ToWstring(nameE->Attribute("text"));
			}
		}
	}

	auto parameters_xml = patchmanager_xml->FirstChildElement("Parameters");
	if(!parameters_xml)
	{
		return;
	}

	for(auto parameter_xml = parameters_xml->FirstChildElement("param"); parameter_xml; parameter_xml = parameter_xml->NextSiblingElement("param"))
	{
		int type{};
		int moduleHandle{};

		parameter_xml->QueryIntAttribute("type", &type);
		parameter_xml->QueryIntAttribute("module", &moduleHandle);

		auto parameter = reinterpret_cast<PatchParameter_base*>(PersistanceFactory::Instance()->CreateObject(type));
		parameter->setPatchManager(this);
		parameter->Import(parameter_xml, targetType);
		m_parameters.push_back(parameter);

		if(moduleHandle)
		{
			parameterModuleHandles.push_back(std::make_pair(parameter, moduleHandle));
		}
	}
}

void CPatchManager::InitModulePointers(std::map<int32_t, CUG*>& uniqueIds, std::vector< std::pair<PatchParameter_base*, int>>& parameterModuleHandles)
{
	for(auto& mh : parameterModuleHandles)
	{
		auto it = uniqueIds.find(mh.second);
		assert(it != uniqueIds.end());
		mh.first->setModule((*it).second);
	}
}

// Waves parameters ordered by vstParamterNumber, stateful, read_only, name, handle.
void CPatchManager::ExportGetSortedParameters(std::list<PatchParameter_base*>& sortedList, ExportFormatType targetType)
{
	for(auto& p : m_parameters)
	{
		if( p->doesExportToPlugin(targetType) )
		{
			sortedList.push_back( p );
		}
	}

    sortedList.sort(
        [](const PatchParameter_base* p1p, const PatchParameter_base* p2p) -> bool
           {
               auto& p1 = *p1p;
               auto& p2 = *p2p;
               
                int userOrder1 = p1.GetVstParameterNumber();
                int userOrder2 = p2.GetVstParameterNumber();

                // special cases.
                // -1 = "don't care" - sort to end.
                // x < -1000 = sort to end reversed.
                if( userOrder1 == -1 )
                {
                    userOrder1 = INT_MAX;
                }
                else
                {
                    if( userOrder1 <= -1000 )
                    {
                        userOrder1 = 10000 + (-1000 - userOrder1);
                    }
                }

                if( userOrder2 == -1 )
                {
                    userOrder2= INT_MAX;
                }
                else
                {
                    if( userOrder2 <= -1000 )
                    {
                        userOrder2 = 10000 + (-1000 - userOrder2);
                    }
                }

                if( userOrder1 != userOrder2 )
                {
                    return userOrder1 < userOrder2;
                }

                // Put private parameters last. (Polyphony, Voice control etc)
                if( p1.is_private() != p2.is_private() )
                {
                    return /*p1->is_private() < */ p2.is_private();
                }

                // Put non-stateful parameters last. (Program names, Voice control etc)
                if( (bool) p1.isStateful != (bool) p2.isStateful )
                {
                    return (bool) p1.isStateful;
                }

                // Some hosts can only automate first 16 params, so put read-only ones last
                if( p1.is_read_only() != p2.is_read_only() )
                {
                    return p2.is_read_only();
                }

                // Name Alphabetic.
                if( p1.GetName() != p2.GetName() )
                {
                    return p1.GetName() < p2.GetName();
                }

                assert(p1.Handle() != p2.Handle());
                return p1.Handle() < p2.Handle();
            }
        );
}

// Assign VST parameter indexs.
void CPatchManager::ExportAssignParamIndexes(ExportFormatType targetType)
{
	// Query user-specified indexs. 'auto' ones will go after the highest.
	int highestIndex = -1;

	for (auto p : m_parameters)
	{
		bool isPrivate;
		p->IsPrivate(&isPrivate);
		if (!isPrivate)
		{
			highestIndex = max(highestIndex, p->GetVstParameterNumber());
		}
	}

	// Assign index in order they appear in plugin.
	std::list<PatchParameter_base*> sortedList;
	ExportGetSortedParameters(sortedList, targetType);

	std::vector<int> uniqueIndicies;

//	_RPT0(_CRT_WARN, "PARAMETERS ---------------------------------\n" );
	for (auto p : sortedList)
	{
		const auto parameterIndex = p->GetVstParameterNumber();

		// AUTO or duplicate?
		if(parameterIndex == -1 || std::find(uniqueIndicies.begin(), uniqueIndicies.end(), parameterIndex) != uniqueIndicies.end())
		{
			p->SetVstParameterNumber( ++highestIndex );
		}

		uniqueIndicies.push_back(p->GetVstParameterNumber());
	}
	_RPT0(_CRT_WARN, "--------------------------------------------------\n" );
}
	
// for JUCE
/*
bool PatchParameter_base::doesExportToPlugin(ExportFormatType targetType)
{
	// Identify scope parameters (first trace only at this point).
	int datatype;
	GetDatatype( FT_VALUE, &datatype );

	bool isScope = isPolyphonic() && datatype == DT_BLOB && moduleParameterId_ == 0;

	bool dont_export = hostControlId_ == HC_OVERSAMPLING_RATE || hostControlId_ == HC_OVERSAMPLING_FILTER || hostControlId_ == HC_VOICE_ALLOCATION_MODE;

	bool lIsPrivate;
	IsPrivate(&lIsPrivate);

	return (isScope || ( is_stateful() && instansiateDsp() && !isPolyphonic() && !dont_export )) && !lIsPrivate;
}
*/
std::map<int32_t, paramSummary> CPatchManager::ExportParameterEnums()
{
	map<string, int> uniqueNames;
	std::map<int32_t, paramSummary> enums;

	for (bool isprivate : {false, true}) // prioritize public parameters
	{
		for (auto p : m_parameters)
		{
			const bool dont_export = p->hostControlId_ == HC_OVERSAMPLING_RATE || p->hostControlId_ == HC_OVERSAMPLING_FILTER || p->hostControlId_ == HC_VOICE_ALLOCATION_MODE;
			if (p->is_private() != isprivate || p->m_short_name.empty() || !p->instansiateDsp() || p->isPolyphonic() || dont_export)
				continue;

			// Generate parameter enumeration.
			wstring paramName = Uppercase(p->m_short_name);
			MakeLegalVariableName(paramName);
			const auto paramName_UTF8 = WStringToUtf8(paramName);

			// Find a unique variant by appending _2, _3, ... to the *base* name. NOTE: build each
			// candidate from paramName_UTF8 fresh - don't feed the suffixed result back in, or the
			// suffixes accumulate (FOO, FOO_2, FOO_2_3, FOO_2_3_4, ...).
			std::string uniqueName;
			for (int i = 1; ; ++i)
			{
				std::ostringstream oss;
				oss << paramName_UTF8;
				if (i > 1)
				{
					oss << "_" << i;
				}

				uniqueName = oss.str();
				if (uniqueNames.insert({ uniqueName, p->Handle() }).second)
					break;
			}

			int32_t datatype{};
			p->GetDatatype(FT_VALUE, &datatype);

			enums[p->Handle()] = { "PARAM_" + uniqueName, datatype };
		}
	}

	return enums;
}

// The DAW-automatable parameters of a GMPI plugin export, in DAW-index order.
// Mirrors the VST3 wrapper's DAW-facing parameter list: non-private parameters with an
// assigned index, excluding datatypes VST/GMPI hosts can't automate (string, blob, ...).
std::vector<GmpiParamExport> CPatchManager::ExportGmpiParameters()
{
	std::vector<GmpiParamExport> result;

	for (auto p : m_parameters)
	{
		bool isPrivate{};
		p->IsPrivate(&isPrivate);

		if (isPrivate || p->GetVstParameterNumber() < 0 || !p->instansiateDsp() || p->isPolyphonic())
			continue;

		int32_t datatype{};
		p->GetDatatype(FT_VALUE, &datatype);

		if (datatype != DT_FLOAT && datatype != DT_DOUBLE && datatype != DT_INT && datatype != DT_BOOL && datatype != DT_ENUM)
			continue;

		result.push_back({
			p->GetVstParameterNumber(),
			JmUnicodeConversions::WStringToUtf8(p->m_short_name),
			p->getValueNormalised(0)
			});
	}

	std::sort(result.begin(), result.end(), [](const GmpiParamExport& a, const GmpiParamExport& b) { return a.index < b.index; });

	return result;
}

int32_t CPatchManager::RegisterGui2(gmpi::api::IParameterObserver* gui)
{
	m_guis2.push_back(gui);
	return gmpi::MP_OK;
}

void CPatchManager::DeregisterAllGuiPatchAutomators()
{
	// When deleting a Patch-Automator during editing, need to notify all the GuiPatchAutomator3s, else they are left with dangling pointer. (CRASH).
	// Not relevent to plugins.
	for (auto g : m_guis2)
	{
		auto pa3 = dynamic_cast< GuiPatchAutomator3* > (g);
		if (pa3)
			pa3->Sethost(nullptr);
	}
}

int32_t CPatchManager::UnRegisterGui2(gmpi::api::IParameterObserver* gui)
{
	for (auto it = m_guis2.begin(); it != m_guis2.end(); ++it)
	{
		if (*it == gui)
		{
			m_guis2.erase(it);
			break;
		}
	}

	return gmpi::MP_OK;
}

void CPatchManager::DoHostCommand(int p_command_id)
{
	Container()->DoHostCommand( p_command_id );
}

void CPatchManager::initializeGui(gmpi::api::IParameterObserver* gui, int32_t parameterHandle, gmpi::Field FieldId)
{
	for (auto& p : m_parameters)
	{
		if (p->Handle() == parameterHandle)
		{
			for (int voice = 0; voice < p->getVoiceCount(); ++voice)
			{
				const auto raw = p->GetValue((ParameterFieldType) FieldId, voice);

				gui->setParameter(parameterHandle, FieldId, voice, static_cast<int32_t>(raw.size()), (const uint8_t*)raw.data());
			}
			break;
		}
	}
}

int32_t CPatchManager::sendSdkMessageToAudio(int32_t handle, int32_t id, int32_t size, const void* messageData)
{
	return Application()->sendSdkMessageToAudio(handle, id, size, messageData);
}

int32_t CPatchManager::GetParameterIterator( int subPathNodehandle, class IGuiHostParameterIterator** returnValue)
{
	*returnValue = new PmParameterIterator( this, subPathNodehandle );
	return 0;
}

// IGuiHost2
void CPatchManager::setParameterValue(RawView value, int32_t parameterHandle, gmpi::FieldType moduleFieldId, int32_t voice)
{
	for (auto p : m_parameters)
	{
		if (p->Handle() == parameterHandle)
		{
			p->SetValue(value, (ParameterFieldType)moduleFieldId, voice);

			// test for preset categories HC (using generic parameter)
			if (p->hostControlId_ != HC_NONE)
			{
				OnSetHostControl(p->hostControlId_, moduleFieldId, static_cast<int>(value.size()), value.data(), voice);
			}
			break;
		}
	}
}

// copied off MpController. not sure if best approach.
void CPatchManager::OnSetHostControl(int hostControl, int32_t paramField, int32_t size, const void* data, int32_t voice)
{
	switch (hostControl)
	{
	case HC_PROGRAM_CATEGORY:
		UpdateProgramCategoryList();
		break;
	}
}

RawView CPatchManager::getParameterValue(int32_t parameterHandle, int32_t moduleFieldId, int32_t voice )
{
	for (auto& p : m_parameters)
	{
		if (p->Handle() == parameterHandle)
			return p->GetValue((ParameterFieldType)moduleFieldId, voice);
	}
	return {};
}

int32_t CPatchManager::getParameterHandle(int32_t moduleHandle, int32_t moduleParameterId)
{
	HostControls hostControl = (HostControls) (-1 - moduleParameterId);

	if (hostControl > HC_NONE)
	{
		for (auto& p : m_parameters)
		{
			if (p->hostControlId_ == hostControl && ( moduleHandle == -1 || moduleHandle == p->ModuleHandle() ))
			{
				return p->Handle();
				break;
			}
		}

		// For host controls "moduleHandle" is container handle.
		auto hc = GetHostGeneratedParameter(hostControl, true, dynamic_cast<CContainer*>(Container()->Document()->uniqueIdDatabase.HandleToObjectWithNull(moduleHandle)));
		return hc ? hc->Handle() : -1;
	}
	else
	{
		for (auto& p : m_parameters)
		{
			if (p->ModuleHandle() == moduleHandle && p->ModuleParameterId() == moduleParameterId)
			{
				return p->Handle();
				break;
			}
		}
	}

	return -1;
}

int32_t CPatchManager::getParameterModuleAndParamId(int32_t parameterHandle, int32_t* returnModuleHandle, int32_t* returnModuleParameterId)
{
	auto param = GetParameter(parameterHandle);
	if (param)
	{
		if (param->hostControlId_ != HC_NONE)
		{
			*returnModuleParameterId = -1 - param->hostControlId_;

			if (AttachesToVoiceContainer(param->hostControlId_) || AttachesToParentContainer(param->hostControlId_))
			{
				assert(dynamic_cast<CContainer*>(param->module())->getVoiceControlContainer()->Handle() == param->module()->Handle());
				*returnModuleHandle = param->ModuleHandle();
			}
			else
			{
				*returnModuleHandle = -1;
			}
		}
		else
		{
			*returnModuleHandle = param->ModuleHandle();
			*returnModuleParameterId = param->ModuleParameterId();
		}
		return gmpi::MP_OK;
	}

	return gmpi::MP_FAIL;
}

int32_t CPatchManager::resolveFilename(const wchar_t* shortFilename, int32_t maxChars, wchar_t* returnFullFilename)
{
	return Application()->resolveFilename(shortFilename, maxChars, returnFullFilename);
}

int32_t CPatchManager::getController(int32_t handle, gmpi::IMpController** returnController)
{
	auto cug2 = dynamic_cast<CUG2*>( Container()->Document()->uniqueIdDatabase.HandleToObjectWithNull(handle) );
	if (cug2)
	{
		*returnController = cug2->getController();
	}
	return gmpi::MP_OK;
}

void CPatchManager::serviceGuiQueue()
{
	// called from DrawingFrameBase::OnTimer() for EVERY DX View (so a little redundant) but improves frame rate quite a bit
	// by polling DSP more often.
	// In SynthEdit.exe: Doubles up on CSynthEditAppBase::OnTimer() too (which is a little sluggish on it's own)
	Application()->OnTimer();
}

void CPatchManager::OnParameterUpdate( PatchParameter_base* param, ParameterFieldType field, int voice, const void* data, int32_t size )
{
	for (auto g : m_guis2)
	{
		g->setParameter(param->Handle(), static_cast<gmpi::Field>(field), voice, size, (const uint8_t*)data);
	}
}

void CPatchManager::RegisterHandles( UniqueSnowflakeOwner* uniqueIdDatabase, bool paste_mode )
{
	for( auto it = m_parameters.begin() ; it != m_parameters.end() ;)
	{
		PatchParameter_base* param = *it;
		int oldHandle = param->Handle();
		uniqueIdDatabase->Register( param );
		/* not OK if loading prefab
		#if defined( _DEBUG )
		if( param->Handle() != oldHandle ) // ensure handle not already in use (re-assigned by register)
		{
			_RPT0(_CRT_WARN, "!!!Parameter handle already in use!!!! ( OK if 'copy'ing )\n" );
		}
		#endif
		*/

		if( param->Handle() != oldHandle ) // handle already in use, re-allocated during register().
		{
			// When we copy/paste a parameter, can't use same VST parameter number. set back to 'auto'.
			param->SetVstParameterNumber(-1);

			// check new handle not used by any other parameter (that has not been registered yet).
			// can happen when upgrading pre 1.1 file.
			int newHandle = param->Handle();

			for( auto it2 = m_parameters.begin() ; it2 != m_parameters.end() ;)
			{
				PatchParameter_base* p2 = *it2;

				if( p2->Handle() == newHandle && p2 != param )
				{
					assert( paste_mode == true && "why re-generating parameter's handles?, will lose ability to load existing banks)." );
					// assign new handle.
					uniqueIdDatabase->Unregister( param );
					uniqueIdDatabase->setHandleAutoGenerated( param );
					newHandle = param->Handle();
					// re-start check.
					it2 = m_parameters.begin();
				}
				else
				{
					++it2;
				}
			}
		}
		else
		{
			++it;
		}
	}
}

void CPatchManager::onContainerIgnoreProgramChangeUpdate()
{
	for(auto& p : m_parameters)
	{
		PatchParameter_base* param = p;
		param->onSetIgnoreProgramChange();
	}
}

int CPatchManager::getMidiChannel()
{
	return midiChannel;
}

void CPatchManager::setMidiChannel( int p_chan )
{
	midiChannel = p_chan;
	m_container->SendIntValueToDsp( "setc", midiChannel );
}

ApplicationBase* CPatchManager::Application()
{
	return m_container->Document()->Application();
}

int32_t PmParameterIterator::First()
{
	m_it = m_patch_mgr->m_parameters.begin();
	SkipUnqualified();
	return 0;
}

int32_t PmParameterIterator::Next()
{
	++m_it;
	SkipUnqualified();
	return 0;
}

int32_t PmParameterIterator::IsDone( bool* returnValue)
{
	*returnValue = m_it == m_patch_mgr->m_parameters.end();
	return 0;
}

PatchParameter_base* PmParameterIterator::Current()
{
	return *m_it;
}

void PmParameterIterator::SkipUnqualified()
{
	while( m_it != m_patch_mgr->m_parameters.end() )
	{
		CUG* m = (*m_it)->module();

/* attempt to see Patch-Cables
 */		
		if (m == nullptr) // unattached Host-Control to show under it's top container.
		{
			if(	m_patch_mgr->Container()->Handle() == moduleHandle_ )
				return;
		}

		while( m )
		{
			if( m->Handle() == moduleHandle_ )
			{
				return;
			}

			m = m->Container();
		}

		m_it++;
	}
}

std::string CPatchManager::exportVst3Preset(int32_t pluginId, int patch)
{
	if (patch < 0)  // -1 = current preset.
		patch = GetProgram();

	// Sort for export consistancy.
	list<PatchParameter_base*> sortedParameters;
	{
		for (auto& p : m_parameters)
		{
			if (p->hostControlId_ != HC_PROGRAM_CATEGORY && p->is_stateful())// || p->hostControlId_ == HC_PROGRAM_NAME))
			{
				sortedParameters.push_back(p);
			}
		}

		// seems logical to list in same order as DAW.
		sortedParameters.sort([](PatchParameter_base* a, PatchParameter_base* b) -> bool
		{
			if (a->GetVstParameterNumber() != a->GetVstParameterNumber())
				return a->GetVstParameterNumber() < b->GetVstParameterNumber();

			return a->Handle() < b->Handle();
		});
	}

	TiXmlDocument doc;
	TiXmlDeclaration* decl = new TiXmlDeclaration("1.0", "", "");
	doc.LinkEndChild(decl);

	auto element = new TiXmlElement("Preset");
	doc.LinkEndChild(element);

	{
		std::ostringstream oss;
		oss << std::hex << std::nouppercase << std::setw(8) << std::setfill('0') << pluginId;
		element->SetAttribute("pluginId", oss.str());
	}

	auto name = WStringToUtf8(getProgramNameIndexed(patch));
	if (!name.empty())
	{
		element->SetAttribute("name", name.c_str());
	}

	auto category = WStringToUtf8( getProgramCategoryIndexed(patch) );
	if (!category.empty())
	{
		element->SetAttribute("category", category.c_str());
	}

	for (auto parameter : sortedParameters)
	{
		auto paramElement = new TiXmlElement("Param");
		element->LinkEndChild(paramElement);
		paramElement->SetAttribute("id", parameter->Handle());

		int maxPatches;
		if (parameter->hostControlId_ == HC_PROGRAM_NAME) // hack
		{
			maxPatches = 128;
		}
		else
		{
			maxPatches = parameter->ignoreProgramChange() ? 0 : parameter->getPatchCount() - 1;
		}

		int p = min(patch, maxPatches);

		auto size = parameter->GetValue(FT_VALUE, 0, p).size();
		if (size > maxXmlAttributeBytes)
		{
			// XML can't really handle HUGE data (like Wavetables). Hack to blank out those.
			paramElement->SetAttribute("val", "");
		}
		else
		{
			paramElement->SetAttribute("val", parameter->ToXmlStringSafe(0, p));
		}

		// MIDI learn.
		if (parameter->m_automation != -1)
		{
			paramElement->SetAttribute("MIDI", parameter->m_automation);

			if (!parameter->m_automation_sysex.empty())
				paramElement->SetAttribute("MIDI_SYSEX", WStringToUtf8(parameter->m_automation_sysex));
		}
	}

	TiXmlPrinter printer;
	printer.SetIndent(" ");
	doc.Accept(&printer);

	return printer.CStr();
}

void CPatchManager::ExportPresetXml(int32_t pluginId, TiXmlNode* XmlParent, bool isSinglePreset, int presetIndex)
{
	int firstPreset = 0;
	int lastPreset = 127;
	if (isSinglePreset)
	{
		if (presetIndex == -1) // current preset.
		{
			firstPreset = lastPreset = GetProgram();
		}
		else
		{
			firstPreset = lastPreset = presetIndex;
		}
	}
	else
	{
		if (presetIndex != -1) // number of presets to export.
		{
			lastPreset = presetIndex;
		}
	}

	// Sort for export consistancy.
	list<PatchParameter_base*> sortedParameters;
	for(auto& p : m_parameters)
	{
		if(p->hostControlId_ != HC_PROGRAM_CATEGORY && p->is_stateful())// || p->hostControlId_ == HC_PROGRAM_NAME))
		{
			sortedParameters.push_back(p);
		}
	}

	// seems logical to list in same order as DAW.
	sortedParameters.sort([](PatchParameter_base* a, PatchParameter_base* b) -> bool
	{
		if (a->GetVstParameterNumber() != a->GetVstParameterNumber())
			return a->GetVstParameterNumber() < b->GetVstParameterNumber();

		return a->Handle() < b->Handle();
	});

	auto presets_xml = XmlParent;

	// Trim off tailing unnamed presets from exported list. Tend to be blank rubbish.
	while (firstPreset < lastPreset && getProgramNameIndexed(lastPreset).empty())
		--lastPreset;

	for (int preset = firstPreset; preset <= lastPreset; ++preset)
	{
		auto preset_xml = new TiXmlElement("Preset");
		presets_xml->LinkEndChild(preset_xml);

		{
			std::ostringstream oss;
			oss << std::hex << std::nouppercase << std::setw(8) << std::setfill('0') << pluginId;
			preset_xml->SetAttribute("pluginId", oss.str());
		}

		string presetName = WStringToUtf8(getProgramNameIndexed(preset));
		if (presetName.empty())
		{
			std::ostringstream ossp;
			ossp << "Preset " << (preset + 1);
			presetName = ossp.str();
		}
		preset_xml->SetAttribute("name", presetName);

		auto category = WStringToUtf8(getProgramCategoryIndexed(preset));
		if(!category.empty())
			preset_xml->SetAttribute("category", category);

		for (auto& parameter : sortedParameters)
		{
			auto parameter_xml = new TiXmlElement("Param");
			preset_xml->LinkEndChild(parameter_xml);

			parameter_xml->SetAttribute("id", parameter->Handle());

			int maxPatches;
			if (parameter->hostControlId_ == HC_PROGRAM_NAME) // hack
			{
				maxPatches = 128;
			}
			else
			{
				maxPatches = parameter->getPatchCount() - 1;
			}

			int p = (std::min)(preset, maxPatches);

			const auto size = parameter->GetValue(FT_VALUE, 0, p).size();
			if (size > maxXmlAttributeBytes)
			{
				// XML can't really handle HUGE data (like Wavetables). Hack to blank out those.
				parameter_xml->SetAttribute("val", "");
			}
			else
			{
				parameter_xml->SetAttribute("val", parameter->ToXmlStringSafe(0, p));
			}

			// MIDI learn.
			if (parameter->m_automation != -1)
			{
				parameter_xml->SetAttribute("MIDI", parameter->m_automation);

				if (!parameter->m_automation_sysex.empty())
					parameter_xml->SetAttribute("MIDI_SYSEX", WStringToUtf8(parameter->m_automation_sysex));
			}
		}
	}
}

void CPatchManager::ImportPresetXml(TiXmlNode* XmlParent, bool isSinglePreset, int presetIndex, std::string overridingCategory)
{
	// see also MpController::setPreset()

	/* Possible formats.
	1) PatchManager/Parameters/Parameter/Preset/patch-list
	2)      Presets/Parameters/Parameter/Preset/patch-list
	3) Presets/Preset/Param.val
	4)     Parameters/Param.val
	5)         Preset/Param.val
	*/

	auto patchManagerE = XmlParent->FirstChildElement("PatchManager");
	if (!patchManagerE)
	{
		auto presetsXml = XmlParent->FirstChildElement("Presets");
		if (presetsXml)
		{
			// could be format 2 or 3
			auto parameters_xml = presetsXml->FirstChildElement("Parameters");
			if (parameters_xml)
			{
				// "Presets" (2) serves same purpose as "PatchManager" (1)
				patchManagerE = presetsXml;
			}
		}
	}

	if (patchManagerE) // format 1 and 2.
	{
		auto parameters_xml = patchManagerE->FirstChildElement("Parameters");
		if (parameters_xml)
		{
			// Old format, presets within parameters.
			auto ppresetNames_xml = patchManagerE->FirstChildElement("PresetNames");
			if (ppresetNames_xml)
			{
				int i = 0;
				for (TiXmlElement* name_xml = ppresetNames_xml->FirstChildElement("Name"); name_xml; name_xml = name_xml->NextSiblingElement("Name"))
				{
					const char* pText = name_xml->GetText();
					setProgramNameIndexed(Utf8ToWstring(pText), i);
					++i;
				}
			}

			for (TiXmlElement* parameter_xml = parameters_xml->FirstChildElement("Parameter"); parameter_xml; parameter_xml = parameter_xml->NextSiblingElement("Parameter"))
			{
				int ParameterHandle = -1;
				parameter_xml->QueryIntAttribute("Handle", &ParameterHandle);

				if (auto parameter = GetParameter(ParameterHandle); parameter)
				{
					ParseXmlPreset(
						parameter_xml,
						[this, parameter, isSinglePreset](int voiceId, int preset, const char* xmlvalue)
						{
							if (isSinglePreset)
							{
								if (preset == 0)
								{
									parameter->FromXmlString(xmlvalue, voiceId, -1); // -1 = CURRENT PRESET
								}
							}
							else
							{
								parameter->FromXmlString(xmlvalue, voiceId, preset);
							}
						}
					);
				}
			}
		}
	}
	else // format 3,4
	{
		/* Possible formats.
		1) PatchManager/Parameters/Parameter/Preset/patch-list
		2)      Presets/Parameters/Parameter/Preset/patch-list
		3) Presets/Preset/Param.val
		4)     Parameters/Param.val
		5)         Preset/Param.val
		*/

		int preset = isSinglePreset ? -1 : 0;

		TiXmlElement* preset_xml = nullptr;
		auto presetsXml = XmlParent->FirstChildElement("Presets");
		if (presetsXml) // exported from SE has Presets/Preset
		{
			preset_xml = presetsXml->FirstChildElement("Preset"); // format 3.
		}
		else
		{
			preset_xml = XmlParent->FirstChildElement("Parameters"); // Format 4. "Parameters" in place of "Preset".
			if(!preset_xml)
			{
				preset_xml = XmlParent->FirstChildElement("Preset"); // Format 5. (VST2 fxp preset).
			}
		}

		const int countMaximumPresets = 128;
		for(; preset_xml && countMaximumPresets > preset; preset_xml = preset_xml->NextSiblingElement())
		{
			// Query plugin's 4-char code. Presence Indicates also that preset format supports MIDI learn.
			int32_t fourCC = -1;
			int formatVersion = 0;
			{
				std::string hexcode;
				if(TIXML_SUCCESS == preset_xml->QueryStringAttribute("pluginId", &hexcode))
				{
					formatVersion = 1;
					try
					{
						fourCC = static_cast<int32_t>(std::stoul(hexcode.c_str(), nullptr, 16));
					}
					catch(...)
					{
						// who gives a f*ck
					}
				}
			}

			std::string presetName("Preset"); // format 4 presets have no name.
			preset_xml->QueryStringAttribute("name", &presetName);
			setProgramNameIndexed(Utf8ToWstring(presetName), preset);

			{
				std::string category;
				preset_xml->QueryStringAttribute("category", &category);
				if(!category.empty())
					setProgramCategoryIndexed(Utf8ToWstring(category), preset);
				else
					setProgramCategoryIndexed(Utf8ToWstring(overridingCategory), preset);
			}

			std::vector<int32_t> parameterHandlesLoaded;

			for(auto parameter_xml = preset_xml->FirstChildElement("Param"); parameter_xml; parameter_xml = parameter_xml->NextSiblingElement())
			{
				int ParameterHandle = -1;
				parameter_xml->QueryIntAttribute("id", &ParameterHandle);

				auto parameter = GetParameter(ParameterHandle);

				if(parameter)
				{
					parameterHandlesLoaded.push_back(ParameterHandle);

					// MIDI learn.
					if(formatVersion > 0)
					{
						int tempController = -1;
						parameter_xml->QueryIntAttribute("MIDI", &tempController);
						if(tempController != -1)
						{
							std::string tempSysex;
							parameter_xml->QueryStringAttribute("MIDI_SYSEX", &tempSysex);
							parameter->m_automation = tempController;
							parameter->m_automation_sysex = Utf8ToWstring(tempSysex);
						}
					}

					const int effectivePreset = parameter->ignoreProgramChange() ? 0 : preset;
					const int voiceCount = parameter->isPolyphonic() ? 128 : 1;

					ParseXmlPreset(
						parameter_xml,
						[parameter, effectivePreset, voiceCount](int voiceId, int preset, const char* xmlvalue)
						{
							if(voiceId < voiceCount && preset == 0)
							{
								parameter->FromXmlString(xmlvalue, voiceId, effectivePreset);
							}
						}
					);
				}
			}

			// Set unhandled parameters to default
			for(auto& param : m_parameters)
			{
				if(std::find(parameterHandlesLoaded.begin(), parameterHandlesLoaded.end(), param->Handle()) == parameterHandlesLoaded.end())
				{
					if(param->ignoreProgramChange() || !param->is_stateful() || param->hostControlId_ != HC_NONE)
						continue;

					const int patchCount = 128;
					param->InitializePatchMemory(true, patchCount);
					param->UpdateGui(FT_VALUE);
					param->UpdateDspValue(0, 0);
				}
			}

			if(isSinglePreset)
				break;

			++preset;
		}
		// TODO: Clear remaining presets? 
	}

	// update preset browser in case categories changed
	if(auto pb = GetHostGeneratedParameter(HC_PROGRAM_CATEGORIES_LIST, false, Container()); pb)
		pb->UpdateGui();
}

void CPatchManager::ExportSubPresetXml(TiXmlElement* XmlParent, CContainer* container)
{
	assert(false); // uses obsolete, buggy format ("patch - list"). needs to be redone.
#if 0
	const bool isSinglePreset = true;

	// Preset Names.
	TiXmlElement* presetNames_xml = new TiXmlElement("PresetNames");
	XmlParent->LinkEndChild(presetNames_xml);

	std::ostringstream oss;

	int firstPreset = 0;
	int lastPreset = 127;
	if (isSinglePreset)
	{
		firstPreset = lastPreset = GetProgram();
	}

	for (int preset = firstPreset; preset <= lastPreset; ++preset)
	{
		string v = WStringToUtf8(getProgramNameIndexed(preset));

		TiXmlElement* presetName_xml = new TiXmlElement("Name");
		presetNames_xml->LinkEndChild(presetName_xml);
		TiXmlText* text = new TiXmlText(v);
		presetName_xml->LinkEndChild(text);
	}

	// Parameters.
	TiXmlElement* parameters_xml = new TiXmlElement("Parameters");
	XmlParent->LinkEndChild(parameters_xml);

	// Sort for export consistancy.
	auto sortedParameters = GetSubParameters(container);

	// Parameters settings and patch-memory.
	for each(auto p in sortedParameters)
	{
		p->ExportXmlPreset(parameters_xml, isSinglePreset, firstPreset);
	}
#endif
}

list<PatchParameter_base*> CPatchManager::GetSubParameters(CContainer* container)
{
	list<PatchParameter_base*> sortedParameters;

	for(auto& p : m_parameters)
	{
		if ((bool) p->isStateful)
		{
			// only interested in parameters in relevant container.
			auto m = p->module();
			while (m)
			{
				if (m == container)
				{
					sortedParameters.push_back(p);
					break;
				}
				m = m->Container();
			}
		}
	}

	sortedParameters.sort([](PatchParameter_base* a, PatchParameter_base* b) -> bool
	{
		return a->GetName() > b->GetName();
	});

	return sortedParameters;
}

void CPatchManager::ImportSubPresetXml(TiXmlElement* XmlParent, CContainer* container, bool strictMatching)
{
	const bool isSinglePreset = true;

	auto sortedParameters = GetSubParameters(container);

	TiXmlElement* parameters_xml = XmlParent->FirstChildElement("Parameters");

	for (TiXmlElement* parameter_xml = parameters_xml->FirstChildElement("Parameter"); parameter_xml; parameter_xml = parameter_xml->NextSiblingElement("Parameter"))
	{
		int ParameterHandle = -1;
		parameter_xml->QueryIntAttribute("Handle", &ParameterHandle);
		std::string paramerterStoredName;
		parameter_xml->QueryStringAttribute("Name", &paramerterStoredName);
		wstring paramerterStoredNameW = Utf8ToWstring(paramerterStoredName);

		int parameterStoredDatatype = -1;
		parameter_xml->QueryIntAttribute("DataType", &parameterStoredDatatype);

		PatchParameter_base* parameter = nullptr;
		if (strictMatching)
		{
			// strict.
			parameter = GetParameter(ParameterHandle);
		}
		else
		{
			// Loose.
			for(auto& p : sortedParameters)
			{
				int actualdatatype;
				p->GetDatatype(FT_VALUE, &actualdatatype);
				if (p->GetName() == paramerterStoredNameW && actualdatatype == parameterStoredDatatype)
				{
					parameter = p;
					break;
				}
			}
		}

		if (parameter)
		{
			ParseXmlPreset(
				parameter_xml,
				[this, parameter, isSinglePreset](int voiceId, int preset, const char* xmlvalue)
				{
					if (isSinglePreset)
					{
						if (preset == 0)
						{
							parameter->FromXmlString(xmlvalue, voiceId, -1); // -1 = CURRENT PRESET
						}
					}
					else
					{
						parameter->FromXmlString(xmlvalue, voiceId, preset);
					}
				}
			);
		}
	}
}

void CPatchManager::setModified(bool presetIsModified)
{
	auto pb = GetHostGeneratedParameter(HC_PROGRAM_MODIFIED, true, Container());
	assert(pb);
	{
		pb->SetValue(RawView(presetIsModified)); //> setValueNormalised(1.f, false);
	}
}
