#pragma once

#include "Module_Info3_base.h"
#include "./modules/se_sdk3/mp_sdk_common.h"

typedef std::map< int, MP_CreateFunc2> FactoryMethodList2_t;

class Module_Info3_internal : public Module_Info3_base
{
public:
	Module_Info3_internal(const wchar_t* moduleId);

	int32_t RegisterPluginConstructor( int subType, MP_CreateFunc2 create );
	bool fromExternalDll() override { return false;}

	ug_base* BuildSynthOb() override;
	gmpi::IMpUnknown* Build(int subType, bool quietFail = false) override;

protected:
	Module_Info3_internal() {} // Serialising.
	int getClassType() override { return 2; } // 0 - Module_Info3, 1 - Module_Info, 2 - Module_Info3_internal, 3 - Module_Info_Plugin

	FactoryMethodList2_t factoryMethodList_; // new way.
};



