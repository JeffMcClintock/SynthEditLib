// SPDX-License-Identifier: ISC
// Copyright 2007-2026 Jeff McClintock.
#include "helpers/GmpiPluginEditor.h"
#include "helpers/Timer.h"
#include "Extensions/ParameterIterator.h"

using namespace gmpi;
using namespace gmpi::editor;
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

class ParametersQueryController final : public gmpi::api::IController, public TimerClient
{
	int32_t handle{};
	gmpi::shared_ptr<gmpi::api::IControllerHost> host;
	bool parametersDirty{};
//	Pin<std::string> pinText;

	void onSetText()
	{
		// pinText changed
//		drawingHost->invalidateRect(&bounds);
	}

public:

	inline static gmpi::api::IController* constructingInstance{};

	ParametersQueryController()
	{
//		pinText.onUpdate = [this](PinBase* p) { onSetText(); };
	}

	// IController
	ReturnCode initialize(gmpi::api::IUnknown* phost, int32_t phandle) override
	{
		handle = phandle;
		phost->queryInterface(&gmpi::api::IControllerHost::guid, host.put_void());

		// subscribe to parameter add/remove/change notifications. (not implemented yet)
		host->subscribe();

		refreshParams();

		startTimerHz(4);

		return ReturnCode::Ok;
	}
	ReturnCode syncState() override {return ReturnCode::Ok;}

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

		// set my own parameter
//		pinText = infoText;
		constexpr int32_t voice{};
		host->setParameter(0, gmpi::Field::Value, voice, infoText.size(), (const uint8_t*)infoText.data());
	}

	// IParameterObserver
	ReturnCode setParameter(int32_t parameterIndex, gmpi::Field fieldId, int32_t voice, int32_t size, const uint8_t* data) override
	{
		parametersDirty = true; // debounce updates.
		return ReturnCode::Ok;
	}

	ReturnCode queryInterface(const gmpi::api::Guid* iid, void** returnInterface) override
	{
		GMPI_QUERYINTERFACE(gmpi::api::IController);
		GMPI_QUERYINTERFACE(gmpi::api::IParameterObserver);
		return ReturnCode::NoSupport;
	}
	GMPI_REFCOUNT;
};

namespace
{
auto r = Register<ParametersQueryGui>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<Plugin id="SE Parameters Query" name="Parameters Query" category="Sub-Controls">
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

