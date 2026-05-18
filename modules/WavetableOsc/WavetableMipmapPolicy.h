#pragma once

/*
#include "WavetableMipmapPolicy.h"
*/

#include "Wavetable.h"
#include <vector>

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
	int offset2; // when grouped by mip,slot
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
	int slotCount;				// Waveforms per wavetable.
	int totalWaveMemorySize;

public:
	WavetableMipmapPolicy(void) :
		MasterWaveSize(0)
		, slotCount(0)
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
	float GetMaximumIncrement( int mip ) const
	{
		return mips[mip].maximumIncrement;
	}
	int GetTotalMipMapSize() const
	{
		return totalWaveMemorySize;
	}

	void initialize( const WaveTable* wavetable )
	{
		initialize(wavetable->waveSize, wavetable->slotCount);
	}

	void initialize(int pWaveSize, int pSlotCount)
	{
		mips.clear();

		MasterWaveSize = pWaveSize;
		slotCount = pSlotCount;

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

			mips.push_back(mipIndex(SlotStorage, wavesize, MipOversampleRatio, MipStartIdx));

			MipStartIdx += SlotStorage * slotCount;
			totalWaveMemorySize += SlotStorage;

			// next higher octave.
			partials = partials >> 1;
			octave++;
		}
	}

	// With PSOLA window, need exactly twice the storage per wave.
	int TotalMemoryRequired() const
	{
		return sizeof(WaveTable) + (totalWaveMemorySize * slotCount - 1) * sizeof(float);
	}

	int WaveMemoryRequiredSamples() const
	{
		return sizeof(WaveTable) + (totalWaveMemorySize-1);
	}

	int getSlotOffset( int slot, int mip ) const
	{
		// Wavetable Grouped by Mips.
		return mips[mip].offset2 + mips[mip].SlotStorage * slot;
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
