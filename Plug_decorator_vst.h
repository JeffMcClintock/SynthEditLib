#pragma once
#include "PlugDescriptionDecorator.h"

class Plug_decorator_vst :
	public PlugDescriptionDecorator
{
public:
	Plug_decorator_vst() {}
	virtual std::wstring getName(IPlug* self) override;
	virtual int getDecoratorSortOrder() override
	{
		return 20;
	}; // must be second.

protected:
	void Import(IPlug * self, tinyxml2::XMLElement * xml, ExportFormatType targetType) override;
	void Export(IPlug * self, tinyxml2::XMLElement * xml, ExportFormatType targetType) override;
};
