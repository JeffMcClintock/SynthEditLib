// SPDX-License-Identifier: ISC
// Copyright 2006-2026 Jeff McClintock.
//
// Scope4 - oscilloscope DSP. Ported from the legacy SE SDK3 "SE Scope3 XP" /
// "SE TrigScope3 XP" modules (Scope3.cpp / TriggerScope.cpp) to the GMPI
// Processor API. The matching GUI lives in Scope4Gui.cpp, which also supplies
// the plugin XML (shared <Audio> + <GUI> sections) for both ids below.

#include <algorithm>
#include <cassert>
#include <climits>
#include <cstdint>
#include "Processor.h"

using namespace gmpi;

#define SCOPE_BUFFER_SIZE 400
#define SCOPE_CHANNELS 2

// 4x as big when oversampling. +1 for sample-rate.
typedef float ScopeResults[SCOPE_BUFFER_SIZE + 1];

class Scope4 : public Processor
{
public:
	gmpi::ReturnCode open(gmpi::api::IUnknown* phost) override;

	// sub-process states
	void subProcess(int sampleFrames);
	void waitForTrigger1(int sampleFrames);
	void waitForTrigger2(int sampleFrames);
	void subProcessCruise(int sampleFrames);
	void forceTrigger();
	void onSetPins() override;

	// The base scope triggers off Signal A; the triggered scope overrides this
	// to use a dedicated Trigger input. INT_MAX timeout = wait forever for a trigger.
	virtual AudioInPin* getTriggerPin() { return &pinSignalA; }
	virtual int getTimeOut() { return static_cast<int>(host->getSampleRate()) / 3; }

protected:
	void sendResultToGui(int block_offset);

	// pins (member declaration order MUST match the XML <Audio> pin order)
	AudioInPin  pinSignalA;
	AudioInPin  pinSignalB;
	FloatInPin  pinVoiceActive;
	BlobOutPin  pinSamplesA;
	BlobOutPin  pinSamplesB;
	BoolOutPin  pinPolyDetect;

	int index_ = 0;
	int timeoutCount_ = 0;
	ScopeResults resultsA_{};
	ScopeResults resultsB_{};
	int channelsleepCount_[SCOPE_CHANNELS] = { 0, 0 };
	int captureSamples = SCOPE_BUFFER_SIZE;
};

gmpi::ReturnCode Scope4::open(gmpi::api::IUnknown* phost)
{
	Processor::open(phost);

	setSubProcess(&Scope4::subProcess);

	// determine if polyphonic or not.
	int isCloned = 0;
	// TODO: the GMPI IProcessorHost has no isCloned() yet. While stubbed at 0
	// the module reports monophonic and the GUI draws a single trace.
	pinPolyDetect = isCloned != 0;

	captureSamples = SCOPE_BUFFER_SIZE;

	return gmpi::ReturnCode::Ok;
}

void Scope4::subProcess(int sampleFrames)
{
	// get pointers to input buffers (already offset by the current block position)
	const float* signalA = getBuffer(pinSignalA);
	const float* signalB = getBuffer(pinSignalB);

	int count = captureSamples - index_;
	if (count > sampleFrames)
		count = sampleFrames;

	const int remain = sampleFrames - count;

	if (channelsleepCount_[0] > 0)
	{
		int i = index_;
		for (int c = count; c > 0; c--)
		{
			assert(i < captureSamples);
			resultsA_[i++] = *signalA++;
		}
	}
	if (channelsleepCount_[1] > 0)
	{
		int i = index_;
		for (int c = count; c > 0; c--)
		{
			resultsB_[i++] = *signalB++;
		}
	}

	index_ += count;

	if (index_ >= captureSamples)
	{
		sendResultToGui(getBlockPosition());

		// process remaining samples with whatever sub-process is now active.
		// Advance the block position so getBuffer()/sendPinUpdate() stay aligned,
		// then restore it for the outer process() loop.
		blockPos_ += count;
		(this->*(getSubProcess()))(remain);
		blockPos_ -= count;
	}
}

// wait for waveform to go negative.
void Scope4::waitForTrigger1(int sampleFrames)
{
	const float* signala = getBuffer(*getTriggerPin());

	for (int s = sampleFrames; s > 0; s--)
	{
		if (*signala++ <= 0.f)
		{
			index_ = 0;
			setSubProcess(&Scope4::waitForTrigger2);

			const int consumed = sampleFrames - s;
			blockPos_ += consumed;
			waitForTrigger2(s);
			blockPos_ -= consumed;
			return;
		}
	}

	timeoutCount_ -= sampleFrames;
	if (timeoutCount_ < 0)
	{
		setSubProcess(&Scope4::waitForTrigger2);
	}
}

// wait for waveform to cross back positive (the trigger point).
void Scope4::waitForTrigger2(int sampleFrames)
{
	const float* signala = getBuffer(*getTriggerPin());

	for (int s = sampleFrames; s > 0; s--)
	{
		if (*signala++ > 0.f)
		{
			forceTrigger();

			const int consumed = sampleFrames - s;
			blockPos_ += consumed;
			(this->*(getSubProcess()))(s);
			blockPos_ -= consumed;
			return;
		}
	}

	timeoutCount_ -= sampleFrames;
	if (timeoutCount_ < 0)
	{
		forceTrigger();
	}
}

// do nothing for ~1/25th second. Gives the GUI time to display the image.
void Scope4::subProcessCruise(int sampleFrames)
{
	timeoutCount_ -= sampleFrames;
	if (timeoutCount_ < 0)
	{
		// in absence of trigger signal, redraw 3 times per second.
		timeoutCount_ = getTimeOut();
		setSubProcess(&Scope4::waitForTrigger1);
	}
}

void Scope4::forceTrigger()
{
	index_ = 0;
	setSubProcess(&Scope4::subProcess);
}

void Scope4::sendResultToGui(int block_offset)
{
	const int datasize = SCOPE_BUFFER_SIZE * static_cast<int>(sizeof(resultsA_[0]));

	if (channelsleepCount_[0] > 0)
	{
		resultsA_[captureSamples - 1] = pinVoiceActive.getValue(); // last entry is voice-active
		pinSamplesA.setRaw({ reinterpret_cast<const uint8_t*>(resultsA_), static_cast<size_t>(datasize) });
		pinSamplesA.sendPinUpdate(block_offset);
	}

	if (channelsleepCount_[1] > 0)
	{
		pinSamplesB.setRaw({ reinterpret_cast<const uint8_t*>(resultsB_), static_cast<size_t>(datasize) });
		pinSamplesB.sendPinUpdate(block_offset);
	}

	if (!pinSignalA.isStreaming())
		--channelsleepCount_[0];
	if (!pinSignalB.isStreaming())
		--channelsleepCount_[1];

	if (channelsleepCount_[0] <= 0 && channelsleepCount_[1] <= 0 && !getTriggerPin()->isStreaming())
	{
		setSubProcess(&Scope4::subProcessNothing);
		setSleep(true);
	}
	else
	{
		// waste of CPU to send updates more often than the GUI can repaint,
		// wait approx 1/10th second between captures.
		timeoutCount_ = static_cast<int>(host->getSampleRate()) / 10;
		setSubProcess(&Scope4::subProcessCruise);
	}

	index_ = 0; // ready for next capture cycle, even if subProcessCruise is bypassed by suspend/resume.
}

void Scope4::onSetPins() // one or more pins updated. Check pin update flags to determine which.
{
	if (pinSignalA.isUpdated())
		channelsleepCount_[0] = 2; // need at least 2 captures to ensure a flat-line signal is captured.

	if (pinSignalB.isUpdated())
		channelsleepCount_[1] = 2;

	// Avoid resetting capture unless the module is actually asleep.
	if (getSubProcess() == &Scope4::subProcessNothing)
	{
		setSubProcess(&Scope4::subProcess);
	}

	if (pinVoiceActive.isUpdated())
	{
		int32_t isPolyphonic = 0;
		// TODO: the GMPI IProcessorHost has no isCloned() yet.

		if (pinVoiceActive <= 0.0f && isPolyphonic != 0)
		{
			// send blank capture to indicate voice muted.
			pinSamplesA.setRaw({});
			pinSamplesA.sendPinUpdate();
			pinSamplesB.setRaw({});
			pinSamplesB.sendPinUpdate();

			// do nothing.
			setSubProcess(&Scope4::subProcessNothing);
		}
	}

	setSleep(false);
}

// ── Triggered scope ──────────────────────────────────────────────────────────
// Adds a dedicated Trigger input and never free-runs (waits forever for a trigger).
class TriggerScope4 final : public Scope4
{
public:
	AudioInPin* getTriggerPin() override { return &pinTrigger; }
	int getTimeOut() override { return INT_MAX; }

private:
	// Registers after the base-class pins, so this is the last <Audio> pin in the XML.
	AudioInPin pinTrigger;
};

namespace
{
auto rScope = gmpi::Register<Scope4>::withId("SE Scope4");
auto rTrig  = gmpi::Register<TriggerScope4>::withId("SE TrigScope4");
}
