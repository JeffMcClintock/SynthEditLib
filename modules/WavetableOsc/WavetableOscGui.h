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

	// Placement of the slots within the landscape. Shared between the cached geometry build and the
	// per-frame build of the selected slot, so both land on the same projection. Valid whenever
	// slotGeometry_ is.
	struct Layout
	{
		float vscale          = 0.0f; // waveform half-height in pixels
		float horizontalDelta = 0.0f; // total x shift from the frontmost to the backmost slot
		float x_increment     = 0.0f; // pixels per sample
		float backYaxis       = 0.0f; // baseline y of the backmost slot
		float frontYaxis      = 0.0f; // baseline y of the frontmost slot
		float endX            = 0.0f; // x of the waveform's right-hand end, slot-local
		int   slotCount       = 0;    // slots in the wavetable the layout was built for
	};
	Layout layout_;

	// Origin (baseline left-hand end) of one slot's waveform within the landscape.
	gmpi::drawing::Point slotOrigin(int slot) const;

	// Thin one slot's waveform to a polyline in slot-local coordinates. SimplifyGraph is
	// translation-invariant, so the result serves both the slot's outline and any ribbon bordering it.
	void simplifySlot(WaveTable* waveTable, int slot, std::vector<gmpi::drawing::Point>& simplified) const;

	// Build one slot's waveform outline from its simplified points.
	gmpi::drawing::PathGeometry buildSlotOutline(gmpi::drawing::Factory& factory, int slot, const std::vector<gmpi::drawing::Point>& wave) const;

	// Cached display geometry: an evenly-spaced subset of the slots, not all of them. Depends only on
	// the wavetable data and the widget size, so it is rebuilt only when one of those changes - which
	// slot is highlighted is a per-frame pen choice (see selectedOutline_ for the slot that isn't here).
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

	// The selected slot usually isn't one of the drawn subset, so its outline is built on demand and
	// kept only until the selection moves. Deliberately not folded into slotGeometry_: caching every
	// slot the user sweeps past is exactly the cost the drawn-subset limit exists to avoid.
	// Empty whenever the selected slot is already in slotGeometry_ (that one is just drawn highlighted).
	gmpi::drawing::PathGeometry selectedOutline_;
	int selectedOutlineSlot_ = -1;

	// GUI pins (matching XML GUI pin order for "SE Wavetable Display")
	gmpi::editor::Pin<std::string> pinWaveFiles;  // pin 0 - WaveTableFile
	gmpi::editor::Pin<float>       pinSlot;       // pin 1 - Slot (0..1, drives the red-highlight slot)
};

#endif
