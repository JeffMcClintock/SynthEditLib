#include "WavetableOscGui.h"
#include "WavetableOsc.h"
#define _USE_MATH_DEFINES
#include <math.h>
#include <cstdlib>
#include <vector>
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

	float vscale = height * 0.25f;

	// Fill background.
	auto backgroundBrush = g.createSolidColorBrush(colorFromHex(color_background));
	g.fillRectangle(r, backgroundBrush);

	WaveTable* waveTable = currentWavetable();

	if(!waveTable)
		return ReturnCode::Ok;

	// Wavetable 3D display - always visible so the user can see the loaded shape even with no audio running.
	{
		auto penLines = g.createSolidColorBrush(colorFromHex(color_foreground));
		auto penHighlightedFirst = g.createSolidColorBrush(colorFromHex(color_highlighted));
		auto blackBrush = g.createSolidColorBrush(color_fill);

		float horizontalDelta = width / 3.0f;
		float x_increment = (width - horizontalDelta) / (float) waveTable->waveSize;
		float backYaxis = vscale * 0.5f;
		float frontYaxis = height - backYaxis;

		// Slot pin (0..1) maps to one of the slotCount file slots; that slot's waveform draws in highlight color.
		const float slotPos = std::min( 1.0f, std::max( 0.0f, pinSlot.value ) );
		const int highlightedSlot = (int)( slotPos * (float)( waveTable->slotCount - 1 ) + 0.5f );

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

		// Highlight whichever drawn slot lies closest to the selected slot, so the highlight
		// stays visible even when the exact slot isn't among those drawn.
		int highlightedDrawIndex = 0;
		for( size_t j = 1 ; j < drawnSlots.size() ; ++j )
		{
			if( std::abs( drawnSlots[j] - highlightedSlot ) < std::abs( drawnSlots[highlightedDrawIndex] - highlightedSlot ) )
				highlightedDrawIndex = (int)j;
		}

		// Draw from back (highest slot index) to front so nearer waveforms overdraw farther ones.
		for( int j = (int)drawnSlots.size() - 1 ; j >= 0 ; --j )
		{
			const int slot = drawnSlots[j];
			float* wavedata = waveTable->GetSlotPtr(slot);

			float yOffset = frontYaxis - (frontYaxis-backYaxis) * ((float) slot / (float) waveTable->slotCount);
			float xOffset = (slot * horizontalDelta) / waveTable->slotCount;

			auto& pen = ( j == highlightedDrawIndex ) ? penHighlightedFirst : penLines;

			// Draw waveform as polyline using path geometry.
			auto geometry = g.getFactory().createPathGeometry();
			auto sink = geometry.open();

			float xPos = 0.0f;
			sink.beginFigure(Point(xOffset + xPos, yOffset), FigureBegin::Hollow);
			for(int i = 0; i < waveTable->waveSize ; ++i)
			{
				float yVal = yOffset + (wavedata[i] * -vscale);
				sink.addLine(Point(xOffset + xPos, yVal));
				xPos += x_increment;
			}
			sink.addLine(Point(xOffset + xPos, yOffset));
			sink.endFigure(FigureEnd::Open);
			sink.close();

			g.drawGeometry(geometry, pen, 1.0f);

			// Fill polygon between this and the next drawn slot (toward the front) with black.
			if( j > 0 )
			{
				const int slot2 = drawnSlots[j - 1];
				float* wavedata2 = waveTable->GetSlotPtr(slot2);
				float yOffset2 = frontYaxis - (frontYaxis-backYaxis) * ((float) slot2 / (float) waveTable->slotCount);
				float xOffset2 = (slot2 * horizontalDelta) / waveTable->slotCount;

				auto fillGeometry = g.getFactory().createPathGeometry();
				auto fillSink = fillGeometry.open();

				xPos = 0.0f;
				fillSink.beginFigure(Point(xOffset + xPos, yOffset), FigureBegin::Filled);
				for(int i = 0; i < waveTable->waveSize ; ++i)
				{
					fillSink.addLine(Point(xOffset + xPos, yOffset + (wavedata[i] * -vscale)));
					xPos += x_increment;
				}
				fillSink.addLine(Point(xOffset + xPos, yOffset));

				// Back along next slot.
				fillSink.addLine(Point(xOffset2 + xPos, yOffset2));
				for(int i = waveTable->waveSize - 1 ; i >= 0 ; --i)
				{
					fillSink.addLine(Point(xOffset2 + xPos, yOffset2 + (wavedata2[i] * -vscale)));
					xPos -= x_increment;
				}
				fillSink.addLine(Point(xOffset2 + xPos, yOffset2));

				fillSink.endFigure(FigureEnd::Closed);
				fillSink.close();

				g.fillGeometry(fillGeometry, blackBrush);
			}
		}
	}

	/*
	// Wavetable name text.
	auto textFormat = g.getFactory().createTextFormat(12.0f);
	auto whiteBrush = g.createSolidColorBrush(Colors::White);

	std::string curWaveFile = getWaveFileName();
	char txt[100];
	snprintf(txt, sizeof(txt), "%s", curWaveFile.c_str());
	g.drawTextU(txt, textFormat, Rect(1.0f, 1.0f, width, 20.0f), whiteBrush);
	*/

	return ReturnCode::Ok;
}
