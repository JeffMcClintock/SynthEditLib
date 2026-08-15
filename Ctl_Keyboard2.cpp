// implementation of the Ctl_Keyboard2 class
//
#ifdef _WIN32
//#include "mmsystem.h"
#endif

#include "Application.h" // must be first to fix STL/MFC problems
#include "SynthEditDocBase.h"
#include "Ctl_Keyboard2.h"
#include "CContainer.h"
#include "ug_midi_keyboard.h"

#include "Notify_msg.h"
#include "UgDatabase.h"
#include "SeAudioMaster.h"

#include "SkinMgr.h"

#define BASE_KEY 36

Ctl_Keyboard2::Ctl_Keyboard2( Module_Info* p_type ) : CUG( p_type )
	,midi_note(-1)
	,toggle_mode(false)
	,PanelWndPosition(0,0,0,0)

{
	for(int i = 0 ; i < 128 ; i++)
		key_state[i] = false;
}

void Ctl_Keyboard2::PlayNote(int p_note_num, bool note_on)
{
	//	_RPT1(_CRT_WARN, "Ctl_Keyboard2::PlayNote      %d ms\n", timeGetTime() );
	//if( gene rator == nullptr )
	//{
	//	return;
	//}
	if( toggle_mode )
	{
		if( !note_on )
			return;

		if( key_state[p_note_num] && note_on )
		{
			midi_note = p_note_num; // so dragging don't turn on key again
			note_on = false;
		}
	}

	if( note_on )
	{
		midi_note = p_note_num;
		key_state[p_note_num] = true;
		//		_RPTW1(_CRT_WARN, L"             *****ON  %d\n", p_note_num );
		//NotifyGenerators( NUG_NOTE_ON, p_note_num );
	}
	else
	{
		if( key_state[p_note_num] == false ) // seem to get ghost key off msg on sound start?, don't know why
			return;

		//		_RPTW1(_CRT_WARN, L"             *****OFF %d\n", p_note_num );
		//NotifyGenerators( NUG_NOTE_OFF, p_note_num );
		key_state[p_note_num] = false;
		// moved midi_note = -1;
	}
}

//void Ctl_Keyboard2::OnSynthStop()
//{
//	// un highlight any held keys
//	for(int k = 0 ; k < 128 ; k++ )
//	{
//		key_state[k] = false;
//	}
//
//	CUG::OnSynthStop();
//}

void Ctl_Keyboard2::ClearHeldNotes()
{
	for(int k = 0 ; k < 128 ; k++ )
	{
		if(key_state[k])
		{
			PlayNote(k, false);
		}
	}
}


/*
> for that octave being on the keyboard "home row."  The second octave (plus
> a few notes beyond that) is on the 'qwerty' row and the black keys for
that
> are on the number row.  [q] defaults to middle C and (if you want to get
> really fancy) the whole keyboard can be adjusted up or down by an octave
> with either page-up/page-down or the +/- keys on the number pad.  So, for
> example, the notes c-c#-d-d#-e are zsxdc (some of the keys on the black
> rows are skipped to approximate the piano keyboard).  And middle
> C-C#-D-D#-E would be q2w3e.

*/
//#define MIDDLE_A  69

int Ctl_Keyboard2::key_to_note(int p_char)
{
	switch(p_char )
	{
		// bottom rows ZXC
	case 'Z':
	case 'z':
		return 48;

	case 'S':
	case 's':
		return 49;

	case 'X':
	case 'x':
		return 50;

	case 'D':
	case 'd':
		return 51;

	case 'C':
	case 'c':
		return 52;

	case 'V':
	case 'v':
		return 53;

	case 'G':
	case 'g':
		return 54;

	case 'B':
	case 'b':
		return 55;

	case 'H':
	case 'h':
		return 56;

	case 'N':
	case 'n':
		return 57;

	case 'J':
	case 'j':
		return 58;

	case 'M':
	case 'm':
		return 59;

		/*
			case ',': // don't work
			case '<':
				return 60;
			case 'l':
			case 'L':
				return 61;
			case '.':
			case '>':
				return 62;
			case ';':
			case ':':
				return 63;
			case '/':
			case '?':
				return 64;
		*/
		// top 2 rows  qwerty
	case 'Q':
	case 'q':
		return 60;

	case '2':
		return 61;

	case 'W':
	case 'w':
		return 62;

	case '3':
		return 63;

	case 'E':
	case 'e':
		return 64;

	case 'R':
	case 'r':
		return 65;

	case '5':
		return 66;

	case 'T':
	case 't':
		return 67;

	case '6':
		return 68;

	case 'Y':
	case 'y':
		return 69;

	case '7':
		return 70;

	case 'U':
	case 'u':
		return 71;

	case 'I':
	case 'i':
		return 72;

	case '9':
		return 73;

	case 'O':
	case 'o':
		return 74;

	case '0':
		return 75;

	case 'P':
	case 'p':
		return 76;

	default:
		return -1;
	};
}

gmpi::drawing::RectL Ctl_Keyboard2::getViewObRect(int p_view_type)
{
	if( p_view_type == CF_PANEL_VIEW )
		return PanelWndPosition;
	else
		return CUG::getViewObRect(p_view_type);
}

void Ctl_Keyboard2::setViewObRect(int p_view_type, gmpi::drawing::RectL& p_rect)
{
	if( p_view_type == CF_PANEL_VIEW )
	{
		if( PanelWndPosition != p_rect )
		{
			PanelWndPosition = p_rect;
		}
	}
	else
		CUG::setViewObRect( p_view_type, p_rect );
}

bool Ctl_Keyboard2::show_title_on_panel()
{
	return true;
}
