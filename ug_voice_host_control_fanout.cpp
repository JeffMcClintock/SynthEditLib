#include "ug_voice_host_control_fanout.h"
#include <algorithm>
#include "resource.h"
#include "module_register.h"
#include "ug_container.h"
#include "ug_oversampler.h"
#include "ug_oversampler_in.h"
#include "CVoiceList.h"
#include "UPlug.h"

SE_DECLARE_INIT_STATIC_FILE(ug_voice_host_control_fanout)

using namespace std;
using namespace gmpi;

void ug_voice_host_control_fanout::ListInterface2(std::vector<class InterfaceObject*>& PList)
{
	// MIDI In/Out hints keep this module downstream of any MIDI automator but upstream of modules
	// that consume voice-control events. No MIDI data is ever actually routed through.
	LIST_VAR3N(L"MIDI In",  DR_IN,  DT_MIDI2, L"0", L"", 0, L"");
	LIST_VAR3N(L"MIDI Out", DR_OUT, DT_MIDI2, L"",  L"", 0, L"");
}

namespace
{
	REGISTER_MODULE_1(L"SE Voice Host Control Fanout", IDS_MN_PP_SETTER, IDS_MG_DEBUG, ug_voice_host_control_fanout, CF_STRUCTURE_VIEW, L"");
}

ug_voice_host_control_fanout::ug_voice_host_control_fanout()
{
	// Deliberately DO NOT set UGF_PARAMETER_SETTER — that flag is what causes
	// FlagUpStream to transfer pins to the secondary setter and apply 1-block latency.
}

int ug_voice_host_control_fanout::Open()
{
	SET_PROCESS_FUNC(&ug_base::process_sleep);
	auto r = ug_base::Open();

	BroadcastInitialValues();
	return r;
}

// At graph-start, broadcast each pin's buffer-default to every connected voice clone.
// Each voice independently receives the same default (e.g. 5 V on HC_VOICE_PITCH so every
// voice idles at Middle-A), matching the per-voice mental model shown by a multi-output
// VoiceSplitter where each output reads 5 V.
//
// CRITICAL: this is invoked SYNCHRONOUSLY from Open() — before any block processing starts.
// That way our events are enqueued on downstream voice modules' queues BEFORE any MIDI events
// (e.g. a CC64 = 127 at timestamp 0 from a MIDI file) land via the MIDI-CV redirector during
// its block-0 DoProcess. When the downstream module processes events at timestamp 0 in queue
// order, our 0 V defaults are processed first and the MIDI value correctly overwrites them.
// Using RUN_AT(SampleClock(), …) instead would race with MIDI events because the fanout's
// sort order is higher than the redirector's — see PolyOverlap_GuyR test.
void ug_voice_host_control_fanout::BroadcastInitialValues()
{
	for (auto p : plugs)
	{
		if ((p->flags & PF_VALUE_CHANGED) != 0 && p->Direction == DR_OUT)
		{
			SendPinsCurrentValue(SampleClock(), p);
			p->ClearFlag(PF_VALUE_CHANGED);
		}
	}
}

void ug_voice_host_control_fanout::SetupDynamicPlugs()
{
	SetupDynamicPlugs2();
}

FeedbackTrace* ug_voice_host_control_fanout::PropagatePolyphonicDownstream()
{
	// Mirror DspPatchManager::InitSetDownstream: for each poly output pin, walk its
	// connections and call PPSetDownstream() on each target. This makes downstream modules
	// (MidiToCv2, VoiceSplitter, etc.) detect the upstream polyphonic source and get
	// voice-cloned. Without this, VoiceSplitter stays mono and an auto-inserted poly→mono
	// adder sums every voice's pitch CV instead of routing one voice per splitter clone.
	for (auto p : plugs)
	{
		if (p->Direction != DR_OUT) continue;
		if (!p->GetFlag(PF_POLYPHONIC_SOURCE)) continue;
		for (auto to_plug : p->connections)
		{
			if (auto fb = to_plug->PPSetDownstream())
				return fb;
		}
	}
	return nullptr;
}

void ug_voice_host_control_fanout::ConnectDirectPathHostControl(ug_container* voiceContainer, HostControls hostConnect, UPlug* toPlug)
{
	assert(isDirectPathHostControl(hostConnect));
	assert((toPlug->flags & PF_HOST_CONTROL) != 0);
	assert(toPlug->Direction == DR_IN);
	assert(toPlug->DataType != DT_MIDI);

	// Reuse an existing pin for this HC if we've already created one — many-to-many fan-out
	// works via the pin's connections list.
	auto plugIt = plugs.begin() + 2; // skip the 2 dummy MIDI plugs.
	for (auto& c : connections_)
	{
		if (c.hostConnect == hostConnect)
		{
			connect_oversampler_safe(*plugIt, toPlug);
			return;
		}
		++plugIt;
	}

	// No existing pin for this HC — create one. Every pin gets PF_VALUE_CHANGED so its buffer
	// default fires at graph-start, ensuring downstream voice modules receive an initial event
	// on every HC input (so they can initialise their internal state).
	auto fromPin = new UPlug(this, DR_OUT, toPlug->DataType);
	fromPin->SetFlag((toPlug->flags & (PF_HOST_CONTROL | PF_PATCH_STORE)) | PF_VALUE_CHANGED);
	AddPlug(fromPin);
	fromPin->CreateBuffer();

	// Poly HCs generally need to make downstream modules polyphonic (same rule as the legacy
	// setter path). Exception: HC_VOICE_ACTIVE is used by modules like Scope3 to *monitor*
	// polyphony status, not to become polyphonic themselves — so don't flag it.
	if (HostControlisPolyphonic(hostConnect) && hostConnect != HC_VOICE_ACTIVE)
	{
		fromPin->SetFlag(PF_POLYPHONIC_SOURCE);
		// HC_VOICE_PITCH idle default is Middle-A (5 V) so the voice's pitch CV output sits at
		// a sensible note (matches legacy reference waves which show 5 V on idle pitch channels).
		if (HC_VOICE_PITCH == hostConnect)
		{
			fromPin->SetBufferValue("5");
		}
	}

	connections_.push_back({ hostConnect });

	connect_oversampler_safe(fromPin, toPlug);
	voiceContainer->ConnectVoiceHostControl(hostConnect, fromPin);
}

int ug_voice_host_control_fanout::calcDelayCompensation()
{
	// Early-out the standard scan: our input plugs are dummy MIDI pins with no connections, so
	// the default walk would return 0 regardless. We want to advertise our *effective* latency —
	// see header comment for the full story.
	if (cumulativeLatencySamples != LATENCY_NOT_SET)
		return cumulativeLatencySamples;

	// Events arriving on our output pins (via VoiceList::sendDirectPathValue) have already had
	// the oversampler-input latency baked into their timestamp: the event flows through
	// ug_oversampler_in::HandleEvent → +latencySamples → redirector.HandleEvent → SampleClock()
	// → container->OnMidi → sendDirectPathValue. So if we're inside an oversampler, advertise
	// that latency so downstream calcDelayCompensation() sees a consistent picture and doesn't
	// insert a LatencyAdjustEventBased2 compensator that would double-compensate.
	int effectiveLatency = 0;
	if (auto os = dynamic_cast<ug_oversampler*>(AudioMaster()); os && os->oversampler_in)
	{
		effectiveLatency = os->oversampler_in->latencySamples;
	}

	cumulativeLatencySamples = effectiveLatency;
	return cumulativeLatencySamples;
}

ug_base* ug_voice_host_control_fanout::Clone(CUGLookupList& UGLookupList)
{
	auto clone = (ug_voice_host_control_fanout*)ug_base::Clone(UGLookupList);

	// Copy the connection table, re-register each cloned pin with the voice container.
	auto plugIt = clone->plugs.begin() + 2; // skip 2 MIDI plugs.
	for (auto& c : connections_)
	{
		auto fromPin = *plugIt++;
		clone->connections_.push_back(c);
		// Register the cloned pin with the container's direct-path table.
		// parent_container for the clone is set up during the ug_base::Clone call.
		clone->parent_container->ConnectVoiceHostControl(c.hostConnect, fromPin);
	}

	return clone;
}
