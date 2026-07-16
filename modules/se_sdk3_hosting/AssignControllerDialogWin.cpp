#ifdef _WIN32

#include "AssignControllerDialogWin.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <algorithm>
#include <vector>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace
{

enum
{
	IDC_TYPE_LIST = SE2::AssignControllerDialogIds::TypeList,
	IDC_CC_LIST = SE2::AssignControllerDialogIds::CcList,
	IDC_NUMBER_EDIT = SE2::AssignControllerDialogIds::NumberEdit,
	IDC_SYSEX_EDIT = SE2::AssignControllerDialogIds::SysexEdit,
};

// Builds a DLGTEMPLATE in memory, so plugins need no resource (.rc) file.
// Everything in the template is a WORD stream; items start DWORD-aligned.
struct DlgTemplateBuilder
{
	std::vector<WORD> words;

	void addDword(DWORD v)
	{
		words.push_back(LOWORD(v));
		words.push_back(HIWORD(v));
	}
	void addString(const wchar_t* s)
	{
		do {
			words.push_back(static_cast<WORD>(*s));
		} while (*s++);
	}

	// DLGTEMPLATE header. itemCount must match the number of addItem calls.
	DlgTemplateBuilder(WORD itemCount, short cx, short cy, const wchar_t* title)
	{
		addDword(DS_SETFONT | DS_MODALFRAME | DS_CENTER | WS_POPUP | WS_CAPTION | WS_SYSMENU);
		addDword(0); // no extended style
		words.push_back(itemCount);
		words.insert(words.end(), { 0, 0, (WORD)cx, (WORD)cy });
		words.push_back(0); // no menu
		words.push_back(0); // standard dialog class
		addString(title);
		words.push_back(8); // font point size
		addString(L"MS Shell Dlg");
	}

	// DLGITEMTEMPLATE. sysClass: 0x0080 button, 0x0081 edit, 0x0083 listbox.
	void addItem(DWORD style, short x, short y, short cx, short cy, WORD id, WORD sysClass, const wchar_t* text = L"")
	{
		if (words.size() & 1) // DWORD-align
			words.push_back(0);

		addDword(style | WS_CHILD);
		addDword(0); // no extended style
		words.insert(words.end(), { (WORD)x, (WORD)y, (WORD)cx, (WORD)cy, id });
		words.push_back(0xFFFF);
		words.push_back(sysClass);
		addString(text);
		words.push_back(0); // no creation data
	}
};

int32_t selectedItemData(HWND list, int32_t fallback)
{
	const auto sel = (int)SendMessage(list, LB_GETCURSEL, 0, 0);
	if (sel == LB_ERR)
		return fallback;
	return (int32_t)SendMessage(list, LB_GETITEMDATA, sel, 0);
}

void selectItemByData(HWND list, int32_t data)
{
	const auto count = (int)SendMessage(list, LB_GETCOUNT, 0, 0);
	for (int i = 0; i < count; ++i)
	{
		if ((int32_t)SendMessage(list, LB_GETITEMDATA, i, 0) == data)
		{
			SendMessage(list, LB_SETCURSEL, i, 0);
			return;
		}
	}
	SendMessage(list, LB_SETCURSEL, 0, 0);
}

// Show the one right-hand control the selected type needs (per the shared visibility rule).
void updateVisibility(HWND dlg)
{
	const auto field = SE2::fieldForType(selectedItemData(GetDlgItem(dlg, IDC_TYPE_LIST), ControllerType::None));

	ShowWindow(GetDlgItem(dlg, IDC_CC_LIST),     field == SE2::AssignmentField::CcList ? SW_SHOW : SW_HIDE);
	ShowWindow(GetDlgItem(dlg, IDC_NUMBER_EDIT), field == SE2::AssignmentField::Number ? SW_SHOW : SW_HIDE);
	ShowWindow(GetDlgItem(dlg, IDC_SYSEX_EDIT),  field == SE2::AssignmentField::Sysex  ? SW_SHOW : SW_HIDE);
}

INT_PTR CALLBACK dialogProc(HWND dlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	auto assignment = reinterpret_cast<SE2::MidiAssignment*>(GetWindowLongPtr(dlg, DWLP_USER));

	switch (message)
	{
	case WM_INITDIALOG:
	{
		SetWindowLongPtr(dlg, DWLP_USER, lParam);
		assignment = reinterpret_cast<SE2::MidiAssignment*>(lParam);

		// Decode the current assignment (shared model).
		const auto [type, number] = SE2::decodeAutomation(assignment->automation);

		auto typeList = GetDlgItem(dlg, IDC_TYPE_LIST);
		for (const auto& t : SE2::controllerTypeList())
		{
			const auto idx = (int)SendMessage(typeList, LB_ADDSTRING, 0, (LPARAM)t.name);
			SendMessage(typeList, LB_SETITEMDATA, idx, t.type);
		}
		selectItemByData(typeList, type);

		// CC names from the shared table (value == CC number).
		auto ccList = GetDlgItem(dlg, IDC_CC_LIST);
		for (const auto& cc : SE2::ccNameList())
		{
			const auto idx = (int)SendMessage(ccList, LB_ADDSTRING, 0, (LPARAM)cc.name.c_str());
			SendMessage(ccList, LB_SETITEMDATA, idx, cc.ccNumber);
		}
		selectItemByData(ccList, type == ControllerType::CC ? number : 0);

		SetDlgItemInt(dlg, IDC_NUMBER_EDIT, std::clamp(number, 0, SE2::kMaxRpnNumber), FALSE);
		SetDlgItemTextW(dlg, IDC_SYSEX_EDIT, assignment->sysex.c_str());

		updateVisibility(dlg);
		return TRUE;
	}

	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case IDC_TYPE_LIST:
			if (HIWORD(wParam) == LBN_SELCHANGE)
				updateVisibility(dlg);
			return TRUE;

		case IDOK:
		{
			const auto type = selectedItemData(GetDlgItem(dlg, IDC_TYPE_LIST), ControllerType::None);

			// Only CC/RPN/NRPN carry a number; other types are canonically number 0.
			int number = 0;
			const auto field = SE2::fieldForType(type);
			if (field == SE2::AssignmentField::CcList)
			{
				number = selectedItemData(GetDlgItem(dlg, IDC_CC_LIST), 0);
			}
			else if (field == SE2::AssignmentField::Number)
			{
				number = std::clamp((int)GetDlgItemInt(dlg, IDC_NUMBER_EDIT, nullptr, FALSE), 0, SE2::kMaxRpnNumber);
			}

			assignment->automation = SE2::encodeAutomation(type, number);

			const auto len = GetWindowTextLengthW(GetDlgItem(dlg, IDC_SYSEX_EDIT));
			assignment->sysex.resize(len);
			if (len > 0)
				GetDlgItemTextW(dlg, IDC_SYSEX_EDIT, assignment->sysex.data(), len + 1);

			EndDialog(dlg, 1);
			return TRUE;
		}

		case IDCANCEL:
			EndDialog(dlg, 0);
			return TRUE;
		}
		break;
	}

	return FALSE; // not handled
}

} // namespace

namespace SE2
{

bool ShowAssignControllerDialog(void* parentWindow, MidiAssignment& assignment)
{
	// Layout in dialog units (scale with the dialog font, so high-DPI works unaided).
	DlgTemplateBuilder dlg(6, 248, 164, L"Assign Controller");

	constexpr DWORD listStyle = WS_VISIBLE | WS_TABSTOP | WS_BORDER | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT;
	constexpr DWORD editStyle = WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL; // right-hand controls start hidden

	dlg.addItem(listStyle,               7,   7, 112, 132, IDC_TYPE_LIST,   0x0083);
	dlg.addItem(listStyle & ~WS_VISIBLE, 125,  7, 116, 132, IDC_CC_LIST,     0x0083);
	dlg.addItem(editStyle | ES_NUMBER,   125,  7, 116,  14, IDC_NUMBER_EDIT, 0x0081);
	dlg.addItem(editStyle,               125,  7, 116,  14, IDC_SYSEX_EDIT,  0x0081);
	dlg.addItem(WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 137, 143, 50, 14, IDOK,     0x0080, L"OK");
	dlg.addItem(WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,    191, 143, 50, 14, IDCANCEL, 0x0080, L"Cancel");

	// The plugin window is a child; own the dialog by its top-level ancestor so
	// modality disables the whole host window, like the native file dialogs do.
	auto owner = GetAncestor((HWND)parentWindow, GA_ROOT);

	const auto result = DialogBoxIndirectParamW(
		(HINSTANCE)&__ImageBase,
		(LPCDLGTEMPLATEW)dlg.words.data(),
		owner,
		dialogProc,
		(LPARAM)&assignment);

	return result == 1;
}

} // namespace SE2

#endif // _WIN32
