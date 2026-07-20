#include "WavetableOscGui.h"
#include "WavetableOsc.h"
#define _USE_MATH_DEFINES
#include <math.h>
#include <cstdlib>
#include <vector>
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

// Build the cached 3D landscape geometry: one outline per drawn slot, plus the black fill ribbon
// linking it to the next-nearer slot. Geometry is device-independent, so it survives across frames
// and device contexts - only the wavetable data and the widget size feed into it (never the
// selected-slot highlight, which is applied per-frame at draw time).
void WavetableOscGui::buildDisplayGeometry(gmpi::drawing::Graphics& g, float width, float height, WaveTable* waveTable)
{
	using namespace gmpi::drawing;

	slotGeometry_.clear();

	const float vscale = height * 0.25f;
	const float horizontalDelta = width / 3.0f;
	const float x_increment = (width - horizontalDelta) / (float) waveTable->waveSize;
	const float backYaxis = vscale * 0.5f;
	const float frontYaxis = height - backYaxis;

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

	// Pre-simplify each drawn slot's waveform once, thinning near-collinear points so the path
	// geometries carry far fewer vertices. Points are kept in slot-local coordinates
	// (SimplifyGraph is translation-invariant), so the same simplified set serves both the
	// slot's outline and the two ribbon edges it borders.
	const float endX = waveTable->waveSize * x_increment;
	std::vector<std::vector<Point>> simplifiedSlots( drawnSlots.size() );
	{
		std::vector<Point> raw;
		raw.reserve( waveTable->waveSize );
		for( size_t j = 0 ; j < drawnSlots.size() ; ++j )
		{
			const float* wavedata = waveTable->GetSlotPtr( drawnSlots[j] );
			raw.clear();
			for( int i = 0 ; i < waveTable->waveSize ; ++i )
				raw.push_back( Point( i * x_increment, wavedata[i] * -vscale ) );

			SimplifyGraph( raw, simplifiedSlots[j] );
		}
	}

	auto factory = g.getFactory();

	// Build back-to-front (highest slot index first), each entry carrying its outline plus the
	// fill ribbon toward the next-nearer slot, so drawing the entries in order reproduces the
	// original depth occlusion.
	slotGeometry_.reserve( drawnSlots.size() );
	for( int j = (int)drawnSlots.size() - 1 ; j >= 0 ; --j )
	{
		const int slot = drawnSlots[j];
		const std::vector<Point>& wave = simplifiedSlots[j];

		const float yOffset = frontYaxis - (frontYaxis-backYaxis) * ((float) slot / (float) waveTable->slotCount);
		const float xOffset = (slot * horizontalDelta) / waveTable->slotCount;

		SlotGeometry sg;
		sg.slot = slot;

		// Waveform outline.
		sg.outline = factory.createPathGeometry();
		{
			auto sink = sg.outline.open();
			sink.beginFigure(Point(xOffset, yOffset), FigureBegin::Hollow);
			for( const auto& p : wave )
				sink.addLine(Point(xOffset + p.x, yOffset + p.y));
			sink.addLine(Point(xOffset + endX, yOffset));
			sink.endFigure(FigureEnd::Open);
			sink.close();
		}

		// Fill polygon between this and the next drawn slot (toward the front).
		if( j > 0 )
		{
			const int slot2 = drawnSlots[j - 1];
			const std::vector<Point>& wave2 = simplifiedSlots[j - 1];
			const float yOffset2 = frontYaxis - (frontYaxis-backYaxis) * ((float) slot2 / (float) waveTable->slotCount);
			const float xOffset2 = (slot2 * horizontalDelta) / waveTable->slotCount;

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

	// Wavetable 3D display - always visible so the user can see the loaded shape even with no audio running.
	auto penLines       = g.createSolidColorBrush(colorFromHex(color_foreground));
	auto penHighlighted = g.createSolidColorBrush(colorFromHex(color_highlighted));
	auto fillBrush      = g.createSolidColorBrush(color_fill);

	// Slot pin (0..1) maps to one of the slotCount file slots; highlight whichever drawn slot lies
	// closest to it, so the highlight stays visible even when the exact slot isn't among those
	// drawn. This is the only per-frame decision - the geometry itself is cached.
	const float slotPos = std::min( 1.0f, std::max( 0.0f, pinSlot.value ) );
	const int highlightedSlot = (int)( slotPos * (float)( waveTable->slotCount - 1 ) + 0.5f );

	int highlightedSlotValue = slotGeometry_.empty() ? -1 : slotGeometry_.front().slot;
	for( const auto& sg : slotGeometry_ )
	{
		if( std::abs( sg.slot - highlightedSlot ) < std::abs( highlightedSlotValue - highlightedSlot ) )
			highlightedSlotValue = sg.slot;
	}

	// slotGeometry_ is stored back-to-front, each entry carrying the fill ribbon toward the
	// next-nearer slot, so drawing outline-then-fill in order reproduces the depth occlusion.
	for( auto& sg : slotGeometry_ )
	{
		auto& pen = ( sg.slot == highlightedSlotValue ) ? penHighlighted : penLines;
		g.drawGeometry( sg.outline, pen, 1.0f );

		if( sg.fill )
			g.fillGeometry( sg.fill, fillBrush );
	}

	return ReturnCode::Ok;
}
