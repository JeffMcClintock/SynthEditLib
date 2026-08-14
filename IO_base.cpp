// IO_base.cpp: implementation of the IO_base class.
//
//////////////////////////////////////////////////////////////////////

//#include <Windows.h>
#include "IO_base.h"
#include <algorithm>
#include "my_VstTimeInfo.h"
#include "UIoManager.h"
#include "SynthRuntime_editor.h"

IO_base::IO_base() :
	m_driver_list_initialised(false)
{
}

IO_base::~IO_base()
{
	while( !m_driver_list.empty() )
	{
		delete m_driver_list.front();
		m_driver_list.pop_front();
	}
}

void IO_base::AddDriverDescription(IO_output_info* p_info)
{
	m_driver_list.push_back( p_info );
}

void IO_base::Stop()
{
}

bool IO_base::StartStream()
{
	return true;
}

int IO_base::getOffsetMs()
{
	return 0;
}
/*
// returns time offset between system clock and audio out
int IO_base::ZoneSwitchOutput()
{
	return 0;
}
*/
constexpr double fScaler16 = (double)0x7fffL;
constexpr double fScaler24 = (double)0x7fffffL;
constexpr double fScaler32 = (double)0x7fffffffL;


void IO_base::float32toInt16(float* p_from, void* p_to, int frames)
{
	/*
	// slow way. can use as a corectness check
	double sc = fScaler16 + .49999;
	short* b = (short*) p_to;

	while(--frames >= 0)
	{
		*b++ = (short)((double)(*p_from++) * sc);
	}
	*/

	int scaler = 0x7FFF;
	short* b = (short*)p_to;

	while (--frames >= 0)
	{
		*b++ = static_cast<short>(0.5f + (std::min)(1.0f, (std::max)(-1.0f, *p_from++)) * (float)scaler);
	}
}

void IO_base::float32toInt24(float* p_from, void* p_to, int frames)
{
	int scaler = 0x7FFF00;
	long a;
	unsigned char* b = (unsigned char*)p_to;
	char* aa = (char*)&a;

	while(--frames >= 0)
	{
		float in = *p_from++;
		//in = min( in, 1.f );
		//in = max( in,-1.f );
		//a = (long)((double)in * sc);
		a = static_cast<int32_t>(0.5f + (std::min)(1.0f, (std::max)(-1.0f, in)) * (float)scaler);

#if ASIO_LITTLE_ENDIAN
		int compare = (b[0]<<16) + (b[1]<<8) + b[2];

		// sign extend to 32 bits for comparison
		if( (compare & 0x800000) != 0 )
			compare |= 0xff000000;

		assert( abs(compare - a) < 3 );
		*b++ = aa[2];
		*b++ = aa[1];
		*b++ = aa[0];
#else
		*b++ = aa[0];
		*b++ = aa[1];
		*b++ = aa[2];
#endif
	}
}

void IO_base::float32toInt32(float* source, void* destc, int frames)
{
	constexpr int32_t scaler = 0x7FFF0000;
	constexpr float c = static_cast<float>(scaler);
	int32_t* dest = reinterpret_cast<int32_t*>(destc);

#if defined(__SSE__) && defined(_WIN32)
	const __m128 scaler = _mm_set_ps1(c);
	const __m128 clipHi = _mm_set_ps1(1.0f);
	const __m128 clipLo = _mm_set_ps1(-1.0f);

	for (; frames > 3; frames -= 4)
	{
		_mm_storeu_si128((__m128i*)dest, _mm_cvtps_epi32(_mm_mul_ps(scaler, _mm_min_ps(clipHi, _mm_max_ps(clipLo, _mm_loadu_ps(source))))));
		source += 4;
		dest += 4;
	}

#else
	for (; frames > 0; frames--)
	{
		*dest++ = static_cast<int32_t>(std::round(std::clamp(*source++, -1.f, 1.f) * c));
	}
#endif
}

void IO_base::int16toFloat32(void* p_from, float* p_to, int frames)
{
	constexpr float sc = 1.f / ((float)fScaler16 + .49999f);
	short* b = (short*) p_from;

	for(int i = frames ; i > 0 ; i--)
		*p_to++ = sc * (float)*b++;
}

void IO_base::int24toFloat32(void* p_from, float* /*p_to*/, int frames)
{
	assert(false); //TODO!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
	constexpr float sc = 1.f / ((float)fScaler24 + .49999f);
	char* b = (char*) p_from;
	long a;
	char* aa = (char*)&a;

	for(int i = frames ; i > 0 ; i--)
	{
		a = (long) (sc * (float)*b++);
#if ASIO_LITTLE_ENDIAN
		*b++ = aa[3];
		*b++ = aa[2];
		*b++ = aa[1];
#else
		*b++ = aa[1];
		*b++ = aa[2];
		*b++ = aa[3];
#endif
	}
}

void IO_base::int32toFloat32(void* p_from, float* p_to, int frames)
{
	constexpr float sc = 1.f / ((float)fScaler32 + .49999f);
	int* b = (int*) p_from;

	for(int i = frames ; i > 0 ; i--)
		*p_to++ = sc * (float)*b++;
}

#if 0 // moved
void IO_base::InitialMusicTimeUpdate()
{
	my_VstTimeInfo timeInfo;
	memset(&timeInfo, 0, sizeof(my_VstTimeInfo)); // note will zero mask too, so save it if nesc
	timeInfo.tempo = 120.0;
	timeInfo.flags = my_VstTimeInfo::kVstTransportPlaying | my_VstTimeInfo::kVstTempoValid | my_VstTimeInfo::kVstTimeSigValid | my_VstTimeInfo::kVstBarsValid | my_VstTimeInfo::kVstPpqPosValid;
	timeInfo.timeSigNumerator = 4;
	timeInfo.timeSigDenominator = 4;
	timeInfo.flags = my_VstTimeInfo::kVstTransportPlaying;

	cli ent->UpdateTempo(&timeInfo);
}
#endif

void IO_base::RenderBlock(int sampleFrames)
{
	if (m_io_manager) // ASIO control panel may call here, even though m_io_manager don't exist yet
	{
		AudioInput(sampleFrames, (float**)pluginInputBuffersPtr.data());

		m_io_manager->RenderBlock(sampleFrames, (const float**)pluginInputBuffersPtr.data(), (float**)pluginOutputBuffersPtr.data(), pluginInputBuffersPtr.size(), pluginOutputBuffersPtr.size());

		AudioOutput(sampleFrames, (float**)pluginOutputBuffersPtr.data());
	}
}

void IO_base::AllocPluginBuffers(int size, int numIns, int numOuts)
{
	pluginInputBuffers.assign(numIns, {});
	pluginOutputBuffers.assign(numOuts, {});

	pluginInputBuffersPtr.clear();
	pluginOutputBuffersPtr.clear();
	pluginInputBuffersPtr.reserve(numIns);
	pluginOutputBuffersPtr.reserve(numOuts);

	for (auto& b : pluginInputBuffers)
	{
		b.assign(size, 0.f);
		pluginInputBuffersPtr.push_back(b.data());
	}
	for (auto& b : pluginOutputBuffers)
	{
		b.assign(size, 0.f);
		pluginOutputBuffersPtr.push_back(b.data());
	}
}
