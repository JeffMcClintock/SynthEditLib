#ifndef WAVETABLEOSCGUI_H_INCLUDED
#define WAVETABLEOSCGUI_H_INCLUDED

#include "helpers/GmpiPluginEditor.h"
#include "helpers/Timer.h"
#include "Wavetable.h"

struct SlotAnimInfo
{
	float table;
	float slot;
	int voice;
	int counter;
};

class WavetableOscGui : public gmpi::editor::PluginEditor, public gmpi::TimerClient
{
	int idleTimer;
	int x;
	int phase;
	static const int traceSamples = 256;
	static const int traceTrails = 20;
	int trace[traceSamples][traceTrails];
	int animationFine;
	SlotAnimInfo slotAnimation[4];
	char* currentWavetableMem_;
	std::string curWaveFile_;

public:
	WavetableOscGui();
	~WavetableOscGui();

	// PluginEditor overrides
	gmpi::ReturnCode render(gmpi::drawing::api::IDeviceContext* dc) override;
	bool onTimer() override;

	WaveTable* currentWavetable()
	{
		return (WaveTable*) currentWavetableMem_;
	}
	void updateCurrentWavetable();
	void UpGradeWavetable();
	void updateWaveDisplay();

private:
	void redraw()
	{
		if(drawingHost)
			drawingHost->invalidateRect(nullptr);
	}

	void onModulationChanged(gmpi::editor::PinBase* pin);

	// GUI pins (matching XML GUI pin order)
	gmpi::editor::Pin<float> pinTableModulation;  // pin 0
	gmpi::editor::Pin<float> pinSlotModulation;   // pin 1
	gmpi::editor::Pin<std::string> pinWaveFiles;  // pin 2
	gmpi::editor::Pin<gmpi::Blob> pinWaveDisplay; // pin 3

	static const int animateVoicesCount = 4;
	float VoiceModulations[animateVoicesCount][3]; // 0-Voice, 1-Table, 2-Slot.
	int selectedFromSlot;
	int selectedToSlot;
	int pinWaveTableDisplay;
	std::vector< std::string > waveFilePoolNames;
	void refreshWaveFilePoolNames();
	std::string getWaveFilePoolName( int idx );
	std::string getWaveFileName( int idx );
	void setWaveFileName( int idx, std::string filename );
};

#endif
