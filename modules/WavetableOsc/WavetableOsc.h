#ifndef WAVETABLEOSC_H_INCLUDED
#define WAVETABLEOSC_H_INCLUDED

#include "Processor.h"
#include "WavetableMipmapPolicy.h"
#define _USE_MATH_DEFINES
#include <vector>
#include <math.h>
#include <memory>
#include "WaveTableLoad.h"
#include "../shared/SharedObject.h"

#undef min
#undef max

const static int pitchTableLowVolts = -4; // ~ 1Hz
const static int pitchTableHiVolts = 11;  // ~ 20kHz.

// Fast version using table.
inline float ComputeIncrement2( const float* pitchTable, float pitch )
{
	float index = 12.0f * (pitch * 10.0f - (float) pitchTableLowVolts);
	int table_floor = static_cast<int>(index);
	float fraction = index - (float) table_floor;

	if( table_floor <= 0 ) // indicated index *might* be less than zero. e.g. Could be 0.1 which is valid, or -0.1 which is not.
	{
		if( ! (index >= 0.0f) ) // reverse logic to catch Nans.
		{
			return pitchTable[0];
		}
	}
	else
	{
		const int maxTableIndex = (pitchTableHiVolts - pitchTableLowVolts) * 12;
		if( table_floor >= maxTableIndex )
		{
			return pitchTable[maxTableIndex];
		}
	}

	// Cubic interpolator.
	assert( table_floor >= 0 && table_floor <= (pitchTableHiVolts - pitchTableLowVolts) * 12 );

	float y0 = pitchTable[table_floor-1];
	float y1 = pitchTable[table_floor+0];
	float y2 = pitchTable[table_floor+1];
	float y3 = pitchTable[table_floor+2];

	return y1 + 0.5f * fraction*(y2 - y0 + fraction*(2.0f*y0 - 5.0f*y1 + 4.0f*y2 - y3 + fraction*(3.0f*(y1 - y2) + y3 - y0)));
}

class PitchFixed
{
public:
	inline static void CalcInitial( const float* pitchTable, float pitch, float& returnIncrement )
	{
		returnIncrement = ComputeIncrement2( pitchTable, pitch );
	};

	inline static void Calculate( const float* pitchTable, float pitch, float& returnIncrement )
	{
		// do nothing. Hopefully optimizes away to nothing.
	};

	inline static void IncrementPointer( const float* pitch )
	{
		// do nothing. Hopefully optimizes away to nothing.
	};
	enum { Active = false };
};

class PitchChanging
{
public:
	inline static void CalcInitial( const float* pitchTable, float pitch, float& returnIncrement )
	{
		// do nothing. Hopefully optimizes away to nothing.
	};

	inline static void Calculate( const float* pitchTable, float pitch, float& returnIncrement )
	{
		returnIncrement = ComputeIncrement2( pitchTable, pitch );
	};

	inline static void IncrementPointer( const float*& pitch )
	{
		++pitch;
	};
	enum { Active = true };
};


class RootPitchChanging
{
public:
	inline static void CalcInitialPsola(const float* pitchTable, float Increment, float PsolaIntensity, float PsolaRootPitch, float& grainIncrement)
	{
	};
	inline static void CalculatePsola(const float* pitchTable, float Increment, float PsolaIntensity, float PsolaRootPitch, float& grainIncrement)
	{
		if(PsolaIntensity < 0.0f) // avoid crashing due to -ve increment when PSOLA AMNT heavily modulated.
			PsolaIntensity = 0.0f;
		if(PsolaIntensity > 1.0f)
			PsolaIntensity = 1.0f;

		float psolaIncrement = ComputeIncrement2(pitchTable, PsolaRootPitch);
        float i = Increment + PsolaIntensity * (psolaIncrement - Increment);
        const float psolaLimit = 0.25f; // prevent PSOLA going sub-sonic. two octave down anyhow.
        float minInc = Increment * psolaLimit;
        if( i < minInc )
        {
            i = minInc;
        }
        grainIncrement = 0.5f * i;
	};
	inline static void IncrementPointer(const float* ptr)
	{
		++ptr;
	};
	enum { Active = false };
};

class RootPitchFixed
{
public:
	inline static void CalcInitialPsola( const float* pitchTable, float Increment, float PsolaIntensity, float PsolaRootPitch, float& grainIncrement )
	{
		return RootPitchChanging::CalculatePsola(pitchTable, Increment, PsolaIntensity, PsolaRootPitch, grainIncrement);
	};
	inline static void CalculatePsola( const float* pitchTable, float Increment, float PsolaIntensity, float PsolaRootPitch, float& grainIncrement )
	{
	};
	inline static void IncrementPointer( const float* ptr )
	{
		// do nothing. Hopefully optimizes away to nothing.
	};
	enum { Active = false };
};


class SlotChanging
{
public:
	inline static void CalcInitial(const float slot, int slotCount, int& returnSlotFloor, float& returnSlotFraction)
	{
		// do nothing. Hopefully optimizes away to nothing.
	};
	inline static void Calculate(const float slot, int slotCount, int& returnSlotFloor, float& returnSlotFraction)
	{
		if( (int&)slot & 0x80000000 ) // bitwise negative test.
		{
			returnSlotFloor = 0;
			returnSlotFraction = 0.0f;
			return;
		}

		float index = slot * (float)(slotCount - 1);

		int i = static_cast<int>(index);

		if(i >= 0 && i < slotCount - 1)		// Very large 'index' values overflow float-to-int to 0x80000000 (-2147483648). hence -ve test.
		{
			returnSlotFraction = index - (float)i;
			returnSlotFloor = i;
		}
		else
		{
			returnSlotFraction = 1.0f;
			returnSlotFloor = slotCount - 2;
		}
	};

	inline static void IncrementPointer(const float*& ptr)
	{
		++ptr;
	};
};

class SlotFixed
{
public:
	inline static void CalcInitial( const float slot, int slotCount, int& returnSlotFloor, float& returnSlotFraction )
	{
		SlotChanging::Calculate( slot, slotCount, returnSlotFloor, returnSlotFraction );
	};
	inline static void Calculate( const float slot, int slotCount, int& returnSlotFloor, float& returnSlotFraction )
	{
		// do nothing. Hopefully optimizes away to nothing.
	};
	inline static void IncrementPointer( const float* ptr )
	{
		// do nothing. Hopefully optimizes away to nothing.
	};
};

class SlotChangingRev
{
public:
	inline static void CalcInitial(const float slot, int slotCount, int& returnSlotFloor, float& returnSlotFraction)
	{
		// do nothing. Hopefully optimizes away to nothing.
	};
	inline static void Calculate(const float slot, int slotCount, int& returnSlotFloor, float& returnSlotFraction)
	{
		SlotChanging::Calculate(1.0f - slot, slotCount, returnSlotFloor, returnSlotFraction);
	};
	inline static void IncrementPointer(const float*& ptr)
	{
		++ptr;
	};
};

class SlotFixedRev
{
public:
	inline static void CalcInitial( const float slot, int slotCount, int& returnSlotFloor, float& returnSlotFraction )
	{
		SlotChanging::Calculate(1.0f - slot, slotCount, returnSlotFloor, returnSlotFraction);
	};
	inline static void Calculate( const float slot, int slotCount, int& returnSlotFloor, float& returnSlotFraction )
	{
		// do nothing. Hopefully optimizes away to nothing.
	};
	inline static void IncrementPointer( const float* ptr )
	{
		// do nothing. Hopefully optimizes away to nothing.
	};
};


class TableFixed
{
public:
	inline static void CalcInitial( const float table, int tableCount, int& returnFloor, float& returnFraction )
	{
		// same calculation as slot modulation.
		SlotChanging::Calculate(table, tableCount, returnFloor, returnFraction);
	};
	inline static void Calculate( const float table, int tableCount, int& returnFloor, float& returnFraction  )
	{
		// do nothing. Hopefully optimizes away to nothing.
	};
	inline static void IncrementPointer( const float* ptr )
	{
		// do nothing. Hopefully optimizes away to nothing.
	};
};

class TableChanging
{
public:
	inline static void CalcInitial( const float table, int tableCount, int& returnFloor, float& returnFraction )
	{
		// do nothing. Hopefully optimizes away to nothing.
	};
	inline static void Calculate( const float table, int tableCount, int& returnFloor, float& returnFraction )
	{
		// same calculation as slot modulation.
		SlotChanging::Calculate(table, tableCount, returnFloor, returnFraction);
	};
	inline static void IncrementPointer( const float*& ptr )
	{
		++ptr;
	};
};

class PolicySyncOff
{
public:
	enum { SyncActive = false };

	inline static void IncrementPointer( const float* ptr )
	{
		// do nothing. Hopefully optimizes away to nothing.
	};
};

class PolicySyncOn
{
public:
	enum { SyncActive = true };

	inline static void IncrementPointer( const float*& ptr )
	{
		++ptr;
	};
};

class EffectNone
{
public:
	inline static float ModifyPhase( float phase, float intensity )
	{
		// Hopefully optimizes away to nothing.
		return phase;
	}
	// Mip level determined by increment.
	inline static float MipIncrement( float increment, float intensity )
	{
		return increment;
	}
	inline static void IncrementPointer( const float* ptr )
	{
		// do nothing. Hopefully optimizes away to nothing.
	};
};

class EffectPhaseDistortion : public EffectNone
{
private:
	float countA;

public:
	inline static float ModifyPhase( float phase, float intensity )
	{
		float dist = std::min( intensity, 0.9f );
		dist = 0.5f + dist * 0.5f;
		float distortedPhase;
		if( phase < dist )
		{
			distortedPhase = phase * 0.5f / dist;
		}
		else
		{
			distortedPhase = 0.5f + (phase - dist) * 0.5f / (1.0f - dist);
		}

		// additional phase-smoothing with sine function.
		return phase + (1.0f + sinf((distortedPhase-0.25f) * (float) M_PI * 2.0f)) * (0.5f-dist) * 0.5f;
	};
	inline static void IncrementPointer( const float*& ptr )
	{
		++ptr;
	};
};

class EffectFormant : public EffectNone
{
public:
	inline static float ModifyPhase( float phase, float intensity )
	{
		float distortedPhase = 0.5f + (phase - 0.5f) * (1.0f + 10.0f * intensity);
		if( distortedPhase >= 1.0f || distortedPhase <= -1.0f )
		{
			distortedPhase = 0.0f;
		}

		return distortedPhase;
	};
	// Mip level determined by increment.
	inline static float MipIncrement( float increment, float intensity )
	{
		return increment * (1.0f + 10.0f * intensity);
	};
	inline static void IncrementPointer( const float*& ptr )
	{
		++ptr;
	};
};


struct Grain
{
	float count;
	int waveSize;
	float syncCounter;
	float cycleCount;
	float* wave;

	Grain() : count(0), waveSize(0), syncCounter(0.0f){};
};

// SE_DECLARE_INIT_STATIC_FILE equivalent for static library linking
#ifndef SE_DECLARE_INIT_STATIC_FILE
#define SE_DECLARE_INIT_STATIC_FILE(filename) void se_static_library_init_##filename(){}
#endif

class WavetableOsc : public gmpi::Processor
{
private:
	const static int CrossFadeSamples = 50; // when changing Mip level.
	const static int syncCrossFadeSamples = 8;
	const static int MaxGrains = 32; // power-of-2 please.
	const static int extraInterpolationPreSamples = 1;
	const static int extraInterpolationPostSamples = 3;

	float count;
	float countB;
	float crossfadeincrement;
	Grain grains[MaxGrains];

	float *hanning{};
	static const int HanningSize = 256;
	static const int TableCount = 64;
	float* waveData_{};
	int guiUpdateCount_{};
    int guiUpdateRate_{};

	// Shared memory (replaces allocateSharedMemory)
	struct PitchTableData { std::vector<float> data; };
	struct HanningData { std::vector<float> data; };
	struct WavetableData { std::vector<float> data; WaveTable header; };
	struct CurrentVoiceData { WavetableOsc* voice{}; };

	std::shared_ptr<PitchTableData> pitchTableShared_;
	std::shared_ptr<HanningData> hanningShared_;
	std::shared_ptr<WavetableData> wavetableDataShared_[NUM_WAVETABLE_OSCS];
	inline static WavetableOsc* mostRecentVoice_{};

public:

	WavetableOsc();
	gmpi::ReturnCode open(gmpi::api::IUnknown* phost) override;
	void onSetPins() override;
	void updateGuiWaveform(void);

	// Interpolate between tables and between slots. float counter.
	inline float get_sample3b( const float* wavedata, int table_floor, float fraction ) const
	{
		assert(table_floor >= 0);

		// Cubic.
		float y0 = wavedata[(table_floor-1)];
		float y1 = wavedata[(table_floor+0)];
		float y2 = wavedata[(table_floor+1)];
		float y3 = wavedata[(table_floor+2)];

		return y1 + 0.5f * fraction*(y2 - y0 + fraction*(2.0f*y0 - 5.0f*y1 + 4.0f*y2 - y3 + fraction*(3.0f*(y1 - y2) + y3 - y0)));
	}

	inline float interpolateLinearTruncate( const float* wavedata, float index, int countMask ) const
	{
		index = std::min( std::max( index, 0.0f ), (float)countMask );
		int table_floor = static_cast<int>(index);
		float fraction = index - (float) table_floor;

		float p0 = wavedata[table_floor];
		float p1 = wavedata[table_floor+1];
		return p0 + (p1 - p0) * fraction;
	}

	int slotCount;
	static const int grainformCount = 4;
	int currentGrainform;
	int GrainformCounter;
	int currentGrainformMipwavesize;
	int currentGrain_mipLevel;
	float currentGrain_table;
	float currentGrain_slot;
	float grainform[grainformCount][maximumWaveSize * 2 + extraInterpolationPreSamples + extraInterpolationPostSamples]; // pre-calculated windowed cycles.
	int GrainformDuration_;
	template< class PitchModulationPolicy, class TableModulationPolicy, class SlotModulationPolicy, class SyncModulationPolicy, class RootPitchModulationPolicy >
	void sub_process_PSOLA_template_fast( int sampleFrames )
	{
		// get pointers to in/output buffers.
		const float* table = getBuffer(pinTable);
		const float* pslot = getBuffer(pinSlot);
		float* signalOut = getBuffer(pinSignalOut);
		const float* pitch = getBuffer(pinPitch);
		const float* intensity = getBuffer(pinEffect);
		const float* rootPitch = getBuffer(pinPsolaRootPitch);

		const float* sync = nullptr;

		if( SyncModulationPolicy::SyncActive )
		{
			sync = getBuffer(pinSync);
		}

		float increment, grainIncrementFast, table_frac, slot_frac;
		int table_floor, slot1_floor;
		PitchModulationPolicy::CalcInitial( pitchTable, *pitch, increment );
		RootPitchModulationPolicy::CalcInitialPsola( pitchTable, increment, *intensity, *rootPitch, grainIncrementFast );
		TableModulationPolicy::CalcInitial( *table, TableCount, table_floor, table_frac );
		SlotModulationPolicy::CalcInitial( *pslot, slotCount, slot1_floor, slot_frac );

		for( int s = sampleFrames; s > 0; --s )
		{
			PitchModulationPolicy::Calculate( pitchTable, *pitch, increment );
			RootPitchModulationPolicy::CalculatePsola( pitchTable, increment, *intensity, *rootPitch, grainIncrementFast );

			float samp = 0.0f;
			for( int g = 0 ; g < MaxGrains ; ++g )
			{
				if( grains[g].waveSize )
				{
					float index = grains[g].count * (float) ( grains[g].waveSize );
					int table_floor = static_cast<int>(index);
					float fraction = index - (float) table_floor;
					float grainSample = get_sample3b( grains[g].wave, table_floor, fraction );

					if( SyncModulationPolicy::SyncActive )
					{
						if( grains[g].syncCounter < 1.0f ) // This grain being rapid-faded?
						{
							grainSample *= grains[g].syncCounter;
							grains[g].syncCounter -= 1.0f / (float) syncCrossFadeSamples;
							if( grains[g].syncCounter < 0.0f )
							{
								grains[g].waveSize = 0; // indicates inactive grain.
							}
						}
					}

					samp += grainSample;

					grains[g].count += grainIncrementFast;
					if( grains[g].count > 1.0f ) // 1 grain? done.
					{
						grains[g].waveSize = 0; // indicates inactive grain.
					}
				}
			}
			*signalOut = samp;

			// trigger sync?
			if( SyncModulationPolicy::SyncActive )
			{
				if( ( *sync > 0.0f ) != syncState )
				{
					syncState = *sync > 0.0f;
					if( syncState )
					{
						count = 1.0f;
						for( int g2 = 0 ; g2 < MaxGrains ; ++g2 )
						{
							if( grains[g2].waveSize != 0 && grains[g2].syncCounter == 1.0f ) // indicates active grain.
							{
								grains[g2].syncCounter -= 1.0f / (float) syncCrossFadeSamples;
							}
						}

					}
				}
				++sync;
			}

			count += increment;
			if( count > 1.0f ) // is count wrapping? (pretty intensive on high frequencies )
			{
				// count wraps at 1.0
				int count_floor = static_cast<int>(count);
				count -= (float) count_floor;

				// start a fresh grain.
				for( int g = 0 ; g < MaxGrains ; ++g )
				{
					if( grains[g].waveSize == 0 )
					{
						grains[g].count = count * 0.5f; // Make grain phase fractionally correct.
						grains[g].syncCounter = 1.0f;
						float* wave = &( grainform[currentGrainform][1] ); // create one virtual negative table value to aid interpolation.

						// if time since last grain calc.
						if( GrainformCounter < 0 )
						{
							int mipLevel = mipMapPolicy.CalcMipLevel( grainIncrementFast * 2.0f );
							int mipwavesize = currentGrainformMipwavesize = mipMapPolicy.GetWaveSize(mipLevel);

							TableModulationPolicy::Calculate( *table, TableCount, table_floor, table_frac );
							SlotModulationPolicy::Calculate( *pslot, slotCount, slot1_floor, slot_frac );

							GrainformCounter = GrainformDuration_; // restart counter.
							// If parameters the same, just re-use previous grain.
							if( mipLevel != currentGrain_mipLevel || *table != currentGrain_table || *pslot != currentGrain_slot )
							{
								currentGrain_mipLevel = mipLevel;
								currentGrain_table = *table;
								currentGrain_slot = *pslot;

								// increment/wrap current grainform.
								currentGrainform = (currentGrainform + 1) & (grainformCount - 1);
								wave = &( grainform[currentGrainform][1] ); // create one virtual negative table value to aid interpolation.

								// re-fill grainform.

								float* wave1a = waveData_ + mipMapPolicy.getSlotOffset(table_floor, slot1_floor, mipLevel);
								float* wave1b = waveData_ + mipMapPolicy.getSlotOffset(table_floor, slot1_floor + 1, mipLevel);
								float* wave2a = waveData_ + mipMapPolicy.getSlotOffset(table_floor + 1, slot1_floor, mipLevel);
								float* wave2b = waveData_ + mipMapPolicy.getSlotOffset(table_floor + 1, slot1_floor + 1, mipLevel);

								float* pHanning = hanning + mipMapPolicyHanning.getSlotOffset(0,0,mipLevel);

								// Add sample 'off-front' to aid interpolation.
								wave[-1] = 0.0f;
								wave[0] = wave[mipwavesize / 2] = wave[mipwavesize] = wave[mipwavesize + mipwavesize / 2] = 0.0f; // due to phase alignment.
								// Wrap a few samples 'off-end' to simplify interpolation. Note hanning window means these are all zero.
								for( int e = 0 ; e < extraInterpolationPostSamples ; ++e )
								{
									wave[mipwavesize * 2 + e] = 0.0f;
								}

#ifdef SE_WT_OSC_STORE_HALF_CYCLES // Assume symetrical wave.
								for( int c = 1 ; c < (mipwavesize >> 1) ; ++c )
								{
									// Calc sample interpolating between slots. Wave1
									float p1 = wave1a[c];	// first slot's sample. no interpolation.
									float p2 = wave1b[c];	// second slot's sample. no interpolation.
									float output1 = p1 + slot_frac * (p2 - p1);		// interpolate between slots.

									// Calc sample interpolating between slots. Wave2
									float p3 = wave2a[c];		// first slot's sample. no interpolation.
									float p4 = wave2b[c];		// second slot's sample. no interpolation.
									float output2 = p3 + slot_frac * (p4 - p3);		// interpolate between slots.

									float grainSample = output1 + table_frac * (output2 - output1);	// interpolate between tables.
                                    wave[c]                 =  grainSample * pHanning[c];
									wave[mipwavesize - c]   = -grainSample * pHanning[mipwavesize - c]; // calc 2nd half of cycle by flipping first half.
									wave[mipwavesize + c]   = -wave[mipwavesize - c]; // Mirror first whole cycle into 2nd half of grain.
									wave[mipwavesize * 2 - c] = -wave[c]; // Mirror first whole cycle into 2nd half of grain.
								}

#else
								for( int c = 0 ; c <= mipwavesize ; ++c )
								{
									// Calc sample interpolating between slots. Wave1
									float p1 = wave1[slot1_idx + c];	// first slot's sample. no interpolation.
									float p2 = wave1[slot2_idx + c];	// second slot's sample. no interpolation.
									float output1 = p1 + slot_frac * (p2 - p1);		// interpolate between slots.

									// Calc sample interpolating between slots. Wave2
									float p3 = wave2[slot1_idx + c];		// first slot's sample. no interpolation.
									float p4 = wave2[slot2_idx + c];		// second slot's sample. no interpolation.
									float output2 = p3 + slot_frac * (p4 - p3);		// interpolate between slots.

									float grainSample = output1 + table_frac * (output2 - output1);	// interpolate between tables.

									wave[c] = grainSample * pHanning[c]; // Window.
									wave[mipwavesize * 2 - c] = -wave[c]; // Mirror
								}
#endif
							}
						}
						grains[g].waveSize = (currentGrainformMipwavesize * 2);
						grains[g].wave = wave;
						break;
					}
				}
			}

			// Increment buffer pointers.
			++signalOut;
			PitchModulationPolicy::IncrementPointer( pitch );
			TableModulationPolicy::IncrementPointer( table );
			SlotModulationPolicy::IncrementPointer( pslot );
			RootPitchModulationPolicy::IncrementPointer( rootPitch );
		}

		GrainformCounter -= sampleFrames;
		pinSlotModulationToGui.setValue( *getBuffer(pinSlot) , blockPos_ );
		pinTableModulationToGui.setValue( *getBuffer(pinTable) , blockPos_ );

        guiUpdateCount_ -= sampleFrames;
		if( guiUpdateCount_ < 0 )
		{
			updateGuiWaveform();
		}
	};
private:

	inline void calcMipLevel( WavetableMipmapPolicy& mipMapPolicy, float increment, int& returnMipLevelA, unsigned int& returnCountMaskA );

	gmpi::StringInPin pinWaveBankId;            // id=0
	gmpi::AudioInPin pinPitch;                  // id=1
	gmpi::AudioInPin pinTable;                  // id=2
	gmpi::AudioInPin pinSlot;                   // id=3
	gmpi::AudioInPin pinEffect;                 // id=4
	gmpi::EnumInPin pinMode;                    // id=5
	gmpi::AudioOutPin pinSignalOut;             // id=6
	gmpi::FloatOutPin pinTableModulationToGui;  // id=7
	gmpi::FloatOutPin pinSlotModulationToGui;   // id=8
	gmpi::FloatInPin pinVoiceActive;            // id=9
	gmpi::BoolInPin pinSyncToNoteOn;            // id=10
	gmpi::AudioInPin pinSync;                   // id=11
	gmpi::AudioInPin pinPsolaRootPitch;         // id=12
	gmpi::BoolInPin pinInvertSlotModulation;    // id=13
	gmpi::StringInPin pinWaveTableFiles;        // id=14
	gmpi::BlobOutPin pinGuiWaveDisplay;         // id=15

	float *pitchTable{};
	WavetableMipmapPolicy mipMapPolicy;
	WavetableMipmapPolicy mipMapPolicyHanning;
	float syncCrossFadeLevel;
	int mipLevelA;
	int mipLevelB;
	unsigned int countMaskA;
	unsigned int countMaskB;
	bool previousActiveState;
	bool syncState;

	WavetableLoader waveLoader_;
};

#endif
