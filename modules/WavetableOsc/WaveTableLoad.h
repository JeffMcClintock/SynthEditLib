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
	void setWaveFileName(float* dest, int osc, int wavetable, std::wstring& waveFilename);
    void setWaveFileName2(float* waveData, int osc, int wavetable, std::wstring& filename, WaveTable* sourceWaveTable);
	int WavebankMemoryRequired()
	{
		return mipInfo.TotalMemoryRequired();
	}
	int WaveMemoryRequiredSamples()
	{
		return mipInfo.WaveMemoryRequiredSamples();
	}
    WavetableMipmapPolicy& getMipInfo(){ return mipInfo;}

	static std::wstring UserWavetableFolder_;
	static std::wstring FactoryWavetableFolder_;

	std::wstring loadedWavetables[NUM_WAVETABLE_OSCS][WaveTable::MaximumTables];

private:
	WavetableMipmapPolicy mipInfo;
};
