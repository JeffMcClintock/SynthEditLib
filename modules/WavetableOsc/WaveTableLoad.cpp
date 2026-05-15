#include "WaveTableLoad.h"
#include "../shared/platform_string.h"

#define FIX_ZERO_CROSSINGS
#undef min
#undef max

WavetableLoader::WavetableLoader()
{
	// Mip-maps require extra memory. Calculate.
	int newSlotCount = WaveTable::MorphedSlotRatio * (WaveTable::WavetableFileSlotCount - 1) + 1; // add extra slots in-between.
	mipInfo.initialize(WaveTable::WavetableFileSampleCount, newSlotCount, true);
}

void WavetableLoader::InitBuffer(WaveTable* waveTableHeader)
{
	waveTableHeader->SetSize(mipInfo.getSlotCount(), WaveTable::WavetableFileSampleCount);
}

// Load wavetable off disk. The caller is expected to have already resolved 'filename' to a full
// path via synthedit::IEmbeddedFileSupport::findResourceUri.
void WavetableLoader::setWaveFileName(float* waveData, int osc, std::wstring& filename)
{
	if(loadedWavetables[osc] != filename)
	{
		// Mip-maps require extra memory. Calculate.
		int newSlotCount = WaveTable::MorphedSlotRatio * (WaveTable::WavetableFileSlotCount - 1) + 1; // add extra slots in-between.
		WavetableMipmapPolicy mipInfo2;
		mipInfo2.initialize(WaveTable::WavetableFileSampleCount, newSlotCount, true);

		loadedWavetables[osc] = filename;

		// Create temporary storage for wavetable file to load into.
		char WavetableStorage[sizeof(WaveTable)+(WaveTable::WavetableFileSampleCount * WaveTable::WavetableFileSlotCount - 1) * sizeof(float)];

		WaveTable* waveTableFile = (WaveTable*)&WavetableStorage;
		waveTableFile->SetSize(WaveTable::WavetableFileSlotCount, WaveTable::WavetableFileSampleCount);

		const bool success = waveTableFile->LoadFile3(ToPlatformString(filename).c_str(), true);

		if(!success)
		{
			// If we fail to load for whatever reason, generate a sine wave to prevent harsh noise.
			waveTableFile->GenerateWavetable(0, 0, WaveTable::WavetableFileSlotCount - 1, 2); // Generate a sine.
		}

		// Generate Mip-mapped wavetable with extra morphed 'ghost' slots.
		waveTableFile->CopyAndMipmap2(mipInfo, waveData);
	}
}

// Load wavetable from memory.
void WavetableLoader::setWaveFileName2(float* waveData, int osc, std::wstring& filename, WaveTable* sourceWaveTable)
{
	if(loadedWavetables[osc] != filename)
	{
		loadedWavetables[osc] = filename;

		// Generate Mip-mapped wavetable with extra morphed 'ghost' slots.
		sourceWaveTable->CopyAndMipmap2(mipInfo, waveData);
	}
}
