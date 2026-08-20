#include "Ctl_Slider.h"
#include "tinyxml/tinyxml.h"
#include "modules/se_sdk3/MpString.h"
#include "modules/se_sdk3_hosting/GmpiResourceManager.h"
#include "UgDatabase.h"
#include "SkinMgr.h"

using namespace std;

Ctl_Slider::Ctl_Slider( Module_Info* p_type ) : CControl( p_type )
{
}

TiXmlElement* Ctl_Slider::ExportXml(TiXmlElement* XmlParent, ExportFormatType targetType)
{
	TiXmlElement* module_element = CUG::ExportXml(XmlParent, targetType);

	if (module_element == nullptr || targetType != SAT_SUBCONTROLS_GUI)
	{
		return module_element;
	}

	// new (under development).
	module_element->SetAttribute("Type", "SE Slider");
	FlagRequiredModuleForExport(L"SE Slider");

	IPlug* p = GetPlug(L"Appearance");
	int disp_type = 0;

	if (p != 0) // allow for older file version
	{
		disp_type = StringToInt(p->GetDefault());
	}

	RegisterImages(disp_type);

	// Add fake plugs to pass image name etc.
	// <Plug Id = "15" / >

	// displayType (Appearance)
	auto plugsElement = module_element->FirstChildElement("Plugs");
	auto plugElement = new TiXmlElement("Plug");
	plugsElement->LinkEndChild(plugElement);

	plugElement->SetAttribute("Default", disp_type);
	plugElement->SetAttribute("Id", 15);

	// Show Readout.
	bool showReadout = GetPlug(L"Show Readout")->GetDefault().compare(L"1") == 0;
	if (!showReadout)
	{
		plugElement = new TiXmlElement("Plug");
		plugsElement->LinkEndChild(plugElement);
		plugElement->SetAttribute("Default", (int)showReadout);
		plugElement->SetAttribute("Id", 23);
	}

	return module_element;
}

void Ctl_Slider::RegisterImages(int disp_type)
{
	// knob image.
//	char* imageName = "knob_med";
	vector< string > images;
	switch( disp_type )
	{
	case -1: // None
		break;

	case 0: // native slider.
	case 1:
//		imageName = "vslider_med";
		images.push_back("vslider_med");
		break;

	case 2:
//		imageName = "hslider_med";
		images.push_back("hslider_med");
		break;

	case 3:
//		imageName = "knob_med";
		images.push_back("knob_med");
		break;

	case 4:
	case 5:
//		imageName = "button";
		images.push_back("button");
		break;

	case 7:
//		imageName = "knob_sm";
		images.push_back("knob_sm");
		break;

	case 6: // native button.
	case 8:
	case 9: // toggle.
//		imageName = "button_sm";
		images.push_back("button_sm");
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

void Ctl_Slider::Export(Json::Value& module_element, ExportFormatType targetType)
{
	assert(targetType == SAT_SYNTHEDIT_GUI_STRUCT || targetType == SAT_SYNTHEDIT_DOCUMENT || targetType == SAT_SUBCONTROLS_GUI || targetType == SAT_SYNTHEDIT_GUI_PANEL); // else update if conditions.

	CUG::Export(module_element, targetType);

	module_element["type"] = "SE Slider";
	FlagRequiredModuleForExport(L"SE Slider");

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
	{
		Json::Value pin_element(Json::objectValue);
		pin_element["default"] = std::to_string(disp_type);
		pin_element["Id"] = 15;
		module_element["Pins"].append(pin_element);
	}

	// "Hint"
	{
		Json::Value pin_element(Json::objectValue);
		pin_element["default"] = WStringToUtf8(getShortDescription());
		pin_element["Id"] = 14;
		module_element["Pins"].append(pin_element);
	}

	// Show Readout.
	bool showReadout = GetPlug(L"Show Readout")->GetDefault().compare(L"1") == 0;
	if (!showReadout)
	{
		Json::Value pin_element2(Json::objectValue);
		pin_element2["default"] = "0";
		pin_element2["Id"] = 23;
		module_element["Pins"].append(pin_element2);
	}

	// "Title"
	if (show_title_on_panel())
	{
		Json::Value pin_element2(Json::objectValue);
		pin_element2["default"] = WStringToUtf8( GetName() );
		pin_element2["Id"] = 16;
		module_element["Pins"].append(pin_element2);
	}
}

