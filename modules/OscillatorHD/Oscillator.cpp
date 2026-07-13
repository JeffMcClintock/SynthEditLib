#define _USE_MATH_DEFINES
#include <math.h>
#include <array>
#include <vector>
#include <memory>
#include <cstdlib>
#include "Processor.h"
#include "OscMipmaps.h"
#include "../shared/SharedObject.h"

#undef min
#undef max

using namespace gmpi;

typedef double phasor_t;

// Whether a given per-sample input is modulated (audio-rate) or held constant.
// The template process routines are specialised on these so the per-sample work
// for unmodulated inputs optimises away to nothing.
class ModulatedPinPolicy
{
public:
	inline static void IncrementPointer(const float*& samplePointer)
	{
		++samplePointer;
	}
	enum { Active = true };
};

class NotModulatedPinPolicy
{
public:
	inline static void IncrementPointer(const float*& /*samplePointer*/)
	{
		// do nothing. Hopefully optimizes away to nothing.
	}
	enum { Active = false };
};

class OscPitchFixed : public NotModulatedPinPolicy
{
public:
	inline static void Calculate(const float* /*pitchTable*/, const float* /*pitch*/, float& /*returnIncrement*/)
	{
		// do nothing. Hopefully optimizes away to nothing.
	}
};

class OscPitchChanging : public ModulatedPinPolicy
{
public:
    const static int pitchTableLowVolts = -10; // ~ 1/60 Hz
    const static int pitchTableHiVolts = 11;  // ~ 20kHz.

    inline static float ComputeIncrement2(const float* pitchTable, float pitch)
    {
    //	float index = 12.0f * (pitch * 10.0f - (float)pitchTableLowVolts);
		constexpr float a = 12.0f * 10.0f; // 12 tone, 10V / Oct
		constexpr float b = 12.0f * static_cast<float>(pitchTableLowVolts);
		float index = pitch * a - b;
	    int table_floor = static_cast<int>(index);

	    /*
	    std:min slow compared to direct branching.
	    // not as exact as limiting pitch ( fractional part will be wrong (but harmless)
	    table_floor = std::min( table_floor, (pitchTableHiVolts - pitchTableLowVolts) * 12 );
	    table_floor = std::max( table_floor, 0 );
	    */
	    if (table_floor <= 0) // indicated index *might* be less than zero. e.g. Could be 0.1 which is valid, or -0.1 which is not.
	    {
		    if (!(index >= 0.0f)) // reverse logic to catch Nans.
		    {
			    return pitchTable[0];
		    }
	    }
	    else
	    {
		    constexpr int maxTableIndex = (pitchTableHiVolts - pitchTableLowVolts) * 12;
		    if (table_floor >= maxTableIndex)
		    {
			    return pitchTable[maxTableIndex];
		    }
	    }

	    const float fraction = index - (float)table_floor;

	    // Cubic interpolator.
	    assert(table_floor >= 0 && table_floor <= (pitchTableHiVolts - pitchTableLowVolts) * 12);

		const float y0 = pitchTable[table_floor - 1];
		const float y1 = pitchTable[table_floor + 0];
		const float y2 = pitchTable[table_floor + 1];
		const float y3 = pitchTable[table_floor + 2];

	    return y1 + 0.5f * fraction*(y2 - y0 + fraction*(2.0f*y0 - 5.0f*y1 + 4.0f*y2 - y3 + fraction*(3.0f*(y1 - y2) + y3 - y0)));
    }
    inline static void Calculate(const float* pitchTable, const float* pitch, float& returnIncrement)
	{
		returnIncrement = ComputeIncrement2(pitchTable, *pitch);
	}
};

class phaseModulationFixed : public NotModulatedPinPolicy
{
};

class phaseModulationChanging : public ModulatedPinPolicy
{
};

class WaveReadoutNormal : public NotModulatedPinPolicy
{
public:
	inline static void CalcPW(const float* /*pulseWidth*/, float /*returnPW*/)
	{
	}
	inline static phasor_t CalcPhaseB(phasor_t /*count*/, float /*pw*/)
	{
		return 0;
	}
	inline static float ModifySign(float sample)
	{
		return sample;
	}
};

class WaveReadoutInverted : public NotModulatedPinPolicy // For Ramp.
{
public:
	inline static void CalcPW(const float* /*pulseWidth*/, float /*returnPW*/)
	{
	}
	inline static phasor_t CalcPhaseB(phasor_t /*count*/, float /*pw*/)
	{
		return 0;
	}
	inline static float ModifySign(float sample)
	{
		return -sample;
	}
};

class WaveReadoutPulse : public ModulatedPinPolicy
{
public:
	inline static void CalcPW(const float* pulseWidth, float& returnPW)
	{
		returnPW = 0.25f - 0.25f * *pulseWidth;

		const float pwlimit = 0.0025f;
		if (returnPW < pwlimit)
			returnPW = pwlimit;
		if (returnPW > 0.5f - pwlimit)
			returnPW = 0.5f - pwlimit;
	}
	inline static phasor_t CalcPhaseB(phasor_t count, float pw)
	{
		return count - static_cast<phasor_t>(pw * 2.0f);
	}
	inline static float ModifySign(float sample)
	{
		return sample;
	}
};

struct Grain
{
	int waveSize;
	phasor_t count;
	const float* wave = {};
	int fadeIncrement;
	int fadeIndex;
	phasor_t minIncrement;
	phasor_t maxIncrement;

	Grain() : count( 0 ), waveSize( 0 ), fadeIndex( 0 ), fadeIncrement(0){};
	inline void stop()
	{
		waveSize = 0;
	}
	inline bool isActive() const
	{
		return waveSize != 0;
	}

	void PrintState() const
	{
#if 0 // defined(_DEBUG) && defined(WIN32)
	_RPT0(_CRT_WARN, "osc-HD---GRAIN------------\n");
	_RPT1(_CRT_WARN, "   waveSize %d\n    {", waveSize);
	for(int i = -4; i < (std::min)(20, waveSize + 4); ++i)
	{
		_RPT1(_CRT_WARN, " %f, ", wave[i]);
	}
	_RPT0(_CRT_WARN, "\n");
#endif
	}
};

// 4-point
struct CubicInterpolator
{
	inline static float Interpolate(phasor_t count, Grain& grain)
	{
		// Tested float accumulator with double index (no good). Both accumulator and index must be doubles.
		phasor_t index = count * (phasor_t)grain.waveSize;
		int table_floor = static_cast<int>(index);

		float fraction = (float)(index - (phasor_t)table_floor);

		table_floor &= grain.waveSize - 1;

		assert(table_floor >= 0);

		float y0 = grain.wave[table_floor - 1];
		float y1 = grain.wave[table_floor + 0];
		float y2 = grain.wave[table_floor + 1];
		float y3 = grain.wave[table_floor + 2];

		return y1 + 0.5f * fraction*(y2 - y0 + fraction * (2.0f*y0 - 5.0f*y1 + 4.0f*y2 - y3 + fraction * (3.0f * (y1 - y2) + y3 - y0)));
	}

	inline static float Interpolate(phasor_t count, int waveSize, float* wave)
	{
		// Tested float accumulator with double index (no good). Both accumulator and index must be doubles.
		phasor_t index = count * (phasor_t)waveSize;
		int table_floor = static_cast<int>(index);

		float fraction = static_cast<float>(index - static_cast<phasor_t>(table_floor));

		table_floor &= waveSize - 1;

		assert(table_floor >= 0);

		float y0 = wave[table_floor - 1];
		float y1 = wave[table_floor + 0];
		float y2 = wave[table_floor + 1];
		float y3 = wave[table_floor + 2];

		return y1 + 0.5f * fraction*(y2 - y0 + fraction * (2.0f*y0 - 5.0f*y1 + 4.0f*y2 - y3 + fraction * (3.0f * (y1 - y2) + y3 - y0)));
	}
};

// SE_DECLARE_INIT_STATIC_FILE equivalent for static library linking
#ifndef SE_DECLARE_INIT_STATIC_FILE
#define SE_DECLARE_INIT_STATIC_FILE(filename) void se_static_library_init_##filename(){}
#endif

class Oscillator : public gmpi::Processor
{
	const static int MaxGrains = 4;
	const static size_t syncCrossFadeSamples = 8;
	const static size_t syncCrossFadeTableSize = syncCrossFadeSamples + 1;

	/* not macos: constexpr*/ std::array<float, syncCrossFadeTableSize> fill_crossfade_array()
	{
		std::array<float, syncCrossFadeTableSize> v{ 0 };
		for(uint64_t i = 0; i < syncCrossFadeTableSize; ++i)
		{
			v[i] = 0.5f + 0.5f * sinf( (float)M_PI * ((float)(i + 1) / (float)(syncCrossFadeSamples + 1) - 0.5f));
		}
		return v;
	}

	Grain grains[MaxGrains];
	float increment = 0;
	float* pitchTable = {};
	float* waveData_ = {};
	std::array<float, syncCrossFadeTableSize> syncFadeCurve_ = fill_crossfade_array();
	bool syncState = false;
	float prevSync = 0.0f;
	bool firstTime = true;
	unsigned int random = rand();
	phasor_t prevPhase = 0.0f; // randomish but reproducible start.

	// pink noise stuff: 6 parallel one-pole lowpasses (Paul Kellet). The pole/gain
	// coefficients are retuned to the current sample rate in open() so the spectral
	// tilt and level stay consistent at any rate.
	static constexpr int kPinkSections = 6;
	float pinkBuf[kPinkSections] = {};
	float pinkPole[kPinkSections] = {};
	float pinkGain[kPinkSections] = {};

	// Per-sample-rate shared pitch lookup table (keeps the SharedObjectManager copy alive).
	std::shared_ptr<std::vector<float>> pitchTableShared_;

	std::shared_ptr<std::vector<MipMapCalculator::WavetableMip>> waveSawtooth;
	std::shared_ptr<std::vector<MipMapCalculator::WavetableMip>> waveTriangle;
	std::shared_ptr<std::vector<MipMapCalculator::WavetableMip>> waveSine;

	int zeroSamplesCounter = 0;

	// Pins in XML pin-index order (pins auto-register in member declaration order).
	gmpi::AudioInPin pinPitch;         // 0
	gmpi::AudioInPin pinPulseWidth;    // 1
	gmpi::EnumInPin pinWaveform;       // 2
	gmpi::AudioInPin pinSync;          // 3
	gmpi::AudioInPin pinPhaseMod;      // 4
	gmpi::AudioInPin pinPmDepthDummy;  // 5 - private "PM Depth dmy", unused.
	gmpi::BoolInPin pinBypass;         // 6
	gmpi::EnumInPin pinDcoMode;        // 7
	gmpi::AudioOutPin pinSignalOut;    // 8
	gmpi::FloatInPin pinVoiceActive;   // 9

	bool previousActiveState = false;

public:
	enum EWaveShape{ WS_SINE, WS_SAW, WS_RAMP, WS_TRI, WS_PULSE, WS_WHITE_NOISE, WS_PINK_NOISE};

	Oscillator() = default;
	gmpi::ReturnCode open(gmpi::api::IUnknown* phost) override;

	void startGrain( phasor_t initCount, float increment, int fadeIncrement = 0);

	const static int maxVolts = 10;
	inline float SampleToVoltage(float s)
	{
		return s * (float)maxVolts;
	}
	inline float SampleToFrequency(float volts)
	{
		return 440.f * powf(2.f, SampleToVoltage(volts) - (float)maxVolts * 0.5f);
	}
	inline float ComputeIncrement(float SampleRate, float pitch)
	{
		return SampleToFrequency(pitch) / SampleRate;
	}

	void sub_process_white_noise( int sampleFrames );
	void sub_process_pink_noise( int sampleFrames );
	void sub_process_silence(int sampleFrames);

	inline void DoSync2(float phase, float sync, float prevSync)
	{
		for (int g2 = 0; g2 < MaxGrains; ++g2)
		{
			if (grains[g2].waveSize != 0 && grains[g2].fadeIncrement != -1) // indicates active grain.
			{
				grains[g2].fadeIncrement = -1;
				grains[g2].fadeIndex = syncCrossFadeSamples - 1;
			}
		}

		float count = increment * sync / (sync - prevSync);
		startGrain(100.0 + count - phase, increment, 1); // 5 is to guard against count going negative when phase is high.
	}

	template< class WaveShapePolicy, class PitchModulationPolicy, class phaseModulationPolicy, class SyncModulationPolicy, class InterpolationPolicy >
	void sub_process_template2(int sampleFrames)
	{
		// get pointers to in/output buffers.
		float* signalOut = getBuffer(pinSignalOut);
		const float* pitch = PitchModulationPolicy::Active ? getBuffer(pinPitch) : nullptr;
		const float* sync = SyncModulationPolicy::Active ? getBuffer(pinSync) : nullptr;
		const float* phase = getBuffer(pinPhaseMod); // Need phase available for sync regardless of being modulated or not.
		const float* pulseWidth = WaveShapePolicy::Active ? getBuffer(pinPulseWidth) : nullptr;

		phasor_t delataPhase = 0;
		float pw = 0;

		for (int s = sampleFrames; s > 0; --s)
		{
			PitchModulationPolicy::Calculate(pitchTable, pitch, increment);

			if (phaseModulationPolicy::Active)
			{
				delataPhase = prevPhase - *phase;
				prevPhase = *phase;
			}

			// trigger sync?
			if (SyncModulationPolicy::Active)
			{
				if ((*sync > 0.0f) != syncState)
				{
					syncState = *sync > 0.0f;
					if (syncState)
					{
						DoSync2(*phase + (float)delataPhase, *sync, prevSync);
					}
				}
				prevSync = *sync;
				++sync;
			}

			float samp = 0.0f;
			for (int g = 0; g < MaxGrains; ++g)
			{
				if (grains[g].waveSize)
				{
					WaveShapePolicy::CalcPW(pulseWidth, pw);

					float grainSample = InterpolationPolicy::Interpolate(grains[g].count, grains[g]);

					if constexpr(WaveShapePolicy::Active) // i.e. is - pulsewave.
					{
						const auto c = WaveShapePolicy::CalcPhaseB(grains[g].count, pw);
						grainSample = InterpolationPolicy::Interpolate(c, grains[g]) - grainSample;
					}

					if (grains[g].fadeIncrement != 0) // This grain being rapid-faded?
					{
						grainSample *= syncFadeCurve_[grains[g].fadeIndex];

						if (grains[g].fadeIncrement > 0)
						{
							if (grains[g].fadeIndex == syncCrossFadeSamples - 1)
							{
								grains[g].fadeIncrement = 0; // indicates steady active grain.
							}
							grains[g].fadeIndex += 1;
						}
						else
						{
							if (grains[g].fadeIndex == 0)
							{
								grains[g].waveSize = 0; // indicates inactive grain.
							}
							grains[g].fadeIndex -= 1;
						}
					}

					samp += WaveShapePolicy::ModifySign(grainSample);

					grains[g].count += increment;

					if (phaseModulationPolicy::Active)
					{
						grains[g].count += delataPhase;
						if (grains[g].count < 0.0f) // wrapped -ve?
						{
							grains[g].count += 10.0f;
						}
					}
				}
			}
			*signalOut = samp;

			// Increment buffer pointers.
			PitchModulationPolicy::IncrementPointer(pitch);
			phaseModulationPolicy::IncrementPointer(phase);
			WaveShapePolicy::IncrementPointer(pulseWidth);
			++signalOut;
		}

		int activeGrains = 0;
		for (int g = 0; g < MaxGrains; ++g)
		{
			if (grains[g].waveSize)
			{
				++activeGrains;
				if (grains[g].count >= 1.0)
				{
					// count wraps at 1.0
					int count_floor = static_cast<int>(grains[g].count);
					grains[g].count -= (phasor_t)count_floor;

					if ((grains[g].maxIncrement < increment || grains[g].minIncrement > increment) && grains[g].fadeIncrement == 0)
					{
						// Fade out old grain.
						grains[g].fadeIncrement = -1;
						grains[g].fadeIndex = syncCrossFadeSamples - 1;

						// Fade in new one at new MIP level.
						startGrain(grains[g].count, (float)increment, 1);
					}
				}
			}
		}

		if (activeGrains == 1)
		{
			ChooseProcessMethod();
		}
	}

	template< class WaveShapePolicy, class PitchModulationPolicy, class phaseModulationPolicy, class InterpolationPolicy >
	void sub_process_template2fast(int sampleFrames)
	{
		// get pointers to in/output buffers.
		float* signalOut = getBuffer(pinSignalOut);
		const float* pitch = PitchModulationPolicy::Active ? getBuffer(pinPitch) : nullptr;
		const float* phase = phaseModulationPolicy::Active ? getBuffer(pinPhaseMod) : nullptr;
		const float* pulseWidth = WaveShapePolicy::Active ? getBuffer(pinPulseWidth) : nullptr;

		phasor_t delataPhase = 0;
		float pw = 0;

		for (int s = sampleFrames; s > 0; --s)
		{
			PitchModulationPolicy::Calculate(pitchTable, pitch, increment);

			if (phaseModulationPolicy::Active)
			{
				delataPhase = prevPhase - *phase;
				prevPhase = *phase;
			}

#ifdef _DEBUG
			assert(grains[0].waveSize != 0);
			for (int g = 1; g < MaxGrains; ++g)
			{
				assert(grains[g].waveSize == 0);
			}
#endif
			float samp;
			{
				const int g = 0;
				assert(grains[g].waveSize);
				{
					WaveShapePolicy::CalcPW(pulseWidth, pw);

					samp = InterpolationPolicy::Interpolate(grains[g].count, grains[g]);

					if (WaveShapePolicy::Active) // i.e. is - pulsewave.
					{
						samp = InterpolationPolicy::Interpolate(WaveShapePolicy::CalcPhaseB(grains[g].count, pw), grains[g]) - samp;
					}

					assert(grains[g].fadeIncrement == 0); // This grain being rapid-faded?

					samp = WaveShapePolicy::ModifySign(samp);

					grains[g].count += increment;

					if (phaseModulationPolicy::Active)
					{
						grains[g].count += delataPhase;
						if (grains[g].count < 0.0f) // wrapped -ve?
						{
							grains[g].count += 10.0f;
						}
					}
				}
			}
			*signalOut = samp;

			// Increment buffer pointers.
			++signalOut;
			PitchModulationPolicy::IncrementPointer(pitch);
			phaseModulationPolicy::IncrementPointer(phase);
			WaveShapePolicy::IncrementPointer(pulseWidth);
		}

		{
			const int g = 0;
			assert(grains[g].waveSize);
			{
				if (grains[g].count >= 1.0)
				{
					// count wraps at 1.0
					int count_floor = static_cast<int>(grains[g].count);

					grains[g].count -= (phasor_t)count_floor;

					if ((grains[g].maxIncrement < increment || grains[g].minIncrement > increment) && grains[g].fadeIncrement == 0)
					{
						// Fade out old grain.
						grains[g].fadeIncrement = -1;
						grains[g].fadeIndex = syncCrossFadeSamples - 1;

						// Fade in new one at new MIP level.
						startGrain(grains[g].count, (float)increment, 1);
						ChooseProcessMethod();
					}
				}
			}
		}
	}

	void recalculatePitch();
	void resetOscillator();
	void doSync(bool newSyncState);
	void onSetPins() override;
	void ChooseProcessMethod();
};

SE_DECLARE_INIT_STATIC_FILE(Oscillator);

ReturnCode Oscillator::open(api::IUnknown* phost)
{
	const auto r = Processor::open(phost);

	const float sampleRate = host->getSampleRate();

	// Retune the pink-noise filter to the current sample rate. The reference
	// coefficients (Paul Kellet) are tuned for 44100 Hz. Each section is a one-pole
	// lowpass, so we keep every section's cutoff (in Hz) fixed by mapping the pole
	// pole_new = pole_ref^(44100/fs), and scale the gain to hold the section's
	// response shape (g/(1-pole) constant). The extra sqrt(fs/44100) factor
	// compensates for white noise's per-Hz power thinning out as fs rises, so the
	// audible pink level is the same at any rate. At exactly 44100 Hz this reduces
	// to the original coefficients bit-for-bit.
	{
		constexpr float kRefRate = 44100.0f;
		constexpr float refPole[kPinkSections] = { 0.997f, 0.985f, 0.950f, 0.850f, 0.620f, 0.250f };
		constexpr float refGain[kPinkSections] = { 0.029591f, 0.032534f, 0.048056f, 0.090579f, 0.108990f, 0.255784f };
		const float levelComp = sqrtf(sampleRate / kRefRate);
		for (int i = 0; i < kPinkSections; ++i)
		{
			pinkPole[i] = powf(refPole[i], kRefRate / sampleRate);
			pinkGain[i] = refGain[i] * (1.0f - pinkPole[i]) / (1.0f - refPole[i]) * levelComp;
		}
	}

	// 20kHz is about 10.5 Volts. 1Hz is about -3.7 volts. 0.01Hz = -10V
	// -4 -> 11 Volts should cover most posibilities. 15V Range. 12 entries per volt = 180 entries.
	const int extraEntriesAtStart = 1; // for interpolator.
	const int extraEntriesAtEnd = 3; // for interpolator.
	const int pitchTableSize = extraEntriesAtStart + extraEntriesAtEnd + (OscPitchChanging::pitchTableHiVolts - OscPitchChanging::pitchTableLowVolts) * 12;
	const float oneSemitone = 1.0f / 12.0f;

	// Pitch lookup table - shared across instances at the same sample rate.
	pitchTableShared_ = SharedObjectManager<std::vector<float>>::getOrCreateSharedMemory(
		sampleRate,
		0,
		[this, pitchTableSize, extraEntriesAtStart, oneSemitone](float sr) -> std::shared_ptr<std::vector<float>>
		{
			auto p = std::make_shared<std::vector<float>>(pitchTableSize);
			const float overSampleRate = 1.0f / sr;
			for (int i = 0; i < pitchTableSize; ++i)
			{
				const float pitch = (OscPitchChanging::pitchTableLowVolts + (i - extraEntriesAtStart) * oneSemitone) * 0.1f;
				const float hz = SampleToFrequency(pitch);
				(*p)[i] = hz * overSampleRate;
			}
			return p;
		});

	// Shift apparent start of table to entry #1, so we can access table[-1] without hassle.
	pitchTable = pitchTableShared_->data() + extraEntriesAtStart;

	// TODO release mem after period of inactivity.
	waveTriangle = SharedObjectManager< std::vector<MipMapCalculator::WavetableMip> >::getOrCreateSharedMemory(
		sampleRate,
		WS_TRI,
		[](float sampleRate) -> std::shared_ptr<std::vector<MipMapCalculator::WavetableMip>>
		{
			auto TriangleSpectrum = [](int partial) -> std::tuple<float, float>
			{
				constexpr float scale = 4.0f / (float)(M_PI * M_PI); // scale to 5V.

				if(partial == 0)
				{
					return { 0.0f, 0.0f }; // DC and nyquist
				}
				else
				{
					if((partial & 0x01) == 0)
					{
						return { 0.0f, 0.0f };
					}

					float level = scale / (partial * partial);
					if((partial >> 1) & 1) // every 2nd harmonic inverted
					{
						level = -level;
					}
					return { 0.0f, level };
				}
			};

			const auto mips = MipMapCalculator::CalcMips(sampleRate, TriangleSpectrum);
			MipMapCalculator::PrintMips(sampleRate, mips, "Triangle");
			return MipMapCalculator::generateWavetable(mips, TriangleSpectrum);;
		}
	);

	waveSawtooth = SharedObjectManager< std::vector<MipMapCalculator::WavetableMip> >::getOrCreateSharedMemory(
		sampleRate,
		WS_SAW,
		[](float sampleRate) -> std::shared_ptr<std::vector<MipMapCalculator::WavetableMip>>
		{
			auto sawToothSpectrum = [](int partial) -> std::tuple<float, float>
			{
				constexpr float scale = -1.0f / M_PI;

				if(partial == 0)
				{
					return { 0.0f, 0.0f }; // DC and nyquist
				}
				else
				{
					return { 0.0f, scale / partial };
				}
			};

			const auto mips = MipMapCalculator::CalcMips(sampleRate, sawToothSpectrum);
			MipMapCalculator::PrintMips(sampleRate, mips, "SawTooth");
			return MipMapCalculator::generateWavetable(mips, sawToothSpectrum);
		}
		);

	waveSine = SharedObjectManager< std::vector<MipMapCalculator::WavetableMip> >::getOrCreateSharedMemory(
		sampleRate,
		WS_SINE,
		[](float sampleRate) -> std::shared_ptr<std::vector<MipMapCalculator::WavetableMip>>
		{
			auto sineSpectrum = [](int partial) -> std::tuple<float, float>
			{
				if(partial == 1)
				{
					return { 0.0f, 0.5f };
				}
				else
				{
					return { 0.0f, 0.0f }; // DC and nyquist and all other harmonics.
				}
			};

			const auto mips = MipMapCalculator::CalcMips(sampleRate, sineSpectrum);
			MipMapCalculator::PrintMips(sampleRate, mips, "Sine");
			return MipMapCalculator::generateWavetable(mips, sineSpectrum);
		}
	);


	setSubProcess(&Oscillator::sub_process_silence);

	return r;
}

typedef void (Oscillator::* OscProcess_ptr)(int sampleFrames);

#define TPB( wave, pitch, phase, sync ) (&Oscillator::sub_process_template2< wave, pitch, phase, sync, CubicInterpolator > )

const OscProcess_ptr ProcessSelection2[3][2][2][2] =
{
	TPB(WaveReadoutNormal, OscPitchFixed, phaseModulationFixed, NotModulatedPinPolicy),
	TPB(WaveReadoutNormal, OscPitchFixed, phaseModulationFixed, ModulatedPinPolicy),
	TPB(WaveReadoutNormal, OscPitchFixed, phaseModulationChanging, NotModulatedPinPolicy),
	TPB(WaveReadoutNormal, OscPitchFixed, phaseModulationChanging, ModulatedPinPolicy),
	TPB(WaveReadoutNormal, OscPitchChanging, phaseModulationFixed, NotModulatedPinPolicy),
	TPB(WaveReadoutNormal, OscPitchChanging, phaseModulationFixed, ModulatedPinPolicy),
	TPB(WaveReadoutNormal, OscPitchChanging, phaseModulationChanging, NotModulatedPinPolicy),
	TPB(WaveReadoutNormal, OscPitchChanging, phaseModulationChanging, ModulatedPinPolicy),

	TPB(WaveReadoutInverted, OscPitchFixed   , phaseModulationFixed, NotModulatedPinPolicy),
	TPB(WaveReadoutInverted, OscPitchFixed   , phaseModulationFixed, ModulatedPinPolicy),
	TPB(WaveReadoutInverted, OscPitchFixed   , phaseModulationChanging, NotModulatedPinPolicy),
	TPB(WaveReadoutInverted, OscPitchFixed   , phaseModulationChanging, ModulatedPinPolicy),
	TPB(WaveReadoutInverted, OscPitchChanging, phaseModulationFixed, NotModulatedPinPolicy),
	TPB(WaveReadoutInverted, OscPitchChanging, phaseModulationFixed, ModulatedPinPolicy),
	TPB(WaveReadoutInverted, OscPitchChanging, phaseModulationChanging, NotModulatedPinPolicy),
	TPB(WaveReadoutInverted, OscPitchChanging, phaseModulationChanging, ModulatedPinPolicy),

	TPB(WaveReadoutPulse, OscPitchFixed   , phaseModulationFixed, NotModulatedPinPolicy),
	TPB(WaveReadoutPulse, OscPitchFixed   , phaseModulationFixed, ModulatedPinPolicy),
	TPB(WaveReadoutPulse, OscPitchFixed   , phaseModulationChanging, NotModulatedPinPolicy),
	TPB(WaveReadoutPulse, OscPitchFixed   , phaseModulationChanging, ModulatedPinPolicy),
	TPB(WaveReadoutPulse, OscPitchChanging, phaseModulationFixed, NotModulatedPinPolicy),
	TPB(WaveReadoutPulse, OscPitchChanging, phaseModulationFixed, ModulatedPinPolicy),
	TPB(WaveReadoutPulse, OscPitchChanging, phaseModulationChanging, NotModulatedPinPolicy),
	TPB(WaveReadoutPulse, OscPitchChanging, phaseModulationChanging, ModulatedPinPolicy),
};

#define TPC( wave, pitch, phase ) (&Oscillator::sub_process_template2fast< wave, pitch, phase, CubicInterpolator > )

const OscProcess_ptr ProcessSelection2fast[3][2][2] =
{
	TPC(WaveReadoutNormal, OscPitchFixed, phaseModulationFixed),
	TPC(WaveReadoutNormal, OscPitchFixed, phaseModulationChanging),
	TPC(WaveReadoutNormal, OscPitchChanging, phaseModulationFixed),
	TPC(WaveReadoutNormal, OscPitchChanging, phaseModulationChanging),

	TPC(WaveReadoutInverted, OscPitchFixed   , phaseModulationFixed),
	TPC(WaveReadoutInverted, OscPitchFixed   , phaseModulationChanging),
	TPC(WaveReadoutInverted, OscPitchChanging, phaseModulationFixed),
	TPC(WaveReadoutInverted, OscPitchChanging, phaseModulationChanging),

	TPC(WaveReadoutPulse, OscPitchFixed   , phaseModulationFixed),
	TPC(WaveReadoutPulse, OscPitchFixed   , phaseModulationChanging),
	TPC(WaveReadoutPulse, OscPitchChanging, phaseModulationFixed),
	TPC(WaveReadoutPulse, OscPitchChanging, phaseModulationChanging),
};

void Oscillator::recalculatePitch()
{
	float pitch = pinPitch;
	OscPitchChanging::Calculate(pitchTable, &pitch, increment);
}

void Oscillator::resetOscillator()
{
	float increment;
	float pitch = pinPitch;
	OscPitchChanging::Calculate(pitchTable, &pitch, increment);

	for (int g = 0; g < MaxGrains; ++g)
	{
		grains[g].waveSize = 0;
	}

	startGrain(0.0, increment);
}

void Oscillator::doSync(bool newSyncState)
{
	syncState = newSyncState;

	if (syncState)
	{
		prevSync = -1; // prevent numeric junk.
		DoSync2(pinPhaseMod, pinSync, -1.0f);
		ChooseProcessMethod();
	}
}

void Oscillator::onSetPins(void)
{
	if (pinWaveform.isUpdated())
		resetOscillator();

	auto pitchUpdated = pinPitch.isUpdated() && !pinPitch.isStreaming();

	if (pitchUpdated || pinBypass.isUpdated())
	{
		if (!pinBypass)
			recalculatePitch();
	}

	// handle static phase offset
	if (pinPhaseMod.isUpdated() && !pinPhaseMod.isStreaming())
	{
		const auto phaseMod = pinPhaseMod.getValue();
		const double delataPhase = prevPhase - phaseMod;
		prevPhase = phaseMod;

		for (auto& grain : grains)
		{
			if (grain.waveSize)
			{
				grain.count += delataPhase;
				if (grain.count < 0.0f) // wrapped -ve?
				{
					grain.count += 10.0f;
				}
			}
		}

//		assert(accumulator > 0.0f);
	}

	// Any update on pinTrigger is a trigger. Actual pin value is garbage.
	if (pinVoiceActive.isUpdated())
	{
		bool newActiveState = pinVoiceActive > 0.0f;

		if (newActiveState && !previousActiveState && pinDcoMode != 0)
		{
			resetOscillator();
			ChooseProcessMethod();
		}

		previousActiveState = newActiveState;
	}

	if (pinSync.isUpdated() && !pinSync.isStreaming())
	{
		bool newSyncState = pinSync > 0.0f;

		if (newSyncState != syncState)
		{
			doSync(newSyncState);
		}
	}

	ChooseProcessMethod();

	pinSignalOut.setStreaming(!pinBypass);

	if(pinBypass.isUpdated() && !pinBypass)
		zeroSamplesCounter = 0;
}

void Oscillator::ChooseProcessMethod()
{
	if (pinBypass)
	{
		setSubProcess(&Oscillator::sub_process_silence);
	}
	else
	{
		int wavetype = -1;

		switch (pinWaveform)
		{
		case WS_WHITE_NOISE:
			setSubProcess(&Oscillator::sub_process_white_noise);
			break;

		case WS_PINK_NOISE:
			setSubProcess(&Oscillator::sub_process_pink_noise);
			break;

		case WS_SINE:
		case WS_SAW:
		case WS_TRI:
			wavetype = 0;	// normal
			break;

		case WS_RAMP:
			wavetype = 1;	// inverted
			break;

		case WS_PULSE:
			wavetype = 2;	// Pulse
			break;

		default:
			break;
		}

		if (wavetype > -1)
		{
			// Fast mode can be used if only one grain active at a steady level.
			 bool fastMode = grains[0].waveSize != 0 && grains[0].fadeIncrement == 0 && !pinSync.isStreaming();
			for (int g = 1; g < MaxGrains; ++g)
			{
				fastMode &= grains[g].waveSize == 0;
			}

			if (fastMode)
			{
				setSubProcess(static_cast <SubProcessPtr> (ProcessSelection2fast[wavetype][pinPitch.isStreaming()][pinPhaseMod.isStreaming()]));
			}
			else
			{
				setSubProcess(static_cast <SubProcessPtr> (ProcessSelection2[wavetype][pinPitch.isStreaming()][pinPhaseMod.isStreaming()][pinSync.isStreaming()]));
			}
		}
	}
}

void Oscillator::startGrain( phasor_t initCount, float increment, int fadeIncrement)
{
	assert(initCount >= 0.0 && initCount < 200.0); // negative phases seem OK EDIT: Nope. results in negative table index on lookup sample.

	// Only matters for one-shot, wrap count to range 0 - 1
	auto count_floor = static_cast<int>(initCount);
	initCount -= (phasor_t)count_floor;

	// start a fresh grain.
	for (int g = MaxGrains - 1; g > 0; --g)
	{
		grains[g] = grains[g-1];
	}

	const int g = 0;
	grains[g].count = initCount; // Make grain phase fractionally correct.
	grains[g].fadeIncrement = fadeIncrement;
	if( fadeIncrement == 1 )
	{
		grains[g].fadeIndex = -1;
	}

	grains[g].fadeIndex += grains[g].fadeIncrement;

	std::vector<MipMapCalculator::WavetableMip>* mipMap;

	switch (pinWaveform)
	{
	case WS_SINE:
		mipMap = waveSine.get();
		break;
	case WS_TRI:
		mipMap = waveTriangle.get();
		break;
	default:
		mipMap = waveSawtooth.get();
		break;
	}

	const auto mip = CalcMipLevel(*mipMap, increment);

	grains[g].wave = mip->GetWave(); // TODO arrrange layout of mip same as grain.
	grains[g].maxIncrement = mip->maximumIncrement;
	grains[g].minIncrement = mip->minimumIncrement;
	grains[g].waveSize = mip->GetWaveSize();

	grains[g].PrintState();
}

void Oscillator::sub_process_white_noise( int sampleFrames )
{
	float* signalOut = getBuffer( pinSignalOut );

	unsigned int itemp;
	unsigned int idum = random;
	const unsigned int jflone = 0x3f800000; // see 'numerical recipies in c' pg 285
	const unsigned int jflmsk = 0x007fffff;

	for( int s = sampleFrames; s > 0; --s )
	{
		idum = idum * 1664525L + 1013904223L;// use mask to quickly convert integer to float between -0.5 and 0.5
		itemp = jflone | (jflmsk & idum);
		*signalOut++ = (*(float*)&itemp) - 1.5f;
	}

	random = idum; // store new random number
}

void Oscillator::sub_process_pink_noise( int sampleFrames )
{
	float* signalOut = getBuffer( pinSignalOut );

	unsigned int itemp;
	unsigned int idum = random;
	const unsigned int jflone = 0x3f800000; // see 'numerical recipies in c' pg 285
	const unsigned int jflmsk = 0x007fffff;

	for( int s = sampleFrames; s > 0; --s )
	{
		idum = idum * 1664525L + 1013904223L;
		// use mask to quickly convert integer to float between -0.5 and 0.5
		itemp = jflone | (jflmsk & idum);
		float white = (*(float*)&itemp) - 1.5f;

		// filtering white noise (coefficients retuned for the sample rate in open()).
		float pink = 0.0f;
		for (int i = 0; i < kPinkSections; ++i)
		{
			pinkBuf[i] = pinkPole[i] * pinkBuf[i] + pinkGain[i] * white;
			pink += pinkBuf[i];
		}
		*signalOut++ = pink;
	}

	random = idum; // store new random number
}

void Oscillator::sub_process_silence(int sampleFrames)
{
	if (zeroSamplesCounter > host->getBlockSize())
	{
		setSubProcess(&Oscillator::subProcessNothing);
	}
	else
	{
		auto signalOut = getBuffer(pinSignalOut);

		for (int s = sampleFrames; s > 0; --s)
		{
			*signalOut++ = 0.f;
		}

		zeroSamplesCounter += sampleFrames;
	}
}

namespace
{
	bool registered = Register<Oscillator>::withXml(R"XML(
<?xml version="1.0" ?>
<PluginList>
  <Plugin id="SE Oscillator4" name="Oscillator HD" category="Waveform">
    <Audio>
      <Pin name="Pitch" datatype="float" rate="audio" default="0.5" />
      <Pin name="Pulse Width" datatype="float" rate="audio" default="0.5" />
      <Pin name="Waveform" datatype="enum" default="1" metadata="Sine, Saw, Ramp, Triangle, Pulse, White Noise, Pink Noise" />
      <Pin name="Sync" datatype="float" rate="audio" metadata="100, 0, 5, -5" />
      <Pin name="Phase Mod" datatype="float" rate="audio" />
      <Pin name="PM Depth dmy" datatype="float" rate="audio" private="true" />
      <Pin name="Disable" datatype="bool" default="false"/>
      <Pin name="Reset Mode" datatype="enum" metadata="VCO (Freerun),DCO (Sync)" />
      <Pin name="Audio Out" datatype="float" rate="audio" direction="out" />
      <Pin name="VoiceActive" hostConnect="Voice/Active" datatype="float" isPolyphonic="true" />
    </Audio>
  </Plugin>
</PluginList>
)XML");
}
