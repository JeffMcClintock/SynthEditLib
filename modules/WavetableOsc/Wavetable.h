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
	// Slots per wavetable. A file that declares its own frame count (the Serum/Vital 'clm '
	// chunk) is loaded at that real count, clamped to [MinSlotCount, MaxSlotCount]. Sources
	// that declare nothing - pitch-detected samples, the legacy pre-sliced format, the builtin
	// shapes - get DefaultSlotCount.
	static const int DefaultSlotCount = 64;
	static const int MaxSlotCount = 256;  // Serum's own ceiling.
	// The audio loop always reads slot N alongside slot N+1 to crossfade the two, so even a
	// single-frame file has to occupy two slots.
	static const int MinSlotCount = 2;

	static const int WavetableFileSampleCount = 512;

#if defined(_DEBUG)
    static const int MorphedSlotRatio = 8;		// lighter memory.
	static int debugSlot;
	static float* diagnosticOutput;
	static float* diagnosticProbeOutput;
#else
    static const int MorphedSlotRatio = 8;		// For every imported slot, generate 7 in-between slots via FFT morph.
#endif

	int32_t slotCount;		// Waveforms per wavetable.
	int32_t waveSize;		// Samples in each single waveform.
	float Wavedata[1];		// Actual size depends on number of slots etc.

	float* GetSlotPtr( int slot )
	{
		return Wavedata + slot * waveSize;
	}

	static int CalcMemoryRequired(int slotCount, int waveSize )
	{
		return sizeof( WaveTable ) + sizeof(float) * waveSize * slotCount;
	}

    void SetSize( int numWaveSlots, int numWaveSamples )
    {
        slotCount = numWaveSlots;
        waveSize = numWaveSamples;
    }

	struct SerumMetadata
	{
		int frameSize = 0;           // 0 = no clm chunk found; otherwise cycle size declared by the file (Serum/Vital convention).
		int interpolationMode = 0;   // 0 none, 1 linear crossfade, 2-4 spectral.
	};

	// How a wav file's samples map onto slots. Worked out up-front by PlanLoad, because the raw
	// buffer, the mip layout and the bake all have to be sized to slotCount before the samples
	// can be sliced into place.
	struct FileLayout
	{
		enum Strategy
		{
			SerumFrames, // file declares its cycle size in a 'clm ' chunk - slice it deterministically.
			PreSliced,   // file is exactly DefaultSlotCount * WavetableFileSampleCount - already a wavetable.
			PitchDetect, // anything else: an ordinary sample, so hunt for the cycles by autocorrelation.
		};

		Strategy strategy = PitchDetect;
		int slotCount = DefaultSlotCount;
		int srcFrameSize = 0;  // SerumFrames only: cycle size the file declares.
		int framesInFile = 0;  // SerumFrames only: whole frames the file holds.
	};

	static bool LoadWaveFile( const _TCHAR* filename, std::vector<float> &returnSamples, int& returnSampleRate, SerumMetadata* returnSerumMeta = nullptr );
	static void NormalizeWave( std::vector<float>& wave );
	static float ExtractPeriod( const std::vector<float>& sample, int autocorrelateto, int slot );
	static void SliceAndDiceGetSlicePositions( const std::vector<float>& Wavefile, int slices, std::vector<int>& returnSlicePositions );

	static FileLayout PlanLoad( int sampleCount, const SerumMetadata& serumMeta );
	// Slices already-read samples into this table, which the caller must have sized to
	// plan.slotCount x WavetableFileSampleCount.
	void LoadSamples( std::vector<float>& wave, int fileSampleRate, const FileLayout& plan, int waveTablenumber = 0, int method = 1, int diagnosticPitchDetectType = 0, float* rawPitchEstimates = 0 );
	void ExportFile( const std::wstring& filename, int wavetableNumber, int selectedFromSlot, bool entireTable );
	void GenerateWavetable( int wavetableNumber, int selectedFromSlot, int selectedToSlot, int shape );
	void MorphSlots( int wavetableNumber, int selectedFromSlot, int selectedToSlot );
	void CopyAndMipmap( WaveTable* sourceWavetable, class WavetableMipmapPolicy &mipinfo );
	void CopyAndMipmap2(WavetableMipmapPolicy &destMipInfo, float* destSamples);
};
