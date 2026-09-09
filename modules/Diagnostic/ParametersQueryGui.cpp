// SPDX-License-Identifier: ISC
// Copyright 2007-2026 Jeff McClintock.
#include "helpers/GmpiPluginEditor.h"

using namespace gmpi;
using namespace gmpi::editor;
using namespace gmpi::editor;
using namespace gmpi::drawing;

class ParametersQueryGui final : public PluginEditor
{
 	void onSetText()
	{
		// pinText changed
	}

 	Pin<std::string> pinText;

public:
	ParametersQueryGui() = default;

	ReturnCode render(gmpi::drawing::api::IDeviceContext *drawingContext) override
	{
		Graphics g(drawingContext);

		auto textFormat = g.getFactory().createTextFormat();
		auto brush = g.createSolidColorBrush(Colors::Red);

		g.drawTextU("Hello World!", textFormat, bounds, brush);

		return ReturnCode::Ok;
	}
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
</Plugin>
)XML");
}

