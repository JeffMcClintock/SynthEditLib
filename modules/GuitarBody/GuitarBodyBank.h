#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>

// A guitar body as a bank of resonant band-pass filters plus a direct path. No host types, so it can be tested on its own.
namespace guitar_body
{
struct Mode
{
	double hz;
	double q;
	double gain;
};

// A generic steel-string flat-top: the air (Helmholtz) resonance, the top and back plates, then a thinning
// spread of plate modes standing in for the dense upper region.
inline constexpr std::array<Mode, 16> modes = { {
	{ 100.0, 20.0, 1.00 }, // air
	{ 204.0, 30.0, 0.90 }, // top
	{ 235.0, 25.0, 0.50 }, // back
	{ 290.0, 25.0, 0.35 },
	{ 390.0, 30.0, 0.45 },
	{ 480.0, 30.0, 0.30 },
	{ 560.0, 35.0, 0.30 },
	{ 700.0, 30.0, 0.25 },
	{ 850.0, 30.0, 0.20 },
	{ 1050.0, 25.0, 0.20 },
	{ 1300.0, 25.0, 0.15 },
	{ 1600.0, 20.0, 0.15 },
	{ 2000.0, 20.0, 0.12 },
	{ 2500.0, 18.0, 0.10 },
	{ 3200.0, 15.0, 0.08 },
	{ 4000.0, 12.0, 0.06 },
} };

inline constexpr double pi = 3.14159265358979323846;
inline constexpr double brightModeHz = 600.0;  // Brightness tilts the modes above this.
inline constexpr double directGain = 0.3;      // the bridge also radiates directly, unfiltered.

struct Settings
{
	float size = 1.0f;       // 0.5..2: scales every mode down in frequency; 1 = dreadnought.
	float resonance = 0.5f;  // 0..1: mode Q from x0.4 to x2.5.
	float brightness = 0.5f; // 0..1: upper modes from x0.25 to x4.
	float mix = 1.0f;        // 0 = dry, 1 = body only.

	bool operator==(const Settings&) const = default;
};

class Bank
{
	struct Biquad
	{
		// Band-pass with 0 dB peak (RBJ), transposed direct form II; b1 is always 0 and b2 = -b0.
		double b0 = 0.0, a1 = 0.0, a2 = 0.0;
		double s1 = 0.0, s2 = 0.0;
		double gain = 0.0;

		double process(double x)
		{
			const double y = b0 * x + s1;
			s1 = -a1 * y + s2;
			s2 = -b0 * x - a2 * y;
			return y;
		}

		std::complex<double> response(double w) const
		{
			const std::complex<double> z1 = std::polar(1.0, -w);
			const std::complex<double> z2 = z1 * z1;
			return b0 * (1.0 - z2) / (1.0 + a1 * z1 + a2 * z2);
		}
	};

	std::array<Biquad, modes.size()> filters_;
	double wetGain_ = 1.0;
	double dry_ = 0.0;
	double wet_ = 1.0;

public:
	void setup(const Settings& settings, double sampleRate)
	{
		const double size = std::clamp(static_cast<double>(settings.size), 0.5, 2.0);
		const double qScale = std::pow(2.5, 2.0 * std::clamp(static_cast<double>(settings.resonance), 0.0, 1.0) - 1.0);
		const double brightScale = std::pow(4.0, 2.0 * std::clamp(static_cast<double>(settings.brightness), 0.0, 1.0) - 1.0);

		for (size_t i = 0; i < modes.size(); ++i)
		{
			const auto& m = modes[i];
			auto& f = filters_[i];
			const double hz = m.hz / size;
			if (hz >= 0.45 * sampleRate)
			{
				f.b0 = f.a1 = f.a2 = f.gain = 0.0;
				continue;
			}
			const double w = 2.0 * pi * hz / sampleRate;
			const double alpha = std::sin(w) / (2.0 * m.q * qScale);
			const double a0 = 1.0 + alpha;
			f.b0 = alpha / a0;
			f.a1 = -2.0 * std::cos(w) / a0;
			f.a2 = (1.0 - alpha) / a0;
			f.gain = m.gain * (m.hz > brightModeHz ? brightScale : 1.0);
		}

		// Same average loudness as the dry signal across the guitar's range, so Mix and the other controls don't jump the level.
		constexpr int points = 96;
		double power = 0.0;
		for (int i = 0; i < points; ++i)
		{
			const double hz = 80.0 * std::pow(5000.0 / 80.0, i / (points - 1.0));
			power += std::norm(response(2.0 * pi * hz / sampleRate, 1.0));
		}
		wetGain_ = 1.0 / std::sqrt(power / points);

		const double mix = std::clamp(static_cast<double>(settings.mix), 0.0, 1.0);
		dry_ = 1.0 - mix;
		wet_ = mix * wetGain_;
	}

	// Of the wet path before normalisation; w in radians per sample.
	std::complex<double> response(double w, double wetGain) const
	{
		std::complex<double> h = directGain;
		for (const auto& f : filters_)
			h += f.gain * f.response(w);
		return h * wetGain;
	}

	// Of the whole module at hz, including Mix.
	std::complex<double> responseAt(double hz, double sampleRate) const
	{
		return dry_ + response(2.0 * pi * hz / sampleRate, wet_);
	}

	// Returns the output and sets `ringing` to the summed resonator output, which falls to zero once they have rung out.
	float process(float x, double& ringing)
	{
		double resonators = 0.0;
		for (auto& f : filters_)
			resonators += f.gain * f.process(x);
		ringing = resonators;
		return static_cast<float>(dry_ * x + wet_ * (directGain * x + resonators));
	}

	// Of a constant input, once the resonators have settled: band-passes block DC.
	float steadyOutput(float x) const
	{
		return static_cast<float>((dry_ + wet_ * directGain) * x);
	}

	// Called between blocks so a long decay never reaches denormal territory.
	void flushDenormals()
	{
		for (auto& f : filters_)
		{
			if (std::abs(f.s1) < 1e-20)
				f.s1 = 0.0;
			if (std::abs(f.s2) < 1e-20)
				f.s2 = 0.0;
		}
	}

	void reset()
	{
		for (auto& f : filters_)
			f.s1 = f.s2 = 0.0;
	}
};
}
