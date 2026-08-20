#pragma once

/*

#include "FrameRateLogger.h"

#if defined( _DEBUG )
	static FrameRateLogger l;
	l.OnFrame();
#endif

*/

class FrameRateLogger
{
	int prevTime_;
	int count_;

public:
	FrameRateLogger();
	~FrameRateLogger();
	void OnFrame();
};

