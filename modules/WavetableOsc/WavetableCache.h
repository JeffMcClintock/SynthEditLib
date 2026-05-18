#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "Wavetable.h"
#include "WavetableMipmapPolicy.h"

// One fully-loaded wavetable shared across all DSP voices, DSP instances and
// GUI instances that reference the same file. Members are written exactly once
// during the cache load; afterwards readers can access them lock-free across
// any number of threads.
struct CachedWavetable
{
	// 64-slot single-cycle form. Used by the GUI's 3D landscape display, and as
	// the source for the mip-map bake.
	std::vector<float> rawStorage;

	// Mip-mapped form, laid out per `mipInfo`. Used by the DSP audio loop.
	std::vector<float> bakedStorage;

	// Describes how `bakedStorage` is sliced into (slot, mipLevel) regions.
	WavetableMipmapPolicy mipInfo;

	WaveTable*       raw()       { return reinterpret_cast<WaveTable*>(rawStorage.data()); }
	const WaveTable* raw() const { return reinterpret_cast<const WaveTable*>(rawStorage.data()); }
	float*           baked()       { return bakedStorage.data(); }
	const float*     baked() const { return bakedStorage.data(); }
};

// Process-wide cache: same URI -> same `shared_ptr<CachedWavetable>`. Entries
// are held weakly, so the bake is freed when the last referencing instance
// goes away. Stale weak entries are pruned opportunistically on the next
// `getOrLoad` for the same key.
class WavetableCache
{
	std::mutex mtx_;
	std::unordered_map<std::string, std::weak_ptr<CachedWavetable>> entries_;

public:
	// Returns a baked wavetable for the given resolved URI. The first caller
	// for a URI pays the file-I/O + FFT-bake cost (under the cache mutex);
	// subsequent callers get the cached result immediately.
	// Returns a non-null pointer even on file-read failure - the bake will
	// contain a fallback sine wave so audio doesn't go silent.
	// Builtin test-wavetable names (e.g. "{Sine}", "{Square}", "{Saw}") are
	// synthesised via WaveTable::GenerateWavetable instead of loaded from disk.
	std::shared_ptr<CachedWavetable> getOrLoad(const std::string& fullUri);
};

WavetableCache& wavetableCache();

// If `name` is a builtin test-wavetable identifier like "{Sine}", returns the
// corresponding WaveTable::GenerateWavetable shape index. Otherwise returns -1.
// Callers should bypass the host's resource-URI resolver for builtins (those
// names don't exist on disk) and pass the name straight to getOrLoad.
int builtinWavetableShape(const std::string& name);
