/* Copyright (c) 2007-2025 SynthEdit Ltd

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
#include "Processor.h"

using namespace gmpi;

class RampGeneratorAdaptive2 // copied from SDK3
{
	double adaptiveHi_ = {};
	double adaptiveLo_ = {};

public:
	RampGeneratorAdaptive2() :
		currentValue_((std::numeric_limits<double>::max)())
		, dv(0.0)
	{
	}

	void Init(float sampleRate)
	{
		inverseTransitionTime_ = 1.0 / (sampleRate * 0.015); // 15ms default.

		adaptiveLo_ = 1.0 / (sampleRate * 0.050); // 50ms max.
		adaptiveHi_ = 1.0 / (sampleRate * 0.001); // 1ms min.
	}

	void setTarget(float targetValue)
	{
		if (currentValue_ == (std::numeric_limits<double>::max)())
		{
			currentValue_ = targetValue_ = targetValue;
			dv = 0.0;
			return;
		}

		if (currentValue_ == targetValue_) // too fast, slow down a bit.
		{
			if (inverseTransitionTime_ > adaptiveLo_)
			{
				inverseTransitionTime_ *= 0.9;
			}
		}
		else
		{
			if (inverseTransitionTime_ < adaptiveLo_)
			{
				inverseTransitionTime_ *= 1.05; // slower 'decay', kind of peak follower.
			}
		}

		targetValue_ = targetValue;
		dv = (targetValue_ - currentValue_) * inverseTransitionTime_;
	}

	void setValueInstant(float targetValue)
	{
		currentValue_ = targetValue_ = targetValue;
		dv = 0.0;
	}

	float getInstantValue() const
	{
		return static_cast<float>(currentValue_);
	}

	float getTargetValue() const
	{
		return static_cast<float>(targetValue_);
	}

	inline float getNext()
	{
		currentValue_ += dv;

		if (dv > 0.0)
		{
			if (currentValue_ >= targetValue_)
			{
				currentValue_ = targetValue_;
				dv = 0.0;
			}
		}
		else
		{
			if (currentValue_ <= targetValue_)
			{
				currentValue_ = targetValue_;
				dv = 0.0;
			}
		}

		return static_cast<float>(currentValue_);
	}

	inline bool isDone()
	{
		return dv == 0.0;
	}

	void jumpToTarget()
	{
		setValueInstant(static_cast<float>(targetValue_));
	}
private:
	double dv = {};
	double currentValue_ = {};
	double targetValue_ = {};
	double inverseTransitionTime_ = {};
};

struct FloatToVolts2 final : public Processor
{
	IntInPin pinSmoothing;
	FloatInPin pinFloatIn;
	AudioOutPin pinVoltsOut;

	RampGeneratorAdaptive2 smoother;

	FloatToVolts2()
	{
	}

	gmpi::ReturnCode open(api::IUnknown* phost) override
	{
		auto r = Processor::open(phost);

		smoother.Init(host->getSampleRate());

		return r;
	}

	void subProcess( int sampleFrames )
	{
		if (smoother.isDone())
		{
			pinVoltsOut.setStreaming(false, 0);
		}

		// get pointers to in/output buffers.
		auto voltsOut = getBuffer(pinVoltsOut);

		for( int s = sampleFrames; s > 0; --s )
		{
			// TODO: Signal processing goes here.
			*voltsOut++ = smoother.getNext();
		}
	}

	void onSetPins() override
	{
		// Check which pins are updated.
		if( pinSmoothing.isUpdated() )
		{
		}
		if( pinFloatIn.isUpdated() )
		{
			smoother.setTarget(0.1f * pinFloatIn.getValue());
		}

		// Set state of output audio pins.
		pinVoltsOut.setStreaming(true);

		// Set processing method.
		setSubProcess(&FloatToVolts2::subProcess);
	}
};

namespace
{
auto r = Register<FloatToVolts2>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<PluginList>
    <Plugin id="SE Float to Volts2" name="Float to Volts2" category="Debug">
        <Audio>
            <Pin name="Smoothing" datatype="enum" default="2" metadata="None,Fast (4 samp),Smooth (30ms)"/>
            <Pin name="Float In" datatype="float"/>
            <Pin name="Volts Out" datatype="float" rate="audio" direction="out"/>
        </Audio>
    </Plugin>
</PluginList>
)XML");
}
