#include "platform.h"
#include "InterfaceObject_editor.h"

InterfaceObject* new_InterfaceObjectA(void* addr, const wchar_t* p_name, EDirection p_direction, EPlugDataType p_datatype, const wchar_t* def_val, const wchar_t* defid, int flags, const wchar_t* p_comment, float** p_sample_ptr)
{
	return new InterfaceObject_editor(addr, p_name, p_direction, p_datatype, def_val, defid, flags, p_comment, p_sample_ptr);
}

InterfaceObject* new_InterfaceObjectB(int p_id, struct pin_description& p_plugs_info)
{
	return new InterfaceObject_editor(p_id, p_plugs_info);
}
InterfaceObject* new_InterfaceObjectC(int p_id, struct pin_description2& p_plugs_info)
{
	return new InterfaceObject_editor(p_id, p_plugs_info);
}
