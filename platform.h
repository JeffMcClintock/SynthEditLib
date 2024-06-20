#pragma once
#include "./modules/se_sdk2/se_datatypes.h"

// platform specific overloads
class InterfaceObject;

InterfaceObject* new_InterfaceObjectA(void* addr, const wchar_t* p_name, EDirection p_direction, EPlugDataType p_datatype, const wchar_t* def_val, const wchar_t* = L"-1", int flags = 0, const wchar_t* p_comment = L"", float** p_sample_ptr = nullptr);
InterfaceObject* new_InterfaceObjectB(int p_id, struct pin_description& p_plugs_info); // new
InterfaceObject* new_InterfaceObjectC(int p_id, struct pin_description2& p_plugs_info); // newer
