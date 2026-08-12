#pragma once
#include "PlugDescriptionDecorator.h"

class Plug_decorator_namable :
	public PlugDescriptionDecorator
{
public:
	Plug_decorator_namable() {}
	virtual std::wstring getName(IPlug* /*self*/) override
	{
		return m_name;
	}
	void setName(IPlug* self, const std::wstring& p_name) override;
	int getDecoratorSortOrder() override
	{
		return 20;
	} // must be second.
protected:
	void Export(IPlug* self, class Json::Value& object_json, int targetType) override;
	void Import(IPlug * self, tinyxml2::XMLElement * xml, ExportFormatType targetType) override;
	void Export(IPlug * self, tinyxml2::XMLElement * xml, ExportFormatType targetType) override;

private:
	std::wstring m_name;
};
