#pragma once

struct HoverScopeAudioCollector
{
	const float* buffer{};
	float resultsA_[400 + 1]; // 4x as big when oversampling. +1 for sample-rate.
	int index_{};
	int timeoutCount_{};
	int channelsleepCount_{};
	int captureSamples{ 400 };
	int state = 1; // 0 = idle, 1 = waiting for trigger, 2 = waiting for trigger +ve, 3 = capturing, 4 = cruise.
	class IWriteableQue* queue{};
	int32_t moduleHandle{};

	void process(int blockPosition, int sampleFrames);
};