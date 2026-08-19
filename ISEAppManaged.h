#pragma once
#include <string>

// MFC Application calling interface into managed C++ (forwarded to WPF Application).
// These implemented in SEAppManagedBridge class.
namespace SE2
{
	class IPresenter;
}

#ifndef _WIN32
typedef int32_t HWND;
#endif

namespace gmpi { namespace api { struct IDialogHost; } }

class ISEAppManaged
{
public:
	virtual ~ISEAppManaged() {}

	virtual HWND MainWindowhandle() = 0;
	virtual gmpi::api::IDialogHost* getCurrentDialogHost() = 0;
	virtual void UpdateUndoMenus( bool CanUndo, bool CanRedo, std::wstring undo_description, std::wstring redo_description ) = 0;
	virtual void DeferredMessageBox(const wchar_t* msg, int flags) = 0; // Async
	virtual void AppPropertyChanged(const std::wstring& propertyName ) = 0;
	virtual	void AttachContainerModel( class CContainer* container ) = 0;
	virtual void CloseAllViews() = 0;
	virtual void ReloadAllViews() = 0;
	// Close (consolidate) any torn-out tab windows. Called when the document is
	// replaced or closed (New/Open/Close). Default no-op for headless/CL hosts.
	virtual void CloseTornOutWindows() {}
	// Record the current window arrangement into the document just before it is
	// saved, and restore it just after a document is loaded (see
	// CSynthEditDocBase::m_windowLayout).
	//
	// Both default to a no-op, which is the whole fallback story for front ends
	// with no torn-out windows (macOS, Wayland, CL): every tab opens in the main
	// window as it always has, and because Capture leaves m_windowLayout as loaded,
	// saving there preserves a layout made on Windows rather than erasing it.
	virtual void CaptureWindowLayout() {}
	virtual void ApplyWindowLayout() {}
	virtual void OpenView(
		class CContainer* p_object, int view_flag/*, const std::wstring& p_title, int p_window_icon*/) = 0;
};
