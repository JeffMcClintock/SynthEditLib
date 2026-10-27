#include "FretPluckedOsc.h"
#include <algorithm>
#include <cmath>

SE_DECLARE_INIT_STATIC_FILE(FretPluckedOsc);

using namespace gmpi;
using namespace fret_plucked;

namespace {
bool registered = Register<FretPluckedOsc>::withXml(R"XML(
<?xml version="1.0" encoding="utf-8" ?>
<PluginList>
  <Plugin id="SE Fret Plucked Osc" name="Fret Plucked Oscillator" category="Debug">
	<Audio>
	  <Pin name="Pitch" datatype="float" rate="audio" default="0.5"/>
	  <Pin name="Gate" datatype="bool"/>
	  <Pin name="Velocity" datatype="float" rate="audio" default="1"/>
	  <Pin name="Harmonics" datatype="int" default="24"/>
	  <Pin name="Series" datatype="enum" metadata="All,Odd,Even"/>
	  <Pin name="Harmonic Spacing" datatype="float" default="1"/>
	  <Pin name="Spectrum Tilt" datatype="float" default="1"/>
	  <Pin name="Decay Time" datatype="float" default="4"/>
	  <Pin name="Decay Tilt" datatype="float" default="1"/>
	  <Pin name="Detune" datatype="float" default="0"/>
	  <Pin name="Detune Pattern" datatype="enum" metadata="Alternating,Random"/>
	  <Pin name="Release Time" datatype="float" default="0.2"/>
	  <Pin name="Lowest Note" datatype="int" default="24"/>
	  <Pin name="Highest Note" datatype="int" default="96"/>
	  <Pin name="VoiceActive" hostConnect="Voice/Active" datatype="float" isPolyphonic="true" default="1"/>
	  <Pin name="RenderMode" datatype="int" hostConnect="Processor/OfflineRenderMode"/>
	  <Pin name="Signal Out" direction="out" datatype="float" rate="audio"/>
	  <Pin name="Unison" datatype="int" default="0"/>
	  <Pin name="Pick Noise" datatype="float" default="0"/>
	  <Pin name="Pick Tone" datatype="float" default="0.5"/>
	  <Pin name="Body" datatype="float" default="0"/>
	</Audio>
  </Plugin>
</PluginList>
)XML");

constexpr int kRenderModeOffline = 2;
constexpr double ln1000 = 6.907755278982137;
} // namespace

ReturnCode FretPluckedOsc::open(api::IUnknown* phost)
{
	auto r = Processor::open(phost);
	sampleRate_ = host->getSampleRate();

	// First acquisition spawns the worker thread, so do it here rather than on the audio thread.
	loader_ = bakeLoader();
	return r;
}

float FretPluckedOsc::t60Multiplier(float seconds) const
{
	return static_cast<float>(std::exp(-ln1000 / ((std::max)(1e-3f, seconds) * sampleRate_)));
}

Params FretPluckedOsc::currentParams() const
{
	Params p;
	p.harmonics = std::clamp(pinHarmonics.getValue(), 1, 256);
	p.series = static_cast<Series>(std::clamp(pinSeries.getValue(), 0, 2));
	p.spacing = std::clamp(pinSpacing.getValue(), 0.0f, 16.0f);
	p.spectrumTilt = pinSpectrumTilt;
	p.decayTime = (std::max)(0.01f, pinDecayTime.getValue());
	p.decayTilt = pinDecayTilt;
	p.detune = std::clamp(pinDetune.getValue(), -1200.0f, 1200.0f);
	p.detunePattern = static_cast<DetunePattern>(std::clamp(pinDetunePattern.getValue(), 0, 1));
	p.unison = std::clamp(pinUnison.getValue(), 0, maxUnison);
	p.pickNoise = std::clamp(pinPickNoise.getValue(), 0.0f, 4.0f);
	p.pickTone = std::clamp(pinPickTone.getValue(), 0.0f, 1.0f);
	p.body = std::clamp(pinBody.getValue(), 0.0f, 4.0f);
	p.lowNote = pinLowestNote;
	p.highNote = pinHighestNote;
	return p;
}

void FretPluckedOsc::onSetPins()
{
	if (pinVoiceActive.isUpdated())
	{
		const bool active = pinVoiceActive > 0.0f;
		if (active && !voiceActive_ && state_ != State::Idle)
			stopVoice(); // a fresh voice starts clean, so its next gate is accepted.
		voiceActive_ = active;
	}

	if (pinReleaseTime.isUpdated())
		releaseMul_ = t60Multiplier(pinReleaseTime);

	if (pinHarmonics.isUpdated() || pinSeries.isUpdated() || pinSpacing.isUpdated() || pinSpectrumTilt.isUpdated()
		|| pinDecayTime.isUpdated() || pinDecayTilt.isUpdated() || pinDetune.isUpdated() || pinDetunePattern.isUpdated()
		|| pinUnison.isUpdated() || pinPickNoise.isUpdated() || pinPickTone.isUpdated() || pinBody.isUpdated()
		|| pinLowestNote.isUpdated() || pinHighestNote.isUpdated())
	{
		requestTable(currentParams());
	}

	if (pinGate.isUpdated())
	{
		const bool gate = pinGate;
		if (gate && !gate_)
		{
			noteOn();
		}
		else if (!gate && gate_)
		{
			// Released notes ignore the gate until they have died away.
			waitingToStart_ = false;
			if (state_ == State::Playing)
				state_ = State::Released;
		}
		gate_ = gate;
	}

	chooseSubProcess();
}

void FretPluckedOsc::requestTable(const Params& params)
{
	if (pending_)
	{
		if (pending_->params == params)
			return;
		pending_->cancelled.store(true, std::memory_order_release);
		loader_->dispose(std::move(pending_));
	}

	if (table_ && table_->params == params)
		return;

	if (pinRenderMode == kRenderModeOffline)
	{
		// No real-time deadline, so bake here; the render is never left waiting on the worker.
		adoptTable(tableCache().getOrBake(params, sampleRate_));
		return;
	}

	pending_ = std::make_shared<BakeRequest>();
	pending_->params = params;
	pending_->sampleRate = sampleRate_;
	loader_->enqueue(pending_);
}

void FretPluckedOsc::adoptTable(std::shared_ptr<const Table> table)
{
	// This may be the last reference to a multi-MB table, so free it on the worker.
	if (table_ && table_ != voiceTable_)
		loader_->dispose(std::move(table_));
	table_ = std::move(table);
}

void FretPluckedOsc::adoptPendingIfReady()
{
	if (pending_ && pending_->ready.load(std::memory_order_acquire))
	{
		adoptTable(std::move(pending_->result));
		pending_.reset();
	}
}

void FretPluckedOsc::noteOn()
{
	if (state_ != State::Idle)
		return;

	adoptPendingIfReady();
	if (table_)
		startNote();
	else
		waitingToStart_ = true;
}

void FretPluckedOsc::startNote()
{
	const float pitch = *getBuffer(pinPitch);
	const double midiNote = 69.0 + 12.0 * (10.0 * pitch - 5.0);

	voiceTable_ = table_;
	note_ = &voiceTable_->note(static_cast<int>(std::lround(midiNote)));
	data_ = voiceTable_->data(*note_);
	tailMul_ = t60Multiplier(voiceTable_->tailT60);
	level_ = std::clamp(*getBuffer(pinVelocity), 0.0f, 1.0f);
	pos_ = 0.0;
	gain_ = 1.0f;
	quietCount_ = 0;
	inLoop_ = false;
	state_ = State::Playing;
	waitingToStart_ = false;
}

void FretPluckedOsc::stopVoice()
{
	state_ = State::Idle;
	if (voiceTable_ != table_)
		loader_->dispose(std::move(voiceTable_));
	voiceTable_.reset();
	note_ = {};
	data_ = {};
}

void FretPluckedOsc::endNote(int blockOffset)
{
	stopVoice();

	// Inside a subProcess the block position isn't exact; streaming changes want one.
	TempBlockPositionSetter positionScope(this, getBlockPosition() + blockOffset);
	chooseSubProcess();
}

using FretProcessPtr = void (FretPluckedOsc::*)(int sampleFrames);

void FretPluckedOsc::chooseSubProcess()
{
	if (state_ != State::Idle)
	{
		pinSignalOut.setStreaming(true);
		setSubProcess(pinPitch.isStreaming()
			? static_cast<FretProcessPtr>(&FretPluckedOsc::subProcess<true>)
			: static_cast<FretProcessPtr>(&FretPluckedOsc::subProcess<false>));
	}
	else if (waitingToStart_)
	{
		// Stay awake to poll for the first table.
		pinSignalOut.setStreaming(true);
		setSubProcess(&FretPluckedOsc::subProcessWaiting);
	}
	else
	{
		// Silent, so downstream - and the voice - can sleep.
		pinSignalOut.setStreaming(false);
		setSubProcess(&FretPluckedOsc::subProcessSilence);
	}
}

void FretPluckedOsc::subProcessSilence(int sampleFrames)
{
	float* out = getBuffer(pinSignalOut);
	for (int s = 0; s < sampleFrames; ++s)
		out[s] = 0.0f;
}

void FretPluckedOsc::subProcessWaiting(int sampleFrames)
{
	adoptPendingIfReady();
	if (table_)
	{
		startNote();
		TempBlockPositionSetter positionScope(this, getBlockPosition());
		chooseSubProcess();
		(this->*getSubProcess())(sampleFrames);
		return;
	}

	subProcessSilence(sampleFrames);
	nudgeSleepCounter();
}
