#include "FretPluckedOscGui.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace gmpi;
using namespace gmpi::drawing;
using namespace fret_plucked;

namespace {
bool registeredGui = gmpi::Register<FretPluckedOscGui>::withId("SE Fret Plucked Osc");

constexpr int pointsPerRidge = 64; // at least.
constexpr double twoPi = 6.283185307179586;
constexpr float floorDb = -60.0f;
constexpr float loudnessExponent = 0.6f; // Stevens: loudness grows as amplitude^0.6.

// Height 0..1 for a level in dB: perceived loudness, shifted so floorDb sits on the floor.
float loudness(float db)
{
	const float floor = std::pow(10.0f, loudnessExponent * floorDb / 20.0f);
	const float l = std::pow(10.0f, loudnessExponent * db / 20.0f);
	return std::clamp((l - floor) / (1.0f - floor), 0.0f, 1.0f);
}
}

FretPluckedOscGui::FretPluckedOscGui()
{
	pinEnvelopeData.onUpdate = [this](editor::PinBase*)
	{
		Params p;
		if (decodeEnvelopeBlob(pinEnvelopeData.value, p))
			params_ = p;
		else
			params_.reset();
		redraw();
	};
}

ReturnCode FretPluckedOscGui::render(drawing::api::IDeviceContext* dc)
{
	const auto colorBackground = colorFromHex(0x2A3632);
	const auto colorRidge = colorFromHex(0x25E456);
	const auto colorLowest = colorFromHex(0xF8F600);
	const auto colorFloor = colorFromHex(0x8F958F, 0.6f);

	Graphics g(dc);
	ClipDrawingToBounds _(g, bounds);

	Rect r;
	getClipArea(&r);
	const float width = getWidth(r);
	const float height = getHeight(r);

	auto backgroundBrush = g.createSolidColorBrush(colorBackground);
	g.fillRectangle(r, backgroundBrush);

	if (!labelFormat_)
	{
		std::vector<std::string_view> family = { "Arial" };
		labelFormat_ = g.getFactory().createTextFormat(10.0f, family);
		labelFormat_.setTextAlignment(TextAlignment::Leading);
		labelFormat_.setParagraphAlignment(ParagraphAlignment::Center);
		labelFormat_.setWordWrapping(WordWrapping::NoWrap);
	}

	// The partials come from the DSP, which only reports once a voice has run.
	if (!params_)
	{
		auto hintBrush = g.createSolidColorBrush(colorFloor);
		g.drawTextU("Play a note to show the harmonic envelopes", labelFormat_, Rect{ 6.0f, 0.0f, width, height }, hintBrush);
		return ReturnCode::Ok;
	}

	// Harmonic envelopes of middle A only; unison copies share their partial's envelope.
	constexpr double f0 = 440.0;
	constexpr double maxHz = 20000.0;
	Params p = *params_;
	p.unison = 0;
	auto parts = partials(p);
	std::erase_if(parts, [](const Partial& part) { return part.hz(f0) > maxHz; });
	if (parts.empty())
		return ReturnCode::Ok;
	const int lowestNumber = parts.front().number;
	std::sort(parts.begin(), parts.end(), [](const Partial& a, const Partial& b) { return a.hz(f0) < b.hz(f0); });

	// Show the decay, the freeze, and the looped tail fading at the lowest partial's rate until it reaches the floor.
	const double freeze = freezeSeconds;
	const double tailT60 = (std::max)(1e-3, static_cast<double>(p.decayTime));
	const double lowestDropAtFreeze = -floorDb * freeze / tailT60;
	const double span = std::clamp(freeze + tailT60 * (std::max)(0.0, (-floorDb - lowestDropAtFreeze) / -floorDb), freeze * 1.25, 12.0);
	const double topHz = (std::max)(2.0 * f0, parts.back().hz(f0) * 1.05);

	double maxAmplitude = 0.0;
	for (const auto& part : parts)
		maxAmplitude = (std::max)(maxAmplitude, part.amplitude);

	// Each partial and its twin start in step, so the pair's envelope swings between their sum and their difference.
	const double loopLength = loopPeriods(p, f0) / f0;
	double fastestBeat = 0.0;
	for (const auto& part : parts)
		fastestBeat = (std::max)(fastestBeat, beatHz(part, part.hz(f0), loopLength));

	auto levelDb = [&](const Partial& part, double t)
	{
		const double decayed = (std::min)(t, freeze);
		const double tail = (std::max)(0.0, t - freeze);
		const double share = 1.0 / (1.0 + part.twinLevel);
		const double twin = part.twinLevel * share;
		const double beat = std::sqrt(share * share + twin * twin + 2.0 * share * twin * std::cos(twoPi * beatHz(part, part.hz(f0), loopLength) * t));
		return static_cast<float>(20.0 * std::log10(part.amplitude * beat / maxAmplitude) + floorDb * (decayed / part.t60 + tail / tailT60));
	};

	// Oblique projection: time left to right, frequency receding from the front (highest) to the back (lowest).
	constexpr float margin = 4.0f;
	constexpr float labelHeight = 14.0f;
	const float depthX = width * 0.3f;
	const float frontY = height - labelHeight;
	const float depthY = (frontY - margin) * 0.4f;
	constexpr float rightLabelWidth = 44.0f;
	const float plotWidth = width - depthX - 2.0f * margin - rightLabelWidth;
	const float vscale = (std::max)(1.0f, frontY - margin - depthY);

	auto project = [&](double hz, double t, float db)
	{
		const float depth = static_cast<float>(1.0 - hz / topHz);
		return Point(margin + plotWidth * static_cast<float>(t / span) + depthX * depth, frontY - depthY * depth - vscale * loudness(db));
	};

	auto factory = g.getFactory();

	// Floor outline, and the freeze line where every envelope stops decaying and the loop begins.
	auto floorBrush = g.createSolidColorBrush(colorFloor);
	g.drawLine(project(0.0, 0.0, floorDb), project(topHz, 0.0, floorDb), floorBrush, 1.0f);
	g.drawLine(project(0.0, 0.0, floorDb), project(0.0, span, floorDb), floorBrush, 1.0f);
	g.drawLine(project(0.0, freeze, floorDb), project(topHz, freeze, floorDb), floorBrush, 1.0f);
	g.drawLine(project(topHz, 0.0, floorDb), project(topHz, span, floorDb), floorBrush, 1.0f);

	// Enough points to follow the fastest beat, within reason.
	const int points = std::clamp(static_cast<int>(span * fastestBeat * 12.0), pointsPerRidge, 1200);
	std::vector<double> times;
	times.reserve(points + 2);
	for (int i = 0; i <= points; ++i)
		times.push_back(span * i / points);
	times.push_back(freeze);
	std::sort(times.begin(), times.end());

	auto ridgeBrush = g.createSolidColorBrush(colorRidge);
	auto lowestBrush = g.createSolidColorBrush(colorLowest);
	auto fillBrush = g.createSolidColorBrush(colorFromHex(0x25E456, 0.1f));

	// Lowest frequency first, at the back: the shorter higher harmonics stand in front of it.
	for (const auto& part : parts)
	{
		const double hz = part.hz(f0);

		auto outline = factory.createPathGeometry();
		auto fill = factory.createPathGeometry();
		{
			auto line = outline.open();
			auto area = fill.open();
			line.beginFigure(project(hz, 0.0, levelDb(part, 0.0)), FigureBegin::Hollow);
			area.beginFigure(project(hz, 0.0, floorDb), FigureBegin::Filled);
			for (const double t : times)
			{
				const Point pt = project(hz, t, levelDb(part, t));
				line.addLine(pt);
				area.addLine(pt);
			}
			area.addLine(project(hz, span, floorDb));
			line.endFigure(FigureEnd::Open);
			area.endFigure(FigureEnd::Closed);
			line.close();
			area.close();
		}

		// Opaque first, so nearer envelopes hide farther ones.
		g.fillGeometry(fill, backgroundBrush);
		g.fillGeometry(fill, fillBrush);
		g.drawGeometry(outline, part.number == lowestNumber ? lowestBrush : ridgeBrush, 1.0f);
	}

	// The front edge is the time axis; the frequency labels sit off its right-hand end, beside the depth edge.
	auto label = [&](Point at, const char* text)
	{
		g.drawTextU(text, labelFormat_, Rect{ at.x + 2.0f, at.y, at.x + 80.0f, at.y + labelHeight }, floorBrush);
	};
	char text[32];
	const double tickStep = span > 6.0 ? 2.0 : 1.0;
	for (double t = 0.0; t < span - 0.4 * tickStep; t += tickStep) // the end is left clear for the frequency label
	{
		const Point at = project(topHz, t, floorDb);
		g.drawLine(at, { at.x, at.y + 3.0f }, floorBrush, 1.0f);
		std::snprintf(text, sizeof(text), t == freeze ? "%g s loop" : "%g s", t);
		label({ at.x - 2.0f, at.y }, text);
	}
	const Point a4 = project(f0, span, floorDb);
	label({ a4.x + 2.0f, a4.y - labelHeight * 0.5f }, "A4");
	std::snprintf(text, sizeof(text), "%.1f kHz", topHz / 1000.0);
	const Point top = project(topHz, span, floorDb);
	label({ top.x + 6.0f, top.y - labelHeight * 0.5f }, text);

	return ReturnCode::Ok;
}
