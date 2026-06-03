// SPDX-License-Identifier: ISC
// Copyright 2007-2026 Jeff McClintock.
//
// One-pole (6 dB/octave) high-pass filter, TPT / zero-delay-feedback form.
// The DSP lives in the shared OnePoleTpt.h core; this file just selects the
// high-pass output and registers the plugin.

#include "OnePoleTpt.h"

using namespace gmpi;

struct HighPassTpt final : OnePoleTptBase</*isHighPass*/ true> {};

namespace
{
auto r = Register<HighPassTpt>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<Plugin id="SE High Pass (TPT)" name="High Pass (TPT)" category="SDK Examples">
    <Audio>
        <Pin name="Signal" datatype="float" rate="audio" linearInput="true"/>
        <Pin name="Pitch" datatype="float" rate="audio" default="0.5"/>
        <Pin name="Output" datatype="float" rate="audio" direction="out"/>
    </Audio>
</Plugin>
)XML");
}
