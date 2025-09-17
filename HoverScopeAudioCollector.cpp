#include <assert.h>
#include <cmath>
#include <algorithm>
#include "HoverScopeAudioCollector.h"
#include "Hosting/message_queues.h"

using namespace gmpi::hosting;

HoverScopeAudioCollector::HoverScopeAudioCollector(
	  int32_t pmoduleHandle
	, int psampleRate
	, float* pbuffer
	, gmpi::hosting::IWriteableQue* pqueue
)
	: buffer(pbuffer)
	, sampleRate(psampleRate)
	, queue(pqueue)
	, moduleHandle(pmoduleHandle)
{
	constexpr int totalCapturePoints = 192; // 96 pixels in HD
	constexpr float captureTime = 2.f; // 2s
	samplesPerPoint = static_cast<int>(std::roundf(sampleRate * captureTime / totalCapturePoints));
	pointCounter = samplesPerPoint;
}

void HoverScopeAudioCollector::process(int blockPosition, int sampleFrames)
{
	if (!buffer)
		return;

	// get pointers to in/output buffers
	auto signalA = buffer + blockPosition;

#if 1 // long-term display
	
	for (int s = sampleFrames; s > 0; s--)
	{
		if (pointCounter <= 0)
		{
			pointCounter = samplesPerPoint;
			resultsA_[index_++] = pointMin;
			resultsA_[index_++] = pointMax;

//			if (index_ >= captureSamples)
			{
				const int32_t valueCount = index_;
				const int32_t totalBytes = static_cast<int32_t>(sizeof(float)) * valueCount;

				my_msg_que_output_stream strm(queue, moduleHandle, "hvsw"); // hoverscope waveform.
				strm << static_cast<int32_t>(totalBytes + sizeof(int32_t)); // message size (waveform + count)
				strm << valueCount;
				strm.Write(resultsA_, totalBytes);
				strm.Send();

				index_ = 0;
				pointMin = *signalA;
				pointMax = *signalA;
			}
		}
		else
		{
			pointMin = (std::min)(pointMin, *signalA);
			pointMax = (std::max)(pointMax, *signalA);
		}

		pointCounter--;
		signalA++;
	}

#else // zoomed-in scope
	int s = sampleFrames;
	for (; s > 0; s--)
	{
		if (state == 0) // wait for -ve sample
		{
			timeoutCount_ -= s;
			s = 0;
			if (timeoutCount_ < 0)
			{
				state = 1;
			}
		}

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

//			if (channelsleepCount_ > 0)
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
// TODO				auto bufferOffset = getBlockPosition() + sampleFrames - s;
//				sendResultToGui(bufferOffset);

				// process remaining samples.
				//(this->*(getSubProcess()))(bufferOffset + sampleFrames - remain, remain);
				my_msg_que_output_stream strm(queue, moduleHandle, "hvsw"); // hoverscope waveform.
				strm << static_cast<int32_t>(sizeof(float) * captureSamples); // message length.
				strm.Write(resultsA_, sizeof(float) * captureSamples);
				strm.Send();

				timeoutCount_ = 4000; // todo
				state = 0; // pause a while
				index_ = 0;
			}

			s -= count;
		}
	}
#endif
}