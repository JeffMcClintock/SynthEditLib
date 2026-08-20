#undef  WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#undef  NOMINMAX
#define NOMINMAX
#include <chrono>
#include "IO_None.h"
#include "SynthRuntime_editor.h"

IO_None::IO_None() :
	m_offset(0)
{
}

IO_None::~IO_None()
{
}

void IO_None::MakeDriverList()
{
	if (m_driver_list_initialised)
		return;

	AddDriverDescription( new IO_output_info(this,L"<none>" ,L"<none>" ));
	m_driver_list_initialised = true;
}

bool IO_None::Prepare(const std::wstring& /*p_unique_id*/, bool /*audio_in_needed*/, bool /*audio_out_needed*/ )
{
	return IO_base::Prepare({}, true, true);
}

void IO_None::Stop()
{
}

bool IO_None::StartStream()
{
	elapsed_sample_time = 0;
	m_start_clock = std::chrono::steady_clock::now();
	return true;
}

int IO_None::getOffsetMs()
{
	return m_offset;
}

int IO_None::getBufferSize()
{
	return SeAudioMaster::getOptimumBlockSize();
}

int IO_None::getSampleRate()
{
	return preferredSampleRate;
}

void IO_None::Render()
{
	const int l_block_size = SeAudioMaster::getOptimumBlockSize(); // not in VST

	if( m_realtime )
	{
		const auto sampleRate = getSampleRate();

		while(!client->isProcessingComplete() && !m_error_state)
		{
			RenderBlock(l_block_size);

			elapsed_sample_time += l_block_size;
			const auto elapsed_real_time = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now() - m_start_clock).count();
			// perform calculation at 64 bit precision (32 bits (*1000) wrap after about 1 min)

			auto elapsed_ms = (1000 * elapsed_sample_time ) / sampleRate;
            const std::chrono::duration<double, std::milli> time_to_sleep(elapsed_ms - elapsed_real_time);
			if( elapsed_ms > elapsed_real_time )
			{
				std::this_thread::sleep_for(time_to_sleep); // This call's timing is very in-exact
			}
		}
	}
	else
	{
		while( !client->isProcessingComplete())
		{
			RenderBlock(l_block_size);
		}
	}
}
