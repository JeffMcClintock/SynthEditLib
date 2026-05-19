#pragma once

/*
#include "WavetableMipmapPolicy.h"
*/

#include "Wavetable.h"
#include <vector>
#include <algorithm>
#include <climits>
#include <cmath>

// Per-mip storage layout. waveSize is the cycle length in samples; maxHarmonic
// is the highest harmonic stored (anything above this is zeroed in the bake so
// playback at the mip's max fundamental doesn't alias).
struct mipIndex
{
	mipIndex(int waveSize_, int maxHarmonic_, float maximumIncrement_, int offset_) :
		waveSize(waveSize_)
		, SlotStorage(waveSize_)
		, offset2(offset_)
		, maxHarmonic(maxHarmonic_)
		, maximumIncrement(maximumIncrement_)
	{
	}
	int SlotStorage;       // bytes per slot in this mip (= waveSize for non-half-cycle storage).
	int offset2;           // byte-offset of slot 0 within the baked storage.
	int waveSize;
	int maxHarmonic;       // highest harmonic stored (after audible-Nyquist cap).
	float maximumIncrement; // helps decide when to shift down to a lower mip-level.
};

#define maximumWaveSize 2048
#define minimumWaveSize 32
#define oversampleRatio 4		// Samples per harmonic = 2 * oversampleRatio; flatter cubic-interp passband.

class WavetableMipmapPolicy
{
private:
	std::vector<mipIndex> mips;
	int MasterWaveSize;
	int slotCount;				// Waveforms per wavetable.
	int totalWaveMemorySize;

public:
	WavetableMipmapPolicy(void) :
		MasterWaveSize(0)
		, slotCount(0)
		, totalWaveMemorySize(0)
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
		return 1 + mips[mip].maxHarmonic; // DC + harmonics 1..maxHarmonic.
	}
	float GetMaximumIncrement( int mip ) const
	{
		return mips[mip].maximumIncrement;
	}
	int GetTotalMipMapSize() const
	{
		return totalWaveMemorySize;
	}

	void initialize( const WaveTable* wavetable, float sampleRate )
	{
		initialize(wavetable->waveSize, wavetable->slotCount, sampleRate);
	}

	// Per-note mip layout (HD-style): for each MIDI key, recycle the current mip if it
	// can still play that note without aliasing; otherwise spawn a new mip whose
	// maxHarmonic = floor(audibleNyquist / Hz). Wave size = nearest pow-of-2 >= 2 * oversample * maxHarmonic,
	// clamped to [minimumWaveSize, maximumWaveSize]. Switch boundaries land on half-semitone
	// notes so vibrato around an integer note doesn't flap between mips.
	void initialize(int pWaveSize, int pSlotCount, float sampleRate)
	{
		mips.clear();
		MasterWaveSize = pWaveSize;
		slotCount = pSlotCount;
		totalWaveMemorySize = 0;

		const int sourceMaxHarmonic = (std::min)(pWaveSize, maximumWaveSize) / 2;

		constexpr float nyquistSafety = 0.95f;
		constexpr float humanHearingMaxHz = 20000.0f;
		const float audibleNyquist = sampleRate * 0.5f * nyquistSafety;
		const float maxAudibleFreq = (std::min)(audibleNyquist, humanHearingMaxHz);

		// Switch boundaries are between notes (+0.5 semitone offset) so that exact-MIDI
		// pitches and small vibrato around them don't trigger constant mip flipping.
		constexpr float middleA_midi = 69.0f;
		constexpr float A4_hz = 440.0f;

		int currentMaxHarmonic = INT_MAX;
		int currentOffset = 0;

		for (int key = 0; key < 128; ++key)
		{
			// Use the half-semitone above this key as the "playable" pitch to consider,
			// so the mip's safety margin holds for any pitch up to key + 0.5 semitones.
			const float Hz = A4_hz * std::pow(2.0f, (key + 0.5f - middleA_midi) / 12.0f);

			// Can the current mip cover this key safely?
			if (currentMaxHarmonic != INT_MAX && (float)currentMaxHarmonic * Hz < maxAudibleFreq)
				continue;

			// Need a new mip. Cap harmonics at both the source's max and what fits below audible Nyquist.
			int newMaxH = static_cast<int>(maxAudibleFreq / Hz);
			newMaxH = (std::min)(newMaxH, sourceMaxHarmonic);
			newMaxH = (std::max)(newMaxH, 0);

			if (newMaxH == currentMaxHarmonic) continue; // no actual change

			currentMaxHarmonic = newMaxH;

			// Wavesize: oversample, round up to nearest pow-of-2, clamp.
			int requiredSize = 2 * newMaxH * oversampleRatio;
			int ws = minimumWaveSize;
			while (ws < requiredSize) ws *= 2;
			ws = (std::min)(ws, maximumWaveSize);
			ws = (std::max)(ws, minimumWaveSize);

			// max fundamental for this mip = audibleNyquist / maxH (in Hz), converted to cycles/sample.
			float maxInc = (newMaxH > 0) ? (maxAudibleFreq / (float)newMaxH / sampleRate)
			                             : 1.0f; // a "silent" mip for very high pitches plays anything.

			mips.emplace_back(ws, newMaxH, maxInc, currentOffset);
			currentOffset += ws * slotCount;
			totalWaveMemorySize += ws;
		}

		// Always have at least one mip (degenerate case).
		if (mips.empty())
		{
			mips.emplace_back(minimumWaveSize, 0, 1.0f, 0);
			totalWaveMemorySize = minimumWaveSize;
		}
	}

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
