#include <cstdio>
#include "./ClassicControlGuiBase.h"
#include "../sharedLegacyWidgets/TextWidget.h"

void ClassicControlGuiBase::onSetTitle()
{
	if (!widgets.empty())
	{
		auto header = dynamic_cast<TextWidget*>(widgets.back().get());

		header->SetText(pinTitle);

		if (header->ClearDirty())
			invalidateMeasure();
	}
}

int32_t ClassicControlGuiBase::initialize()
{
	auto r = gmpi_gui::MpGuiGfxBase::initialize(); // ensure all pins initialised (so widgets are built).

	// widgets.back() on an empty vector is UB (crashed TIDE at address -16 =
	// empty back() with 16-byte elements; TideSynth BACKLOG U2d). Widgets are
	// built by pin-init callbacks above; a host where those don't fire must
	// not bring the whole process down. Loud, not silent, per U2d's rule.
	if (widgets.empty())
	{
		fprintf(stderr, "SynthEdit: ClassicControlGuiBase::initialize: no widgets built - control will not draw (check font/skin resources)\n");
		return r;
	}
	if (auto* titleWidget = dynamic_cast<TextWidget*>(widgets.back().get()); titleWidget)
		titleWidget->SetText(pinTitle);

	return r;
}

bool ClassicControlGuiBase::useBackwardCompatibleArrangement()
{
	// In SE 1.1 List-Entry had centered headings.
	// In 1.3 everything got left-justified by mistake.Rectify this, but only if (1.4) backward-compatibility switched off.
	if(backwardCompatibleVerticalArrange == -1)
	{
		FontMetadata* fontData{};
		FontCache::instance()->GetTextFormat(getHost(), getGuiHost(), "control_label", &fontData);

		backwardCompatibleVerticalArrange = (int) fontData->verticalSnapBackwardCompatibilityMode;
	}

	return backwardCompatibleVerticalArrange == 1;
}
