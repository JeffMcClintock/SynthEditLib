#include "../se_sdk3/mp_sdk_audio.h"
#include "../shared/real_fft.h"
#define _USE_MATH_DEFINES
#include <math.h>

using namespace gmpi;

SE_DECLARE_INIT_STATIC_FILE(ImpulseResponse2);

class ImpulseResponse2 : public MpBase2
{
	AudioInPin pinImpulsein;
	AudioInPin pinSignalin;
	IntInPin pinFreqScale;
	BlobOutPin pinResults;
	FloatOutPin pinSampleRateToGui;

	static const int FFT_SIZE = 4096;
	static const int FFT_HALF = FFT_SIZE / 2;

	float results[FFT_SIZE];
	float buffer[FFT_SIZE];   // linearised capture window: trigger spike centred at buffer[FFT_HALF]
	float history[FFT_SIZE];  // circular record of recent input, supplies the pre-trigger half

	int writePos = 0;         // next write index into history[] (circular)
	int triggerPos = -1;      // history index of the spike; -1 = armed, waiting for trigger
	int postCount = 0;        // samples still needed after the spike to fill the second half

public:
	ImpulseResponse2()
	{
		// Register pins.
		initializePin(0, pinImpulsein);
		initializePin(1, pinSignalin);
		initializePin(2, pinFreqScale);
		initializePin(3, pinResults);
		initializePin(4, pinSampleRateToGui);

		memset(results, 0, sizeof(results));
		memset(history, 0, sizeof(history));
		// todo? SetFlag(UGF_POLYPHONIC_AGREGATOR|UGF_VOICE_MON_IGNORE);
	}

	void subProcess(int sampleFrames)
	{
		auto signal = getBuffer(pinSignalin);
		auto impulse = getBuffer(pinImpulsein);

		for (int s = 0; s < sampleFrames; ++s)
		{
			// Continuously record the input so a captured window can include material
			// from *before* the trigger. A delay-compensated linear-phase filter (e.g.
			// SINC) has a symmetric impulse response centred on the trigger, so half of
			// it arrives ahead of the spike.
			history[writePos] = signal[s];
			const int recordedPos = writePos;
			if (++writePos >= FFT_SIZE)
				writePos = 0;

			if (triggerPos < 0)
			{
				// Armed: wait for the impulse spike.
				if (impulse[s] >= 1.0f)
				{
					triggerPos = recordedPos;
					postCount = FFT_HALF; // gather the second half, centring the spike.
				}
			}
			else if (--postCount <= 0)
			{
				// Second half captured. Linearise the circular buffer so the spike lands
				// at buffer[FFT_HALF], with FFT_HALF samples either side of it.
				const int start = (triggerPos - FFT_HALF) & (FFT_SIZE - 1);
				for (int i = 0; i < FFT_SIZE; ++i)
					buffer[i] = history[(start + i) & (FFT_SIZE - 1)];

				printResult();
				triggerPos = -1; // re-arm for the next impulse.
			}
		}
	}

	void onSetPins()
	{
		// Set processing method.
		SET_PROCESS2(&ImpulseResponse2::subProcess);
	}

	void printResult()
	{
		pinSampleRateToGui.setValue(getSampleRate(), 0);

		if (pinFreqScale == 0) // Impulse Response
		{
			// Odd byte-count flags 'raw impulse' to the GUI. Centre the displayed
			// window on the spike (buffer[FFT_HALF]) so both halves are visible.
			pinResults.setValueRaw(sizeof(buffer[0]) * FFT_SIZE / 2 - 1, &buffer[FFT_SIZE / 4]);
			pinResults.sendPinUpdate(0);
			return;
		}

		// no window (square window).
		// Calculate FFT.
		realft2(buffer, FFT_SIZE, 1);

		// calc power. Get magnitude of real and imaginary vectors.
		float nyquist = buffer[1]; // DC & nyquist combined into 1st 2 entries
		for (int i = 1; i < FFT_SIZE / 2; i++)
		{
			float power = buffer[2 * i] * buffer[2 * i] + buffer[2 * i + 1] * buffer[2 * i + 1];
			// square root
			power = sqrtf(power);
			results[i] = power;
		}

		results[0] = fabs(buffer[0]) / 2.f; // DC component is divided by 2
		results[FFT_SIZE / 2] = fabs(nyquist);

		// Send to GUI.
		pinResults.setValueRaw(sizeof(results[0]) * FFT_SIZE / 2, &results);
		pinResults.sendPinUpdate(0);
	}
};

namespace
{
	auto r = sesdk::Register<ImpulseResponse2>::withId(L"SE Impulse Response2");
}
