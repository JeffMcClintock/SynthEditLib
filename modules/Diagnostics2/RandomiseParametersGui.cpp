// SPDX-License-Identifier: ISC
// Copyright 2007-2026 Jeff McClintock.
#include <random>
#include "helpers/GmpiPluginEditor.h"
#include "helpers/GmpiPluginController.h"
#include "Extensions/ParameterIterator.h"

using namespace gmpi;
using namespace gmpi::editor;
using namespace gmpi::drawing;

// A 'Randomise' button. Clicking it sets every parameter in the patch to a random normalized value.
// The GUI just reports the button state (via a private parameter), the Controller does the work.
class RandomiseParametersGui final : public PluginEditor
{
	Pin<bool> pinTrigger;
	bool pressed{};

public:
	ReturnCode render(gmpi::drawing::api::IDeviceContext* drawingContext) override
	{
		Graphics g(drawingContext);

		auto backgroundBrush = g.createSolidColorBrush(pressed ? Colors::DimGray : Colors::Black);
		g.fillRectangle(bounds, backgroundBrush);

		auto brush = g.createSolidColorBrush(Colors::White);

		// stroke is centered on the rect, so pull it in to avoid clipping half of it.
		constexpr float strokeWidth = 1.0f;
		g.drawRectangle(inflateRect(bounds, -0.5f * strokeWidth), brush, strokeWidth);

		auto textFormat = g.getFactory().createTextFormat();
		textFormat.setTextAlignment(TextAlignment::Center);
		textFormat.setParagraphAlignment(ParagraphAlignment::Center);

		g.drawTextU("Randomise", textFormat, bounds, brush);

		return ReturnCode::Ok;
	}

	ReturnCode onPointerDown(Point point, int32_t flags) override
	{
		pressed = true;
		inputHost->setCapture();
		pinTrigger = true; // Controller randomises on the rising edge.
		drawingHost->invalidateRect(&bounds);
		return ReturnCode::Ok;
	}

	ReturnCode onPointerUp(Point point, int32_t flags) override
	{
		if(!pressed)
			return ReturnCode::Unhandled;

		pressed = false;
		inputHost->releaseCapture();
		pinTrigger = false;
		drawingHost->invalidateRect(&bounds);
		return ReturnCode::Ok;
	}
};

class RandomiseParametersController final : public gmpi::controller::ControllerBase
{
	Pin<bool> pinTrigger;
	gmpi::shared_ptr<gmpi::api::IParameterSetter> parameterSetter; // to set parameters other than my own.
	bool trigger{};
	std::mt19937 randomGenerator{ std::random_device{}() };

public:
	RandomiseParametersController()
	{
		pinTrigger.onUpdate = [this](PinBase*) { onSetTrigger(); };
	}

	// IController
	ReturnCode initialize(gmpi::api::IUnknown* phost, int32_t phandle) override
	{
		ControllerBase::initialize(phost, phandle);
		phost->queryInterface(&gmpi::api::IParameterSetter::guid, parameterSetter.put_void());

		return ReturnCode::Ok;
	}

	void onSetTrigger()
	{
		// act only on the rising edge, in case the host notifies the same value more than once.
		const bool risingEdge = pinTrigger.value && !trigger;
		trigger = pinTrigger.value;

		if(risingEdge)
			randomise();
	}

	void randomise()
	{
		if(!parameterSetter)
			return;

		std::uniform_real_distribution<float> distribution(0.0f, 1.0f);

		synthedit::ParameterInformation info(host.get());

		const auto triggerHandle = getParameterHandle(pinTrigger);

		for(auto& param : info.parameters)
		{
			if(param.isHostControl || param.datatype == gmpi::PinDatatype::Blob || param.handle == triggerHandle)
				continue;

			const float normalized = distribution(randomGenerator);
			constexpr int32_t voice{};
			parameterSetter->setParameter(param.handle, gmpi::Field::Normalized, voice, sizeof(normalized), (const uint8_t*)&normalized);
		}
	}
};

namespace
{
auto r = Register<RandomiseParametersGui>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<Plugin id="SE Randomise Parameters" name="Randomise Parameters" category="Sub-Controls">
    <Parameters>
      <Parameter id="0" datatype="bool" name="Trigger" private="true" ignorePatchChange="true" persistant="false" />
    </Parameters>
    <GUI graphicsApi="GmpiUi">
        <Pin name="Trigger" datatype="bool" parameterId="0" isMinimised="true"/>
    </GUI>
    <Controller/>
</Plugin>
)XML");

auto rc = Register<RandomiseParametersController>::withId("SE Randomise Parameters");
}
