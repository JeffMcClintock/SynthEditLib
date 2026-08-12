#pragma once
#include "PlugDescriptionDecorator.h"

// For SDK3 Autoduplicate plugs.
// Not for Container.

class Plug_decorator_autoduplicate :
	public PlugDescriptionDecorator
{
public:
	Plug_decorator_autoduplicate( int id = -1 );
	int getPlugDescID(IPlug* self) override;
	void setPlugDescID( IPlug* self, int id ) override;
	int getDecoratorSortOrder() override
	{
		return 50;
	}

protected:
	void Import(IPlug * self, tinyxml2::XMLElement * xml, ExportFormatType targetType) override;
	void Export(IPlug * self, tinyxml2::XMLElement * xml, ExportFormatType targetType) override;
	class TiXmlElement* ExportXml(IPlug* self) override;
	void Export(IPlug* self, class Json::Value& object_json, int targetType) override;

private:
	int id_;
};
