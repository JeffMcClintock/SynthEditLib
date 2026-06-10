// SPDX-License-Identifier: ISC
// Copyright 2007-2026 Jeff McClintock.
//
// RBJ filters: the classic 2nd-order biquads from Robert Bristow-Johnson's
// 'Audio EQ Cookbook' - low/high-pass and all-pass with Q, band-pass/reject
// and bell with bandwidth in octaves, low/high-shelf with slope, and a
// pole-radius notch for hum removal.
//
// Built on the iir1 library (see iir1/README-SynthEdit.md). Fixed 2nd-order
// designs (no Poles pin). Parameter changes crossfade between two filter
// instances - see IirFilterBase.h.

#include "IirFilterBase.h"

using namespace gmpi;
using namespace iir_filters;

namespace
{
	float clampQ(float q)
	{
		return std::clamp(q, 0.01f, 100.0f);
	}

	float clampOctaves(float bandWidth)
	{
		return std::clamp(bandWidth, 0.01f, 16.0f);
	}

	// Slope above 1 can put a negative value under the cookbook's square root
	// once the shelf gain exceeds ~11 dB, so cap it at 1 ('as steep as it can').
	float clampSlope(float slope)
	{
		return std::clamp(slope, 0.01f, 1.0f);
	}
}

// Low-pass / High-pass / All-pass: Signal, Pitch Hz, Q, Output.
template <typename FilterT>
struct RbjPass final : public IirFilterBase<FilterT>
{
	FloatInPin pinQ; // resonance at the cutoff; no peak below ~0.707
	AudioOutPin pinOutput;

	RbjPass()
	{
		this->initFilter(pinOutput);
	}

	void setupFilterDesign(FilterT& f) override
	{
		f.setupN(this->normalisedPitch(), clampQ(pinQ.getValue()));
	}

	bool designParamsUpdated() override
	{
		return IirFilterBase<FilterT>::designParamsUpdated() || pinQ.isUpdated();
	}
};

using RbjLp = RbjPass<Iir::RBJ::LowPass>;
using RbjHp = RbjPass<Iir::RBJ::HighPass>;
using RbjAp = RbjPass<Iir::RBJ::AllPass>;

// Band-pass / Band-reject: Signal, Pitch Hz, Bandwidth Oct, Output.
// Band-pass is the cookbook's 'constant 0 dB peak gain' variant.
template <typename FilterT>
struct RbjBand final : public IirFilterBase<FilterT>
{
	FloatInPin pinBandwidth; // octaves
	AudioOutPin pinOutput;

	RbjBand()
	{
		this->initFilter(pinOutput);
	}

	void setupFilterDesign(FilterT& f) override
	{
		f.setupN(this->normalisedPitch(), clampOctaves(pinBandwidth.getValue()));
	}

	bool designParamsUpdated() override
	{
		return IirFilterBase<FilterT>::designParamsUpdated() || pinBandwidth.isUpdated();
	}
};

using RbjBp = RbjBand<Iir::RBJ::BandPass2>;
using RbjBr = RbjBand<Iir::RBJ::BandStop>;

// Notch: Signal, Pitch Hz, Q, Output. Pole-radius design: deeper and narrower
// than Band-reject at the same settings, suited to mains-hum removal (Q ~10-20).
struct RbjNotch final : public IirFilterBase<Iir::RBJ::IIRNotch>
{
	FloatInPin pinQ;
	AudioOutPin pinOutput;

	RbjNotch()
	{
		initFilter(pinOutput);
	}

	void setupFilterDesign(Iir::RBJ::IIRNotch& f) override
	{
		f.setupN(normalisedPitch(), clampQ(pinQ.getValue()));
	}

	bool designParamsUpdated() override
	{
		return IirFilterBase::designParamsUpdated() || pinQ.isUpdated();
	}
};

// Low-shelf / High-shelf: Signal, Pitch Hz, Gain Db, Slope, Output.
template <typename FilterT>
struct RbjShelf final : public IirFilterBase<FilterT>
{
	FloatInPin pinGain;  // shelf gain, dB
	FloatInPin pinSlope; // 1 = as steep as it can without overshoot
	AudioOutPin pinOutput;

	RbjShelf()
	{
		this->initFilter(pinOutput);
	}

	void setupFilterDesign(FilterT& f) override
	{
		f.setupN(this->normalisedPitch(), pinGain.getValue(), clampSlope(pinSlope.getValue()));
	}

	bool designParamsUpdated() override
	{
		return IirFilterBase<FilterT>::designParamsUpdated() || pinGain.isUpdated() || pinSlope.isUpdated();
	}
};

using RbjLs = RbjShelf<Iir::RBJ::LowShelf>;
using RbjHs = RbjShelf<Iir::RBJ::HighShelf>;

// Bell (peaking EQ): Signal, Pitch Hz, Bandwidth Oct, Gain Db, Output.
struct RbjBell final : public IirFilterBase<Iir::RBJ::BandShelf>
{
	FloatInPin pinBandwidth; // octaves
	FloatInPin pinGain;      // peak gain, dB
	AudioOutPin pinOutput;

	RbjBell()
	{
		initFilter(pinOutput);
	}

	void setupFilterDesign(Iir::RBJ::BandShelf& f) override
	{
		// note the iir1 argument order: centre, gain, then bandwidth.
		f.setupN(normalisedPitch(), pinGain.getValue(), clampOctaves(pinBandwidth.getValue()));
	}

	bool designParamsUpdated() override
	{
		return IirFilterBase::designParamsUpdated() || pinBandwidth.isUpdated() || pinGain.isUpdated();
	}
};

namespace
{
auto r1 = Register<RbjLp>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<Plugin id="SE RBJ LP" name="RBJ Low-pass" category="Filters">
    <Audio>
        <Pin name="Signal" datatype="float" rate="audio" linearInput="true"/>
        <Pin name="Pitch Hz" datatype="float" default="400" metadata="1,20000"/>
        <Pin name="Q" datatype="float" default="0.707" metadata="0.1,40"/>
        <Pin name="Output" datatype="float" rate="audio" direction="out"/>
    </Audio>
</Plugin>
)XML");

auto r2 = Register<RbjHp>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<Plugin id="SE RBJ HP" name="RBJ High-pass" category="Filters">
    <Audio>
        <Pin name="Signal" datatype="float" rate="audio" linearInput="true"/>
        <Pin name="Pitch Hz" datatype="float" default="400" metadata="1,20000"/>
        <Pin name="Q" datatype="float" default="0.707" metadata="0.1,40"/>
        <Pin name="Output" datatype="float" rate="audio" direction="out"/>
    </Audio>
</Plugin>
)XML");

auto r3 = Register<RbjAp>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<Plugin id="SE RBJ AP" name="RBJ All-pass" category="Filters">
    <Audio>
        <Pin name="Signal" datatype="float" rate="audio" linearInput="true"/>
        <Pin name="Pitch Hz" datatype="float" default="400" metadata="1,20000"/>
        <Pin name="Q" datatype="float" default="0.707" metadata="0.1,40"/>
        <Pin name="Output" datatype="float" rate="audio" direction="out"/>
    </Audio>
</Plugin>
)XML");

auto r4 = Register<RbjBp>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<Plugin id="SE RBJ BP" name="RBJ Band-pass" category="Filters">
    <Audio>
        <Pin name="Signal" datatype="float" rate="audio" linearInput="true"/>
        <Pin name="Pitch Hz" datatype="float" default="400" metadata="1,20000"/>
        <Pin name="Bandwidth Oct" datatype="float" default="1" metadata="0.05,8"/>
        <Pin name="Output" datatype="float" rate="audio" direction="out"/>
    </Audio>
</Plugin>
)XML");

auto r5 = Register<RbjBr>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<Plugin id="SE RBJ BR" name="RBJ Band-reject" category="Filters">
    <Audio>
        <Pin name="Signal" datatype="float" rate="audio" linearInput="true"/>
        <Pin name="Pitch Hz" datatype="float" default="400" metadata="1,20000"/>
        <Pin name="Bandwidth Oct" datatype="float" default="1" metadata="0.05,8"/>
        <Pin name="Output" datatype="float" rate="audio" direction="out"/>
    </Audio>
</Plugin>
)XML");

auto r6 = Register<RbjNotch>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<Plugin id="SE RBJ Notch" name="RBJ Notch" category="Filters">
    <Audio>
        <Pin name="Signal" datatype="float" rate="audio" linearInput="true"/>
        <Pin name="Pitch Hz" datatype="float" default="50" metadata="1,20000"/>
        <Pin name="Q" datatype="float" default="10" metadata="1,100"/>
        <Pin name="Output" datatype="float" rate="audio" direction="out"/>
    </Audio>
</Plugin>
)XML");

auto r7 = Register<RbjLs>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<Plugin id="SE RBJ LS" name="RBJ Low-shelf" category="Filters">
    <Audio>
        <Pin name="Signal" datatype="float" rate="audio" linearInput="true"/>
        <Pin name="Pitch Hz" datatype="float" default="400" metadata="1,20000"/>
        <Pin name="Gain Db" datatype="float" default="0" metadata="-100,100"/>
        <Pin name="Slope" datatype="float" default="1" metadata="0.1,1"/>
        <Pin name="Output" datatype="float" rate="audio" direction="out"/>
    </Audio>
</Plugin>
)XML");

auto r8 = Register<RbjHs>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<Plugin id="SE RBJ HS" name="RBJ High-shelf" category="Filters">
    <Audio>
        <Pin name="Signal" datatype="float" rate="audio" linearInput="true"/>
        <Pin name="Pitch Hz" datatype="float" default="400" metadata="1,20000"/>
        <Pin name="Gain Db" datatype="float" default="0" metadata="-100,100"/>
        <Pin name="Slope" datatype="float" default="1" metadata="0.1,1"/>
        <Pin name="Output" datatype="float" rate="audio" direction="out"/>
    </Audio>
</Plugin>
)XML");

auto r9 = Register<RbjBell>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<Plugin id="SE RBJ Bell" name="RBJ Bell" category="Filters">
    <Audio>
        <Pin name="Signal" datatype="float" rate="audio" linearInput="true"/>
        <Pin name="Pitch Hz" datatype="float" default="400" metadata="1,20000"/>
        <Pin name="Bandwidth Oct" datatype="float" default="1" metadata="0.05,8"/>
        <Pin name="Gain Db" datatype="float" default="0" metadata="-100,100"/>
        <Pin name="Output" datatype="float" rate="audio" direction="out"/>
    </Audio>
</Plugin>
)XML");
}
