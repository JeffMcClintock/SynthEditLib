// SPDX-License-Identifier: ISC
// Copyright 2007-2026 Jeff McClintock.
//
// Low-shelf equaliser, TPT / zero-delay-feedback form (Cytomic SVF mixing).
// DSP core in SvfEq.h; this file supplies the low-shelf coefficients.

#include "SvfEq.h"

using namespace gmpi;

struct LowShelfTpt final : SvfEqBase
{
	void updateEqCoeffs(float gainDb, float Q) override
	{
		const float A = dBtoA(gainDb);
		gScale = 1.0f / std::sqrt(A);
		k = 1.0f / Q;
		m0 = 1.0f;
		m1 = k * (A - 1.0f);
		m2 = A * A - 1.0f;
	}
};

namespace
{
auto r = Register<LowShelfTpt>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<Plugin id="SE Low Shelf (TPT)" name="Low Shelf (TPT)" category="SDK Examples">
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
