#ifndef KEYBOARD2GUI_H_INCLUDED
#define KEYBOARD2GUI_H_INCLUDED

#include "Drawing.h"
#include "mp_sdk_gui2.h"
#include <vector>

class KeyboardBase : public gmpi_gui::MpGuiGfxBase
{
	static const int MAX_KEYS = 128;

protected:
	std::vector<bool> keyStates;

	int baseKey_ = 36; // MIDI key num of leftmost key.

public:
	KeyboardBase()
	{
		keyStates.assign(MAX_KEYS, false);
	}

	// overrides
	int32_t OnRender(GmpiDrawing_API::IMpDeviceContext* drawingContext) override;
	int32_t onPointerDown(int32_t flags, GmpiDrawing_API::MP1_POINT point) override;
	int32_t onPointerMove(int32_t flags, GmpiDrawing_API::MP1_POINT point) override;
	int32_t onPointerUp(int32_t flags, GmpiDrawing_API::MP1_POINT point) override;

	virtual void PlayNote(int p_note_num, bool note_on) = 0;

	void DoPlayNote(int p_note_num, bool note_on)
	{
		// Don't re-play note that was already toggled on
		if (keyStates[p_note_num] == note_on)
			return;

		assert(p_note_num >= 0 && p_note_num < 128);

		keyStates[p_note_num] = note_on;
		PlayNote(p_note_num, note_on);
		invalidateRect();
	}
};

class Keyboard2Gui : public KeyboardBase
{
public:
	Keyboard2Gui();

	FloatArrayGuiPin pinGates;
	FloatArrayGuiPin pinTriggers;
//	FloatArrayGuiPin pinPitches;
	FloatArrayGuiPin pinVelocities;

private:
	void onValueChanged(int index);

	void PlayNote(int p_note_num, bool note_on);
	bool getKeyState(int voice);
};

#endif


