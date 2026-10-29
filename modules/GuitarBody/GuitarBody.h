#pragma once

#include "Processor.h"
#include "GuitarBodyBank.h"

#ifndef SE_DECLARE_INIT_STATIC_FILE
#define SE_DECLARE_INIT_STATIC_FILE(filename) void se_static_library_init_##filename(){}
#endif

// Colours a plucked string with the resonances of an acoustic guitar body. Linear, so one instance after the voices
// sounds the same as one per voice.
class GuitarBody : public gmpi::Processor
{
	guitar_body::Bank bank_;
	guitar_body::Settings settings_;
	double sampleRate_ = 44100.0;
	int quietSamples_ = 0;
	int quietCount_ = 0;
	bool settled_ = false;
	bool setUp_ = false;

public:
	gmpi::ReturnCode open(gmpi::api::IUnknown* phost) override;
	void onSetPins() override;

	void subProcess(int sampleFrames);
	void subProcessSettled(int sampleFrames);

private:
	void settle(int blockOffset);

	gmpi::AudioInPin pinSignalIn;
	gmpi::AudioOutPin pinSignalOut;
	gmpi::FloatInPin pinSize;
	gmpi::FloatInPin pinResonance;
	gmpi::FloatInPin pinBrightness;
	gmpi::FloatInPin pinMix;
};
