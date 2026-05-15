#ifndef WAVETABLEOSCGUI_H_INCLUDED
#define WAVETABLEOSCGUI_H_INCLUDED

#include "helpers/GmpiPluginEditor.h"
#include "Wavetable.h"

class WavetableOscGui : public gmpi::editor::PluginEditor
{
	// Backing storage for currentWavetable() - sized to hold a full WaveTable header
	// plus its trailing flexible-array float Wavedata[]. float alignment is sufficient
	// since WaveTable's leading int32_t members and the float[] payload are all 4-byte aligned.
	std::vector<float> currentWavetableMem_;
	std::string curWaveFile_;

public:
	WavetableOscGui();

	// PluginEditor overrides
	gmpi::ReturnCode render(gmpi::drawing::api::IDeviceContext* dc) override;

	WaveTable* currentWavetable()
	{
		return reinterpret_cast<WaveTable*>(currentWavetableMem_.data());
	}
	void updateCurrentWavetable();

private:
	void redraw()
	{
		if(drawingHost)
			drawingHost->invalidateRect(nullptr);
	}

	// GUI pins (matching XML GUI pin order for "SE Wavetable Display")
	gmpi::editor::Pin<std::string> pinWaveFiles;  // pin 0 - WaveTableFile
	gmpi::editor::Pin<float>       pinSlot;       // pin 1 - Slot (0..1, drives the red-highlight slot)
};

#endif
