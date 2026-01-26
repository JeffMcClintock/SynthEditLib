#pragma once

#include <cstdint>
#include <list>
#include <utility>
#include <string>

#include "UPlug.h"

struct feedbackPin
{
    feedbackPin(UPlug* pin);
    int32_t moduleHandle;
    int32_t pinIndex;
    std::wstring debugModuleName;
};

struct FeedbackTrace
{
    std::list<std::pair<feedbackPin, feedbackPin>> feedbackConnectors;
    int reason_;

    FeedbackTrace(int reason) : reason_(reason) {}
    void AddLine(UPlug* from, UPlug* to);

    void DebugDump();
};
