// ug_delay module
//
#pragma once
#include "ug_base.h"


#define interpolationExtraSamples 1

inline void CalculateModulation(float modulationOffset, float*& modulation, int buffer_size, int padded_buffer_size, int& read_offset_int, float&read_offset_fine)
{
	float readOffset = ( modulationOffset - *modulation ) * buffer_size + ( padded_buffer_size - buffer_size );
//	read_offset_int = static_cast<int32_t>(readOffset)); // fast float-to-int using SSE. truncation toward zero.
	read_offset_int = static_cast<int32_t>(readOffset);

	read_offset_fine = readOffset - (float) read_offset_int;

	if( read_offset_int < 1 )
	{
		read_offset_int = 1;
		read_offset_fine = 0.0f;
	}
	else
	{
		if( read_offset_int >= padded_buffer_size )
		{
			read_offset_int = padded_buffer_size;
			read_offset_fine = 0.0f;
		}
	}
	++modulation;
};

class PolicyModulationDigitalChanging
{
public:
	inline static void CalculateInitial(float modulationOffset, float*& modulation, int buffer_size, int padded_buffer_size, int& read_offset_int, float&read_offset_fine)
	{
		// do nothing. Hopefully optimizes away to nothing.
	}
	inline static void Calculate(float modulationOffset, float*& modulation, int buffer_size, int padded_buffer_size, int& read_offset_int, float&read_offset_fine)
	{
		CalculateModulation(modulationOffset, modulation, buffer_size, padded_buffer_size, read_offset_int, read_offset_fine);
	}
};

class PolicyModulationFixed
{
public:
	inline static void CalculateInitial(float modulationOffset, float*& modulation, int buffer_size, int padded_buffer_size, int& read_offset_int, float&read_offset_fine)
	{
		CalculateModulation(modulationOffset, modulation, buffer_size, padded_buffer_size, read_offset_int, read_offset_fine);
	}

	inline static void Calculate(float modulationOffset, float*& modulation, int buffer_size, int padded_buffer_size, int& read_offset_int, float&read_offset_fine)
	{
		// do nothing. Hopefully optimizes away to nothing.
	}
};

class PolicyFeedbackOff
{
public:
	inline static float Calculate(  float prev_out, float*&feedback  )
	{
		return 0.0f;
	}
};
class PolicyFeedbackModulated
{
public:
	inline static float Calculate( float prev_out, float*&feedback )
	{
		return prev_out * *feedback++;
	}
};

class PolicyFeedbackFixed
{
public:
	inline static float Calculate( float prev_out, float*&feedback  )
	{
		return prev_out * *feedback;
	}
};

class PolicyInterpolationCubic
{
public:
	inline static float Calculate( int count, float* buffer, int read_offset, float fraction, int buffer_size )
	{
		int index = count + read_offset - 1;

		if( index >= buffer_size )
		{
			index -= buffer_size;
		}

		assert( index >= 0 && index < buffer_size + interpolationExtraSamples );

		// Buffer has 3 extra samples 'off-end' filled with duplicate samples from buffer start. Avoid need to handle wrapping.
		float y0 = buffer[ index+0 ];
		float y1 = buffer[ index+1 ];
		float y2 = buffer[ index+2 ];
		float y3 = buffer[ index+3 ];

		return y1 + 0.5f * fraction*(y2 - y0 + fraction*(2.0f*y0 - 5.0f*y1 + 4.0f*y2 - y3 + fraction*(3.0f*(y1 - y2) + y3 - y0)));
	}
};

class PolicyInterpolationLinear
{
public:
	inline static float Calculate(int count, float* buffer, int read_offset, float read_offset_fine, int padded_size)
	{
		int index = count + read_offset;

		if( index >= padded_size )
		{
			index -= padded_size;
		}

		assert(index >= 0 && index < padded_size );
		assert(index != count || read_offset_fine < 0.001f); // can't read the value past we just wrote?

		// Buffer has 3 extra samples 'off-end' filled with duplicate samples from buffer start. Avoid need to handle wrapping.
		float y0 = buffer[ index+0 ];
		float y1 = buffer[ index+1 ];

		return y0 + read_offset_fine * ( y1 - y0 );
	}
};

class PolicyInterpolationNone
{
public:
	inline static float Calculate(int count, float* buffer, int read_offset, float read_offset_fine, int padded_size)
	{
		int index = count + read_offset;

		if( index >= padded_size )
		{
			index -= padded_size;
		}

		assert(index >= 0 && index < padded_size + interpolationExtraSamples);
		return buffer[ index ];
	}
};


class ug_delay : public ug_base
{
public:
	DECLARE_UG_INFO_FUNC2;
	DECLARE_UG_BUILD_FUNC(ug_delay);

	template< class ModulationPolicy, class InterpolationPolicy, class FeedbackPolicy >
	void subProcess( int bufferOffset, int sampleFrames )
	{
		float* signalIn		= bufferOffset + input_ptr;
		float* modulation	= bufferOffset + modulation_ptr;
		float* signalOut	= bufferOffset + output_ptr;
		float* feedback		= bufferOffset + feedback_ptr;

		float prev_out = m_prev_out;

		int read_offset;
		float read_offset_fine;
		ModulationPolicy::CalculateInitial( m_modulation_input_offset, modulation, buffer_size, padded_size, read_offset, read_offset_fine );

		for( int s = sampleFrames; s > 0; --s )
		{
			ModulationPolicy::Calculate(m_modulation_input_offset, modulation, buffer_size, padded_size, read_offset, read_offset_fine);

			buffer[count] = *signalIn++ + FeedbackPolicy::Calculate( prev_out, feedback );

			// mirror initial few samples "off end" too, to simplify interpolation.
			if( count < interpolationExtraSamples )
			{
				buffer[count + padded_size] = buffer[count];
			}

			*signalOut++ = prev_out = InterpolationPolicy::Calculate(count, buffer, read_offset, read_offset_fine, padded_size);

			// increment count.
			if( ++count == padded_size )
			{
				count = 0;
			}
		}

		m_prev_out = prev_out;
	}

	void onSetPin(timestamp_t p_clock, UPlug* p_to_plug, state_type p_state) override;
	void sub_process_static(int start_pos, int sampleframes);

	~ug_delay();
	ug_delay();
	int Open() override;
	void CreateBuffer();

	void resetStaticCounter()
	{
		static_output_count = buffer_size + AudioMaster()->BlockSize();
	}

protected:
	float delay_time;
	float m_modulation_input_offset; // for backward compatability
	bool interpolate;
	int count;
	int buffer_size;
	int padded_size;

	float* buffer;
	float* input_ptr;
	float* output_ptr;
	float* modulation_ptr;
	float* feedback_ptr;
	process_func_ptr current_process_func;
	float m_prev_out;
};

class ug_delay2 :

	public ug_delay

{

public:

	ug_delay2()
	{
		m_modulation_input_offset = 1.f;
	}

	DECLARE_UG_INFO_FUNC2;

	DECLARE_UG_BUILD_FUNC(ug_delay2);

};

// A pure latency fixer: delays the signal by a whole number of samples and reports EXACTLY that
// number to the host — one 'Latency (ms)' value drives both the delay line and the report, so
// report == physical by identity. Unlike a Lookahead the physical delay stays on this wire
// (invisible to internal PDC → no LatencyAdjust on siblings → a deliberate lookahead offset is
// preserved). No modulation, no interpolation, no feedback: use Delay2 for echo effects, whose
// delay must NOT be reported. See ug_base::calcReportedLatency.
//
// The 'Latency (ms)' pin carries IO_MINIMISED, which prevents connections by hiding the pin: it is
// a design-time constant, so the latency is fixed for the life of a build and there is deliberately
// NO runtime re-report path (no SetModuleLatency hook, no restart trigger) — hosts expect a
// plugin's latency to be constant anyway. Don't add one.
class ug_compensated_delay : public ug_base
{
public:
	DECLARE_UG_BUILD_FUNC(ug_compensated_delay);
	DECLARE_UG_INFO_FUNC2;
	~ug_compensated_delay();

	int Open() override;

	// Latches the latency while the pin default is still reliably readable — before this pass
	// splices pads into the arm and before oversampling containers re-plumb the buffer away.
	// Contributes NOTHING to compensation (latencySamples stays 0); the override exists only for
	// its timing. See getReportedSelfLatency.
	int calcDelayCompensation() override;

	int getReportedSelfLatency() override;
	void onSetPin(timestamp_t p_clock, UPlug* p_to_plug, state_type p_state) override;
	ug_base* Clone(CUGLookupList& UGLookupList) override; // clones copy the latched latency

	void sub_process(int start_pos, int sampleframes);
	void sub_process_static(int start_pos, int sampleframes);

private:
	static constexpr int PN_MS = 0;
	static constexpr int PN_IN = 1;
	static constexpr int PN_OUT = 2;

	float readMsPinDefault(); // GetPlug is non-const
	int msToSamples(float ms) const;
	void prepareBuffer(int samples);

	float latencyMs = 0.0f;    // LIST_VAR3 member (populated once pins transmit)
	int latchedSamples = -1;   // set during calcDelayCompensation; -1 = not yet latched

	float* buffer = nullptr;   // circular, exactly bufferSamples long; nullptr when zero latency
	int bufferSamples = 0;     // == the reported latency: report and physical share one number
	int writeIndex = 0;

	float* input_ptr = nullptr;
	float* output_ptr = nullptr;
};