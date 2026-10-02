#include "Processor.h"

using namespace gmpi;

// DSP side of 'SE: PatchMemGet' (its XML is in GmpiUiTest.cpp): outputs the patch value.
struct PatchMemGetProcessor final : public Processor
{
	FloatInPin pinPatchValue;
	FloatOutPin pinValue;

	void onSetPins() override
	{
		pinValue = pinPatchValue;
	}
};

namespace
{
auto r = Register<PatchMemGetProcessor>::withId("SE: PatchMemGet");
}
