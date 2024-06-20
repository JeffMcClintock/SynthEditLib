/* Copyright (c) 2007-2023 SynthEdit Ltd

Permission to use, copy, modify, and /or distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THIS SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS.IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
#include "mp_sdk_audio.h"
#define _USE_MATH_DEFINES
#include <math.h>
#include <future>
#include <atomic>
#include "../shared/xp_simd.h"

using namespace gmpi;

static const float maxVolts = 10;

inline float SampleToVoltage(float s)
{
	return s * maxVolts;
}
inline float SampleToFrequency(float volts)
{
	return 440.f * powf(2.f, SampleToVoltage(volts) - maxVolts * 0.5f);
}


class ThickOscillator final : public MpBase2
{
	AudioInPin pinPitch;
	FloatInPin pinPulseWidth;
	IntInPin pinWaveform;
	AudioOutPin pinAudioOut;
	AudioInPin pinSync;
	AudioInPin pinPhaseMod;
	IntInPin pinFreqScale;
	AudioInPin pinPMDepth;
	BoolInPin pinSmoothPeaksGibbsEffect;
	BoolInPin pinSyncX_FadeAntiAlias;

	float readPos = 0;
	std::vector<float> wave;
	std::atomic<bool> exiting;
	std::atomic<int> refreshWave;

	std::thread backgroundThread;
	int refreshWaveBG = 0;
	float loopFundamental = 1.0f;

public:
	ThickOscillator()
	{
		initializePin( pinPitch );
		initializePin( pinPulseWidth );
		initializePin( pinWaveform );
		initializePin( pinAudioOut );
		initializePin( pinSync );
		initializePin( pinPhaseMod );
		initializePin( pinFreqScale );
		initializePin( pinPMDepth );
		initializePin( pinSmoothPeaksGibbsEffect );
		initializePin( pinSyncX_FadeAntiAlias );
	}
	~ThickOscillator()
	{
		exiting = true;
		backgroundThread.join();
	}

	void subProcess( int sampleFrames )
	{
		// get pointers to in/output buffers.
		auto pitch = getBuffer(pinPitch);
//		auto pulseWidth = getBuffer(pinPulseWidth);
		auto audioOut = getBuffer(pinAudioOut);
		auto sync = getBuffer(pinSync);
		auto phaseMod = getBuffer(pinPhaseMod);
		auto pMDepth = getBuffer(pinPMDepth);

		auto Hz = SampleToFrequency(*pitch);
		float inc = Hz / loopFundamental;

		for( int s = sampleFrames; s > 0; --s )
		{
			int table_floor = FastRealToIntTruncateTowardZero(readPos);
			while(table_floor >= wave.size() )
			{
				table_floor -= wave.size();
				readPos -= wave.size();
			}

			*audioOut = wave[table_floor];

			readPos += inc;

			// Increment buffer pointers.
			++pitch;
			++audioOut;
			++sync;
			++phaseMod;
			++pMDepth;
		}
	}

	int32_t open() override
	{
		wave.assign(getSampleRate(), 0.0f);

		backgroundThread = std::thread([this]() {
			generateWave();
		});

		return MpBase2::open();
	}

	void onSetPins() override
	{
		// Check which pins are updated.
		if( pinPitch.isStreaming() )
		{
		}
		if( pinPulseWidth.isUpdated() )
		{
//			auto res = std::async(std::launch::async, [&] {
//				generateWave();
////				std::this_thread::sleep_for(std::chrono::seconds(2));
////				std::cout << "Doing Delayed Task... at "	<< Time() << " sec, value " << some_value << std::endl;
//			});

			refreshWave++;
		}
		if( pinWaveform.isUpdated() )
		{
		}
		if( pinSync.isStreaming() )
		{
		}
		if( pinPhaseMod.isStreaming() )
		{
		}
		if( pinFreqScale.isUpdated() )
		{
		}
		if( pinPMDepth.isStreaming() )
		{
		}
		if( pinSmoothPeaksGibbsEffect.isUpdated() )
		{
		}
		if( pinSyncX_FadeAntiAlias.isUpdated() )
		{
		}

		// Set state of output audio pins.
		pinAudioOut.setStreaming(true);

		// Set processing method.
		setSubProcess(&ThickOscillator::subProcess);
	}

	void generateWave()
	{
		std::vector<float> nextWave;

		while (!exiting)
		{
			while (true)
			{
				if (exiting)
				{
					return;
				}

				const auto current = refreshWave.load();
				if (current != refreshWaveBG)
				{
					refreshWaveBG = current;
					break;
				}

				std::this_thread::sleep_for(std::chrono::milliseconds(10));
			}

			// assign 2 times PI to a constexp value, using math header
			constexpr float PI2 = 2.0f * M_PI;
			const auto SR = getSampleRate();
			const float nyquist = SR / 2.0f;
			const float harmonicSlope = pinPulseWidth.getValue();

#if 1
			// calculate the size of the loop which is nearest to our ideal frequency
			const float fundamentalHz = 227.0f;
			const int cyclesPerLoop = static_cast<int>(SR / fundamentalHz);
			const float loopHz = SR / cyclesPerLoop;
			loopFundamental = loopHz;

			const int loopSamples = static_cast<int>(SR);
			nextWave.assign(loopSamples, 0.0f);

			int thickness = 10;

//			float x = 1.0f;
			float harmonic = 1;
			while (true)
			{
				float freq = loopFundamental * harmonic;
				if (freq >= nyquist)
					break;

				float amplitude = 1.0f / harmonic;
				amplitude *= (std::max)(0.0f, 1.0f + harmonicSlope * freq / 440.f);

				const int totalCycles = static_cast<int>( 0.5f + cyclesPerLoop * harmonic);

				for (int cycles = totalCycles - thickness; cycles <= totalCycles + thickness; ++cycles)
				{
					double phaseOffset = (rand() % 2000) * PI2 / 2000.0;
					double ampEnv = 1.0 - (abs(cycles - totalCycles) / (float)(thickness - 1));
					for (int i = 0; i < nextWave.size(); ++i)
					{
						double phase = phaseOffset + cycles * PI2 * i / (float)loopSamples;
						nextWave[i] += sinf(phase) * amplitude * ampEnv;
					}

					phaseOffset += PI2 / 3.0;
				}

				harmonic += 1.0;
//				x += harmonicSlope;
			}

#else


			int harmonicStart = 1; // wave.size() / 2;
			int harmonicEnd = wave.size() / 2; // wave.size() / 2;
			//		float amp = 1.0f;// / harmonics;

			float fundamental = 220.0f;
			for (int i = harmonicStart; i < harmonicEnd; ++i)
			{
				if (exiting)
				{
					return;
				}

				float hz = i;
				float harmonic = hz / fundamental;
				float zigzag = fabsf(harmonic - floorf(harmonic) - 0.5f) - 0.5; // a negative series of peaks and troughs

				// no harmonic at 0 hz
				if (/*amp < 0.0f ||*/ i < fundamental * 0.5f)
				{
					continue;
				}

				float harmonicSlope = 1.0f / harmonic;

				float randomPhase = (rand() / (float)RAND_MAX) * PI2;
				for (int j = 0; j < wave.size(); ++j)
				{
					// plain envelope
					float baseEnv = (std::max)(0.0f, 1.0f - (j / (float)wave.size()));

					// envelope fading out high harmonics sooner
					float harmonicEnv = (std::max)(0.0f, 1.0f - (0.2f * harmonic) * (j / (float)wave.size()));

					// envelope going more pure

					float purity = 3.0f + (std::max)(0.0f, 20.0f * (j / (float)wave.size())); // 0 (noise) thru 20 ish (more pure)
					float amp = 1.0f + purity * zigzag;
					if (amp <= 0.0f)
					{
						continue;
					}
					amp *= harmonicSlope;

					float phase = randomPhase + ((i * j) / (float)(wave.size())) * PI2;
					wave[j] += amp * harmonicEnv * sinf(phase) * 0.1f;
				}
			}
#endif
			float max = 0.0f;
			for (auto s : nextWave)
			{
				max = (std::max)(max, fabsf(s));
			}

			float scale = 0.5f / max;
			for (auto& s : nextWave)
			{
				s *= scale;
			}

			wave.resize(nextWave.size());
			std::copy(nextWave.begin(), nextWave.end(), wave.begin());
		}
	}
};

namespace
{
	auto r = Register<ThickOscillator>::withId(L"SE Thick Oscillator");
}
