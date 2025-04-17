#include <assert.h>
#include <algorithm>
#include "HoverScopeAudioCollector.h"
#include "my_msg_que_output_stream.h"

void HoverScopeAudioCollector::process(int blockPosition, int sampleFrames)
{
	// get pointers to in/output buffers
	auto signalA = buffer + blockPosition;

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
}