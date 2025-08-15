
#include <string>
// Fix for <sstream> on Mac (sstream uses undefined int_64t)
#include "./modules/se_sdk3/mp_api.h"
#include <sstream>

#include "Module_Info3_base.h"
#include "Module_Info3_internal.h"

#include "ug_plugin3.h"
#include "midi_defs.h"
#include "HostControls.h"
#include "tinyxml/tinyxml.h"
#include "IPluginGui.h"
#include "conversion.h"
#include "UgDatabase.h"
#include "./modules/shared/xplatform.h"
#include "SafeMessageBox.h"
#include "GmpiSdkCommon.h"
#include "ug_gmpi.h"

#include <assert.h>
#include "modules/tinyXml2/tinyxml2.h"
#include "platform.h"

#if !defined( _WIN32 )
// mac
#include <dlfcn.h>
#endif

using namespace std;

void Messagebox([[maybe_unused]] std::wostringstream& oss)
{
	SafeMessagebox(0, oss.str().c_str(), L"", MB_OK | MB_ICONSTOP);
}

Module_Info3_base::Module_Info3_base(const wchar_t* moduleId) :
	Module_Info(moduleId)
	, ug_flags(0)
	, m_window_type(MP_WINDOW_TYPE_NONE)
{
	CLEAR_BITS(flags, CF_OLD_STYLE_LISTINTERFACE); // undo this flag (set in base class)
	SET_BITS(flags, CF_STRUCTURE_VIEW);
}

Module_Info3_base::Module_Info3_base() :
	ug_flags(0)
	, m_window_type(MP_WINDOW_TYPE_NONE)
{
	CLEAR_BITS(flags, CF_OLD_STYLE_LISTINTERFACE); // undo this flag (set in base class)
	SET_BITS(flags, CF_STRUCTURE_VIEW);
}

void SetPinFlag(const char* xml_name, int xml_flag, TiXmlElement* pin, int& flags)
{
	auto v = pin->Attribute(xml_name);

	if (v && strcmp("true", v) == 0)
	{
		SET_BITS(flags, xml_flag);
	}
}
void SetPinFlag(const char* xml_name, int xml_flag, tinyxml2::XMLElement* pin, int& flags)
{
	auto v = pin->Attribute(xml_name);

	if (v && strcmp("true", v) == 0)
	{
		SET_BITS(flags, xml_flag);
	}
}

void Module_Info3_base::ScanXml(TiXmlElement* pluginData)
{
	Module_Info::ScanXml(pluginData);

	for (auto& fn : flagNames)
	{
		SetPinFlag(fn.name, fn.writeFlags, pluginData, ug_flags);
	}

	std::string graphicsApi;

	auto gui_plugin_class = pluginData->FirstChild("GUI");
	// New. Graphics API specified on "GUI".
	if (gui_plugin_class)
	{
		graphicsApi = FixNullCharPtr(gui_plugin_class->ToElement()->Attribute("graphicsApi"));
	}

	// Legacy. Graphics API specified on "Plugin".
	if (graphicsApi.empty())
	{
		graphicsApi = FixNullCharPtr(pluginData->Attribute("graphicsApi"));
	}

	if (graphicsApi.empty() || graphicsApi.compare("none") == 0)
	{
//		flags |= CF_NON_VISIBLE;
	}
	else
	{
		static constexpr std::pair<int, const char*> graphicsApis[] =
		{
			{MP_WINDOW_TYPE_NONE, ""},
			{MP_WINDOW_TYPE_NONE, "none"},
			{MP_WINDOW_TYPE_XP, "GmpiGui"},
			{MP_WINDOW_TYPE_COMPOSITED, "composited"},
			{MP_WINDOW_TYPE_HWND, "HWND"},
			{MP_WINDOW_TYPE_WPF_INTERNAL, "VSTGUI"},
			{MP_WINDOW_TYPE_XP, "WPF-internal"},
			{MP_WINDOW_TYPE_CADMIUM, "Cadmium"}
		};
		bool found = false;
		for (auto& it : graphicsApis)
		{
			if (graphicsApi.compare(it.second) == 0)
			{
				found = true;
				m_window_type = it.first;
				if (m_window_type == MP_WINDOW_TYPE_WPF_INTERNAL)
				{
//					flags |= CF_NON_VISIBLE;
				}
				break;
			}
		}

		if(!found)
		{
			std::wostringstream oss;
			oss << L"module XML - 'graphicsApi' invalid. Legal: {none, composited, HWND, GmpiGui, WPF-internal}\n" << Filename();
			Messagebox(oss);
		}
	}
}

void Module_Info::ScanXml(TiXmlElement* pluginData)
{
	m_unique_id = Utf8ToWstring(pluginData->Attribute("id"));

	if (m_unique_id.empty())
	{
		m_unique_id = L"<!!!BLANK-ID!!!>"; // error
	}

	m_name = Utf8ToWstring(pluginData->Attribute("name"));

	if (m_name.empty())
	{
		m_name = m_unique_id; // L"<UN-NAMED>";
	}

	m_group_name = Utf8ToWstring(pluginData->Attribute("category"));
	helpUrl_ = Utf8ToWstring(pluginData->Attribute("helpUrl"));
	std::string oldWindowType = FixNullCharPtr(pluginData->Attribute("windowType"));

	if (!oldWindowType.empty())
	{
		SafeMessagebox(0, L"XML: 'windowType' obsolete. Use 'graphicsApi'");
	}

	SetPinFlag("polyphonicSource", CF_NOTESOURCE | CF_DONT_EXPAND_CONTAINER, pluginData, flags);
	SetPinFlag("alwaysExport", CF_ALWAYS_EXPORT, pluginData, flags);

	// get parameter info.
	auto parameters = pluginData->FirstChild("Parameters");

	if (parameters)
	{
		RegisterParameters(parameters);
	}

	if (pluginData->FirstChild("Controller"))
	{
		// nothing to do, just note it's existance. todo, hassle for now.
		// m_controller_registered = true;
	}

	int scanTypes[] = { gmpi::MP_SUB_TYPE_AUDIO, gmpi::MP_SUB_TYPE_GUI, gmpi::MP_SUB_TYPE_CONTROLLER };
	for (auto sub_type : scanTypes)
	{
		string sub_type_name;

		switch (sub_type)
		{
		case gmpi::MP_SUB_TYPE_AUDIO:
			sub_type_name = "Audio";
			break;

		case gmpi::MP_SUB_TYPE_GUI:
			sub_type_name = "GUI";
			break;

		case gmpi::MP_SUB_TYPE_CONTROLLER:
			sub_type_name = "Controller";
			break;
		}

		auto plugin_class = pluginData->FirstChild(sub_type_name);

		if (plugin_class)
		{
			if ((sub_type == gmpi::MP_SUB_TYPE_AUDIO && scanned_xml_dsp) || (sub_type == gmpi::MP_SUB_TYPE_GUI && scanned_xml_gui)) // already
			{
				std::wostringstream oss;
				oss << L"Module FOUND TWICE!, only one loaded\n" << Filename();
				Messagebox(oss);
			}
			else
			{
				if (sub_type == gmpi::MP_SUB_TYPE_AUDIO)
				{
					plugin_class->ToElement()->QueryIntAttribute("latency", &latency);
				}

				RegisterPins(plugin_class, sub_type);

				if (sub_type == gmpi::MP_SUB_TYPE_GUI && scanned_xml_gui)
				{
					// Override asssumptions made by RegisterPins() if need be.
					TiXmlElement* guiElement = plugin_class->ToElement();
					const char* s = guiElement->Attribute("DisplayOnPanel");

					if (s && strcmp(s, "false") == 0)
					{
						CLEAR_BITS(flags, CF_PANEL_VIEW);
					}
					s = guiElement->Attribute("DisplayOnStructure");

					if (s && strcmp(s, "false") == 0)
					{
						CLEAR_BITS(flags, CF_STRUCTURE_VIEW);
					}
				}
			}
		}
	}


	// If GUI XML found, assume module can create a GUI class.
	if (scanned_xml_gui)
	{
		m_gui_registered = true;
	}

	// If DSP XML found, assume module can create a DSP class.
	if (scanned_xml_dsp)
	{
		m_dsp_registered = true;
		flags |= CF_STRUCTURE_VIEW;
	}

	auto patchPoints_xml = pluginData->FirstChild("PatchPoints");

	if (patchPoints_xml)
	{
		for (auto param = patchPoints_xml->FirstChild("PatchPoint"); param; param = param->NextSibling("PatchPoint"))
		{
			auto pData2 = param->ToElement();
			//			auto pp = new patchpoint_description();

			patchPoints.push_back(patchpoint_description());

			auto& pp = patchPoints.back();

			// DSP Pin ID.
			pData2->QueryIntAttribute("pinId", &(pp.dspPin));

			// Center.
			pp.x = pp.y = 0;

			std::string centerPointS = FixNullCharPtr(pData2->Attribute("center"));
			size_t p = centerPointS.find_first_of(',');
			if (p != std::string::npos)
			{
				auto sx = centerPointS.substr(0, p);
				auto sy = centerPointS.substr(p + 1);

				pp.x = StringToInt(sx.c_str());
				pp.y = StringToInt(sy.c_str());
			}

			pData2->QueryIntAttribute("radius", &(pp.radius));
		}
	}


#if 0 // defined( _DEBUG ) && defined( SE_ED IT_SUPPORT )
	// Verify fidelity between original XML and save-as-vst XML re-generation.
	{
		TiXmlDocument doc;
		TiXmlDeclaration* decl = new TiXmlDeclaration("1.0", "", "");
		doc.LinkEndChild(decl);
		TiXmlElement* ModulesElement = new TiXmlElement("Plugins");
		doc.LinkEndChild(ModulesElement);
		ExportXml(ModulesElement);

		auto  node = ModulesElement->FirstChild("Plugin");
		TiXmlElement* PluginElement = node->ToElement();

		_RPTW1(_CRT_WARN, L"\nCOMPARE MODULE XML: %s \n", m_unique_id.c_str());
		auto  original = pluginData;
		auto  copy = PluginElement;

		CompareXml(original, copy);
	}

#endif
}

void Module_Info::RegisterParameters(TiXmlNode* parameters) // XML data passed in.
{
	assert(scanned_xml_parameters == false && "Already scanned parameters");
	std::wstring msg;
	parameter_description* pind;

	int pinId{};
	for (auto param = parameters->FirstChild("Parameter"); param; param = param->NextSibling("Parameter"))
	{
		TiXmlElement* pData2 = param->ToElement();
		pind = new parameter_description();

		// I.D.
		pData2->QueryIntAttribute("id", &pinId);
		pind->id = pinId;

		// Datatype.
		std::string pin_datatype = FixNullCharPtr(pData2->Attribute("datatype"));
		// Name.
		pind->name = Utf8ToWstring(pData2->Attribute("name"));

		const auto metadataPtr = pData2->Attribute("metadata");
		if (metadataPtr)
		{
			// File extension, enum list or range.
			pind->metaData = Utf8ToWstring(metadataPtr);
		}
		else
		{
			// check for min/max range.
			const auto minPtr = pData2->Attribute("min");
			const auto maxPtr = pData2->Attribute("max");
			if (minPtr || maxPtr)
			{
				pind->metaData = Utf8ToWstring(minPtr ? minPtr : "0") + L"," + Utf8ToWstring(maxPtr ? maxPtr : "10");
			}
		}

		// Default.
		pind->defaultValue = Utf8ToWstring(pData2->Attribute("default"));

		// Automation.
		int controllerType = ControllerType::None;
		if (!XmlStringToController(FixNullCharPtr(pData2->Attribute("automation")), controllerType))
		{
			std::wostringstream oss;
			oss << L"err. module XML file (" << Filename() << L"): pin id " << pind->id << L" unknown automation type.";
			Messagebox(oss);
			controllerType = controllerType << 24;
		}

		pind->automation = controllerType;

		// Datatype.
		int temp = {};
		if (XmlStringToDatatype(pin_datatype, temp) && temp != DT_CLASS)
		{
			pind->datatype = (EPlugDataType)temp;
		}
		else
		{
			std::wostringstream oss;
			oss << L"err. module XML file (" << Filename() << L"): parameter id " << pind->id << L" unknown datatype '" << Utf8ToWstring(pin_datatype) << L"'. Valid [float, int ,string, blob, midi ,bool ,enum ,double]";
			Messagebox(oss);
		}

		SetPinFlag(("private"), IO_PAR_PRIVATE, pData2, pind->flags);
		SetPinFlag(("ignorePatchChange"), IO_IGNORE_PATCH_CHANGE, pData2, pind->flags);
		// isFilename don't appear to be used???? !!!!!
		SetPinFlag(("isFilename"), IO_FILENAME, pData2, pind->flags);
		SetPinFlag(("isPolyphonic"), IO_PAR_POLYPHONIC, pData2, pind->flags);
		SetPinFlag(("isPolyphonicGate"), IO_PAR_POLYPHONIC_GATE, pData2, pind->flags);
		// exception. default for 'persistant' is true (set in pind constructor).
		std::string persistantFlag = FixNullCharPtr(pData2->Attribute("persistant"));

		if (persistantFlag == "false")
		{
			CLEAR_BITS(pind->flags, IO_PARAMETER_PERSISTANT);
		}

				// can't handle poly parameters with 128 patches, bogs down dsp_patch_parameter_base::getQueMessage()
				// !! these are not really parameters anyhow, more like controlers (host genereated)
		if ((pind->flags & IO_PAR_POLYPHONIC) && (pind->flags & IO_IGNORE_PATCH_CHANGE) == 0)
		{
			std::wostringstream oss;
			oss << L"err. '" << m_name << L"' module XML file: pin id " << pind->id << L" - Polyphonic parameters need ignorePatchChange=true";
			Messagebox(oss);
		}

		if (m_parameters.find(pind->id) != m_parameters.end())
		{
			std::wostringstream oss;
			oss << L"err. module XML file: parameter id " << pind->id << L" used twice.";
			Messagebox(oss);
		}
		else
		{
			bool res = m_parameters.insert({ pind->id, pind }).second;
			assert(res && "parameter already registered");
		}

		++pinId;
	}

	scanned_xml_parameters = true;
}

void Module_Info::RegisterPins(TiXmlNode* plugin_data, int32_t plugin_sub_type) // XML data passed in.
{
//	int type_specific_flags = 0;
	module_info_pins_t* pinlist = nullptr;

	switch (plugin_sub_type)
	{
	case gmpi::MP_SUB_TYPE_AUDIO:
		pinlist = &plugs;
		assert(!scanned_xml_dsp);
		scanned_xml_dsp = true;
		break;

	case gmpi::MP_SUB_TYPE_GUI:
	case gmpi::MP_SUB_TYPE_GUI2:
		pinlist = &gui_plugs;
		assert(!scanned_xml_gui);
		scanned_xml_gui = true;
//		type_specific_flags = IO_UI_COMMUNICATION;
		// if class has GUI pins, assume it shows on panel
		SET_BITS(flags, CF_PANEL_VIEW);
		break;

	case gmpi::MP_SUB_TYPE_CONTROLLER:
		pinlist = &controller_plugs;
		break;
	}

	ScanPinXml(plugin_data, pinlist, plugin_sub_type);
}

void Module_Info::RegisterParameters(tinyxml2::XMLElement* parameters) // XML data passed in.
{
	assert(scanned_xml_parameters == false && "Already scanned parameters");
	std::wstring msg;
	parameter_description* pind;

	/*
		for( unsigned int k = 0 ; k < parameters.size(); k++ )
		{
			XNode *pData = parameters[k];
			XNodes param = pData->GetChilds((L"Parameter") );
			for( unsigned int l = 0 ; l < param.size(); l++ )
		XNode *pData2 = param[l];
	*/
	for (auto param = parameters->FirstChildElement("Parameter"); param; param = param->NextSiblingElement("Parameter"))
	{
		auto pData2 = param->ToElement();
		pind = new parameter_description();
		// I.D.
		pData2->QueryIntAttribute("id", &(pind->id));
		// Datatype.
		std::string pin_datatype = FixNullCharPtr(pData2->Attribute("datatype"));
		// Name.
		pind->name = Utf8ToWstring(pData2->Attribute("name"));
		// File extension or enum list.
		pind->metaData = Utf8ToWstring(pData2->Attribute("metadata"));
		// Default.
		pind->defaultValue = Utf8ToWstring(pData2->Attribute("default"));

		// Automation.
		int controllerType = ControllerType::None;
		if (!XmlStringToController(FixNullCharPtr(pData2->Attribute("automation")), controllerType))
		{
			std::wostringstream oss;
			oss << L"err. module XML file (" << Filename() << L"): pin id " << pind->id << L" unknown automation type.";
			Messagebox(oss);
			controllerType = controllerType << 24;
		}

		pind->automation = controllerType;

		// Datatype.
		int temp;
		if (XmlStringToDatatype(pin_datatype, temp) && temp != DT_CLASS)
		{
			pind->datatype = (EPlugDataType)temp;
		}
		else
		{
			std::wostringstream oss;
			oss << L"err. module XML file (" << Filename() << L"): parameter id " << pind->id << L" unknown datatype '" << Utf8ToWstring(pin_datatype) << L"'. Valid [float, int ,string, blob, midi ,bool ,enum ,double]";
			Messagebox(oss);
		}

		SetPinFlag(("private"), IO_PAR_PRIVATE, pData2, pind->flags);
		SetPinFlag(("ignorePatchChange"), IO_IGNORE_PATCH_CHANGE, pData2, pind->flags);
		// isFilename don't appear to be used???? !!!!!
		SetPinFlag(("isFilename"), IO_FILENAME, pData2, pind->flags);
		SetPinFlag(("isPolyphonic"), IO_PAR_POLYPHONIC, pData2, pind->flags);
		SetPinFlag(("isPolyphonicGate"), IO_PAR_POLYPHONIC_GATE, pData2, pind->flags);
		// exception. default for 'persistant' is true (set in pind constructor).
		std::string persistantFlag = FixNullCharPtr(pData2->Attribute("persistant"));

		if (persistantFlag == "false")
		{
			CLEAR_BITS(pind->flags, IO_PARAMETER_PERSISTANT);
		}

				// can't handle poly parameters with 128 patches, bogs down dsp_patch_parameter_base::getQueMessage()
				// !! these are not really parameters anyhow, more like controlers (host genereated)
		if ((pind->flags & IO_PAR_POLYPHONIC) && (pind->flags & IO_IGNORE_PATCH_CHANGE) == 0)
		{
			std::wostringstream oss;
			oss << L"err. '" << m_name << L"' module XML file: pin id " << pind->id << L" - Polyphonic parameters need ignorePatchChange=true";
			Messagebox(oss);
		}

		if (m_parameters.find(pind->id) != m_parameters.end())
		{
			std::wostringstream oss;
			oss << L"err. module XML file: parameter id " << pind->id << L" used twice.";
			Messagebox(oss);
			delete pind;
		}
		else
		{
			bool res = m_parameters.insert({ pind->id, pind }).second;
			assert(res && "parameter already registered");
		}
	}

	scanned_xml_parameters = true;
}

void Module_Info::RegisterPins(tinyxml2::XMLElement* plugin_data, int32_t plugin_sub_type) // XML data passed in.
{
//	int type_specific_flags = 0;
	module_info_pins_t* pinlist = nullptr;

	switch (plugin_sub_type)
	{
	case gmpi::MP_SUB_TYPE_AUDIO:
		pinlist = &plugs;
		assert(!scanned_xml_dsp);
		scanned_xml_dsp = true;
		break;

	case gmpi::MP_SUB_TYPE_GUI:
	case gmpi::MP_SUB_TYPE_GUI2:
		pinlist = &gui_plugs;
		assert(!scanned_xml_gui);
		scanned_xml_gui = true;
//		type_specific_flags = IO_UI_COMMUNICATION;
		// if class has GUI pins, assume it shows on panel
		SET_BITS(flags, CF_PANEL_VIEW);
		break;

	case gmpi::MP_SUB_TYPE_CONTROLLER:
		pinlist = &controller_plugs;
		break;
	}

	ScanPinXml(plugin_data, pinlist, plugin_sub_type);
}

void Module_Info::ScanPinXml(tinyxml2::XMLElement* xmlObjects, module_info_pins_t* pinlist, int32_t plugin_sub_type) // XML data passed in.
{
	int nextPinId = 0;

	for (auto  node = xmlObjects->FirstChildElement();
		node;
		node = node->NextSiblingElement())
	{
		if (strcmp(node->Value() /*>ValueStr()*/, "Pin") == 0)
		{
			RegisterPin(node, pinlist, plugin_sub_type, nextPinId);
			++nextPinId;
		}
		else
		{
			if (strcmp(node->Value(), "Group") == 0)
			{
				assert(false); // used? if so check auto pin numbering.
				ScanPinXml(node, pinlist, plugin_sub_type); // recursion
			}
		}
	}
}

void Module_Info::ScanPinXml(TiXmlNode* xmlObjects, module_info_pins_t* pinlist, int32_t plugin_sub_type) // XML data passed in.
{
	int nextPinId = 0;

	for (auto  node = xmlObjects->FirstChild();
		node;
		node = node->NextSibling())
	{
		if (node->ValueStr() == "Pin")
		{
			RegisterPin(node->ToElement(), pinlist, plugin_sub_type, nextPinId);
			++nextPinId;
		}
		else
		{
			if (node->ValueStr() == "Group")
			{
				assert(false); // used? if so check auto pin numbering.
//				ScanPinXml(node, pinlist, plugin_sub_type); // recursion
			}
		}
	}
}

void Module_Info::RegisterPin(TiXmlElement* pin, module_info_pins_t* pinlist, int32_t plugin_sub_type, int& pin_id) // XML data passed in.
{
	pin_description2 pind;
	pind.datatype = plugin_sub_type == gmpi::MP_SUB_TYPE_AUDIO ? DT_FSAMPLE : DT_FLOAT;
	//		_RPT1(_CRT_WARN, "%s\n", pins[i]->GetXML() );

	// id
	pin->QueryIntAttribute("id", &(pin_id));

	// name
	pind.name = Utf8ToWstring(pin->Attribute("name"));

	// flags
	pind.flags = (plugin_sub_type == gmpi::MP_SUB_TYPE_AUDIO) ? 0 : IO_UI_COMMUNICATION;

	for (auto attrib = pin->FirstAttribute(); attrib; attrib = attrib->Next())
	{
		pind.flags |= CModuleFactory::PinFlagsFromXml(attrib->Name(), attrib->Value());
	}
	
#ifdef _DEBUG
	{
		// flags
		int32_t testCorrectness = (plugin_sub_type == gmpi::MP_SUB_TYPE_AUDIO) ? 0 : IO_UI_COMMUNICATION;
		SetPinFlag("private", IO_HIDE_PIN, pin, testCorrectness);
		SetPinFlag("autoRename", IO_RENAME, pin, testCorrectness);
		SetPinFlag("isFilename", IO_FILENAME, pin, testCorrectness);
		SetPinFlag("settableOutput", IO_SETABLE_OUTPUT, pin, testCorrectness);
		SetPinFlag("linearInput", IO_LINEAR_INPUT, pin, testCorrectness);
		SetPinFlag("ignorePatchChange", IO_IGNORE_PATCH_CHANGE, pin, testCorrectness);
		SetPinFlag("autoDuplicate", IO_AUTODUPLICATE, pin, testCorrectness);
		SetPinFlag("isMinimised", IO_MINIMISED, pin, testCorrectness);
		SetPinFlag("isPolyphonic", IO_PAR_POLYPHONIC, pin, testCorrectness);
		SetPinFlag("autoConfigureParameter", IO_AUTOCONFIGURE_PARAMETER, pin, testCorrectness);
		SetPinFlag("noAutomation", IO_PARAMETER_SCREEN_ONLY, pin, testCorrectness);
		SetPinFlag("redrawOnChange", IO_REDRAW_ON_CHANGE, pin, testCorrectness);
		SetPinFlag("isContainerIoPlug", IO_CONTAINER_PLUG, pin, testCorrectness);
		SetPinFlag("isAdderInputPin", IO_ADDER, pin, testCorrectness);
		
		assert(testCorrectness == pind.flags);
	}
#endif
	
	if (pin->Attribute("isParameter") != 0)
	{
		SafeMessagebox(0, (L"'isParameter' not allowed in pin XML"));
	}

	// parameter and host-connect pins. doing this first to determine expected pin datatype.
	int parameterId = -1;
	int parameterFieldId = FT_VALUE;
	int expectedPinDatatype = -1;
	{
		// parameter ID. Defaults to same as pin ID, but can be overridden.
		// Pins can be driven from patch-store.

		const auto parameterIdAt = pin->Attribute("parameterId");
		if (parameterIdAt)
		{
			sscanf(parameterIdAt, "%d", &parameterId);
		}

		// host-connect
		pind.hostConnect = Utf8ToWstring(pin->Attribute("hostConnect"));

		// parameterField.
		if (!pind.hostConnect.empty() || parameterId != -1)
		{
			const auto parameterField = pin->Attribute("parameterField");
			if (parameterField)
			{
				// field id can be stored as int (plugin XML), or as enum text (sems XML)
				if (isdigit(*parameterField))
				{
					sscanf(parameterField, "%d", &parameterFieldId);
				}
				else
				{
					if (!XmlStringToParameterField(parameterField, parameterFieldId))
					{
						std::wostringstream oss;
						oss << L"ERROR. module XML file (" << Filename() << L"): pin id " << pin_id << L" unknown Parameter Field ID.";

						Messagebox(oss);
					}
				}
			}

			if (parameterId != -1) // parameter pin
			{
				pind.flags |= (IO_PATCH_STORE | IO_HIDE_PIN);

				auto it = m_parameters.find(parameterId);
				if (it != m_parameters.end())
				{
					const auto param = *it->second;

					expectedPinDatatype = getFieldDatatype((ParameterFieldType)parameterFieldId);
					if (expectedPinDatatype == -1) // type must be same as parameter type
					{
						expectedPinDatatype = param.datatype;
					}
				}
			}
			else // host-connect pin
			{
				pind.flags |= IO_HOST_CONTROL | IO_HIDE_PIN;
				const auto hostControlId = (HostControls)StringToHostControl(pind.hostConnect.c_str());

				if (hostControlId == HC_NONE)
				{
					std::wostringstream oss;
					oss << L"ERROR. module XML: '" << pind.hostConnect << L"' unknown HOST CONTROL.";
					Messagebox(oss);
				}
				if (pind.direction != DR_IN && (hostControlId < HC_USER_SHARED_PARAMETER_INT0 || hostControlId > HC_USER_SHARED_PARAMETER_INT4))
				{
					std::wostringstream oss;
					oss << L"ERROR. module XML file (" << Filename() << L"): pin id " << pin_id << L" hostConnect pin wrong direction. Expected: direction=\"in\"";
					Messagebox(oss);
				}

				expectedPinDatatype = GetHostControlDatatype(hostControlId);
				if (expectedPinDatatype == DT_ENUM)
				{
					expectedPinDatatype = DT_INT;
				}
			}
		}
	}
//	bool isDtTest = this->m_unique_id == L"GMPI Datatypes";

	// datatype
	{
		const auto dt = pin->Attribute("datatype");
		if (dt)
		{
			const std::string pin_datatype(dt);

			int temp;
			if (XmlStringToDatatype(pin_datatype, temp))
			{
				pind.datatype = (EPlugDataType)temp;

				if (pind.datatype == DT_FLOAT)
				{
					const auto rate = pin->Attribute("rate");
					if (rate && strcmp(rate, "audio") == 0)
					{
						pind.datatype = DT_FSAMPLE;
					}
				}
				else if (pind.datatype == DT_CLASS) // e.g. "class:geometry"
				{
					if (pin_datatype.size() > 6)
						pind.classname = pin_datatype.substr(6);
				}
				else if (pind.datatype == DT_TEXT && GetExtension(Filename())== L"gmpi")
				{
					// by default GMPI uses UTF-8 encoding for strings. (SE uses UCS16)
					pind.datatype = DT_STRING_UTF8;
				}
			}
			else
			{
				std::wostringstream oss;
				oss << L"err. module XML file (" << Filename() << L"): parameter id " << pin_id << L" unknown datatype '" << Utf8ToWstring(pin_datatype) << L"'. Valid [float, int ,string, blob, midi ,bool ,enum ,double]";

				Messagebox(oss);
			}
		}
		else
		{
			// if not explicitly specified, take datatype from parameter or host-control.
			if (expectedPinDatatype != -1)
			{
				pind.datatype = (EPlugDataType)expectedPinDatatype;
			}
			else
			{
				// blank dataype defaults to DT_FSAMPLE, else it's an error.
				if(dt)
				{
					std::wostringstream oss;
					oss << L"err. module XML file (" << Filename() << L"): pin " << pin_id << L": unknown datatype. Valid [float, int ,string, blob, midi ,bool ,enum ,double]";

					Messagebox(oss);
				}
			}
		}
	}
	
	// default (depends on datatype)
	if (const auto s = pin->Attribute("default"); s)
	{
		// we need to be consistant when for example comparing default of "" with "0" or "0.0" etc.
		// else we get spurios "upgrading module" dialogs
		pind.default_value = uniformDefaultString(Utf8ToWstring(s), pind.datatype);

		// special-case Volts, need multiply by 10
		if (pind.datatype == DT_FSAMPLE && !pind.default_value.empty())
		{
			// multiply default by 10 (to Volts). DoubleToString() removes trailing zeros.
			pind.default_value = DoubleToString(10.0f * StringToFloat(pind.default_value));
		}
	}

	// direction
	std::string pin_direction = FixNullCharPtr(pin->Attribute("direction"));

	if (pin_direction.compare("out") == 0)
	{
		pind.direction = DR_OUT;

		if (!pind.default_value.empty())
		{
			SafeMessagebox(0, (L"module data: default not supported on output pin"));
		}
	}
	else
	{
		pind.direction = DR_IN;
	}

	std::wstring pin_automation = Utf8ToWstring(pin->Attribute("automation"));

	if (!pin_automation.empty())
	{
		SafeMessagebox(0, (L"'automation' not allowed in pin XML"));
	}

	// meta data
	pind.meta_data = Utf8ToWstring(pin->Attribute("metadata"));

	// notes
	pind.notes = Utf8ToWstring(pin->Attribute("notes"));

	if (pinlist->find(pin_id) != pinlist->end())
	{
		std::wostringstream oss;
		oss << L"ERROR. module XML file (" << Filename() << L"): pin id " << pin_id << L" used twice.";
		Messagebox(oss);
	}
	else
	{
		InterfaceObject* iob = new_InterfaceObjectC(pin_id, pind);
		iob->setParameterId(parameterId);
		iob->setParameterFieldId(parameterFieldId);

		// constraints
		// Can't have isParameter on output Gui Pin.
		if (iob->GetDirection() == DR_OUT && iob->isUiPlug() && iob->isParameterPlug())
		{
			std::wostringstream oss;
			oss << L"ERROR. module XML file (" << Filename() << L"): pin id " << pin_id << L" Can't have isParameter on output Gui Pin";
			Messagebox(oss);
		}

		// Patch Store pins must be hidden.
		if (iob->isParameterPlug() && !iob->DisableIfNotConnected())
		{
			std::wostringstream oss;
			oss << L"ERROR. module XML file (" << Filename() << L"): pin id " << pin_id << L" Patch-store input pins must have private=true.";
			Messagebox(oss);
		}

		// DSP parameters only support FT_VALUE
		if (iob->getParameterFieldId() != FT_VALUE && !iob->isUiPlug() && iob->isParameterPlug())
		{
			std::wostringstream oss;
			oss << L"ERROR. module XML file (" << Filename() << L"): pin id " << pin_id << L" parameterField not supported on Audio (DSP) pins.";
			Messagebox(oss);
		}

		// parameter used by pin but not declared in "Parameters" section.
		if (iob->isParameterPlug() && m_parameters.find(iob->getParameterId()) == m_parameters.end())
		{
			std::wostringstream oss;
			oss << L"ERROR. module XML file (" << Filename() << L"): pin id " << pin_id << L" parameter: " << iob->getParameterId() << L" not defined";
			Messagebox(oss);
		}

		// parameter used on autoduplicate pin.
		if (iob->isParameterPlug() && iob->autoDuplicate())
		{
			std::wostringstream oss;
			oss << L"ERROR. module XML file (" << Filename() << L"): pin id " << pin_id << L"  - can't have both 'autoDuplicate' and 'parameterId'";
			Messagebox(oss);
		}

		auto res = pinlist->insert(std::pair<int, InterfaceObject*>(pin_id, iob));
		assert(res.second && "pin already registered");
	}
}

void Module_Info::RegisterPin(tinyxml2::XMLElement* pin, module_info_pins_t* pinlist, int32_t plugin_sub_type, int& pin_id) // XML data passed in.
{
	int type_specific_flags = 0;

	if (plugin_sub_type != gmpi::MP_SUB_TYPE_AUDIO)
	{
		type_specific_flags = IO_UI_COMMUNICATION;
	}

	pin_description2 pind;
	pind.clear();
	//		_RPT1(_CRT_WARN, "%s\n", pins[i]->GetXML() );
	// id
//	int pin_id; // = XStr2Int( pin->Attribute("id") ));
	pin->QueryIntAttribute("id", &(pin_id));
	// name
	pind.name = Utf8ToWstring(pin->Attribute("name"));
	// datatype
	std::string pin_datatype = FixNullCharPtr(pin->Attribute("datatype"));
	std::string pin_rate = FixNullCharPtr(pin->Attribute("rate"));

	// Datatype.
	int temp;
	if (XmlStringToDatatype(pin_datatype, temp))
	{
		pind.datatype = (EPlugDataType)temp;
		if (pind.datatype == DT_CLASS) // e.g. "class:geometry"
		{
			if(pin_datatype.size() > 6)
				pind.classname = pin_datatype.substr(6);
		}
	}
	else
	{
		std::wostringstream oss;
		oss << L"err. module XML file (" << Filename() << L"): parameter id " << pin_id << L" unknown datatype '" << Utf8ToWstring(pin_datatype) << L"'. Valid [float, int ,string, blob, midi ,bool ,enum ,double]";
		Messagebox(oss);
	}

	// default
	pind.default_value = Utf8ToWstring(pin->Attribute("default"));

	if (pin_rate.compare("audio") == 0)
	{
		if (!pind.default_value.empty())
		{
			// multiply default by 10 (to Volts). DoubleToString() removes trilaing zeros.
			pind.default_value = DoubleToString(10.0f * StringToFloat(pind.default_value));
		}

		pind.datatype = DT_FSAMPLE;

		if (pin_datatype.compare("float") != 0)
		{
			std::wostringstream oss;
			oss << L"ERROR. module XML file (" << Filename() << L"): pin id " << pin_id << L" audio-rate not supported.";
			Messagebox(oss);
		}
	}

	// direction
	std::string pin_direction = FixNullCharPtr(pin->Attribute("direction"));

	if (pin_direction.compare("out") == 0)
	{
		pind.direction = DR_OUT;

		if (!pind.default_value.empty())
		{
			SafeMessagebox(0, (L"module data: default not supported on output pin"));
		}
	}
	else
	{
		pind.direction = DR_IN;
	}

	// flags
	pind.flags = type_specific_flags;
	for (auto attrib = pin->FirstAttribute(); attrib; attrib = attrib->Next())
	{
		pind.flags |= CModuleFactory::PinFlagsFromXml(attrib->Name(), attrib->Value());
	}
	
#ifdef _DEBUG
	{
		// flags
		int32_t testCorrectness = type_specific_flags;
		SetPinFlag("private", IO_HIDE_PIN, pin, testCorrectness);
		SetPinFlag("autoRename", IO_RENAME, pin, testCorrectness);
		SetPinFlag("isFilename", IO_FILENAME, pin, testCorrectness);
		SetPinFlag("settableOutput", IO_SETABLE_OUTPUT, pin, testCorrectness);
		SetPinFlag("linearInput", IO_LINEAR_INPUT, pin, testCorrectness);
		SetPinFlag("ignorePatchChange", IO_IGNORE_PATCH_CHANGE, pin, testCorrectness);
		SetPinFlag("autoDuplicate", IO_AUTODUPLICATE, pin, testCorrectness);
		SetPinFlag("isMinimised", IO_MINIMISED, pin, testCorrectness);
		SetPinFlag("isPolyphonic", IO_PAR_POLYPHONIC, pin, testCorrectness);
		SetPinFlag("autoConfigureParameter", IO_AUTOCONFIGURE_PARAMETER, pin, testCorrectness);
		SetPinFlag("noAutomation", IO_PARAMETER_SCREEN_ONLY, pin, testCorrectness);
		SetPinFlag("redrawOnChange", IO_REDRAW_ON_CHANGE, pin, testCorrectness);
		SetPinFlag("isContainerIoPlug", IO_CONTAINER_PLUG, pin, testCorrectness);
		SetPinFlag("isAdderInputPin", IO_ADDER, pin, testCorrectness);

		assert(testCorrectness == pind.flags);
	}
#endif
	
	std::wstring pin_automation = Utf8ToWstring(pin->Attribute("automation"));

	if (pin->Attribute("isParameter") != 0)
	{
		SafeMessagebox(0, (L"'isParameter' not allowed in pin XML"));
	}
	if (!pin_automation.empty())
	{
		SafeMessagebox(0, (L"'automation' not allowed in pin XML"));
	}

	// parameter ID. Defaults to ssame as pin ID, but can be overridden.
	// Pins can be driven from patch-store.
	int parameterId = -1;
	int parameterFieldId = FT_VALUE;
	std::wstring parameterIdString = Utf8ToWstring(pin->Attribute("parameterId"));

	if (parameterIdString.empty())
	{
		parameterId = -1; // pin_id;
	}
	else
	{
		parameterId = StringToInt(parameterIdString);
		pind.flags |= (IO_PATCH_STORE | IO_HIDE_PIN);
	}

	// host-connect
	pind.hostConnect = Utf8ToWstring(pin->Attribute("hostConnect"));

	// parameterField.
	if (!pind.hostConnect.empty() || parameterId != -1)
	{
		const std::string parameterField(FixNullCharPtr(pin->Attribute("parameterField")));

		if (parameterField.empty() || isdigit(parameterField[0]))
		{
			// In exported VST3s, just using int in XML. Faster, more compact.
//			pin->QueryIntAttribute("parameterField", &parameterFieldId);
			parameterFieldId = StringToInt(parameterField.c_str());
		}
		else
		{
			// see matching enum ParameterFieldType
			if (!XmlStringToParameterField(parameterField, parameterFieldId))
			{
				std::wostringstream oss;
				oss << L"ERROR. module XML file (" << Filename() << L"): pin id " << pin_id << L" unknown Parameter Field ID.";
				Messagebox(oss);
			}
		}
	}

	if (!pind.hostConnect.empty())
	{
		pind.flags |= IO_HOST_CONTROL | IO_HIDE_PIN;
		HostControls hostControlId = (HostControls)StringToHostControl(pind.hostConnect.c_str());
		if (hostControlId == HC_NONE)
		{
			std::wostringstream oss;
			oss << L"ERROR. module XML: '" << pind.hostConnect << L"' unknown HOST CONTROL.";
			Messagebox(oss);
		}

		if (parameterFieldId == FT_VALUE)
		{
			int expectedDatatype = GetHostControlDatatype(hostControlId);
			if (expectedDatatype == DT_ENUM)
			{
				expectedDatatype = DT_INT;
			}

			if (parameterFieldId == FT_VALUE && expectedDatatype != -1 && expectedDatatype != pind.datatype)
			{
				std::wostringstream oss;
				oss << L"ERROR. module XML file (" << Filename() << L"): pin id " << pin_id << L" hostConnect wrong datatype. Expected: " << expectedDatatype;
				Messagebox(oss);
			}
		}

		if (pind.direction != DR_IN && (hostControlId < HC_USER_SHARED_PARAMETER_INT0 || hostControlId > HC_USER_SHARED_PARAMETER_INT4))
		{
			std::wostringstream oss;
			oss << L"ERROR. module XML file (" << Filename() << L"): pin id " << pin_id << L" hostConnect pin wrong direction. Expected: direction=\"in\"";
			Messagebox(oss);
		}
	}

	// meta data
	pind.meta_data = Utf8ToWstring(pin->Attribute("metadata"));

	// notes
	pind.notes = Utf8ToWstring(pin->Attribute("notes"));

	if (pinlist->find(pin_id) != pinlist->end())
	{
		std::wostringstream oss;
		oss << L"ERROR. module XML file (" << Filename() << L"): pin id " << pin_id << L" used twice.";
		Messagebox(oss);
	}
	else
	{
		InterfaceObject* iob = new_InterfaceObjectC(pin_id, pind);
		// iob->setAutomation(controllerType);
		iob->setParameterId(parameterId);
		iob->setParameterFieldId(parameterFieldId);

		// constraints
		// Can't have isParameter on output Gui Pin.
		if (iob->GetDirection() == DR_OUT && iob->isUiPlug() && iob->isParameterPlug())
		{
			if (
				m_unique_id != L"Slider" && // exception for old crap.
				m_unique_id != L"List Entry" &&
				m_unique_id != L"Text Entry" &&
				m_group_name != L"Debug"
				)
			{
				std::wostringstream oss;
				oss << L"ERROR. module XML file (" << Filename() << L"): pin id " << pin_id << L" Can't have isParameter on output Gui Pin";
				Messagebox(oss);
			}
		}

		// Patch Store pins must be hidden.
		if (iob->isParameterPlug() && !iob->DisableIfNotConnected())
		{
			std::wostringstream oss;
			oss << L"ERROR. module XML file (" << Filename() << L"): pin id " << pin_id << L" Patch-store input pins must have private=true.";
			Messagebox(oss);
		}

		// DSP parameters only support FT_VALUE
		if (iob->getParameterFieldId() != FT_VALUE && !iob->isUiPlug() && iob->isParameterPlug())
		{
			std::wostringstream oss;
			oss << L"ERROR. module XML file (" << Filename() << L"): pin id " << pin_id << L" parameterField not supported on Audio (DSP) pins.";
			Messagebox(oss);
		}

		// parameter used by pin but not declared in "Parameters" section.
		if (iob->isParameterPlug() && m_parameters.find(iob->getParameterId()) == m_parameters.end())
		{
			std::wostringstream oss;
			oss << L"ERROR. module XML file (" << Filename() << L"): pin id " << pin_id << L" parameter: " << iob->getParameterId() << L" not defined";
			Messagebox(oss);
		}

		// parameter used on autoduplicate pin.
		if (iob->isParameterPlug() && iob->autoDuplicate())
		{
			std::wostringstream oss;
			oss << L"ERROR. module XML file (" << Filename() << L"): pin id " << pin_id << L"  - can't have both 'autoDuplicate' and 'parameterId'";
			Messagebox(oss);
		}

		auto res = pinlist->insert(std::pair<int, InterfaceObject*>(pin_id, iob));
		assert(res.second && "pin already registered");
	}
}

void Module_Info3_base::SetupPlugs()
{
	assert(false && "todo");
	/*
		if( LoadDllOnDemand() )
		{
			return;
		}

		// in case of re-load
		ClearPlugs();

		// instansiate ug
		ug_plugin *ug = (ug_plugin *) BuildSynthOb();

		if( ug )	// ignore display-only objects
		{
			SetupPlugs_pt2( ug );
			ug = 0; // deleted by SetupPlugs_pt2
		}
	*/
}

// SDK3 don't suuport DOcOb at present, hopefully won't need to.
//class CDocOb* Module_Info3_base::BuildDocObject()
//{
//	return doc_create(this);
//}

ug_base* Module_Info3_base::BuildSynthOb()
{
	return 0;
}

int Module_Info3_base::ModuleTechnology()
{
	return MT_SDK3;
}

Module_Info3_internal::Module_Info3_internal(const wchar_t* moduleId) :
	Module_Info3_base(moduleId)
{
}

#if 0
Module_Info3_internal::Module_Info3_internal(const char* xml) :
	Module_Info3_base(L"")
{
	TiXmlDocument doc2;
	doc2.Parse(xml);

	if (doc2.Error())
	{
		std::wostringstream oss;
		oss << L"Module XML Error: [SynthEdit.exe]" << doc2.ErrorDesc() << L"." << doc2.Value();
		Messagebox(oss);
	}
	else
	{
		auto pluginList = doc2.FirstChild("PluginList");
		auto node = pluginList->FirstChild("Plugin");
		assert(node);
		auto PluginElement = node->ToElement();
		m_unique_id = Utf8ToWstring(PluginElement->Attribute("id"));

		ScanXml(PluginElement);
	}
}
#endif

int32_t Module_Info3_internal::RegisterPluginConstructor(int subType, MP_CreateFunc2 create)
{
	auto res = factoryMethodList_.insert({ subType, create });

	/* might be needed?
	m_gui_registered = true; //or...
	m_dsp_registered = true;
	m_module_dll_available = true;
	*/

	return res.second ? gmpi::MP_OK : gmpi::MP_FAIL;
}

gmpi::IMpUnknown* Module_Info3_internal::Build(int subType, bool quietFail)
{
	// internal module (new type VST-GUI).
	auto it = factoryMethodList_.find(subType);

	if (it != factoryMethodList_.end())
	{
		return (*(*it).second)();
	}
	return nullptr;
}

ug_base* Module_Info3_internal::BuildSynthOb()
{
	// New way. Generic object factory.
	gmpi_sdk::mp_shared_ptr<gmpi::IMpUnknown> com_object;
	com_object.Attach(ModuleFactory()->Create(gmpi::MP_SUB_TYPE_AUDIO, UniqueId()));

	if (com_object)
	{
		// Query interface support.
		gmpi_sdk::mp_shared_ptr<gmpi::IMpPlugin2> plugin;
		com_object->queryInterface(gmpi::MP_IID_PLUGIN2, plugin.asIMpUnknownPtr());

		if (plugin)
		{
			// create and attach appropriate wrapper.
			auto ug = new ug_plugin3<gmpi::IMpPlugin2, gmpi::MpEvent>();
			ug->setModuleType(this);
			ug->flags = static_cast<UgFlags>(ug_flags);
			ug->latencySamples = latency;
			ug->AttachGmpiPlugin(plugin);
			plugin->setHost(static_cast<gmpi::IGmpiHost*>(ug));

			return ug;
		}

		gmpi::shared_ptr<gmpi::api::IProcessor> gmpi_plugin;
		com_object->queryInterface(*(gmpi::MpGuid*)&gmpi::api::IProcessor::guid, gmpi_plugin.put_void());

		if (gmpi_plugin)
		{
			// Now plugin safely created. Host it.
			auto ug = new ug_gmpi(this, gmpi_plugin);
			ug->flags = static_cast<UgFlags>(ug_flags);
			ug->latencySamples = latency;

			return ug;
		}
	}

	return {};
}
