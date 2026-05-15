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
{
	pinWaveFiles.onUpdate = [this](editor::PinBase*) { updateCurrentWavetable(); };

	currentWavetableMem_ = new char[WaveTable::CalcMemoryRequired(WaveTable::WavetableFileSlotCount,WaveTable::WavetableFileSampleCount)];

	currentWavetable()->slotCount = 0;
	currentWavetable()->waveSize = 0;
}

WavetableOscGui::~WavetableOscGui()
{
	delete [] currentWavetableMem_;
}

// Load the current wavetable to the GUI, for display purposes only.
void WavetableOscGui::updateCurrentWavetable()
{
	string curWaveFile = getWaveFileName();
	if( curWaveFile_ != curWaveFile )
	{
		curWaveFile_ = curWaveFile;

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

		auto isAbsolutePath = [](const std::wstring& p) -> bool
		{
			if (p.empty()) return false;
			if (p[0] == L'/' || p[0] == L'\\') return true;
			if (p.size() >= 2 && p[1] == L':') return true; // Windows drive letter.
			return false;
		};

		bool success = false;

		if (isAbsolutePath(wCurWaveFile))
		{
			success = currentWavetable()->LoadFile3(wCurWaveFile.c_str(), true);
		}

		if (!success)
		{
			for( int s = 0 ; s < 3 ; ++s )
			{
				std::wstring filePath = searchPaths[s] + wCurWaveFile;
				if( true == (success = currentWavetable()->LoadFile3( filePath.c_str(), true )) )
				{
					break;
				}
			}
		}

		if( !success )
		{
			currentWavetable()->slotCount = 0;
			currentWavetable()->waveSize = 0;
		}
#endif

		redraw();
	}
}

ReturnCode WavetableOscGui::render(gmpi::drawing::api::IDeviceContext* dc)
{
	Graphics g(dc);
	ClipDrawingToBounds _(g, bounds);

	drawing::Rect r;
	getClipArea(&r);
	float width = getWidth(r);
	float height = getHeight(r);

	float vscale = height * 0.25f;

	// Fill background.
	auto backgroundBrush = g.createSolidColorBrush(Color(50.0f/255.0f, 50.0f/255.0f, 50.0f/255.0f));
	g.fillRectangle(r, backgroundBrush);

	WaveTable* waveTable = currentWavetable();

	if(!waveTable)
		return ReturnCode::Ok;

	// Wavetable 3D display - always visible so the user can see the loaded shape even with no audio running.
	{
		auto penLines = g.createSolidColorBrush(Color(0.0f, 155.0f/255.0f, 0.0f));
		auto penHighlightedFirst = g.createSolidColorBrush(Color(1.0f, 50.0f/255.0f, 50.0f/255.0f));
		auto blackBrush = g.createSolidColorBrush(Colors::Black);

		float horizontalDelta = width / 3.0f;
		float x_increment = (width - horizontalDelta) / (float) waveTable->waveSize;
		float backYaxis = vscale * 0.5f;
		float frontYaxis = height - backYaxis;

		// Slot-highlight pin removed - external animation signal will be wired up in a later step.
		const int highlightedSlot = -1;

		for( int slot = waveTable->slotCount - 1 ; slot >= 0 ; --slot )
		{
			float* wavedata = waveTable->GetSlotPtr(slot);

			float yOffset = frontYaxis - (frontYaxis-backYaxis) * ((float) slot / (float) waveTable->slotCount);
			float xOffset = (slot * horizontalDelta) / waveTable->slotCount;

			auto& pen = ( slot == highlightedSlot ) ? penHighlightedFirst : penLines;

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
				float* wavedata2 = waveTable->GetSlotPtr(slot - 1 );
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

	std::string curWaveFile = getWaveFileName();
	char txt[100];
	snprintf(txt, sizeof(txt), "%s", curWaveFile.c_str());
	g.drawTextU(txt, textFormat, Rect(1.0f, 1.0f, width, 20.0f), whiteBrush);

	return ReturnCode::Ok;
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

string WavetableOscGui::getWaveFileName()
{
	return pinWaveFiles.value;
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
