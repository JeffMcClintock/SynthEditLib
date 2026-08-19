#pragma once

#include <chrono>
#include <vector>
#include <string>
#include <functional>
#include "se_types.h"

struct driverInfo
{
	int nativeIndex{ -1 }; // as used by midiInGetDevCaps etc.
	std::string id;
	std::string name;
	bool is_output{};
};

// Abstract MIDI driver interface.
// Platform-specific implementations handle MIDI I/O (e.g. Windows legacy MIDI, macOS CoreMIDI, MIDI 2.0).
class IMidiDriver
{
public:
	virtual ~IMidiDriver() = default;

	// Enumerate available MIDI devices.
	virtual std::vector<driverInfo> GetDriverList() = 0;

	// Set up MIDI input and output devices. Returns 0 on success.
	virtual int Setup(
		const std::string& midiInDevs,
		const std::string& midiOutDev,
		class IO_base* audioDriver,
		std::function<void(timestamp_t, int, unsigned char*)> onMidiInput
	) = 0;

	// Stop MIDI input and output. Returns 0 on success.
	virtual int Stop() = 0;

	// Send a MIDI output message.
	virtual void Output(int ms_clock, int msg_size, const unsigned char* midi_bytes) = 0;

	// Start the MIDI streams (called after audio driver is started).
	virtual void StartStreams(
		std::chrono::steady_clock::time_point systemStartClock,
		int& time_offset_midi_in
	) = 0;

	// Play/rotate the next MIDI output buffer.
	virtual void PlayBuffer(unsigned int ms_generated) = 0;

	// Update MIDI timing synchronisation.
	virtual void TimeSync(
		int time_offset_audio,
		int latency_ms,
		int& time_offset_midi,
		int& midi_time_correction,
		int& midi_in_timestamp_adjust,
		int& time_offset_midi_in
	) = 0;

	// Query whether MIDI output is active.
	virtual bool IsMidiOutInUse() const = 0;

	// Query whether MIDI input is active.
	virtual bool IsMidiInOpen() const = 0;

	// Get the MIDI output buffer switch period in samples.
	virtual int GetBufferSwitchPeriod() const = 0;

	// Get MIDI time correction value for output.
	virtual int GetMidiTimeCorrection() const = 0;

	// Set the error message callback.
	virtual void SetErrorCallback(std::function<void(const wchar_t*)> callback) = 0;
};
