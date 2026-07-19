#include "./WavetableOsc.h"
#include "Extensions/EmbeddedFile.h"

#undef min
#undef max

SE_DECLARE_INIT_STATIC_FILE(WavetableOsc);

using namespace gmpi;

namespace {
bool registered = Register<WavetableOsc>::withXml(R"XML(
<?xml version="1.0" encoding="utf-8" ?>
<PluginList>
  <Plugin id="SE Wavetable Osc" name="Wavetable Osc" category="Waveform" helpUrl="WavetableOsc.htm">
	<Audio>
	  <Pin name="Pitch" datatype="float" rate="audio" default="0.5"/>
	  <Pin name="Slot" datatype="float" rate="audio" />
	  <Pin name="Signal Out" direction="out" datatype="float" rate="audio"/>
	  <Pin name="VoiceActive" hostConnect="Voice/Active" datatype="float" isPolyphonic="true" default="1" />
	  <Pin name="WaveTableFile" datatype="string" isFilename="true" metadata="wav" />
	  <Pin name="RenderMode" datatype="int" hostConnect="Processor/OfflineRenderMode" />
	</Audio>
  </Plugin>
  <Plugin id="SE Wavetable Display" name="Wavetable Display" category="Waveform">
	<GUI>
	  <Pin name="WaveTableFile" datatype="string" isFilename="true" metadata="wav" />
	  <Pin name="Slot" datatype="float" />
	</GUI>
  </Plugin>
</PluginList>
)XML");
}

WavetableOsc::WavetableOsc()
{
	// Equal-amplitude crossfade table: curve[i] + curve[N - i] = 1 exactly, so a
	// fade-in + fade-out pair always sums to unity gain. Without this property the
	// crossfade midpoint boosts the signal by ~17% and shows up as a visible blip
	// on the output, especially for harmonically-rich waveforms (ramp, saw).
	for (int i = 0; i < (int)syncFadeCurve_.size(); ++i)
	{
		syncFadeCurve_[i] = 0.5f - 0.5f * cosf((float)M_PI * (float)i / (float)syncCrossFadeSamples);
	}
}

namespace {
// Processor/OfflineRenderMode pin values (from the host): 0 = Live (real-time), 2 = offline
// render. In offline mode there's no real-time deadline, so we load synchronously instead of
// on the background thread.
constexpr int kRenderModeOffline = 2;

// Pitch pin is normalised 0..1 across a 10 V range; A4 (440 Hz) sits at the midpoint.
constexpr double kMaxVolts = 10.0;

constexpr double sampleToVoltage(double s)
{
	return s * kMaxVolts;
}

inline double sampleToFrequency(double s)
{
	return 440.0 * exp2(sampleToVoltage(s) - kMaxVolts * 0.5);
}
} // namespace

ReturnCode WavetableOsc::open(api::IUnknown* phost)
{
	auto r = Processor::open(phost);

	const float sampleRate = host->getSampleRate();
	sampleRate_ = sampleRate;

	// Pitch lookup table - shared across instances at same sample rate.
	const int extraEntriesAtStart = 1; // for interpolator.
	const int extraEntriesAtEnd = 3; // for interpolator.
	const int pitchTableSize = extraEntriesAtStart + extraEntriesAtEnd + (pitchTableHiVolts - pitchTableLowVolts) * 12;
	constexpr double oneSemitone = 1.0/12.0;

	pitchTableShared_ = SharedObjectManager<PitchTableData>::getOrCreateSharedMemory(
		sampleRate, 0,
		[&](float sr) {
			auto p = std::make_shared<PitchTableData>();
			p->data.resize(pitchTableSize);
			for (int i = 0; i < pitchTableSize; ++i)
			{
				double pitch = (pitchTableLowVolts + (i - extraEntriesAtStart) * oneSemitone) * 0.1;
				double hz = sampleToFrequency(pitch);
				p->data[i] = hz / (double)sr;
			}
			return p;
		});
	pitchTable = pitchTableShared_->data.data() + extraEntriesAtStart;

	// Acquire the shared background loader here, off the audio thread - the first acquisition
	// constructs it and spawns its worker, which must never happen inside the audio callback.
	loader_ = wavetableLoader();

	// Wavetable buffer allocation happens lazily in onSetPins, via the
	// process-wide WavetableCache keyed on (URI, sample rate).

	return r;
}

using WavetableOscProcess_ptr = void (WavetableOsc::*)(int sampleFrames);

constexpr WavetableOscProcess_ptr ProcessSelection[2][2] =
{
	&WavetableOsc::subProcess<PitchFixed,    SlotFixed>,
	&WavetableOsc::subProcess<PitchFixed,    SlotChanging>,
	&WavetableOsc::subProcess<PitchChanging, SlotFixed>,
	&WavetableOsc::subProcess<PitchChanging, SlotChanging>,
};

void WavetableOsc::onSetPins(void)
{
	if (pinWaveTableFile.isUpdated())
	{
		// Supersede any load still in flight: mark it cancelled and hand our reference to the
		// worker's dispose queue. The request may already hold a finished multi-MB bake that
		// we'd otherwise be freeing right here on the audio thread.
		if (pending_)
		{
			pending_->cancelled.store(true, std::memory_order_release);
			loader_->dispose(std::move(pending_));
		}

		// Resolve the filename here on the audio thread - it's cheap and the host calls have
		// audio-thread affinity; only the resolved URI ever crosses to the worker.
		std::string fullUri;
		const std::string& curWaveFile = pinWaveTableFile.getValue();
		if (builtinWavetableShape(curWaveFile) >= 0)
		{
			// Builtin test wavetable - skip host resource resolution, the name is the cache key.
			fullUri = curWaveFile;
		}
		else if (auto synthEditHost = host.as<synthedit::IEmbeddedFileSupport>())
		{
			ReturnString fullFilename;
			if (synthEditHost->findResourceUri(curWaveFile.c_str(), &fullFilename) == ReturnCode::Ok)
			{
				synthEditHost->registerResourceUri(fullFilename.c_str());
				fullUri = fullFilename.c_str();
			}
		}

		if (fullUri.empty())
		{
			// No file - unload and fall silent.
			adoptTable(nullptr);
		}
		else if (pinRenderMode == kRenderModeOffline)
		{
			// Offline/bounce render: there's no real-time deadline, so load + bake synchronously
			// on this thread. Blocking is correct here - it guarantees the render is never left
			// silent waiting on a worker. The old table is held until getOrLoad returns (see
			// adoptTable), so a same-file reassignment is a cache hit, not a reload.
			adoptTable(wavetableCache().getOrLoad(fullUri, sampleRate_));
		}
		else
		{
			// Live / real-time: loading + baking is far too slow for the audio thread, so hand it
			// to the background loader and cut to silence until it lands. Keep the *old* table
			// referenced (waveTable_ untouched) so it stays in memory - shared with any other
			// voice, and reused rather than reloaded if the new file resolves to it - but stop
			// playing it (waveData_ = nullptr). The new table is kept alive by pending_->result
			// while it loads, so old and new can coexist (e.g. while a sleeping voice lags behind).
			waveData_ = nullptr;
			for (auto& g : grains) g.waveSize = 0;

			pending_ = std::make_shared<WavetableLoadRequest>();
			pending_->uri = std::move(fullUri);
			pending_->sampleRate = sampleRate_;
			loader_->enqueue(pending_);
		}
	}

	// DCO (Sync to note-on) mode - reset grain phase on voice activation.
	if (pinVoiceActive.isUpdated())
	{
		const bool newActiveState = pinVoiceActive > 0.0f;

		if (newActiveState != previousActiveState && newActiveState)
		{
			for (auto& g : grains) g.waveSize = 0; // next subProcess will spawn a fresh grain at phase 0.
		}
		previousActiveState = newActiveState;
	}

	chooseSubProcess();
}

void WavetableOsc::chooseSubProcess()
{
	if (waveData_)
	{
		pinSignalOut.setStreaming(true);
		setSubProcess(static_cast<SubProcessPtr>(
			ProcessSelection[pinPitch.isStreaming()][pinSlot.isStreaming()]));
	}
	else if (pending_)
	{
		// A load is in flight - output silence, but keep the pin streaming so the module
		// can't sleep: subProcessLoading must run every block to poll for completion.
		pinSignalOut.setStreaming(true);
		setSubProcess(&WavetableOsc::subProcessLoading);
	}
	else
	{
		// Nothing loaded and nothing loading (e.g. empty filename, or before the first file).
		// The output is statically zero - mark it non-streaming so this module and everything
		// downstream can sleep once the buffers drain.
		pinSignalOut.setStreaming(false);
		setSubProcess(&WavetableOsc::subProcessSilence);
	}
}

void WavetableOsc::subProcessSilence(int sampleFrames)
{
	float* signalOut = getBuffer(pinSignalOut);
	for (int s = 0; s < sampleFrames; ++s)
		signalOut[s] = 0.0f;
}

void WavetableOsc::subProcessLoading(int sampleFrames)
{
	if (pending_ && pending_->ready.load(std::memory_order_acquire))
	{
		publishLoaded(); // selects the real subProcess (or silence if the load somehow failed).
		(this->*getSubProcess())(sampleFrames); // render this block on the freshly-adopted setup.
		return;
	}

	// Still loading: emit clean silence, and keep the module awake so process() keeps being
	// called - otherwise polling stops and the finished table never gets installed.
	subProcessSilence(sampleFrames);
	nudgeSleepCounter();
}

void WavetableOsc::adoptTable(std::shared_ptr<CachedWavetable> table)
{
	// Ship the outgoing table to the worker for disposal *before* releasing our slot - with
	// the cache holding only weak references, this instance may own the last reference to a
	// tens-of-MB bake, and that free must not run inside the audio callback. Because `table`
	// is already held here, a same-file reassignment remains a cache hit throughout.
	if (waveTable_ && loader_)
		loader_->dispose(std::move(waveTable_));

	waveTable_ = std::move(table);

	if (waveTable_)
	{
		waveData_    = waveTable_->baked();
		mipMapPolicy = waveTable_->mipInfo;
		slotCount    = mipMapPolicy.getSlotCount();
	}
	else
	{
		waveData_ = nullptr;
	}

	// Fresh wavetable - reset grains so subProcess lazily re-inits at the new table.
	for (auto& g : grains) g.waveSize = 0;
}

void WavetableOsc::publishLoaded()
{
	adoptTable(std::move(pending_->result));
	pending_.reset(); // small now - `result` was moved out; freeing the husk here is fine.

	// We're inside a subProcess, where the block position isn't exact; pin streaming-state
	// changes want an explicit position (debug-asserted in AudioOutPin::setStreaming).
	// blockPos_ is the start of the current sub-block - the correct timestamp.
	TempBlockPositionSetter positionScope(this, getBlockPosition());
	chooseSubProcess();
}
