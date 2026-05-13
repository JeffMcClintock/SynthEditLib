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

#ifdef _WIN32

namespace GmpiGuiHosting
{
	// Win32 dirty-region accounting used by the orphan DrawingFrame_win32 path
	// (referenced by SynthEdit IDE / GMPI_Wrappers, but not by SynthEditLib's
	// CMake build). Mac builds get UpdateRegionMac directly inside
	// SynthEditCocoaView.mm.
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
	};
} // namespace.

#endif
