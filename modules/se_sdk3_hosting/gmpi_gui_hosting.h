#pragma once

#error don't use obsolete.
/*
#include "modules/se_sdk3_hosting/gmpi_gui_hosting.h"
using namespace GmpiGuiHosting;
*/

#include <vector>
#include <string>

#include "../se_sdk3/mp_sdk_gui2.h"
#include "../se_sdk3/mp_gui.h"
#include "../shared/unicode_conversion.h"
#include "helpers/NativeUi.h"

namespace GmpiGuiHosting
{
#ifdef _WIN32
	// This code is for Win32 desktop apps

	class UpdateRegionWinGdi
	{
		std::vector<GmpiDrawing::RectL> rects;
		HRGN hRegion = 0;
		std::string regionDataBuffer;
		GmpiDrawing::RectL bounds;

	public:
		UpdateRegionWinGdi();
		~UpdateRegionWinGdi();

		void copyDirtyRects(HWND window, GmpiDrawing::SizeL swapChainSize);
		void optimizeRects();

		inline std::vector<GmpiDrawing::RectL>& getUpdateRects()
		{
			return rects;
		}
		//inline GmpiDrawing::RectL& getBoundingRect()
		//{
		//	return bounds;
		//}
	};
#else

class UpdateRegionMac
{
public:
	std::vector<GmpiDrawing::Rect> rects;

	// Merge-on-add. Mirrors DirtyRectQueue::add() on Windows so the queue
	// never accumulates overlapping or subset rects.
	void add(GmpiDrawing::Rect rect);
};

#endif

} // namespace.
