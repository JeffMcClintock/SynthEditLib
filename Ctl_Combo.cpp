#include "Ctl_Combo.h"
#include "SkinMgr.h"
#include "tinyxml/tinyxml.h"
#include "modules/se_sdk3_hosting/GmpiResourceManager.h"
#include "modules/se_sdk3/MpString.h"
#include "Notify_msg.h"
#include "CContainer.h"
#include "UgDatabase.h"

using namespace std;

Ctl_Combo::Ctl_Combo( Module_Info* p_type ) : CControl( p_type )
{
}

TiXmlElement* Ctl_Combo::ExportXml(TiXmlElement* XmlParent, ExportFormatType targetType )
{
	TiXmlElement* module_element = CUG::ExportXml(XmlParent, targetType );

	if (module_element == nullptr || targetType != SAT_SUBCONTROLS_GUI)
	{
		return module_element;
	}

	// new (under development).
	module_element->SetAttribute("Type", "SE List Entry");
	FlagRequiredModuleForExport(L"SE List Entry");

	IPlug* p = GetPlug( L"Appearance" );
	int disp_type = 0;

	if( p != 0 ) // allow for older file version
	{
		disp_type = StringToInt( p->GetDefault() );
	}

	// Add fake plugs to pass image name etc.
	// <Plug ParameterId = "-1" Idx = "1" / >
	auto plugsElement = module_element->FirstChildElement( "Plugs" );

	// "Appearance"
	auto plugElement = new TiXmlElement("Plug");
	plugsElement->LinkEndChild( plugElement );
	plugElement->SetAttribute( "default", disp_type );
	plugElement->SetAttribute("Id", 13);

	if (targetType == SAT_SUBCONTROLS_GUI)
		RegisterImages(disp_type);

	return module_element;
}

void Ctl_Combo::Export(Json::Value& module_element, ExportFormatType targetType)
{
	assert(targetType == SAT_SYNTHEDIT_GUI_STRUCT || targetType == SAT_SYNTHEDIT_DOCUMENT || targetType == SAT_SUBCONTROLS_GUI || targetType == SAT_SYNTHEDIT_GUI_PANEL); // else update if conditions.

	CUG::Export(module_element, targetType);

	// Replace with GMPI-GUI version.
	module_element["type"] = "SE List Entry";
	FlagRequiredModuleForExport(L"SE List Entry");

	IPlug* p = GetPlug(L"Appearance");
	int disp_type = 0;

	if (p != 0) // allow for older file version
	{
		disp_type = StringToInt(p->GetDefault());
	}
	if (targetType == SAT_SUBCONTROLS_GUI)
		RegisterImages(disp_type);

	// Add fake plugs to pass image name etc.
	// <Plug ParameterId = "-1" Idx = "1" / >

	// "Appearance"
	Json::Value pin_element(Json::objectValue);
	pin_element["default"] = std::to_string(disp_type);
	pin_element["Id"] = 13;
	module_element["Pins"].append(pin_element);

	if (show_title_on_panel())
	{
		Json::Value pin_element2(Json::objectValue);
		pin_element2["default"] = WStringToUtf8(GetName());
		pin_element2["Id"] = 14;
		module_element["Pins"].append(pin_element2);
	}

	// "Hint"
	{
		Json::Value pin_element2(Json::objectValue);
		pin_element2["default"] = WStringToUtf8(getShortDescription());
		pin_element2["Id"] = 11;
		module_element["Pins"].append(pin_element2);
	}
}

void Ctl_Combo::RegisterImages(int disp_type)
{
	vector< string > images;

	// image, if any.
	switch (disp_type)
	{
	case ACM_ROTARY_SWITCH_LABELED:
	case ACM_ROTARY_SWITCH:
		images.push_back("switch_rotary");
		break;

	case ACM_UP_DOWN_SELECTOR:
		images.push_back("arrow_left");
		images.push_back("arrow_right");
		break;

	default:
		break;
	}

	switch (disp_type)
	{
	case ACM_LED_STACK:
	case ACM_LED_STACK_LABELED:
	case ACM_BUTTON_SELECTOR:
	case ACM_BUTTON_STACK:
		images.push_back("button_sm");
		break;
	}

	switch (disp_type)
	{
	case ACM_LED_STACK:
	case ACM_LED_STACK_LABELED:
		images.push_back("led");
		break;
	}

	for (auto& imagename : images)
	{
		// Export image to plugin folder.
		gmpi_sdk::MpString returnUri;
		GmpiResourceManager::Instance()->RegisterResourceUri(Handle(), currentVst3SkinName, imagename.c_str(), "Image", &returnUri); // resource leak, but OK for a hack.
		GmpiResourceManager::Instance()->RegisterResourceUri(Handle(), currentVst3SkinName, imagename.c_str(), "ImageMeta", &returnUri);
	}
}

void Ctl_Combo::OnDownstreamPlugChange(IPlug* p_my_plug, IPlug* p_downstream_plug, int p_msg_id)
{
	switch( p_msg_id )
	{
	case OM_DOWNSTREAM_PLUG_ENUM_CHANGE:
	{
		CControl::OnDownstreamPlugChange(p_my_plug, p_downstream_plug, p_msg_id);
		// in rare case, loading old version,
		// this is called before "Appearance" is added by AutoUpgrade
		int disp_type = 0;
		IPlug* appearence_plug = GetPlug((L"Appearance"));

		if( appearence_plug )
			disp_type = StringToInt( appearence_plug->GetDefault() );

		if( disp_type != 0 && Container() )
		{
			Container()->VO_Notify( OM_LAYOUT_CHANGE2, this );	// notify ctl_wnd
		}
	}
	break;

	// used by soundfont player to make connected combos reflect current patch
	case OM_DOWNSTREAM_PLUG_DEFAULT_CHANGE:
	{
//		setValue( p_downstream_plug->GetDefault() );
	}
	break;

	default:
		CControl::OnDownstreamPlugChange( p_my_plug, p_downstream_plug, p_msg_id );
	};
}
