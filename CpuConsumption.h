#pragma once

#include <functional>
#include <tuple>

static const int CPU_BATCH_SIZE = 16;

// The editor's CPU load (median, peak), installed by CSynthEditAppBase. Empty in plugins.
inline std::function<std::tuple<float, float>()> editorCpuLoad;

