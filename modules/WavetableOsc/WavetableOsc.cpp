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
	  <Pin name="Formant" datatype="float" rate="audio" metadata="-10,10"/>
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
	for( auto& g : grains)
		g.wave = grainform[0];
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

void WavetableOsc::calcMipLevel( WavetableMipmapPolicy& mipMapPolicy, double increment, int& returnMipLevelA, unsigned int& returnCountMaskA )
{
	returnMipLevelA = mipMapPolicy.CalcMipLevel(static_cast<float>(increment));
	int	MipwavesizeA = mipMapPolicy.GetWaveSize(returnMipLevelA);
	returnCountMaskA = MipwavesizeA - 1;
}

ReturnCode WavetableOsc::open(api::IUnknown* phost)
{
	auto r = Processor::open(phost);

	const float sampleRate = host->getSampleRate();

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

	// Hanning window - mip-mapped, shared across all instances.
	mipMapPolicyHanning.initialize(512, 1);

	hanningShared_ = SharedObjectManager<HanningData>::getOrCreateSharedMemory(
		-1.0f, 0,
		[&](float) {
			auto h = std::make_shared<HanningData>();
			h->data.resize(mipMapPolicyHanning.GetTotalMipMapSize());
			for (int mip = 0; mip < mipMapPolicyHanning.getMipCount(); ++mip)
			{
				int hanningSize = mipMapPolicyHanning.GetWaveSize(mip);
				float* pHanning = h->data.data() + mipMapPolicyHanning.getSlotOffset(0, mip);
				for (int i = 0; i < hanningSize; ++i)
				{
					pHanning[i] = 0.5f - 0.5f * cosf(1.0f * (float)M_PI * i / (float)hanningSize);
				}
			}
			return h;
		});
	hanning = hanningShared_->data.data();

	GrainformDuration_ = (int)(sampleRate / 440.0f); // Update grainform about 440Hz.

	// Wavetable buffer allocation happens lazily in onSetPins, via the
	// process-wide WavetableCache keyed on the resolved file URI.

	return r;
}

using WavetableOscProcess_ptr = void (WavetableOsc::*)(int sampleFrames);

constexpr WavetableOscProcess_ptr ProcessSelection[2][2][2] =
{
	&WavetableOsc::subProcess<PitchFixed,    SlotFixed,    PsolaFixed>,
	&WavetableOsc::subProcess<PitchFixed,    SlotFixed,    PsolaChanging>,
	&WavetableOsc::subProcess<PitchFixed,    SlotChanging, PsolaFixed>,
	&WavetableOsc::subProcess<PitchFixed,    SlotChanging, PsolaChanging>,
	&WavetableOsc::subProcess<PitchChanging, SlotFixed,    PsolaFixed>,
	&WavetableOsc::subProcess<PitchChanging, SlotFixed,    PsolaChanging>,
	&WavetableOsc::subProcess<PitchChanging, SlotChanging, PsolaFixed>,
	&WavetableOsc::subProcess<PitchChanging, SlotChanging, PsolaChanging>,
};

void WavetableOsc::onSetPins(void)
{
	// Check which pins are updated.
	if( pinWaveTableFile.isUpdated() )
	{
		// Resolve the filename via SynthEdit's embedded-file support, then pull a
		// shared baked wavetable from the process-wide cache. Two instances loading
		// the same file share one bake.
		waveTable_.reset();
		waveData_ = nullptr;

		const std::string& curWaveFile = pinWaveTableFile.getValue();
		if (builtinWavetableShape(curWaveFile) >= 0)
		{
			// Builtin test wavetable - skip host resource resolution, the name is the cache key.
			waveTable_ = wavetableCache().getOrLoad(curWaveFile);
		}
		else if (auto synthEditHost = host.as<synthedit::IEmbeddedFileSupport>())
		{
			ReturnString fullFilename;
			if (synthEditHost->findResourceUri(curWaveFile.c_str(), &fullFilename) == ReturnCode::Ok)
			{
				synthEditHost->registerResourceUri(fullFilename.c_str());
				waveTable_ = wavetableCache().getOrLoad(fullFilename.c_str());
			}
		}

		if (waveTable_)
		{
			waveData_  = waveTable_->baked();
			mipMapPolicy = waveTable_->mipInfo;
			slotCount  = mipMapPolicy.getSlotCount();
		}

		double increment = ComputeIncrement2( pitchTable, pinPitch );
		calcMipLevel( mipMapPolicy, increment, mipLevelA, countMaskA);

		currentGrain_mipLevel = -1; // force calculation of fresh graincycle.
	}

	// DCO (Sync to note-on) mode.
	if( pinVoiceActive.isUpdated() )
	{
		bool newActiveState = pinVoiceActive > 0.0f;

		if( newActiveState != previousActiveState )
		{
			if( newActiveState )
			{
				currentGrain_mipLevel = -1; // force calculation of fresh graincycle. Fix for voices retaining old sample on import.

				count = 0.0;

				GrainformCounter = -1; // force re-calc of waveshape.
				if( waveData_ != 0 )
				{
					// calc the mip level etc. Else first cycle too dull/bright.
					double increment = ComputeIncrement2(pitchTable, pinPitch);
					calcMipLevel(mipMapPolicy, increment, mipLevelA, countMaskA);

	//				if( pinMode >= 3 )  // PSOLA
					{
						// Don't trigger new grain instantly becuase after a patch change, might need to wait a few samples for slot pin to settle.
						count = 1.0 - increment * 4.0; // 4-5 samples till next grain.

						for( int g = 0; g < MaxGrains; ++g )
						{
							grains[g].waveSize = 0; // indicates inactive grain.
						}
					}
				}
			}
		}
		previousActiveState = newActiveState;
	}

	// Set state of output audio pins.
	pinSignalOut.setStreaming(true);

	if(waveData_)
	{
		// If Pitch streaming, root-pitch also needs updating.
		bool rootPitchStreaming = pinPitch.isStreaming();
		setSubProcess(static_cast <SubProcessPtr> ( ProcessSelection[ pinPitch.isStreaming() ][ pinSlot.isStreaming() ][rootPitchStreaming] ));
	}
	else
	{
		setSubProcess(&WavetableOsc::subProcessNothing);
	}
}

