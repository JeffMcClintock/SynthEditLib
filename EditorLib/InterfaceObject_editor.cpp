#include "InterfaceObject_editor.h"
#include "Plug.h"
#include "./Plug_decorator_autoduplicate.h"
#include "./Plug_decorator_namable.h"
#include "tinyxml/tinyxml.h"

// ref also : Plug_decorator_default::ExportXml
class TiXmlElement* InterfaceObject_editor::ExportXml(IPlug* self)
{
	// Settable outputs MUST set 'Default' attribute, else SE fails to create a buffer for the pin in ug_base::Setup
	if (isSettableOutput() /*&& self->GetDefault() == DefaultVal*/)
	{
		auto plugElement = new TiXmlElement("Plug");
		plugElement->SetAttribute("Default", WStringToUtf8(DefaultVal));
		return plugElement;
	}

	// No need to inform DSP of default values (it knows them already).
	return {};
}

sRange InterfaceObject_editor::GetDefaultRange(IPlug* self)
{
	return sRange(subtype);
}

std::wstring InterfaceObject_editor::getFileExt(IPlug* self)
{
	if (self->is_filename())	// default enum list doubles as file extension for data type 'file'
	{
		return self->getDefaultEnumList();
	}

	return {};
}

void InterfaceObject_editor::setName(IPlug* self, const std::wstring& p_name)
{
	if (p_name != GetName())
	{
		IPlugDescriptionDecorator* pd = self->AddDecorator(new Plug_decorator_namable());
		pd->setName(self, p_name);
	}
};

void InterfaceObject_editor::setPlugDescID(IPlug* self, int id)		// autoduplicate plugs only (SDK3).
{
	assert(self->autoDuplicate());

	if (id != m_id)
	{
		self->AddDecorator(new Plug_decorator_autoduplicate(id));
	}
}

void InterfaceObject_editor::Export(class Json::Value& pins_json, ExportFormatType targetType)
{
	//TiXmlElement* pinXml = new TiXmlElement("Pin");
	//DspXml->LinkEndChild(pinXml);
	Json::Value pin_json(Json::objectValue);

	//pinXml->SetAttribute("name", WStringToUtf8(GetName()));
	//pinXml->SetAttribute("datatype", XmlStringFromDatatype(GetDatatype()));
	pin_json["name"] = WStringToUtf8(GetName());
	pin_json["type"] = XmlStringFromDatatype(GetDatatype());

	if (GetDatatype() == DT_FSAMPLE)
	{
		//pinXml->SetAttribute("rate", "audio");
//		pin_json["rate"] = "audio";
		pin_json["type"] = "audio"; // combined "float"/"rate=audio" -> just "audio".
	}

	const char* direction = 0; // or "in" (default)
	if (GetDirection({}) == DR_OUT)
	{
		direction = "out";
	}
	else
	{
		auto defaultStr = DefaultVal; // GetDefault(0);
		if (!defaultStr.empty())
		{
			bool needsDefault = true;
			// In SDK3 Audio data defaults are literal (not volts).
			if (GetDatatype() == DT_FSAMPLE || GetDatatype() == DT_FLOAT || GetDatatype() == DT_DOUBLE)
			{
				float d = StringToFloat(defaultStr); // won't cope with large 64bit doubles.

				if (GetDatatype() == DT_FSAMPLE)
				{
					d *= 0.1f; // Volts to actual value.
				}

				defaultStr = FloatToString(d);

				needsDefault = d != 0.0f;
			}
			if (GetDatatype() == DT_ENUM || GetDatatype() == DT_BOOL || GetDatatype() == DT_INT || GetDatatype() == DT_INT64)
			{
				int d = StringToInt(defaultStr); // won't cope with large 64bit ints.
				defaultStr = IntToString(d);

				needsDefault = d != 0;
			}

			if (needsDefault)
			{
				//pinXml->SetAttribute("default", WStringToUtf8(default));
				pin_json["default"] = WStringToUtf8(defaultStr);
			}
		}
	}

	if (direction)
	{
		//pinXml->SetAttribute("direction", direction);
		pin_json["direction"] = direction;
	}

	if (DisableIfNotConnected())
	{
		//pinXml->SetAttribute("private", "true");
		pin_json["private"] = "true";
	}
	if (isRenamable())
	{
		//pinXml->SetAttribute("autoRename", "true");
		pin_json["autoRename"] = "true";
	}
	if (is_filename())
	{
		//pinXml->SetAttribute("isFilename", "true");
		pin_json["isFilename"] = "true";
	}
	if ((GetFlags() & IO_LINEAR_INPUT) != 0)
	{
		//pinXml->SetAttribute("linearInput", "true");
		pin_json["linearInput"] = "true";
	}
	if ((GetFlags() & IO_IGNORE_PATCH_CHANGE) != 0)
	{
		//pinXml->SetAttribute("ignorePatchChange", "true");
		pin_json["ignorePatchChange"] = "true";
	}
	if ((GetFlags() & IO_AUTODUPLICATE) != 0)
	{
		//pinXml->SetAttribute("autoDuplicate", "true");
		pin_json["autoDuplicate"] = "true";
	}
	if ((GetFlags() & IO_MINIMISED) != 0)
	{
		//pinXml->SetAttribute("isMinimised", "true");
		pin_json["isMinimised"] = "true";
	}
	if ((GetFlags() & IO_PAR_POLYPHONIC) != 0)
	{
		//pinXml->SetAttribute("isPolyphonic", "true");
		pin_json["isPolyphonic"] = "true";
	}
	if ((GetFlags() & IO_AUTOCONFIGURE_PARAMETER) != 0)
	{
		//pinXml->SetAttribute("autoConfigureParameter", "true");
		pin_json["autoConfigureParameter"] = "true";
	}
	if ((GetFlags() & IO_PARAMETER_SCREEN_ONLY) != 0)
	{
		//pinXml->SetAttribute("noAutomation", "true");
//		pin_json["noAutomation"] = "true";
		pin_json["isMinimised"] = "true";
	}

	if (getParameterId({}) != -1)
	{
		pin_json["parameterId"] = getParameterId({});

		if (getParameterFieldId() != FT_VALUE)
		{
			//pinXml->SetAttribute("parameterField", XmlStringFromParameterField(getParameterFieldId(0)));
			pin_json["parameterField"] = XmlStringFromParameterField(getParameterFieldId());
		}
	}

	HostControls hostControlId = getHostConnect();
	if (hostControlId != HC_NONE)
	{
		pin_json["hostConnect"] = WStringToUtf8(GetHostControlName(hostControlId));
		if (getParameterFieldId() != FT_VALUE)
		{
			pin_json["parameterField"] = XmlStringFromParameterField(getParameterFieldId());
		}
	}

	if (!GetEnumList().empty())
	{
		//		pinXml->SetAttribute("metadata", WStringToUtf8(GetEnumList()));
		pin_json["metadata"] = WStringToUtf8(GetEnumList());
	}

	pins_json.append(pin_json);
}
