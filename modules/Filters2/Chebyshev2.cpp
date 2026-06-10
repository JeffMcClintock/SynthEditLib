// SPDX-License-Identifier: ISC
// Copyright 2007-2026 Jeff McClintock.
//
// Chebyshev type II (inverse Chebyshev) filters, orders 1..12: low/high-pass,
// band-pass/reject and low/high/band-shelf. Flat passband (no ripple) with an
// equiripple stopband; the 'Stopband Db' pin sets how far down the stopband
// sits - larger = deeper rejection but a shallower transition. Notes: at
// 1 pole the transition is very gradual (the passband only reaches unity
// near Nyquist), and the band-shelf centre gain runs ~1 dB hot of the request
// (inherent to the type II prototype).
//
// Built on the iir1 library (see iir1/README-SynthEdit.md). Parameter changes
// crossfade between two filter instances - see IirFilterBase.h.

#include "IirFilterBase.h"

using namespace gmpi;
using namespace iir_filters;

namespace
{
	// At 0 dB the type II design degenerates (stopband meets passband).
	float clampStopband(float stopBandDb)
	{
		return std::clamp(stopBandDb, 1.0f, 200.0f);
	}
}

// Low-pass / High-pass: Signal, Pitch Hz, Stopband Db, Poles, Output.
template <typename FilterT>
struct Chebyshev2Pass final : public IirFilterBase<FilterT>
{
	FloatInPin pinStopband; // stopband attenuation, dB
	EnumInPin pinPoles;
	AudioOutPin pinOutput;

	Chebyshev2Pass()
	{
		this->initFilter(pinPoles, pinOutput);
	}

	void setupFilterDesign(FilterT& f) override
	{
		f.setupN(this->poles(), this->normalisedPitch(), clampStopband(pinStopband.getValue()));
	}

	bool designParamsUpdated() override
	{
		return IirFilterBase<FilterT>::designParamsUpdated() || pinStopband.isUpdated();
	}
};

using Chebyshev2Lp = Chebyshev2Pass<Iir::ChebyshevII::LowPass<maxPoles>>;
using Chebyshev2Hp = Chebyshev2Pass<Iir::ChebyshevII::HighPass<maxPoles>>;

// Band-pass / Band-reject: Signal, Pitch Hz, Width Hz, Stopband Db, Poles, Output.
template <typename FilterT>
struct Chebyshev2Band final : public IirFilterBase<FilterT>
{
	FloatInPin pinWidth;    // passband (or stopband) width, Hz
	FloatInPin pinStopband; // stopband attenuation, dB
	EnumInPin pinPoles;
	AudioOutPin pinOutput;

	Chebyshev2Band()
	{
		this->initFilter(pinPoles, pinOutput);
	}

	void setupFilterDesign(FilterT& f) override
	{
		f.setupN(this->poles(), this->normalisedPitch(),
			this->normalisedWidth(pinWidth.getValue()), clampStopband(pinStopband.getValue()));
	}

	bool designParamsUpdated() override
	{
		return IirFilterBase<FilterT>::designParamsUpdated() || pinWidth.isUpdated() || pinStopband.isUpdated();
	}
};

using Chebyshev2Bp = Chebyshev2Band<Iir::ChebyshevII::BandPass<maxPoles>>;
using Chebyshev2Br = Chebyshev2Band<Iir::ChebyshevII::BandStop<maxPoles>>;

// Low-shelf / High-shelf: Signal, Pitch Hz, Gain Db, Stopband Db, Poles, Output.
template <typename FilterT>
struct Chebyshev2Shelf final : public IirFilterBase<FilterT>
{
	FloatInPin pinGain;     // shelf gain, dB
	FloatInPin pinStopband; // stopband attenuation, dB
	EnumInPin pinPoles;
	AudioOutPin pinOutput;

	Chebyshev2Shelf()
	{
		this->initFilter(pinPoles, pinOutput);
	}

	void setupFilterDesign(FilterT& f) override
	{
		f.setupN(this->poles(), this->normalisedPitch(), pinGain.getValue(), clampStopband(pinStopband.getValue()));
	}

	bool designParamsUpdated() override
	{
		return IirFilterBase<FilterT>::designParamsUpdated() || pinGain.isUpdated() || pinStopband.isUpdated();
	}
};

using Chebyshev2Ls = Chebyshev2Shelf<Iir::ChebyshevII::LowShelf<maxPoles>>;
using Chebyshev2Hs = Chebyshev2Shelf<Iir::ChebyshevII::HighShelf<maxPoles>>;

// Band-shelf: Signal, Pitch Hz, Width Hz, Gain Db, Stopband Db, Poles, Output.
struct Chebyshev2Bs final : public IirFilterBase<Iir::ChebyshevII::BandShelf<maxPoles>>
{
	FloatInPin pinWidth;    // shelf width, Hz
	FloatInPin pinGain;     // shelf gain, dB
	FloatInPin pinStopband; // stopband attenuation, dB
	EnumInPin pinPoles;
	AudioOutPin pinOutput;

	Chebyshev2Bs()
	{
		initFilter(pinPoles, pinOutput);
	}

	void setupFilterDesign(Iir::ChebyshevII::BandShelf<maxPoles>& f) override
	{
		f.setupN(poles(), normalisedPitch(), normalisedWidth(pinWidth.getValue()),
			pinGain.getValue(), clampStopband(pinStopband.getValue()));
	}

	bool designParamsUpdated() override
	{
		return IirFilterBase::designParamsUpdated() || pinWidth.isUpdated() || pinGain.isUpdated() || pinStopband.isUpdated();
	}
};

namespace
{
auto r1 = Register<Chebyshev2Lp>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<Plugin id="SE Chebyshev2 LP" name="Chebyshev II Low-pass" category="Filters">
    <Audio>
        <Pin name="Signal" datatype="float" rate="audio" linearInput="true"/>
        <Pin name="Pitch Hz" datatype="float" default="400" metadata="1,20000"/>
        <Pin name="Stopband Db" datatype="float" default="48" metadata="1,120"/>
        <Pin name="Poles" datatype="enum" default="4" metadata="1=1,2,3,4,5,6,7,8,9,10,11,12"/>
        <Pin name="Output" datatype="float" rate="audio" direction="out"/>
    </Audio>
</Plugin>
)XML");

auto r2 = Register<Chebyshev2Hp>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<Plugin id="SE Chebyshev2 HP" name="Chebyshev II High-pass" category="Filters">
    <Audio>
        <Pin name="Signal" datatype="float" rate="audio" linearInput="true"/>
        <Pin name="Pitch Hz" datatype="float" default="400" metadata="1,20000"/>
        <Pin name="Stopband Db" datatype="float" default="48" metadata="1,120"/>
        <Pin name="Poles" datatype="enum" default="4" metadata="1=1,2,3,4,5,6,7,8,9,10,11,12"/>
        <Pin name="Output" datatype="float" rate="audio" direction="out"/>
    </Audio>
</Plugin>
)XML");

auto r3 = Register<Chebyshev2Bp>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<Plugin id="SE Chebyshev2 BP" name="Chebyshev II Band-pass" category="Filters">
    <Audio>
        <Pin name="Signal" datatype="float" rate="audio" linearInput="true"/>
        <Pin name="Pitch Hz" datatype="float" default="400" metadata="1,20000"/>
        <Pin name="Width Hz" datatype="float" default="100" metadata="1,20000"/>
        <Pin name="Stopband Db" datatype="float" default="48" metadata="1,120"/>
        <Pin name="Poles" datatype="enum" default="4" metadata="1=1,2,3,4,5,6,7,8,9,10,11,12"/>
        <Pin name="Output" datatype="float" rate="audio" direction="out"/>
    </Audio>
</Plugin>
)XML");

auto r4 = Register<Chebyshev2Br>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<Plugin id="SE Chebyshev2 BR" name="Chebyshev II Band-reject" category="Filters">
    <Audio>
        <Pin name="Signal" datatype="float" rate="audio" linearInput="true"/>
        <Pin name="Pitch Hz" datatype="float" default="400" metadata="1,20000"/>
        <Pin name="Width Hz" datatype="float" default="100" metadata="1,20000"/>
        <Pin name="Stopband Db" datatype="float" default="48" metadata="1,120"/>
        <Pin name="Poles" datatype="enum" default="4" metadata="1=1,2,3,4,5,6,7,8,9,10,11,12"/>
        <Pin name="Output" datatype="float" rate="audio" direction="out"/>
    </Audio>
</Plugin>
)XML");

auto r5 = Register<Chebyshev2Ls>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<Plugin id="SE Chebyshev2 LS" name="Chebyshev II Low-shelf" category="Filters">
    <Audio>
        <Pin name="Signal" datatype="float" rate="audio" linearInput="true"/>
        <Pin name="Pitch Hz" datatype="float" default="400" metadata="1,20000"/>
        <Pin name="Gain Db" datatype="float" default="0" metadata="-100,100"/>
        <Pin name="Stopband Db" datatype="float" default="48" metadata="1,120"/>
        <Pin name="Poles" datatype="enum" default="4" metadata="1=1,2,3,4,5,6,7,8,9,10,11,12"/>
        <Pin name="Output" datatype="float" rate="audio" direction="out"/>
    </Audio>
</Plugin>
)XML");

auto r6 = Register<Chebyshev2Hs>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<Plugin id="SE Chebyshev2 HS" name="Chebyshev II High-shelf" category="Filters">
    <Audio>
        <Pin name="Signal" datatype="float" rate="audio" linearInput="true"/>
        <Pin name="Pitch Hz" datatype="float" default="400" metadata="1,20000"/>
        <Pin name="Gain Db" datatype="float" default="0" metadata="-100,100"/>
        <Pin name="Stopband Db" datatype="float" default="48" metadata="1,120"/>
        <Pin name="Poles" datatype="enum" default="4" metadata="1=1,2,3,4,5,6,7,8,9,10,11,12"/>
        <Pin name="Output" datatype="float" rate="audio" direction="out"/>
    </Audio>
</Plugin>
)XML");

auto r7 = Register<Chebyshev2Bs>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<Plugin id="SE Chebyshev2 BS" name="Chebyshev II Band-shelf" category="Filters">
    <Audio>
        <Pin name="Signal" datatype="float" rate="audio" linearInput="true"/>
        <Pin name="Pitch Hz" datatype="float" default="400" metadata="1,20000"/>
        <Pin name="Width Hz" datatype="float" default="1000" metadata="1,20000"/>
        <Pin name="Gain Db" datatype="float" default="0" metadata="-100,100"/>
        <Pin name="Stopband Db" datatype="float" default="48" metadata="1,120"/>
        <Pin name="Poles" datatype="enum" default="4" metadata="1=1,2,3,4,5,6,7,8,9,10,11,12"/>
        <Pin name="Output" datatype="float" rate="audio" direction="out"/>
    </Audio>
</Plugin>
)XML");
}
