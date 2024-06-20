
#include "datatype_to_id.h"
#include "variable_policies.h"
#include "modules/se_sdk2/se_datatypes.h"
#include <assert.h>



using namespace std;

// Meyer's singleton
PersistanceFactory* PersistanceFactory::Instance()
{
	static PersistanceFactory obj; // dll safe??? !!!
	return &obj;
}

PersistanceFactory::~PersistanceFactory()
{
	for(auto it = class_list.begin(); it != class_list.end() ; ++it )
	{
		delete (*it).second;
	}
}

bool PersistanceFactory::SERegisterClass(int p_class_id, PersistanceRegister_base* p_reg)
{
	return class_list.insert({ p_class_id, p_reg }).second;
}

void* PersistanceFactory::CreateObject(int p_class_id)
{
	auto it = class_list.find(p_class_id);

	if( it != class_list.end() )
	{
		return (*it).second->CreateObject();
	}

	assert(false); // unsupported type
	return 0;
}
