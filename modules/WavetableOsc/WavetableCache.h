#pragma once

#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Wavetable.h"
#include "WavetableMipmapPolicy.h"

// The single-cycle form of one wavetable file, at whatever slot count the source turned out
// to hold. Nothing about it depends on the sample rate, so every referencing instance shares
// one of these however many rates are in play. Drawn directly by the GUI's 3D landscape
// display, and the source the mip bake is derived from.
struct RawWavetable
{
	std::vector<float> storage;

	WaveTable*       get()       { return reinterpret_cast<WaveTable*>(storage.data()); }
	const WaveTable* get() const { return reinterpret_cast<const WaveTable*>(storage.data()); }
};

// One fully-baked wavetable shared across all DSP voices and DSP instances that reference the
// same file at the same sample rate. Members are written exactly once during the cache load;
// afterwards readers can access them lock-free across any number of threads.
struct CachedWavetable
{
	// Keeps the raw form alive for as long as the bake derived from it lives.
	std::shared_ptr<RawWavetable> rawTable;

	// Mip-mapped form, laid out per `mipInfo`. Used by the DSP audio loop. This is the
	// expensive part - tens of MB - which is why the GUI takes the raw form on its own.
	std::vector<float> bakedStorage;

	// Describes how `bakedStorage` is sliced into (slot, mipLevel) regions.
	WavetableMipmapPolicy mipInfo;

	WaveTable*       raw()       { return rawTable->get(); }
	const WaveTable* raw() const { return rawTable->get(); }
	float*           baked()       { return bakedStorage.data(); }
	const float*     baked() const { return bakedStorage.data(); }
};

// Process-wide cache, in two levels:
//   URI                -> `shared_ptr<RawWavetable>`   (the file, loaded and sliced once)
//   (URI, sample_rate) -> `shared_ptr<CachedWavetable>` (a mip bake of that raw form)
// Entries are held weakly, so each level is freed once its last referencing instance goes
// away. Stale weak entries are pruned opportunistically on the next lookup of the same key.
//
// The split exists because the two levels cost wildly different amounts and have different
// lifetimes: the raw form is well under a megabyte and is rate-independent, while a bake runs
// to tens of MB and is per-rate (mip boundaries are measured in cycles-per-sample). Callers
// that only need the waveform - the display - take the raw form and never pay for a bake.
//
// Locking: the mutex guards only the maps and the building-key sets. The slow work - file
// I/O and the FFT bake - happens OUTSIDE the lock, with in-progress keys tracked in
// buildingRaw_/buildingBaked_ so a second caller for the SAME key waits on the condition
// variable rather than duplicating the work. Callers for other keys - notably the GUI thread
// fetching a raw waveform while the loader thread bakes something else - never block.
class WavetableCache
{
	struct CacheKey
	{
		std::string uri;
		float sampleRate;
		bool operator==(const CacheKey& o) const { return uri == o.uri && sampleRate == o.sampleRate; }
	};
	struct CacheKeyHash
	{
		size_t operator()(const CacheKey& k) const
		{
			return std::hash<std::string>{}(k.uri) ^ (std::hash<float>{}(k.sampleRate) << 1);
		}
	};

	std::mutex mtx_;
	std::condition_variable buildDone_;
	std::unordered_map<std::string, std::weak_ptr<RawWavetable>> rawEntries_;
	std::unordered_map<CacheKey, std::weak_ptr<CachedWavetable>, CacheKeyHash> entries_;

	// Keys whose load/bake is currently running outside the lock.
	std::unordered_set<std::string> buildingRaw_;
	std::unordered_set<CacheKey, CacheKeyHash> buildingBaked_;

	// The actual file-read + slicing, no locking. Never returns null (falls back to sine).
	static std::shared_ptr<RawWavetable> buildRaw(const std::string& fullUri);

public:
	// Returns the single-cycle form for the given resolved URI, without baking anything.
	// This is what the display wants: it draws the waveform and never touches a mip.
	// Returns a non-null pointer even on file-read failure - the table will contain a
	// fallback sine wave.
	// Builtin test-wavetable names (e.g. "{Sine}", "{Square}", "{Saw}") are
	// synthesised via WaveTable::GenerateWavetable instead of loaded from disk.
	std::shared_ptr<RawWavetable> getOrLoadRaw(const std::string& fullUri);

	// Returns a baked wavetable for the given resolved URI + sample rate. The first
	// caller for a (URI, SR) pays the load + FFT-bake cost (outside the cache mutex);
	// concurrent callers for the same key wait for that one result, and everyone after
	// gets the cached entry immediately. The raw form underneath is shared with any
	// other rate, and with the display.
	std::shared_ptr<CachedWavetable> getOrLoad(const std::string& fullUri, float sampleRate);
};

WavetableCache& wavetableCache();

// If `name` is a builtin test-wavetable identifier like "{Sine}", returns the
// corresponding WaveTable::GenerateWavetable shape index. Otherwise returns -1.
// Callers should bypass the host's resource-URI resolver for builtins (those
// names don't exist on disk) and pass the name straight to getOrLoad.
int builtinWavetableShape(const std::string& name);
