//#include "./HoverScope.h"
//#include "../shared/xplatform.h"
                                #include "module_register.h"
#include "Processor.h"

using namespace gmpi;

SE_DECLARE_INIT_STATIC_FILE(HoverScope);

/* TODO !!!
	properties->flags = UGF_VOICE_MON_IGNORE;
*/
#define SCOPE_BUFFER_SIZE 400
#define SCOPE_CHANNELS 1
typedef float ScopeResults[SCOPE_BUFFER_SIZE + 1]; // 4x as big when oversampling. +1 for sample-rate.

class HoverScope final : public Processor
{
public:
	HoverScope()
	{
		init(pinSamplesA);
		init(pinSignalA);
		init(pinVoiceActive);
		init(pinPolyDetect);
	}

	gmpi::ReturnCode open(api::IUnknown* phost) override;

	// methods
	void subProcess(int sampleFrames);
	//void waitForTrigger1(int sampleFrames);
	//void waitForTrigger2(int sampleFrames);
	void subProcessCruise(int sampleFrames);
	void forceTrigger();
	void onSetPins() override;

	AudioInPin* getTriggerPin()
	{
		return &pinSignalA;
	}
	int getTimeOut()
	{
		return (int) host.getSampleRate() / 3;
	}

protected:
	void sendResultToGui(int block_offset);

	// pins
	BlobOutPin pinSamplesA;
	AudioInPin pinSignalA;
	FloatInPin pinVoiceActive;
	BoolOutPin pinPolyDetect;

	int index_{};
	int timeoutCount_;
	//	int sleepCount;
	ScopeResults resultsA_;
	//	bool channelActive_[SCOPE_CHANNELS];
	int channelsleepCount_[SCOPE_CHANNELS];
	HoverScope** currentVoice_{};
	int captureSamples;

	int state = 1; // 0 = idle, 1 = waiting for trigger, 2 = waiting for trigger +ve, 3 = capturing, 4 = cruise.
};

gmpi::ReturnCode HoverScope::open(api::IUnknown* phost)
{
	Processor::open(phost);

	setSubProcess( &HoverScope::subProcess );

//	channelActive_[0] = channelActive_[1] = true; // TODO !! getPin(i)->IsConnected();

	// Module must transmit an initial value on all output pins. [ now handled by SDK ]
	//pinSamplesA.sendPinUpdate();
	//pinSamplesB.sendPinUpdate();

	// determine if polyphonic or not.
	int isCloned{};
	// TODO getHost()->isCloned( &isCloned );

	pinPolyDetect = isCloned != 0;

	captureSamples = SCOPE_BUFFER_SIZE;

	return gmpi::ReturnCode::Ok;
}

#if 0
// wait for waveform to restart.
void HoverScope::waitForTrigger1(int sampleFrames)
{
	auto signala = getBuffer(*getTriggerPin());

	for(int s = sampleFrames ; s > 0 ; s--)
	{
		if(*signala++ <= 0.f)
		{
			index_ = 0;
			setSubProcess(&HoverScope::waitForTrigger2);
			waitForTrigger2(bufferOffset + sampleFrames - s, s);
			return;
		}
	}

	timeoutCount_ -= sampleFrames;
	if(timeoutCount_ < 0)
	{
		setSubProcess(&HoverScope::waitForTrigger2);
	}
}

void HoverScope::waitForTrigger2(int sampleFrames)
{
//	float* signala	= bufferOffset + pinSignalA.begin();
	auto signala = getBuffer(*getTriggerPin());

	for(int s = sampleFrames ; s > 0 ; s--)
	{
		if(*signala++ > 0.f)
		{
			forceTrigger();

			(this->*(getSubProcess()))(bufferOffset + sampleFrames - s, s);
			return;
		}
	}

	timeoutCount_ -= sampleFrames;
	if(timeoutCount_ < 0)
	{
		forceTrigger();
	}
}
#endif

void HoverScope::subProcess(int sampleFrames)
{
	// get pointers to in/output buffers
	auto signalA = getBuffer(pinSignalA);

	int s = sampleFrames;
	for (; s > 0; s--)
	{
		if (state == 1) // wait for -ve sample
		{
			timeoutCount_ -= s;

			for (; s > 0; s--)
			{
				if (*signalA++ <= 0.f)
				{
					state = 2;
					break;
				}
			}

			if (1 == state && timeoutCount_ < 0)
			{
				state = 3;
			}
		}
		
		if (state == 2) // wait for +ve sample
		{
			timeoutCount_ -= s;

			for (; s > 0; s--)
			{
				if (*signalA++ > 0.f)
				{
					state = 3;
					break;
				}
			}

			if (2 == state && timeoutCount_ < 0)
			{
				state = 3;
			}
		}

		if (state == 3) // capture data
		{
			const int count = (std::min)(s, captureSamples - index_);

			if (channelsleepCount_[0] > 0)
			{
				int i = index_;
				for (int c = count; c > 0; c--)
				{
					assert(i < captureSamples);
					resultsA_[i++] = *signalA++;
				}
			}

			index_ += count;

			if (index_ >= captureSamples)
			{
				auto bufferOffset = getBlockPosition() + sampleFrames - s;
				sendResultToGui(bufferOffset);

				// process remaining samples.
				//(this->*(getSubProcess()))(bufferOffset + sampleFrames - remain, remain);
			}

			s -= count;
		}
	}
}

// do nothing for 1/25th second.  Gives GUI time to display image.
void HoverScope::subProcessCruise(int sampleFrames)
{
	timeoutCount_ -= sampleFrames;
	if(timeoutCount_ < 0)
	{
		// in absence of trigger signal, redraw 3 times per second.
		timeoutCount_ = getTimeOut(); // (int)getSampleRate() / 3;
		state = 1; // wait for -ve sample
		setSubProcess(&HoverScope::subProcess);
	}
}

void HoverScope::forceTrigger()
{
	index_ = 0;
	state = 3;
	setSubProcess(&HoverScope::subProcess);
}

void HoverScope::sendResultToGui(int block_offset)
{
	{
		/*
		if( captureSamples > SCOPE_BUFFER_SIZE * 2 ) // oversampling? If so compress timescale.
		{
			int j = 0;
			for( int i = 0 ; i < SCOPE_BUFFER_SIZE ; ++i )
			{
				resultsA_[i] = (resultsA_[j] + resultsA_[j+1] + resultsA_[j+2] + resultsA_[j+3]) * 0.25f;
				resultsB_[i] = (resultsB_[j] + resultsB_[j+1] + resultsB_[j+2] + resultsB_[j+3]) * 0.25f;
				j += 4;
			}
		}
		else
		{
			int j = 0;
			for( int i = 0 ; i < SCOPE_BUFFER_SIZE ; ++i )
			{
				resultsA_[i] = (resultsA_[j] + resultsA_[j+1]) * 0.5f;
				resultsB_[i] = (resultsB_[j] + resultsB_[j+1]) * 0.5f;
				j += 2;
			}
		}
		*/
		const int datasize = SCOPE_BUFFER_SIZE * sizeof(resultsA_[0]);

		if (channelsleepCount_[0] > 0)
		{
			pinSamplesA.setValueRaw( datasize, &resultsA_ );
			pinSamplesA.sendPinUpdate( block_offset );
		}
	}

	if (!pinSignalA.isStreaming())
	{
		--channelsleepCount_[0];
	}

	if (channelsleepCount_[0] <= 0 && !getTriggerPin()->isStreaming())
	{
		setSubProcess(&HoverScope::subProcessNothing);
		setSleep(true);
	}
	else
	{
		// waste of CPU to send updates more often than GUI can repaint,
		// wait approx 1/10th seccond between captures.
		timeoutCount_ = (int)host.getSampleRate() / 10;
		setSubProcess(&HoverScope::subProcessCruise);
	}

	index_ = 0; // ensure we are ready for next capture cycle, even if subProcessCruise gets bypassed by suspend/resume.
}

void HoverScope::onSetPins()  // one or more pins_ updated.  Check pin update flags to determin which ones.
{
	if (pinSignalA.isUpdated())
		channelsleepCount_[0] = 2; // need to send at least 2 captures to ensure flat-line signal captured.

	/*
	if( pinSignalA.isStreaming() || pinSignalB.isStreaming() || getTriggerPin()->isStreaming() )
	{
		sleepCount = -1; // indicates no sleep.
	}
	else
	{
		// need to send at least 2 captures to ensure flat-line signal captured.
		// Current capture may be half done.
		sleepCount = 2;
	}

	if (pinSignalA.isUpdated())
	{
		channelActive_[0] = true;
	}

	if (pinSignalB.isUpdated())
	{
		channelActive_[1] = true;
	}
	*/

	// Avoid resetting capture unless module is actually asleep.
	if( getSubProcess() == &HoverScope::subProcessNothing )
	{
		setSubProcess(&HoverScope::subProcess);
	}

	if( pinVoiceActive.isUpdated() )
	{
		int32_t isPolyphonic{};
		// TODO getHost()->isCloned( &isPolyphonic );

//		_RPT2(_CRT_WARN, "HoverScope::onSetPins pinVoiceActive = %f [%x]\n", (double) pinVoiceActive, this );
		if( pinVoiceActive <= 0.0f && isPolyphonic != 0 )
		{
			// send blank capture to indicate voice muted.
			pinSamplesA.setValueRaw(0, 0);
			pinSamplesA.sendPinUpdate();

			// do nothing.
			setSubProcess(&HoverScope::subProcessNothing);
//			setSleep( true );
		}

	}

	setSleep(false);
}

namespace
{
auto r = gmpi::Register<HoverScope>::withId("SE: HoverScope");
}