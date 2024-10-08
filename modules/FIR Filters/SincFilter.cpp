#define _USE_MATH_DEFINES
#include <math.h>
#include <algorithm>
#include <numeric>
#include "Sinc.h"

#include "./SincFilter.h"
#include "mp_sdk_gui2.h"

REGISTER_PLUGIN2(SincFilterLpHp, L"SE Sinc Lowpass Filter");
REGISTER_PLUGIN2(SincFilterHp, L"SE Sinc Highpass Filter");

SincFilterLpHp::SincFilterLpHp( )
{
	// Register pins.
	initializePin(pinSignal);
	initializePin(pinCuttoffkHz);
	initializePin(pinTaps);
	initializePin(pinOutput);
}

void SincFilterLpHp::subProcess(int sampleFrames)
{
	// get pointers to in/output buffers.
	const float* signal = getBuffer(pinSignal);
	float* __restrict output = getBuffer(pinOutput);

	const auto numCoefs = coefs.size();

	// maintain history as contiguous samples by shifting left each time.
	std::copy(signal, signal + sampleFrames, hist.begin() + numCoefs);

#ifndef GMPI_SSE_AVAILABLE

	// Process first coefs (copy).
	float* h = hist.data();
	auto out = output;
	for (int s = 0; s < sampleFrames; ++s)
	{
		*out++ = h[s] * coefs[0];
	}
	++h;

	// Process remaining coefs (add).
	for (int t = 1; t < numCoefs; ++t)
	{
		out = output;
		for (int s = 0; s < sampleFrames; ++s)
		{
			*out++ += h[s] * coefs[t];
		}
		++h;
	}

#else
#if 1

	// Process first coefs (copy to output).
	const float* __restrict h = hist.data();
#if 0
	{
		float* out = output;
		float* hi = h;

		// process fiddly non-sse-aligned prequel.
		int s{};
		for (; s < sampleFrames && reinterpret_cast<intptr_t>(out) & 0x0f ; ++s)
		{
			*out++ = *hi++ * coefs[0];
		}

		const __m128 tap = _mm_set_ps1(coefs[0]);
		for (; s < sampleFrames; s += 4)
		{
			_mm_storeu_ps(out, _mm_mul_ps(_mm_loadu_ps(hi), tap));
			hi += 4;
			out += 4;
		}
	}
#else
	{
		const auto coef = coefs[0];
		std::transform(h, h + sampleFrames, output, [coef](auto& n) {return coef * n; });
	}
#endif

	// Process remaining coefs (add to output).
	{
		for (unsigned int t = 1; t < numCoefs; ++t)
		{
			++h; // shift window right

#if 0
			// 1.5% CPU
			const auto& coef = coefs[t];
			std::transform(h, h + sampleFrames, output, output, [coef](const float& n, const float& o) {return o + coef * n; });
#else
			// 0.26% CPU
			const float* hi = h;
			float* out = output;

			// process fiddly non-sse-aligned prequel.
			int s{};
			for (; s < sampleFrames && reinterpret_cast<intptr_t>(out) & 0x0f; ++s)
			{
				*out++ = *hi++ * coefs[t];
				s++;
			}

			const __m128 tap = _mm_set_ps1(coefs[t]);

			for (; s < sampleFrames; s += 4)
			{
				// output[s] += h[s] * taps[t];
				_mm_storeu_ps(out, _mm_add_ps(_mm_loadu_ps(out), _mm_mul_ps(_mm_loadu_ps(hi), tap)));
				hi += 4;
				out += 4;
			}
#endif
		}
	}

#else
	// FAULTY: WRITES OVER END OF BUFFER becuase it does not align writes to SSE boundary, just goes to the last sample plus (up to) 3 more. which is too far when buffer ALREADY aligns on SSE
	// 
	// Process 4 samples at a time.
	float* h = hist.data();

	// Process first coefs (copy).
	float* h2 = h++;
	__m128 tap = _mm_set_ps1(coefs[0]);
	float* out = output;
	for (int s = 0; s < sampleFrames; s += 4)
	{
		_mm_storeu_ps(out, _mm_mul_ps(_mm_loadu_ps(h2), tap));
		h2 += 4;
		out += 4;
	}


	// Process remaining coefs (add).
	for (unsigned int t = 1; t < numCoefs; ++t)
	{
		h2 = h++;
		tap = _mm_set_ps1(coefs[t]);
		out = output;
		for (int s = 0; s < sampleFrames; s += 4)
		{
			// output[s] += h[s] * taps[t];
			_mm_storeu_ps(out, _mm_add_ps(_mm_loadu_ps(out), _mm_mul_ps(_mm_loadu_ps(h2), tap)));
			h2 += 4;
			out += 4;
		}
	}

#endif
#endif

	// shift history.
	std::copy(hist.begin() + sampleFrames, hist.begin() + sampleFrames + numCoefs, hist.begin());
}

void SincFilterLpHp::subProcessStatic(int sampleFrames)
{
	subProcess(sampleFrames);

	if (staticCount < sampleFrames && pinOutput.isStreaming())
	{
		assert(staticCount >= 0);
		pinOutput.setStreaming(false, getBlockPosition() + staticCount);
	}

	staticCount -= sampleFrames;
}

int calcAlignedTaps(int selectedTaps)
{
	const int sseAlign = 4;
	const int alignedTaps = ((std::max)(1, selectedTaps) + sseAlign - 1) & -(sseAlign);
	return alignedTaps;
}

void SincFilterLpHp::onSetPins()
{
	if (pinTaps.isUpdated() || pinCuttoffkHz.isUpdated() )
	{
		double cuttoff = 1000.0f * pinCuttoffkHz / getSampleRate();
		const int alignedTaps = calcAlignedTaps(pinTaps.getValue());
		coefs.resize(alignedTaps);
		calcWindowedSinc(cuttoff, isHighPass(), alignedTaps, coefs.data());

		const auto blockSize = host.getBlockSize();
		const int numCoefs = static_cast<int>(coefs.size());
		hist.assign(numCoefs + blockSize, 0.0f);

		host.SetLatency(alignedTaps / 2);
	}

	// Set state of output audio pins.
	pinOutput.setStreaming(true);

	// Set processing method.
	if (pinSignal.isStreaming())
	{
		setSubProcess(&SincFilterLpHp::subProcess);
	}
	else
	{
		staticCount = static_cast<int>(hist.size());
		setSubProcess(&SincFilterLpHp::subProcessStatic);
	}
}

#if 0 // deprecated. use host.SetLatency() from processor
class SincFilterController : public GmpiSdk::Controller
{
public:
	int32_t setPinDefault(int32_t pinType, int32_t pinId, const char* defaultValue) override
	{
		if (pinType == 0 && pinId == 2) // taps
		{
			const auto tapCount = atoi(defaultValue);
			const int alignedTaps = calcAlignedTaps(tapCount);
			getHost()->setLatency(alignedTaps / 2);
		}

		return GmpiSdk::Controller::setPinDefault(pinType, pinId, defaultValue);
	}

	GMPI_REFCOUNT;
	GMPI_QUERYINTERFACE1(gmpi::MP_IID_CONTROLLER, gmpi::IMpController);
};

namespace
{
	bool r[] = {
		sesdk::Register<SincFilterController>::withId(L"SE Sinc Highpass Filter"),
		sesdk::Register<SincFilterController>::withId(L"SE Sinc Lowpass Filter"),
	};
}
#endif

/*
float* h = hist;
// convolution.
for (int s = sampleFrames; s > 0; --s)
{
float sum = 0;
for (int t = 0; t < numCoefs; ++t)
{
sum += h[t] * taps[t];
}
*output++ = sum;
++h;
}
*/
/*
// version without memcpy, slower.

// write new values into history buffer.
int todo = (std::min)(sampleFrames, histSize - histIdx);
int s;
for (s = 0; s < todo; ++s)
{
hist[histIdx++] = signal[s];
}
if (histIdx == histSize)
{
histIdx = 0;
for (; s < sampleFrames; ++s)
{
hist[histIdx++] = signal[s];
}
}

// zero output buffer.
memset(output, 0, sizeof(float) * sampleFrames);

// convolution.
int r = histReadIdx;
for (int t = 0; t < numCoefs; ++t)
{
int q = r;
int todo = (std::min)(sampleFrames, histSize - q);
int s;
for (s = 0; s < todo; ++s)
{
output[s] += hist[q++] * taps[t];
}

if (q == histSize)
{
q = 0;
for (; s < sampleFrames; ++s)
{
output[s] += hist[q++] * taps[t];
}
}

if (++r >= histSize)
r = 0;
}

histReadIdx += sampleFrames;
if (histReadIdx >= histSize)
histReadIdx -= histSize;

#endif
#endif

#else
for (int s = sampleFrames; s > 0; --s)
{
hist[histIdx++] = *signal++;
if (histIdx == numCoefs)
{
histIdx = 0;
}

// convolution.
float sum = 0;
int x = histIdx;
for (int t = 0; t < numCoefs; ++t)
{
sum += hist[x++] * taps[t];
if (x == numCoefs)
{
x = 0;
}
}
*output++ = sum;
}
#endif
*/

/*
// Hand-made memcpy, no faster.
__m128* dest = (__m128*) hist;
float* src = &hist[sampleFrames];
for (int s = 0; s < histSize - sampleFrames + 3; s = s + 4)
{
*dest++ = _mm_loadu_ps(src);
src += 4;
}
*/
