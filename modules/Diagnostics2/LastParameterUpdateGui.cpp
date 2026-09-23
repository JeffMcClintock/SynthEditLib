// SPDX-License-Identifier: ISC
// Copyright 2007-2026 Jeff McClintock.
#include <cstring>
#include <map>
#include "helpers/GmpiPluginEditor.h"
#include "helpers/GmpiPluginController.h"
#include "helpers/unicode_conversion.h"
#include "Extensions/ParameterIterator.h"

using namespace gmpi;
using namespace gmpi::editor;
using namespace gmpi::drawing;

namespace
{
// read a fixed-size value, or fail if the data is the wrong size.
template<typename T>
bool readValue(std::span<const uint8_t> data, T& returnValue)
{
	if(data.size() != sizeof(T))
		return false;

	std::memcpy(&returnValue, data.data(), sizeof(T));
	return true;
}

template<typename T>
std::string numberToText(std::span<const uint8_t> data)
{
	T value{};
	return readValue(data, value) ? std::to_string(value) : "?";
}

std::string valueToText(gmpi::PinDatatype datatype, std::span<const uint8_t> data)
{
	switch(datatype)
	{
	case gmpi::PinDatatype::Float32:
		return numberToText<float>(data);

	case gmpi::PinDatatype::Float64:
		return numberToText<double>(data);

	case gmpi::PinDatatype::Int32:
	case gmpi::PinDatatype::Enum:
		return numberToText<int32_t>(data);

	case gmpi::PinDatatype::Int64:
		return numberToText<int64_t>(data);

	case gmpi::PinDatatype::Bool:
		return data.empty() ? "?" : (data[0] ? "true" : "false");

	case gmpi::PinDatatype::String:
		return { reinterpret_cast<const char*>(data.data()), data.size() };

	case gmpi::PinDatatype::WideString:
		return gmpi::unicode::to_utf8({ reinterpret_cast<const wchar_t*>(data.data()), data.size() / sizeof(wchar_t) });

	default:
		return "<" + std::to_string(data.size()) + " bytes>";
	}
}
}

// Displays the name and value of the most recent parameter update, anywhere in the patch.
class LastParameterUpdateGui final : public PluginEditor
{
	Pin<std::string> pinText;

public:
	LastParameterUpdateGui()
	{
		pinText.onUpdate = [this](PinBase*) { drawingHost->invalidateRect(&bounds); };
	}

	ReturnCode render(gmpi::drawing::api::IDeviceContext* drawingContext) override
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

class LastParameterUpdateController final : public gmpi::controller::ControllerBase
{
	Pin<std::string> pinText;

	struct ParameterDescription
	{
		std::string longName;
		gmpi::PinDatatype datatype{};
		bool isHostControl{};
	};
	std::map<int32_t, ParameterDescription> descriptions; // by handle.
	bool updating{};

	const ParameterDescription* describe(int32_t parameterHandle)
	{
		// listing every parameter is slow, so only re-list on meeting a new parameter.
		if(auto it = descriptions.find(parameterHandle); it != descriptions.end())
			return &it->second;

		descriptions.clear();

		synthedit::ParameterInformation info(host.get());
		for(auto& param : info.parameters)
			descriptions[param.handle] = { param.longName, param.datatype, param.isHostControl };

		if(auto it = descriptions.find(parameterHandle); it != descriptions.end())
			return &it->second;

		return {};
	}

public:
	// IController
	ReturnCode initialize(gmpi::api::IUnknown* phost, int32_t phandle) override
	{
		ControllerBase::initialize(phost, phandle);

		// get notified of changes to all parameters.
		host->subscribe();

		return ReturnCode::Ok;
	}

	void onParameter(int32_t parameterHandle, gmpi::Field fieldId, int32_t voice, std::span<const uint8_t> data) override
	{
		if(gmpi::Field::Value != fieldId || updating)
			return;

		// host-controls (e.g. Program Modified) update as a side-effect of every other parameter, and would hide them.
		auto description = describe(parameterHandle);
		if(!description || description->isHostControl)
			return;

		std::string text = description->longName + ": " + valueToText(description->datatype, data);
		if(voice != 0)
			text += " (voice " + std::to_string(voice) + ")";

		// my text is itself a parameter, so another of these modules would display it, which would update mine...
		// ignore updates caused by my own, to break the loop.
		updating = true;
		pinText = text;
		updating = false;
	}
};

namespace
{
auto r = Register<LastParameterUpdateGui>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<Plugin id="SE Last Parameter Update" name="Last Parameter Update" category="SDK Examples">
    <Parameters>
      <Parameter id="0" datatype="string_utf8" name="Text" private="true" ignorePatchChange="true" persistant="false" />
    </Parameters>
    <GUI graphicsApi="GmpiUi">
        <Pin name="Text" datatype="string_utf8" parameterId="0" isMinimised="true"/>
    </GUI>
    <Controller/>
</Plugin>
)XML");

auto rc = Register<LastParameterUpdateController>::withId("SE Last Parameter Update");
}
