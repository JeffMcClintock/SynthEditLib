#pragma once
// UIoManager.h
// This class communicates with the audio hardware.
// Audio and MIDI I/O should be encapsulated here.
// MIDI I/O is delegated to an IMidiDriver implementation.

#if !defined(AFX_UIOMANAGER_H__D74460C2_CA1E_11D4_B6F9_00104B15CCF0__INCLUDED_)
#define AFX_UIOMANAGER_H__D74460C2_CA1E_11D4_B6F9_00104B15CCF0__INCLUDED_

#include <chrono>
#include <vector>
#include <functional>
#include "EventProcessor.h"
#include "IMidiDriver.h"

class UIoManager;
class IO_base;

//std::vector<driverInfo> GetMidiDriverList();

typedef void (UIoManager::* io_func)(); // Pointer to member function

class UIoManager
{
	EventList events;
	void AddEvent(SynthEditEvent* p_event);
	void HandleEvent(SynthEditEvent* e);
	timestamp_t SampleClock() const
	{
		return private_sample_clock;
	}
	void SetSampleClock(timestamp_t p_sample_clock)
	{
		private_sample_clock = p_sample_clock;
	}
	timestamp_t private_sample_clock = 0;

public:
	UIoManager();
	bool Initialise(class SynthRuntime_editor* pclient, IO_base* p_audio_driver, const std::wstring& m_driver_guid, bool in_required, bool out_required);
	void OnRebuildDsp();
	virtual ~UIoManager();
	void Stop();
	void Render();
	bool GetRunRealtimeFlag()
	{
		return realtime_flag;
	}

	IO_base* AudioDriver()
	{
		return m_audio_driver;
	}

	IMidiDriver* MidiDriver()
	{
		return m_midi_driver;
	}

	void play_midi_buffer();
	void StartStream();
	int MIDISetup();
	int MidiStop();
	bool StartSoundcard();
	void StopSoundcard();
	void OnMidiData(timestamp_t timestamp, int size, unsigned char* data);
	void RegisterMidiInput(class MidiIn* ug);
	void RegisterMidiOutput( class ug_midi_out* ug);
	bool RegisterAudioInput();
	bool RegisterAudioOutput(float p_total_time = -1.f );
	int32_t RegisterIoModule(class ISpecialIoModule* m);
	void RenderBlock(int sampleFrames, const float** inputs, float** outputs, size_t inChannelCount, size_t outChannelCount);

	int	latency_ms = 0;
	int time_offset_midi_in{};
	int time_offset_midi{};
	int time_offset_audio{};
	int time_sync_interval{};
	std::chrono::steady_clock::time_point system_start_clock;
	int midi_time_correction{};
	int midi_in_timestamp_adjust{};
	bool realtime_flag{};

	bool hasAudioInput = false;
	bool hasAudioOutput = false;

	// list of midi-in devices to inform of MIDI data
	MidiIn* MidiInUg{};

	// MIDI stuff
	void TimeSync();
	void OnFirstSample();

	IO_base* m_audio_driver{};
	std::wstring m_audio_driver_guid;
	static int m_inhibit_midi_in_warning; // not in VST
	bool m_audio_driver_prepared;
	std::string CurrentMidiOutDev;
	std::string CurrentMidiInDevs;

	std::function<void(const wchar_t*)> m_error_message_callback;

	// MIDI driver abstraction (not owned; lifetime managed by the application)
	IMidiDriver* m_midi_driver{};

private:
	void RunAt( timestamp_t p_sampleclock, io_func p_func );
	void on_error();
	void ErrorMessage(std::wstring message);
};

#endif // !defined(AFX_UIOMANAGER_H__D74460C2_CA1E_11D4_B6F9_00104B15CCF0__INCLUDED_)
