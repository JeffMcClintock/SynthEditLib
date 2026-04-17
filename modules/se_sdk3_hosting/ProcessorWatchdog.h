#pragma once

/*
#include "ProcessorWatchdog.h"
*/

#include <functional>

#ifdef _DEBUG
#include <string>
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#define WATCHDOG_DEBUG_LOG(msg) OutputDebugStringA(msg)
#else
#include <cstdio>
#define WATCHDOG_DEBUG_LOG(msg) fprintf(stderr, "%s", msg)
#endif
#else
#define WATCHDOG_DEBUG_LOG(msg) ((void)0)
#endif

// Monitors DSP processor activity and notifies when processor goes offline/online.
// The watchdog timer counts down on each tick; if it reaches zero without being reset,
// the processor is considered offline. When a message is received, the counter resets
// and the processor is considered online.
class ProcessorWatchdog
{
public:
	using OfflineCallback = std::function<void(bool isOffline)>;

	ProcessorWatchdog() = default;

	explicit ProcessorWatchdog(int timerPeriodMs)
		: timerInit(1000 / timerPeriodMs)
		, counter(timerInit)
	{
		WATCHDOG_DEBUG_LOG(("ProcessorWatchdog: ctor timerPeriodMs=" + std::to_string(timerPeriodMs) +
			" timerInit=" + std::to_string(timerInit) + "\n").c_str());
	}

	// Set the callback to be invoked when offline status changes
	void setCallback(OfflineCallback callback)
	{
		WATCHDOG_DEBUG_LOG(("ProcessorWatchdog: setCallback hasCallback=" +
			std::to_string(callback != nullptr) + "\n").c_str());
		onOfflineChanged = std::move(callback);
		counter = timerInit;
	}

	// Call when a message is received from the DSP.
	// Resets the watchdog counter and notifies if processor came back online.
	void onDspMessage()
	{
		if (counter <= 0)
		{
			if(onOfflineChanged)
			{
				WATCHDOG_DEBUG_LOG("ProcessorWatchdog: onDspMessage -> ONLINE (was offline)\n");
				onOfflineChanged(false); // processor is online
			}
			else
			{
				WATCHDOG_DEBUG_LOG("ProcessorWatchdog: onDspMessage -> ONLINE (no one listening)\n");
			}
		}
		counter = timerInit;
	}

	// Call on each timer tick.
	void onTimerTick()
	{
		if (--counter == 0)
		{
			if(onOfflineChanged)
			{
				WATCHDOG_DEBUG_LOG("ProcessorWatchdog: onTimerTick -> OFFLINE\n");
				onOfflineChanged(true); // processor is offline
			}
			else
			{
				WATCHDOG_DEBUG_LOG("ProcessorWatchdog: onTimerTick -> OFFLINE (no one listening)\n");
			}
		}
	}

private:
	static constexpr int defaultTimerPeriodMs = 35;
	int timerInit = 1000 / defaultTimerPeriodMs; // default ~1 second timeout
	int counter = timerInit;
	OfflineCallback onOfflineChanged;
};
