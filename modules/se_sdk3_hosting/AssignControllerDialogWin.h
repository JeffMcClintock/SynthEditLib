#pragma once
/*
#include "modules/se_sdk3_hosting/AssignControllerDialogWin.h"
*/
#include <string>
#include <cstdint>

namespace SE2
{

struct MidiAssignment
{
	int32_t automation = -1; // (ControllerType << 24) | controller-number, or plain negative for none.
	std::wstring sysex;
};

// Win32 recreation of the editor's 'Assign Controller' dialog (SynthEdit2/AssignControllerDialog.xaml)
// for plugins, which can't use WinUI. Modal. Returns true if the user pressed OK,
// with 'assignment' updated to their choice.
bool ShowAssignControllerDialog(void* parentWindow, MidiAssignment& assignment);

// Control IDs, public so tests can drive the dialog with GetDlgItem/WM_COMMAND.
namespace AssignControllerDialogIds
{
	enum
	{
		TypeList = 1001,
		CcList,
		NumberEdit,
		SysexEdit,
	};
}

} // namespace SE2
