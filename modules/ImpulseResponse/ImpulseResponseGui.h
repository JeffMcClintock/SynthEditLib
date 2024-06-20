#ifndef IMPULSERESPONSEGUI_H_INCLUDED
#define IMPULSERESPONSEGUI_H_INCLUDED

#if defined(_WIN32) && !defined(_WIN64)

#include "../se_sdk3/MP_SDK_GUI.h"

class ImpulseResponseGui : public SeGuiCompositedGfxBase
{
public:
	ImpulseResponseGui( IMpUnknown* host );
	void onValueChanged();

	// overrides
	int32_t paint( HDC hDC );

private:
	BlobGuiPin pinResults;
	FloatGuiPin pinSampleRate;

	float displayOctaves;
	float displayDbMax;
	float displayDbRange;
};

#endif
#endif


