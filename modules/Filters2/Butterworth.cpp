// SPDX-License-Identifier: ISC
// Copyright 2007-2026 Jeff McClintock.
//
// Butterworth filters, orders 1..12: low/high-pass, band-pass/reject and
// low/high/band-shelf. Maximally flat passband; steepness set by the Poles pin.
//
// Built on the iir1 library (see iir1/README-SynthEdit.md). These replace the
// deprecated DspFilters-based 'Butterworth ... 2' modules
// (modules/Filters/IIR_Filters2.cpp): same pins and behaviour, but maintained
// upstream code with no denormal DC injected into the signal path. Parameter
// changes crossfade between two filter instances - see IirFilterBase.h.

#include "IirFilterBase.h"

using namespace gmpi;
using namespace iir_filters;

// Low-pass / High-pass: Signal, Pitch Hz, Poles, Output.
template <typename FilterT>
struct ButterworthPass3 final : public IirFilterBase<FilterT>
{
	EnumInPin pinPoles;
	AudioOutPin pinOutput;

	ButterworthPass3()
	{
		this->initFilter(pinPoles, pinOutput);
	}

	void setupFilterDesign(FilterT& f) override
	{
		f.setupN(this->poles(), this->normalisedPitch());
	}
};

using ButterworthLp3 = ButterworthPass3<Iir::Butterworth::LowPass<maxPoles>>;
using ButterworthHp3 = ButterworthPass3<Iir::Butterworth::HighPass<maxPoles>>;

// Band-pass / Band-reject: Signal, Pitch Hz, Width Hz, Poles, Output.
template <typename FilterT>
struct ButterworthBand3 final : public IirFilterBase<FilterT>
{
	FloatInPin pinWidth; // passband (or stopband) width, Hz
	EnumInPin pinPoles;
	AudioOutPin pinOutput;

	ButterworthBand3()
	{
		this->initFilter(pinPoles, pinOutput);
	}

	void setupFilterDesign(FilterT& f) override
	{
		f.setupN(this->poles(), this->normalisedPitch(), this->normalisedWidth(pinWidth.getValue()));
	}

	bool designParamsUpdated() override
	{
		return IirFilterBase<FilterT>::designParamsUpdated() || pinWidth.isUpdated();
	}
};

using ButterworthBp3 = ButterworthBand3<Iir::Butterworth::BandPass<maxPoles>>;
using ButterworthBr3 = ButterworthBand3<Iir::Butterworth::BandStop<maxPoles>>;

// Low-shelf / High-shelf: Signal, Pitch Hz, Gain Db, Poles, Output.
template <typename FilterT>
struct ButterworthShelf3 final : public IirFilterBase<FilterT>
{
	FloatInPin pinGain; // shelf gain, dB
	EnumInPin pinPoles;
	AudioOutPin pinOutput;

	ButterworthShelf3()
	{
		this->initFilter(pinPoles, pinOutput);
	}

	void setupFilterDesign(FilterT& f) override
	{
		f.setupN(this->poles(), this->normalisedPitch(), pinGain.getValue());
	}

	bool designParamsUpdated() override
	{
		return IirFilterBase<FilterT>::designParamsUpdated() || pinGain.isUpdated();
	}
};

using ButterworthLs3 = ButterworthShelf3<Iir::Butterworth::LowShelf<maxPoles>>;
using ButterworthHs3 = ButterworthShelf3<Iir::Butterworth::HighShelf<maxPoles>>;

// Band-shelf: Signal, Pitch Hz, Width Hz, Gain Db, Poles, Output.
struct ButterworthBs3 final : public IirFilterBase<Iir::Butterworth::BandShelf<maxPoles>>
{
	FloatInPin pinWidth; // shelf width, Hz
	FloatInPin pinGain;  // shelf gain, dB
	EnumInPin pinPoles;
	AudioOutPin pinOutput;

	ButterworthBs3()
	{
		initFilter(pinPoles, pinOutput);
	}

	void setupFilterDesign(Iir::Butterworth::BandShelf<maxPoles>& f) override
	{
		f.setupN(poles(), normalisedPitch(), normalisedWidth(pinWidth.getValue()), pinGain.getValue());
	}

	bool designParamsUpdated() override
	{
		return IirFilterBase::designParamsUpdated() || pinWidth.isUpdated() || pinGain.isUpdated();
	}
};

namespace
{
auto r1 = Register<ButterworthLp3>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<Plugin id="SE Butterworth LP3" name="Butterworth Low-pass" category="Filters">
    <Audio>
        <Pin name="Signal" datatype="float" rate="audio" linearInput="true"/>
        <Pin name="Pitch Hz" datatype="float" default="400" metadata="1,20000"/>
        <Pin name="Poles" datatype="enum" default="4" metadata="1=1,2,3,4,5,6,7,8,9,10,11,12"/>
        <Pin name="Output" datatype="float" rate="audio" direction="out"/>
    </Audio>
</Plugin>
)XML");

auto r2 = Register<ButterworthHp3>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<Plugin id="SE Butterworth HP3" name="Butterworth High-pass" category="Filters">
    <Audio>
        <Pin name="Signal" datatype="float" rate="audio" linearInput="true"/>
        <Pin name="Pitch Hz" datatype="float" default="400" metadata="1,20000"/>
        <Pin name="Poles" datatype="enum" default="4" metadata="1=1,2,3,4,5,6,7,8,9,10,11,12"/>
        <Pin name="Output" datatype="float" rate="audio" direction="out"/>
    </Audio>
</Plugin>
)XML");

auto r3 = Register<ButterworthBp3>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<Plugin id="SE Butterworth BP3" name="Butterworth Band-pass" category="Filters">
    <Audio>
        <Pin name="Signal" datatype="float" rate="audio" linearInput="true"/>
        <Pin name="Pitch Hz" datatype="float" default="400" metadata="1,20000"/>
        <Pin name="Width Hz" datatype="float" default="100" metadata="1,20000"/>
        <Pin name="Poles" datatype="enum" default="4" metadata="1=1,2,3,4,5,6,7,8,9,10,11,12"/>
        <Pin name="Output" datatype="float" rate="audio" direction="out"/>
    </Audio>
</Plugin>
)XML");

auto r4 = Register<ButterworthBr3>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<Plugin id="SE Butterworth BR3" name="Butterworth Band-reject" category="Filters">
    <Audio>
        <Pin name="Signal" datatype="float" rate="audio" linearInput="true"/>
        <Pin name="Pitch Hz" datatype="float" default="400" metadata="1,20000"/>
        <Pin name="Width Hz" datatype="float" default="100" metadata="1,20000"/>
        <Pin name="Poles" datatype="enum" default="4" metadata="1=1,2,3,4,5,6,7,8,9,10,11,12"/>
        <Pin name="Output" datatype="float" rate="audio" direction="out"/>
    </Audio>
</Plugin>
)XML");

auto r5 = Register<ButterworthLs3>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<Plugin id="SE Butterworth LS3" name="Butterworth Low-shelf" category="Filters">
    <Audio>
        <Pin name="Signal" datatype="float" rate="audio" linearInput="true"/>
        <Pin name="Pitch Hz" datatype="float" default="400" metadata="1,20000"/>
        <Pin name="Gain Db" datatype="float" default="0" metadata="-100,100"/>
        <Pin name="Poles" datatype="enum" default="4" metadata="1=1,2,3,4,5,6,7,8,9,10,11,12"/>
        <Pin name="Output" datatype="float" rate="audio" direction="out"/>
    </Audio>
</Plugin>
)XML");

auto r6 = Register<ButterworthHs3>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<Plugin id="SE Butterworth HS3" name="Butterworth High-shelf" category="Filters">
    <Audio>
        <Pin name="Signal" datatype="float" rate="audio" linearInput="true"/>
        <Pin name="Pitch Hz" datatype="float" default="400" metadata="1,20000"/>
        <Pin name="Gain Db" datatype="float" default="0" metadata="-100,100"/>
        <Pin name="Poles" datatype="enum" default="4" metadata="1=1,2,3,4,5,6,7,8,9,10,11,12"/>
        <Pin name="Output" datatype="float" rate="audio" direction="out"/>
    </Audio>
</Plugin>
)XML");

auto r7 = Register<ButterworthBs3>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<Plugin id="SE Butterworth BS3" name="Butterworth Band-shelf" category="Filters">
    <Audio>
        <Pin name="Signal" datatype="float" rate="audio" linearInput="true"/>
        <Pin name="Pitch Hz" datatype="float" default="400" metadata="1,20000"/>
        <Pin name="Width Hz" datatype="float" default="1000" metadata="1,20000"/>
        <Pin name="Gain Db" datatype="float" default="0" metadata="-100,100"/>
        <Pin name="Poles" datatype="enum" default="4" metadata="1=1,2,3,4,5,6,7,8,9,10,11,12"/>
        <Pin name="Output" datatype="float" rate="audio" direction="out"/>
    </Audio>
</Plugin>
)XML");
}
