#pragma once
#include <cstdint>
#include <map>
#include "ProcessorStateManager.h" // paramInfo

/*
#include "SeParameterInfo.h"
*/

// The plugin's parameters.se.xml as one immutable description per process.
//
// Stage 1 of TideSynth/docs/state-management-consolidation-plan.md. Until now
// the controller (MpController::Initialize) and the processor
// (ProcessorStateMgr::init) each parsed this resource into a map of their own,
// with the same ::init(), so two readers of one preset were driven by two
// separately built tables that could drift. This is the one table.
//
// Built on first use from BundleInfo's "parameters.se.xml" with that same
// ::init(). Metadata only, never written after construction, holds no pointer
// to any controller, processor, queue or GUI, so it is safe to share between
// the two halves when they are in one process and harmless to build twice when
// they are not (AUv3). A plugin without the resource (TIDE Rack builds its
// patch at runtime) gets an empty map. Callers hold a const reference, never a
// copy. Defined in Controller.cpp.
const std::map<int32_t, paramInfo>& seParameterInfo();
