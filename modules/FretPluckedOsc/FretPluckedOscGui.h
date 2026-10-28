#pragma once

#include <optional>
#include "helpers/GmpiPluginEditor.h"
#include "FretPluckedBake.h"

// Panel view of the Fret Plucked Oscillator: a waterfall of the A4 note, time left to right, frequency back to front,
// loudness bottom to top, one envelope per harmonic. The DSP sends its Params; the partials are recomputed here.
class FretPluckedOscGui : public gmpi::editor::PluginEditor
{
public:
	FretPluckedOscGui();

	gmpi::ReturnCode render(gmpi::drawing::api::IDeviceContext* dc) override;

private:
	void redraw()
	{
		if (drawingHost)
			drawingHost->invalidateRect(nullptr);
	}

	std::optional<fret_plucked::Params> params_;
	gmpi::drawing::TextFormat labelFormat_;

	gmpi::editor::Pin<gmpi::Blob> pinEnvelopeData; // pin 0
};
