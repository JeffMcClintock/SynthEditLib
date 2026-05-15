#pragma once

#include "WavetableMipmapPolicy.h"

/*
#include "WaveTableLoad.h"
*/

class WavetableLoader
{
public:
	WavetableLoader();

	void InitBuffer(WaveTable* waveTableHeader);
	void setWaveFileName(float* dest, int osc, std::wstring& waveFilename);
    void setWaveFileName2(float* waveData, int osc, std::wstring& filename, WaveTable* sourceWaveTable);
	int WavebankMemoryRequired()
	{
		return mipInfo.TotalMemoryRequired();
	}
	int WaveMemoryRequiredSamples()
	{
		return mipInfo.WaveMemoryRequiredSamples();
	}
    WavetableMipmapPolicy& getMipInfo(){ return mipInfo;}

	std::wstring loadedWavetables[NUM_WAVETABLE_OSCS];

private:
	WavetableMipmapPolicy mipInfo;
};
