#pragma once

/*
#include "ProcessorWatchdog.h"
*/

#include <functional>

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
	}

	// Set the callback to be invoked when offline status changes
	void setCallback(OfflineCallback callback)
	{
		onOfflineChanged = std::move(callback);
	}

	// Call when a message is received from the DSP.
	// Resets the watchdog counter and notifies if processor came back online.
	void onDspMessage()
	{
		if (counter <= 0 && onOfflineChanged)
			onOfflineChanged(false); // processor is online

		counter = timerInit;
	}

	// Call on each timer tick.
	void onTimerTick()
	{
		if (--counter == 0 && onOfflineChanged)
			onOfflineChanged(true); // processor is offline
	}

private:
	static constexpr int defaultTimerPeriodMs = 35;
	int timerInit = 1000 / defaultTimerPeriodMs; // default ~1 second timeout
	int counter = timerInit;
	OfflineCallback onOfflineChanged;
};
