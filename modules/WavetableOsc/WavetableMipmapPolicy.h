#pragma once

/*
#include "WavetableMipmapPolicy.h"
*/

#include "Wavetable.h"
#include <vector>

#define USE_FLOAT_COUNTER

struct mipIndex
{
	mipIndex(int ss,int ws, int oversampleRatio, int off2) :
		waveSize(ws)
		, SlotStorage(ss)
		, offset2(off2)
	{
		maximumIncrement = (float)oversampleRatio / (float) waveSize;
	}
	int SlotStorage;
	int offset2; // when grouped by mip,wave,slot
	int waveSize;
	float maximumIncrement; // helps decide when to shift down to a lower mip-level.
};

#define maximumWaveSize 512 // 1024	//
#define minimumWaveSize 32 //64		// Maintain clarity due to imperfections in interpolator.
#define oversampleRatio 2		// Improves quality of interpolator.

class WavetableMipmapPolicy
{
public:

private:
	std::vector<mipIndex> mips;
	int MasterWaveSize;
	int waveTableCount;			// Total Wavetable slots.
	int slotCount;				// Waveforms per wavetable.
	int totalWaveMemorySize;
	bool storeHalfCycles;

public:
	WavetableMipmapPolicy(void) :
		MasterWaveSize(0)
		, waveTableCount(0)
		, slotCount(0)
		, storeHalfCycles(true)
	{
	}

	int getMipCount() const
	{
		return (int)mips.size();
	}
	int GetWaveSize(int mip) const
	{
		return mips[mip].waveSize;
	}
	int GetFftBinCount( int mip ) const // number of components needed in reverse FFT (including DC Componenet)
	{
		int mostPartials = (std::min)( MasterWaveSize, maximumWaveSize ) / 2; // Number of partials in MIP zero (most harmonics).
		return 1 + ( mostPartials >> mip ); // add one for DC component.
	}
#ifdef USE_FLOAT_COUNTER
	float GetMaximumIncrement( int mip ) const
	{
		return mips[mip].maximumIncrement;
	}
#else
	int GetMaximumIncrement( int mip )
	{
		return mips[mip].maximumIncrement;
	}
	int CalcMipLevel(int increment);
#endif
	int GetTotalMipMapSize() const
	{
		return totalWaveMemorySize;
	}

	void initialize( const WaveTable* wavetable )
	{
		storeHalfCycles = false;
#ifdef SE_WT_OSC_STORE_HALF_CYCLES // Assume symetrical wave.
		storeHalfCycles = true;
#endif
		initialize(wavetable->waveTableCount, wavetable->waveSize, wavetable->slotCount, storeHalfCycles);
	}

	void initialize( int pWaveTableCount, int pWaveSize, int pSlotCount, bool pStoreHalfCycles )
	{
		mips.clear();

		waveTableCount = pWaveTableCount;
		MasterWaveSize = pWaveSize;
		slotCount = pSlotCount;
		storeHalfCycles = pStoreHalfCycles;

		int wavesize = (std::min)( MasterWaveSize, maximumWaveSize );

		int partials = wavesize / 2;
		int octave = 0;
		totalWaveMemorySize = 0;
		int MipStartIdx = 0;
		while( partials > 0 )
		{
			wavesize = (MasterWaveSize >> octave ) * oversampleRatio;
			wavesize = (std::min)( wavesize, maximumWaveSize );
			wavesize = (std::max)( wavesize, minimumWaveSize );
			int MipOversampleRatio = wavesize / (MasterWaveSize >> octave ); // large waves are not oversampled.

			int SlotStorage = wavesize;
			if(storeHalfCycles)
			{
				SlotStorage /= 2;
			}

			mips.push_back(mipIndex(SlotStorage, wavesize, MipOversampleRatio, MipStartIdx));

			MipStartIdx += SlotStorage * waveTableCount * slotCount;
			totalWaveMemorySize += SlotStorage;

			// next higher octave.
			partials = partials >> 1;
			octave++;
		}
	}

	// With PSOLA window, need exactly twice the storage per wave.
	int TotalMemoryRequired() const
	{
		return sizeof(WaveTable) + (totalWaveMemorySize * waveTableCount * slotCount - 1) * sizeof(float);
	}

	int WaveMemoryRequiredSamples() const
	{
		return sizeof(WaveTable) + (totalWaveMemorySize-1);
	}

	int getSlotOffset( int table, int slot, int mip ) const
	{
		// Wavetable Grouped by Mips.
		return mips[mip].offset2 + mips[mip].SlotStorage * (slot + table * slotCount);
	}

	int CalcMipLevel(float increment) const
	{
		for( int i = 0 ; i < (int) mips.size() ; ++i )
		{
			if( mips[i].maximumIncrement >= increment )
			{
				return i;
			}
		}
		return (int)mips.size() - 1;
	}
	inline int getSlotCount()
	{
		return slotCount;
	}
};
