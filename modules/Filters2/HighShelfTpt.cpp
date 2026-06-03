// SPDX-License-Identifier: ISC
// Copyright 2007-2026 Jeff McClintock.
//
// High-shelf equaliser, TPT / zero-delay-feedback form (Cytomic SVF mixing).
// DSP core in SvfEq.h; this file supplies the high-shelf coefficients.

#include "SvfEq.h"

using namespace gmpi;

struct HighShelfTpt final : SvfEqBase
{
	void updateEqCoeffs(float gainDb, float Q) override
	{
		const float A = dBtoA(gainDb);
		gScale = std::sqrt(A);
		k = 1.0f / Q;
		m0 = A * A;
		m1 = k * (1.0f - A) * A;
		m2 = 1.0f - A * A;
	}
};

namespace
{
auto r = Register<HighShelfTpt>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<Plugin id="SE High Shelf (TPT)" name="High Shelf (TPT)" category="SDK Examples">
    <Audio>
        <Pin name="Signal" datatype="float" rate="audio" linearInput="true"/>
        <Pin name="Pitch" datatype="float" rate="audio" default="0.5"/>
        <Pin name="Gain dB" datatype="float" default="0"/>
        <Pin name="Q" datatype="float" default="0.707"/>
        <Pin name="Output" datatype="float" rate="audio" direction="out"/>
    </Audio>
</Plugin>
)XML");
}
