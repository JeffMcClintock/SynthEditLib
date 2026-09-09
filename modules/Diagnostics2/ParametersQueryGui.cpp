// SPDX-License-Identifier: ISC
// Copyright 2007-2026 Jeff McClintock.
#include "helpers/GmpiPluginEditor.h"
#include "Extensions/ParameterIterator.h"

using namespace gmpi;
using namespace gmpi::editor;
using namespace gmpi::editor;
using namespace gmpi::drawing;

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

class ParametersQueryController final : public gmpi::api::IController
{
	int32_t handle{};
	gmpi::shared_ptr<gmpi::api::IControllerHost> host;

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

		// todo: sign up to notifications about parameters add/remove/change, and update the GUI string accordingly. perhaps sighing up causes the callback to init all params without the need for a special iterateParameters() call.
		synthedit::ParameterInformation info(phost);

		std::string infoText;
		for(auto& param : info.parameters)
		{
			infoText += "Parameter handle: " + std::to_string(param.handle) + ", datatype: " + std::to_string(static_cast<int>(param.datatype)) + "\n";
		}

//		pinText = infoText;
		constexpr int32_t voice{};
		host->setParameter(0, gmpi::Field::Value, voice, infoText.size(), (const uint8_t*) infoText.data());

		return ReturnCode::Ok;
	}
	ReturnCode syncState() override {return ReturnCode::Ok;}

	// IParameterObserver
	ReturnCode setParameter(int32_t parameterIndex, gmpi::Field fieldId, int32_t voice, int32_t size, const uint8_t* data) override
	{
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

