#pragma once
#include "PlugDescriptionDecorator.h"

class Plug_decorator_sdk2 :
	public PlugDescriptionDecorator
{
public:
	Plug_decorator_sdk2() : m_auto_delete_feature(false), parameterId_(-1) {}
	std::wstring getDefaultEnumList(IPlug* self) override;
	void Initialise(IPlug* self, bool loaded_from_file = false ) override;

	int getDecoratorSortOrder() override
	{
		return 30;
	} // must be after 'default' decorator.
	int getParameterId(IPlug* self) override;
	void setParameterId(int parameterId );

protected:
	class TiXmlElement* ExportXml(IPlug* self) override;
	void Export(IPlug* self, class Json::Value& object_json, int targetType) override;
	void Import(IPlug* self, tinyxml2::XMLElement* xml, ExportFormatType targetType) override;
	void Export(IPlug* self, tinyxml2::XMLElement* xml, ExportFormatType targetType) override;

private:
	bool m_auto_delete_feature;
	int parameterId_; // fix for autoduplicating plugs only.
};
