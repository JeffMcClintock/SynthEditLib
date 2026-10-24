// SE Fluid: an animated, fluid-looking surface drawn by a GPU shader brush
// (gmpi_ui/GmpiApiShaders.h), the first module to use one. The shader is
// fluid.sh; shaders/compiled/shaders_fluid.cpp is its blob table.
//
// Where the host has no shader factory (the CPU backend, a DAW whose plugin
// GUI is not on the GPU path yet) it draws a slowly wandering two-colour
// gradient instead: recognisably the same control, cheap, never blank.
//
// SE_FLUID_DEBUG=1 reports each brush creation on stderr. Note the editor's
// breadcrumb thumbnail renders every module once through EditorScreenshot's
// CPU factory (no shader factory, fallback) before the real view does; that
// first fallback is expected, not a sign the GPU path is missing.

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include "helpers/GmpiPluginEditor.h"
#include "helpers/Timer.h"
#include "GmpiApiShaders.h"

using namespace gmpi;
using namespace gmpi::drawing;
using namespace gmpi::editor;

namespace gmpi::drawing::shaders
{
extern const drawing::api::ShaderBlob fluid_blobs[];
extern const int32_t fluid_blobCount;
}

namespace
{
constexpr int kFrameMs = 33;          // 30 Hz while animating
constexpr int kFrameMsEnergetic = 16; // 60 Hz while driven
const Color kDefaultA{ 0.012f, 0.047f, 0.25f, 1.0f }; // deep blue (linear)
const Color kDefaultB{ 0.95f, 0.22f, 0.02f, 1.0f };   // orange

Color colorFromHexOr(const std::string& s, Color fallback)
{
	return s.empty() ? fallback : colorFromHexString(s);
}
} // namespace

class FluidGui final : public PluginEditor, public gmpi::TimerClient
{
	using clock = std::chrono::steady_clock;

	// pins (member order MUST match the XML <GUI> pin order)
	Pin<float>       pinSpeed;
	Pin<float>       pinScale;
	Pin<float>       pinTurbulence;
	Pin<float>       pinEnergy;
	Pin<std::string> pinColorA;
	Pin<std::string> pinColorB;
	Pin<float>       pinOpacity;

	Color colorA_ = kDefaultA;
	Color colorB_ = kDefaultB;

	// The brush is immutable, so a pin change means a new one; the host
	// shares the compiled program between them, so that is a small object.
	drawing::api::IShaderBrush* brush_{};
	Rect brushBounds_{};             // the bounds brush_ was made for (its brush space maps them to 0..1)
	bool brushStale_ = true;
	bool probed_ = false;            // whether we asked the factory for IShaderFactory yet
	bool haveShaderFactory_ = false;

	bool timerRunning_ = false;
	int  timerMs_ = 0;
	const clock::time_point start_ = clock::now();

	void redraw()
	{
		if (drawingHost)
			drawingHost->invalidateRect(&bounds);
	}

	void onParamsChanged()
	{
		brushStale_ = true;
		redraw();
	}

	void releaseBrush()
	{
		if (brush_)
			brush_->release();
		brush_ = nullptr;
	}

	// Build brush_ for `r` from the pins, or leave it null where the backend
	// has no shader support (then the gradient fallback paints).
	void rebuildBrush(Graphics& g, const Rect& r)
	{
		releaseBrush();
		brushStale_ = false;
		brushBounds_ = r;

		auto factory = g.getFactory();
		drawing::api::IShaderFactory* shaderFactory{};
		AccessPtr::get(factory)->queryInterface(&drawing::api::IShaderFactory::guid, reinterpret_cast<void**>(&shaderFactory));
		probed_ = true;
		haveShaderFactory_ = shaderFactory != nullptr;
		if (!shaderFactory)
		{
			if (std::getenv("SE_FLUID_DEBUG"))
				std::fprintf(stderr, "[SE Fluid] no IShaderFactory on this context's factory: gradient fallback\n");
			return;
		}

		float params[4 * 4] = {
			pinSpeed.value, pinScale.value, pinTurbulence.value, pinEnergy.value,
			colorA_.r, colorA_.g, colorA_.b, 1.0f,
			colorB_.r, colorB_.g, colorB_.b, 1.0f,
			pinOpacity.value, 0.0f, 0.0f, 0.0f,
		};

		// Brush space 0..1 across the control: brush -> local is scale then
		// translate (row-vector composition).
		BrushProperties props;
		props.transform = makeScale(Size{ (std::max)(1.0f, getWidth(r)), (std::max)(1.0f, getHeight(r)) })
		                * makeTranslation(Size{ r.left, r.top });

		const auto rc = shaderFactory->createShaderBrush(shaders::fluid_blobs, shaders::fluid_blobCount, params, 4, &colorA_, &props, &brush_);
		shaderFactory->release();
		if (std::getenv("SE_FLUID_DEBUG"))
			std::fprintf(stderr, "[SE Fluid] createShaderBrush -> %d, brush %p, bounds %g,%g-%g,%g\n", int(rc), (void*)brush_, r.left, r.top, r.right, r.bottom);
	}

	// No shader: two colours, the boundary drifting on the host's clock.
	void renderFallback(Graphics& g, const Rect& r)
	{
		const float t = std::chrono::duration<float>(clock::now() - start_).count() * (std::max)(0.0f, pinSpeed.value) * 0.5f;
		const float cx = r.left + getWidth(r) * (0.5f + 0.35f * std::sin(t));
		const float cy = r.top + getHeight(r) * (0.5f + 0.35f * std::sin(t * 0.7f + 1.0f));
		auto a = colorA_;
		auto b = colorB_;
		a.a = b.a = (std::clamp)(pinOpacity.value, 0.0f, 1.0f);
		auto brush = g.createLinearGradientBrush({ cx, cy }, { cx + getWidth(r) * 0.6f, cy + getHeight(r) * 0.6f }, b, a);
		g.fillRectangle(r, brush);
	}

	void updateTimer()
	{
		const bool animate = pinSpeed.value > 0.0f || pinEnergy.value > 0.0f;
		const int wantMs = pinEnergy.value > 0.0f ? kFrameMsEnergetic : kFrameMs;
		if (animate && (!timerRunning_ || timerMs_ != wantMs))
		{
			if (timerRunning_)
				stopTimer();
			startTimer(wantMs);
			timerRunning_ = true;
			timerMs_ = wantMs;
		}
		else if (!animate && timerRunning_)
		{
			stopTimer();
			timerRunning_ = false;
		}
	}

public:
	FluidGui()
	{
		pinSpeed.onUpdate      = [this](PinBase*) { onParamsChanged(); };
		pinScale.onUpdate      = [this](PinBase*) { onParamsChanged(); };
		pinTurbulence.onUpdate = [this](PinBase*) { onParamsChanged(); };
		pinEnergy.onUpdate     = [this](PinBase*) { onParamsChanged(); };
		pinOpacity.onUpdate    = [this](PinBase*) { onParamsChanged(); };
		pinColorA.onUpdate     = [this](PinBase*) { colorA_ = colorFromHexOr(pinColorA.value, kDefaultA); onParamsChanged(); };
		pinColorB.onUpdate     = [this](PinBase*) { colorB_ = colorFromHexOr(pinColorB.value, kDefaultB); onParamsChanged(); };
	}

	~FluidGui()
	{
		releaseBrush();
	}

	ReturnCode measure(const Size* availableSize, Size* returnDesiredSize) override
	{
		const float minSize = 16.0f;
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
		const Rect r = bounds;

		if (brushStale_ || !(brushBounds_ == r))
			rebuildBrush(g, r);

		if (brush_)
			drawingContext->fillRectangle(&r, brush_); // the shader paints; time comes from the host
		else
			renderFallback(g, r);

		updateTimer();
		return ReturnCode::Ok;
	}
};

namespace
{
auto rFluid = gmpi::Register<FluidGui>::withXml(R"XML(
<?xml version="1.0" encoding="utf-8" ?>
<Plugin id="SE Fluid" name="Fluid" category="Controls" graphicsApi="GmpiGui">
    <GUI graphicsApi="GmpiGui">
        <Pin name="Speed" datatype="float" default="1.0"/>
        <Pin name="Scale" datatype="float" default="3.0"/>
        <Pin name="Turbulence" datatype="float" default="1.0"/>
        <Pin name="Energy" datatype="float" default="0.0"/>
        <Pin name="Colour A" datatype="string" default="03103F"/>
        <Pin name="Colour B" datatype="string" default="FC8020"/>
        <Pin name="Opacity" datatype="float" default="1.0"/>
    </GUI>
</Plugin>
)XML");
}
