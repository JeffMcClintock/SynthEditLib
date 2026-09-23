// SPDX-License-Identifier: ISC
// Copyright 2007-2026 Jeff McClintock.
#include "helpers/GmpiPluginEditor.h"
#include "helpers/GmpiPluginController.h"
#include "helpers/Timer.h"
#include "Extensions/ParameterIterator.h"

using namespace gmpi;
using namespace gmpi::editor;
using namespace gmpi::drawing;

namespace
{
// for display only. c.f. the 'datatype' attribute of a Parameter in the module's XML.
const char* datatypeName(gmpi::PinDatatype datatype)
{
	switch(datatype)
	{
	case gmpi::PinDatatype::Enum:       return "enum";
	case gmpi::PinDatatype::WideString: return "text";
	case gmpi::PinDatatype::Midi:       return "midi";
	case gmpi::PinDatatype::Float64:    return "double";
	case gmpi::PinDatatype::Bool:       return "bool";
	case gmpi::PinDatatype::Audio:      return "audio";
	case gmpi::PinDatatype::Float32:    return "float";
	case gmpi::PinDatatype::Int32:      return "int";
	case gmpi::PinDatatype::Int64:      return "int64";
	case gmpi::PinDatatype::Blob:       return "blob";
	case gmpi::PinDatatype::Struct:     return "struct";
	case gmpi::PinDatatype::String:     return "string_utf8";
	case gmpi::PinDatatype::Object:     return "object";
	default:                            return "?";
	};
}
}

class ParametersQueryGui final : public PluginEditor
{
	Pin<std::string> pinText;

 	void onSetText()
	{
		// pinText changed
		drawingHost->invalidateRect(&bounds);
	}

public:
	ParametersQueryGui()
	{
		pinText.onUpdate = [this](PinBase* p) { onSetText(); };
	}

	ReturnCode render(gmpi::drawing::api::IDeviceContext *drawingContext) override
	{
		Graphics g(drawingContext);

		auto backgroundBrush = g.createSolidColorBrush(Colors::Black);
		g.fillRectangle(bounds, backgroundBrush);

		auto textFormat = g.getFactory().createTextFormat();
		auto brush = g.createSolidColorBrush(Colors::White);

		g.drawTextU(pinText.value.c_str(), textFormat, bounds, brush);

		return ReturnCode::Ok;
	}
};

class ParametersQueryController final : public gmpi::controller::ControllerBase, public TimerClient
{
	Pin<std::string> pinText;
	bool parametersDirty{};

public:
	// IController
	ReturnCode initialize(gmpi::api::IUnknown* phost, int32_t phandle) override
	{
		ControllerBase::initialize(phost, phandle);

		// subscribe to parameter add/remove/change notifications. (not implemented yet)
		host->subscribe();

		refreshParams();

		startTimerHz(4);

		return ReturnCode::Ok;
	}

	bool onTimer() override
	{
		if(parametersDirty)
		{
			parametersDirty = false;
			refreshParams();
		}
		return true; // keep timer running.
	}

	void refreshParams()
	{
		synthedit::ParameterInformation info(host.get());

		std::string infoText;
		for(auto& param : info.parameters)
		{
			infoText += param.longName;
			infoText += " [" + std::string(datatypeName(param.datatype)) + "]";
			infoText += " handle: " + std::to_string(param.handle) + "\n";
		}

		pinText = infoText;
	}

	void onParameter(int32_t parameterHandle, gmpi::Field fieldId, int32_t voice, std::span<const uint8_t> data) override
	{
		parametersDirty = true; // debounce updates.
	}
};

namespace
{
auto r = Register<ParametersQueryGui>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<Plugin id="SE Parameters Query" name="Parameters Query" category="SDK Examples">
    <Parameters>
      <Parameter id="0" datatype="string_utf8" name="Results" private="true" ignorePatchChange="true" persistant="false" />
    </Parameters>
    <GUI graphicsApi="GmpiUi">
        <Pin name="Text" datatype="string_utf8" parameterId="0" isMinimised="true"/>
    </GUI>
    <Controller/>
</Plugin>
)XML");

auto rc = Register<ParametersQueryController>::withId("SE Parameters Query");
}

