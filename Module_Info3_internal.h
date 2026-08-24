#pragma once

#include <algorithm>
#include "Module_Info3_base.h"
#include "./modules/se_sdk3/mp_sdk_common.h"

typedef std::map< int, MP_CreateFunc2> FactoryMethodList2_t;

class Module_Info3_internal : public Module_Info3_base
{
public:
	Module_Info3_internal(const wchar_t* moduleId);

	int32_t RegisterPluginConstructor( int subType, MP_CreateFunc2 create );
	bool fromExternalDll() override { return false;}

	// BACKLOG S46. A statically-registered module has no file, so the base
	// class's ModuleTechnology() -- and Module_Info3's "dodgy" extension sniff,
	// its own word -- both have nothing to inspect. The base answered MT_SDK3
	// unconditionally, so a GMPI module's "string" pins were read as SDK3 wide
	// text and its captions arrived as raw wchar_t bytes in a utf-8 pin
	// (3 NULs per character on Linux, 1 on Windows).
	//
	// So the technology is RECORDED at registration, where the caller knows it
	// -- gmpi::RegisterPlugin[WithXml] stamps MT_GMPI, every other path keeps
	// the historical MT_SDK3 default -- and ModuleTechnology() reports the
	// record instead of guessing. XML that arrives LATER (a host enriching
	// pins from bundle resources, the common SDK3 arrangement and a possible
	// GMPI one) then resolves its string pins against the recorded value, which
	// is why this lives on the object and not in any parse path.
	int ModuleTechnology() override { return technology_; }
	// Highest-wins, so registration ORDER cannot matter: a module registering
	// its editor through gmpi:: and (hypothetically) its processor through an
	// SDK3 path would stamp both MT_GMPI and the MT_SDK3 default, in whatever
	// order static initialisers run. No such mixed module exists today
	// (checked), but a nondeterministic answer is the one outcome this field
	// must never produce.
	void setModuleTechnology(int t) { technology_ = (std::max)(technology_, t); }

	ug_base* BuildSynthOb() override;
	gmpi::IMpUnknown* Build(int subType, bool quietFail = false) override;

protected:
	Module_Info3_internal() {} // Serialising.
	int getClassType() override { return 2; } // 0 - Module_Info3, 1 - Module_Info, 2 - Module_Info3_internal, 3 - Module_Info_Plugin

	FactoryMethodList2_t factoryMethodList_; // new way.
	int technology_ = MT_SDK3; // see ModuleTechnology() above. MT_SDK3 is the historical default.
};



