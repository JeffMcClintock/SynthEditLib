#pragma once

// Flip this on to log MIDI parsing + direct-path fan-out + voice-allocation events to a file.
// Useful for diagnosing test failures where audio diverges from reference.
// Each participating .cpp should include this header AFTER defining DEBUG_CONTAINER_MIDI=1 if
// it wants logging enabled.
//
// Output path: C:\temp\midi_log.txt.

#ifdef DEBUG_CONTAINER_MIDI
#include <cstdio>

#define DMIDI_LOG(...) do { \
	std::fprintf(stderr, "[MIDILOG] "); \
	std::fprintf(stderr, __VA_ARGS__); \
	std::fflush(stderr); \
} while(0)

#else
#define DMIDI_LOG(...) ((void)0)
#endif
