#include "GuitarBody.h"
#include <cmath>

SE_DECLARE_INIT_STATIC_FILE(GuitarBody);

using namespace gmpi;

namespace {
bool registered = Register<GuitarBody>::withXml(R"XML(
<?xml version="1.0" encoding="utf-8" ?>
<PluginList>
  <Plugin id="SE Guitar Body" name="Guitar Body" category="Debug">
	<Audio>
	  <Pin name="Signal In" datatype="float" rate="audio"/>
	  <Pin name="Signal Out" direction="out" datatype="float" rate="audio"/>
	  <Pin name="Size" datatype="float" default="1"/>
	  <Pin name="Resonance" datatype="float" default="0.5"/>
	  <Pin name="Brightness" datatype="float" default="0.5"/>
	  <Pin name="Mix" datatype="float" default="1"/>
	</Audio>
  </Plugin>
</PluginList>
)XML");

constexpr double quiet = 1e-6;       // summed resonator output, -120 dB.
constexpr double quietSeconds = 0.03; // longer than a period of the lowest mode at the largest size.
} // namespace

ReturnCode GuitarBody::open(api::IUnknown* phost)
{
	auto r = Processor::open(phost);
	sampleRate_ = host->getSampleRate();
	quietSamples_ = static_cast<int>(quietSeconds * sampleRate_);
	return r;
}

void GuitarBody::onSetPins()
{
	const guitar_body::Settings settings{ pinSize, pinResonance, pinBrightness, pinMix };
	const bool changed = !setUp_ || !(settings == settings_);
	if (changed)
	{
		settings_ = settings;
		setUp_ = true;
		bank_.setup(settings_, sampleRate_);
	}

	// Anything new to hear wakes the bank; it rings out and settles again by itself.
	if (pinSignalIn.isStreaming() || pinSignalIn.isUpdated() || changed || !settled_)
	{
		settled_ = false;
		quietCount_ = 0;
		pinSignalOut.setStreaming(true);
		setSubProcess(&GuitarBody::subProcess);
	}
}

void GuitarBody::subProcess(int sampleFrames)
{
	const float* in = getBuffer(pinSignalIn);
	float* out = getBuffer(pinSignalOut);
	const bool inputStreaming = pinSignalIn.isStreaming();

	for (int s = 0; s < sampleFrames; ++s)
	{
		double ringing;
		out[s] = bank_.process(in[s], ringing);

		if (!inputStreaming)
		{
			quietCount_ = std::abs(ringing) < quiet ? quietCount_ + 1 : 0;
			if (quietCount_ >= quietSamples_)
			{
				// A non-streaming output must hold one value from the stop position on.
				const float steady = bank_.steadyOutput(in[s]);
				for (int r = s; r < sampleFrames; ++r)
					out[r] = steady;
				settle(s);
				return;
			}
		}
	}

	bank_.flushDenormals();
}

void GuitarBody::settle(int blockOffset)
{
	settled_ = true;

	// Inside a subProcess the block position isn't exact; streaming changes want one.
	TempBlockPositionSetter positionScope(this, getBlockPosition() + blockOffset);
	pinSignalOut.setStreaming(false);
	setSubProcess(&GuitarBody::subProcessSettled);
}

void GuitarBody::subProcessSettled(int sampleFrames)
{
	const float steady = bank_.steadyOutput(*getBuffer(pinSignalIn));
	float* out = getBuffer(pinSignalOut);
	for (int s = 0; s < sampleFrames; ++s)
		out[s] = steady;
}
