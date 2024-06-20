#ifndef PLAINIMAGEGUI_H_INCLUDED
#define PLAINIMAGEGUI_H_INCLUDED

#include "mp_sdk_gui2.h"
#include "../shared/ImageCache.h"
#include "../se_sdk3/Drawing.h"

class PlainImageGui : public gmpi_gui::MpGuiGfxBase, ImageCacheClient
{
public:
	PlainImageGui();

	// overrides.
	int32_t OnRender(GmpiDrawing_API::IMpDeviceContext* drawingContext) override;
	int32_t measure(GmpiDrawing_API::MP1_SIZE availableSize, GmpiDrawing_API::MP1_SIZE* returnDesiredSize) override;
	int32_t arrange(GmpiDrawing_API::MP1_RECT finalRect) override;

private:
	void onSetFilename();
	void onSetMode();

	StringGuiPin pinFilename;
 	IntGuiPin pinStretchMode;

	GmpiDrawing::Bitmap bitmap_;
	ImageMetadata* bitmapMetadata_;

	enum class StretchMode { Fixed, Tiled, Stretch };
};

#endif


