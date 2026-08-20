#pragma once

#include "Control.h"

class Ctl_Text : public CControl
{
public:
	Ctl_Text( Module_Info* p_type = 0 );
	bool needBrowseButton();
	void SetFileExt(const std::wstring& p_file_ext);
	DECLARE_BUILD_FUNC(Ctl_Text);

	TiXmlElement* ExportXml(TiXmlElement* XmlParent, ExportFormatType targetType ) override;
	void Export(Json::Value& module_element, ExportFormatType targetType) override;
	void OnDownstreamPlugChange( IPlug* p_my_plug, IPlug* p_downstream_plug, int p_msg_id ) override;

private:
	std::wstring FileExt;
};
