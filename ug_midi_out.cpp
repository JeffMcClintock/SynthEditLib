
#include "ug_midi_out.h"
#include "resource.h"
#include "module_register.h"
#include "ug_oversampler.h"

SE_DECLARE_INIT_STATIC_FILE(ug_midi_out);

namespace
{
REGISTER_MODULE_1(L"MIDI Out", IDS_MN_MIDI_OUT,IDS_MG_MIDI,ug_midi_out ,CF_STRUCTURE_VIEW,L"Provides live MIDI output, usually via your soundcard external midi interface, but can be configured to send MIDI to your soundcard's synth.  Use the 'Audio' - 'Preferences' menu to change the destination.");
}

// Fill an array of InterfaceObjects with plugs and parameters
void ug_midi_out::ListInterface2(std::vector<class InterfaceObject*>& PList)
{
	// IO Var, Direction, Datatype, NoteSource, ppactive, Default, defid (index into unit_gen::PlugFormats)
	LIST_VAR3N( L"MIDI In",  DR_IN, DT_MIDI2 , L"0", L"", 0, L"");
}

ug_midi_out::ug_midi_out()
{
	SET_PROCESS_FUNC( &ug_midi_out::sub_process );
}

void ug_midi_out::HandleEvent(SynthEditEvent* e)
{
	switch( e->eventType )
	{
	case UET_EVENT_MIDI:
		// lambda, set by IO Manager
		midiOutCallback(e->timeStamp, e->parm2, (const unsigned char*)e->Data());
		break;

	default:
		ug_base::HandleEvent( e );
	};
}

int ug_midi_out::Open()
{
	ug_base::Open();

	if(dynamic_cast<ug_oversampler*>(AudioMaster()))
	{
		message(L"This module can't be oversampled. Please move it out of oversampler Container.");
		return 0;
	}

	AudioMaster()->RegisterIoModule(this);

	return 0;
}
