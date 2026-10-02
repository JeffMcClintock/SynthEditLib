#include "Processor.h"

using namespace gmpi;

// DSP side of 'SE: PatchMemMeter' (its XML is in GmpiUiTest.cpp): the input drives the patch value.
struct PatchMemMeterProcessor final : public Processor
{
	FloatInPin pinValue;
	FloatOutPin pinPatchValue;

	void onSetPins() override
	{
		pinPatchValue = pinValue;
	}
};

namespace
{
auto r = Register<PatchMemMeterProcessor>::withId("SE: PatchMemMeter");
}
