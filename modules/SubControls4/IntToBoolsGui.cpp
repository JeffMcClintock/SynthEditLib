#include <algorithm>
#include "helpers/GmpiPluginEditor.h"
#include "Extensions/PinCount.h"

using namespace gmpi;
using namespace gmpi::editor;

// SE_DECLARE_INIT_STATIC_FILE(IntToBoolsGui2)

// same code works for int-to-bools and also bools-to-int, just different XMLs
class IntToBoolsGui final : public PluginEditorNoGui
{
    Pin<int32_t> intPin;
    std::vector< Pin<bool> > boolPins;

public:
    ReturnCode initialize() override
    {
        // handle updates on the int pin.
        intPin.onUpdate = [this](PinBase*)
            {
                const int newOutputPin = intPin.value;

				for(int i = 0; i < boolPins.size(); ++i)
                    boolPins[i] = (i == newOutputPin);
            };

		// get the number of pins (minus the int pin) so we know how many bool pins to create.
        synthedit::PinInformation info(editorHost);
        const auto boolPinCount = static_cast<int>(info.pins.size()) - 1;

        // create the bool pins dynamically.
		boolPins.reserve(boolPinCount);
        for(int i = 0; i < boolPinCount; ++i)
        {
            boolPins.emplace_back();
			auto& pin = boolPins.back();

            pin.host = editorHost.get();
			init(pin);

            // handle updates on the bool pins
            boolPins.back().onUpdate = [this, i](PinBase*)
                {
                    if(boolPins[i].value)
                        intPin = i;
                };
        }

        return PluginEditorNoGui::initialize();
    }
};

namespace
{
auto r = Register<IntToBoolsGui>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<Plugin id="SE:Bools To Int2" name="Bools To Int2" category="Sub-Controls/Conversion">
    <GUI>
        <Pin name="Int Val" datatype="int" direction="out"/>
        <Pin name="Bool Val" datatype="bool" autoRename="true" autoDuplicate="true"/>
    </GUI>
</Plugin>
)XML");

auto r2 = Register<IntToBoolsGui>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<Plugin id="SE:Int to Bools2" name="Int to Bools2" category="Sub-Controls/Conversion">
    <GUI>
        <Pin name="Value" datatype="int"/>
        <Pin name="Spare" datatype="bool" direction="out" autoRename="true" autoDuplicate="true"/>
    </GUI>
</Plugin>
)XML");
}