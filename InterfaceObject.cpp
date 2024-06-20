
#include <assert.h>
#include <typeinfo>
#include "UPlug.h"
#include "conversion.h"
#include "./plug_description.h"
#include "./IPluginGui.h"
#include "./HostControls.h"
#include "./USampBlock.h"
#include "./UMidiBuffer2.h"

using namespace std;

SafeInterfaceObjectArray::~SafeInterfaceObjectArray()
{
	// delete contained objects
	for( auto it = begin(); it != end() ; ++it )
	{
		delete *it;
	}
}

InterfaceObject::InterfaceObject( void* addr, const wchar_t* p_name, EDirection p_direction, EPlugDataType p_datatype, const wchar_t* def_val, const wchar_t* defid, int flags, const wchar_t* /*p_comment*/, float** p_sample_ptr ) :
	Direction( p_direction ),
	Name( p_name ),
	Flags(flags),
	DefaultVal( def_val ),
	subtype(defid)
	,sample_ptr(p_sample_ptr)
	,address(addr)
	,datatype(p_datatype)
	,m_id(-1)
	,parameterFieldId_(FT_VALUE)
	,parameterId_(-1)
	,hostConnect_(HC_NONE)
{
	assert( (intptr_t) sample_ptr != 0x01010101 );

	if( GetDirection() == DR_IN && GetDatatype() == DT_FSAMPLE && (GetFlags() & (IO_LINEAR_INPUT|IO_POLYPHONIC_ACTIVE)) == 0 )
	{
		//assert(false); // don't forget to set you plugs to
		//		_RPT1(_CRT_WARN, "]Auto set poly active: %s. [FORGOTTEN TO SET FLAG??]\n",Name );
		SetFlags(GetFlags() | IO_POLYPHONIC_ACTIVE );
	}

	//	#ifdef _DEBUG
	//		assert( p_datatype == GetDatatype() );
	//		if(p_direction == DR _PARAMETER && CheckDirection(&p_typeinfo) != DR_IN)
	//			assert(false); // parameter should be a pointer (they are inputs)
	//		checkpointer(&p_typeinfo); // not checking allows use after connected
	//	#endif
	/*
	if( !p_comment.empty() )
	{
		if( wcscmp(L" {FT_RANGE_HI}", p_comment) == 0 )
		{
			parameterFieldId_ = FT_RANGE_HI;
		}
		if( wcscmp(L" {FT_RANGE_LO}", p_comment) == 0 )
		{
			parameterFieldId_ = FT_RANGE_LO;
		}
	}*/
	// Hack to support host-connect on SDK2
	if( (Flags & IO_HOST_CONTROL) != 0 )
	{
		hostConnect_ = StringToHostControl(Name);
	}
	if ((Flags & IO_PATCH_STORE) != 0)
	{
		parameterId_ = 0; // default for older modules.
	}
}; // does the datatype enum not agree with the variable?

// SDK3 version
InterfaceObject::InterfaceObject( int p_id, pin_description2& p_plugs_info ) :
	Direction( p_plugs_info.direction )
	,Name( p_plugs_info.name.c_str() )
	,Flags(p_plugs_info.flags)
	,DefaultVal( p_plugs_info.default_value.c_str() )
	,subtype( p_plugs_info.meta_data.c_str() )
	,datatype(p_plugs_info.datatype)
	,classname(p_plugs_info.classname)
	,sample_ptr(0)
	,address(0)
	,m_id(p_id)
	,parameterFieldId_(FT_VALUE)
	,parameterId_(-1)
	,hostConnect_( StringToHostControl(p_plugs_info.hostConnect) )
{
	if( GetDirection() == DR_IN && GetDatatype() == DT_FSAMPLE && (GetFlags() & (IO_LINEAR_INPUT|IO_POLYPHONIC_ACTIVE)) == 0 )
	{
		//assert(false); // don't forget to set you plugs to
		//		_RPT1(_CRT_WARN, "]Auto set poly active: %s. [FORGOTTEN TO SET FLAG??]\n",Name );
		SetFlags(GetFlags() | IO_POLYPHONIC_ACTIVE );
	}

	//	if( !p_plugs_info.notes.empty() ) //&& p_plugs_info.notes.at(0) == L'{' )
	{
		// hacky way for old internal modules to access parameterFieldId_.
		assert( wcscmp(L"{FT_SHORT_NAME}", p_plugs_info.notes.c_str()) != 0 );
		assert( wcscmp(L"{FT_ENUM_LIST}", p_plugs_info.notes.c_str()) != 0 );
		/* never used???
			if( wcscmp(L"{FT_SHORT_NAME}", p_plugs_info.notes.c_str()) == 0 )
			{
				parameterFieldId_ = FT_SHORT_NAME;
			}
			if( wcscmp(L"{FT_ENUM_LIST}", p_plugs_info.notes.c_str()) == 0 )
			{
				parameterFieldId_ = FT_ENUM_LIST;
			}
		*/
	}
}

InterfaceObject::InterfaceObject( int p_id, pin_description& p_plugs_info ) :
	Direction( p_plugs_info.direction )
	,Name( p_plugs_info.name )
	,Flags(p_plugs_info.flags)
	,DefaultVal( p_plugs_info.default_value )
	,subtype( p_plugs_info.meta_data )
	,datatype(p_plugs_info.datatype)
	,sample_ptr(0)
	,address(0)
	,m_id(p_id)
	,parameterFieldId_(FT_VALUE)
	,parameterId_(-1)
	,hostConnect_(HC_NONE)
{
	if( GetDirection() == DR_IN && GetDatatype() == DT_FSAMPLE && (GetFlags() & (IO_LINEAR_INPUT|IO_POLYPHONIC_ACTIVE)) == 0 )
	{
		//assert(false); // don't forget to set you plugs to
		//		_RPT1(_CRT_WARN, "]Auto set poly active: %s. [FORGOTTEN TO SET FLAG??]\n",Name );
		SetFlags(GetFlags() | IO_POLYPHONIC_ACTIVE );
	}

	if( p_plugs_info.notes && *(p_plugs_info.notes) == '{' )
	{
		// hacky way for old internal modules to access parameterFieldId_.
		if( wcscmp(L"{FT_SHORT_NAME}", p_plugs_info.notes) == 0 )
		{
			parameterFieldId_ = FT_SHORT_NAME;
		}

		if( wcscmp(L"{FT_ENUM_LIST}", p_plugs_info.notes) == 0 )
		{
			parameterFieldId_ = FT_ENUM_LIST;
		}
	}
}


EDirection InterfaceObject::GetDirection()
{
	return Direction;
}

const std::wstring& InterfaceObject::GetName()
{
	return Name;
}

const std::wstring& InterfaceObject::GetDefaultVal()
{
	return DefaultVal;
}

const std::wstring& InterfaceObject::GetEnumList()
{
	return subtype;
}

bool InterfaceObject::GetPPActiveFlag()
{
	assert( (Flags & IO_POLYPHONIC_ACTIVE ) == 0 || (Flags & IO_LINEAR_INPUT) == 0 ); // check both flags arn't set (mutually exclusive)
	return (Flags & IO_LINEAR_INPUT) == 0 ; // if not linear input, assume polyphonic active.
}

void* InterfaceObject::GetVarAddr()
{
	return address;
}

// For debug purpose, infer direction from datatypr
// *datatype
EDirection InterfaceObject::CheckDirection(const type_info* dtype)
{
	if( *dtype == typeid(float*))
		return DR_IN;

	if( *dtype == typeid(float))
		return DR_OUT;

	if( *dtype == typeid(double*))
		return DR_IN;

	if( *dtype == typeid(double))
		return DR_OUT;

	//	if( *dtype == typeid(short*))
	//		return DR_IN;
	if( *dtype == typeid(short))
		return DR_OUT;

	if( *dtype == typeid(std::wstring*))
		return DR_IN;

	if( *dtype == typeid(std::wstring))
		return DR_OUT;

	//	if( *dtype == typeid(UMidiBuffer*))
	//		return DR_IN;
	//	if( *dtype == typeid(UMidiBuffer))
	//		return DR_OUT;
	if( *dtype == typeid(bool))
		return DR_OUT;

	if( *dtype == typeid(bool*))
		return DR_IN;

	// Input Array version (used by ADDER)
	//	if( *dtype == typeid(InputArray_Sample))
	//		return DR_IN;
	assert(false); //add datatype here (checkdirection)
	return DR_OUT;
}

