#ifndef WAVETABLEOSCGUI_H_INCLUDED
#define WAVETABLEOSCGUI_H_INCLUDED

#include "helpers/GmpiPluginEditor.h"
#include "WavetableCache.h"

class WavetableOscGui : public gmpi::editor::PluginEditor
{
	// Shared baked wavetable from the process-wide cache (the DSP holds an identical
	// shared_ptr; the bake is freed when the last instance lets go).
	std::shared_ptr<CachedWavetable> currentWavetable_;
	std::string curWaveFile_;

public:
	WavetableOscGui();

	// PluginEditor overrides
	gmpi::ReturnCode render(gmpi::drawing::api::IDeviceContext* dc) override;

	WaveTable* currentWavetable()
	{
		return currentWavetable_ ? currentWavetable_->raw() : nullptr;
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
