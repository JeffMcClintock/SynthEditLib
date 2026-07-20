#ifndef WAVETABLEOSCGUI_H_INCLUDED
#define WAVETABLEOSCGUI_H_INCLUDED

#include "helpers/GmpiPluginEditor.h"
#include "WavetableCache.h"

class WavetableOscGui : public gmpi::editor::PluginEditor
{
	// Shared raw wavetable from the process-wide cache. The display draws the waveform and
	// never reads a mip, so it takes the raw form on its own rather than dragging in a bake
	// (tens of MB) it would never touch. Shared with the DSP, which derives its bake from the
	// same object; freed when the last instance lets go.
	std::shared_ptr<RawWavetable> currentWavetable_;
	std::string curWaveFile_;

public:
	WavetableOscGui();

	// PluginEditor overrides
	gmpi::ReturnCode render(gmpi::drawing::api::IDeviceContext* dc) override;

	WaveTable* currentWavetable()
	{
		return currentWavetable_ ? currentWavetable_->get() : nullptr;
	}
	void updateCurrentWavetable();

private:
	void redraw()
	{
		if(drawingHost)
			drawingHost->invalidateRect(nullptr);
	}

	// Rebuild the cached 3D landscape geometry from the current wavetable at the given size.
	void buildDisplayGeometry(gmpi::drawing::Graphics& g, float width, float height, WaveTable* waveTable);

	// Cached display geometry. The 3D landscape depends only on the wavetable data and the widget
	// size, so it is rebuilt only when one of those changes. The selected-slot highlight is a
	// per-frame pen choice that needs no rebuild, so it is not baked into the geometry.
	struct SlotGeometry
	{
		int                         slot;    // source slot index (drives the highlight test)
		gmpi::drawing::PathGeometry outline; // the waveform polyline
		gmpi::drawing::PathGeometry fill;    // black ribbon toward the next-nearer slot (null on the frontmost)
	};
	std::vector<SlotGeometry> slotGeometry_;
	bool  geometryDirty_  = true;
	float geometryWidth_  = 0.0f;
	float geometryHeight_ = 0.0f;

	// GUI pins (matching XML GUI pin order for "SE Wavetable Display")
	gmpi::editor::Pin<std::string> pinWaveFiles;  // pin 0 - WaveTableFile
	gmpi::editor::Pin<float>       pinSlot;       // pin 1 - Slot (0..1, drives the red-highlight slot)
};

#endif
