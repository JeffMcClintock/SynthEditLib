#pragma once

enum GuiHostCommands { HC_reset=-1, HC_null, HC_CopyPatch, HC_LoadPatch, HC_SavePatch, HC_LoadBank, HC_SaveBank, HC_SaveSubPreset=20, HC_LoadSubPreset, HC_LoadSubPresetRelaxedMatching };

class IGuiHostParameterIterator
{
public:
	virtual int32_t First() = 0;
	virtual int32_t Next() = 0;
	virtual int32_t IsDone( bool* returnValue) = 0;
	virtual class PatchParameter_base* Current() = 0;
	virtual int32_t Release() = 0; // possibly should be ref counted
};
