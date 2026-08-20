#include <sstream>
#include "CPlugin.h"
#include "PatchManager.h"
#include "UgDatabase.h"
#include "CContainer.h"
#include "ModuleFactory_Editor.h"

CPlugin::CPlugin( Module_Info* p_type ) : CUG( p_type )
{
}

CPlugin::~CPlugin()
{
	// when module dll not available, patch store pins don't automatically unregister, need to manually check
	{
		for( auto it = Plugs.begin(); it != Plugs.end() ; ++it )
		{
			get_patch_manager()->UnRegister( this, (*it)->getPlugDescID() );
		}
	}

}

void CPlugin::ReloadDll()
{
}

// does this user have the module dll?
bool CPlugin::DllAvailable()
{
	return getType()->isDllAvailable();
}

void CPlugin::Upgrade(int from_version)
{
	// If dll not available, don't attempt to upgrade plugs ('plain' plugin reports 20 plugs)
	if( DllAvailable() )
	{
		// Check if SDK2 module can be upgraded to SDK3 module.
		auto mi = ModuleFactory()->GetById( getType()->UniqueId() );
		if( mi && mi->ModuleTechnology() != MT_SDK2 )
		{
			CUG* replacement = (CUG*) Container()->AddReplacementUg( this, CreateDocObject( mi->UniqueId() ) );
			replacement->UpgradeFrom(this);
			OnDelete();
			return;
		}

		CUG::Upgrade(from_version);
	}
}

void CPlugin::OnHelp()
{
}

