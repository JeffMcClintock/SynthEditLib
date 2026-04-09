#define _USE_MATH_DEFINES
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <math.h>
#include <vector>
#include <fstream>
#include <assert.h>
#include "float.h"
#include "../shared/real_fft.h"
//#include "mfc_emulation.h"
#include "WavetableMipmapPolicy.h"
#include "../shared/unicode_conversion.h"
#include <cstdint>
#include "../../mfc_emulation.h"

#ifdef _WIN32
#include <windows.h>
#include "Shlobj.h"
#endif

using namespace std;

#define FIX_ZERO_CROSSINGS

#ifndef WAVE_FORMAT_PCM
#define WAVE_FORMAT_PCM     1
#endif

#if defined(_DEBUG)
int WaveTable::debugSlot = -1;
float* WaveTable::diagnosticOutput = 0;
float* WaveTable::diagnosticProbeOutput = 0;
#endif

vector<double> PeriodExtractor::overrideSettings;

struct MYWAVEFORMATEX
{
    uint16_t  wFormatTag;         /* format type */
    uint16_t  nChannels;          /* number of channels (i.e. mono, stereo...) */
    int32_t	  nSamplesPerSec;     /* sample rate */
    int32_t   nAvgBytesPerSec;    /* for buffer estimation */
    uint16_t  nBlockAlign;        /* block size of data */
    uint16_t  wBitsPerSample;     /* number of bits per sample of mono data */
    uint16_t  cbSize;             /* the count in bytes of the size of */
                                    /* extra information (after cbSize) */
};// MYWAVEFORMATEX; //, *PWAVEFORMATEX, NEAR *NPWAVEFORMATEX, FAR *LPWAVEFORMATEX;


void WaveTable::NormalizeWave( vector<float>& wave )
{
	// normalise wave.
	float maximum = 0.0f;
	for( unsigned int i = 0 ; i < wave.size() ; ++i )
	{
		maximum = (std::max)( maximum, fabsf(wave[i]) );
	}
	maximum = (std::max)(maximum, 0.00001f); // prevent divide by zero.

	float scale = 0.5f / maximum;
	for( unsigned int i = 0 ; i < wave.size() ; ++i )
	{
		wave[i] *= scale;
	}
}

#ifdef _DEBUG
//#define DEBUG_PITCH_DETECT
#endif
float WaveTable::ExtractPeriod( const vector<float>& sample, int autocorrelateto, int slot )
{
	const int minimumPeriod = 10; // wave less than 16 samples not much use.
	const int correlateCount = 1024;
    const int n = correlateCount * 2;

#ifdef _DEBUG
	bool debugAutocorrelate = slot == -1;
	float debugtrace[5][n];
	if( debugAutocorrelate )
	{
		memset( debugtrace, 0, sizeof(debugtrace) );
	}
#endif

	// New - FFT based autocorrelate with low-pass filter.

    float realData[correlateCount * 2 +3];

    // Copy wave to temp array for FFT.
	autocorrelateto = max(0, min(autocorrelateto, (int)sample.size() - correlateCount )); // can't correlate too near end of sample.
    int tocopy = min(correlateCount, (int)sample.size() - autocorrelateto);
    for (int s = 0; s < tocopy; s++)
    {
        realData[s + 1] = sample[(int)autocorrelateto + s];

#ifdef _DEBUG
		if( debugAutocorrelate )
		{
			debugtrace[0][s] = realData[s + 1];
		}
#endif

	}

    // Zero-pad.
    for (int s = tocopy; s < n; s++)
    {
        realData[s + 1] = 0.0f;
    }

    // Perform forward FFT.
    realft(realData, n, 1);

    float autoCorrelation[n + 2];
    int no2 = n / 2;

    for (int i = 1; i < n ; i+=2)
    {
        autoCorrelation[i] = (realData[i] * realData[i] + realData[i+1] * realData[i+1]) / no2;
        autoCorrelation[i+1] = 0.0f; //  (realData[i] * dum - realData[i - 1] * realData[i]) / no2;
    }

    //low-pass filter in freq domain to deemphasis high-harmonics which confuse pitch-detection, and minimise unwanted 'ringing'
    for (int i = 1; i < n; i += 2)
    {
//        float fade = min(1.0f, 16 * i / (float)n);
//                float window = 0.5f + 0.5f * (float)Math.Cos(fade * Math.PI); // hanning.
//                float window = 1.0f - fade; // liniear ramp.
        const float curvyness = 50.0f;
        float window;
		int window_peak = 12;
		if( i > window_peak )
		{
			window = (1.0f + 1.0f / curvyness) / (1.0f + (curvyness - 1.0f) * 2.0f * (i-window_peak) / (float)n) - 1.0f / curvyness; // 1/r curve. 2 * i cuts it off at 50%
		}
		else
		{
			window = i / (float) window_peak;
		}
/*
		if (i < correlateCount - 1)
        {
            test[2][i] = ans[i] / 2;
            test[3][i] = window;
            test[3][i+1] = window;
        }
*/
#ifdef _DEBUG
		if( debugAutocorrelate )
		{
            debugtrace[1][i] = autoCorrelation[i];
            debugtrace[1][i+1] = debugtrace[1][i]; // fill in zeros.
            debugtrace[2][i] = window;
            debugtrace[2][i+1] = window;
		}
#endif
        autoCorrelation[i] *= window;
    }
    // even indices are zero.

    realft(autoCorrelation, n, -1);

	float maxVal = 0.0f;
	float minVal = FLT_MAX; // numeric_limits<float>::max( );
	float normalise = -autoCorrelation[1]; // invert correlation as brute-force method had opposite sign ( strong correlations as -ve peaks ).
	for (int j = 1; j < correlateCount; ++j)
	{
//		autoCorrelation[j] = autoCorrelation[j] / normalise; // normalise.
//       autoCorrelation[j] *= correlateCount / max( 50.0f, (float) (correlateCount - j)); // compensated for drop-off, but not are far right, else goes extreme. RAMPS LOUDER TOWARD RIGHT.
		float x = (float) j / (float) correlateCount; // compensated for drop-off.
		float inverseWindow = 1.0f - x * 0.85f; // compensate for linear drop-off, also attenuate correlation toward right to favour lowest harmonic at left.
		const float tailoff = 0.5f;
		if( x > tailoff )	// tail-off last part to avoid numerical errors as correlation becomes tiny, and to 'filter' out -ve octave erros a little. 
		{
			x = x - tailoff;
			inverseWindow += x * x;
		}
		autoCorrelation[j] /= (inverseWindow * normalise);
        //autoCorrelation[j] *= correlateCount / max( 50.0f, (float) (correlateCount + 200 - j)); // compensated for drop-off, but not at far right, else goes extreme.
//		float window = 0.5f + 0.5f * (float)cos(j * M_PI / correlateCount); // hanning.
//		autoCorrelation[j] = min( 1.5f, autoCorrelation[j]); // limit far right weirdness.
#ifdef _DEBUG
		if( debugAutocorrelate )
		{
//			assert( maxVal < 1.0f );
            debugtrace[3][j] = autoCorrelation[j];
		}
#endif


		minVal = min( minVal, autoCorrelation[j] );
		maxVal = max( maxVal, autoCorrelation[j] );
	}
	
	/*
    for (int i = 0; i < correlateCount; ++i)
    {
        test[1][i] = ans[i + 1] / ans[1];

        // example also compensated for drop-off.
        test[1][i] *= Wavesize / (float) (Wavesize - i);
	}
*/
//#ifdef _DEBUG
//	bool debugAutocorrelate = autocorrelateto == 983;
//#endif
	// old way.
#if 0

    // assume 1/3 point in sample s good place.
//	int autocorrelateto = sample.size() / 3;

    // autocorrelation.
    float minVal = numeric_limits<float>::max( ); // double.MaxValue;
    float maxVal = 0.0;

#ifdef DEBUG_PITCH_DETECT
//    Debug.WriteLine("**************************");
	_RPT0(_CRT_WARN, "**************************\n" );
#endif
    vector<float> autoCorrelation( correlateCount, 0.0f ); // = new float[correlateCount];
    for (int i = minimumPeriod; i < correlateCount; ++i)
    {
        float error = 0.0f;
        for (int j = 0; j < correlateCount; ++j)
        {
			float s1, s2;
			if( autocorrelateto + j < sample.size() )
			{
				s1 = sample[autocorrelateto + j];
			}
			else
			{
				s1 = 0.0f;
			}
			if( autocorrelateto + i + j < sample.size() )
			{
				s2 = sample[autocorrelateto + i + j];
			}
			else
			{
				s2 = 0.0f;
			}

            double diff = s1 - s2;
            error += diff * diff;
        }
/*
#ifdef DEBUG_PITCH_DETECT
//	if( debugAutocorrelate )
	{
//        Debug.WriteLine(i + ", " + error);
		_RPT2(_CRT_WARN, "%d, %f\n", i, error );

	}
#endif
	*/
        autoCorrelation[i] = (float) error;

		minVal = min( minVal, error );
        maxVal = max( maxVal, error );
    }
#endif

    // Now search auto-correlation for first dip.
    float upperThreshhold = maxVal - 0.3f * (maxVal-minVal);
    //double lowerThreshhold = minVal + (maxVal-minVal) * 0.05;

    int state = 0; // 0 - before first peak, 1 - after first peak, 2 
    float bestScore = FLT_MAX; // numeric_limits<float>::max( ); //double.MaxValue;
    int bestPeriod = 0;
//	float sucessWindow = (maxVal-minVal) * 0.05;

	int i = 0;
    for (i = minimumPeriod; i < correlateCount; ++i)
    {
		// Good candidates have small corrrelation sum, and high correlation sum at half the period. (helps reject 1 octave below actual pitch).
		float score = autoCorrelation[i]; // - (autoCorrelation[i/2] + autoCorrelation[1 + i/2]) / 2; // average a couple to reduce noise.

		#ifdef DEBUG_PITCH_DETECT
		//	if( debugAutocorrelate )
			{
		//        Debug.WriteLine(i + ", " + error);
				_RPT2(_CRT_WARN, "%d, %f\n", i, score );

			}
		#endif

        if (score > upperThreshhold) // found beginning of a peak.
        {
			break;
		}
	}

#ifdef _DEBUG
		if( debugAutocorrelate )
		{
			_RPT2(_CRT_WARN, " 1st peak starts %d (thresh %f)\n", i, upperThreshhold );
		}
#endif
		
	// search for lowest dip *after* this first peak (to ignore perfect dip at time zero).
	// gives a more reasonable version for low-val.
	minVal = FLT_MAX; // numeric_limits<float>::max( ); // double.MaxValue;
	for (int j = i; j < correlateCount; ++j)
	{
		minVal = min( minVal, autoCorrelation[j] );
	}

	// Recalc thresholds for remainder of correlation.
	float lowerThreshhold = minVal + 0.33f * (maxVal-minVal);
    upperThreshhold = maxVal - 0.3f * (maxVal-minVal);

	// Thresholds have shifted, so may need to 'climb' peak a little further (else we might inadvertently be below new lower threshold and think we are heading down already)..
	for (; i < correlateCount; ++i)
	{
		if (autoCorrelation[i] > upperThreshhold ) // going into a dip. done looking for first peak.
		{
			break;
		}
	}
#ifdef _DEBUG
		if( debugAutocorrelate )
		{
			_RPT2(_CRT_WARN, " 1st peak adjusted %d (thresh %f)\n", i, upperThreshhold );
		}
#endif


	// search for start of dip.
	for (; i < correlateCount; ++i)
	{
/* don't care
			if (autoCorrelation[j] > peak ) // found higher peak.
			{
				peak = autoCorrelation[j];
//						i = j; // advance i to peak point.
			}
*/
		if (autoCorrelation[i] < lowerThreshhold ) // going into a dip. done looking for first peak.
		{
			break;
		}
	}

#ifdef _DEBUG
		if( debugAutocorrelate )
		{
			_RPT2(_CRT_WARN, " 1st dip starts %d (thresh %f)\n", i, lowerThreshhold );
		}
#endif
/*
	// look for new max value beyond first peak to better calibrate seach for dip.
	maxVal = 0.0f;
	for (int j = i; j < correlateCount; ++j)
	{
		maxVal = max( maxVal, autoCorrelation[j] );
	}
*/
	// search for bottom of dip.
	float best = FLT_MAX; // numeric_limits<float>::max( );
	for (int j = i; j < correlateCount; ++j)
	{
		if (autoCorrelation[j] < best ) // lowest point so far.
		{
			best = autoCorrelation[j];
			bestPeriod = j;
		}
		if (autoCorrelation[j] > lowerThreshhold ) // end of dip. done.
		{
			break;
		}
	}


#ifdef _DEBUG
		if( debugAutocorrelate )
		{
			_RPT2(_CRT_WARN, " dip center %d (thresh %f)\n", bestPeriod, lowerThreshhold );
		}
#endif

	// At this point we have found the first low dip after the first big peak. This is usually the correct result (except for piano sample).
	// Since this is our best estimate we should be confidant there are no bigger dips nearby (within 50% of our best-guess period).
	// any such dip would be a better guess.
/*
	int	prevBestPeriod = bestPeriod;
	for (int j = i; j < correlateCount; ++j)
	{
		if (autoCorrelation[j] < best ) // lowest point so far.
		{
			best = autoCorrelation[j];
			bestPeriod = j;
		}
		if ( j > bestPeriod + bestPeriod/2 ) // should be safe to search up to half way to next cycle.
		{
			break;
		}
	}
*/
	// IMPROVED. Search entire remainder of impulse.
	int	prevBestPeriod = bestPeriod;
	for (int j = i; j < correlateCount; ++j)
	{
		float score = autoCorrelation[j]; // + 0.1f * (maxVal-minVal) * (j-bestPeriod) / bestPeriod; // add penalty of 10% at best * 2.
		if (score < best ) // lowest point so far.
		{
			best = score;
			bestPeriod = j;
		}
	}


#ifdef _DEBUG
		if( debugAutocorrelate && bestPeriod != prevBestPeriod )
		{
			_RPT1(_CRT_WARN, " BETTER dip center %d nearby\n", bestPeriod );
		}
#endif

/*
        else
        {
            if ( score < bestScore ) // ..now look for lowest dip.
            {
				if ( score < (bestScore - sucessWindow) || abs(bestPeriod-i) < minimumPeriod ) // Dip is better if lower than prev best and close to prev best, or if further right, but at least 5% better.
				{
					bestScore = score;
					bestPeriod = i;
				}
			}
*/
/*			Failed when first dip (at zero) way lower than any subsequent one.
            if (state == 1)
            {
                if (autoCorrelation[i] < lowerThreshhold) // ..now look for valley
                {
                    state = 2;
                }
            }
            else
            {
                if (autoCorrelation[i] > upperThreshhold) // found 2nd peak, done.
                {
                    break;
                }
                if (autoCorrelation[i] < bestScore)
                {
                    bestScore = autoCorrelation[i];
                    bestPeriod = i;
                }
            }
        }
    }
*/


#ifdef _DEBUG
		if( debugAutocorrelate )
		{
			// Normalise spectrum.
			maxVal = 0.001f;
			for (int j = 0; j < correlateCount; ++j)
			{
				maxVal = max( maxVal, debugtrace[1][j] );
			}
			maxVal = 1.0f / maxVal;
			for (int j = 0; j < correlateCount; ++j)
			{
				debugtrace[1][j] *= maxVal;

				debugtrace[3][j] = min(6.0f, debugtrace[3][j]); // clip Correlation
			}

			_RPT0(_CRT_WARN, "Sample, FFT, Window, Correlation \n" );
			for (int j = 1; j < correlateCount; ++j)
			{
				for (int i = 0; i < 4; ++i)
				{
					_RPT1(_CRT_WARN, "%f, ", debugtrace[i][j] );
				}
				_RPT0(_CRT_WARN, "\n" );
			}
			_RPT1(_CRT_WARN, "Period %d\n", bestPeriod );
		}
#endif
   //Debug.WriteLine("Period Est: " + bestPeriod);
    //Debug.WriteLine("**************************");
#ifdef DEBUG_PITCH_DETECT
	_RPT1(_CRT_WARN, "Period %d\n", bestPeriod );
#endif
    return (float) bestPeriod;
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/* moved 
float WaveTable::ExtractPeriod2( vector<float>& sample, int autocorrelateto, int sampleRate )
{
	const int minimumPeriod = 10; // wave less than 16 samples not much use.
	const int n = 2048;
	int correlateCount = n;

	// New - FFT based.
	float realData[n+1];

	// Copy wave to temp array for FFT.
	autocorrelateto = max( 0, min( autocorrelateto, (int) sample.size( ) - n ) ); // can't correlate too near end of sample.
	int tocopy = min( n, (int) sample.size( ) - autocorrelateto );
	for( int s = 0; s < tocopy; s++ )
	{
		float window = 0.5f - 0.5f * cosf( s * 2.0f *M_PI / n ); // hanning.
		realData[s + 1] = sample[(int) autocorrelateto + s] * window;
	}

	// Zero-pad.
	for( int s = tocopy; s < n; s++ )
	{
		realData[s + 1] = 0.0f;
	}

	// Perform forward FFT.
	realft( realData, n, 1 );

	// convert to magnitude spectrum.
	float autoCorrelation[n + 2];
	float no2 = n / 2;

	float lowestCommonDenominator[n / 2];
	memset( lowestCommonDenominator, 0, sizeof( lowestCommonDenominator ) );

	const double reference Db = -60.0; // -40 -> -60 seems sweet spot
	const double minimumGain = pow( 10.0, reference Db * 0.05 );

	for( int i = 1; i < n; i += 2 )
	{
		float magnitude = ( realData[i] * realData[i] + realData[i + 1] * realData[i + 1] ) / no2;

		// Avoid overflow on zero gain.
		if( magnitude < minimumGain )
		{
			magnitude = reference Db;
		}
		else
		{
			magnitude = 20.0 * log10( magnitude ); // dB
		}

		magnitude -= reference Db;

		int freq = i / 2;
		autoCorrelation[freq] = magnitude;
		/ *
		float supression = 10.f;
		for( int harmonic = 1; harmonic < 20; ++harmonic )
		{
			int addToBin = freq / harmonic;
			lowestCommonDenominator[addToBin] += ( ( magnitude - supression) * addToBin ) / ( freq * harmonic );
		}
		* /
		/ *
		int harmonic = 1;
		int bin = freq;
		int nextBin = freq / ( harmonic + 1 );
		while( bin != nextBin )
		{
			// enhance.
			lowestCommonDenominator[bin--] += magnitude;

			int gapSize = bin - nextBin;
			while( bin > nextBin )
			{
				// supress.
				lowestCommonDenominator[bin--] -= magnitude / (float) gapSize;
			}
			++harmonic;
			nextBin = freq / ( harmonic + 1 );
		}
		* /

		/ *
// 2 each partial contributes to only it's harmonic bins once.
		int gapSize = 1;
		int bin = freq;
		int nextBin = bin;
		int harmonic = 1;
		while( bin > 0 && magnitude > 0.001f)
		{
			float ammount = ( magnitude* (freq+bin)) / ( 2.0f * freq * harmonic );
			if( bin == nextBin )
			{
				// enhance.
				lowestCommonDenominator[bin] += ammount;
				lowestCommonDenominatorCount[bin]++;

				++harmonic;
				nextBin = freq / harmonic;
				gapSize = bin - nextBin - 1;
			}
			else
			{
//				lowestCommonDenominator[bin] -= ammount / (float) gapSize;
			}

//			magnitude -= magnitude / 200.0;
			--bin;
		}

		// sub harmonic.
		// debugtrace[2][freq * 2] += magnitude / 4;
	}
	* /

	/ *
	for( int bin = 1; bin < n; bin += 2 )
	{
		if( lowestCommonDenominatorCount[bin] > 1 )
			lowestCommonDenominator[bin] /= (float) lowestCommonDenominatorCount[bin];
	}
	* /
		/ *
	// 3 suppression model.
		if( magnitude > 0.0f )
		{
			int gapSize = 1;
			int bin = freq;
			int nextBin = bin;
			int harmonic = 1;
			// suppress lower partials.
			while( bin >= 0 )
			{
				//float ammount = ( magnitude* ( freq + bin ) ) / ( 2.0f * freq * harmonic );
				if( bin == nextBin )
				{
					++harmonic;
					nextBin = freq / harmonic;
					gapSize = bin - nextBin - 1;
				}
				else
				{
					// suppress.
					lowestCommonDenominator[bin] -= magnitude * 0.01f;
				}

				//			magnitude -= magnitude / 200.0;
				--bin;
			}
			// suppress higher partials.
			for( bin = freq + 1; bin < n / 2; ++bin )
			{
				lowestCommonDenominator[bin] -= magnitude * 0.01f;
			}
		}
		* /

		// sub harmonic.
		// debugtrace[2][freq * 2] += magnitude / 4;


	}
	/ *
	// 4 suppression model backwARDS.
	for( int freq = 2; freq < n / 2; ++freq )
	{
		float pEnergy = 0.0f;
		float nEnergy = 0.0f;
		int windowWidth = std::max(1, std::min( 5, freq / 4 ));
		int positiveCount = 0;
		for( int bin = 1; bin < n / 2; ++bin )
		{
			float e = autoCorrelation[bin];
			if( (bin % freq) < windowWidth || (bin % freq) > freq - windowWidth )
			{
				pEnergy += e;
				++positiveCount;
			}
			else
			{
				nEnergy += e;
				//				energy -= e;
				if( bin < freq )
				{
				}
				else
				{
				}
			}
		}
//		float scale = 1.0f - windowWidth / 5.0f;
		lowestCommonDenominator[freq] = ( pEnergy - nEnergy ) / ( n / 2 );
//		lowestCommonDenominator[freq] = pEnergy / positiveCount - nEnergy / ( n / 2 - positiveCount);

#ifdef _DEBUG
		if( debugAutocorrelate )
		{
			if( freq == 1 )
			{
				_RPT4( _CRT_WARN, "%f = %f / % d - %f", lowestCommonDenominator[freq], pEnergy, positiveCount, nEnergy );
				_RPT1( _CRT_WARN, " / %d\n", ( n / 2 - positiveCount ) );
			}
			if( freq == 2 )
			{
				_RPT4( _CRT_WARN, "%f = %f / % d - %f", lowestCommonDenominator[freq], pEnergy, positiveCount, nEnergy );
				_RPT1( _CRT_WARN, " / %d\n", ( n / 2 - positiveCount ) );
			}
		}
#endif
	}
	* /
	int bins = n / 2;

	// skip empty bins.
	int lastBin = bins;
	while( autoCorrelation[lastBin - 1] == 0.0f && lastBin > 1 )
	{
		lastBin--;
	}

	// log spaced freq probes.
	const int debugSlot = 462;
	//const int debugSlot = (int) ( 0.5 + debugFreq * subBinRatio );
	// fit to model sub-sampled.
	for( int resultSlot = 0; resultSlot < bins; ++resultSlot )
	{
		double candidateFundamentalBin = PeriodExtractor::ResultBinToFftBin( resultSlot, bins );
		double pEnergy = 0.0f;
		double windowWidth = std::min( 10.0, candidateFundamentalBin / 4.0 );
		{
			for( int bin = 0; bin < lastBin; ++bin )
			{
				double e = autoCorrelation[bin];
				double nearest = candidateFundamentalBin * floor( 0.5 + bin / candidateFundamentalBin );
				double dist = fabs( bin - nearest );
				double window;

				if( bin / candidateFundamentalBin < 0.5 ) // all peaks below fundamental are negative.
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
				pEnergy += e * window;
			}
		}

		lowestCommonDenominator[resultSlot] = pEnergy / ( n / 2 );

	}

	// Return highest peak.
	int bestPeriod = 0;
	{
		float best = -100000.0f;
		for( int i = 0; i < n / 2; ++i )
		{
			if( lowestCommonDenominator[i] > best )
			{
				best = lowestCommonDenominator[i];
				bestPeriod = i;
			}
		}
	}
	// convert bin to period.

	//double freq = 2.0 * pow( 2.0, minOctave + maxOctave * bestPeriod / (double) bins );
	double freq = PeriodExtractor::ResultBinToFftBin( bestPeriod, bins );
	double returnValue = n / freq;


	return returnValue;
}
*/
float AutoCorrelate( vector<float>& sample, int cycleStart, int autocorrelateto, int correlateCount )
{
    float error = 0.0;
    for (int j = 0; j < correlateCount; ++j)
    {
		float s1, s2;
		int i = cycleStart + j;
		if( i < (int)sample.size() && i >= 0 )
		{
			s1 = sample[i];
		}
		else
		{
			s1 = 0.0f;
		}
		
		i = cycleStart + autocorrelateto + j;
		if( i < (int)sample.size() && i >= 0 )
		{
			s2 = sample[i];
		}
		else
		{
			s2 = 0.0f;
		}

        float diff = s1 - s2;
        error += diff * diff;
	}

	return error;
}
// Golden seach stuff.
#define R 0.61803399
// The golden ratios.
#define C (1.0-R)
#define SHFT2(a,b,c) (a)=(b);(b)=(c);
#define SHFT3(a,b,c,d) (a)=(b);(b)=(c);(c)=(d);

using namespace std;


// - QDSS Windowed Sinc ReSampling subroutine.

// function parameters
// : x      = new sample point location (relative to old indexes)
//            (e.g. every other integer for 0.5x decimation)
// : indat  = original data array
// : alim   = size of data array
// : fmax   = low pass filter cutoff frequency
// : fsr    = sample rate
// : wnwdth = width of windowed Sinc used as the low pass filter

// resamp() returns a filtered new sample point

float resamp( float x, vector<float>& indat, float fmax, float fsr, int wnwdth)
{
    float r_g,r_w,r_a,r_snc,r_y; // some local variables
    r_g = 2 * fmax / fsr;            // Calc gain correction factor
    r_y = 0;
    for( int i = -wnwdth/2 ; i < (wnwdth/2)-1 ; ++ i) // For 1 window width
    {
        int j = (int) (x + i);           // Calc input sample index

        // calculate von Hann Window. Scale and calculate Sinc
        r_w     = 0.5f - 0.5f * cosf(2.0f * (float)M_PI * (0.5f + (j - x)/wnwdth) );
        r_a     = 2.0f * (float)M_PI * (j - x) * fmax/fsr;
        r_snc   = 1.0f;

        if( r_a != 0.0f )
        {
            r_snc = sinf(r_a) / r_a;
        }

        if( (j >= 0) && (j < (int) indat.size()) )
        {
            r_y = r_y + r_g * r_w * r_snc * indat[j];
        }
    }
    return r_y;                   // Return new filtered sample
}

// datasize must be power-of-two.
float resampleCyclic( float x, float* indat, int dataSize, float fmax = 0.5f, float fsr = 1.0f, int wnwdth = 20 )
{
    float r_g,r_w,r_a,r_snc,r_y; // some local variables
    r_g = 2 * fmax / fsr;            // Calc gain correction factor
    r_y = 0;
    for( int i = -wnwdth/2 ; i < (wnwdth/2)-1 ; ++ i) // For 1 window width
    {
        int j = (int) (x + i);           // Calc input sample index

        // calculate von Hann Window. Scale and calculate Sinc
        r_w     = 0.5f - 0.5f * cosf(2.0f * (float)M_PI * (0.5f + (j - x)/wnwdth) );
        r_a     = 2.0f * (float)M_PI * (j - x) * fmax/fsr;
        r_snc   = 1.0f;

        if( r_a != 0.0f )
        {
            r_snc = sinf(r_a) / r_a;
        }

//        if( (j >= 0) && (j < indat.size()) )
        {
            r_y = r_y + r_g * r_w * r_snc * indat[j & (dataSize-1)];
        }
    }
    return r_y;                   // Return new filtered sample
}


float AutoCorrelateFractional( vector<float>& sample, int cycleStart, float autocorrelateto, int correlateCount )
{
    float error = 0.0;
    for (int j = 0; j < correlateCount; ++j)
    {
		float s1, s2;
		if( cycleStart + j < (int)sample.size() && cycleStart + j >= 0 )
		{
			s1 = sample[cycleStart + j];
		}
		else
		{
			s1 = 0.0f;
		}

        const int sincSize = 20;
        const float filtering = 0.5f;
		s2 = resamp(cycleStart + j + autocorrelateto, sample, filtering, 1.0, sincSize);

        float diff = s1 - s2;
        error += diff * diff;
	}

	return error;
}

void WaveTable::SliceAndDiceGetSlicePositions( const vector<float>& Wavefile, int slices, vector<int>& returnSlicePositions )
{
	returnSlicePositions.clear();

	// trim silence.
	float threshhold = 0.05f;
	int startPos = 0;
	int EndPos = Wavefile.size() - 1;
	// Find first loud bit.
	for( int i = 0; i < EndPos; ++i )
	{
		if( fabs( Wavefile[i] ) > threshhold )
		{
			startPos = i;
			break;
		}
	}
	// backtrack to zero-xing.
	bool positive = Wavefile[startPos] > 0.0f;
	for( int i = startPos; i > 0; --i )
	{
		if( ( Wavefile[i] > 0.0f ) != positive )
		{
			startPos = i;
			break;
		}
	}

	// Find tail.
	for( int i = EndPos; i > startPos; --i )
	{
		if( fabs( Wavefile[i] ) > threshhold )
		{
			EndPos = i;
			break;
		}
	}

	int slots = slices;
	// We can only operate to within 2 period of end of wave.
	float period = ExtractPeriod( Wavefile, EndPos, 0 );// get best guess at period near end of file.
	int lastCycleStart = EndPos - (int) period * 2; // cycle extration looks ahead one cycle.

	for( int slot = 0; slot < slots; ++slot )
	{
		int cycleStart = startPos + ( ( lastCycleStart - startPos ) * slot ) / ( slots - 1 );
		returnSlicePositions.push_back( cycleStart );
	}
}

void PeriodExtractor::MedianFilterPitches(std::vector<float>& periods)
{
	const int medianFilterWidth = 10;
	vector<float> filter;
	filter.resize(medianFilterWidth);

	for (int slot = 0; slot < periods.size(); ++slot)
	{
		int start = std::max(0, slot - medianFilterWidth / 2);
		if (start < periods.size() - medianFilterWidth)
		{
			for (int i = 0; i < medianFilterWidth; ++i)
			{
				filter[i] = periods[start + i];
			}
			std::sort(filter.begin(), filter.end());
		}
		float median = filter[medianFilterWidth / 2];
		if (periods[slot] / median > 1.3f || periods[slot] / median < 0.6f) // try to filter out octave errors.
		{
			periods[slot] = median;
		}
	}
}


void SliceAndDice(vector<float>& Wavefile, WaveTable* waveTable, int waveTablenumber, int method, int diagnosticPitchDetectType, float* rawPitchEstimates, int sampleRate)
{
	WaveTable::NormalizeWave( Wavefile );

	int slots = waveTable->slotCount;
	int wavesize = waveTable->waveSize;

	vector<int> slicePositions;

	WaveTable::SliceAndDiceGetSlicePositions( Wavefile, slots, slicePositions );

	PeriodExtractor pitchDetect;

	vector<float> periods;
	periods.resize(slots);

    for (int slot = 0; slot < slots; ++slot)
    {
		int cycleStart = slicePositions[slot];

		switch( method )
		{
		case 0:
			periods[slot] = WaveTable::ExtractPeriod( Wavefile, cycleStart, slot );
			break;

		case 1:
			{
			float* diagnosticOutput = 0;
			float* diagnosticProbeOutput = 0;
#if defined( _DEBUG) && defined (_WIN32)
			diagnosticOutput = WaveTable::diagnosticOutput;
			diagnosticProbeOutput =  WaveTable::diagnosticProbeOutput;
#endif

			periods[slot] = pitchDetect.ExtractPeriod2( &(Wavefile[0]), Wavefile.size(), cycleStart, diagnosticOutput, diagnosticProbeOutput );

#if 0 // defined( _DEBUG) && defined (_WIN32)
			if( WaveTable::diagnosticOutput )
			{
				int count = Wavefile.size() / 2;
				float* wave = Wavefile.data();

				const int n = 2048; // all copied from Period Extracotr, keep in sync.
				int bins = n / 2;

				wofstream myfile;
				myfile.open( L"c:\\temp\\pitchdetectiondetail3.txt" );

				myfile << L"Sample, Hz, FFT, Hz, PitchDetect, Hz, Window \n";
				const float fftSize = 2048;
				float Hz = sampleRate / periods[slot];
				double detectedPeriodBin = ( Hz * n / (float) sampleRate );
				for( int j = 1; j < bins; ++j )
				{
					float HzFft = ( sampleRate * j ) / fftSize;
					// probe.
					//double freqBin = 2.0 * pow( 2.0, minOctave + maxOctave * j / (double) bins );
					double freqBin = PeriodExtractor::ResultBinToFftBin( j, bins );
					float HzProbe = ( sampleRate * freqBin ) / fftSize;
					myfile << wave[j] << L", " << HzFft << L", " << WaveTable::diagnosticOutput[j] << L", " << HzProbe << L", " << WaveTable::diagnosticProbeOutput[j] << L", " << HzProbe << L", " << PeriodExtractor::calcProbeFunction( freqBin, detectedPeriodBin );
					//_RPT0( _CRT_WARN, "\n" );
					myfile << L"\n";
				}
				//_RPT2( _CRT_WARN, "Best Bin %d Period %f\n", bestPeriod, returnValue );

				myfile.close();
			}
#endif
		}
			break;
		}
				//		_RPT1(_CRT_WARN, "%f\n", periods[slot] );
	}
//	_RPT0(_CRT_WARN, "\n" );

	if (rawPitchEstimates && diagnosticPitchDetectType == 0 )
	{
		for( int slot = 0; slot < slots; ++slot )
		{
			rawPitchEstimates[slot] = periods[slot];
		}
	}

	// weed out glitches.
	PeriodExtractor::MedianFilterPitches(periods);

	if (rawPitchEstimates && diagnosticPitchDetectType == 1)
	{
		for (int slot = 0; slot < slots; ++slot)
		{
			rawPitchEstimates[slot] = periods[slot];
		}
	}

	for (int slot = 0; slot < slots; ++slot)
    {
		int cycleStart = slicePositions[slot];
		float period = periods[slot];

		// BETTER: downhill crawl using integer steps, then switch to fractional.
        double closest = FLT_MAX; // numeric_limits<double>::max( );
		float best = 0.0f;
		// Find precise loop-point 2-cycles forward.
		const int searchRangeCourse = 10;
		for( int f = -searchRangeCourse ; f < searchRangeCourse ; ++f )
		{
			float r = AutoCorrelate( Wavefile, cycleStart, f + (int)period, (int) period );
//			_RPT2(_CRT_WARN, "%d, %f\n", f, r );
			if( r < closest )
			{
				closest = r;
				best = (float)f;			
			}
		}

		// fine-tune. Goldren search.
		{
			float bx = best;
			float ax = bx - 1.0f;
			float cx = bx + 1.0f;
			float tol = 0.05f;
			float tol2 = 0.01f;
			float *xmin = &best;
			/*
			float golden(float ax, float bx, float cx, float (*f)(float), float tol, float *xmin)
			Given a function f, and given a bracketing triplet of abscissas ax, bx, cx (such that bx is
			between ax and cx, and f(bx) is less than both f(ax) and f(cx)), this routine performs a
			golden section search for the minimum, isolating it to a fractional precision of about tol. The
			abscissa of the minimum is returned as xmin, and the minimum function value is returned as
			golden, the returned function value.
			*/
			float f1,f2,x0,x1,x2,x3;
			x0=ax; // At any given time we will keep track of four
			x3=cx; // points, x0,x1,x2,x3.
			if (fabs(cx-bx) > fabs(bx-ax)) { //Make x0 to x1 the smaller segment,
				x1=bx;
				x2=bx+C*(cx-bx); //and ll in the new point to be tried.
			} else {
				x2=bx;
				x1=bx-C*(bx-ax);
			}
			//The initial function evaluations. Note that
			// we never need to evaluate the function
			// at the original endpoints.
			//f1=(*f)(x1);
			f1 = AutoCorrelate( Wavefile, cycleStart, x1 + (int)period, (int) period );
			//f2=(*f)(x2);
			f2 = AutoCorrelate( Wavefile, cycleStart, x2 + (int)period, (int) period );

			// while (fabs(x3 - x0) > tol*(fabs(x1) + fabs(x2))) { // original, can get stuck or slow.
			while( fabs(x3 - x0) > tol2 )
			{
				if (f2 < f1) { // One possible outcome,
					SHFT3(x0,x1,x2,R*x1+C*x3) // its housekeeping,
					//SHFT2(f1,f2,(*f)(x2)) // and a new function evaluation.
					SHFT2(f1,f2,AutoCorrelateFractional( Wavefile, cycleStart, x2 + period, (int) period )) // and a new function evaluation.
				} else { // The other outcome,
					SHFT3(x3,x2,x1,R*x2+C*x0)
					//SHFT2(f2,f1,(*f)(x1)) // and its new function evaluation.
					SHFT2(f2,f1,AutoCorrelateFractional( Wavefile, cycleStart, x1 + period, (int) period )) // and its new function evaluation.
				}
//				_RPT0(_CRT_WARN, "*" );
			} // Back to see if we are done.
			if (f1 < f2) { // We are done. Output the best of the two
				*xmin=x1; // current values.
				closest = f1;
			} else {
				*xmin=x2;
				closest = f2;
			}
//			_RPT2(_CRT_WARN, "\nALT: %f, %f\n", closest, *xmin );
		}

/* brute-force
		const float searchStep = 0.05f;
		const float searchRange = 1.0f - searchStep;
		for( float f = -searchRange ; f < searchRange ; f += searchStep )
		{
			float r = AutoCorrelateFractional( Wavefile, cycleStart, f + period, (int) period );
			_RPT2(_CRT_WARN, "%f, %f\n", f, r );
			if( r < closest )
			{
				closest = r;
				best = f;			
			}
		}
*/
//		_RPT2(_CRT_WARN, "PERIOD Adjust, %f, %f\n", period, best );
		float precisePeriod = period + best;

		if (rawPitchEstimates && diagnosticPitchDetectType == 2)
		{
			rawPitchEstimates[slot] = precisePeriod;
		}

		// Figure out where to put slot waveform.
		int destSlot = slot; // loaded in order
		float* dest = waveTable->Wavedata + (waveTablenumber * waveTable->slotCount + destSlot) * waveTable->waveSize;

        // Copy single cycle to wavetable slot with interpolation.
        //                Debug.WriteLine("Slot " + slot + ". error " + closest);
        float filtering = min( 0.5f, 0.5f * wavesize / precisePeriod );
		vector<float> slotsamples;
		slotsamples.resize(wavesize);
        for (int s = 0; s < wavesize; ++s)
        {
            float xfade = (float)s / (float)wavesize;
            float i = precisePeriod * (float)s / (float)wavesize;
            const int sincSize = 20;
			float x1 = i;
			float x2 = x1 + precisePeriod;

            float s1 = resamp(cycleStart + x1, Wavefile, filtering, 1.0f, sincSize);
            float s2 = resamp(cycleStart + x2, Wavefile, filtering, 1.0f, sincSize);

//			dest[s] = xfade * s1 + (1.0 - xfade) * s2;
//			slotsamples[s] = xfade * s1 + (1.0 - xfade) * s2;
			slotsamples[s] = s2 + xfade * (s1 - s2);
        }

		#if defined (FIX_ZERO_CROSSINGS)
			assert(wavesize == 512);
			float realData[513];

			{
				// Copy wave to temp array for FFT.
				for (int s = 0; s < wavesize; s++)
				{
					realData[s + 1] = slotsamples[s];
				}

				// Perform forward FFT.
				realft(realData, wavesize, 1);

				float scale = 2.0f / wavesize;

				// Perform reverse FFT.
				for (int s = 1; s < wavesize / 2; s++)
				{
					double magnitudeR = realData[s * 2 + 1] * scale; // FFT is off-by one (FORTRAN code)
					double magnitudeI = realData[s * 2 + 2] * scale;
					double totalMagnitude = sqrtf(magnitudeR * magnitudeR + magnitudeI * magnitudeI); // combine real and imaginary.

					// Put all energy back into imaginary part, therby setting phase to zero for all partials.
					realData[s * 2 + 1] = 0;
					realData[s * 2 + 2] = (float) totalMagnitude;
				}
				// Nyquist and DC not wanted.
				realData[1] = 0.0F; // DC.
				realData[2] = 0.0F; // nyquist.
/*
				for (int i = 0; i < wavesize; ++i)
				{
					realData[i + 1] = 0;
				}
				realData[2] = slot / (float) SlotCount;
*/
				realft(realData, wavesize, -1);

				// Copy back to wave.
				for (int s = 0; s < wavesize; s++)
				{
					slotsamples[s] = realData[s + 1];
				}
			}
		#endif

		for (int s = 0; s < wavesize; ++s)
		{
			dest[s] = slotsamples[s];
		}
    }


	// Normalise energy across slots.
	vector<float> slotRms;
	slotRms.assign(slots, 0.0f);
	float maximumRms = 0.0;
	float maximumAmp = 0.0;

	// Calc RMS for each slot. Also determin biggest Amplitude slot, and it's RMS. It will be the 
    for (int slot = 0; slot < slots; ++slot)
    {
		slotRms[slot] = 0.0f;
		float* src = waveTable->Wavedata + (waveTablenumber * waveTable->slotCount + slot) * waveTable->waveSize;
		float m = 0.0f;
		for (int s = 0; s < wavesize; ++s)
		{
			if( fabsf(*src) > m )
			{
				m = fabsf(*src);
			}
			float f = *src++;
			slotRms[slot] += f*f; // squared.
		}
		slotRms[slot] = sqrtf(slotRms[slot] / wavesize); // rootmean.
		if( m > maximumAmp )
		{
			maximumAmp = m;
			maximumRms = slotRms[slot];
		}
	}

	// Smooth gains to avoid glitches.
	{
		int n = slots;
		float cuttoff = 0.08f;
		float l = exp(-M_PI * 2 * cuttoff);

		// filter forward discarding results from near end to get a rough guess of shape at end of graph. This will be our filter's initial value.
		int s = (3 * n) / 4;
		float y1n = slotRms[s];
		for (; s < n; s++)
		{
			// low pass
			float xn = slotRms[s];
			y1n = xn + l * (y1n - xn);
		}

		// Filter backward.
		for (int s = n - 1; s >= 0; s--)
		{
			float xn = slotRms[s];
			// low pass
			slotRms[s] = y1n = xn + l * (y1n - xn);
		}

		// Filter foreward.
		for (int s = 0; s < n; s++)
		{
			float xn = slotRms[s];
			// low pass
			slotRms[s] = y1n = xn + l * (y1n - xn);
		}
	}

	// normalise accross slots.
//	float threshhold = 0.01f;
	float targetRms = 0.5f * maximumRms / maximumAmp; // loudest cycle will be normalise at +/- 0.5
	float maxGain = 8.0f; // prevent blowout of quiet cycles.
    for (int slot = 0; slot < slots; ++slot)
    {
		float* src = waveTable->Wavedata + (waveTablenumber * waveTable->slotCount + slot) * waveTable->waveSize;
		float gain = targetRms / slotRms[slot];
		gain = (std::min)(gain, maxGain);
		//if( slotRms[slot] > threshhold )
		{
			//_RPT3(_CRT_WARN, "Slot %d: RMS %f, gain %f\n", slot, slotRms[slot], gain  );
			for (int s = 0; s < wavesize; ++s)
			{
				*src++ *= gain;
			}
		}
		if (rawPitchEstimates && diagnosticPitchDetectType == 3)
		{
			rawPitchEstimates[slot] = gain;
		}
	}

#if defined( _DEBUG )
//	_RPT0(_CRT_WARN, "normalised\n" );
    for (int slot = 0; slot < slots; ++slot)
    {
		slotRms[slot] = 0.0f;
		float* src = waveTable->Wavedata + (waveTablenumber * waveTable->slotCount + slot) * waveTable->waveSize;
		for (int s = 0; s < wavesize; ++s)
		{
			float f = *src++;
			slotRms[slot] += f*f; // squared.
		}
		slotRms[slot] = sqrtf(slotRms[slot] / wavesize); // root-mean.
//		_RPT2(_CRT_WARN, "Slot %d: RMS %f\n", slot, slotRms[slot]  );
	}
#endif
}

bool WaveTable::LoadFile3( const _TCHAR* filename, bool fileIsWavetable, int wavetableNumber ) // Load single wavetable.
{
    return LoadFile2(0, filename, fileIsWavetable, wavetableNumber, true );
}

bool WaveTable::LoadWaveFile( const _TCHAR* filename, std::vector<float> &returnSamples, int& returnSampleRate )
{
	ifstream myfile;
	myfile.open( filename, ios_base::in | ios_base::binary );
	if( !myfile )
		return false;

	MYWAVEFORMATEX waveheader;
	char* wave_data = 0;
	unsigned int wave_data_bytes = 0;

	memset( &waveheader, 0, sizeof( waveheader ) );

    char chunkName[4];
    int chunkLength;

	myfile.read( (char*) &chunkName, 4 );
	myfile.read( (char*) &chunkLength, 4 );

	if( chunkName[0] == 'R' && chunkName[1] == 'I' && chunkName[2] == 'F' && chunkName[3] == 'F' ) // RIFF.
	{
		myfile.read( (char*) &chunkName, 4 );

        if( chunkName[0] == 'W' && chunkName[1] == 'A' && chunkName[2] == 'V' && chunkName[3] == 'E' ) // "WAVE"
		{
		    myfile.read( (char*) &chunkName, 4 );
		    myfile.read( (char*) &chunkLength, 4 );

            while(!myfile.eof())
            {
   			    if( chunkName[0] == 'f' && chunkName[1] == 'm' && chunkName[2] == 't' && chunkName[3] == ' ' ) //  "fmt "
                {
			        myfile.read( (char*) &waveheader, ( std::min )( (size_t) chunkLength, sizeof( waveheader ) ) );

			        if( chunkLength > sizeof( waveheader ) )
			        {
				        myfile.ignore( chunkLength - sizeof( waveheader ) );
			        }
                }
                else
                {
   			        if( chunkName[0] == 'd' && chunkName[1] == 'a' && chunkName[2] == 't' && chunkName[3] == 'a' ) //  "data"
                    {
			            wave_data_bytes = chunkLength;
			            wave_data = new char[wave_data_bytes];
			            myfile.read( wave_data, wave_data_bytes );
                    }
                    else
                    {
			            // Next chunk.
				        myfile.ignore( chunkLength );
                    }
                }

		        myfile.read( (char*) &chunkName, 4 );
		        myfile.read( (char*) &chunkLength, 4 );
            }
		}
	}

    if( wave_data == 0 || waveheader.wBitsPerSample == 0) // no data or header found.
    {
		return false;
	}

	int SampleCount = wave_data_bytes / waveheader.nBlockAlign;

	returnSamples.resize( SampleCount );

	switch( waveheader.wFormatTag )
	{
	case WAVE_FORMAT_PCM:
		switch( waveheader.wBitsPerSample )
		{
		case 8:
		{
			const float CharToFloatMul = 1.f / 256.f;
			int j = 0;
			for( int i = 0; i < SampleCount; ++i )
			{
				//wave[i] = CharToFloatMul * ((char*)wave_data)[j];
				//j += waveheader.nChannels;
				returnSamples[i] = 0.0f;
				for( int c = 0; c < waveheader.nChannels; ++c )
				{
					returnSamples[i] += CharToFloatMul * ( ( (unsigned char*) wave_data )[j] - 128.0f );
					++j;
				}
			}
		}
			break;

		case 16:
		{
			const float ShortToFloatMul = 1.f / 32768.f;
			int j = 0;
			for( int i = 0; i < SampleCount; ++i )
			{
				//returnSamples[i] = ShortToFloatMul * ((short*)wave_data)[j];
				//j += waveheader.nChannels;
				returnSamples[i] = 0.0f;
				for( int c = 0; c < waveheader.nChannels; ++c )
				{
					returnSamples[i] += ShortToFloatMul * ( (short*) wave_data )[j];
					++j;
				}
			}

		}
			break;

		case 24:
		{
			const float IntToFloatMul = 1.f / 4294967296.0f; // (float) 0x0100000000;
			int j = 0;
			unsigned char* src = (unsigned char*) wave_data;
			for( int i = 0; i < SampleCount; ++i )
			{
				returnSamples[i] = 0.0f;
				for( int c = 0; c < waveheader.nChannels; ++c )
				{
					int intSample = ( src[0] << 8 ) + ( src[1] << 16 ) + ( src[2] << 24 );
					returnSamples[i] += IntToFloatMul * intSample;
					src += 3;
				}
			}

		}
			break;

		case 32:
		{
			const float IntToFloatMul = 1.f / 4294967296.0f; // (float) 0x0100000000;
			int j = 0;
			for( int i = 0; i < SampleCount; ++i )
			{
				returnSamples[i] = 0.0f;
				for( int c = 0; c < waveheader.nChannels; ++c )
				{
					returnSamples[i] += IntToFloatMul * ( (int*) wave_data )[j];
					++j;
				}
			}
		}
			break;

		default:
		{
			//					message(L"This WAVE file format is not supported.  Convert it to 16 bit uncompressed mono or stereo.");
		}

		}
		break;

	case 03: // WAVE_FORMAT_IEEE_FLOAT:
		if( waveheader.wBitsPerSample == 32 )
		{
			int j = 0;
			for( int i = 0; i < SampleCount; ++i )
			{
				//returnSamples[i] = ((float*)wave_data)[j];
				//j += waveheader.nChannels;
				returnSamples[i] = 0.0f;
				for( int c = 0; c < waveheader.nChannels; ++c )
				{
					returnSamples[i] += ( (float*) wave_data )[j];
					++j;
				}
			}
		}

		break;

	default:
		//		message(L"This WAVE file format is not supported.  Convert it to PCM or Float.");
		;
	};

	delete[] wave_data;

	returnSampleRate = waveheader.nSamplesPerSec;

	return true;
}

bool WaveTable::LoadFile2(int selectedFromSlot, const _TCHAR* filename, bool fileIsWavetable, int waveTablenumber, bool entireTable, int method, int diagnosticPitchDetectType, float* rawPitchEstimates)
{
    assert( this->waveSize > 0 && this->waveSize < 5000 );
	assert( this->slotCount > 0 && this->slotCount < 500 );
	assert( this->waveTableCount > 0 && this->waveTableCount < 500 );

    assert( selectedFromSlot >= 0 && selectedFromSlot < slotCount );

	int sampleRate;
	vector<float> wave;

	if( false == LoadWaveFile( filename, wave, sampleRate ) || wave.empty() )
    {
        return false;
    }

	int SampleCount = wave.size();

	if( entireTable )
	{
		// If imported file happens to be exactly the right size to fill the table. We'll assume it's already sliced and diced.
		int FullTableSamples = slotCount * waveSize;
		if( SampleCount == FullTableSamples && fileIsWavetable )
		{
			float* dest = Wavedata + waveTablenumber * waveSize * slotCount;
			for( int i = 0 ; i < SampleCount ; ++i )
			{
				dest[i] = wave[i];
			}
		}
		else
		{
			SliceAndDice(wave, this, waveTablenumber, method, diagnosticPitchDetectType, rawPitchEstimates, sampleRate);
		}
	}
	else // 1 slot
	{
		int samples = std::min( waveSize, SampleCount );
		NormalizeWave( wave );
		float* dest = Wavedata + (waveTablenumber * slotCount + selectedFromSlot) * waveSize;
		for( int i = 0 ; i < samples ; ++i )
		{
			dest[i] = wave[i];
		}
	}

	return true;
}

/*

#ifdef UNICODE
        wstring filename = ofn.lpstrFile;
#else
        wstring filename;// = Utf8ToWstring(ofn.lpstrFile);
        //hack...
        {
            const char* in = ofn.lpstrFile;
            std::wstring out;
            unsigned int codepoint = 0;
            int following = 0;
            for (in;  *in != 0;  ++in)
            {
                unsigned char ch = *in;
                if (ch <= 0x7f)
                {
                    codepoint = ch;
                    following = 0;
                }
                else if (ch <= 0xbf)
                {
                    if (following > 0)
                    {
                        codepoint = (codepoint << 6) | (ch & 0x3f);
                        --following;
                    }
                }
                else if (ch <= 0xdf)
                {
                    codepoint = ch & 0x1f;
                    following = 1;
                }
                else if (ch <= 0xef)
                {
                    codepoint = ch & 0x0f;
                    following = 2;
                }
                else
                {
                    codepoint = ch & 0x07;
                    following = 3;
                }
                if (following == 0)
                {
                    if (codepoint > 0xffff)
                    {
                        out.append(1, static_cast<wchar_t>(0xd800 + (codepoint >> 10)));
                        out.append(1, static_cast<wchar_t>(0xdc00 + (codepoint & 0x03ff)));
                    }
                    else
                        out.append(1, static_cast<wchar_t>(codepoint));
                    codepoint = 0;
                }
            }
            filename = out;
        }
#endif

*/

	/*
void WaveTable::LoadFile( const platform_string& filename, int wavetableNumber, int selectedFromSlot, bool entireTable )
{
#if !defined( _DEBUG )
//	loadingMode = true;
#endif
	WaveTable* waveTable = this;

	_TCHAR szFile[512] = _T("wavetable.wav");
    LPTSTR filter = _T("Wavefiles\0*.WAV\0All\0*.*\0");

	OPENFILENAME ofn;
	memset(&ofn,0,sizeof(ofn));

	ofn.lStructSize = sizeof ( ofn );
	ofn.hwndOwner = NULL  ;
	ofn.lpstrFile = szFile ;
	ofn.lpstrFile[0] = '\0';
	ofn.nMaxFile = sizeof( szFile );
	ofn.lpstrFilter = filter; //L"Wavefiles\0*.WAV\0All\0*.*\0";
	ofn.nFilterIndex =1;
	ofn.lpstrFileTitle = NULL;
	ofn.nMaxFileTitle = 0;
	ofn.lpstrInitialDir=NULL ;
	ofn.Flags = OFN_PATHMUSTEXIST|OFN_FILEMUSTEXIST ;

	if( entireTable )
	{
		ofn.Flags |= (OFN_ALLOWMULTISELECT|OFN_EXPLORER);
	}

	BOOL r = GetOpenFileName( &ofn );

	if( r )
	{
//		if( (ofn.Flags & OFN_ALLOWMULTISELECT) == 0 )
		{
//			platform_string filename( ofn.lpstrFile );
			LoadFile2( selectedFromSlot, filename.c_str(), wavetableNumber, entireTable );
		}

		else
		{
			LPTSTR p = ofn.lpstrFile;
			while(*p != 0 )
			{
				++p;
			}
			++p;
			if( *p == 0 ) // only one filename
			{
				platform_string filename( ofn.lpstrFile );
				LoadFile2( selectedFromSlot, filename.c_str(), wavetableNumber, entireTable );
			}
			else // multiple selections.
			{
				platform_string foldername( ofn.lpstrFile );

				foldername += _T("\\");
				while( *p != 0 && wavetableNumber < waveTable->waveTableCount )
				{
					platform_string fullfilename = foldername + p;
					LoadFile2( selectedFromSlot, fullfilename.c_str(), wavetableNumber, entireTable );
					++wavetableNumber;

					while(*p != 0 )
					{
						++p;
					}
					++p;
				}
			}
		}
	}
}
		*/


struct wave_file_header
{
	char chnk1_name[4];
	int32_t chnk1_size;
	char chnk2_name[4];
	char chnk3_name[4];
	int32_t chnk3_size;
	uint16_t wFormatTag;
	uint16_t nChannels;
	int32_t nSamplesPerSec;
	int32_t nAvgBytesPerSec;
	uint16_t nBlockAlign;
	uint16_t wBitsPerSample;
	char chnk4_name[4];
	int32_t chnk4_size;
};

void WaveTable::ExportFile( const std::wstring& pfilename, int wavetableNumber, int selectedFromSlot, bool entireTable )
{
    std::wstring filename = pfilename;

	WaveTable* waveTable = this;
/*
	_TCHAR szFile[512] = _T("wavetable.wav");
    LPTSTR filter = _T("Wavefiles\0*.WAV\0All\0*.*\0");

	OPENFILENAME ofn;
	memset(&ofn,0,sizeof(ofn));

	ofn.lStructSize = sizeof ( ofn );
	ofn.hwndOwner = NULL ;
	ofn.lpstrFile = szFile ;
	ofn.nMaxFile = sizeof( szFile );
	ofn.lpstrFilter = filter;
	ofn.nFilterIndex =1;
	ofn.lpstrFileTitle = NULL ;
	ofn.nMaxFileTitle = 0 ;
	ofn.lpstrInitialDir=NULL ;
	ofn.Flags = OFN_PATHMUSTEXIST|OFN_FILEMUSTEXIST ;

	BOOL r = GetSaveFileName( &ofn );

	platform_string filename(ofn.lpstrFile);

	if( r )
*/
	{
		int WAVE_FORMAT_IEEE_FLOAT = 0x0003;  /*  Microsoft Corporation  */
		bool floatFormat = true; // false for 16-bit PCM
		int bits_per_sample = 32;
		int n_channels = 1;
		int sample_count = 0;
		int sample_rate = 44100;
		float* src = 0;

		if( entireTable )
		{
			sample_count = waveTable->slotCount * waveTable->waveSize;
			src = waveTable->Wavedata + wavetableNumber * waveTable->waveSize * waveTable->slotCount;
		}
		else // 1 slot
		{
			sample_count = waveTable->waveSize;
			src = waveTable->Wavedata + (wavetableNumber * waveTable->slotCount + selectedFromSlot) * waveTable->waveSize;
		}


		wave_file_header wav_head;
		memcpy(wav_head.chnk1_name,"RIFF",4);
		memcpy(wav_head.chnk2_name,"WAVE",4);
		memcpy(wav_head.chnk3_name,"fmt ",4);
		memcpy(wav_head.chnk4_name,"data",4);

		if( floatFormat )
		{
			wav_head.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
			bits_per_sample = 32;
		}
		else
		{
			wav_head.wFormatTag = WAVE_FORMAT_PCM;
			bits_per_sample = 16;
		}

		wav_head.wBitsPerSample = bits_per_sample;
		wav_head.chnk3_size = 16;
		wav_head.nChannels = n_channels;
		wav_head.chnk4_size = (int32_t) (sample_count * wav_head.wBitsPerSample / 8 * wav_head.nChannels);
		wav_head.chnk1_size = wav_head.chnk4_size + 36;
		wav_head.nSamplesPerSec = sample_rate;
		wav_head.nAvgBytesPerSec = wav_head.nSamplesPerSec * wav_head.nChannels * wav_head.wBitsPerSample / 8;
		wav_head.nBlockAlign = (wav_head.wBitsPerSample / 8) * wav_head.nChannels;

		ofstream myfile;
		myfile.open ( filename.c_str(), ios_base::out | ios_base::binary | ios_base::trunc );
		if( !myfile )
		{
			#if defined( _WIN32 ) && !defined(SE_TARGET_WAVES)
				wstring errormsg = L"Can't save: " + filename;
				MessageBox( NULL,  errormsg.c_str(), (LPCWSTR)L"File save error", MB_OK );
			#endif
		}

		myfile.write( (char*)&wav_head, 44 );
		if( floatFormat )
		{
			myfile.write( (char*)src, sizeof(float) * sample_count );
		}
		else
		{
			for( int i = 0 ; i < sample_count; ++i )
			{
				int16_t s = (int) (0.5f + src[i] * (float) 0x10000);
				myfile.write( (char*)&s, sizeof(s) );
			}
		}

		myfile.close();
	}
}

void WaveTable::GenerateWavetable( int wavetableNumber, int selectedFromSlot, int selectedToSlot, int shape )
{
    assert( wavetableNumber >= 0 && wavetableNumber < waveTableCount );
    assert( selectedFromSlot >= 0 && selectedFromSlot < slotCount );
    assert( selectedToSlot >= 0 && selectedToSlot < slotCount );

	WaveTable* waveTable = this;
	vector<float> harmonics;

	switch( shape )
	{
	case 7: // Noise.
		{
			for( int slot = selectedFromSlot ; slot <= selectedToSlot ; ++slot )
			{
				// White noise
				float* dest = waveTable->Wavedata + (wavetableNumber * waveTable->slotCount + slot) * waveTable->waveSize;
				for( int i = 0 ; i < waveTable->waveSize ; ++i )
				{
					dest[i] = rand() / (float) RAND_MAX - 0.5f;
				}
			}
		}
		break;

	case 8: // Random.
		{
			/*
			// White noise
			float* dest = waveTable->Wavedata + (wavetableNumber * waveTable->slotCount + selectedSlot) * waveTable->waveSize;
			for( int i = 0 ; i < waveTable->waveSize ; ++i )
			{
				dest[i] = rand() / (float) RAND_MAX - 0.5f;
			}
			*/

			// Random harmonics.
			int totalHarmonics = waveTable->waveSize / 2;
			harmonics.resize(totalHarmonics);
			int mask = rand() & 0x3;
			mask = max(mask,1);
			int mask2 = rand() & 0x1;
			float falloff = rand() / (float) RAND_MAX ;
			for( int partial = 1 ; partial < totalHarmonics ; ++partial )
			{
				float level = rand() / (float) RAND_MAX - 0.5f;
				level = level / (partial * falloff); // pinkify it.
				if( ((partial & mask) == 0) == mask2 ) // mix up even/odd harmonics.
				{
					level = 0.0f;
				}
				harmonics[partial] = level;
			}
		}
		break;
	case 9: // Silence.
		{
			harmonics.resize(2); // entry 0 not used.
			harmonics[1] = 0.0f;
		}
		break;
	case 10: // DC.
		{
			for( int slot = selectedFromSlot ; slot <= selectedToSlot ; ++slot )
			{
				float* dest = waveTable->Wavedata + (wavetableNumber * waveTable->slotCount + slot) * waveTable->waveSize;
				for( int i = 0 ; i < waveTable->waveSize ; ++i )
				{
					dest[i] = 1.0f;
				}
			}
		}
		break;
	case 2: // Sine.
		{
			harmonics.resize(2); // entry 0 not used.
			harmonics[1] = 1.0f;
		}
		break;
	case 0: // Saw.
	case 4: // Pulse 15%.
	case 5: // Pulse 50%.
	case 6: // Pulse 85%.
		{
			int totalHarmonics = waveTable->waveSize / 2;
			harmonics.resize(totalHarmonics);
			for( int partial = 1 ; partial < totalHarmonics ; ++partial )
			{
				float level = -1.0f / partial;
				harmonics[partial] = level;
			}
		}
		break;
	case 1: // Ramp.
		{
			int totalHarmonics = waveTable->waveSize / 2;
			harmonics.resize(totalHarmonics);
			for( int partial = 1 ; partial < totalHarmonics ; ++partial )
			{
				float level = 1.0f / partial;
				harmonics[partial] = level;
			}
		}
		break;
	case 3: // Triangle.
		{
			int totalHarmonics = waveTable->waveSize / 2;
			harmonics.resize(totalHarmonics);
			for( int partial = 1 ; partial < totalHarmonics ; ++partial )
			{
				float level = 1.0f / (partial * partial);
				if( (partial & 0x01) == 0 )
				{
					level = 0.0f;
				}

				if( (partial>>1) & 1 ) // every 2nd harmonic inverted
				{
					level = -level;
				}

				harmonics[partial] = level;
			}
		}
		break;
	}

	if( harmonics.size() > 0 ) //shape != 7 ) // noise 
	{
		int totalHarmonics = harmonics.size();

		// windowing function to reduce gibbs phenomena (hamming). Reduced effectivness once mip-mapp truncates series anyhow.
		for( int partial = 1 ; partial < totalHarmonics ; ++partial )
		{
			float window = 0.5f + 0.5f * cosf( (partial - 1.f) * (float)M_PI / (float) totalHarmonics );
			harmonics[partial] *= window;
		}

		float maximum = 0.0f;
		for( int slot = selectedFromSlot ; slot <= selectedToSlot ; ++slot )
		{
			float* dest = waveTable->Wavedata + (wavetableNumber * waveTable->slotCount + slot) * waveTable->waveSize;
			for( int i = 0 ; i < waveTable->waveSize ; ++i )
			{
				// Sum harmonics.
				float s = 0.0f;
				for( int partial = 1 ; partial < totalHarmonics ; ++partial )
				{
					s += harmonics[partial] * sinf( (float) partial * 2.0f * (float)M_PI * (float) i / (float) waveTable->waveSize );
				}
				dest[i] = s;
				maximum = std::max(maximum,s);
			}

			// Pulse (adds two saws).
			switch( shape )
			{
			case 4: // Pulse 15%.
			case 5: // Pulse 50%.
			case 6: // Pulse 85%.
				{
					vector<float> inverseSaw;
					inverseSaw.resize(waveTable->waveSize);
					for( int i = 0 ; i < waveTable->waveSize ; ++i )
					{
						inverseSaw[i] = dest[i];
					}

					int offset = waveTable->waveSize / 2;
					switch( shape )
					{
					case 4: // Pulse 15%.
						offset = (15 * waveTable->waveSize) / 100;
						break;
					case 5: // Pulse 50%.
						offset = waveTable->waveSize / 2;
						break;
					case 6: // Pulse 85%.
						offset = (85 * waveTable->waveSize) / 100;
						break;
					};

					for( int i = 0 ; i < waveTable->waveSize ; ++i )
					{
						dest[i] -= inverseSaw[(i + offset) % waveTable->waveSize];
					}
				}
				break;
			default:
				break;
			}

			// Normalise.
			if( maximum > 0.000000001f ) // avoid divide by zero on silence.
			{
				float scale = 0.5f / maximum;
				for( int i = 0 ; i < waveTable->waveSize ; ++i )
				{
					dest[i] *= scale;
				}
			}
		}
	}

	
	/* old
	case GW_ANALOG_WAVES:
		{
			float* dest;
			dest = waveTable->Wavedata + (wavetableNumber * waveTable->slotCount) * waveTable->waveSize;
			// clear out existing.
			for( int i = 0 ; i < waveTable->waveSize * waveTable->slotCount ; ++ i )
			{
				dest[i] = 0.0f;
			}

			int waveNumber = -1;

			// Sine.
			waveNumber++;
			dest = waveTable->Wavedata + (wavetableNumber * waveTable->slotCount + waveNumber) * waveTable->waveSize;
			for( int i = 0 ; i < waveTable->waveSize ; ++i )
			{
				dest[i] = 0.5f * sinf( 2.0f * (float)M_PI * (float) i / (float) waveTable->waveSize );
			}

			// triangle.
			waveNumber++;;
			dest = waveTable->Wavedata + (wavetableNumber * waveTable->slotCount + waveNumber) * waveTable->waveSize;
			for( int i = 0 ; i < waveTable->waveSize ; ++i )
			{
				float s = -2.f * (float) i / (float) waveTable->waveSize + 1.0f;
				if( s > 0.5f )
					s = -s + 1.0f;
				if( s < -0.5f )
					s = -s - 1.0f;

				dest[i] = s;
//_RPT1(_CRT_WARN, "%f\n", s );
			}

			// Square.
			waveNumber++;
			dest = waveTable->Wavedata + (wavetableNumber * waveTable->slotCount + waveNumber) * waveTable->waveSize;
			for( int i = 0 ; i < waveTable->waveSize ; ++i )
			{
				dest[i] = i < (waveTable->waveSize/2) ? 0.5f : -0.5f;
			}

			// Sawtooth.
			waveNumber++;
			dest = waveTable->Wavedata + (wavetableNumber * waveTable->slotCount + waveNumber) * waveTable->waveSize;
			for( int i = 0 ; i < waveTable->waveSize ; ++i )
			{
				dest[i] = -((float) i / (float) waveTable->waveSize - 0.5f);
			}
			invalidateRect();

			// Update patchmem and DSP.
			pinWaveBank.sendPinUpdate();
	}
	break;

	case GW_PULSE_WIDTH_MODULATION:
		{
			float* dest;
			
			for( int waveNumber = 0 ; waveNumber < waveTable->slotCount ; ++waveNumber )
			{
				float pw = (float)waveNumber / (float)waveTable->slotCount;
				int pulsewidth = (waveNumber * waveTable->waveSize) / waveTable->slotCount;
				// Pulse.
				dest = waveTable->Wavedata + (wavetableNumber * waveTable->slotCount + waveNumber) * waveTable->waveSize;
				for( int i = 0 ; i < waveTable->waveSize ; ++i )
				{
					dest[i] = i > pulsewidth ? pw - 1.0f : pw;
				}
			}
		}
		break;

	case GW_SAW_SYNC_SWEEP:
		{
			for( int waveNumber = 0 ; waveNumber < waveTable->slotCount ; ++waveNumber )
			{
				int pulsewidth = waveTable->waveSize - (waveNumber * waveTable->waveSize) / waveTable->slotCount;

				float* dest = waveTable->Wavedata + (wavetableNumber * waveTable->slotCount + waveNumber) * waveTable->waveSize;
				int j = 0;
				for( int i = 0 ; i < waveTable->waveSize ; ++i )
				{
					dest[i] = -((float) j++ / (float) waveTable->waveSize - 0.5f);
					if( j > pulsewidth )
					{
						j = 0;
					}
				}
			}
		}
		break;
	}
*/
}

void WaveTable::MorphSlots( int wavetableNumber, int selectedFromSlot, int selectedToSlot )
{
	WaveTable* waveTable = this;
	int waveSize = waveTable->waveSize;

	float spectrum1[1026];
	float spectrum2[1026];

	// Preload slot zero.
	float* src = waveTable->Wavedata + (wavetableNumber * waveTable->slotCount + selectedFromSlot) * waveTable->waveSize;

	memcpy( spectrum2, src, sizeof(float) * waveSize );
	realft( spectrum2 - 1, waveSize, 1 );

	// normalise spectrum.
    float scale = 2.0f / (float)waveSize;
	for( int i = 0 ; i < waveSize;++i )
	{
		spectrum2[i] *= scale;
	}

	// convert to magnitude/phase format.
	for( int s = 2 ; s < waveSize / (int)sizeof(float) ; s += 2 )
	{
		float phase = atan2( spectrum2[s], spectrum2[s+1] );
		float magnitude = sqrtf(spectrum2[s] * spectrum2[s] + spectrum2[s+1] * spectrum2[s+1] );
		spectrum2[s] = magnitude;
		spectrum2[s + 1] = phase;
	}

	spectrum2[0] *= 0.5f;	// normalise DC. !! not copied above !!!
	spectrum2[1] = (float)M_PI_2;	// allows DC to be treated correctly.

	memcpy( spectrum1, spectrum2, sizeof(spectrum1) );

	// load the upper slot.
	src = waveTable->Wavedata + (wavetableNumber * waveTable->slotCount + selectedToSlot) * waveTable->waveSize;
	memcpy( spectrum2, src, sizeof(float) * waveSize );
	realft( spectrum2 - 1, waveSize, 1 );
/*
	// zero-out DC, nyquist values.
	spectrum2[0] = 0.0f;
	spectrum2[1] = 0.0f;
*/
	// normalise spectrum.
	scale = 2.0f / (float)waveSize;
	for( int i = 0 ; i < waveSize;++i )
	{
		spectrum2[i] *=  scale;
	}

	// convert to magnitude/phase format.
	for( int s = 2 ; s < waveSize / (int)sizeof(float) ; s += 2 )
	{
		float phase = atan2( spectrum2[s], spectrum2[s+1] );
		float magnitude = sqrtf(spectrum2[s] * spectrum2[s] + spectrum2[s+1] * spectrum2[s+1] );
		spectrum2[s] = magnitude;
		spectrum2[s + 1] = phase;
	}

	spectrum2[0] *= 0.5f;	// normalise DC.
	spectrum2[1] = (float)M_PI_2;	// allows DC to be treated correctly.

	// Generate in-between 'ghost' slots.
	{

		for( int morphSlot = selectedFromSlot + 1 ; morphSlot < selectedToSlot ; ++morphSlot )
		{
			float morph = (float) (morphSlot-selectedFromSlot) / (float) (selectedToSlot-selectedFromSlot);

			int destSlot = morphSlot;

			float spectrum[1026];

			// morph specturm smoothly.
			for( int s = 0 ; s < waveSize ; s += 2 )
			{
				float magnitude1 = spectrum1[s];
				float phase1 = spectrum1[s+1];
				float magnitude2 = spectrum2[s];
				float phase2 = spectrum2[s+1];

//float test1 = magnitude1 * sinf(phase1);
//float test2 = magnitude1 * cosf(phase1);


				float magnitude = magnitude1 + morph * ( magnitude2 - magnitude1 );
				float phaseDelta = phase2 - phase1;
				if( phaseDelta > (float)M_PI ) // choose shortest phase wrap
				{
					phaseDelta -= (float)M_PI * 2.0f;
				}
				else
				{
					if( phaseDelta < (float)-M_PI ) // choose shortest phase wrap
					{
						phaseDelta += (float)M_PI * 2.0f;
					}
				}
				float phase = phase1 + morph * phaseDelta;

				spectrum[s] = magnitude * sinf(phase);
				spectrum[s+1] = magnitude * cosf(phase);
			}

			spectrum[0] *= 2.0f; // DC Level needs to be twice other partials.
			spectrum[1] = 0.0f; // remove 'phase' of DC componenet, else ends up as a false nyquist level.

			{

				realft( spectrum - 1, (unsigned int) waveSize, -1 );

				//float scale = 2.0f / waveSize;

				float* dest = waveTable->Wavedata + (wavetableNumber * waveTable->slotCount + destSlot) * waveTable->waveSize;

				for( int count = 0 ; count < waveSize ; ++count )
				{
					*dest++ = spectrum[count];// * scale;
				}
			}
		}
	}
}

void CalcMagnitudePhaseSpectrum( float* dest, float* src, int sourceSize )
{
	memcpy( dest, src, sizeof(float) * sourceSize );
	realft( dest - 1, sourceSize, 1 );

	/* no, leave DC-removal to import. For diagnostics, it's useful to be able to play DC through the PSOLA effect.
	// zero-out DC, nyquist values.
	spectrum2[0] = 0.0f;
	spectrum2[1] = 0.0f;
	*/

	// normalise spectrum.
    float scale = 2.0f / (float)sourceSize;
	for( int i = 0 ; i < sourceSize; ++i )
	{
		dest[i] *=  scale;
	}

	// convert to magnitude/phase format.
	for( int s = 2 ; s < sourceSize ; s += 2 )
	{
		float phase = atan2( dest[s], dest[s+1] );
		float magnitude = sqrtf(dest[s] * dest[s] + dest[s+1] * dest[s+1] );
		dest[s] = magnitude;
		dest[s + 1] = phase;
	}

	dest[0] *= 0.5f;	// normalise DC.
	dest[1] = (float) M_PI_2;	// allows DC to be treated correctly.
}

// PLease use CopyAndMipmap2 instead.
void WaveTable::CopyAndMipmap( WaveTable* sourceWavetable, WavetableMipmapPolicy &mipInfo )
{
//    _RPT0(_CRT_WARN, "CopyAndMipmap(). START....\n" );

	assert( sourceWavetable->waveTableCount == 1 ); // only handles a single wavetable.
/*
    // Calculate size of DSP MIP-Mapped wavetable.
    WaveTable newDimensions = *sourceWavetable;
	newDimensions.waveTableCount = 1;
	newDimensions.slotCount = WaveTable::MorphedSlotRatio * (sourceWavetable->slotCount - 1 ) + 1; // add extra slots in-between.

	// Mip-maps require extra memory. Calculate.
	WavetableMipmapPolicy mipInfo;
	mipInfo.initialize(&newDimensions);
*/
	int table = 0;

	const int fftMaxSize = 1024;

	float spectrum1[fftMaxSize + 2];
	float spectrum2[fftMaxSize + 2];
	float spectrum[fftMaxSize + 2]; // morphed spectrum.

	int sourceSize = sourceWavetable->waveSize;

	// zero out unneeded FFT data.
	for( int s = sourceSize ; s < fftMaxSize ; ++s )
	{
		spectrum1[s] = 0.0f;
		spectrum2[s] = 0.0f;
		spectrum[s] = 0.0f;
	}

	// Preload slot zero.
	float* src = sourceWavetable->Wavedata + sourceWavetable->waveSize * ( table * sourceWavetable->slotCount );

	CalcMagnitudePhaseSpectrum( spectrum2, src, sourceSize );

	// Generate in-between 'ghost' slots.
	for( int slot = 0 ; slot < sourceWavetable->slotCount - 1 ; ++slot )
	{
		memcpy( spectrum1, spectrum2, sizeof(spectrum1) );

		// load the upper slot.
		float* src = sourceWavetable->Wavedata + sourceWavetable->waveSize * ( slot + 1 + table * sourceWavetable->slotCount );
		CalcMagnitudePhaseSpectrum( spectrum2, src, sourceSize );

		int morphTo = WaveTable::MorphedSlotRatio;
		if( slot == sourceWavetable->slotCount - 2 ) // on very last slot, do the extra one.
		{
			++morphTo;
		}

		for( int morphSlot = 0 ; morphSlot < morphTo ; ++morphSlot )
		{
			float morph = (float) morphSlot / (float) WaveTable::MorphedSlotRatio;

			int destSlot = slot * WaveTable::MorphedSlotRatio + morphSlot;

			// morph specturm smoothly.
			for( int s = 0 ; s < sourceSize ; s += 2 )
			{
				float magnitude1 = spectrum1[s];
				float phase1 = spectrum1[s+1];
				float magnitude2 = spectrum2[s];
				float phase2 = spectrum2[s+1];

				float magnitude = magnitude1 + morph * ( magnitude2 - magnitude1 );
				float phaseDelta = phase2 - phase1;
				if( phaseDelta > M_PI ) // choose shortest phase wrap
				{
					phaseDelta -= (float) M_PI * 2.0f;
				}
				else
				{
					if( phaseDelta < -M_PI ) // choose shortest phase wrap
					{
						phaseDelta += (float) M_PI * 2.0f;
					}
				}
				float phase = phase1 + morph * phaseDelta;

				spectrum[s] = magnitude * sinf(phase);
				spectrum[s+1] = magnitude * cosf(phase);
			}

			spectrum[0] *= 2.0f; // DC Level needs to be twice other partials.
			spectrum[1] = 0.0f; // remove 'phase' of DC componenet, else ends up as a false nyquist level.

			// for each mip level.
			for( int mip = 0 ; mip < mipInfo.getMipCount() ; ++mip )
			{
//				_RPT1(_CRT_WARN, "%x\n", ( dest- waveTableMipMapped->Wavedata ) / 4 );
				// halve the size of the FFT, removing upper octave.
				// Perform inverse FFT.
				// Copy to dest.
				float waveDownsampled[1024];
				int mipWaveSize = mipInfo.GetWaveSize(mip);
				int binCount = mipInfo.GetFftBinCount(mip); // number of harmonics plus 1 (DC component).
				float scale2 = (float)mipWaveSize * 0.5f;

				// Copy required number of harmonics.
				for( int i = 0 ; i < binCount * 2; ++i )
				{
					waveDownsampled[i] = spectrum[i] * scale2;
				}

				// zero-out unwanted high harmonics.
				for( int i = binCount * 2 ; i < mipWaveSize; ++i )
				{
					waveDownsampled[i] = 0.0f;
				}

				realft( waveDownsampled - 1, (unsigned int) mipWaveSize, -1 );

				float scale = 2.0f / mipWaveSize;

				float* dest = Wavedata + mipInfo.getSlotOffset( 0, destSlot, mip );

				#ifdef SE_WT_OSC_STORE_HALF_CYCLES // Assume symetrical wave.
					for( int count = 0 ; count < mipWaveSize / 2 ; ++count )
					{
						*dest++ = waveDownsampled[count] * scale;
					}
				#else
					for( int count = 0 ; count < mipWaveSize ; ++count )
					{
						*dest++ = waveDownsampled[count] * scale;
					}
				#endif
			}
		}
	}
//    _RPT0(_CRT_WARN, "CopyAndMipmap(). DONE....\n" );
}

void WaveTable::CopyAndMipmap2(WavetableMipmapPolicy &destMipInfo, int wavetable, float* destSamples)
{
	// New
	{
		const int interpolationSamples = 4;
		float spectrumSource[interpolationSamples][WaveTable::WavetableFileSampleCount];

		// prepare spectrum interpolation array. need prev, current and 2 future.
		for( int slot = -3; slot < slotCount; ++slot )
		{
			// shuffle spectrum.
			for( int i = 0; i < interpolationSamples - 1; ++i )
			{
				memcpy( spectrumSource[i], spectrumSource[i + 1], sizeof( spectrumSource[0] ) );
			}

			// load next slot.
			int loadSlot = std::min( std::max( slot + 2, 0 ), slotCount - 1);
			float* src = GetSlotPtr( wavetable, loadSlot );
			CalcMagnitudePhaseSpectrum( spectrumSource[interpolationSamples - 1], src, WaveTable::WavetableFileSampleCount );

			if( slot >= 0 && slot < slotCount - 1 )
			{
				int morphs = WaveTable::MorphedSlotRatio;
				if( slot == slotCount - 2 ) // on very last slot, do the extra one.
				{
					++morphs;
				}

				for( int morphSlot = 0; morphSlot < morphs; ++morphSlot )
				{
					float fraction = (float) morphSlot / (float) WaveTable::MorphedSlotRatio;

					int destSlot = slot * WaveTable::MorphedSlotRatio + morphSlot;

					// morph spectrum smoothly. ignore phase.
					float spectrum[WaveTable::WavetableFileSampleCount];
					for( int s = 0; s < WaveTable::WavetableFileSampleCount; s += 2 )
					{
						/*
						// linear.
						float magnitude1 = spectrumSource[1][s];
						float magnitude2 = spectrumSource[2][s];
						float magnitude = magnitude1 + fraction * ( magnitude2 - magnitude1 );
						*/

						// cubic.
						float y0 = spectrumSource[0][s];
						float y1 = spectrumSource[1][s];
						float y2 = spectrumSource[2][s];
						float y3 = spectrumSource[3][s];
						float magnitude = y1 + 0.5f * fraction*( y2 - y0 + fraction*( 2.0f*y0 - 5.0f*y1 + 4.0f*y2 - y3 + fraction*( 3.0f*( y1 - y2 ) + y3 - y0 ) ) );

						spectrum[s] = 0.0f;
						spectrum[s + 1] = magnitude;
					}

					spectrum[0] *= 2.0f; // DC Level needs to be twice other partials.
					spectrum[1] = 0.0f; // remove 'phase' of DC componenet, else ends up as a false nyquist level.

					// for each mip level.
					for( int mip = 0; mip < destMipInfo.getMipCount( ); ++mip )
					{
						//				_RPT1(_CRT_WARN, "%x\n", ( dest- waveTableMipMapped->Wavedata ) / 4 );
						// halve the size of the FFT, removing upper octave.
						// Perform inverse FFT.
						// Copy to dest.
						float waveDownsampled[1024];
						int mipWaveSize = destMipInfo.GetWaveSize( mip );
						int binCount = destMipInfo.GetFftBinCount( mip ); // number of harmonics plus 1 (DC component).
						float scale2 = (float) mipWaveSize * 0.5f;

						// Copy required number of harmonics.
						for( int i = 0; i < binCount * 2; ++i )
						{
							waveDownsampled[i] = spectrum[i] * scale2;
						}

						// zero-out unwanted high harmonics.
						for( int i = binCount * 2; i < mipWaveSize; ++i )
						{
							waveDownsampled[i] = 0.0f;
						}

						realft( waveDownsampled - 1, (unsigned int) mipWaveSize, -1 );

						float scale = 2.0f / mipWaveSize;

						float* dest = destSamples + destMipInfo.getSlotOffset( wavetable, destSlot, mip );
						//				_RPT4(_CRT_WARN, "CopyAndMipmap(). WT:%d SL:%d MIP:%d offset:%d\n", wavetable, destSlot, mip, destMipInfo.getSlotOffset(wavetable, destSlot, mip));

						for( int count = 0; count < mipWaveSize / 2; ++count )
						{
							*dest++ = waveDownsampled[count] * scale;
						}
					}
				}
			}
		}
	}

//debug.
/*
	for( int s = 0; s < WaveTable::MorphedSlotRatio * slotCount - 1; ++s )
	{
		float* dest = destSamples + destMipInfo.getSlotOffset( 0, s, 0 );
		_RPT1( _CRT_WARN, "%f\n", dest[100] );
	}
*/
	/* old
	return;

    _RPT0(_CRT_WARN, "CopyAndMipmap(). START....\n" );
    WaveTable* sourceWavetable = this;

	assert( sourceWavetable->waveTableCount == 1 ); // only handles a single wavetable.

	int table = 0;

	const int fftMaxSize = 1024;

	float spectrum1[fftMaxSize + 2];
	float spectrum2[fftMaxSize + 2];
	float spectrum[fftMaxSize + 2]; // morphed spectrum.

	int sourceSize = sourceWavetable->waveSize;

	// zero out unneeded FFT data.
	for( int s = sourceSize ; s < fftMaxSize ; ++s )
	{
		spectrum1[s] = 0.0f;
		spectrum2[s] = 0.0f;
		spectrum[s] = 0.0f;
	}

	// Preload slot zero.
	float* src = sourceWavetable->Wavedata + sourceWavetable->waveSize * (table * sourceWavetable->slotCount);

	CalcMagnitudePhaseSpectrum( spectrum2, src, sourceSize );

	// Generate in-between 'ghost' slots.
	for( int slot = 0 ; slot < sourceWavetable->slotCount - 1 ; ++slot )
	{
		memcpy( spectrum1, spectrum2, sizeof(spectrum1) );

		// load the upper slot.
		float* src = sourceWavetable->Wavedata + sourceWavetable->waveSize * ( slot + 1 + table * sourceWavetable->slotCount );
		CalcMagnitudePhaseSpectrum( spectrum2, src, sourceSize );

		int morphTo = WaveTable::MorphedSlotRatio;
		if( slot == sourceWavetable->slotCount - 2 ) // on very last slot, do the extra one.
		{
			++morphTo;
		}

		for( int morphSlot = 0 ; morphSlot < morphTo ; ++morphSlot )
		{
			float morph = (float) morphSlot / (float) WaveTable::MorphedSlotRatio;

			int destSlot = slot * WaveTable::MorphedSlotRatio + morphSlot;

#ifdef FIX_ZERO_CROSSINGS

			// morph spectrum smoothly. ignore phase.
			for( int s = 0 ; s < sourceSize ; s += 2 )
			{
				float magnitude1 = spectrum1[s];
				float magnitude2 = spectrum2[s];
				float magnitude = magnitude1 + morph * ( magnitude2 - magnitude1 );

				spectrum[s] = 0.0f;
				spectrum[s+1] = magnitude;
			}
#else

			// morph spectrum smoothly.
			for( int s = 0 ; s < sourceSize ; s += 2 )
			{
				float magnitude1 = spectrum1[s];
				float phase1 = spectrum1[s+1];
				float magnitude2 = spectrum2[s];
				float phase2 = spectrum2[s+1];

				float magnitude = magnitude1 + morph * ( magnitude2 - magnitude1 );
				float phaseDelta = phase2 - phase1;
				if( phaseDelta > M_PI ) // choose shortest phase wrap
				{
					phaseDelta -= (float) M_PI * 2.0f;
				}
				else
				{
					if( phaseDelta < -M_PI ) // choose shortest phase wrap
					{
						phaseDelta += (float) M_PI * 2.0f;
					}
				}
				float phase = phase1 + morph * phaseDelta;

				spectrum[s] = magnitude * sinf(phase);
				spectrum[s+1] = magnitude * cosf(phase);
			}
#endif

			spectrum[0] *= 2.0f; // DC Level needs to be twice other partials.
			spectrum[1] = 0.0f; // remove 'phase' of DC componenet, else ends up as a false nyquist level.

			// for each mip level.
			for( int mip = 0 ; mip < destMipInfo.getMipCount() ; ++mip )
			{
//				_RPT1(_CRT_WARN, "%x\n", ( dest- waveTableMipMapped->Wavedata ) / 4 );
				// halve the size of the FFT, removing upper octave.
				// Perform inverse FFT.
				// Copy to dest.
				float waveDownsampled[1024];
				int mipWaveSize = destMipInfo.GetWaveSize(mip);
				int binCount = destMipInfo.GetFftBinCount(mip); // number of harmonics plus 1 (DC component).
				float scale2 = (float)mipWaveSize * 0.5f;

				// Copy required number of harmonics.
				for( int i = 0 ; i < binCount * 2; ++i )
				{
					waveDownsampled[i] = spectrum[i] * scale2;
				}

				// zero-out unwanted high harmonics.
				for( int i = binCount * 2 ; i < mipWaveSize; ++i )
				{
					waveDownsampled[i] = 0.0f;
				}

				realft( waveDownsampled - 1, (unsigned int) mipWaveSize, -1 );

				float scale = 2.0f / mipWaveSize;

				float* dest = destSamples + destMipInfo.getSlotOffset(wavetable, destSlot, mip);
//				_RPT4(_CRT_WARN, "CopyAndMipmap(). WT:%d SL:%d MIP:%d offset:%d\n", wavetable, destSlot, mip, destMipInfo.getSlotOffset(wavetable, destSlot, mip));

				#ifdef SE_WT_OSC_STORE_HALF_CYCLES // Assume symetrical wave.
					for( int count = 0 ; count < mipWaveSize / 2 ; ++count )
					{
						*dest++ = waveDownsampled[count] * scale;
					}
				#else
					for( int count = 0 ; count < mipWaveSize ; ++count )
					{
						*dest++ = waveDownsampled[count] * scale;
					}
				#endif
			}

		}
	}
//    _RPT0(_CRT_WARN, "CopyAndMipmap(). DONE....\n" );
	for( int s = 0; s < WaveTable::MorphedSlotRatio * sourceWavetable->slotCount - 1; ++s )
	{
		float* dest = destSamples + destMipInfo.getSlotOffset( 0, s, 0 );
		_RPT1(_CRT_WARN, "%f\n", dest[100]);
	}
	*/

}


////////////////////////////////////////////////////////////

PeriodExtractor::PeriodExtractor()
{
	// FFT quietest signal. (anything quieter truncated).
	settings.referenceDb = -200.0; // -160.0; // Try to get enough harmonics to work with, but not much noise and low-level crap.
	settings.retainDb = 958; // 50.0
	settings.WhiteningFilter = 19;

	// Genetic alg stuff
	double* dest = (double*) &settings;
	int count = min( overrideSettings.size(), ( int ) sizeof( Genes ) / sizeof( double ) );
	for( int i = 0; i < count; ++i )
	{
		*dest++ = overrideSettings[i];
	}

	// enfore any rules needed to prevent crash.
	settings.retainDb = std::max(1.0, settings.retainDb);
	settings.WhiteningFilter = std::max(0.000001, settings.WhiteningFilter);
}


PeriodExtractor::~PeriodExtractor()
{
}

void PeriodExtractor::Whiten( float* spectrum, int n, bool debug )
{
	float spectralShape[1024];
	assert( n <= 1024 );

	// filter forward discarding results from near end to get a rough guess of spectral shape at end of graph. This will be our filter's initial value.
	float cuttoff = 0.01f; // faster settling.
	float l = exp( -M_PI * 2 * cuttoff );
	int s = ( 3 * n ) / 4;
	float y1n = spectrum[s];
	for( ; s < n; s++ )
	{
		// low pass
		float xn = spectrum[s];
		y1n = xn + l * ( y1n - xn );
	}

	// Nice and smooth spectral shape.
	cuttoff = settings.WhiteningFilter * 0.001f; //  0.01f;
	l = exp( -M_PI * 2 * cuttoff );

	// Filter backward.
	for( int s = n - 1; s >= 0; s-- )
	{
		float xn = spectrum[s];
		// low pass
		spectralShape[s] = y1n = xn + l * ( y1n - xn );
	}

	// Filter foreward.
	float shapeMax = 0.0f;
	for( int s = 0; s < n; s++ )
	{
		float xn = spectralShape[s];
		// low pass
		spectralShape[s] = y1n = xn + l * ( y1n - xn );
		shapeMax = std::max( shapeMax, spectralShape[s] );
	}

	// apply whitening. flatten spectrum, plus scale according to original average height. i.e. quieter hamonics reduced in amplitude.
	for( int s = 0; s < n; s++ )
	{
		float spec = spectrum[s];
		float importance = ( spectralShape[s] - ( shapeMax - settings.retainDb ) ) / settings.retainDb;
		importance = std::max( importance, 0.0f );
		float whitened = ( spectrum[s] - spectralShape[s] ) * importance;
		spectrum[s] = std::max( 0.0f, whitened );
		/*
		if( debug )
		{
		_RPT3( _CRT_WARN, "%f ,%f, %f\n", spec, spectrum[s], spectralShape[s] );
		}
		*/
	}
}


float PeriodExtractor::ExtractPeriod2(float* sample, int sampleCount, int autocorrelateto, float* diagnosticOutput, float* diagnosticProbeOutput)
{
	const int minimumPeriod = 10; // wave less than 16 samples not much use.
	const int n = 2048;
	int correlateCount = n;
	int bins = n / 2;

	// New - FFT based.
	float realData[n + 1];

	// Copy wave to temp array for FFT.
	autocorrelateto = max( 0, min( autocorrelateto, sampleCount - n ) ); // can't correlate too near end of sample.
	int tocopy = min( n, sampleCount - autocorrelateto );
	for( int s = 0; s < tocopy; s++ )
	{
		float window = 0.5f - 0.5f * cosf( s * 2.0f *M_PI / n ); // hanning.
		realData[s + 1] = sample[(int) autocorrelateto + s] * window;
	}

	// Zero-pad.
	for( int s = tocopy; s < n; s++ )
	{
		realData[s + 1] = 0.0f;
	}

	// Perform forward FFT.
	realft( realData, n, 1 );

	// convert to magnitude spectrum.
	float autoCorrelation[n + 2];
	float no2 = n / 2;

	float lowestCommonDenominator[n / 2];
	memset( lowestCommonDenominator, 0, sizeof( lowestCommonDenominator ) );

	const double minimumGain = pow( 10.0, settings.referenceDb * 0.05 );

	realData[2] = 0.0f; // normally nyquist, packed in beside DC. By zeroing it, we avoid nyquist level adding to DC result. 
	realData[1] *= 0.5f; // DC must be divided by 2. 
	for( int i = 1; i < n; i += 2 )
	{
		float magnitude = ( realData[i] * realData[i] + realData[i + 1] * realData[i + 1] ) / no2;

		// Avoid overflow on zero gain.
		if( magnitude < minimumGain )
		{
			magnitude = settings.referenceDb;
		}
		else
		{
			magnitude = 20.0 * log10( magnitude ); // dB
		}

		magnitude -= settings.referenceDb;

		int freq = i / 2;
		autoCorrelation[freq] = magnitude;
	}

	Whiten( autoCorrelation, n / 2, diagnosticOutput != 0 );

	if( diagnosticOutput )
	{
		for( int bin = 0; bin < n / 2; ++bin )
		{
			diagnosticOutput[bin] = autoCorrelation[bin];
		}
	}

	// skip empty bins.
	int lastBin = bins;
	while( autoCorrelation[lastBin - 1] == 0.0f && lastBin > 1 )
	{
		lastBin--;
	}

	// log spaced freq probes.
	// fit to model sub-sampled.
	float scale = 1.0f / (n / 2);
	for( int resultSlot = 0; resultSlot < bins; ++resultSlot )
	{
		double candidateFundamentalBin = ResultBinToFftBin( resultSlot, bins );

		double pEnergy = 0.0f;
		for( int bin = 0; bin < lastBin; ++bin )
		{
			float window = calcProbeFunction( bin, candidateFundamentalBin );
			pEnergy += autoCorrelation[bin] * window;
		}
		lowestCommonDenominator[resultSlot] = pEnergy * scale;
	}

	if( diagnosticProbeOutput )
	{
		for( int i = 0; i < n / 2; ++i )
		{
			diagnosticProbeOutput[i] = lowestCommonDenominator[i];
		}
	}

	// Return highest peak.
	int bestPeriod = 0;
	{
		float best = -100000.0f;
		for( int i = 0; i < n / 2; ++i )
		{
			if( lowestCommonDenominator[i] > best )
			{
				best = lowestCommonDenominator[i];
				bestPeriod = i;
			}
		}

		// fail? Usually signal too low to register. Avoid spurios super-high results.
		if( bestPeriod == n / 2 - 1 && lowestCommonDenominator[bestPeriod] < 0.1f )
		{
			bestPeriod = 0;
		}

		// estimate certanty.
		float close = best - 0.05; // 5% ish.
		int count = 0;
		for( int i = 0; i < n / 2; ++i )
		{
			if( lowestCommonDenominator[i] > close )
			{
				count++;
			}
		}

		float certainty = 1.0f - count / (float) ( n / 2 );

		if( certainty < 0.2f ) // fail 20% of reading almost as good.
		{
			return -1.0f;
		}
	}

	// convert bin to period.
	//double freq = 2.0 * pow( 2.0, minOctave + maxOctave * bestPeriod / (double) bins );
	double freq = PeriodExtractor::ResultBinToFftBin( bestPeriod, bins );
	double period = n / freq;
	return period;
}
