
#include <algorithm>
#include <cmath>
#include "ug_delay.h"
#include "resource.h"
#include "module_register.h"
#include "ISeAudioMaster.h"
#include "ISeShellDsp.h"

SE_DECLARE_INIT_STATIC_FILE(ug_delay);

namespace
{
REGISTER_MODULE_1(L"Delay", IDS_MN_DELAY,IDS_MG_DEBUG,ug_delay ,CF_STRUCTURE_VIEW,L"Creates an echo effect");
REGISTER_MODULE_1(L"Delay2", IDS_MN_DELAY2,IDS_MG_OLD,ug_delay2,CF_STRUCTURE_VIEW,L"Creates an echo effect");
REGISTER_MODULE_1(L"Compensated Delay", L"Compensated Delay", L"Effects", ug_compensated_delay, CF_STRUCTURE_VIEW,
	L"Delays the signal by 'Latency (ms)' and reports exactly that latency to the host for plugin "
	L"delay-compensation (unlike Delay2, whose delay is unreported). A pure latency fixer: no "
	L"modulation, no interpolation, no feedback - use Delay2 for echo effects.");
}

#define PN_SIGNAL		0
#define PN_MODULATION	1
#define PN_OUTPUT		2
#define PN_DELAY_TIME	3
#define PN_FEEDBACK	5

// Fill an array of InterfaceObjects with plugs and parameters
void ug_delay::ListInterface2(std::vector<class InterfaceObject*>& PList)
{
	// IO Var, Direction, Datatype, Name, Default, range/enum list
	// defid used to name a enum list or range of values
	LIST_PIN2( L"Signal In", input_ptr, DR_IN, L"", L"", IO_LINEAR_INPUT, L"");
	LIST_PIN2( L"Modulation", modulation_ptr, DR_IN, L"5", L"5,-5,5,-5",IO_POLYPHONIC_ACTIVE, L"Varies the delay time dynamically ( -5V to +5V )");
	LIST_PIN2( L"Signal Out", output_ptr, DR_OUT, L"", L"", 0, L"");
	LIST_VAR3( L"Delay Time (secs)",delay_time, DR_IN, DT_FLOAT , L"0.25", L"",IO_MINIMISED, L"Max delay time in Seconds. Limited to maximum 10s.");
	LIST_VAR3( L"Interpolate Output", interpolate, DR_IN, DT_BOOL , L"0", L"", IO_MINIMISED, L"Provides smoother modulation of delay time, but increases CPU load");
	LIST_PIN2( L"Feedback", feedback_ptr, DR_IN, L"0", L"10,0,10,0",IO_POLYPHONIC_ACTIVE, L"");
}

void ug_delay2::ListInterface2(std::vector<class InterfaceObject*>& PList)

{
	// IO Var, Direction, Datatype, Name, Default, range/enum list
	// defid used to name a enum list or range of values
	LIST_PIN2( L"Signal In", input_ptr, DR_IN, L"", L"", IO_LINEAR_INPUT, L"");
	LIST_PIN2( L"Modulation", modulation_ptr, DR_IN, L"10", L"10,0,10,0",IO_POLYPHONIC_ACTIVE, L"Varies the delay time dynamically ( 0 to 10V )");
	LIST_PIN2( L"Signal Out", output_ptr, DR_OUT, L"", L"", 0, L"");
	LIST_VAR3( L"Delay Time (secs)",delay_time, DR_IN, DT_FLOAT , L"1.0", L"",IO_MINIMISED, L"Max delay time in Seconds. Limited to maximum 10s.");
	LIST_VAR3( L"Interpolate Output", interpolate, DR_IN, DT_BOOL , L"0", L"", IO_MINIMISED, L"Provides smoother modulation of delay time, but increases CPU load");
	LIST_PIN2( L"Feedback", feedback_ptr, DR_IN, L"0", L"10,0,10,0",IO_POLYPHONIC_ACTIVE, L"");
}

// Pin order matches ug_lookahead2: 0 = ms parameter, 1 = Signal In, 2 = Signal Out.
void ug_compensated_delay::ListInterface2(std::vector<class InterfaceObject*>& PList)
{
	// IO Var, Direction, Datatype, Default, Range, defid, Help (order must match PN_MS / PN_IN / PN_OUT).
	LIST_VAR3(L"Latency (ms)", latencyMs, DR_IN, DT_FLOAT, L"0", L"", IO_MINIMISED,
		L"Delays the signal by this amount AND reports it to the host for plugin delay-compensation. "
		L"A design-time constant.");
	LIST_PIN2(L"Signal In", input_ptr, DR_IN, L"0", L"", IO_LINEAR_INPUT, L"");
	LIST_PIN2(L"Signal Out", output_ptr, DR_OUT, L"0", L"", 0, L"");
}

ug_compensated_delay::~ug_compensated_delay()
{
	delete[] buffer;
}

// Read the ms pin's document default from the Default-Setter's build-time buffer, exactly as
// ug_lookahead2::calcDelayCompensation does — including its UGF_DEFAULT_SETTER guard. Falls back
// to the member (populated once pins transmit).
float ug_compensated_delay::readMsPinDefault()
{
	if (const auto p = GetPlug(PN_MS); !p->connections.empty())
	{
		if (const auto from = p->connections.front();
			from->UG->GetFlag(UGF_DEFAULT_SETTER) && from->io_variable && from->DataType == DT_FLOAT)
		{
			return *reinterpret_cast<float*>(from->io_variable);
		}
	}
	return latencyMs;
}

// One value, one conversion: this number is BOTH the delay line's length and the host report, so
// they cannot disagree. Clamped to sane physical bounds: never negative, capped at 10 seconds
// (matching ug_delay's buffer cap).
int ug_compensated_delay::msToSamples(float ms) const
{
	const double sr = static_cast<double>(getSampleRate());
	const double samples = std::round(static_cast<double>(ms) * sr * 0.001);
	return static_cast<int>((std::max)(0.0, (std::min)(samples, 10.0 * sr)));
}

// (Re)size the delay line. Zero latency = no buffer, pure pass-through.
void ug_compensated_delay::prepareBuffer(int samples)
{
	if (samples == bufferSamples && (buffer != nullptr || samples == 0))
		return;

	delete[] buffer;
	buffer = samples > 0 ? new float[samples]() : nullptr; // zero-initialised
	bufferSamples = samples;
	writeIndex = 0;
}

// Latch the latency at the only moment the pin default is reliably readable: during the
// compensation pass, BEFORE this module's arms are padded (pads on our pins are inserted by our
// own base call below, and a spliced pad's output plug has no io_variable) and BEFORE
// oversampling-capable containers re-plumb their children (which nulls the Default-Setter buffer
// between this pass and Open — see the note on UPlug::SetDefault). Contributes nothing to
// compensation: latencySamples stays 0.
int ug_compensated_delay::calcDelayCompensation()
{
	if (latchedSamples < 0)
		latchedSamples = msToSamples(readMsPinDefault());

	return ug_base::calcDelayCompensation();
}

// Normally the value latched at compensation time; the guarded lazy read is only a fallback for
// builds where the compensation pass never ran (latency compensation disabled — where the report
// is clamped to 0 anyway).
int ug_compensated_delay::getReportedSelfLatency()
{
	if (latchedSamples >= 0)
		return latchedSamples;

	return msToSamples(readMsPinDefault());
}

// Polyphonic clones are created AFTER the compensation pass, so they never latch for themselves —
// carry the latched latency across (ug_base::Clone copies latencySamples the same way).
ug_base* ug_compensated_delay::Clone(CUGLookupList& UGLookupList)
{
	auto clone = static_cast<ug_compensated_delay*>(ug_base::Clone(UGLookupList));
	clone->latchedSamples = latchedSamples;
	return clone;
}

int ug_compensated_delay::Open()
{
	ug_base::Open();

	prepareBuffer(latchedSamples >= 0 ? latchedSamples : msToSamples(readMsPinDefault()));

	SET_PROCESS_FUNC(&ug_compensated_delay::sub_process);
	OutputChange(SampleClock(), GetPlug(PN_OUT), ST_STATIC);

	return 0;
}

void ug_compensated_delay::onSetPin(timestamp_t p_clock, UPlug* p_to_plug, state_type /*p_state*/)
{
	if (p_to_plug == GetPlug(PN_MS))
	{
		// pin is not exposed, only the default is used, except possibly in the editor, in which case we just restart the graph.
		if(buffer)
			AudioMaster()->getShell()->DoAsyncRestart();

		// this fires at the first sample, before any audio has flowed. The REPORT never changes:
		// the pin is IO_MINIMISED, a design-time constant.
		prepareBuffer(msToSamples(latencyMs));
	}

	// Output follows the input's streaming/static state, delayed by the buffer length: when the
	// input goes static the output keeps changing until the line has drained, so keep running for
	// bufferSamples more and only then announce static (ug_delay's drain pattern).
	const state_type in_state = GetPlug(PN_IN)->getState();

	if (in_state == ST_RUN)
	{
		SET_PROCESS_FUNC(&ug_compensated_delay::sub_process);
		OutputChange(p_clock, GetPlug(PN_OUT), ST_RUN);
	}
	else
	{
		static_output_count = bufferSamples + AudioMaster()->BlockSize();
		SET_PROCESS_FUNC(&ug_compensated_delay::sub_process_static);
		OutputChange(p_clock, GetPlug(PN_OUT), ST_RUN); // still draining
	}
}

void ug_compensated_delay::sub_process(int start_pos, int sampleframes)
{
	const float* in = input_ptr + start_pos;
	float* __restrict out = output_ptr + start_pos;

	if (bufferSamples == 0) // zero latency: pure pass-through
	{
		for (int s = sampleframes; s > 0; --s)
		{
			*out++ = *in++;
		}
		return;
	}

	int w = writeIndex;
	for (int s = sampleframes; s > 0; --s)
	{
		const float delayed = buffer[w];
		buffer[w] = *in++;
		*out++ = delayed;

		if (++w == bufferSamples)
			w = 0;
	}
	writeIndex = w;
}

void ug_compensated_delay::sub_process_static(int start_pos, int sampleframes)
{
	sub_process(start_pos, sampleframes);
	SleepIfOutputStatic(sampleframes);

	if (static_output_count <= 0) // line fully drained: output now holds the input's static value
	{
		OutputChange(SampleClock() + sampleframes, GetPlug(PN_OUT), ST_ONE_OFF);
	}
}

ug_delay::ug_delay() :
	buffer(nullptr)
	,interpolate(false)
	,count(0)
	,m_modulation_input_offset(0.5f)
	,m_prev_out(0.f)
{
}

/* Chris K..
!!!I noticed in your Delay2 - that the feedback is of the "Feedback into the next input sample" system.
With this system the Feedback signal is delayed One additional sample ( Feedback time = DelayTime + 1sm )
I think this is the most generally used - but causes the signal to drift/lag by 1 sample each feedback cycle - so after 44 feedback cycles the signal is 1ms late @44.1Khz.
(Also complicates tuned delay lines)
!!!
*/


void ug_delay::onSetPin(timestamp_t p_clock, UPlug* p_to_plug, state_type p_state)
{
	if( p_to_plug == GetPlug(PN_DELAY_TIME) )
	{
		CreateBuffer();
		return;
	}

	state_type mod_state = GetPlug(PN_MODULATION)->getState();
	state_type fb_state = GetPlug(PN_FEEDBACK)->getState();

    typedef void(ug_delay2::*myProcessPtr)(int,int);

	if( interpolate )
	{
		if( mod_state == ST_RUN )
		{
			if( fb_state == ST_RUN )
			{
                myProcessPtr s = &ug_delay::subProcess<PolicyModulationDigitalChanging, PolicyInterpolationLinear, PolicyFeedbackModulated >;
				SET_PROCESS_FUNC( s );

			}
			else
			{
				SET_PROCESS_FUNC( (myProcessPtr) (&ug_delay::subProcess<PolicyModulationDigitalChanging, PolicyInterpolationLinear, PolicyFeedbackFixed >) );
			}
		}
		else // Not modulated.
		{
			if( fb_state == ST_RUN )
			{
				SET_PROCESS_FUNC( (myProcessPtr) (&ug_delay::subProcess<PolicyModulationFixed, PolicyInterpolationLinear, PolicyFeedbackModulated >) );
			}
			else
			{
				SET_PROCESS_FUNC( (myProcessPtr) (&ug_delay::subProcess<PolicyModulationFixed, PolicyInterpolationLinear, PolicyFeedbackFixed >) );
			}
		}
	}
	else // no interpolate
	{
		if( mod_state == ST_RUN )
		{
			if( fb_state == ST_RUN )
			{
				//SET_PROCESS_FUNC( &ug_delay::sub_process_modulated_feedback );
				SET_PROCESS_FUNC( (myProcessPtr) (&ug_delay::subProcess<PolicyModulationDigitalChanging, PolicyInterpolationNone, PolicyFeedbackModulated >) );
			}
			else
			{
				//SET_PROCESS_FUNC( &ug_delay::sub_process_modulated );
				SET_PROCESS_FUNC( (myProcessPtr) (&ug_delay::subProcess<PolicyModulationDigitalChanging, PolicyInterpolationNone, PolicyFeedbackFixed >) );
			}
		}
		else // not modulate.
		{
			if( fb_state == ST_RUN )
			{
				//SET_PROCESS_FUNC( &ug_delay::sub_process_feedback );
				SET_PROCESS_FUNC( (myProcessPtr) (&ug_delay::subProcess<PolicyModulationFixed, PolicyInterpolationNone, PolicyFeedbackModulated >) );
			}
			else
			{
				//SET_PROCESS_FUNC( &ug_delay::sub_process );
				SET_PROCESS_FUNC( (myProcessPtr) (&ug_delay::subProcess<PolicyModulationFixed, PolicyInterpolationNone, PolicyFeedbackFixed >) );
			}
		}
	}

	// if input state changes, reset sta-tic output count
	if( p_to_plug == GetPlug(PN_SIGNAL) )
	{
		resetStaticCounter();
		OutputChange( p_clock, GetPlug(PN_OUTPUT), ST_RUN );
	}

	if( GetPlug(PN_SIGNAL)->getState() < ST_RUN )
	{
		current_process_func = process_function;
		SET_PROCESS_FUNC( &ug_delay::sub_process_static );
	}
}

// Shut down delay after signal died down
void ug_delay::sub_process_static(int start_pos, int sampleframes)
{
	(this->*(current_process_func))( start_pos, sampleframes );
	// now check output for signal
	float* output = output_ptr + start_pos;

	for( int s = sampleframes ; s > 0 ; s-- )
	{
		if( *output++ != 0.f )
		{
			resetStaticCounter();
			return;
		}
	}

	SleepIfOutputStatic(sampleframes);

	if( static_output_count <= 0 )
	{
		OutputChange( SampleClock() + sampleframes, GetPlug(PN_OUTPUT), ST_ONE_OFF );
	}
}

int ug_delay::Open()
{
	ug_base::Open();
	OutputChange( SampleClock(), GetPlug(PN_OUTPUT), ST_RUN );

	if( !buffer )
		CreateBuffer();

	return 0;
}

ug_delay::~ug_delay()
{
	if( buffer )
		delete [] buffer;
}

void ug_delay::CreateBuffer()
{
	if( buffer )
	{
		delete [] buffer;
		buffer = nullptr;
	}

	buffer_size = (int) ( getSampleRate() * delay_time );

	if( buffer_size < 1 )
		buffer_size = 1;

	if( buffer_size > getSampleRate() * 10 )	// limit to 10 s sample
		buffer_size = (int) getSampleRate() * 10;

//	_RPTN(0, "ug_delay[%0x]::CreateBuffer()  buffer_size = %d, modulation=%f\n", (int) this, buffer_size, *modulation_ptr);

	/*	buffer = new float[buffer_size];
	memset(buffer, 0, buffer_size * sizeof(float) ); // clear buffer
*/
	// allow extra to avoid overwritting 'tail'
	padded_size = buffer_size + interpolationExtraSamples;

	// Add some samples off-end to simplify interpolation.
	int allocatedSize = padded_size + interpolationExtraSamples;

	buffer = new float[allocatedSize];
	memset(buffer, 0, allocatedSize * sizeof(float) ); // clear buffer
	buffer[buffer_size + interpolationExtraSamples] = 10000000; // testing for glitches

	// ensure we arn't accessing data outside buffer
	count = 0;
	resetStaticCounter();
}
