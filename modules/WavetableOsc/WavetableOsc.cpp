#include "./WavetableOsc.h"
#include <sstream>
#include "../shared/unicode_conversion.h"
#include "../shared/platform_string.h"
#include "Extensions/EmbeddedFile.h"

#undef min
#undef max

#define OSC1_HANDLE 295518659

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

#define MAX_VOLTS ( 10.f )
#define FSampleToVoltage(s) ( (float) (s) * (float) MAX_VOLTS)
#define FSampleToFrequency(volts) ( 440.f * powf(2.f, FSampleToVoltage(volts) - (float) MAX_VOLTS / 2.f ) )
inline static unsigned int FrequencyToIntIncrement( float sampleRate, double freq_hz )
{
	double temp_float = (UINT_MAX+1.f) * freq_hz / sampleRate + 0.5f;
	return (unsigned int) temp_float;
};

void WavetableOsc::calcMipLevel( WavetableMipmapPolicy& mipMapPolicy, float increment, int& returnMipLevelA, unsigned int& returnCountMaskA )
{
	returnMipLevelA = mipMapPolicy.CalcMipLevel(increment);
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
	const float oneSemitone = 1.0f/12.0f;

	pitchTableShared_ = SharedObjectManager<PitchTableData>::getOrCreateSharedMemory(
		sampleRate, 0,
		[&](float sr) {
			auto p = std::make_shared<PitchTableData>();
			p->data.resize(pitchTableSize);
			for (int i = 0; i < pitchTableSize; ++i)
			{
				float pitch = (pitchTableLowVolts + (i - extraEntriesAtStart) * oneSemitone) * 0.1f;
				float hz = FSampleToFrequency(pitch);
				p->data[i] = hz / sr;
			}
			return p;
		});
	pitchTable = pitchTableShared_->data.data() + extraEntriesAtStart;

	// Hanning window - mip-mapped, shared across all instances.
	mipMapPolicyHanning.initialize(512, 1, false);

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

	mostRecentVoice_ = this;

	// Wavetable memory - shared across instances, one per oscillator.
	int32_t handle = host->getHandle();
	int oscNumber = handle != OSC1_HANDLE;

	wavetableDataShared_[oscNumber] = SharedObjectManager<WavetableData>::getOrCreateSharedMemory(
		-1.0f, oscNumber,
		[&](float) {
			auto wt = std::make_shared<WavetableData>();
			wt->data.resize(waveLoader_.WavebankMemoryRequired() / sizeof(float));
			wt->header.SetSize(waveLoader_.getMipInfo().getSlotCount(), WaveTable::WavetableFileSampleCount);
			return wt;
		});
	waveData_ = wavetableDataShared_[oscNumber]->data.data();

	return r;
}

typedef void (WavetableOsc::* WavetableOscProcess_ptr)(int sampleFrames);

#define TPA( pitch, slot, root) (&WavetableOsc::subProcess<pitch, slot, root> )

const WavetableOscProcess_ptr ProcessSelection[2][2][2] =
{
	TPA( PitchFixed,    SlotFixed,    PsolaFixed),
	TPA( PitchFixed,    SlotFixed,    PsolaChanging),
	TPA( PitchFixed,    SlotChanging, PsolaFixed),
	TPA( PitchFixed,    SlotChanging, PsolaChanging),
	TPA( PitchChanging, SlotFixed,    PsolaFixed),
	TPA( PitchChanging, SlotFixed,    PsolaChanging),
	TPA( PitchChanging, SlotChanging, PsolaFixed),
	TPA( PitchChanging, SlotChanging, PsolaChanging),
};

void WavetableOsc::onSetPins(void)
{
	// Check which pins are updated.
	if( pinWaveTableFile.isUpdated() )
	{
		int32_t h = host->getHandle();
		int oscNumber = h != OSC1_HANDLE;
		waveData_ = wavetableDataShared_[oscNumber]->data.data();

		// Mip-maps require extra memory. Calculate.
		slotCount = WaveTable::MorphedSlotRatio * (WaveTable::WavetableFileSlotCount - 1) + 1; // add extra slots in-between.

		{
			// Resolve each filename via SynthEdit's embedded-file support: the host handles
			// search paths (skin/project folder etc.) and gets a chance to register each file
			// for automatic inclusion in VST export.
			auto synthEditHost = host.as<synthedit::IEmbeddedFileSupport>();
			if (synthEditHost)
			{
				const auto utf8Filename = pinWaveTableFile.getValue();
				ReturnString fullFilename;
				if (synthEditHost->findResourceUri(utf8Filename.c_str(), &fullFilename) == ReturnCode::Ok)
				{
					synthEditHost->registerResourceUri(fullFilename.c_str());
					auto fullFilenameW = JmUnicodeConversions::Utf8ToWstring(fullFilename.c_str());
					waveLoader_.setWaveFileName( waveData_, oscNumber, fullFilenameW);
				}
			}
		}

		mipMapPolicy.initialize(WaveTable::WavetableFileSampleCount, slotCount, true );

		float increment = ComputeIncrement2( pitchTable, pinPitch );
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

				mostRecentVoice_ = this;

				syncCrossFadeLevel = 0.0f;
				count = 0.0;

				GrainformCounter = -1; // force re-calc of waveshape.
				if( waveData_ != 0 )
				{
					// calc the mip level etc. Else first cycle too dull/bright.
					float increment = ComputeIncrement2(pitchTable, pinPitch);
					calcMipLevel(mipMapPolicy, increment, mipLevelA, countMaskA);

	//				if( pinMode >= 3 )  // PSOLA
					{
						// Don't trigger new grain instantly becuase after a patch change, might need to wait a few samples for slot pin to settle.
						count = 1.0f - increment * 4.0; // 4-5 samples till next grain.

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

		//if( pinMode == 0 )  // Auto-Sync
		//{
		//	crossfadeincrement = 1.0f / syncCrossFadeSamples;
		//}
		//else
		{
			crossfadeincrement = 1.0f / CrossFadeSamples;
		}
	}
	else
	{
		setSubProcess(&WavetableOsc::subProcessNothing);
	}
}

