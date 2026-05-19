#ifndef WAVETABLEOSC_H_INCLUDED
#define WAVETABLEOSC_H_INCLUDED

#include "Processor.h"
#include "WavetableMipmapPolicy.h"
#define _USE_MATH_DEFINES
#include <array>
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

// HD-style grain: a direct wavetable reader (no Hann envelope, no PSOLA). One
// grain is alive in steady state; a second is spawned during mip transitions
// for a short crossfade, then the old one fades out and stops.
struct Grain
{
	int waveSize = 0;          // 0 = inactive; otherwise the mip's cycle length.
	double count = 0.0;        // phase 0..1 within the cycle.
	int mipLevel = 0;          // index into mipMapPolicy for this grain.
	int fadeIndex = 0;         // current position in the crossfade table.
	int fadeIncrement = 0;     // -1 fading out, 0 steady (full gain), +1 fading in.
	float minIncrement = 0.0f; // lower bound of pitch this mip serves.
	float maxIncrement = 0.0f; // upper bound (above this, switch to a smaller-wavesize mip).
};

// SE_DECLARE_INIT_STATIC_FILE equivalent for static library linking
#ifndef SE_DECLARE_INIT_STATIC_FILE
#define SE_DECLARE_INIT_STATIC_FILE(filename) void se_static_library_init_##filename(){}
#endif

class WavetableOsc : public gmpi::Processor
{
private:
	// At most 2 grains are active at once (one steady + one mid-transition); 4 leaves
	// headroom for back-to-back rapid mip changes during pitch sweeps.
	static constexpr int MaxGrains = 4;
	static constexpr int syncCrossFadeSamples = 8;

	Grain grains[MaxGrains];
	std::array<float, syncCrossFadeSamples + 1> syncFadeCurve_; // sin-shaped 0..1 crossfade.

	float sampleRate_ = 44100.0f;

	// Shared baked wavetable - process-wide cache keyed by (URI, sample rate).
	// `waveData_` is the raw float* into bakedStorage, cached for the audio loop.
	std::shared_ptr<CachedWavetable> waveTable_;
	float* waveData_{};

	// Shared per-sample-rate pitch lookup table.
	struct PitchTableData { std::vector<double> data; };
	std::shared_ptr<PitchTableData> pitchTableShared_;

public:

	WavetableOsc();
	gmpi::ReturnCode open(gmpi::api::IUnknown* phost) override;
	void onSetPins() override;

	// Cubic Hermite interpolation. Caller guarantees [table_floor-1, table_floor+2]
	// are valid - the bake adds wraparound samples on each side of every slot so any
	// table_floor in [0, waveSize) is safe.
	inline float cubic( const float* wavedata, int table_floor, float fraction ) const
	{
		const float y0 = wavedata[table_floor - 1];
		const float y1 = wavedata[table_floor + 0];
		const float y2 = wavedata[table_floor + 1];
		const float y3 = wavedata[table_floor + 2];
		return y1 + 0.5f * fraction*(y2 - y0 + fraction*(2.0f*y0 - 5.0f*y1 + 4.0f*y2 - y3 + fraction*(3.0f*(y1 - y2) + y3 - y0)));
	}

	int slotCount = 0;
	WavetableMipmapPolicy mipMapPolicy;

	// Start a fresh grain in the first empty slot. If `fadeIn` is true the grain begins
	// with its fade gain near 0 and ramps to full over syncCrossFadeSamples audio samples.
	void startGrain(int mipLevel, double initialPhase, bool fadeIn)
	{
		for (int g = 0; g < MaxGrains; ++g)
		{
			if (grains[g].waveSize == 0)
			{
				grains[g].waveSize = mipMapPolicy.GetWaveSize(mipLevel);
				grains[g].mipLevel = mipLevel;
				grains[g].count = initialPhase;
				grains[g].minIncrement = mipMapPolicy.GetMinimumIncrement(mipLevel);
				grains[g].maxIncrement = mipMapPolicy.GetMaximumIncrement(mipLevel);
				if (fadeIn)
				{
					grains[g].fadeIncrement = 1;
					grains[g].fadeIndex = 0;
				}
				else
				{
					grains[g].fadeIncrement = 0;
					grains[g].fadeIndex = syncCrossFadeSamples;
				}
				return;
			}
		}
	}

	template< class PitchModulationPolicy, class SlotModulationPolicy >
	void subProcess( int sampleFrames )
	{
		const float* pslot = getBuffer(pinSlot);
		float* signalOut = getBuffer(pinSignalOut);
		const float* pitch = getBuffer(pinPitch);

		double increment;
		float slot_frac;
		int slot1_floor;
		PitchModulationPolicy::CalcInitial(pitchTable, *pitch, increment);
		SlotModulationPolicy::CalcInitial(*pslot, slotCount, slot1_floor, slot_frac);

		// Lazy-init: spawn the first grain at the current pitch only if NO grain is
		// alive (after a wavetable change or fresh voice activation). The active grain
		// rotates between slots as mip transitions happen, so checking only slot 0
		// would spuriously spawn a phase-zero second grain alongside the real one.
		bool anyAlive = false;
		for (const auto& g : grains)
		{
			if (g.waveSize) { anyAlive = true; break; }
		}
		if (!anyAlive)
		{
			const int initMip = mipMapPolicy.CalcMipLevel(static_cast<float>(increment));
			startGrain(initMip, 0.0, false);
		}

		for( int s = sampleFrames; s > 0; --s )
		{
			PitchModulationPolicy::Calculate(pitchTable, *pitch, increment);
			SlotModulationPolicy::Calculate(*pslot, slotCount, slot1_floor, slot_frac);

			float samp = 0.0f;
			for (int g = 0; g < MaxGrains; ++g)
			{
				if (grains[g].waveSize == 0) continue;

				const int ws = grains[g].waveSize;
				const double index = grains[g].count * (double)ws;
				int table_floor = static_cast<int>(index);
				const float fraction = static_cast<float>(index - (double)table_floor);
				table_floor &= ws - 1; // cycle wrap; wraparound samples in the bake handle the cubic stencil.

				const float* waveA = waveData_ + mipMapPolicy.getSlotOffset(slot1_floor,     grains[g].mipLevel);
				const float* waveB = waveData_ + mipMapPolicy.getSlotOffset(slot1_floor + 1, grains[g].mipLevel);
				const float sA = cubic(waveA, table_floor, fraction);
				const float sB = cubic(waveB, table_floor, fraction);
				float grainSample = sA + slot_frac * (sB - sA);

				// Fade gain during mip-switch crossfade. Steady grains have fadeIncrement=0
				// and use full gain (1.0) implicitly.
				if (grains[g].fadeIncrement != 0)
				{
					grainSample *= syncFadeCurve_[grains[g].fadeIndex];
					if (grains[g].fadeIncrement > 0)
					{
						if (grains[g].fadeIndex == syncCrossFadeSamples)
							grains[g].fadeIncrement = 0;
						else
							++grains[g].fadeIndex;
					}
					else
					{
						if (grains[g].fadeIndex == 0)
							grains[g].waveSize = 0;
						else
							--grains[g].fadeIndex;
					}
				}

				samp += grainSample;
				grains[g].count += increment;
			}

			*signalOut = samp;

			// On cycle wrap, check whether the active steady grain still has the right mip.
			// If pitch has moved outside this mip's range, kick off a crossfade to the new mip.
			for (int g = 0; g < MaxGrains; ++g)
			{
				if (grains[g].waveSize && grains[g].count >= 1.0)
				{
					const int wraps = static_cast<int>(grains[g].count);
					grains[g].count -= (double)wraps;

					if (grains[g].fadeIncrement == 0)
					{
						const float incF = static_cast<float>(increment);
						if (incF > grains[g].maxIncrement || incF < grains[g].minIncrement)
						{
							grains[g].fadeIncrement = -1;
							grains[g].fadeIndex = syncCrossFadeSamples;

							const int newMip = mipMapPolicy.CalcMipLevel(incF);
							startGrain(newMip, grains[g].count, true);
						}
					}
				}
			}

			++signalOut;
			PitchModulationPolicy::IncrementPointer(pitch);
			SlotModulationPolicy::IncrementPointer(pslot);
		}
	}

private:

	gmpi::AudioInPin pinPitch;
	gmpi::AudioInPin pinSlot;
	gmpi::AudioOutPin pinSignalOut;
	gmpi::FloatInPin pinVoiceActive;
	gmpi::StringInPin pinWaveTableFile;

	double *pitchTable{};
	bool previousActiveState = false;
};

#endif
