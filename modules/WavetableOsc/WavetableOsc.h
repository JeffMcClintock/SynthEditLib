#ifndef WAVETABLEOSC_H_INCLUDED
#define WAVETABLEOSC_H_INCLUDED

#include "Processor.h"
#include "WavetableMipmapPolicy.h"
#define _USE_MATH_DEFINES
#include <vector>
#include <math.h>
#include <memory>
#include "WavetableCache.h"
#include "../shared/SharedObject.h"

#undef min
#undef max

const static int pitchTableLowVolts = -4; // ~ 1Hz
const static int pitchTableHiVolts = 11;  // ~ 20kHz.

// Fast version using table. Phase increment computed at double precision so it
// can be safely accumulated into a double phase counter without per-sample drift.
inline double ComputeIncrement2( const double* pitchTable, float pitch )
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

	double y0 = pitchTable[table_floor-1];
	double y1 = pitchTable[table_floor+0];
	double y2 = pitchTable[table_floor+1];
	double y3 = pitchTable[table_floor+2];

	return y1 + 0.5 * fraction*(y2 - y0 + fraction*(2.0*y0 - 5.0*y1 + 4.0*y2 - y3 + fraction*(3.0*(y1 - y2) + y3 - y0)));
}

class PitchFixed
{
public:
	inline static void CalcInitial( const double* pitchTable, float pitch, double& returnIncrement )
	{
		returnIncrement = ComputeIncrement2( pitchTable, pitch );
	};

	inline static void Calculate( const double* pitchTable, float pitch, double& returnIncrement )
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
	inline static void CalcInitial( const double* pitchTable, float pitch, double& returnIncrement )
	{
		// do nothing. Hopefully optimizes away to nothing.
	};

	inline static void Calculate( const double* pitchTable, float pitch, double& returnIncrement )
	{
		returnIncrement = ComputeIncrement2( pitchTable, pitch );
	};

	inline static void IncrementPointer( const float*& pitch )
	{
		++pitch;
	};
	enum { Active = true };
};


class PsolaChanging
{
public:
	inline static void CalcInitialPsola(const double* pitchTable, double Increment, float PsolaAmmount, double& grainIncrement)
	{
	};
	inline static void CalculatePsola(const double* pitchTable, double Increment, float PsolaAmmount, double& grainIncrement)
	{
#if 0
		// was calc psola root pitch then interpolating between that and normal inc.
		// not intuitive to use.
		double psolaIncrement = ComputeIncrement2(pitchTable, PsolaRootPitch);
        double i = Increment + (psolaIncrement - Increment);
        const double psolaLimit = 0.25; // prevent PSOLA going sub-sonic. two octave down anyhow.
        double minInc = Increment * psolaLimit;
        if( i < minInc )
        {
            i = minInc;
        }
        grainIncrement = 0.5 * i;
#else
		// calc PSOLA pitch as a ratio of normal pitch. e.g. 10V = +1 octave.
		// 0.5x because each PSOLA grain spans two mirrored wavetable cycles,
		// so unity formant rate needs the grain to take two fundamental periods.
		grainIncrement = 0.5 * Increment * exp2((double)std::clamp(PsolaAmmount, -2.0f, 2.0f));
#endif
	};
	inline static void IncrementPointer(const float* ptr)
	{
		++ptr;
	};
	enum { Active = false };
};

class PsolaFixed
{
public:
	inline static void CalcInitialPsola( const double* pitchTable, double Increment, float PsolaRootPitch, double& grainIncrement )
	{
		return PsolaChanging::CalculatePsola(pitchTable, Increment, PsolaRootPitch, grainIncrement);
	};
	inline static void CalculatePsola( const double* pitchTable, double Increment, float PsolaRootPitch, double& grainIncrement )
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

struct Grain
{
	double count = 0.0;
	int waveSize = 0;
	float* wave = nullptr;
};

// SE_DECLARE_INIT_STATIC_FILE equivalent for static library linking
#ifndef SE_DECLARE_INIT_STATIC_FILE
#define SE_DECLARE_INIT_STATIC_FILE(filename) void se_static_library_init_##filename(){}
#endif

class WavetableOsc : public gmpi::Processor
{
private:
	const static int MaxGrains = 32; // power-of-2 please.
	const static int extraInterpolationPreSamples = 1;
	const static int extraInterpolationPostSamples = 3;

	double count = 0.99999999;
	Grain grains[MaxGrains];

	float *hanning{};

	// Shared baked wavetable - process-wide cache keyed by full file URI.
	// `waveData_` is the raw float* into bakedStorage, cached for the audio loop.
	std::shared_ptr<CachedWavetable> waveTable_;
	float* waveData_{};

	// Shared per-sample-rate / per-format scratch tables.
	struct PitchTableData { std::vector<double> data; };
	struct HanningData { std::vector<float> data; };

	std::shared_ptr<PitchTableData> pitchTableShared_;
	std::shared_ptr<HanningData> hanningShared_;

public:

	WavetableOsc();
	gmpi::ReturnCode open(gmpi::api::IUnknown* phost) override;
	void onSetPins() override;

	// Interpolate between slots. float counter.
	inline float get_sample3b( const float* wavedata, int table_floor, float fraction ) const
	{
		assert(table_floor >= 0);

		// Cubic.
		const auto y0 = wavedata[(table_floor-1)];
		const auto y1 = wavedata[(table_floor+0)];
		const auto y2 = wavedata[(table_floor+1)];
		const auto y3 = wavedata[(table_floor+2)];

		return y1 + 0.5f * fraction*(y2 - y0 + fraction*(2.0f*y0 - 5.0f*y1 + 4.0f*y2 - y3 + fraction*(3.0f*(y1 - y2) + y3 - y0)));
	}

	int slotCount = 0;
	static const int grainformCount = 4;
	int currentGrainform;
	int GrainformCounter = -1;
	int currentGrainformMipwavesize;
	int currentGrain_mipLevel = -1;
	float currentGrain_slot = -1;
	float grainform[grainformCount][maximumWaveSize * 2 + extraInterpolationPreSamples + extraInterpolationPostSamples]; // pre-calculated windowed cycles.
	int GrainformDuration_;

	template< class PitchModulationPolicy, class SlotModulationPolicy, class PsolaModulationPolicy >
	void subProcess( int sampleFrames )
	{
		// get pointers to in/output buffers.
		const float* pslot = getBuffer(pinSlot);
		float* signalOut = getBuffer(pinSignalOut);
		const float* pitch = getBuffer(pinPitch);
		const float* rootPitch = getBuffer(pinPsolaOffset);

		double increment, grainIncrementFast;
		float slot_frac;
		int slot1_floor;
		PitchModulationPolicy::CalcInitial( pitchTable, *pitch, increment );
		PsolaModulationPolicy::CalcInitialPsola( pitchTable, increment, *rootPitch, grainIncrementFast );
		SlotModulationPolicy::CalcInitial( *pslot, slotCount, slot1_floor, slot_frac );

		for( int s = sampleFrames; s > 0; --s )
		{
			PitchModulationPolicy::Calculate( pitchTable, *pitch, increment );
			PsolaModulationPolicy::CalculatePsola( pitchTable, increment, *rootPitch, grainIncrementFast );

			float samp = 0.0f;
			for( int g = 0 ; g < MaxGrains ; ++g )
			{
				if( grains[g].waveSize )
				{
					double index = grains[g].count * (double) grains[g].waveSize;
					int table_floor = static_cast<int>(index);
					float fraction = static_cast<float>(index - (double) table_floor);
					float grainSample = get_sample3b( grains[g].wave, table_floor, fraction );

					samp += grainSample;

					grains[g].count += grainIncrementFast;
					if( grains[g].count > 1.0 ) // 1 grain? done.
						grains[g].waveSize = 0; // indicates inactive grain.
				}
			}
			*signalOut = samp;

			count += increment;
			if( count > 1.0 ) // is count wrapping? (pretty intensive on high frequencies )
			{
				// count wraps at 1.0
				int count_floor = static_cast<int>(count);
				count -= (double) count_floor;

				// start a fresh grain.
				for( int g = 0 ; g < MaxGrains ; ++g )
				{
					if( grains[g].waveSize == 0 )
					{
						grains[g].count = count;// *0.5; // Make grain phase fractionally correct.
						float* wave = &( grainform[currentGrainform][1] ); // create one virtual negative table value to aid interpolation.

						// if time since last grain calc.
						if( GrainformCounter < 0 )
						{
							int mipLevel = mipMapPolicy.CalcMipLevel( static_cast<float>(grainIncrementFast * 2.0) );
							int mipwavesize = currentGrainformMipwavesize = mipMapPolicy.GetWaveSize(mipLevel);

							SlotModulationPolicy::Calculate( *pslot, slotCount, slot1_floor, slot_frac );

							GrainformCounter = GrainformDuration_; // restart counter.
							// If parameters the same, just re-use previous grain.
							if( mipLevel != currentGrain_mipLevel || *pslot != currentGrain_slot )
							{
								currentGrain_mipLevel = mipLevel;
								currentGrain_slot = *pslot;

								// increment/wrap current grainform.
								currentGrainform = (currentGrainform + 1) & (grainformCount - 1);
								wave = &( grainform[currentGrainform][1] ); // create one virtual negative table value to aid interpolation.

								// re-fill grainform.

								float* wave1a = waveData_ + mipMapPolicy.getSlotOffset(slot1_floor, mipLevel);
								float* wave1b = waveData_ + mipMapPolicy.getSlotOffset(slot1_floor + 1, mipLevel);

								float* pHanning = hanning + mipMapPolicyHanning.getSlotOffset(0,mipLevel);

								// Sample 'off-front' to aid cubic interpolation.
								wave[-1] = 0.0f;
								// Wrap a few samples 'off-end' to simplify interpolation. Hanning window decays to ~0 there.
								for( int e = 0 ; e < extraInterpolationPostSamples ; ++e )
								{
									wave[mipwavesize * 2 + e] = 0.0f;
								}

								// Build a 2-cycle PSOLA grain: each cycle is the full wavetable cycle
								// (interpolated between slots), multiplied by a full Hann envelope of
								// length 2*mipwavesize that peaks at the grain centre. pHanning stores
								// the rising half (0 -> 1) over [0, mipwavesize]; the falling half is
								// the mirror of that, indexed as pHanning[mipwavesize - c].
								for( int c = 0 ; c < mipwavesize ; ++c )
								{
									float p1 = wave1a[c];
									float p2 = wave1b[c];
									float grainSample = p1 + slot_frac * (p2 - p1); // interpolate between slots.

									wave[c]               = grainSample * pHanning[c];
									wave[mipwavesize + c] = grainSample * pHanning[mipwavesize - c];
								}
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
			SlotModulationPolicy::IncrementPointer( pslot );
			PsolaModulationPolicy::IncrementPointer( rootPitch );
		}

		GrainformCounter -= sampleFrames;
	};
private:

	inline void calcMipLevel( WavetableMipmapPolicy& mipMapPolicy, double increment, int& returnMipLevelA, unsigned int& returnCountMaskA );

	gmpi::AudioInPin pinPitch;
	gmpi::AudioInPin pinSlot;
	gmpi::AudioOutPin pinSignalOut;
	gmpi::FloatInPin pinVoiceActive;
	gmpi::AudioInPin pinPsolaOffset;
	gmpi::StringInPin pinWaveTableFile;

	double *pitchTable{};
	WavetableMipmapPolicy mipMapPolicy;
	WavetableMipmapPolicy mipMapPolicyHanning;
	int mipLevelA = 0;
	unsigned int countMaskA = 0;
	bool previousActiveState = false;
};

#endif
