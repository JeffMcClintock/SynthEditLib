#ifndef SLIDER_H_INCLUDED
#define SLIDER_H_INCLUDED

#include "../se_sdk3/mp_sdk_audio.h"
#include "../se_sdk3/smart_audio_pin.h"

class Slider : public MpBase2
{
public:
	Slider();
	int32_t open() override
	{
		pinValueOut.setCurveType(SmartAudioPin::LinearAdaptive); // Automatic.
		return MpBase2::open();
	}
	void subProcess(int sampleFrames);
	void onSetPins() override;

private:
	FloatInPin pinValueIn;
	SmartAudioPin pinValueOut;
};

#endif

