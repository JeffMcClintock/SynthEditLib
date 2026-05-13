#ifdef _WIN32
// not here. #define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "windows.h"

#endif

#include "./gmpi_gui_hosting.h"

#ifdef _WIN32

using namespace GmpiGuiHosting;

void UpdateRegionWinGdi::copyDirtyRects(HWND window, GmpiDrawing::SizeL swapChainSize)
{
	rects.clear();

	/*
	#define ERROR               0
	#define NULLREGION          1
	#define SIMPLEREGION        2
	#define COMPLEXREGION       3
	#define RGN_ERROR ERROR
	*/

	auto regionType = GetUpdateRgn(
		window,
		hRegion,
		FALSE
	);

	assert(regionType != RGN_ERROR);

	if (regionType != NULLREGION)
	{
		int size = GetRegionData(hRegion, 0, NULL); // query size of region data.
		if (size)
		{
			regionDataBuffer.resize(size);
			RGNDATA* pRegion = (RGNDATA *)regionDataBuffer.data();

			GetRegionData(hRegion, size, pRegion);

			const RECT* pRect = (const RECT*)& pRegion->Buffer;

			for (unsigned i = 0; i < pRegion->rdh.nCount; i++)
			{
				GmpiDrawing::RectL r(pRect[i].left, pRect[i].top, pRect[i].right, pRect[i].bottom);

//				_RPTW4(_CRT_WARN, L"rect %d, %d, %d, %d\n", r.left, r.top, r.right,r.bottom);

				// Direct 2D will fail if any rect outside swapchain bitmap area.
				r.Intersect(GmpiDrawing::RectL(0, 0, swapChainSize.width, swapChainSize.height));

				if (!r.empty())
				{
					rects.push_back(r);
				}
			}
		}
		optimizeRects();
	}

//	_RPTW1(_CRT_WARN, L"OnPaint() regionType = %d\n", regionType);
}

void UpdateRegionWinGdi::optimizeRects()
{
#ifdef _DEBUG
    for (int i1 = 0; i1 < rects.size(); ++i1)
    {
        auto area1 = rects[i1].getWidth() * rects[i1].getHeight();

        for (int i2 = i1 + 1; i2 < rects.size(); )
        {
            auto area2 = rects[i2].getWidth() * rects[i2].getHeight();

            GmpiDrawing::RectL unionrect(rects[i1]);

            unionrect.top = (std::min)(unionrect.top, rects[i2].top);
            unionrect.bottom = (std::max)(unionrect.bottom, rects[i2].bottom);
            unionrect.left = (std::min)(unionrect.left, rects[i2].left);
            unionrect.right = (std::max)(unionrect.right, rects[i2].right);

            auto unionarea = unionrect.getWidth() * unionrect.getHeight();

            if (unionarea <= area1 + area2)
            {
				assert(false); // Windows already does optimization.
                rects[i1] = unionrect;
                area1 = unionarea;
                rects.erase(rects.begin() + i2);
            }
            else
            {
                ++i2;
            }
        }
    }
#endif
}

UpdateRegionWinGdi::UpdateRegionWinGdi()
{
	hRegion = ::CreateRectRgn(0, 0, 0, 0);
}

UpdateRegionWinGdi::~UpdateRegionWinGdi()
{
	if (hRegion)
		DeleteObject(hRegion);
}

#endif // _WIN32
