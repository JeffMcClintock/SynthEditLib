// SPDX-License-Identifier: ISC
// Copyright 2007-2026 Jeff McClintock.
//
// Chebyshev type I filters, orders 1..12: low/high-pass, band-pass/reject and
// low/high/band-shelf. Steeper rolloff than Butterworth at the same order, in
// exchange for ripple in the passband (the 'Ripple Db' pin; smaller = flatter,
// approaching Butterworth). Note: the ripple eats into the shelf gains, so a
// +6 dB shelf at 1 dB ripple lands near +5 dB (inherent to the type I design).
//
// Built on the iir1 library (see iir1/README-SynthEdit.md). Parameter changes
// crossfade between two filter instances - see IirFilterBase.h.

#include "IirFilterBase.h"

using namespace gmpi;
using namespace iir_filters;

namespace
{
	// At 0 dB ripple the type I design degenerates (1/eps -> infinity).
	float clampRipple(float rippleDb)
	{
		return std::clamp(rippleDb, 0.01f, 40.0f);
	}
}

// Low-pass / High-pass: Signal, Pitch Hz, Ripple Db, Poles, Output.
template <typename FilterT>
struct Chebyshev1Pass final : public IirFilterBase<FilterT>
{
	FloatInPin pinRipple; // passband ripple, dB
	EnumInPin pinPoles;
	AudioOutPin pinOutput;

	Chebyshev1Pass()
	{
		this->initFilter(pinPoles, pinOutput);
	}

	void setupFilterDesign(FilterT& f) override
	{
		f.setupN(this->poles(), this->normalisedPitch(), clampRipple(pinRipple.getValue()));
	}

	bool designParamsUpdated() override
	{
		return IirFilterBase<FilterT>::designParamsUpdated() || pinRipple.isUpdated();
	}
};

using Chebyshev1Lp = Chebyshev1Pass<Iir::ChebyshevI::LowPass<maxPoles>>;
using Chebyshev1Hp = Chebyshev1Pass<Iir::ChebyshevI::HighPass<maxPoles>>;

// Band-pass / Band-reject: Signal, Pitch Hz, Width Hz, Ripple Db, Poles, Output.
template <typename FilterT>
struct Chebyshev1Band final : public IirFilterBase<FilterT>
{
	FloatInPin pinWidth;  // passband (or stopband) width, Hz
	FloatInPin pinRipple; // passband ripple, dB
	EnumInPin pinPoles;
	AudioOutPin pinOutput;

	Chebyshev1Band()
	{
		this->initFilter(pinPoles, pinOutput);
	}

	void setupFilterDesign(FilterT& f) override
	{
		f.setupN(this->poles(), this->normalisedPitch(),
			this->normalisedWidth(pinWidth.getValue()), clampRipple(pinRipple.getValue()));
	}

	bool designParamsUpdated() override
	{
		return IirFilterBase<FilterT>::designParamsUpdated() || pinWidth.isUpdated() || pinRipple.isUpdated();
	}
};

using Chebyshev1Bp = Chebyshev1Band<Iir::ChebyshevI::BandPass<maxPoles>>;
using Chebyshev1Br = Chebyshev1Band<Iir::ChebyshevI::BandStop<maxPoles>>;

// Low-shelf / High-shelf: Signal, Pitch Hz, Gain Db, Ripple Db, Poles, Output.
template <typename FilterT>
struct Chebyshev1Shelf final : public IirFilterBase<FilterT>
{
	FloatInPin pinGain;   // shelf gain, dB
	FloatInPin pinRipple; // passband ripple, dB
	EnumInPin pinPoles;
	AudioOutPin pinOutput;

	Chebyshev1Shelf()
	{
		this->initFilter(pinPoles, pinOutput);
	}

	void setupFilterDesign(FilterT& f) override
	{
		f.setupN(this->poles(), this->normalisedPitch(), pinGain.getValue(), clampRipple(pinRipple.getValue()));
	}

	bool designParamsUpdated() override
	{
		return IirFilterBase<FilterT>::designParamsUpdated() || pinGain.isUpdated() || pinRipple.isUpdated();
	}
};

using Chebyshev1Ls = Chebyshev1Shelf<Iir::ChebyshevI::LowShelf<maxPoles>>;
using Chebyshev1Hs = Chebyshev1Shelf<Iir::ChebyshevI::HighShelf<maxPoles>>;

// Band-shelf: Signal, Pitch Hz, Width Hz, Gain Db, Ripple Db, Poles, Output.
struct Chebyshev1Bs final : public IirFilterBase<Iir::ChebyshevI::BandShelf<maxPoles>>
{
	FloatInPin pinWidth;  // shelf width, Hz
	FloatInPin pinGain;   // shelf gain, dB
	FloatInPin pinRipple; // passband ripple, dB
	EnumInPin pinPoles;
	AudioOutPin pinOutput;

	Chebyshev1Bs()
	{
		initFilter(pinPoles, pinOutput);
	}

	void setupFilterDesign(Iir::ChebyshevI::BandShelf<maxPoles>& f) override
	{
		f.setupN(poles(), normalisedPitch(), normalisedWidth(pinWidth.getValue()),
			pinGain.getValue(), clampRipple(pinRipple.getValue()));
	}

	bool designParamsUpdated() override
	{
		return IirFilterBase::designParamsUpdated() || pinWidth.isUpdated() || pinGain.isUpdated() || pinRipple.isUpdated();
	}
};

namespace
{
auto r1 = Register<Chebyshev1Lp>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<Plugin id="SE Chebyshev1 LP" name="Chebyshev I Low-pass" category="Filters">
    <Audio>
        <Pin name="Signal" datatype="float" rate="audio" linearInput="true"/>
        <Pin name="Pitch Hz" datatype="float" default="400" metadata="1,20000"/>
        <Pin name="Ripple Db" datatype="float" default="1" metadata="0.01,12"/>
        <Pin name="Poles" datatype="enum" default="4" metadata="1=1,2,3,4,5,6,7,8,9,10,11,12"/>
        <Pin name="Output" datatype="float" rate="audio" direction="out"/>
    </Audio>
</Plugin>
)XML");

auto r2 = Register<Chebyshev1Hp>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<Plugin id="SE Chebyshev1 HP" name="Chebyshev I High-pass" category="Filters">
    <Audio>
        <Pin name="Signal" datatype="float" rate="audio" linearInput="true"/>
        <Pin name="Pitch Hz" datatype="float" default="400" metadata="1,20000"/>
        <Pin name="Ripple Db" datatype="float" default="1" metadata="0.01,12"/>
        <Pin name="Poles" datatype="enum" default="4" metadata="1=1,2,3,4,5,6,7,8,9,10,11,12"/>
        <Pin name="Output" datatype="float" rate="audio" direction="out"/>
    </Audio>
</Plugin>
)XML");

auto r3 = Register<Chebyshev1Bp>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<Plugin id="SE Chebyshev1 BP" name="Chebyshev I Band-pass" category="Filters">
    <Audio>
        <Pin name="Signal" datatype="float" rate="audio" linearInput="true"/>
        <Pin name="Pitch Hz" datatype="float" default="400" metadata="1,20000"/>
        <Pin name="Width Hz" datatype="float" default="100" metadata="1,20000"/>
        <Pin name="Ripple Db" datatype="float" default="1" metadata="0.01,12"/>
        <Pin name="Poles" datatype="enum" default="4" metadata="1=1,2,3,4,5,6,7,8,9,10,11,12"/>
        <Pin name="Output" datatype="float" rate="audio" direction="out"/>
    </Audio>
</Plugin>
)XML");

auto r4 = Register<Chebyshev1Br>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<Plugin id="SE Chebyshev1 BR" name="Chebyshev I Band-reject" category="Filters">
    <Audio>
        <Pin name="Signal" datatype="float" rate="audio" linearInput="true"/>
        <Pin name="Pitch Hz" datatype="float" default="400" metadata="1,20000"/>
        <Pin name="Width Hz" datatype="float" default="100" metadata="1,20000"/>
        <Pin name="Ripple Db" datatype="float" default="1" metadata="0.01,12"/>
        <Pin name="Poles" datatype="enum" default="4" metadata="1=1,2,3,4,5,6,7,8,9,10,11,12"/>
        <Pin name="Output" datatype="float" rate="audio" direction="out"/>
    </Audio>
</Plugin>
)XML");

auto r5 = Register<Chebyshev1Ls>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<Plugin id="SE Chebyshev1 LS" name="Chebyshev I Low-shelf" category="Filters">
    <Audio>
        <Pin name="Signal" datatype="float" rate="audio" linearInput="true"/>
        <Pin name="Pitch Hz" datatype="float" default="400" metadata="1,20000"/>
        <Pin name="Gain Db" datatype="float" default="0" metadata="-100,100"/>
        <Pin name="Ripple Db" datatype="float" default="1" metadata="0.01,12"/>
        <Pin name="Poles" datatype="enum" default="4" metadata="1=1,2,3,4,5,6,7,8,9,10,11,12"/>
        <Pin name="Output" datatype="float" rate="audio" direction="out"/>
    </Audio>
</Plugin>
)XML");

auto r6 = Register<Chebyshev1Hs>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<Plugin id="SE Chebyshev1 HS" name="Chebyshev I High-shelf" category="Filters">
    <Audio>
        <Pin name="Signal" datatype="float" rate="audio" linearInput="true"/>
        <Pin name="Pitch Hz" datatype="float" default="400" metadata="1,20000"/>
        <Pin name="Gain Db" datatype="float" default="0" metadata="-100,100"/>
        <Pin name="Ripple Db" datatype="float" default="1" metadata="0.01,12"/>
        <Pin name="Poles" datatype="enum" default="4" metadata="1=1,2,3,4,5,6,7,8,9,10,11,12"/>
        <Pin name="Output" datatype="float" rate="audio" direction="out"/>
    </Audio>
</Plugin>
)XML");

auto r7 = Register<Chebyshev1Bs>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<Plugin id="SE Chebyshev1 BS" name="Chebyshev I Band-shelf" category="Filters">
    <Audio>
        <Pin name="Signal" datatype="float" rate="audio" linearInput="true"/>
        <Pin name="Pitch Hz" datatype="float" default="400" metadata="1,20000"/>
        <Pin name="Width Hz" datatype="float" default="1000" metadata="1,20000"/>
        <Pin name="Gain Db" datatype="float" default="0" metadata="-100,100"/>
        <Pin name="Ripple Db" datatype="float" default="1" metadata="0.01,12"/>
        <Pin name="Poles" datatype="enum" default="4" metadata="1=1,2,3,4,5,6,7,8,9,10,11,12"/>
        <Pin name="Output" datatype="float" rate="audio" direction="out"/>
    </Audio>
</Plugin>
)XML");
}
