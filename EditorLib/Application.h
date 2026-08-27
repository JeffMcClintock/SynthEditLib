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
	// The vendor name hosts show as the plugin's developer (VST3 PFactoryInfo::vendor, and the
	// folder presets are filed under). Settable through the base so the export dialogs can offer
	// it without knowing which concrete app they are talking to.
	virtual bool SetRegistrationInfo(const std::wstring& /*p_vendor*/, const std::wstring& /*p_serial*/) { return false; }
	// Vendor website and copyright, remembered per machine. The document keeps its own
	// copy (that is what the exporters read); these are the remembered default the export
	// dialogs pre-fill from, so one vendor's details are typed once rather than per project.
	virtual std::wstring getVendorWebsite() { return {}; }
	virtual void setVendorWebsite(const std::wstring& /*p_url*/) {}
	virtual std::wstring getVendorCopyright() { return {}; }
	virtual void setVendorCopyright(const std::wstring& /*p_text*/) {}
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

	// Is the document's rack_mode flag the app's to decide rather than the user's?
	// TIDE *is* a rack, so it forces rackMode on every document it creates or
	// imports; offering the panel's "Rack Mode" toggle there would only let the
	// user break it. Full SynthEdit leaves it a per-project choice.
	virtual bool rackModeIsFixed() { return false; }

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

	// QUIET MODE. Set by -quiet (ParseSynthEditArgs -> ApplyConfigPreInit ->
	// SetQuiet). A prompt raised while this is on never becomes a window: it is
	// written to the log stream and KEPT, and the caller is answered MB_OK so
	// nothing blocks.
	//
	// Keeping them is the point. Suppressing a dialog without recording it is
	// how a real data loss goes unnoticed -- TIDE BACKLOG E48 lost 3,577 bytes
	// of a document and we only know because the dialog BLOCKED and a human
	// read it. Silence would have hidden that.
	bool quiet = false;

	// Every prompt diverted since launch, oldest first, in the order raised.
	//
	// A LIST RATHER THAN A CALLBACK, because the prompts that matter arrive
	// BEFORE anything is listening: E48's fired during session restore, before
	// the command channel existed. A callback registered later cannot be told
	// what it missed; a list can be read whenever the reader turns up.
	struct DivertedPrompt
	{
		std::string title;
		std::string text;
		int flags{};
		int answered{}; // what the caller was told, so a reader can see what the app then did
	};

	// Read and clear. Draining rather than peeking keeps a long-running app
	// from growing this without bound, and makes "what happened since I last
	// asked" the natural question.
	std::vector<DivertedPrompt> takeDivertedPrompts();

private:
	// Appends to divertedPrompts_ AND writes the message out. One function so
	// the two halves cannot drift -- a prompt that is recorded but not printed
	// is invisible to a shell-launched run, and one printed but not recorded is
	// invisible to the command channel.
	int32_t divertPrompt(const wchar_t* msg, const wchar_t* title, int flags);

	std::vector<DivertedPrompt> divertedPrompts_;

public:
	bool rescanIncludesVsts = false;
// on processor atm.	gmpi::hosting::QueuedUsers pendingQueueClients; // parameters waiting to be sent to Processor
};

std::wstring GetHomeDir();
void OpenWebPage(const std::wstring& p_web_url);
