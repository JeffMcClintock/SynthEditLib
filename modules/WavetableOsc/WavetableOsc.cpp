#include "./WavetableOsc.h"
#include <sstream>
#include "../shared/it_enum_list.h"
#include "../shared/unicode_conversion.h"

#undef min
#undef max

#define OSC1_HANDLE 295518659

SE_DECLARE_INIT_STATIC_FILE(WavetableOsc);

using namespace gmpi;

namespace {
bool registered = Register<WavetableOsc>::withXml(R"XML(
<?xml version="1.0" encoding="utf-8" ?>
<PluginList>
  <Plugin id="SE Wavetable Osc" name="Wavetable Osc" category="Waveform" graphicsApi="composited" helpUrl="WavetableOsc.htm">
    <Parameters>
      <Parameter id="0" name="Table Modulation" datatype="float" private="true" ignorePatchChange="true" isPolyphonic="true" persistant="false"/>
      <Parameter id="1" name="Slot Modulation" datatype="float" private="true" ignorePatchChange="true" isPolyphonic="true" persistant="false"/>
      <Parameter id="2" name="WaveTableFiles" datatype="string" private="true" />
      <Parameter id="3" name="WaveDisplay" datatype="blob" private="true" ignorePatchChange="true" persistant="false"/>
    </Parameters>
	<GUI>
	  <Pin id="0" name="Table Modulation from DSP" datatype="float" parameterId="0" private="true" isPolyphonic="true"/>
	  <Pin id="1" name="Slot Modulation from DSP" datatype="float" parameterId="1" private="true" isPolyphonic="true"/>
	  <Pin id="2" name="WaveTableFiles" datatype="string" parameterId="2" private="true" />
	  <Pin id="3" name="WaveDisplay" datatype="blob" parameterId="3" private="true" />
	</GUI>
	<Audio>
	  <Pin id="0" name="Pitch" datatype="float" rate="audio" default="0.5"/>
	  <Pin id="1" name="Table" datatype="float" rate="audio" />
	  <Pin id="2" name="Slot" datatype="float" rate="audio" />
	  <Pin id="3" name="Effect" datatype="float" rate="audio" default="0.5" />
	  <Pin id="4" name="Effect Mode" datatype="enum" metadata="Off=-1, PSOLA = 3, PSOLA.fast, PSOLA - Wide Window" default="4"/>
	  <Pin id="5" name="Signal Out" direction="out" datatype="float" rate="audio"/>
	  <Pin id="6" name="Table Modulation to GUI" direction="out" datatype="float" parameterId="0" private="true" isPolyphonic="true"/>
	  <Pin id="7" name="Slot Modulation to GUI" direction="out" datatype="float" parameterId="1" private="true" isPolyphonic="true"/>
	  <Pin id="8" name="VoiceActive" hostConnect="Voice/Active" datatype="float" isPolyphonic="true" default="1" />
	  <Pin id="9" name="Sync to NoteOn (DCO)" datatype="bool" default="1"/>
	  <Pin id="10" name="Sync" datatype="float" rate="audio" default="0"/>
	  <Pin id="11" name="PSOLA root pitch" datatype="float" rate="audio" default="3.24"/>
	  <Pin id="12" name="WaveTableFiles" datatype="string" parameterId="2" private="true" />
	  <Pin id="13" name="WaveDisplay" direction="out" datatype="blob" parameterId="3" private="true" />
	</Audio>
  </Plugin>
</PluginList>
)XML");
}

WavetableOsc::WavetableOsc()
	:mipLevelA(0)
	,mipLevelB(0)
	,countMaskA(0)
	,syncCrossFadeLevel(0.0f)
	,previousActiveState(false)
	,syncState(false)
	,GrainformCounter(-1)
	,slotCount(0)
	,count(0.99999999f) // force calculation of miplevel on first sample.
	,currentGrain_mipLevel( -1 )
	,currentGrain_table( -1 )
	,currentGrain_slot( -1 )
	,guiUpdateCount_(0)
{
	for( int g = 0 ; g < MaxGrains ; ++g )
	{
		grains[g].wave = grainform[0]; // prevent crash.
	}
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
	mipMapPolicyHanning.initialize(1, 512, 1, false);

	hanningShared_ = SharedObjectManager<HanningData>::getOrCreateSharedMemory(
		-1.0f, 0,
		[&](float) {
			auto h = std::make_shared<HanningData>();
			h->data.resize(mipMapPolicyHanning.GetTotalMipMapSize());
			for (int mip = 0; mip < mipMapPolicyHanning.getMipCount(); ++mip)
			{
				int hanningSize = mipMapPolicyHanning.GetWaveSize(mip);
				float* pHanning = h->data.data() + mipMapPolicyHanning.getSlotOffset(0, 0, mip);
				for (int i = 0; i < hanningSize; ++i)
				{
					pHanning[i] = 0.5f - 0.5f * cosf(1.0f * (float)M_PI * i / (float)hanningSize);
				}
			}
			return h;
		});
	hanning = hanningShared_->data.data();

	GrainformDuration_ = (int)(sampleRate / 440.0f); // Update grainform about 440Hz.

	// Track most recent voice for GUI waveform display.
	mostRecentVoice_ = this;

	guiUpdateRate_ = (int)(sampleRate / 25.f); // gui updates around 20Hz.

	// Wavetable memory - shared across instances, one per oscillator.
	int32_t handle = host->getHandle();
	int oscNumber = handle != OSC1_HANDLE;

	wavetableDataShared_[oscNumber] = SharedObjectManager<WavetableData>::getOrCreateSharedMemory(
		-1.0f, oscNumber,
		[&](float) {
			auto wt = std::make_shared<WavetableData>();
			wt->data.resize(waveLoader_.WavebankMemoryRequired() / sizeof(float));
			wt->header.SetSize(WaveTable::MaximumTables, waveLoader_.getMipInfo().getSlotCount(), WaveTable::WavetableFileSampleCount);
			return wt;
		});
	waveData_ = wavetableDataShared_[oscNumber]->data.data();

	return r;
}

typedef void (WavetableOsc::* WavetableOscProcess_ptr)(int sampleFrames);

#define TPA( pitch, table, slot, synct, root) (&WavetableOsc::sub_process_PSOLA_template_fast< pitch, table, slot, synct, root > )

const WavetableOscProcess_ptr ProcessSelection[2][2][2][2][2] =
{
	TPA( PitchFixed,    TableFixed,    SlotFixed,    PolicySyncOff, RootPitchFixed),
	TPA( PitchFixed,    TableFixed,    SlotFixed,    PolicySyncOff, RootPitchChanging),
	TPA( PitchFixed,    TableFixed,    SlotFixed,    PolicySyncOn , RootPitchFixed),
	TPA( PitchFixed,    TableFixed,    SlotFixed,    PolicySyncOn , RootPitchChanging),
	TPA( PitchFixed,    TableFixed,    SlotChanging, PolicySyncOff, RootPitchFixed),
	TPA( PitchFixed,    TableFixed,    SlotChanging, PolicySyncOff, RootPitchChanging),
	TPA( PitchFixed,    TableFixed,    SlotChanging, PolicySyncOn , RootPitchFixed),
	TPA( PitchFixed,    TableFixed,    SlotChanging, PolicySyncOn , RootPitchChanging),
	TPA( PitchFixed,    TableChanging, SlotFixed,    PolicySyncOff, RootPitchFixed),
	TPA( PitchFixed,    TableChanging, SlotFixed,    PolicySyncOff, RootPitchChanging),
	TPA( PitchFixed,    TableChanging, SlotFixed,    PolicySyncOn , RootPitchFixed),
	TPA( PitchFixed,    TableChanging, SlotFixed,    PolicySyncOn , RootPitchChanging),
	TPA( PitchFixed,    TableChanging, SlotChanging, PolicySyncOff, RootPitchFixed),
	TPA( PitchFixed,    TableChanging, SlotChanging, PolicySyncOff, RootPitchChanging),
	TPA( PitchFixed,    TableChanging, SlotChanging, PolicySyncOn , RootPitchFixed),
	TPA( PitchFixed,    TableChanging, SlotChanging, PolicySyncOn , RootPitchChanging),
	TPA( PitchChanging, TableFixed,    SlotFixed,    PolicySyncOff, RootPitchFixed),
	TPA( PitchChanging, TableFixed,    SlotFixed,    PolicySyncOff, RootPitchChanging),
	TPA( PitchChanging, TableFixed,    SlotFixed,    PolicySyncOn , RootPitchFixed),
	TPA( PitchChanging, TableFixed,    SlotFixed,    PolicySyncOn , RootPitchChanging),
	TPA( PitchChanging, TableFixed,    SlotChanging, PolicySyncOff, RootPitchFixed),
	TPA( PitchChanging, TableFixed,    SlotChanging, PolicySyncOff, RootPitchChanging),
	TPA( PitchChanging, TableFixed,    SlotChanging, PolicySyncOn , RootPitchFixed),
	TPA( PitchChanging, TableFixed,    SlotChanging, PolicySyncOn , RootPitchChanging),
	TPA( PitchChanging, TableChanging, SlotFixed,    PolicySyncOff, RootPitchFixed),
	TPA( PitchChanging, TableChanging, SlotFixed,    PolicySyncOff, RootPitchChanging),
	TPA( PitchChanging, TableChanging, SlotFixed,    PolicySyncOn , RootPitchFixed),
	TPA( PitchChanging, TableChanging, SlotFixed,    PolicySyncOn , RootPitchChanging),
	TPA( PitchChanging, TableChanging, SlotChanging, PolicySyncOff, RootPitchFixed),
	TPA( PitchChanging, TableChanging, SlotChanging, PolicySyncOff, RootPitchChanging),
	TPA( PitchChanging, TableChanging, SlotChanging, PolicySyncOn , RootPitchFixed),
	TPA( PitchChanging, TableChanging, SlotChanging, PolicySyncOn , RootPitchChanging),
};

void WavetableOsc::onSetPins(void)
{
	// Check which pins are updated.
	if( pinWaveTableFiles.isUpdated() )
	{
		int32_t h = host->getHandle();
		int oscNumber = h != OSC1_HANDLE;
		waveData_ = wavetableDataShared_[oscNumber]->data.data();

		// Mip-maps require extra memory. Calculate.
		slotCount = WaveTable::MorphedSlotRatio * (WaveTable::WavetableFileSlotCount - 1) + 1; // add extra slots in-between.

		{
			// Load wave files into buffer.
			// Convert std::string pin value to wstring for legacy loader API.
			std::wstring wPinValue = JmUnicodeConversions::Utf8ToWstring(pinWaveTableFiles.getValue());
			it_enum_list it(wPinValue);
			int tableNumber = 0;
			for(it.First(); !it.IsDone() && tableNumber < TableCount; ++it, ++tableNumber)
			{
				std::wstring waveFilename = it.CurrentItem()->text;
				waveLoader_.setWaveFileName( waveData_, oscNumber, tableNumber, waveFilename);
			}
		}

		mipMapPolicy.initialize( WaveTable::FactoryWavetableCount, WaveTable::WavetableFileSampleCount, slotCount, true );

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

					if( pinMode >= 3 )  // PSOLA
					{
						// Don't trigger new grain instantly becuase after a patch change, might need to wait a few samples for slot and table pins to settle.
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

	if(waveData_ != 0)
	{
		// If Pitch streaming, root-pitch also needs updating.
		bool rootPitchStreaming = pinPitch.isStreaming() || pinEffect.isStreaming();
		setSubProcess(static_cast <SubProcessPtr> ( ProcessSelection[ pinPitch.isStreaming() ][ pinTable.isStreaming() ][ pinSlot.isStreaming() ][ pinSync.isStreaming() ][rootPitchStreaming] ));

		if( pinMode == 0 )  // Auto-Sync
		{
			crossfadeincrement = 1.0f / syncCrossFadeSamples;
		}
		else
		{
			crossfadeincrement = 1.0f / CrossFadeSamples;
		}
	}
	else
	{
		setSubProcess(&WavetableOsc::subProcessNothing);
	}
}

void WavetableOsc::updateGuiWaveform(void)
{
	if( mostRecentVoice_ == this )
	{
		int mipLevel = 4;
		const int GuiWaveSize = 32;
		int mipwavesize = mipMapPolicy.GetWaveSize(mipLevel);
		assert( GuiWaveSize == mipwavesize / 2 );
		float wave[GuiWaveSize];
        float modulationTable;
        float modulationSlot;

        modulationTable = currentGrain_table;

		int table_floor, slot1_floor;
		float slot_frac,table_frac;
		TableChanging::Calculate( currentGrain_table, TableCount, table_floor, table_frac );

		modulationSlot = currentGrain_slot;
    	SlotChanging::Calculate( currentGrain_slot, slotCount, slot1_floor, slot_frac );

		float* wave1a = waveData_ + mipMapPolicy.getSlotOffset(table_floor, slot1_floor, mipLevel);
		float* wave1b = waveData_ + mipMapPolicy.getSlotOffset(table_floor, slot1_floor + 1, mipLevel);
		float* wave2a = waveData_ + mipMapPolicy.getSlotOffset(table_floor + 1, slot1_floor, mipLevel);
		float* wave2b = waveData_ + mipMapPolicy.getSlotOffset(table_floor + 1, slot1_floor + 1, mipLevel);

		wave[0] = 0.0f; // due to phase alignment.

#ifdef SE_WT_OSC_STORE_HALF_CYCLES // Assume symetrical wave.
		for(int c = 1; c < (mipwavesize >> 1); ++c)
		{
			// Calc sample interpolating between slots. Wave1
			float p1 = wave1a[c];
			float p2 = wave1b[c];
			float output1 = p1 + slot_frac * (p2 - p1);

			// Calc sample interpolating between slots. Wave2
			float p3 = wave2a[c];
			float p4 = wave2b[c];
			float output2 = p3 + slot_frac * (p4 - p3);

			float grainSample = output1 + table_frac * (output2 - output1);
			wave[c] = grainSample;
		}
#endif

		gmpi::Blob waveBlob(reinterpret_cast<const uint8_t*>(wave), reinterpret_cast<const uint8_t*>(wave) + sizeof(wave));
		pinGuiWaveDisplay.setValue(waveBlob, 0);
	}

    guiUpdateCount_ = guiUpdateRate_;
}
