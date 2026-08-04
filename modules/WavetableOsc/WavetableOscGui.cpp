#include "WavetableOscGui.h"
#include "WavetableOsc.h"
#define _USE_MATH_DEFINES
#include <math.h>
#include <cstdlib>
#include <vector>
#include <algorithm>
#include "helpers/SimplifyGraph.h"
#include "Extensions/EmbeddedFile.h"

using namespace gmpi;
using namespace gmpi::drawing;
using namespace std;

namespace {
bool registeredGui = gmpi::Register<WavetableOscGui>::withId("SE Wavetable Display");
}

WavetableOscGui::WavetableOscGui()
{
	pinWaveFiles.onUpdate = [this](editor::PinBase*) { updateCurrentWavetable(); };
	pinSlot.onUpdate      = [this](editor::PinBase*) { redraw(); };
}

// Resolve the filename and pull the shared raw wavetable from the process-wide cache. Only the
// raw form: the display draws the waveform itself, so it has no use for a mip bake and no
// sample rate to bake one at.
void WavetableOscGui::updateCurrentWavetable()
{
	string curWaveFile = pinWaveFiles.value;
	if( curWaveFile_ != curWaveFile )
	{
		curWaveFile_ = curWaveFile;
		currentWavetable_.reset();
		geometryDirty_ = true;

		if (builtinWavetableShape(curWaveFile_) >= 0)
		{
			// Builtin test wavetable - skip host resource resolution, the name is the cache key.
			currentWavetable_ = wavetableCache().getOrLoadRaw(curWaveFile_);
		}
		else if (auto synthEdit = drawingHost.as<synthedit::IEmbeddedFileSupport>())
		{
			ReturnString fullFilename;
			if (synthEdit->findResourceUri(curWaveFile_.c_str(), &fullFilename) == ReturnCode::Ok)
			{
				synthEdit->registerResourceUri(fullFilename.c_str());
				currentWavetable_ = wavetableCache().getOrLoadRaw(fullFilename.c_str());
			}
		}

		redraw();
	}
}

// Origin (baseline left-hand end) of one slot's waveform, from its depth within the landscape.
gmpi::drawing::Point WavetableOscGui::slotOrigin(int slot) const
{
	return gmpi::drawing::Point(
		(slot * layout_.horizontalDelta) / layout_.slotCount,
		layout_.frontYaxis - (layout_.frontYaxis - layout_.backYaxis) * ((float) slot / (float) layout_.slotCount)
	);
}

// Thin one slot's waveform to a polyline in slot-local coordinates, so the path geometries carry
// far fewer vertices than the raw wave.
void WavetableOscGui::simplifySlot(WaveTable* waveTable, int slot, std::vector<gmpi::drawing::Point>& simplified) const
{
	using namespace gmpi::drawing;

	const float* wavedata = waveTable->GetSlotPtr( slot );

	std::vector<Point> raw;
	raw.reserve( waveTable->waveSize );
	for( int i = 0 ; i < waveTable->waveSize ; ++i )
		raw.push_back( Point( i * layout_.x_increment, wavedata[i] * -layout_.vscale ) );

	SimplifyGraph( raw, simplified );
}

// Build one slot's waveform outline: the polyline, closed off along the baseline at each end.
gmpi::drawing::PathGeometry WavetableOscGui::buildSlotOutline(gmpi::drawing::Factory& factory, int slot, const std::vector<gmpi::drawing::Point>& wave) const
{
	using namespace gmpi::drawing;

	const Point origin = slotOrigin( slot );

	auto outline = factory.createPathGeometry();
	auto sink = outline.open();
	sink.beginFigure(origin, FigureBegin::Hollow);
	for( const auto& p : wave )
		sink.addLine(Point(origin.x + p.x, origin.y + p.y));
	sink.addLine(Point(origin.x + layout_.endX, origin.y));
	sink.endFigure(FigureEnd::Open);
	sink.close();

	return outline;
}

// Build the cached 3D landscape geometry: one outline per drawn slot, plus the black fill ribbon
// linking it to the next-nearer slot. Geometry is device-independent, so it survives across frames
// and device contexts - only the wavetable data and the widget size feed into it (never the
// selected-slot highlight, which is applied per-frame at draw time).
void WavetableOscGui::buildDisplayGeometry(gmpi::drawing::Graphics& g, float width, float height, WaveTable* waveTable)
{
	using namespace gmpi::drawing;

	slotGeometry_.clear();

	// The selected slot's outline is built against the old layout, so drop it along with the rest.
	selectedOutline_ = {};
	selectedOutlineSlot_ = -1;

	const float vscale = height * 0.25f;
	const float horizontalDelta = width / 3.0f;
	const float x_increment = (width - horizontalDelta) / (float) waveTable->waveSize;
	const float backYaxis = vscale * 0.5f;
	const float frontYaxis = height - backYaxis;

	layout_.vscale          = vscale;
	layout_.horizontalDelta = horizontalDelta;
	layout_.x_increment     = x_increment;
	layout_.backYaxis       = backYaxis;
	layout_.frontYaxis      = frontYaxis;
	layout_.endX            = waveTable->waveSize * x_increment;
	layout_.slotCount       = waveTable->slotCount;

	// Limit the number of drawn slots to keep the 3D graph readable. Pick evenly-spaced slots
	// across the full set, always including the first and last.
	const int maxDrawnSlots = 32;
	std::vector<int> drawnSlots;
	if( waveTable->slotCount <= maxDrawnSlots )
	{
		for( int slot = 0 ; slot < waveTable->slotCount ; ++slot )
			drawnSlots.push_back( slot );
	}
	else
	{
		for( int k = 0 ; k < maxDrawnSlots ; ++k )
		{
			const int slot = (int)( (float)k * (float)( waveTable->slotCount - 1 ) / (float)( maxDrawnSlots - 1 ) + 0.5f );
			if( drawnSlots.empty() || drawnSlots.back() != slot )
				drawnSlots.push_back( slot );
		}
	}

	// Pre-simplify each drawn slot's waveform once. The simplified set serves both the slot's
	// outline and the two ribbon edges it borders.
	const float endX = layout_.endX;
	std::vector<std::vector<Point>> simplifiedSlots( drawnSlots.size() );
	for( size_t j = 0 ; j < drawnSlots.size() ; ++j )
		simplifySlot( waveTable, drawnSlots[j], simplifiedSlots[j] );

	auto factory = g.getFactory();

	// Build back-to-front (highest slot index first), each entry carrying its outline plus the
	// fill ribbon toward the next-nearer slot, so drawing the entries in order reproduces the
	// original depth occlusion.
	slotGeometry_.reserve( drawnSlots.size() );
	for( int j = (int)drawnSlots.size() - 1 ; j >= 0 ; --j )
	{
		const int slot = drawnSlots[j];
		const std::vector<Point>& wave = simplifiedSlots[j];

		const Point origin = slotOrigin( slot );
		const float xOffset = origin.x;
		const float yOffset = origin.y;

		SlotGeometry sg;
		sg.slot = slot;

		// Waveform outline.
		sg.outline = buildSlotOutline( factory, slot, wave );

		// Fill polygon between this and the next drawn slot (toward the front).
		if( j > 0 )
		{
			const int slot2 = drawnSlots[j - 1];
			const std::vector<Point>& wave2 = simplifiedSlots[j - 1];
			const Point origin2 = slotOrigin( slot2 );
			const float xOffset2 = origin2.x;
			const float yOffset2 = origin2.y;

			sg.fill = factory.createPathGeometry();
			auto fillSink = sg.fill.open();

			fillSink.beginFigure(Point(xOffset, yOffset), FigureBegin::Filled);
			for( const auto& p : wave )
				fillSink.addLine(Point(xOffset + p.x, yOffset + p.y));
			fillSink.addLine(Point(xOffset + endX, yOffset));

			// Back along next slot.
			fillSink.addLine(Point(xOffset2 + endX, yOffset2));
			for( auto it = wave2.rbegin() ; it != wave2.rend() ; ++it )
				fillSink.addLine(Point(xOffset2 + it->x, yOffset2 + it->y));
			fillSink.addLine(Point(xOffset2, yOffset2));

			fillSink.endFigure(FigureEnd::Closed);
			fillSink.close();
		}

		slotGeometry_.push_back( std::move(sg) );
	}
}

ReturnCode WavetableOscGui::render(gmpi::drawing::api::IDeviceContext* dc)
{
	const int32_t color_background  = 0x2A3632; // dark gray
	const int32_t color_foreground  = 0x25E456; // green
	const int32_t color_highlighted = 0xF8F600; // yellow
	const auto color_fill = colorFromHex(color_foreground, 0.1f); // transparent green

	Graphics g(dc);
	ClipDrawingToBounds _(g, bounds);

	drawing::Rect r;
	getClipArea(&r);
	float width = getWidth(r);
	float height = getHeight(r);

	// Fill background.
	auto backgroundBrush = g.createSolidColorBrush(colorFromHex(color_background));
	g.fillRectangle(r, backgroundBrush);

	WaveTable* waveTable = currentWavetable();

	if(!waveTable)
		return ReturnCode::Ok;

	// The 3D landscape is expensive to tessellate but depends only on the wavetable and the widget
	// size, so cache the geometry and rebuild only when one of those changes.
	if( geometryDirty_ || width != geometryWidth_ || height != geometryHeight_ )
	{
		buildDisplayGeometry( g, width, height, waveTable );
		geometryDirty_  = false;
		geometryWidth_  = width;
		geometryHeight_ = height;
	}

	if( slotGeometry_.empty() ) // empty wavetable - nothing to draw, and no slot safe to index.
		return ReturnCode::Ok;

	// Wavetable 3D display - always visible so the user can see the loaded shape even with no audio running.
	auto penLines       = g.createSolidColorBrush(colorFromHex(color_foreground));
	auto penHighlighted = g.createSolidColorBrush(colorFromHex(color_highlighted));
	auto fillBrush      = g.createSolidColorBrush(color_fill);

	// Slot pin (0..1) maps to one of the slotCount file slots. Highlight that exact slot, even when
	// it isn't one of the drawn subset - in that case its outline is built on the fly (below) and
	// slotted into the back-to-front draw order at its own depth.
	const float slotPos = std::min( 1.0f, std::max( 0.0f, pinSlot.value ) );
	const int highlightedSlot = (int)( slotPos * (float)( waveTable->slotCount - 1 ) + 0.5f );

	const bool highlightIsDrawn = std::any_of(
		slotGeometry_.begin(), slotGeometry_.end(),
		[highlightedSlot](const SlotGeometry& sg) { return sg.slot == highlightedSlot; }
	);

	// Build (or reuse) the extra outline for a selected slot that isn't in the cached subset. Only
	// ever one such outline is kept - caching every slot swept past is what the drawn-subset limit
	// exists to avoid.
	if( !highlightIsDrawn && selectedOutlineSlot_ != highlightedSlot )
	{
		auto factory = g.getFactory();
		std::vector<Point> wave;
		simplifySlot( waveTable, highlightedSlot, wave );

		selectedOutline_ = buildSlotOutline( factory, highlightedSlot, wave );
		selectedOutlineSlot_ = highlightedSlot;
	}

	// slotGeometry_ is stored back-to-front, each entry carrying the fill ribbon toward the
	// next-nearer slot, so drawing outline-then-fill in order reproduces the depth occlusion.
	bool selectedPending = !highlightIsDrawn && selectedOutline_;
	for( auto& sg : slotGeometry_ )
	{
		// The selected slot sits between two drawn slots: draw it once the entry behind it (and its
		// ribbon, which would otherwise paint over it) is done, but before any nearer slot.
		if( selectedPending && sg.slot < highlightedSlot )
		{
			g.drawGeometry( selectedOutline_, penHighlighted, 1.0f );
			selectedPending = false;
		}

		auto& pen = ( sg.slot == highlightedSlot ) ? penHighlighted : penLines;
		g.drawGeometry( sg.outline, pen, 1.0f );

		if( sg.fill )
			g.fillGeometry( sg.fill, fillBrush );
	}

	// Frontmost of all (only reachable if the selection sits beyond the last drawn slot).
	if( selectedPending )
		g.drawGeometry( selectedOutline_, penHighlighted, 1.0f );

	return ReturnCode::Ok;
}
