// Code originally from UgDatabase.cpp but not wanted in plugins
#include "UgDatabase_editor.h"
#include "UgDatabase.h"
#include "module_info.h"
#include "Module_Info3.h"
#include "InterfaceObject.h"
#include "tinyXml/tinyxml.h"
#include "SafeMessagebox.h"
#include "BundleInfo.h"


//bool hasGuiModule(Module_Info* info)
//{
//	if (info->ModuleTechnology() < MT_SDK3)
//	{
//		// assume it has GUI if it has GUI pins.
//		for (auto it = info->plugs.begin(); it != info->plugs.end(); ++it)
//		{
//			if ((*it).second->isUiPlug(0))
//				return true;
//		}
//		return false;
//	}
//	return info->m_gui_registered;
//}
