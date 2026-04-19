#pragma once
#include "ug_base.h"
#include "HostControls.h"

// Neutral pin-holder for direct-path (performance) host-controls.
// Sits in a voice container upstream of MIDI-CV and voice modules.
// Unlike ug_patch_param_setter it is NOT flagged UGF_PARAMETER_SETTER, so
// FlagUpStream's cycle detection walks right through without transferring
// any pins and without applying PF_PARAMETER_1_BLOCK_LATENCY.
//
// Pins are driven by ug_container::OnMidi synchronously (via
// VoiceList::sendDirectPathValue) during the MIDI-CV redirector's turn,
// which runs before MIDI-CV and voice modules — so events reach all
// downstream consumers in the same block.
//
// Two subtle things (see memory/reference_event_system_and_polyphony.md for the
// long version):
//  1. Polyphony propagation. This module's output pins carry poly HCs (pitch,
//     gate, velocity, per-note expression, …) but it's NOT a patch parameter,
//     so DspPatchManager::InitSetDownstream() doesn't walk its connections.
//     We mirror that walk in PropagatePolyphonicDownstream(), which the
//     container calls during PolyphonicSetup. Without it, VoiceSplitter and
//     downstream poly modules stay mono, SE inserts a poly→mono adder, and
//     voice contributions get summed into clipping.
//  2. Startup event race. Every pin needs an initial-value event so the
//     receiving module sees its first pinX.isUpdated() fire (legacy MidiToCv,
//     MidiToCv2 and others rely on this for first-time state setup).
//     Firing via RUN_AT(SampleClock(), ...) races with MIDI events at t=0 —
//     fanout sort order is higher than the MIDI-CV redirector's, so the
//     redirector enqueues CC events first and our defaults overwrite them.
//     We dodge that by calling BroadcastInitialValues() SYNCHRONOUSLY from
//     Open() instead — see comment there.
class ug_voice_host_control_fanout : public ug_base
{
public:
	DECLARE_UG_INFO_FUNC2;
	DECLARE_UG_BUILD_FUNC(ug_voice_host_control_fanout);
	ug_voice_host_control_fanout();
	int Open() override;
	void SetupDynamicPlugs() override;
	virtual ug_base* Clone(CUGLookupList& UGLookupList) override;

	// Create an output pin for the given direct-path HC, connect it to toPlug,
	// and register the pin in the voice container's direct-path fan-out table.
	// Multiple destinations for the same HC reuse the same pin (many-to-many
	// via pin.connections).
	void ConnectDirectPathHostControl(class ug_container* voiceContainer, HostControls hostConnect, class UPlug* toPlug);

	// Graph-start handler that fires each pin's buffer-default to every connected voice
	// clone (each voice idles at the same Middle-A pitch on HC_VOICE_PITCH, etc.).
	void BroadcastInitialValues();

	// Walk our poly output pins and call PPSetDownstream() on each connected target — same
	// propagation that DspPatchManager::InitSetDownstream() does for its poly parameters.
	// Without this, downstream modules (e.g. VoiceSplitter) don't get voice-cloned, and an
	// auto-inserted poly→mono adder sums all voices' pitch CV instead of routing one voice
	// per splitter clone.
	struct FeedbackTrace* PropagatePolyphonicDownstream();

	// Report our cumulative latency as the oversampler-input latency, when we're inside one.
	//
	// Why: our pins have no upstream connections — events are fired programmatically by
	// VoiceList::sendDirectPathValue() from ug_container::OnMidi, which itself runs at the
	// redirector's SampleClock(). When the MIDI event crossed into the oversampled container,
	// ug_oversampler_in::HandleEvent added +latencySamples to its timestamp. So by the time we
	// fire, our events are already at (outer_time * osF + osOffset + input_filter_latency).
	//
	// The default ug_base::calcDelayCompensation() walks input-pin connections to discover
	// upstream latency. Ours are dummy MIDI plugs, so it returns 0 — which then creates a
	// mismatch against other MidiToCv2 inputs (e.g. the pinMIDIIn path, which DOES flow through
	// oversampler_in) that report non-zero latency. The latency-compensation machinery then
	// inserts a LatencyAdjustEventBased2 on the fanout→MidiToCv2 connection that adds +latencySamples
	// AGAIN — double-compensating. Result: gate arrives 1× oversampler-input-latency late
	// (OS_Synth_no_PA shows this as ~13 outer samples late on a 2× oversampler with a
	// 16-tap sinc interpolator → 26 inner / 13 outer latency).
	//
	// Reporting the correct latency here makes the mismatch disappear and no compensator gets
	// inserted on our outputs.
	int calcDelayCompensation() override;

private:
	struct connectionInfo
	{
		HostControls hostConnect;
	};
	std::vector<connectionInfo> connections_;
};
