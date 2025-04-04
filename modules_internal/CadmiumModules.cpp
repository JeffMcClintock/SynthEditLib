#include "mp_sdk_gui2.h"
#include "../se_sdk3/mp_sdk_audio.h"

SE_DECLARE_INIT_STATIC_FILE(CadmiumModules)

using namespace gmpi;

// Don't even need to make a class, just describe it in XML and register it as 'SeGuiInvisibleBase'
// then implement it in GUI_3_0.cpp

namespace
{
	/*
	auto r1 = sesdk::Register<SeGuiInvisibleBase>::withXml(
		R"XML(
<?xml version="1.0" ?>
<PluginList>
  <Plugin id="SE CadmiumRenderer" name="CadmiumRenderer" category="Debug">
    <GUI graphicsApi="GmpiGui">
      <Pin name="JSON" datatype="string"/>
    </GUI>
  </Plugin>
</PluginList>
)XML");
*/
	auto r2 = sesdk::Register<SeGuiInvisibleBase>::withXml(
		R"XML(
<?xml version="1.0" ?>
<PluginList>
  <Plugin id="SE Render" name="Render" category="Experimental/Graphics">
    <GUI graphicsApi="Cadmium">
      <Pin name="Brush" datatype="class:brush"/>
      <Pin name="Geometry" datatype="class:geometry"/>
    </GUI>
  </Plugin>
</PluginList>
)XML");

	auto r3 = sesdk::Register<SeGuiInvisibleBase>::withXml(
		R"XML(
<?xml version="1.0" ?>
<PluginList>
  <Plugin id="CD Square" name="Square" category="Experimental/Graphics">
    <GUI graphicsApi="Cadmium">
      <Pin name="Size" datatype="float" />
      <Pin name="Center" datatype="class:point" />
      <Pin name="Geometry" datatype="class:geometry" direction="out" />
    </GUI>
  </Plugin>
</PluginList>
)XML");

	auto r4 = sesdk::Register<SeGuiInvisibleBase>::withXml(
		R"XML(
<?xml version="1.0" ?>
<PluginList>
  <Plugin id="CD Circle" name="Circle" category="Experimental/Graphics">
    <GUI graphicsApi="Cadmium">
      <Pin name="Radius" datatype="float" />
      <Pin name="Geometry" datatype="class:geometry" direction="out" />
    </GUI>
  </Plugin>
</PluginList>
)XML");

	auto r5 = sesdk::Register<SeGuiInvisibleBase>::withXml(
		R"XML(
<?xml version="1.0" ?>
<PluginList>
  <Plugin id="SE Solid Color Brush" name="Solid Color Brush" category="Experimental/Graphics">
    <GUI graphicsApi="Cadmium">
      <Pin name="Color" datatype="string" default="aaffa500"/>
      <Pin name="Brush" datatype="class:brush" direction="out" />
    </GUI>
  </Plugin>
</PluginList>
)XML");

	auto r6 = sesdk::Register<SeGuiInvisibleBase>::withXml(
		R"XML(
<?xml version="1.0" ?>
<PluginList>
  <Plugin id="CD Pointer" name="Pointer" category="Experimental/Graphics">
    <GUI graphicsApi="Cadmium">
      <Pin name="Point" datatype="class:point" direction="out" />
    </GUI>
  </Plugin>
</PluginList>
)XML");

	auto r7 = sesdk::Register<SeGuiInvisibleBase>::withXml(
		R"XML(
<?xml version="1.0" ?>
<PluginList>
  <Plugin id="CD Point2Values" name="Point to Values" category="Experimental/Graphics">
    <GUI graphicsApi="Cadmium">
      <Pin name="Point" datatype="class:point"  />
      <Pin name="x" datatype="float" direction="out" />
      <Pin name="y" datatype="float" direction="out" />
    </GUI>
  </Plugin>
</PluginList>
)XML");

	auto r8 = sesdk::Register<SeGuiInvisibleBase>::withXml(
		R"XML(
<?xml version="1.0" ?>
<PluginList>
  <Plugin id="CD Apply" name="Apply" category="Experimental/Graphics">
    <GUI graphicsApi="Cadmium">
      <Pin name="List" datatype="class:list" />
    </GUI>
  </Plugin>
</PluginList>
)XML");

	auto r9 = sesdk::Register<SeGuiInvisibleBase>::withXml(
		R"XML(
<?xml version="1.0" ?>
<PluginList>
  <Plugin id="CD Make List" name="Make List" category="Experimental/Graphics">
    <GUI graphicsApi="Cadmium">
      <Pin name="List" datatype="class:list" direction="out" />
      <Pin name="in" datatype="class:any" autoDuplicate="true" />
    </GUI>
  </Plugin>
</PluginList>
)XML");


    auto rA = sesdk::Register<SeGuiInvisibleBase>::withXml(
        R"XML(
<?xml version="1.0" ?>
<PluginList>
  <Plugin id="CD Render Function" name="Render Function" category="Experimental/Graphics">
    <GUI graphicsApi="Cadmium">
      <Pin name="Function" datatype="class:func" direction="out" />
    </GUI>
  </Plugin>
</PluginList>
)XML");

}

class Value : public MpBase2
{
public:
	Value()
	{
		initializePin(pinValueHostIn);
		initializePin(pinValueHostOut);
		initializePin(pinLatchInput);
		initializePin(pinValueIn);
		initializePin(pinValueOut);
	}

	void onSetPins() override
	{
		float value;
		if (pinLatchInput.getValue())
		{
			value = pinValueIn.getValue();
		}
		else
		{
			value = pinValueHostIn.getValue();
		}

		pinValueOut = value;
		pinValueHostOut = value;

		// When updating patch from DSP, SE does not reflect the value back to pinValueHostIn automatically, fake it.
		pinValueHostIn.setValueRaw(sizeof(value), &value);
	}

private:
	FloatInPin pinValueHostIn;
	FloatOutPin pinValueHostOut;
	BoolInPin pinLatchInput;
	FloatInPin pinValueIn;
	FloatOutPin pinValueOut;
};

namespace
{
	auto r20 = sesdk::Register<Value>::withXml(
R"XML(
<?xml version="1.0" ?>
<PluginList>
  <!-- equivalent of PatchMemory - float !!maybe should be 'double' everywhere??? -->
  <Plugin id="CD Value" name="Value" category="Experimental/Graphics">
    <Parameters>
      <Parameter id="0" datatype="float" />
    </Parameters>
    <GUI graphicsApi="Cadmium">
      <Pin name="Value In" datatype="float" />
      <Pin name="Value Out" datatype="float" direction="out" />
      <Pin name="Normalized" datatype="float" direction="out" />
      <Pin name="ID" datatype="int" direction="out" />
    </GUI>
    <Audio>
      <Pin id="0" name="PM In" datatype="float" parameterId="0" />
      <Pin id="1" name="PM Out" direction="out" datatype="float" parameterId="0" private ="true"/>
      <Pin id="2" name="In Enabled" datatype="bool" />
      <Pin id="3" name="Value In" datatype="float" />
      <Pin id="4" name="Value Out" direction="out" datatype="float" />
    </Audio>
  </Plugin>
</PluginList>
)XML");
}

namespace
{
    auto r21 = sesdk::Register<SeGuiInvisibleBase>::withXml(
        R"XML(
<?xml version="1.0" ?>
<PluginList>
  <Plugin id="CD ValueSink" name="Value Sink" category="Experimental/Graphics">
    <GUI graphicsApi="Cadmium">
      <Pin name="ID" datatype="int" direction="in" />
      <Pin name="Normalized" datatype="float" />
    </GUI>
  </Plugin>
</PluginList>
)XML");
}

namespace
{
    auto r22 = sesdk::Register<SeGuiInvisibleBase>::withXml(
        R"XML(
<?xml version="1.0" ?>
<PluginList>
  <Plugin id="CD Mouse2Value" name="Value Dragger" category="Experimental/Graphics">
    <GUI graphicsApi="Cadmium">
      <Pin name="Normalized" datatype="float" />
      <Pin name="Normalized" datatype="float" direction="out"/>
      <Pin name="Drag" datatype="float" />
    </GUI>
  </Plugin>
</PluginList>
)XML");
}

namespace
{
    auto r23 = sesdk::Register<SeGuiInvisibleBase>::withXml(
        R"XML(
<?xml version="1.0" ?>
<PluginList>
  <Plugin id="CD ValueIn" name="Value In" category="Experimental/Graphics">
    <GUI graphicsApi="Cadmium">
      <Pin name="ID" datatype="int" direction="out" />
      <Pin name="Normalized" datatype="float" direction="out" />
    </GUI>
  </Plugin>
</PluginList>
)XML");
}
