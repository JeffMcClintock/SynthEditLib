/////////////////////////////////////////////////////////////////////////////
// Class Ctl_Keyboard2
#pragma once
#include "CUG.h"

class Ctl_Keyboard2 : public CUG
{
public:
	Ctl_Keyboard2( Module_Info* p_type = 0 );
	DECLARE_BUILD_FUNC(Ctl_Keyboard2);
	int key_to_note(int p_char);
	void ClearHeldNotes();
	void PlayNote(int p_note_num, bool note_on);
	gmpi::drawing::RectL getViewObRect(int p_view_type) override;
	void setViewObRect(int p_view_type, gmpi::drawing::RectL& p_rect) override;
	bool show_title_on_panel() override;

private:
	gmpi::drawing::RectL PanelWndPosition;
	bool toggle_mode;
	int midi_note;
	bool key_state[128];
};
