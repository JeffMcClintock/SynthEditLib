#pragma once
/*
#include "modules/se_sdk3_hosting/AssignControllerDialogWin.h"
*/
#include "AssignControllerDialogShared.h" // MidiAssignment, ShowAssignControllerDialog

// Win32 recreation of the editor's 'Assign Controller' dialog (SynthEdit2/AssignControllerDialog.xaml)
// for plugins, which can't use WinUI. The cross-platform model and the ShowAssignControllerDialog
// entry point live in AssignControllerDialogShared.h; this header only adds the Win32 control IDs.

namespace SE2
{

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
