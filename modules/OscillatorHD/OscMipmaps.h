#pragma once

#include <vector>
#include <map>
#include <sstream>
#include <iomanip>
#include <functional>
#include "../shared/real_fft.h"

namespace MipMapCalculator
{
	struct Mip
	{
		int _higestPartial = {};

		Mip(int highest) : _higestPartial(highest)
		{
		}

		inline constexpr static double humanHearingMaxHz = 20000.0;
		inline constexpr static double middleA = 69.0;
		inline constexpr static double semitonesPerOctave = 12.0f;
		inline constexpr static double octave = 12.0f;
		inline constexpr static double nyquistSafetyMargin = 0.95; // Less interpolation artifacts if staying under 90% of nyquist.

		int lowestPartial() const
		{
			return 1; // for now.
		}
		int higestPartial() const
		{
			return _higestPartial;
		}
		double maxHz(double sampleRate) const
		{
			const double nyquist = sampleRate * 0.5;
			const double higestPlayableFrequency = nyquist * nyquistSafetyMargin;
			const double maxAudibleFreq = (std::min)(higestPlayableFrequency, humanHearingMaxHz);

			return maxAudibleFreq / higestPartial();
		}
		double maxNote(double sampleRate) const{ return semitonesPerOctave * log(maxHz(sampleRate) / 440.0) / log(2.0) + middleA; }
		double mipSwitchNote(double sampleRate) const{ return floor(maxNote(sampleRate) - 0.5) + 0.5; } // switch MIPs on half-semitone boundaries to minimize MIP-thrashing.
	};

	struct MipMap
	{
		double sampleRate = {};
		std::vector<Mip> mips;
	};

	MipMap CalcMips(float sampleRate, std::function<std::tuple<float, float>(int)> getHarmonicLevel)
	{
		MipMap result;
		result.sampleRate = sampleRate;

		const double nyquist = sampleRate * 0.5;
		const double higestPlayableFrequency = nyquist * Mip::nyquistSafetyMargin; // Highest Hz possible without risk of aliasing. 95% of nyquist.
		const double maxAudibleFreq = (std::min)(higestPlayableFrequency, Mip::humanHearingMaxHz);

		int currrentPartials = (std::numeric_limits<int>::max)();

		// Also ignore hamonics below -100dB (-96dB = 16 bits of resolution).
		constexpr float minimumHarmonicLevel_dB = -100.0f;
		const float minimumHarmonicLevel = powf(10.0f, minimumHarmonicLevel_dB * 0.05f);
		const float minimumHarmonicLevelSquared = minimumHarmonicLevel * minimumHarmonicLevel;

		for (int key = 0; key < 127; ++key)
		{
			const auto Hz = 440.0 * pow(2.0, (key - Mip::middleA) / Mip::octave);

			// Can the current mip scale this high? If so, recycle it.
			const auto highestPartialHz = Hz * currrentPartials;
			if(highestPartialHz < higestPlayableFrequency)
			{
				continue;
			}

			// What's the highest theoretical audible harmonic for this key?
			auto highestHarmonic = static_cast<int>(maxAudibleFreq / Hz);

			// Ignore inaudible/ non-existant harmonics.
			auto hamonicLevels = getHarmonicLevel(highestHarmonic);
			auto magnitudeSquared = std::get<0>(hamonicLevels) * std::get<0>(hamonicLevels) + std::get<1>(hamonicLevels) * std::get<1>(hamonicLevels);
			while(magnitudeSquared < minimumHarmonicLevelSquared && highestHarmonic > 0)
			{
				--highestHarmonic;
				hamonicLevels = getHarmonicLevel(highestHarmonic);
				magnitudeSquared = std::get<0>(hamonicLevels) * std::get<0>(hamonicLevels) + std::get<1>(hamonicLevels) * std::get<1>(hamonicLevels);
			}

			// Is this MIP identical to previous, if so recycle it.
			if(highestHarmonic == currrentPartials)
			{
				continue;
			}

			currrentPartials = highestHarmonic;
			result.mips.push_back({ highestHarmonic });
		}

		// silent mip at top end
		result.mips.push_back({ 0 });

		std::reverse(result.mips.begin(), result.mips.end());

		return result;
	}

	void PrintMips(double sampleRate, const MipMap& mipMap, const char* name)
	{
		const int width[] = { 6, 9, 9, 9, 9 };
		std::ostringstream s2;
		s2.precision(2);

		s2 << "\n" << name;
		s2 << "\n-----Partials Needed-----["  << std::fixed << sampleRate << "Hz]----\n";
		s2 << std::setw(width[0]) << std::right << "Partls"
//			<< std::setw(width[1]) << std::right << "Used by"
			<< std::setw(width[2]) << std::right << "Max"
			<< std::setw(width[3]) << std::right << "Note"
			<< std::setw(width[4]) << std::right << "Switch"
			<< "\n";

		for (const auto& p : mipMap.mips)
		{
			s2 << std::setw(width[0]) << std::right << p.higestPartial()
				//				<< std::setw(width[1]) << std::right << p.second
			<< std::setw(width[2]) << std::right << std::fixed << p.maxHz(sampleRate)
			<< std::setw(width[3]) << std::right << std::fixed << p.maxNote(sampleRate)
			<< std::setw(width[4]) << std::right << std::fixed << p.mipSwitchNote(sampleRate)
			<< "\n";

#if 0//defined(_DEBUG) && defined(WIN32)
			_RPT1(_CRT_WARN, "%s", s2.str().c_str());
#endif
			s2.str("");
			s2.clear();
		}
	}

	struct WavetableMip
	{
		static constexpr int interpolationSamples = 4;
		double minimumIncrement = {};
		double maximumIncrement = {};

		int GetWaveSize() const
		{
			return static_cast<int>(samples.size()) - 2 * interpolationSamples;
		}
		const float* GetWave() const
		{
			return samples.data() + interpolationSamples;
		}
		std::vector<float> samples;
	};

	int calcWaveSize(int maxHarmonic)
	{
		if(maxHarmonic < 1)
		{
			return WavetableMip::interpolationSamples; // this is the smallest a wave can be safely.
		}

		// No wave to be less than this, to keep aliasing. of sin below -140dB
		const int globalMinimumWaveSize = 256;
		const int globalMaximumWaveSize = 8192;
		
		// Oversample everything to reduce interpolation noise.
		const int globalOversamplingNum = 8;
		const int globalOversamplingDen = 1;

		// Need at minimum 2 samples to represent a harmonic.
		const int minimumWavesize = (std::min)(globalMaximumWaveSize, (maxHarmonic * 2 * globalOversamplingNum) / globalOversamplingDen);

		// FFT requires a wavesize that is power of 2.
		int wavesize = globalMinimumWaveSize;
		while (wavesize < minimumWavesize)
		{
			wavesize = wavesize << 1;
		}

		assert(wavesize > WavetableMip::interpolationSamples); // interpolation sample filling requires this.

		return wavesize;
	}

	std::shared_ptr<std::vector<WavetableMip>> generateWavetable(const MipMap& mipMap, std::function<std::tuple<float, float>(int)> getHarmonicLevel)
	{
		auto wavetable = std::make_shared<std::vector<WavetableMip>>();

		wavetable->reserve(mipMap.mips.size());

		for (const auto& p : mipMap.mips)
		{
			WavetableMip mip;

			const int activepartials = p.higestPartial();

			if(activepartials == 0)
			{
				mip.maximumIncrement = (std::numeric_limits<double>::max)();
			}
			else
			{
				mip.maximumIncrement = MipMapCalculator::Mip::nyquistSafetyMargin * 0.5 / activepartials;
			}

			const auto wavesize = calcWaveSize(activepartials);

			mip.samples.resize(wavesize + 2 * WavetableMip::interpolationSamples);
			float* dest = &(mip.samples[WavetableMip::interpolationSamples]);
			const unsigned int fftSize = wavesize;
			const bool applyGibbsFix = false;// TODO mip > 2 && mip == pMipMapPolicy.getMipCount() - 1; // lowest mip of sawtooth smooth to reduce overtone when stretched right down.

	//#ifdef PRINT_WAVETABLE_STATS
	//		_RPT0(_CRT_WARN, "Harm     lev          Gibbs\n");
	//		_RPT0(_CRT_WARN, "---------------------------\n");
	//#endif

			const int components = (std::min)(activepartials * 2, wavesize - 2);

#ifdef PRINT_WAVETABLE_STATS
			_RPT3(_CRT_WARN, "MIP %3d partials %3d size %d\n", mip, (componenets / 2), wavesize);
#endif

			int i;
			int harmonic = 0;
			for(i = 0; i < (components + 2); i = i + 2)
			{
				std::tie(dest[i], dest[i + 1]) = getHarmonicLevel(harmonic);

				// Gibbs fix intended to make wave smoother for less interpolation noise.
				// Requires extra MIPs to work without attenuating HF.
				if(applyGibbsFix)
				{
					float damping = (float)(i - 2) / (components);
					float window = 0.5f + 0.5f * cosf(damping * (float)M_PI);
					dest[i + 1] *= window;
					dest[i] *= window;
				}
				++harmonic;
			}
			for(; i < (int)fftSize; ++i)
			{
				dest[i] = 0.0f;
			}

			realft(dest - 1, fftSize, -1);

			// Wrap samples off front and back to ease interpolator.
			for(i = -WavetableMip::interpolationSamples; i < 0; ++i)
			{
				dest[i] = dest[i + fftSize];
			}
			for(i = 0; i < WavetableMip::interpolationSamples; ++i)
			{
				dest[fftSize + i] = dest[i];
			}

			wavetable->push_back(std::move(mip));
		}

		wavetable->back().minimumIncrement = -1;

		for(size_t i = 0; i < wavetable->size() - 1; ++i)
		{
			wavetable->at(i).minimumIncrement = wavetable->at(i+1).maximumIncrement;
		}

		return wavetable;
	}

	const WavetableMip* CalcMipLevel(const std::vector<WavetableMip>& mipMap, float increment)
	{
		for(size_t i = 1 ; i < mipMap.size() ; ++i )
		{
			if (mipMap[i].maximumIncrement < increment)
			{
				return &(mipMap[i - 1]);
			}
		}
		return &(mipMap.back());
	}
};

void PrintMidiNoteStats(double sampleRate)
{
	const int width[] = { 4, 9, 9, 9, 9 };

	const double nyquist = sampleRate * 0.5;
	const double higestPlayableFrequency = nyquist * (MipMapCalculator::Mip::humanHearingMaxHz / 22050); // Highest Hz possible without risk of aliasing. 90.7% of nyquist.
	const double maxAudibleFreq = (std::min)(higestPlayableFrequency, MipMapCalculator::Mip::humanHearingMaxHz);

	std::ostringstream s;
	s.precision(2);

	s << "\n" << std::fixed << sampleRate << " Hz";
	s << "\n------------------\n";
	s << std::setw(width[0]) << std::right << "Key"
		<< std::setw(width[1]) << std::right << "Hz"
		<< std::setw(width[2]) << std::right << "Partials"
		<< "\n";


	std::map<int, int> patialsRequired;

	int prevPartials = INT_MAX;
	double prevHz = 0.0001;

	for (int key = 0; key < 128; key++)
	{
		const auto Hz = 440.0 * pow(2.0, (key - MipMapCalculator::Mip::middleA) / MipMapCalculator::Mip::octave);
		const auto halfBentHz = 440.0 * pow(2.f, (0.5 + key - MipMapCalculator::Mip::middleA) / MipMapCalculator::Mip::octave); // halfway to next semitone, to allow for modulation/bender without MIP switching.
		const auto partials = static_cast<int>(maxAudibleFreq / halfBentHz);

		// Can re recycle a lower MIP.
		double transposeHighestPartialFreq = halfBentHz * static_cast<double>(prevPartials);
		const bool canRecycle = transposeHighestPartialFreq <= higestPlayableFrequency;

		s << std::setw(width[0]) << std::right << key
			<< std::setw(width[1]) << std::right << std::fixed << Hz;
		if (canRecycle)
		{
			s << std::setw(width[2]) << std::right << " \"\n";
		}
		else
		{
			s << std::setw(width[2]) << std::right << partials
				<< "\n";

			prevHz = Hz;
			prevPartials = partials;
		}
		
		patialsRequired[prevPartials]++;
	}

//	_RPT1(_CRT_WARN, "%s\n", s.str().c_str());

	std::ostringstream s2;
	s2.precision(2);

	s2 << "\n-----Partials Needed-----[" << sampleRate << "Hz]----\n";
	s2 << std::setw(width[0]) << std::right << "Partls"
		<< std::setw(width[1]) << std::right << "Used by"
		<< std::setw(width[2]) << std::right << "Max"
		<< std::setw(width[3]) << std::right << "Note"
		<< std::setw(width[4]) << std::right << "Switch"
		<< "\n";

	for (auto p : patialsRequired)
	{
		const double maxHz = maxAudibleFreq / p.first;
		const double maxNote = MipMapCalculator::Mip::octave * log(maxHz / 440.0) / log(2.0) + MipMapCalculator::Mip::middleA;
		const double mipSwitchNote = floor(maxNote - 0.5) + 0.5; // switch MIPs on half-semitone boundaries to minimize MIP-thrashing.

		s2 << std::setw(width[0]) << std::right << p.first
			<< std::setw(width[1]) << std::right << p.second
			<< std::setw(width[2]) << std::right << std::fixed << maxHz
			<< std::setw(width[3]) << std::right << std::fixed << maxNote
			<< std::setw(width[4]) << std::right << std::fixed << mipSwitchNote
			<< "\n";

//		_RPT1(_CRT_WARN, "%s", s2.str().c_str());
		s2.str("");
		s2.clear();
	}
}
