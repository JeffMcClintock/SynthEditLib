#pragma once

/*
#include "Shared/GpuRendererOption.h"
*/

// Debug-only "draw with the GPU backend" switch: gmpi_ui's GPU backend
// (backends/GpuGfx.h, bgfx over Direct3D 12) standing in for Direct2D, the way
// SoftwareRendererOption.h swaps in the CPU backend. The window still presents
// through Direct2D; only the drawing moves to the GPU backend.
//
// Not shipped. Unlike the software renderer, the macro is off unless the build
// defines it, because it needs gmpi_gpu (bgfx) linked: SE16's CMakeLists does
// both for Debug when configured with -DSE_GPU_RENDERER=ON.

#include <cstdlib>

#ifndef SE_GPU_RENDERER_OPTION
#define SE_GPU_RENDERER_OPTION 0
#endif

namespace se
{
#if SE_GPU_RENDERER_OPTION

// Read when a drawing frame is constructed, like the software renderer's switch.
inline bool gpuRendererRequested = false;

inline void setGpuRendererEnabled(bool enabled) { gpuRendererRequested = enabled; }

// GMPI_UI_GPU in the environment overrides the preference (1 = GPU, 0 = not), as it does in gmpi_ui's own frames.
inline bool useGpuRenderer()
{
    if (const char* v = std::getenv("GMPI_UI_GPU"); v && *v)
        return *v != '0';
    return gpuRendererRequested;
}

#else

inline void setGpuRendererEnabled(bool) {}
inline constexpr bool useGpuRenderer() { return false; }

#endif
} // namespace se
