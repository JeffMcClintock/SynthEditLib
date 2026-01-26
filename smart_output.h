#pragma once

#define _USE_MATH_DEFINES
#include <math.h>
#include "se_types.h"
#include "ug_base.h"
#include "UPlug.h"

enum smart_output_curve_type { SOT_NO_SMOOTHING, SOT_LINEAR, SOT_S_CURVE, SOT_LOW_PASS };

class smart_output
{
public:
    smart_output(smart_output_curve_type p_curve_type = SOT_LINEAR, int p_transition_samples = 4) :
	    output_level(0.f)
	    ,target(0.f)
	    ,plug(NULL)
	    ,m_transition_samples( p_transition_samples )
	    ,curve_type(p_curve_type)
	    ,static_output_count(0)
	    ,m_static_mode(true)
    {
    }
	void Resume()
    {
	    // if voice suspends/resumes, ug will be at random block pos,
	    // so static_output_count needs resetting
	    if( static_output_count > 0 )
	    {
		    static_output_count = plug->UG->AudioMaster()->BlockSize();
	    }
    }
	void SetPlug( UPlug* p_plug)
    {
	    assert( p_plug->Direction == DR_OUT );
	    assert( p_plug->DataType == DT_FSAMPLE );
	    plug = p_plug;
	    static_output_count = plug->UG->AudioMaster()->BlockSize();
	    m_output_ptr = plug->GetSamplePtr();
	    // send initial status.
	    target = 1.0f; // to ensure stat change is triggerd.
	    Set(0,0.0f,0);
    }
	void Set( timestamp_t p_sample_clock, float new_value )
    {
	    Set( p_sample_clock, new_value, m_transition_samples ); // use default smoothing
    }
	void Set( timestamp_t p_sample_clock, float new_value, int transition_samples)
    {
	    if(	new_value == target && m_static_mode ) // already set to this value and not headed elsewhere)
	    {
		    assert( output_level == new_value );
		    return;
	    }

	    m_static_mode = false;
	    smart_output_curve_type c_type = curve_type;

	    if( transition_samples <= 0 )
	    {
		    c_type = SOT_NO_SMOOTHING;
	    }

	    switch( c_type )
	    {
	    case SOT_NO_SMOOTHING:
		    transition_samples = 1;

		    // deliberate fall-through
	    case SOT_LINEAR:
		    InitLinear( new_value, transition_samples);
		    break;

	    case SOT_S_CURVE:
		    InitSmooth( new_value, transition_samples);
		    break;

	    case SOT_LOW_PASS:
    #if defined( _DEBUG )
		    assert( transition_samples > 1 );
		    InitLowPass( new_value, transition_samples);
    #endif
		    break;
	    };

	    if( transition_samples > 1 ) // minimise spurious stat changes
	    {
		    plug->TransmitState( p_sample_clock, ST_RUN );
	    }
    }
	inline void Process(int start_pos, int sampleframes, bool& can_sleep )
    {
	    assert( plug != NULL );

	    if( m_static_mode )
	    {
		    if( static_output_count > 0 )
		    {
			    //			plug->GetSampleBlock()->SetRange( start_pos, sampleframes, output_level );
			    float* out = m_output_ptr + start_pos;
			    int todo = sampleframes;

			    if( static_output_count < todo )
				    todo = static_output_count;

			    for( int s = todo ; s > 0 ; s-- )
			    {
				    *out++ = output_level;
			    }

			    static_output_count -= sampleframes;
			    can_sleep = false;
		    }
	    }
	    else
	    {
		    can_sleep = false;
		    bool more = process( start_pos, sampleframes );

		    if( !more )
		    {
			    m_static_mode = true;
			    static_output_count = plug->UG->AudioMaster()->BlockSize();
		    }
	    }
    }
	float output_level;
	UPlug* plug;
	int m_transition_samples;
	smart_output_curve_type curve_type;
	int static_output_count;
private:
	void InitSmooth( float p_end, int p_sample_count)
    {
	    float N = (float)p_sample_count; //(mili_seconds * 1000 ) / sample_rate;	//nr of samples
	    float A = p_end - output_level;   //amp
	    v   = output_level;
	    float NNN = N*N*N;
	    dv  = A*(3*N - 2)/(NNN);  //difference v
	    ddv = 6*A*(N - 2)/(NNN);  //difference dv
	    c   = -12*A/(NNN);        //constant added to ddv
	    count = p_sample_count-1;
	    target = p_end;
	    // take first step
	    v += dv;
	    dv += ddv;
	    ddv += c;
    }

	void InitLinear( float p_end, int p_sample_count)
    {
	    float N = (float) p_sample_count;
	    float A = p_end - output_level;   //amp
	    v   = output_level;
	    dv  = A / N;		//difference v
	    ddv = 0.0f;			//difference dv
	    c   = 0.0f;			//constant added to ddv
	    count = p_sample_count-1;
	    target = p_end;
	    // take first step
	    v += dv;
    }

    bool process( int start_pos, int sampleframes)
    {
	    float* out = m_output_ptr + start_pos;
    #if defined( _DEBUG )

	    if( curve_type == SOT_LOW_PASS )
	    {
		    for( int s = sampleframes ; s > 0 ; s-- )
		    {
			    *out++ = v * scale;
			    v = 0;
    #if defined( zero_stuffed )
			    v = m_filter.filter(0.0);
    #else
			    //				v = m_filter.filter(target);
    #endif
		    }

		    output_level = v * scale;
		    return true;
	    }

    #endif

		// more robust in face of rounding errors
		if (curve_type == SOT_LINEAR)
		{
			for (int s = sampleframes; s > 0; s--)
			{
				*out++ = v;

				if ((dv > 0.0 && v >= target) || (dv <= 0.0 && v <= target))
				{
					v = target;
					output_level = v;
					*(out - 1) = v; // ensure output *exact*

					plug->TransmitState(plug->UG->AudioMaster()->BlockStartClock() + start_pos + sampleframes - s, ST_STATIC);
					// fill remainder of block if nesc
					s--;

					for (; s > 0; s--)
					{
						*out++ = v;
					}

					return false;
				}
				v += dv;

				assert(ddv == 0.f);
				assert(c == 0.f);
			}
		}
		else
		{
			for (int s = sampleframes; s > 0; s--)
			{
				*out++ = v;

				if (--count < 0) // done?
				{
					v = target;
					output_level = v;
					*(out - 1) = v; // ensure output *exact*
					plug->TransmitState(plug->UG->AudioMaster()->BlockStartClock() + start_pos + sampleframes - s, ST_STATIC);
					// fill remainder of block if nesc
					s--;

					for (; s > 0; s--)
					{
						*out++ = v;
					}

					return false;
				}

				v += dv;
				dv += ddv;
				ddv += c;
			}
		}

	    output_level = v;
	    return true; // more to do
    }
	float* m_output_ptr;
	bool m_static_mode;
	float dv;	//difference v
	float ddv;	//difference dv
	float c;	//constant added to ddv
	float v;	// output val
	int count;
	float target;

#if defined( _DEBUG )
void InitLowPass( float p_end, int p_sample_count)
{
	float freq_hz = 0.30f * plug->UG->getSampleRate() / p_sample_count; // smooth at update freq / 2 (nyquist)
	m_filter_constant = expf( -(2.0 * M_PI) * freq_hz / plug->UG->getSampleRate());
#if defined( zero_stuffed )
	scale = 0.63 * (float) p_sample_count;
#else
	scale = 1; //200;
#endif
	target = p_end;
	v   = output_level / scale;
	//	v = target + m_filter_constant * ( v - target );
//	m_filter.calc_filter_coeffs(0, freq_hz, plug->UG->SampleRate(), 0.3, 0 ,false);
	//	m_filter.calc_filter_coeffs(7, freq_hz, plug->UG->SampleRate(), 0.3, 0 ,false);
#if defined( zero_stuffed )
	v = m_filter.filter(target);
#endif
	v = target;
}
	float scale;
	float m_filter_constant;
//	CFxRbjFilter m_filter;
#endif
};
