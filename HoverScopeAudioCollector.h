#pragma once

namespace gmpi { namespace hosting{
class IWriteableQue;
}}

struct HoverScopeAudioCollector
{
	HoverScopeAudioCollector(
		int32_t pmoduleHandle
		, int psampleRate
		, float* pbuffer
		, gmpi::hosting::IWriteableQue* pqueue
	);
	const float* buffer{};
	float resultsA_[1024 + 1]; // 4x as big when oversampling. +1 for sample-rate.
	float sampleRate{};
	int samplesPerPoint{};
	float pointMax{};
	float pointMin{};
	int pointCounter{};

	int index_{};
	int timeoutCount_{};
	int channelsleepCount_{};
	int captureSamples{ 400 };
	int state = 1; // 0 = idle, 1 = waiting for trigger, 2 = waiting for trigger +ve, 3 = capturing, 4 = cruise.
	gmpi::hosting::IWriteableQue* queue{};
	int32_t moduleHandle{};

	void process(int blockPosition, int sampleFrames);
};