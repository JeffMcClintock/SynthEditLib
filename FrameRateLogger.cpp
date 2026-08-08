#ifdef _WIN32
#undef  WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#undef  NOMINMAX
#define NOMINMAX
#include <Windows.h>
#include "mmsystem.h"
#else
//#include "Timer.h"
//#include "MacTypes.h"
#endif
#include "FrameRateLogger.h"

FrameRateLogger::FrameRateLogger() : count_(0), prevTime_(0)
{
}


FrameRateLogger::~FrameRateLogger()
{
}

inline int get_sys_reference_time()
{
	// get the system reference time
#if _WIN32
	return timeGetTime();
#else
    /*
	static const double twoRaisedTo32 = 4294967296.;
    UnsignedWide ys;
	Microseconds(&ys);
	double r = ((double)ys.hi * twoRaisedTo32 + (double)ys.lo);
	return (int)(r / 1000.);
     */
    return 0;
#endif
}

void FrameRateLogger::OnFrame()
{
	++count_;

	int time1 = get_sys_reference_time();

	int delta = time1 - prevTime_;

	if( delta > 1000 )
	{
		float framerate = (float) count_ / (0.001f * delta );
//		_RPT1(_CRT_WARN, "Framerate %.2f Hz\n", framerate );
		prevTime_ = time1;
		count_ = 0;
	}
}
