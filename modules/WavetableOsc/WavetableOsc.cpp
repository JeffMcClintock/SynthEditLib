#include "./WavetableOsc.h"
#include "Extensions/EmbeddedFile.h"

#undef min
#undef max

SE_DECLARE_INIT_STATIC_FILE(WavetableOsc);

using namespace gmpi;

namespace {
bool registered = Register<WavetableOsc>::withXml(R"XML(
<?xml version="1.0" encoding="utf-8" ?>
<PluginList>
  <Plugin id="SE Wavetable Osc" name="Wavetable Osc" category="Waveform" helpUrl="WavetableOsc.htm">
	<Audio>
	  <Pin name="Pitch" datatype="float" rate="audio" default="0.5"/>
	  <Pin name="Slot" datatype="float" rate="audio" />
	  <Pin name="Signal Out" direction="out" datatype="float" rate="audio"/>
	  <Pin name="VoiceActive" hostConnect="Voice/Active" datatype="float" isPolyphonic="true" default="1" />
	  <Pin name="WaveTableFile" datatype="string" isFilename="true" metadata="wav" />
	</Audio>
  </Plugin>
  <Plugin id="SE Wavetable Display" name="Wavetable Display" category="Waveform">
	<GUI>
	  <Pin name="WaveTableFile" datatype="string" isFilename="true" metadata="wav" />
	  <Pin name="Slot" datatype="float" />
	</GUI>
  </Plugin>
</PluginList>
)XML");
}

WavetableOsc::WavetableOsc()
{
	// Equal-amplitude crossfade table: curve[i] + curve[N - i] = 1 exactly, so a
	// fade-in + fade-out pair always sums to unity gain. Without this property the
	// crossfade midpoint boosts the signal by ~17% and shows up as a visible blip
	// on the output, especially for harmonically-rich waveforms (ramp, saw).
	for (int i = 0; i < (int)syncFadeCurve_.size(); ++i)
	{
		syncFadeCurve_[i] = 0.5f - 0.5f * cosf((float)M_PI * (float)i / (float)syncCrossFadeSamples);
	}
}

namespace {
// Pitch pin is normalised 0..1 across a 10 V range; A4 (440 Hz) sits at the midpoint.
constexpr double kMaxVolts = 10.0;

constexpr double sampleToVoltage(double s)
{
	return s * kMaxVolts;
}

inline double sampleToFrequency(double s)
{
	return 440.0 * exp2(sampleToVoltage(s) - kMaxVolts * 0.5);
}
} // namespace

ReturnCode WavetableOsc::open(api::IUnknown* phost)
{
	auto r = Processor::open(phost);

	const float sampleRate = host->getSampleRate();
	sampleRate_ = sampleRate;

	// Pitch lookup table - shared across instances at same sample rate.
	const int extraEntriesAtStart = 1; // for interpolator.
	const int extraEntriesAtEnd = 3; // for interpolator.
	const int pitchTableSize = extraEntriesAtStart + extraEntriesAtEnd + (pitchTableHiVolts - pitchTableLowVolts) * 12;
	constexpr double oneSemitone = 1.0/12.0;

	pitchTableShared_ = SharedObjectManager<PitchTableData>::getOrCreateSharedMemory(
		sampleRate, 0,
		[&](float sr) {
			auto p = std::make_shared<PitchTableData>();
			p->data.resize(pitchTableSize);
			for (int i = 0; i < pitchTableSize; ++i)
			{
				double pitch = (pitchTableLowVolts + (i - extraEntriesAtStart) * oneSemitone) * 0.1;
				double hz = sampleToFrequency(pitch);
				p->data[i] = hz / (double)sr;
			}
			return p;
		});
	pitchTable = pitchTableShared_->data.data() + extraEntriesAtStart;

	// Wavetable buffer allocation happens lazily in onSetPins, via the
	// process-wide WavetableCache keyed on (URI, sample rate).

	return r;
}

using WavetableOscProcess_ptr = void (WavetableOsc::*)(int sampleFrames);

constexpr WavetableOscProcess_ptr ProcessSelection[2][2] =
{
	&WavetableOsc::subProcess<PitchFixed,    SlotFixed>,
	&WavetableOsc::subProcess<PitchFixed,    SlotChanging>,
	&WavetableOsc::subProcess<PitchChanging, SlotFixed>,
	&WavetableOsc::subProcess<PitchChanging, SlotChanging>,
};

void WavetableOsc::onSetPins(void)
{
	if (pinWaveTableFile.isUpdated())
	{
		// Resolve the filename via SynthEdit's embedded-file support, then pull a
		// shared baked wavetable from the process-wide cache.
		waveTable_.reset();
		waveData_ = nullptr;

		const std::string& curWaveFile = pinWaveTableFile.getValue();
		if (builtinWavetableShape(curWaveFile) >= 0)
		{
			// Builtin test wavetable - skip host resource resolution, the name is the cache key.
			waveTable_ = wavetableCache().getOrLoad(curWaveFile, sampleRate_);
		}
		else if (auto synthEditHost = host.as<synthedit::IEmbeddedFileSupport>())
		{
			ReturnString fullFilename;
			if (synthEditHost->findResourceUri(curWaveFile.c_str(), &fullFilename) == ReturnCode::Ok)
			{
				synthEditHost->registerResourceUri(fullFilename.c_str());
				waveTable_ = wavetableCache().getOrLoad(fullFilename.c_str(), sampleRate_);
			}
		}

		if (waveTable_)
		{
			waveData_  = waveTable_->baked();
			mipMapPolicy = waveTable_->mipInfo;
			slotCount  = mipMapPolicy.getSlotCount();
		}

		// Reset grains so subProcess lazily re-inits at the new wavetable.
		for (auto& g : grains) g.waveSize = 0;
	}

	// DCO (Sync to note-on) mode - reset grain phase on voice activation.
	if (pinVoiceActive.isUpdated())
	{
		const bool newActiveState = pinVoiceActive > 0.0f;

		if (newActiveState != previousActiveState && newActiveState)
		{
			for (auto& g : grains) g.waveSize = 0; // next subProcess will spawn a fresh grain at phase 0.
		}
		previousActiveState = newActiveState;
	}

	pinSignalOut.setStreaming(true);

	if (waveData_)
	{
		setSubProcess(static_cast<SubProcessPtr>(
			ProcessSelection[pinPitch.isStreaming()][pinSlot.isStreaming()]));
	}
	else
	{
		setSubProcess(&WavetableOsc::subProcessNothing);
	}
}
