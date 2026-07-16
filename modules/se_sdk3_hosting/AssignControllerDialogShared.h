#pragma once
/*
#include "modules/se_sdk3_hosting/AssignControllerDialogShared.h"

Platform-neutral model for the plugin's 'Assign Controller' dialog: the packing of a
MIDI assignment, the controller-type list, the CC-name list, and the field-visibility
rule. The native views (AssignControllerDialogWin.cpp / AssignControllerDialogMac.mm)
are thin shells over this — keep decode/encode/visibility here so the two platforms
can't drift (they used to: the editor's Cocoa dialog hardcoded 2/3/4/5 for the types).
*/
#include <cstdint>
#include <string>
#include <vector>
#include "midi_defs.h" // ControllerType, CONTROLLER_ENUM_LIST

namespace SE2
{

// A parameter's MIDI assignment, as edited by the dialog.
struct MidiAssignment
{
	int32_t automation = ControllerType::None; // (ControllerType << 24) | number, or a plain negative type (e.g. None).
	std::wstring sysex;
};

// RPN/NRPN numbers are 14-bit.
constexpr int kMaxRpnNumber = 16383;

// The packed 'automation' int splits into a high-byte controller type and a 24-bit number
// (only CC/RPN/NRPN carry a number). Negative values are whole types with no number (None).
struct DecodedAutomation { int32_t type; int32_t number; };

inline DecodedAutomation decodeAutomation(int32_t automation)
{
	if (automation < 0)
		return { automation, 0 };
	return { automation >> 24, automation & 0x00FFFFFF };
}

inline int32_t encodeAutomation(int32_t type, int32_t number)
{
	if (type < 0)
		return type; // None etc. carry no number.
	return (type << 24) | (number & 0x00FFFFFF);
}

// Which one right-hand input the chosen type needs (the others stay hidden).
enum class AssignmentField { None, CcList, Number, Sysex };

inline AssignmentField fieldForType(int32_t type)
{
	switch (type)
	{
	case ControllerType::CC:    return AssignmentField::CcList;
	case ControllerType::RPN:
	case ControllerType::NRPN:  return AssignmentField::Number;
	case ControllerType::SYSEX: return AssignmentField::Sysex;
	default:                    return AssignmentField::None;
	}
}

// Controller-type picker entries, in display order (same order as the editor's dialog).
struct ControllerTypeInfo { int32_t type; const wchar_t* name; };
const std::vector<ControllerTypeInfo>& controllerTypeList();

// CC picker entries (value == CC number), from the shared CONTROLLER_ENUM_LIST.
struct CcNameEntry { std::wstring name; int32_t ccNumber; };
const std::vector<CcNameEntry>& ccNameList();

// Show the native modal dialog, seeded from 'assignment'. Returns true and updates
// 'assignment' if the user pressed OK; false on Cancel. Implemented per platform.
// parentWindow is the host window handle (HWND on Windows); may be null on macOS.
bool ShowAssignControllerDialog(void* parentWindow, MidiAssignment& assignment);

} // namespace SE2
