#include "helpers/GmpiPluginEditor.h"
//#include "module_register.h"

using namespace gmpi;
using namespace gmpi::editor;
using namespace gmpi::api;

// SE_DECLARE_INIT_STATIC_FILE(PluginUIScaleGui)
void se_static_library_init_PluginUIScaleGui() {}

// Exposes the HC_PLUGIN_UI_SCALE host control to the SE canvas so the user
// can wire a knob/slider/combo to it. The host-control value is a persistent
// per-plugin user preference (not automated, not preset state); the controller
// loads/saves it to Preferences.xml and triggers a plugin-window resize when it
// changes.
class PluginUIScaleGui final : public PluginEditorNoGui
{
	Pin<float> pinHostUIScale;  // hostConnect="Plugin/UIScale"
	Pin<float> pinUIScale;      // exposed to canvas widgets

	bool suppress = false;      // guard: avoid ping-pong between the two pins

public:
	PluginUIScaleGui()
	{
		pinHostUIScale.onUpdate = [this](PinBase*)
		{
			if (suppress) return;
			suppress = true;
			pinUIScale = pinHostUIScale.value;
			suppress = false;
		};

		pinUIScale.onUpdate = [this](PinBase*)
		{
			if (suppress) return;
			suppress = true;
			pinHostUIScale = pinUIScale.value;
			suppress = false;
		};
	}
};

namespace
{
	auto r = gmpi::Register<PluginUIScaleGui>::withXml(R"XML(
<?xml version="1.0" encoding="utf-8" ?>
<PluginList>
  <Plugin id="SE: Plugin UI Scale" name="Plugin UI Scale" category="Special" vendor="SynthEdit Ltd" >
    <GUI graphicsApi="GmpiGui">
      <Pin name="Host UI Scale" direction="in" datatype="float" hostConnect="Plugin/UIScale"/>
      <Pin name="UI Scale" direction="out" datatype="float"/>
    </GUI>
  </Plugin>
</PluginList>
)XML");
}
