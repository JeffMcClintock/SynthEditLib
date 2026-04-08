#include "WaveTableLoad.h"
#include "../shared/unicode_conversion.h"

#ifdef _WIN32
#include "windows.h"
#include "Shlobj.h"
#else
#include <pwd.h>
#include <sys/stat.h>
#endif

#define FIX_ZERO_CROSSINGS
#undef min
#undef max

std::wstring WavetableLoader::UserWavetableFolder_;
std::wstring WavetableLoader::FactoryWavetableFolder_;

WavetableLoader::WavetableLoader()
{
#ifdef _WIN32
	wchar_t myDocumentsPath[MAX_PATH];
	SHGetFolderPath(NULL, CSIDL_MYDOCUMENTS, NULL, SHGFP_TYPE_CURRENT, myDocumentsPath);
	std::wstring fn(myDocumentsPath);
	fn += L"\\Codex\\";
	UserWavetableFolder_ = fn;
	FactoryWavetableFolder_ = fn;
#else
	// macOS/Linux: Use home directory
	const char* homeDir = getenv("HOME");
	if (!homeDir) {
		struct passwd* pwd = getpwuid(getuid());
		if (pwd) {
			homeDir = pwd->pw_dir;
		}
	}

	if (homeDir) {
		std::wstring fn = Utf8ToWstring(homeDir);
		fn += L"/Documents/Codex/";
		UserWavetableFolder_ = fn;
		FactoryWavetableFolder_ = fn;
	} else {
		UserWavetableFolder_ = L"/tmp/Codex/";
		FactoryWavetableFolder_ = L"/tmp/Codex/";
	}
#endif

	// Mip-maps require extra memory. Calculate.
	int newSlotCount = WaveTable::MorphedSlotRatio * (WaveTable::WavetableFileSlotCount - 1) + 1; // add extra slots in-between.
	mipInfo.initialize(WaveTable::MaximumTables, WaveTable::WavetableFileSampleCount, newSlotCount, true);
}

void WavetableLoader::InitBuffer(WaveTable* waveTableHeader)
{
	waveTableHeader->SetSize(WaveTable::MaximumTables, mipInfo.getSlotCount(), WaveTable::WavetableFileSampleCount);
}

// Load wavetable off disk.
void WavetableLoader::setWaveFileName(float* waveData, int osc, int wavetable, std::wstring& filename)
{
	if(loadedWavetables[osc][wavetable] != filename)
	{
		// Mip-maps require extra memory. Calculate.
		int newSlotCount = WaveTable::MorphedSlotRatio * (WaveTable::WavetableFileSlotCount - 1) + 1; // add extra slots in-between.
		WavetableMipmapPolicy mipInfo2;
		mipInfo2.initialize(1, WaveTable::WavetableFileSampleCount, newSlotCount, true);

		loadedWavetables[osc][wavetable] = filename;
		std::wstring resourceWaveFilename = filename;

		// Copy Wavetable file into buffer.
		// Create temporary storage for wavetable file to load into.
		char WavetableStorage[sizeof(WaveTable)+(WaveTable::WavetableFileSampleCount * WaveTable::WavetableFileSlotCount - 1) * sizeof(float)];

		WaveTable* waveTableFile = (WaveTable*)&WavetableStorage;
		waveTableFile->SetSize(1, WaveTable::WavetableFileSlotCount, WaveTable::WavetableFileSampleCount);

		const wchar_t* searchPaths[] = { FactoryWavetableFolder_.c_str(), UserWavetableFolder_.c_str(), L"/Codex/" };

		// Load Wavetable off disk. Factory samples begin like "F99 whatever.wav".
		bool success = false;
		for(int s = 0; s < 3; ++s)
		{
			std::wstring filePath = searchPaths[s];
			if(s == 0)
				filePath += resourceWaveFilename;
			else
				filePath += filename;

			if(true == (success = waveTableFile->LoadFile3(filePath.c_str(), true)))
			{
				break;
			}
		}

		if(!success)
		{
			// If we fail to load for whatever reason, generate a sine wave to prevent harsh noise.
			waveTableFile->GenerateWavetable(0, 0, WaveTable::WavetableFileSlotCount - 1, 2); // Generate a sine.
		}

		// Generate Mip-mapped wavetable with extra morphed 'ghost' slots.
		waveTableFile->CopyAndMipmap2(mipInfo, wavetable, waveData);
	}
}

// Load wavetable from memory.
void WavetableLoader::setWaveFileName2(float* waveData, int osc, int wavetable, std::wstring& filename, WaveTable* sourceWaveTable)
{
	if(loadedWavetables[osc][wavetable] != filename)
	{
		loadedWavetables[osc][wavetable] = filename;

		// Generate Mip-mapped wavetable with extra morphed 'ghost' slots.
		sourceWaveTable->CopyAndMipmap2(mipInfo, wavetable, waveData);
	}
}
