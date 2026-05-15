#ifndef WAVETABLEOSCGUI_H_INCLUDED
#define WAVETABLEOSCGUI_H_INCLUDED

#include "helpers/GmpiPluginEditor.h"
#include "Wavetable.h"

class WavetableOscGui : public gmpi::editor::PluginEditor
{
	char* currentWavetableMem_;
	std::string curWaveFile_;

public:
	WavetableOscGui();
	~WavetableOscGui();

	// PluginEditor overrides
	gmpi::ReturnCode render(gmpi::drawing::api::IDeviceContext* dc) override;

	WaveTable* currentWavetable()
	{
		return (WaveTable*) currentWavetableMem_;
	}
	void updateCurrentWavetable();

private:
	void redraw()
	{
		if(drawingHost)
			drawingHost->invalidateRect(nullptr);
	}

	// GUI pins (matching XML GUI pin order for "SE Wavetable Display")
	gmpi::editor::Pin<std::string> pinWaveFiles;  // pin 0 - WaveTableFile
	gmpi::editor::Pin<float>       pinSlot;       // pin 1 - Slot (0..1, drives the red-highlight slot)

	std::vector< std::string > waveFilePoolNames;
	void refreshWaveFilePoolNames();
	std::string getWaveFilePoolName( int idx );
	std::string getWaveFileName();
	void setWaveFileName( int idx, std::string filename );
};

#endif
