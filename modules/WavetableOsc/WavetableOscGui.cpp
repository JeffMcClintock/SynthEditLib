#include "WavetableOscGui.h"
#include "WavetableOsc.h"
#define _USE_MATH_DEFINES
#include <math.h>
#include <sstream>
#include "../shared/unicode_conversion.h"
#include "../shared/string_utilities.h"
#include "../shared/it_enum_list.h"
#include "../shared/FileFinder.h"

#ifdef _WIN32
#include <windows.h>
#include "Shlobj.h"
#endif

using namespace gmpi;
using namespace gmpi::drawing;
using namespace std;

namespace {
bool registeredGui = gmpi::Register<WavetableOscGui>::withId("SE Wavetable Osc");
}

WavetableOscGui::WavetableOscGui()
	: selectedFromSlot(0)
	, selectedToSlot(0)
	, pinWaveTableDisplay(0)
	, idleTimer(-1)
	, x(0)
	, phase(0)
	, animationFine(0)
{
	// Set up pin callbacks.
	pinTableModulation.onUpdate = [this](editor::PinBase*) { onModulationChanged(nullptr); };
	pinSlotModulation.onUpdate = [this](editor::PinBase*) { onModulationChanged(nullptr); };
	pinWaveFiles.onUpdate = [this](editor::PinBase*) { updateCurrentWavetable(); UpGradeWavetable(); };
	pinWaveDisplay.onUpdate = [this](editor::PinBase*) { updateWaveDisplay(); };

	currentWavetableMem_ = new char[WaveTable::CalcMemoryRequired(1,WaveTable::WavetableFileSlotCount,WaveTable::WavetableFileSampleCount)];

	currentWavetable()->waveTableCount = 0;
	currentWavetable()->slotCount = 0;
	currentWavetable()->waveSize = 0;

	memset( trace, 0, sizeof(trace) );
	memset( slotAnimation, 0, sizeof(slotAnimation) );
	memset( VoiceModulations, 0, sizeof(VoiceModulations) );

	startTimer(25); // 25ms animation timer
}

void WavetableOscGui::updateWaveDisplay()
{
	redraw();
}

WavetableOscGui::~WavetableOscGui()
{
	stopTimer();
	delete [] currentWavetableMem_;
}

void WavetableOscGui::UpGradeWavetable()
{
	// Initialize default wavetable filenames if not set.
	std::wstring wPinValue = JmUnicodeConversions::Utf8ToWstring(pinWaveFiles.value);
	it_enum_list it(wPinValue);
	it.FindIndex(32);
	if( it.IsDone() || it.CurrentItem()->text.empty() )
	{
		string val;
		for( int wavetableNumber = 0 ; wavetableNumber < 64 ; ++wavetableNumber )
		{
			char numb[10];
			snprintf(numb, sizeof(numb), "%02d", wavetableNumber );
			string filename = "F";
			filename += numb;
			filename += " XXX.wavetable.wav";

			if( wavetableNumber > 0 )
			{
				val += ",";
			}
			val += filename;
		}
		pinWaveFiles = val;
	}
}

// Load the current wavetable to the GUI, for display purposes only.
void WavetableOscGui::updateCurrentWavetable()
{
	string curWaveFile = getWaveFileName(pinWaveTableDisplay);
	if( curWaveFile_ != curWaveFile )
	{
		curWaveFile_ = curWaveFile;

		currentWavetable()->waveTableCount = 1;
		currentWavetable()->slotCount = WaveTable::WavetableFileSlotCount;
		currentWavetable()->waveSize = WaveTable::WavetableFileSampleCount;

#ifdef _WIN32
		wchar_t myDocumentsPath[MAX_PATH];
		SHGetFolderPath( NULL, CSIDL_MYDOCUMENTS, NULL, SHGFP_TYPE_CURRENT, myDocumentsPath );
		std::wstring FactoryWavetableFolder_( myDocumentsPath );
		FactoryWavetableFolder_ += L"\\Codex\\";

		// Load Wavetable off disk.
		std::wstring searchPaths[3];
		searchPaths[0] = FactoryWavetableFolder_;
		searchPaths[1] = FactoryWavetableFolder_;
		searchPaths[2] = L"C:\\Codex\\";

		std::wstring wCurWaveFile = JmUnicodeConversions::Utf8ToWstring(curWaveFile_);

		bool success = false;
		for( int s = 0 ; s < 3 ; ++s )
		{
			std::wstring filePath = searchPaths[s] + wCurWaveFile;
			if( true == (success = currentWavetable()->LoadFile3( filePath.c_str(), true )) )
			{
				break;
			}
		}

		if( !success )
		{
			currentWavetable()->waveTableCount = 0;
			currentWavetable()->slotCount = 0;
			currentWavetable()->waveSize = 0;
		}
#endif

		redraw();
	}
}

void WavetableOscGui::onModulationChanged(editor::PinBase* /*pin*/)
{
	float tableVal = std::min( 1.0f, std::max(pinTableModulation.value, 0.0f ));
	float slotVal = std::min( 1.0f, std::max(pinSlotModulation.value, 0.0f ));

	// Push current to front of modulation stack.
	for( int i = animateVoicesCount - 2 ; i >= 0 ; --i )
	{
		VoiceModulations[i+1][0] = VoiceModulations[i][0];
		VoiceModulations[i+1][1] = VoiceModulations[i][1];
		VoiceModulations[i+1][2] = VoiceModulations[i][2];
	}
	VoiceModulations[0][0] = 0.0f; // voice id (not tracked in new API)
	VoiceModulations[0][1] = tableVal;
	VoiceModulations[0][2] = slotVal;

	idleTimer = 50;
}

ReturnCode WavetableOscGui::render(gmpi::drawing::api::IDeviceContext* dc)
{
	Graphics g(dc);
	ClipDrawingToBounds _(g, bounds);

	drawing::Rect r;
	getClipArea(&r);
	float width = getWidth(r);
	float height = getHeight(r);

	float vscale = height * 0.5f;

	// Fill background.
	auto backgroundBrush = g.createSolidColorBrush(Color(50.0f/255.0f, 50.0f/255.0f, 50.0f/255.0f));
	g.fillRectangle(r, backgroundBrush);

	WaveTable* waveTable = currentWavetable();

	if( waveTable->waveTableCount < 1 )
		return ReturnCode::Ok;

	// Wavetable 3D display (when not animating).
	if( idleTimer < 0 )
	{
		auto penLines = g.createSolidColorBrush(Color(0.0f, 155.0f/255.0f, 0.0f));
		auto penHighlightedFirst = g.createSolidColorBrush(Color(1.0f, 50.0f/255.0f, 50.0f/255.0f));
		auto blackBrush = g.createSolidColorBrush(Colors::Black);

		float horizontalDelta = width / 3.0f;
		float x_increment = (width - horizontalDelta) / (float) waveTable->waveSize;
		float backYaxis = vscale * 0.5f;
		float frontYaxis = height - backYaxis;

		for( int slot = waveTable->slotCount - 1 ; slot >= 0 ; --slot )
		{
			float* wavedata = waveTable->GetSlotPtr( pinWaveTableDisplay, slot );

			float yOffset = frontYaxis - (frontYaxis-backYaxis) * ((float) slot / (float) waveTable->slotCount);
			float xOffset = (slot * horizontalDelta) / waveTable->slotCount;

			auto& pen = ( selectedFromSlot <= slot && selectedToSlot >= slot ) ? penHighlightedFirst : penLines;

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

			// Fill polygon between this and next slot with black.
			if( slot > 0 )
			{
				float* wavedata2 = waveTable->GetSlotPtr( pinWaveTableDisplay, slot - 1 );
				float yOffset2 = frontYaxis - (frontYaxis-backYaxis) * ((float) (slot-1) / (float) waveTable->slotCount);
				float xOffset2 = ((slot-1) * horizontalDelta) / waveTable->slotCount;

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

	// Wavetable name text.
	auto textFormat = g.getFactory().createTextFormat(12.0f);
	auto whiteBrush = g.createSolidColorBrush(Colors::White);

	std::string curWaveFile = getWaveFileName(pinWaveTableDisplay);
	char txt[100];
	snprintf(txt, sizeof(txt), "WT%d: %s", pinWaveTableDisplay, curWaveFile.c_str());
	g.drawTextU(txt, textFormat, Rect(1.0f, 1.0f, width, 20.0f), whiteBrush);

	if( idleTimer < 0 )
		return ReturnCode::Ok;

	// Animated wave display from DSP.
	{
		int numPoints = (int)pinWaveDisplay.value.size() / sizeof(float);
		if(numPoints > 0)
		{
			float* wave = (float*) pinWaveDisplay.value.data();

			auto penBright = g.createSolidColorBrush(Color(175.0f/255.0f, 50.0f/255.0f, 175.0f/255.0f));

			float left = (width - numPoints * 2.0f) / 2.0f;

			auto waveGeometry = g.getFactory().createPathGeometry();
			auto waveSink = waveGeometry.open();

			waveSink.beginFigure(Point(left, height/2.0f - wave[0] * height/2.0f), FigureBegin::Hollow);
			for( int s = 1 ; s < numPoints ;++s )
			{
				waveSink.addLine(Point(left + (float)s, height/2.0f - wave[s] * height/2.0f));
			}
			// Mirror bottom half.
			for( int s = numPoints - 1 ; s >= 0 ; --s )
			{
				float s2 = (float)(numPoints * 2 - s);
				waveSink.addLine(Point(left + s2, height/2.0f + wave[s] * height/2.0f));
			}
			waveSink.endFigure(FigureEnd::Open);
			waveSink.close();

			g.drawGeometry(waveGeometry, penBright, 1.0f);
		}
	}

	// Slot/Table indicator dot.
	float border = 4.0f;
	float scale = std::min(width, height) - border * 2.0f;

	// Grid.
	auto gridPen = g.createSolidColorBrush(Color(0.0f, 100.0f/255.0f, 0.0f));
	for( float grid = 0.0f ; grid <= 1.001f ; grid += 0.2f )
	{
		float pos = border + grid * scale;
		g.drawLine(Point(pos, border), Point(pos, border + scale), gridPen, 1.0f);
		g.drawLine(Point(border, pos), Point(border + scale, pos), gridPen, 1.0f);
	}

	// Dots.
	auto dotBrush = g.createSolidColorBrush(Colors::White);
	for( int i = 0 ; i < 4 ; ++i )
	{
		if( slotAnimation[i].counter > 0 )
		{
			float dx = border + slotAnimation[i].slot * scale;
			float dy = border + (1.0f - slotAnimation[i].table) * scale;

			g.fillRectangle(Rect(dx - 2.0f, dy - 2.0f, dx + 2.0f, dy + 2.0f), dotBrush);
		}
	}

	return ReturnCode::Ok;
}

bool WavetableOscGui::onTimer()
{
	if( idleTimer < 0 )
	{
		return true;
	}

	--idleTimer;

	float* highlightedSlots = VoiceModulations[0];

	for( int i = 0 ; i < 4 ; ++i )
	{
		if( i * 3 + 2 < animateVoicesCount * 3 )
		{
			if( slotAnimation[i].voice != (int) highlightedSlots[i*3] || slotAnimation[i].table != highlightedSlots[i*3 + 1] || slotAnimation[i].slot != highlightedSlots[i*3 + 2] )
			{
				slotAnimation[i].voice = (int) highlightedSlots[i*3];
				slotAnimation[i].table = highlightedSlots[i*3 + 1];
				slotAnimation[i].slot = highlightedSlots[i*3 + 2];
				slotAnimation[i].counter = 10;
			}
		}
	}

	// Rolling wave...
	animationFine = (animationFine + 2) & 0x06;

	if(animationFine != 0)
	{
		redraw();
		return true;
	}

	redraw();

	return true; // more timer calls please.
}

void WavetableOscGui::refreshWaveFilePoolNames()
{
	waveFilePoolNames.clear();

#ifdef _WIN32
	wchar_t myDocumentsPath[MAX_PATH];
	SHGetFolderPath( NULL, CSIDL_MYDOCUMENTS, NULL, SHGFP_TYPE_CURRENT, myDocumentsPath );

	std::wstring searchPaths[2];
	searchPaths[0] = myDocumentsPath;
	searchPaths[0] += L"\\Codex\\*.wav";
	searchPaths[1] = L"C:\\Codex\\*.wav";

	for( int s = 0 ; s < 2 ; ++s )
	{
		for( FileFinder it = searchPaths[s].c_str() ; !it.done() ; ++it )
		{
			if( ! (*it).isFolder )
			{
				string menutext = JmUnicodeConversions::WStringToUtf8( (*it).filename );
				waveFilePoolNames.push_back( menutext );
			}
		}
	}
#endif
}

string WavetableOscGui::getWaveFilePoolName( int idx )
{
	return waveFilePoolNames[idx];
}

string WavetableOscGui::getWaveFileName( int idx )
{
	std::wstring wPinValue = JmUnicodeConversions::Utf8ToWstring(pinWaveFiles.value);
	it_enum_list it( wPinValue );
	it.FindIndex(idx);
	if( ! it.IsDone() )
		return JmUnicodeConversions::WStringToUtf8(it.CurrentItem()->text);

	return "";
}

void WavetableOscGui::setWaveFileName( int idx, string filename )
{
	// Extract current list.
	string currentWaves[64];
	std::wstring wPinValue = JmUnicodeConversions::Utf8ToWstring(pinWaveFiles.value);
	it_enum_list it( wPinValue );
	int i = 0;
	for( it.First(); !it.IsDone() && i < 64; ++it, ++i )
	{
		currentWaves[i] = JmUnicodeConversions::WStringToUtf8(it.CurrentItem()->text);
	}

	// Update one item.
	currentWaves[idx] = filename;

	// recreate list.
	string l = currentWaves[0];
	for( int j = 1 ; j < 64 ; ++j )
	{
		l += ",";
		l += currentWaves[j];
	}

	pinWaveFiles = l;
}
