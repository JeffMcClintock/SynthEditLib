#pragma once
#include "Control.h"

class Ctl_Combo : public CControl
{
	enum CComboMode { ACM_PLAIN, ACM_LED_STACK, ACM_LED_STACK_LABELED, ACM_BUTTON_SELECTOR, ACM_BUTTON_STACK, ACM_ROTARY_SWITCH_LABELED, ACM_ROTARY_SWITCH, ACM_UP_DOWN_SELECTOR };

public:
	DECLARE_BUILD_FUNC(Ctl_Combo);
	void OnDownstreamPlugChange(IPlug* p_my_plug, IPlug* p_downstream_plug, int p_msg_id) override;
	TiXmlElement* ExportXml(TiXmlElement* XmlParent, ExportFormatType targetType ) override;
	void Export(Json::Value& module_element, ExportFormatType targetType) override;
	void RegisterImages(int disp_type);

protected:
	Ctl_Combo( Module_Info* p_type = 0 );
};
