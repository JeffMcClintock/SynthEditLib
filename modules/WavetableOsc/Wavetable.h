#pragma once

/*
#include "Wavetable.h"
*/

#include <algorithm>
#include <string>
#include <vector>
#include "math.h"
#include <cstdint>
#include "../shared/platform_string.h"

#define NUM_WAVETABLE_OSCS 2
#define SE_WT_OSC_STORE_HALF_CYCLES


struct Genes
{
	double referenceDb;
	double retainDb; // = 50.0f; // Whitener - how much spectrum to keep. throws away anything less than this below peak shape.
	double WhiteningFilter; // = 50.0f; // Whitener - how much spectrum to keep. throws away anything less than this below peak shape.
};

class PeriodExtractor
{
public:

	Genes settings;
	static std::vector<double> overrideSettings;

	// Pitch detection range, relative to FFT size.
	static const int maxOctave = 9; // 8 = 512 bins, 9 = 1024

	PeriodExtractor();
	~PeriodExtractor();
	float ExtractPeriod2(float* sample, int sampleCount, int autocorrelateto, float* diagnosticOutput = 0, float* diagnosticProbeOutput = 0);

	inline static double ResultBinToFftBin( int resultBin, int binCount )
	{
		return 2.0 * pow( 2.0, maxOctave * resultBin / (double) binCount );
	}

	inline static float calcProbeFunction( double bin, double candidateFundamentalBin )
	{
		double windowWidth = ( std::min )( 8.0, candidateFundamentalBin / 4.0 );
		double windowheight = 1.0 + 0.25 * ( 8.0 / windowWidth - 1.0 ); // make narrow windows taller to help compete with higher frequencies. mixed results.

		// calulate harmonic probe function.
		double nearest = candidateFundamentalBin * floor( 0.5 + bin / candidateFundamentalBin );
		double dist = fabs( bin - nearest );
		double window;

		if( bin / candidateFundamentalBin < 0.5 ) // all peaks below fundamental are negative. Proportional to ratio of peak to trough.
		{
			window = -windowWidth / ( candidateFundamentalBin * 0.25 );
		}
		else
		{
			if( dist < windowWidth )
			{
				if( nearest < 1.0 )
				{
					window = 0.0; // DC don't count as a harmonic.
				}
				else
				{
					//window = 1.0; // rect.
					//window = 1.0 - dist / windowWidth; // tri
					window = 1.0 - dist * dist / ( windowWidth*windowWidth ); // parabola
				}
			}
			else
			{
				//window = -1.0;
				//window = -( dist - windowWidth ) / ( 0.5 * freq - windowWidth );
				window = ( 0.5 * candidateFundamentalBin - dist ) / ( 0.5 * candidateFundamentalBin - windowWidth ); // parabola.
				window = window * window - 1.0f;
				window *= windowWidth / ( candidateFundamentalBin * 0.25 ); // scale depending on relative width.
			}

		}

		// fundamental more important than higher harmonics, weight in favor with tail off.
		if( bin > candidateFundamentalBin )
		{
			//window *= candidateFundamentalBin / bin; // 1/r falloff.
			//float hamonic = bin / candidateFundamentalBin;
			//window *= 1.0 / ( (hamonic - 1.0) * 0.33 + 1.0); // 1/3r falloff.
			const float harmonicFalloff = 1.0f / 4.0f; // each higher harmonin less important.
			window *= candidateFundamentalBin / ( ( bin - candidateFundamentalBin ) * harmonicFalloff + candidateFundamentalBin ); // 1/4r falloff.
		}

		return (float) ( window * windowheight );
	}

	void Whiten( float* spectrum, int n, bool debug );
	static void MedianFilterPitches(std::vector<float>& periods);
};




// An collection of Wavetables.
struct WaveTable
{
	static const int FactoryWavetableCount = 64;
	static const int UserWavetableCount = 20;
	static const int WavetableFileSlotCount = 64;
	static const int WavetableFileSampleCount = 512;

#if defined(_DEBUG)
    static const int MorphedSlotRatio = 8;		// lighter memory.
	static const int MaximumTables = 64;
	static int debugSlot;
	static float* diagnosticOutput;
	static float* diagnosticProbeOutput;
#else
    static const int MorphedSlotRatio = 8;		// For every imported slot, generate 7 in-between slots via FFT morph.
    static const int MaximumTables = 64;
#endif

	int32_t waveTableCount;	// Total Wavetable slots.
	int32_t slotCount;		// Waveforms per wavetable.
	int32_t waveSize;		// Samples in each single waveform.
	float Wavedata[1];		// Actual size depends on number of slots etc.

	float* GetSlotPtr( int table, int slot )
	{
		table = (std::max)( (std::min)( table, waveTableCount - 1 ), 0 );
		return Wavedata + ( table * slotCount + slot ) * waveSize;
	}

	static int CalcMemoryRequired( int tableCount, int slotCount, int waveSize )
	{
		return sizeof( WaveTable ) + sizeof(float) * waveSize * slotCount * tableCount;
	}

    void SetSize( int numWaveTables, int numWaveSlots, int numWaveSamples )
    {
        waveTableCount = numWaveTables;
        slotCount = numWaveSlots;
        waveSize = numWaveSamples;
    }

	static bool LoadWaveFile( const _TCHAR* filename, std::vector<float> &returnSamples, int& returnSampleRate );
	static void NormalizeWave( std::vector<float>& wave );
	static float ExtractPeriod( const std::vector<float>& sample, int autocorrelateto, int slot );
	static void SliceAndDiceGetSlicePositions( const std::vector<float>& Wavefile, int slices, std::vector<int>& returnSlicePositions );

    bool LoadFile3( const _TCHAR* filename, bool fileIsWavetable, int wavetableNumber = 0 );
	bool LoadFile2(int selectedFromSlot, const _TCHAR* filename, bool fileIsWavetable, int waveTablenumber, bool entireTable, int method = 1, int diagnosticPitchDetectType = 0, float* rawPitchEstimates = 0);
	void ExportFile( const std::wstring& filename, int wavetableNumber, int selectedFromSlot, bool entireTable );
	void GenerateWavetable( int wavetableNumber, int selectedFromSlot, int selectedToSlot, int shape );
	void MorphSlots( int wavetableNumber, int selectedFromSlot, int selectedToSlot );
	void CopyAndMipmap( WaveTable* sourceWavetable, class WavetableMipmapPolicy &mipinfo );
	void CopyAndMipmap2(WavetableMipmapPolicy &destMipInfo, int wavetable, float* destSamples);
};
