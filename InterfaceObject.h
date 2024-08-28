#pragma once

#include <vector>
#include <typeinfo>
#include <string>
#include "HostControls.h"
#include "./modules/se_sdk2/se_datatypes.h"

class IPlug;

// handy type for refering to arrays of interfaceobjects
typedef std::vector<class InterfaceObject*> InterfaceObjectArray;

// same as above but deletes it's pointers on destruction
class SafeInterfaceObjectArray : public InterfaceObjectArray
{
public:
	~SafeInterfaceObjectArray();
};

std::wstring uniformDefaultString(std::wstring defaultValue, EPlugDataType dataType);

// Describe plug or parameter of a unit_gen
class InterfaceObject
{
public:
	InterfaceObject( void* addr, const wchar_t* p_name, EDirection p_direction, EPlugDataType p_datatype, const wchar_t* def_val, const wchar_t* = L"-1", int flags = 0, const wchar_t* p_comment = L"", float** p_sample_ptr = nullptr );
	InterfaceObject( int p_id, struct pin_description& p_plugs_info ); // new
	InterfaceObject( int p_id, struct pin_description2& p_plugs_info ); // newer
	virtual ~InterfaceObject() {}

	// OVERIDES
	// query behaviour
	bool connectedControlsIgnorePatchChange() const
	{
		return ( GetFlags() & IO_IGNORE_PATCH_CHANGE ) != 0;
	}
	bool is_filename() const
	{
		return (GetFlags() & IO_FILENAME) != 0;
	}
	bool isRenamable() const
	{
		return (GetFlags() & IO_RENAME) != 0;
	}
	bool isOldStyleGuiPlug() const
	{
		return 0 != (GetFlags() & IO_OLD_STYLE_GUI_PLUG);
	}
	bool isMinimised() const
	{
		return (GetFlags() & IO_MINIMISED) != 0;
	}
	bool autoConfigureParameter() const
	{
		return (GetFlags() & IO_AUTOCONFIGURE_PARAMETER) != 0;
	}
	bool isSettableOutput() const
	{
		return (GetFlags() & IO_SETABLE_OUTPUT) != 0;
	}
	bool RedrawOnChange() const
	{
		return (GetFlags() & IO_REDRAW_ON_CHANGE) != 0;
	}

	virtual std::wstring GetComments() const
	{
		return {};
	}

	int getParameterId()
	{
		return parameterId_;
	}

	bool isUiPlug()
	{
		return ( GetFlags() & IO_UI_COMMUNICATION ) != 0;
	} // plug used by GUI
	bool isParameterPlug()
	{
		return ( GetFlags() & (IO_PATCH_STORE) ) != 0;
	}
	int getParameterFieldId()
	{
		return parameterFieldId_;
	}
	HostControls getHostConnect()
	{
		return hostConnect_;
	}
	bool DisableIfNotConnected()
	{
		return (GetFlags() & IO_DISABLE_IF_POS) != 0;
	}
	const std::string& getClassName()
	{
		return this->classname;
	}
	bool isHostControlledPlug()
	{
		return ( GetFlags() & IO_HOST_CONTROL ) != 0;
	}
	bool isPolyphonic()
	{
		return ( GetFlags() & IO_PAR_POLYPHONIC ) != 0;
	}
	bool isPolyphonicGate()
	{
		return ( GetFlags() & IO_PAR_POLYPHONIC_GATE ) != 0;
	}
	int getPlugDescID()
	{
		return m_id;
	}	// older modules return -1, in which case use plugin index instead.
	bool autoDuplicate()
	{
		return (GetFlags() & IO_AUTODUPLICATE) != 0;
	}
	bool isCustomisable()
	{
		return (GetFlags() & IO_CUSTOMISABLE) != 0;
	}

	EDirection CheckDirection(const std::type_info* dtype);
	void* GetVarAddr();
	bool GetPPActiveFlag();
	const std::wstring& GetEnumList();
	EPlugDataType GetDatatype()
	{
		return datatype;
	}
	const std::wstring& GetDefaultVal();
	int GetFlags() const
	{
		return Flags;
	}
	bool isContainerIoPlug()
	{
		return 0 != (GetFlags() & IO_CONTAINER_PLUG);
	}
	const std::wstring& GetName();
	EDirection GetDirection();
	void SetFlags(int p_flags)
	{
		Flags = p_flags;
	}

	void setId(int p_id)
	{
		m_id = p_id;
	}
	void setParameterId(int p_id)
	{
		parameterId_ = p_id;
	}
	void setParameterFieldId(int p_id)
	{
		parameterFieldId_ = p_id;
	}
	void clearVariableAddress()
	{
		address=0;
	}
	// don't really make sense here. this should only hold info common to all instances. remove.
	float**		sample_ptr; // allows plug to set a pointer to it's samples automatically

protected:
	InterfaceObject() : m_id(-1) {}

	EDirection	Direction;
	std::wstring		Name;
	std::wstring		DefaultVal;
	std::wstring		subtype;		// info for enum and range type
	int			Flags;

	EPlugDataType datatype;
	std::string classname;

	void* address;
	int m_id; // sequential number
	int parameterId_;
	int parameterFieldId_;
	HostControls hostConnect_;
};
