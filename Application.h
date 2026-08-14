#pragma once
#if defined( _WIN32 )
#undef  WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#undef  NOMINMAX
#define NOMINMAX
#include <Windows.h>
#endif
#include <memory>
#include <string>
#include <vector>
#include <functional>
#include "notify.h"
#include "commandMgr.h"
#include "SafeMessageBox.h"
#include "Hosting/message_queues.h"
#include "helpers/NativeUi.h"

class CSynthEditDocBase;
class ModuleDragAndDropManager;
namespace SE2{struct feedbackPinUi;}
namespace gmpi { namespace api { struct IDialogHost; } }
namespace gmpi { namespace hosting{class IWriteableQue;}}

enum se_realtime_flags { SER_FILE = 1, SER_REALTIME = 2, SER_SOUNDCARD_IN = 4, SER_SOUNDCARD_OUT = 8 };

struct folder_info
{
	std::vector<std::wstring> extensions; // primary one first, alternatives later. e.g. {"bmp", "png", jpg"}
	std::wstring description;
	std::wstring default_folder;
	std::wstring current_folder; // last place user opened file of this type
};

void CopyInitialPrefabs();

class ApplicationBase : public Notifier
{
protected:
	CommandMgr m_command_manager;
	std::vector<std::unique_ptr<folder_info>> m_folder_settings;
	std::unique_ptr<CSynthEditDocBase> document_;

public:
	std::wstring overrideSamplesFolder;

	ApplicationBase();
	virtual ~ApplicationBase(); // defined in .cpp (unique_ptr<incomplete type>)

	// Every app must identify itself.
	virtual std::string getVendor4charCode() = 0;

	// Document management — default creates a SynthEditDoc and stores in document_.
	virtual void createNewDocument();
	virtual CSynthEditDocBase* Document() { return document_.get(); }

	// Default no-op/stub implementations for lightweight apps (CLApp, TideApp).
	// Full-featured apps (CSynthEditAppBase) override these with real implementations.
	virtual void invalidateDsp() {}
	virtual void CloseAllViews() {}
	virtual std::wstring ResolveFilename(const std::wstring& name, const std::wstring& /*extension*/) { return name; }
	virtual int32_t resolveFilename(const wchar_t* /*shortFilename*/, int32_t /*maxChars*/, wchar_t* /*returnFullFilename*/) { return 0; }
	virtual std::string ShortenFilename(std::string name, std::string /*extension*/) { return name; }
	virtual void DoHelp(std::wstring /*p_url*/, int /*p_cmd*/ = 0) {}
	virtual gmpi::hosting::IWriteableQue* MessageQueToDspOrNull() { return {}; }
	virtual gmpi::hosting::QueuedUsers* PendingDspClients() { return {}; }
	virtual int32_t sendSdkMessageToAudio(int32_t /*handle*/, int32_t /*id*/, int32_t /*size*/, const void* /*messageData*/) { return 0; }
	virtual bool OnTimer() { return true; }
	virtual void GetRegistrationInfo(std::wstring& /*p_user_email*/, std::wstring& /*p_serial*/) {}
	virtual int SnapPixels() { return 8; }
	virtual void OnRunStop() {}
	virtual bool IsRegisteredVersion() { return true; }
	virtual void OpenDocumentFile2(const wchar_t* /*lpszFileName*/) {}
	virtual void OpenView(class CContainer* /*p_object*/, int /*view_flag*/) {}
	virtual std::string setVendor4charCodeSanitized(std::string p_code) { return p_code; }
	virtual void DeferredMessageBox(const wchar_t* /*msg*/, int /*flags*/) {}
	virtual void ReloadMenu() {}
	virtual void reportFeedbackErrorUi(std::list< std::pair<SE2::feedbackPinUi, SE2::feedbackPinUi> >& /*feedbackConnectors*/) {}
	virtual bool ApplyHighlights(int /*flags*/, std::vector<class CUG*>* /*modules*/) { return false; }
	virtual gmpi::api::IDialogHost* getCurrentDialogHost() { return {}; }
	virtual void onDocumentChanged() {}

#ifdef _WIN32
	virtual HWND MainWindowhandle() { return {}; }
#endif

	virtual std::wstring getSettingString(const wchar_t* name);

	// "Default Line Style" preference: a structure-view connector line set to
	// CLine2::DEFAULT_STYLE follows this app-wide choice. Lightweight apps (e.g. the
	// command-line tool) keep the historical 'straight' look via the false default.
	virtual bool defaultLinesCurved() { return false; }

	// Single source of truth for "user is dragging a new module from the browser".
	// Implemented by full-featured apps (SynthEditApp); returns nullptr in lightweight
	// apps that don't host the editor UI.
	virtual ModuleDragAndDropManager* getModuleDragAndDropManager() { return nullptr; }

	void setHoverScopePin(int32_t moduleHandle_watched, int32_t moduleHandle_original, int32_t dspHoverPinIdx);
	void refreshFolderLocations();
	struct folder_info* getFolderInfo(const std::wstring& p_extension);
	std::wstring getDefaultPath(const std::wstring& p_file_extension);
	int32_t SeMessageBox(const wchar_t* msg, const wchar_t* title, int flags);

	/// Platform callback for displaying a modal message box (set by host on non-Windows).
	std::function<int32_t(const wchar_t* msg, const wchar_t* title, int flags)> messageBoxCallback;

	using MessageBoxCompletion = std::function<void(int32_t result)>;

	/// Show a message box WITHOUT blocking; onComplete may be empty for the
	/// informational case. Prefer this over SeMessageBox everywhere.
	///
	/// The blocking form cannot be honoured at all on a platform whose only native
	/// dialog is asynchronous, and satisfying it with a nested modal loop - which is
	/// what macOS does - is hostile under Wayland, where the client may be holding a
	/// popup grab the compositor expects it to service.
	void SeMessageBoxAsync(const wchar_t* msg, const wchar_t* title, int flags,
	                       MessageBoxCompletion onComplete = {});

	/// Implemented on IDialogHost::createStockDialog + IStockDialog::showAsync, so a
	/// host gets message boxes from the same seam that already serves file dialogs,
	/// menus and colour pickers - there is no separate message-box hook to implement.

  using FileDialogCompletion = std::function<void(int result, std::wstring filename)>;

	// The chosen path normally comes back RELATIVE to the type's default folder,
	// which is what an asset reference wants: a .wav or skin bitmap recorded
	// relative to the samples folder travels with the patch. A DOCUMENT is not an
	// asset - it is opened and saved by absolute path - so pass absolutePath=true
	// for those. (The Windows and macOS front ends never hit this: their document
	// choosers are native and hand back a full path already.)
    void FileDialogAsync(bool load_or_save, std::wstring extension, std::wstring filename, FileDialogCompletion onComplete, bool absolutePath = false);
	void FileDialogAsync(bool load_or_save, std::vector<std::wstring> extensions, std::wstring filename, FileDialogCompletion onComplete, bool absolutePath = false);
	CommandMgr* CommandManager()
	{
		return &m_command_manager;
	}
	void RefreshModuleData(bool refresh_sems, bool refresh_vsts, bool refresh_prefabs);
	bool LoadOrScanModuleData();

	void BuildCodeSkeleton(int32_t handle, const std::wstring moduleName);

	bool quiet = false;
	bool rescanIncludesVsts = false;
// on processor atm.	gmpi::hosting::QueuedUsers pendingQueueClients; // parameters waiting to be sent to Processor
};

std::wstring GetHomeDir();
void OpenWebPage(const std::wstring& p_web_url);
