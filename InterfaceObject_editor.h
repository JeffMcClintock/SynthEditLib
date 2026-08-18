#pragma once
#include "InterfaceObject.h"
#include "./PlugDescriptionDecorator.h"
#include "IPluginGui.h"

namespace tinyxml
{
	class XMLElement;
}

class InterfaceObject_editor : public InterfaceObject, public IPlugDescriptionDecorator
{
public:
	InterfaceObject_editor(void* addr, const wchar_t* p_name, EDirection p_direction, EPlugDataType p_datatype, const wchar_t* def_val, const wchar_t* defid = L"-1", int flags = 0, const wchar_t* p_comment = L"", float** p_sample_ptr = nullptr)
		: InterfaceObject(addr, p_name, p_direction, p_datatype, def_val, defid, flags, p_comment, p_sample_ptr)
		, comment(p_comment)
	{}
	InterfaceObject_editor(int p_id, struct pin_description& p_plugs_info)
		: InterfaceObject(p_id, p_plugs_info) {}
	InterfaceObject_editor(int p_id, struct pin_description2& p_plugs_info)
		: InterfaceObject(p_id, p_plugs_info) {}


	std::wstring getDefaultEnumList(IPlug*) override
	{
		return subtype;
	}
	EDirection GetDirection(IPlug*) override
	{
		return Direction;
	}
	sRange GetDefaultRange(IPlug*) override;

	// get properties
	std::wstring getName(IPlug*) override
	{
		return Name;
	}
	EPlugDataType getDatatype(IPlug*) override
	{
		return datatype;
	}
	std::wstring getFileExt(IPlug* self) override;

	void Export(class Json::Value& object_json, enum ExportFormatType targetType);
	class TiXmlElement* ExportXml(IPlug* self) override;
	void Export(IPlug* /*self*/, Json::Value& /*object_json*/, int /*targetType*/) override
	{
	}
	void Import(IPlug* /*self*/, tinyxml2::XMLElement* /*xml*/, ExportFormatType /*targetType*/) override
	{
	}
	void Export(IPlug* /*self*/, tinyxml2::XMLElement* /*xml*/, ExportFormatType /*targetType*/) override
	{
	}

	// set properties
	void setName(IPlug* self, const std::wstring& p_name) override;

	// notification
	void DeleteDecorators(IPlug* /*self*/) override {}

	// actions
	void Initialise(IPlug* /*self*/, bool /*loaded_from_file*/ = false) override {} // called on first create and serialise
	void setPlugDescID(IPlug* self, int id) override;		// autoduplicate plugs only (SDK3).

	int getDecoratorSortOrder() override
	{
		return 100;
	} // always last
	IPlugDescriptionDecorator* getPlugDescription() override
	{
		return 0;
	}
	void setPlugDescription(IPlugDescriptionDecorator* /*p_description*/) override
	{
		assert(false);
	}
	int getParameterId(IPlug*) override
	{
		return parameterId_;
	}
	std::wstring GetComments() const override
	{
		return comment;
	}

	std::wstring comment;

	bool isCustomisable(IPlug*) override
	{
		return (GetFlags() & IO_CUSTOMISABLE) != 0;
	}
	bool autoDuplicate(IPlug*) override
	{
		return (GetFlags() & IO_AUTODUPLICATE) != 0;
	}
	int getPlugDescID(IPlug*) override
	{
		return m_id;
	}	// older modules return -1, in which case use plugin index instead.
};
