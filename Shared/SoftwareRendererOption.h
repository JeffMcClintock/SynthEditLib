#pragma once

/*
#include "Shared/SoftwareRendererOption.h"
*/

// Debug-only "draw with the software renderer" switch.
//
// The Linux build of SynthEdit renders entirely on gmpi_ui's CPU backend
// (backends/CpuGfx.h) — no GPU, no Direct2D, no CoreGraphics. That backend is
// portable, so this switch runs it on Windows and macOS too, which is what lets
// a Linux rendering bug be reproduced (and diffed against the native backend)
// without leaving the machine SynthEdit is actually developed on.
//
// It is a development tool, not a product feature, and the gate is the macro
// rather than the preference:
//
//   - SE_SOFTWARE_RENDERER_OPTION is 1 only in a debug build. In release every
//     CPU-backend code path guarded by it compiles out, so a shipping binary
//     carries neither the software renderer nor its HarfBuzz dependency, and
//     useSoftwareRenderer() is a compile-time false the optimiser folds away.
//
//   - The preference itself is still read and written in release builds. A
//     developer who ticks the box in a debug build and then runs a release build
//     keeps the setting; the release build simply ignores it, which is what the
//     hidden tickbox already implies. Dropping the value on every release run
//     would silently un-tick the box behind their back.

#ifndef SE_SOFTWARE_RENDERER_OPTION
 #ifdef _DEBUG
  #define SE_SOFTWARE_RENDERER_OPTION 1
 #else
  #define SE_SOFTWARE_RENDERER_OPTION 0
 #endif
#endif

namespace se
{
#if SE_SOFTWARE_RENDERER_OPTION

// Set at startup, and again when Preferences is applied, from
// ApplicationSettings::SoftwareRenderer.
//
// Read when a drawing frame is CONSTRUCTED, not per frame. Every brush, bitmap,
// geometry and text format a view holds belongs to whichever factory was live
// when it was made, so flipping this under a running editor would hand
// Direct2D resources to the CPU rasterizer. Changing it therefore takes effect
// on the next window opened — same rule the existing "Disable Graphics
// Acceleration" preference plays by.
inline bool softwareRendererRequested = false;

inline void setSoftwareRendererEnabled(bool enabled) { softwareRendererRequested = enabled; }
inline bool useSoftwareRenderer()                    { return softwareRendererRequested; }

#else

inline void setSoftwareRendererEnabled(bool)        {}
inline constexpr bool useSoftwareRenderer()         { return false; }

#endif
} // namespace se
