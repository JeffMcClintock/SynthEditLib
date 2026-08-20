#pragma once
#include "Control.h"

class Ctl_Slider : public CControl
{
public:
	DECLARE_BUILD_FUNC(Ctl_Slider);

	virtual TiXmlElement* ExportXml(TiXmlElement* XmlParent, ExportFormatType targetType ) override;
	virtual void Export(Json::Value& module_element, ExportFormatType targetType) override;
	void RegisterImages(int disp_type);
protected:
	Ctl_Slider( Module_Info* p_type = 0 );
};
