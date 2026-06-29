// SPDX-License-Identifier: ISC
// Copyright 2006-2026 Jeff McClintock.
//
// Scope4Gui - oscilloscope display. Ported from the legacy SE SDK3 Scope3Gui
// to the gmpi_ui editor + drawing API. Renders polyphonic capture buffers fed
// by the Scope4 / TriggerScope4 DSP (Scope4.cpp). This file also supplies the
// plugin XML (shared <Audio> + <GUI> sections) for both "SE Scope4" and
// "SE TrigScope4".

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <iterator>
#include <vector>
#include "helpers/GmpiPluginEditor.h"
#include "helpers/Timer.h"
#include "helpers/SimplifyGraph.h"

using namespace gmpi;
using namespace gmpi::drawing;
using namespace gmpi::editor;

namespace
{
constexpr int   SCOPE_BUFFER_SIZE = 400;
constexpr int   kVoiceCount       = 128;
constexpr float kSnapToPixel      = 0.5f;
constexpr int   kCaptureBytes     = SCOPE_BUFFER_SIZE * static_cast<int>(sizeof(float));

// Phosphor persistence tuning.
constexpr float kFadeTau          = 0.2f;  // decay time-constant in seconds (smaller = faster fade)
constexpr float kWaveIntensity    = 1.15f; // brightness a steadily-swept pixel settles to (>1 lets overlaps bloom)
constexpr float kVelocityFloor    = 0.6f;  // steep segments never dim below this (keeps the trace connected, no gaps)

// Auto-brightness (AGC). A periodic trace hits the same pixels every frame and
// integrates up to kWaveIntensity; an aperiodic/random trace hits each pixel only
// once and sits at the dimmer single-hit level (≈ kWaveIntensity·(1-decay)). Drive
// an overall gain from the brightest accumulated pixel so both read equally bright.
constexpr float kAutoGainTarget  = kWaveIntensity; // displayed peak a normalized trace reaches
constexpr float kAutoGainMax     = 8.0f;  // ceiling on the boost (caps amplification of very faint traces)
constexpr float kAutoGainFloor   = 0.01f; // engage only when a real trace is present (else hold the gain)
constexpr float kAutoGainTauUp   = 0.35f; // smooth ramp when brightening (gain rising — dim content appears)
constexpr float kAutoGainTauDown = 0.10f; // faster when dimming (gain falling) so it never flashes over-bright

constexpr float kLiveWindowSec    = 0.5f;  // a trace is redrawn at full brightness while updated this recently
constexpr float kAnimateWindowSec = 2.0f;  // keep the fade timer running this long after the last update
constexpr int   kTimerMs          = 33;    // ~30 Hz fade animation

// The accumulation buffers render at physical resolution, but each dimension is
// halved (a power of two) until it is no bigger than this. Bounds both memory and
// the glow cost (∝ area) — small scopes stay pixel-sharp, large ones cap and
// upscale on composite.
constexpr int   kMaxBufferDim     = 256;

// Linear-light RGB pixel for the HDR accumulation buffer (values may exceed 1.0).
struct RgbF { float r = 0.0f, g = 0.0f, b = 0.0f; };
inline RgbF operator*(const RgbF& c, float s) { return { c.r * s, c.g * s, c.b * s }; }

// Default colours (LINEAR light) used when a colour pin is left blank. The hex pin
// defaults are the sRGB equivalents of these, so the look is the same either way.
constexpr RgbF  kLitGreen { 0.0f,   1.0f,  0.0f };            // trace A   ("00FF00")
constexpr RgbF  kLitYellow{ 1.0f,   1.0f,  0.0f };            // trace B   ("FFFF00")
constexpr Color kDefaultAxis{ 0.275f, 0.30f, 0.275f, 1.0f }; // axis lines ("8F958F")
constexpr Color kDefaultText{ 0.55f,  0.60f, 0.55f, 1.0f };  // value text ("C4CBC4")
constexpr float kPolyDim = 0.4f;                             // non-main poly voices this much dimmer

// Emissive glow (after Jeff's EmmissiveComponent): bright pixels radiate light to
// their neighbours via an inverse-square falloff kernel, computed in linear light.
constexpr int   kGlowRadius    = 14;   // halo radius in pixels (CPU cost ∝ radius²)
constexpr float kGlowStrength  = 0.55f;// overall glow brightness
constexpr float kGlowThreshold = 0.30f;// only pixels brighter than this emit a glow
constexpr float kGlowFalloff   = 1.5f; // light falloff with distance
constexpr float kGlowEdge      = 5.0f; // feather the kernel's outer edge to zero

// Quadrant of the radial point-spread function, indexed [|dx|][|dy|]. Shared,
// built once (kernel[0][0] stays ~1 so it doubles as the "ready" flag).
float g_glowKernel[kGlowRadius + 1][kGlowRadius + 1] = {};

// fractional / reverse-fractional part, for Wu anti-aliased lines.
inline float fpart(float x)  { return x - std::floor(x); }
inline float rfpart(float x) { return 1.0f - fpart(x); }

// magnitude of a transform's linear (scale) part.
inline float transformScale(const Matrix3x2& m)
{
	const float s = std::sqrt(m._11 * m._11 + m._12 * m._12);
	return (s > 0.0f) ? s : 1.0f;
}

void buildGlowKernel()
{
	const float invR = 1.0f / static_cast<float>(kGlowRadius);
	for (int x = 0; x <= kGlowRadius; ++x)
	{
		for (int y = 0; y <= kGlowRadius; ++y)
		{
			const float distance = 1.0f + kGlowFalloff * std::sqrt(static_cast<float>(x * x + y * y));
			float intensity = 1.0f / (distance * distance);
			const float normalizedRadius = distance * invR; // feather corners to zero at the edge
			intensity *= std::clamp((1.0f - normalizedRadius) * kGlowEdge, 0.0f, 1.0f);
			g_glowKernel[x][y] = intensity;
		}
	}
}
}

// Polyphonic blob pin: stores the raw per-voice capture buffer keyed by voice,
// and fires a per-voice callback. (The stock gmpi::editor::Pin<T> is monophonic.)
class PolyBlobPin final : public PinBase
{
public:
	std::vector<uint8_t> raw[kVoiceCount];
	std::function<void(int voice)> onVoiceUpdate;

	void setFromHost(int32_t voice, std::span<const uint8_t> data) override
	{
		if (voice < 0 || voice >= kVoiceCount)
			return;

		raw[voice].assign(data.begin(), data.end());

		if (onVoiceUpdate)
			onVoiceUpdate(voice);
	}

	size_t byteSize(int voice) const { return raw[voice].size(); }
	const float* asFloats(int voice) const { return reinterpret_cast<const float*>(raw[voice].data()); }
};

class Scope4Gui final : public PluginEditor, public gmpi::TimerClient
{
	using clock = std::chrono::steady_clock;

	// pins (member order MUST match the XML <GUI> pin order)
	PolyBlobPin pinSamplesA;
	PolyBlobPin pinSamplesB;
	Pin<bool>   pinPolyMode;
	Pin<std::string> pinColorA;    // trace A colour, hex e.g. "00FF00"
	Pin<std::string> pinColorB;    // trace B colour
	Pin<std::string> pinColorAxis; // graticule lines + ticks
	Pin<std::string> pinColorText; // voltage value labels
	Pin<float>       pinVelocityDim; // 0 = uniform brightness, 1 = full CRT velocity grading

	// colour pins parsed to (linear) colours, refreshed on pin change.
	RgbF  traceColorA_ = kLitGreen;
	RgbF  traceColorB_ = kLitYellow;
	Color axisColor_   = kDefaultAxis;
	Color textColor_   = kDefaultText;

	TextFormat labelFormat_;
	bool       timerRunning_ = false;

	int newestVoice_ = 0;
	clock::time_point voiceLastUpdated_[kVoiceCount];
	float voiceStatus_[kVoiceCount] = {};
	clock::time_point latestUpdate_{}; // most recent capture across all voices (drives the fade timer)

	// Phosphor persistence, accumulated in LINEAR light with HDR headroom (values
	// may exceed 1.0):
	//   accum_   - the frame buffer; NOT cleared between frames. Each frame it is
	//              multiplied toward zero (fade-out).
	//   waveBuf_ - the incoming wave only; cleared and re-rasterized every frame,
	//              then additively blended into accum_ so overlapping/recently-swept
	//              pixels build past 1.0 and bloom (intensity-graded, like an analog
	//              scope) instead of clipping.
	// On composite the HDR buffer is tone-mapped (overflow spills to white), gamma
	// encoded and packed into a fresh 8-bit image. Plain CPU buffers because a
	// freshly created image realizes its GPU copy from the (just-modified) pixels,
	// whereas a reused bitmap would composite a stale cached copy.
	std::vector<RgbF>    accum_;    // persistence (decays over time)
	std::vector<RgbF>    waveBuf_;  // one trace being rasterized
	std::vector<RgbF>    glowBuf_;  // accum_ + emissive halo, rebuilt each composite
	std::vector<int32_t> dirty_;    // pixels touched while rasterizing one trace (for cheap clear/blend)
	int    bufW_ = 0, bufH_ = 0;    // buffer pixel dimensions
	clock::time_point lastDecay_{}; // for frame-rate-independent (time-based) decay

	float  autoGain_ = 1.0f;        // auto-brightness gain, applied at composite (display only)
	float  frameMax_ = 0.0f;        // brightest accumulated pixel touched this frame (drives the AGC)

	void redraw()
	{
		if (drawingHost)
			drawingHost->invalidateRect(&bounds);
	}

	void onValueChanged(int voiceId)
	{
		voiceLastUpdated_[voiceId] = latestUpdate_ = clock::now();

		const float beforeStatus = voiceStatus_[voiceId];
		if (pinSamplesA.byteSize(voiceId) == kCaptureBytes)
		{
			const float* capturedata = pinSamplesA.asFloats(voiceId);
			voiceStatus_[voiceId] = capturedata[SCOPE_BUFFER_SIZE - 1]; // last entry is voice-active
		}
		else
		{
			voiceStatus_[voiceId] = 0.0f;
		}

		if (beforeStatus != voiceStatus_[voiceId] && voiceStatus_[voiceId] > 0.0f)
			newestVoice_ = voiceId;

		redraw();
	}

	void onPolyModeChanged()
	{
		if (pinPolyMode.value == false)
			newestVoice_ = 0; // monophonic mode.

		redraw();
	}

	// Build the trace point list (one point per pixel on small scopes, one per
	// sample when there's room), reduce collinear points.
	void buildTracePoints(const float* capturedata, float mid_y, float scale, int width, std::vector<Point>& out)
	{
		std::vector<Point> pts;

		if (SCOPE_BUFFER_SIZE <= width)
		{
			// scope wider in pixels than samples: one point per sample.
			const float xinc = width / static_cast<float>(SCOPE_BUFFER_SIZE);
			pts.reserve(SCOPE_BUFFER_SIZE);
			for (int index = 0; index < SCOPE_BUFFER_SIZE; ++index)
			{
				const float y = capturedata[index];
				pts.push_back({ xinc * index, mid_y - y * scale });
			}
		}
		else
		{
			// more samples than pixels: one (interpolated) point per pixel.
			pts.reserve(width);
			float indexf = 0.0f;
			const float indexInc = (SCOPE_BUFFER_SIZE - 1) / static_cast<float>(width);
			for (int i = 0; i < width; ++i)
			{
				const int index = static_cast<int>(indexf);
				const float frac = indexf - index;
				const float y1 = capturedata[index];
				const float y2 = capturedata[index + 1];
				const float y = y1 + frac * (y2 - y1);
				pts.push_back({ static_cast<float>(i), mid_y - y * scale });
				indexf += indexInc;
			}
		}

		SimplifyGraph(pts, out);
	}

	// ── CPU rasterization into the per-frame wave buffer ──────────────────────
	// Plot one pixel at the given coverage (0..1). MAX-combine within a trace so a
	// pixel touched by two segments (e.g. at a vertex) takes the fuller coverage
	// rather than doubling or being overwritten by a partial edge sample.
	void plot(int x, int y, const RgbF& color, float cover)
	{
		if (cover <= 0.0f || x < 0 || x >= bufW_ || y < 0 || y >= bufH_)
			return;

		const int idx = y * bufW_ + x;
		RgbF& w = waveBuf_[idx];
		w.r = (std::max)(w.r, color.r * cover);
		w.g = (std::max)(w.g, color.g * cover);
		w.b = (std::max)(w.b, color.b * cover);
		dirty_.push_back(idx);
	}

	// Xiaolin Wu anti-aliased line: end/edge pixels get fractional coverage so
	// diagonals don't stair-step.
	void rasterizeLine(float x0, float y0, float x1, float y1, const RgbF& color)
	{
		const bool steep = std::abs(y1 - y0) > std::abs(x1 - x0);
		if (steep) { std::swap(x0, y0); std::swap(x1, y1); }
		if (x0 > x1) { std::swap(x0, x1); std::swap(y0, y1); }

		const float dx = x1 - x0;
		const float dy = y1 - y0;
		const float gradient = (dx == 0.0f) ? 1.0f : dy / dx;

		// In steep mode the major axis is y, so (major, minor) maps to (y, x).
		auto put = [&](int major, int minor, float cover)
		{
			if (steep) plot(minor, major, color, cover);
			else       plot(major, minor, color, cover);
		};

		// first endpoint
		float xend = std::round(x0);
		float yend = y0 + gradient * (xend - x0);
		float xgap = rfpart(x0 + 0.5f);
		const int xpxl1 = static_cast<int>(xend);
		const int ypxl1 = static_cast<int>(std::floor(yend));
		put(xpxl1, ypxl1,     rfpart(yend) * xgap);
		put(xpxl1, ypxl1 + 1, fpart(yend)  * xgap);
		float intery = yend + gradient;

		// second endpoint
		xend = std::round(x1);
		yend = y1 + gradient * (xend - x1);
		xgap = fpart(x1 + 0.5f);
		const int xpxl2 = static_cast<int>(xend);
		const int ypxl2 = static_cast<int>(std::floor(yend));
		put(xpxl2, ypxl2,     rfpart(yend) * xgap);
		put(xpxl2, ypxl2 + 1, fpart(yend)  * xgap);

		// span
		for (int x = xpxl1 + 1; x < xpxl2; ++x)
		{
			const int iy = static_cast<int>(std::floor(intery));
			put(x, iy,     rfpart(intery));
			put(x, iy + 1, fpart(intery));
			intery += gradient;
		}
	}

	void rasterizeTrace(const float* capturedata, const RgbF& color, float mid_y, float scale, int width)
	{
		if (width < 1)
			return;

		std::vector<Point> pts;
		buildTracePoints(capturedata, mid_y, scale, width, pts);

		for (size_t i = 1; i < pts.size(); ++i)
		{
			// Beam-velocity intensity grading: the horizontal sweep runs at constant
			// speed, so each segment is traced in a fixed time. A steep (fast-vertical)
			// segment spreads that energy over more screen length, so per-pixel it is
			// dimmer ∝ horizontalExtent / length = cos(slope) — like a real CRT.
			const float amt = std::clamp(pinVelocityDim.value, 0.0f, 1.0f);
			const float dx = std::abs(pts[i].x - pts[i - 1].x);
			const float dy = std::abs(pts[i].y - pts[i - 1].y);
			const float len = std::sqrt(dx * dx + dy * dy);
			const float speed = (len > 0.0f) ? dx / len : 0.0f;
			// Map cos(slope) through [floor..1] so a steep/vertical segment is dimmed
			// but never below kVelocityFloor — it stays a continuous line instead of
			// disappearing and leaving gaps. amt blends from uniform (0) to graded (1),
			// which keeps the grading smooth (steep < sloped < flat) at every amount.
			const float graded = kVelocityFloor + (1.0f - kVelocityFloor) * speed;
			const float intensity = (1.0f - amt) + amt * graded;
			rasterizeLine(pts[i - 1].x, pts[i - 1].y, pts[i].x, pts[i].y, color * intensity);
		}
	}

	// Physical pixels per logical unit: the host's DPI rasterization scale times any
	// extra scale in the current transform (e.g. panel zoom).
	float getDeviceScale(Graphics& g)
	{
		const float dpiScale = drawingHost.get() ? drawingHost->getRasterizationScale() : 1.0f;
		return dpiScale * transformScale(g.getTransform());
	}

	// Parse a hex colour pin (e.g. "00FF00" or "AARRGGBB"); blank → fallback.
	static RgbF rgbFromHex(const std::string& s, RgbF fallback)
	{
		if (s.empty())
			return fallback;
		const Color c = colorFromHexString(s);
		return RgbF{ c.r, c.g, c.b };
	}
	static Color colorFromHexOr(const std::string& s, Color fallback)
	{
		return s.empty() ? fallback : colorFromHexString(s);
	}

	// Black background, centre cross, voltage ticks and labels. Drawn directly
	// each frame: cheap (a handful of lines + 5 labels), redrawn at most ~10 Hz,
	// and avoids the DPI pitfalls of a cached compatible-render-target bitmap.
	void drawGraticule(Graphics& g, float width, float height, float mid_y, float scale)
	{
		const float mid_x = std::floor(0.5f + width * 0.5f);

		// solid black background
		auto backgroundBrush = g.createSolidColorBrush(Colors::Black);
		g.fillRectangle({ 0.0f, 0.0f, width, height }, backgroundBrush);

		// axis lines + ticks in the axis colour, value labels in the text colour.
		auto brush = g.createSolidColorBrush(axisColor_);

		const float penWidth = 1.0f;

		// centre lines
		g.drawLine({ 0.0f, mid_y + kSnapToPixel }, { width, mid_y + kSnapToPixel }, brush, penWidth);
		g.drawLine({ mid_x + kSnapToPixel, 0.0f }, { mid_x + kSnapToPixel, height }, brush, penWidth);

		// voltage ticks
		const int step = (height < 50) ? 4 : 1;
		for (int v = -10; v < 11; v += step)
		{
			const float y = kSnapToPixel + std::floor(v * scale * 0.1f);
			const float tick_width = (v % 5 == 0) ? 4.0f : 2.0f;
			g.drawLine({ mid_x - tick_width, mid_y + y }, { mid_x + tick_width, mid_y + y }, brush, penWidth);
		}

		// labels
		if (height > 30 && labelFormat_)
		{
			brush.setColor(textColor_);

			const auto metrics = labelFormat_.getFontMetrics();
			const float fontBoxSize = calcBodyHeight(metrics);
			const float yOffset = metrics.capHeight * 0.5f - metrics.ascent;

			for (int v = -10; v < 11; v += 5)
			{
				char txt[10];
				const float y = v * scale / 10.0f;
				snprintf(txt, std::size(txt), "%2.0f", static_cast<float>(v));

				const float tx = mid_x + 4.0f;
				const float ty = mid_y - static_cast<int>(y) + yOffset;
				const Rect textRect{ tx, ty, tx + 100.0f, ty + fontBoxSize };
				g.drawTextU(txt, labelFormat_, textRect, brush);
			}
		}
	}

	// Rasterize ONE trace into the (otherwise-clean) wave buffer, then additively
	// blend it into the HDR frame buffer at `gain`. Blending each trace separately
	// is what lets overlapping traces (e.g. the same signal into A and B) SUM and
	// bloom — a shared wave buffer would just have the last-drawn trace overwrite
	// the others. The dirty list keeps clear+blend O(trace length), not O(pixels).
	void blendTrace(const float* data, const RgbF& color, float mid_y, float scale, float gain)
	{
		dirty_.clear();
		rasterizeTrace(data, color, mid_y, scale, bufW_);

		for (int32_t idx : dirty_)
		{
			RgbF& w = waveBuf_[idx];
			if (w.r != 0.0f || w.g != 0.0f || w.b != 0.0f)
			{
				RgbF& a = accum_[idx];
				a.r += w.r * gain;
				a.g += w.g * gain;
				a.b += w.b * gain;
				// brightest accumulated pixel touched this frame → drives the auto-gain.
				frameMax_ = (std::max)(frameMax_, (std::max)(a.r, (std::max)(a.g, a.b)));
				w = RgbF{}; // clear for the next trace (and dedup repeated dirty entries)
			}
		}
	}

	// Blend every currently-live trace into the frame buffer. A voice is "live"
	// while it has been updated within kLiveWindowSec; once it goes stale it stops
	// being redrawn and the decay fades it away.
	void blendLiveTraces(clock::time_point showUpdatesAfter, float mid_y, float scale, float gain)
	{
		if (pinPolyMode.value == true)
		{
			// non-main voices drawn dim - 'B' trace then 'A' trace.
			for (int voice = 0; voice < kVoiceCount; ++voice)
				if (voiceLastUpdated_[voice] > showUpdatesAfter
					&& voice != newestVoice_ && pinSamplesB.byteSize(voice) == kCaptureBytes)
					blendTrace(pinSamplesB.asFloats(voice), traceColorB_ * kPolyDim, mid_y, scale, gain);

			for (int voice = 0; voice < kVoiceCount; ++voice)
				if (voiceLastUpdated_[voice] > showUpdatesAfter
					&& voice != newestVoice_ && pinSamplesA.byteSize(voice) == kCaptureBytes)
					blendTrace(pinSamplesA.asFloats(voice), traceColorA_ * kPolyDim, mid_y, scale, gain);
		}

		// main trace drawn bright.
		if (newestVoice_ >= 0 && voiceLastUpdated_[newestVoice_] > showUpdatesAfter)
		{
			if (pinSamplesB.byteSize(newestVoice_) == kCaptureBytes)
				blendTrace(pinSamplesB.asFloats(newestVoice_), traceColorB_, mid_y, scale, gain);

			if (pinSamplesA.byteSize(newestVoice_) == kCaptureBytes)
				blendTrace(pinSamplesA.asFloats(newestVoice_), traceColorA_, mid_y, scale, gain);
		}
	}

	// (Re)allocate the accumulation buffers when first drawn, resized, or the device
	// scale (DPI / panel zoom) changes. Sized at physical resolution, then halved
	// until both dimensions fit kMaxBufferDim.
	void ensureAccumBuffer(float width, float height, float deviceScale)
	{
		const float physW = width * deviceScale;
		const float physH = height * deviceScale;
		int ds = 1;
		while ((std::max)(physW, physH) / static_cast<float>(ds) > kMaxBufferDim)
			ds *= 2;

		const int w = (std::max)(1, static_cast<int>(physW / ds + 0.5f));
		const int h = (std::max)(1, static_cast<int>(physH / ds + 0.5f));
		if (bufW_ == w && bufH_ == h && !accum_.empty())
			return;

		bufW_ = w;
		bufH_ = h;
		accum_.assign(static_cast<size_t>(w) * h, RgbF{});
		waveBuf_.assign(static_cast<size_t>(w) * h, RgbF{});
		glowBuf_.assign(static_cast<size_t>(w) * h, RgbF{});
		dirty_.reserve(static_cast<size_t>(w) + h);
		lastDecay_ = clock::now();
	}

	// One frame: fade the frame buffer toward zero, then additively blend each live
	// trace into it. All in linear light with HDR headroom — overlaps build past
	// 1.0 rather than clip.
	void updatePersistence(clock::time_point now, clock::time_point showUpdatesAfter, float mid_y, float scale)
	{
		// clamp dt: avoids a degenerate first frame (gain → 0 when dt ≈ 0) and a
		// huge fade/gain jump when returning from a long idle.
		const float dt = std::clamp(std::chrono::duration<float>(now - lastDecay_).count(), 0.001f, 0.1f);
		lastDecay_ = now;

		// fade: multiply every frame-buffer pixel toward zero.
		const float decay = std::exp(-dt / kFadeTau);
		for (RgbF& p : accum_)
		{
			p.r *= decay;
			p.g *= decay;
			p.b *= decay;
		}

		// additively blend each live trace. The gain is scaled by (1 - decay) so a
		// steadily-swept pixel settles to kWaveIntensity independent of frame rate;
		// pixels swept by overlapping traces build higher and bloom.
		const float gain = kWaveIntensity * (1.0f - decay);
		frameMax_ = 0.0f;
		blendLiveTraces(showUpdatesAfter, mid_y, scale, gain);

		// Auto-brightness: a periodic trace integrates up to kWaveIntensity over many
		// frames, but an aperiodic/random trace hits each pixel just once and stays at
		// the dim single-hit level. Normalize the brightest pixel touched this frame
		// toward the target so both read equally bright. Smoothed (slow rise / fast
		// fall) so it ramps gently and never flashes over-bright. Updated only while a
		// trace is live (frameMax_ > floor); otherwise the gain is HELD so a stale
		// trace's trail fades out normally — only the build-up (fade-in) is compensated.
		if (frameMax_ > kAutoGainFloor)
		{
			const float rawGain = std::clamp(kAutoGainTarget / frameMax_, 1.0f, kAutoGainMax);
			const float tau = (rawGain < autoGain_) ? kAutoGainTauDown : kAutoGainTauUp;
			autoGain_ += (rawGain - autoGain_) * (1.0f - std::exp(-dt / tau));
		}
	}

	// Tone-map one linear-HDR pixel to a premultiplied-sRGB BGRA word. Per Jeff's
	// EmmissiveComponent: a channel over 1.0 is clamped and SPILLS half its excess
	// into the other channels, so an over-bright spot warms toward white gradually
	// (a single bright channel stays mostly its own colour; only a very over-bright
	// pixel goes white). Alpha = coverage, so faint glow is translucent (the
	// graticule shows through) and bright traces are opaque.
	static uint32_t toneMapPixel(const RgbF& c)
	{
		float comp[3] = { c.r, c.g, c.b };

		float spill = 0.0f;
		for (float& v : comp)
			if (v > 1.0f) { spill += v - 1.0f; v = 1.0f; }
		if (spill > 0.0f)
			for (float& v : comp)
				v = (std::min)(1.0f, v + spill * 0.5f);

		const float alpha = (std::max)(comp[0], (std::max)(comp[1], comp[2]));
		const uint32_t a8 = static_cast<uint32_t>(alpha * 255.0f + 0.5f);
		// premultiplied sRGB bytes.
		const uint32_t r8 = static_cast<uint32_t>(linearPixelToSRGB(comp[0]) * alpha + 0.5f);
		const uint32_t g8 = static_cast<uint32_t>(linearPixelToSRGB(comp[1]) * alpha + 0.5f);
		const uint32_t b8 = static_cast<uint32_t>(linearPixelToSRGB(comp[2]) * alpha + 0.5f);
		return b8 | (g8 << 8) | (r8 << 16) | (a8 << 24);
	}

	// Emissive glow: copy the accumulation, then for every bright pixel splat its
	// light over the surrounding kernel (inverse-square falloff) so the trace
	// radiates a halo, exactly like a real light source. All in linear light.
	void applyGlow()
	{
		if (g_glowKernel[0][0] == 0.0f)
			buildGlowKernel();

		// Base: each pixel's own light, scaled by the auto-brightness gain so an
		// aperiodic trace is lifted to the same displayed level as a periodic one. The
		// gain is applied here at composite time only — accum_ itself stays un-gained so
		// it keeps integrating (and decaying) correctly frame to frame.
		const float g = autoGain_;
		for (size_t i = 0; i < accum_.size(); ++i)
			glowBuf_[i] = accum_[i] * g;

		for (int y = 0; y < bufH_; ++y)
		{
			const RgbF* row = accum_.data() + static_cast<size_t>(y) * bufW_;
			for (int x = 0; x < bufW_; ++x)
			{
				const RgbF src{ row[x].r * g, row[x].g * g, row[x].b * g };
				if (src.r <= kGlowThreshold && src.g <= kGlowThreshold && src.b <= kGlowThreshold)
					continue; // too dim to emit

				const RgbF emit{ src.r * kGlowStrength, src.g * kGlowStrength, src.b * kGlowStrength };

				const int x0 = (std::max)(0, x - kGlowRadius);
				const int x1 = (std::min)(bufW_ - 1, x + kGlowRadius);
				const int y0 = (std::max)(0, y - kGlowRadius);
				const int y1 = (std::min)(bufH_ - 1, y + kGlowRadius);
				for (int ny = y0; ny <= y1; ++ny)
				{
					RgbF* drow = glowBuf_.data() + static_cast<size_t>(ny) * bufW_;
					const float* krow = g_glowKernel[std::abs(ny - y)];
					for (int nx = x0; nx <= x1; ++nx)
					{
						if (nx == x && ny == y)
							continue; // centre is already the pixel's own light
						const float w = krow[std::abs(nx - x)];
						if (w <= 0.0f)
							continue;
						RgbF& d = drow[nx];
						d.r += emit.r * w;
						d.g += emit.g * w;
						d.b += emit.b * w;
					}
				}
			}
		}
	}

	// Build the emissive glow, tone-map it into a fresh image and composite over the
	// graticule. A new image each frame forces the GPU copy to be re-created from
	// the just-written pixels (a reused bitmap would draw a stale cached copy).
	void compositeAccum(Graphics& g, const Rect& r)
	{
		applyGlow();

		auto img = g.getFactory().createImage(bufW_, bufH_,
			static_cast<int32_t>(BitmapRenderTargetFlags::SRGBPixels));
		{
			auto px = img.lockPixels(BitmapLockFlags::Write);
			if (!px)
				return;
			uint8_t* base = px.getAddress();
			const int stride = px.getBytesPerRow();
			for (int y = 0; y < bufH_; ++y)
			{
				uint32_t* dst = reinterpret_cast<uint32_t*>(base + static_cast<size_t>(y) * stride);
				const RgbF* src = glowBuf_.data() + static_cast<size_t>(y) * bufW_;
				for (int x = 0; x < bufW_; ++x)
					dst[x] = toneMapPixel(src[x]);
			}
		}
		g.drawBitmap(img, r, Rect{ 0.0f, 0.0f, static_cast<float>(bufW_), static_cast<float>(bufH_) });
	}

public:
	Scope4Gui()
	{
		pinSamplesA.onVoiceUpdate = [this](int v) { onValueChanged(v); };
		pinSamplesB.onVoiceUpdate = [this](int v) { onValueChanged(v); };
		pinPolyMode.onUpdate = [this](PinBase*) { onPolyModeChanged(); };

		pinColorA.onUpdate    = [this](PinBase*) { traceColorA_ = rgbFromHex(pinColorA.value, kLitGreen);       redraw(); };
		pinColorB.onUpdate    = [this](PinBase*) { traceColorB_ = rgbFromHex(pinColorB.value, kLitYellow);      redraw(); };
		pinColorAxis.onUpdate = [this](PinBase*) { axisColor_   = colorFromHexOr(pinColorAxis.value, kDefaultAxis); redraw(); };
		pinColorText.onUpdate = [this](PinBase*) { textColor_   = colorFromHexOr(pinColorText.value, kDefaultText); redraw(); };
		pinVelocityDim.onUpdate = [this](PinBase*) { redraw(); };

		const auto now = clock::now();
		std::fill(std::begin(voiceLastUpdated_), std::end(voiceLastUpdated_), now);
	}

	ReturnCode measure(const Size* availableSize, Size* returnDesiredSize) override
	{
		const float minSize = 15.0f;
		returnDesiredSize->width = (std::max)(minSize, availableSize->width);
		returnDesiredSize->height = (std::max)(minSize, availableSize->height);
		return ReturnCode::Ok;
	}

	bool onTimer() override
	{
		redraw();
		return true;
	}

	ReturnCode render(drawing::api::IDeviceContext* drawingContext) override
	{
		Graphics g(drawingContext);

		const Rect r = bounds; // {0, 0, width, height}
		const float width = getWidth(r);
		const float height = getHeight(r);

		const float scale = height * 0.46f;
		const float mid_y = std::floor(0.5f + height * 0.5f);

		const auto now = clock::now();
		// a trace is "live" (redrawn at full brightness) while updated this recently.
		const auto showUpdatesAfter = now - std::chrono::duration_cast<clock::duration>(
			std::chrono::duration<float>(kLiveWindowSec));

		ClipDrawingToBounds clip(g, r);

		if (!labelFormat_)
		{
			std::vector<std::string_view> fam = { "Arial" };
			labelFormat_ = g.getFactory().createTextFormat(11.0f, fam);
			labelFormat_.setTextAlignment(TextAlignment::Leading);
			labelFormat_.setParagraphAlignment(ParagraphAlignment::Center);
			labelFormat_.setWordWrapping(WordWrapping::NoWrap);
		}

		// 1. sharp graticule background, drawn directly in logical coordinates.
		drawGraticule(g, width, height, mid_y, scale);

		// 2. size the (physical-resolution) trace buffer for the current device
		//    scale, then fade it and blend the current traces in BUFFER coordinates.
		const float deviceScale = getDeviceScale(g);
		ensureAccumBuffer(width, height, deviceScale);

		const float bufScale = bufH_ * 0.46f;
		const float bufMidY  = std::floor(0.5f + bufH_ * 0.5f);
		updatePersistence(now, showUpdatesAfter, bufMidY, bufScale);

		// 3. composite the accumulated (fading) traces over the graticule.
		compositeAccum(g, r);

		// 4. keep the fade animation running until the traces have decayed away.
		const bool keepAnimating = std::chrono::duration<float>(now - latestUpdate_).count() < kAnimateWindowSec;
		if (keepAnimating)
		{
			if (!timerRunning_)
			{
				startTimer(kTimerMs);
				timerRunning_ = true;
			}
		}
		else if (timerRunning_)
		{
			timerRunning_ = false;
			stopTimer();
		}

		return ReturnCode::Ok;
	}
};

namespace
{
// Both plugins share the Scope4Gui editor; each binds to its own DSP class
// (registered by id in Scope4.cpp) via the matching <Audio> section.
auto rScope4 = gmpi::Register<Scope4Gui>::withXml(R"XML(
<?xml version="1.0" encoding="utf-8" ?>
<Plugin id="SE Scope4" name="Scope4" category="Controls" graphicsApi="GmpiGui">
    <Parameters>
        <Parameter id="0" name="Capture Data A" datatype="blob" private="true" ignorePatchChange="true" isPolyphonic="true" persistant="false"/>
        <Parameter id="1" name="Capture Data B" datatype="blob" private="true" ignorePatchChange="true" isPolyphonic="true" persistant="false"/>
        <Parameter id="2" name="polyDetect" datatype="bool" private="true" ignorePatchChange="true" persistant="false"/>
    </Parameters>
    <Audio>
        <Pin name="Signal A" datatype="float" rate="audio"/>
        <Pin name="Signal B" datatype="float" rate="audio"/>
        <Pin name="VoiceActive" hostConnect="Voice/Active" datatype="float" isPolyphonic="true"/>
        <Pin name="Capture Data A" direction="out" datatype="blob" parameterId="0" private="true" isPolyphonic="true"/>
        <Pin name="Capture Data B" direction="out" datatype="blob" parameterId="1" private="true" isPolyphonic="true"/>
        <Pin name="polydetect" direction="out" datatype="bool" parameterId="2"/>
    </Audio>
    <GUI graphicsApi="GmpiGui">
        <Pin name="Capture Data A" datatype="blob" parameterId="0" private="true" isPolyphonic="true"/>
        <Pin name="Capture Data B" datatype="blob" parameterId="1" private="true" isPolyphonic="true"/>
        <Pin name="polydetect" datatype="bool" parameterId="2"/>
        <Pin name="Trace A Color" datatype="string" default="00FF00"/>
        <Pin name="Trace B Color" datatype="string" default="FFFF00"/>
        <Pin name="Axis Color" datatype="string" default="8F958F"/>
        <Pin name="Value Color" datatype="string" default="C4CBC4"/>
        <Pin name="Velocity Dim" datatype="float" default="0.7"/>
    </GUI>
</Plugin>
)XML");

auto rTrigScope4 = gmpi::Register<Scope4Gui>::withXml(R"XML(
<?xml version="1.0" encoding="utf-8" ?>
<Plugin id="SE TrigScope4" name="Triggered Scope4" category="Diagnostic" graphicsApi="GmpiGui">
    <Parameters>
        <Parameter id="0" name="Capture Data A" datatype="blob" private="true" ignorePatchChange="true" isPolyphonic="true" persistant="false"/>
        <Parameter id="1" name="Capture Data B" datatype="blob" private="true" ignorePatchChange="true" isPolyphonic="true" persistant="false"/>
        <Parameter id="2" name="polyDetect" datatype="bool" private="true" ignorePatchChange="true" persistant="false"/>
    </Parameters>
    <Audio>
        <Pin name="Signal A" datatype="float" rate="audio"/>
        <Pin name="Signal B" datatype="float" rate="audio"/>
        <Pin name="VoiceActive" hostConnect="Voice/Active" datatype="float" isPolyphonic="true"/>
        <Pin name="Capture Data A" direction="out" datatype="blob" parameterId="0" private="true" isPolyphonic="true"/>
        <Pin name="Capture Data B" direction="out" datatype="blob" parameterId="1" private="true" isPolyphonic="true"/>
        <Pin name="polydetect" direction="out" datatype="bool" parameterId="2"/>
        <Pin name="Trigger" datatype="float" rate="audio"/>
    </Audio>
    <GUI graphicsApi="GmpiGui">
        <Pin name="Capture Data A" datatype="blob" parameterId="0" private="true" isPolyphonic="true"/>
        <Pin name="Capture Data B" datatype="blob" parameterId="1" private="true" isPolyphonic="true"/>
        <Pin name="polydetect" datatype="bool" parameterId="2"/>
        <Pin name="Trace A Color" datatype="string" default="00FF00"/>
        <Pin name="Trace B Color" datatype="string" default="FFFF00"/>
        <Pin name="Axis Color" datatype="string" default="8F958F"/>
        <Pin name="Value Color" datatype="string" default="C4CBC4"/>
        <Pin name="Velocity Dim" datatype="float" default="0.7"/>
    </GUI>
</Plugin>
)XML");
}
