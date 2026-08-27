#pragma once

#include "notify.h"
#include "Application.h"
#include "mp_gui.h"
#include "commandMgr.h"
#include "ElatencyContraintType.h"
#include "SynthRuntime_editor.h"
#include "TimerManager.h"
#include "IO_base.h"
#include "IMidiDriver.h"
#include "ModuleFactory_Editor.h"
#include "Hosting/message_queues.h"
#include "modules/se_sdk3_hosting/ProcessorWatchdog.h"
#include "BrowserFontSize.h"

class CSynthEditDocBase;
namespace SE2
{
struct feedbackPinUi;
}

void CopyInitialPrefabs();


// Application can't have a Timer due to destructor of members happening too late when exiting.
class AppTimerHelper : public se_sdk::TimerClient
{
	class CSynthEditAppBase* app = {};
public:
	AppTimerHelper(CSynthEditAppBase* papp) : app(papp)
	{
		StartTimer(24);
	}
	bool OnTimer() override;
};

// View menu: which pieces of chrome are on screen. Shared by all three front
// ends so the choice follows the user between them, and so a pane hidden on
// one platform is described by the same setting name on the others.
// All default on - the app looks as it always has until something is hidden.
//
// ApplicationSettings holds two of these: one for the main window, one shared by
// every torn-out window. A tab pulled out into its own window is usually there to
// give the structure more room, so it wants less chrome than the main window - see
// ApplicationSettings::MainWindowChrome / TearOutWindowChrome.
struct ChromeVisibility
{
	bool ShowModuleBrowser = true;
	bool ShowPropertiesBrowser = true;
	bool ShowToolbar = true;
	// The breadcrumb/thumbnail strip along the top of the editor (SE2::BreadcrumbBar).
	// Unlike the three above it is drawn inside the editor swapchain rather than
	// being a native widget, so hiding it means giving its TopStripLayout a zero
	// strip height - see the front ends' view-visibility code.
	bool ShowThumbnailBar = true;

	// 'prefix' namespaces the attributes: "" for the main window (those are the
	// original names, so settings files written before the split still load) and
	// "TearOut" for the torn-out windows.
	// 'defaults' supplies the value for an attribute that isn't in the file. The
	// 3-arg serializer form is required, not merely tidy: these default to true,
	// and the 2-arg overload restores T{} (false) when the attribute is absent.
	template< class Serializer >
	void Serialize(Serializer& s, const std::string& prefix, const ChromeVisibility& defaults)
	{
		s((prefix + "ShowModuleBrowser").c_str(), ShowModuleBrowser, defaults.ShowModuleBrowser);
		s((prefix + "ShowPropertiesBrowser").c_str(), ShowPropertiesBrowser, defaults.ShowPropertiesBrowser);
		s((prefix + "ShowToolbar").c_str(), ShowToolbar, defaults.ShowToolbar);
		s((prefix + "ShowThumbnailBar").c_str(), ShowThumbnailBar, defaults.ShowThumbnailBar);
	}
};

// see also SynthEditApp::InitInstance() for LOAD SETTINGS
struct ApplicationSettings
{
	bool SnapEnabled = true;
	bool UndoEnabled = true;
	bool LoadLastFile = true;
	bool GpuDisabled = false;
	bool DeepColorDisabled = false;
	// Debug-only: draw with gmpi_ui's CPU backend (the Linux renderer) instead of
	// Direct2D / CoreGraphics. Serialized in every build, honoured only in debug —
	// see Shared/SoftwareRendererOption.h.
	bool SoftwareRenderer = false;
	std::string ThemePreference = "SystemDefault"; // "SystemDefault", "Light", or "Dark"
	std::string DefaultLineStyle = "Straight"; // "Straight" or "Curved" — what a line's "Default" style resolves to

	// Text size in the Module Browser and Properties Browser: "Default", "Large" or
	// "Larger". One setting for both panes — see BrowserFontSize.h.
	std::string BrowserFontSize = "Default";

	std::vector<std::wstring> RecentFiles;

	std::string CurrentMidiOutDev;
	std::string CurrentMidiInDevs;
	std::wstring m_audio_output_guid;
	float sampleRate_{ 44100.f };
	int32_t AudioDriverBufferSizeMs{ 100 };
	int32_t /*ElatencyContraintType*/ latencyCompensation{ (int32_t) ElatencyContraintType::Full };

	// Vendor identity: who is shipping the plugin rather than what the plugin is.
	// Remembered per machine so it does not have to be retyped for every project.
	std::wstring Registration;
	std::string Vendor4charCode;
	std::wstring VendorWebsite;
	std::wstring VendorCopyright;

	std::wstring AudioPath;
	std::wstring MidiPath;
	std::wstring ModulePath;

	int ModuleBrowserWidth = 270;
	int PropertiesBrowserWidth = 270;

	// View menu chrome, kept independently for the main window and for the
	// torn-out windows (which share one set between them). See ChromeVisibility.
	// Front ends that have no torn-out windows use MainWindowChrome throughout.
	ChromeVisibility MainWindowChrome;
	ChromeVisibility TearOutWindowChrome;

	// Properties Browser label-column width, as a fraction (0..1) of the panel's
	// text area. Fractional (not px) so it scales when the panel itself is resized.
	// Adjusted by dragging the column divider in the Properties Browser.
	float PropertiesLabelColumnFraction = 0.4f;

	template< class Serializer >
	void Serialize(Serializer& s)
	{
		s("SnapEnabled", SnapEnabled);
		s("UndoEnabled", UndoEnabled);
		s("LoadLastFile", LoadLastFile);
		s("GpuDisabled", GpuDisabled);
		s("DeepColorDisabled", DeepColorDisabled);
		s("SoftwareRenderer", SoftwareRenderer);
		s("ThemePreference", ThemePreference);
		s("DefaultLineStyle", DefaultLineStyle);
		// 3-arg form: a settings file written before this attribute existed must restore
		// "Default", not the empty string the 2-arg overload leaves behind.
		s("BrowserFontSize", BrowserFontSize, std::string("Default"));

		s("Registration", Registration);
		s("Vendor4charCode", Vendor4charCode);
		s("VendorWebsite", VendorWebsite);
		s("VendorCopyright", VendorCopyright);
		s("RecentFiles", RecentFiles);

		s("AudioPath", AudioPath);
		s("MidiPath", MidiPath);
		s("ModulePath", ModulePath);

		s("AudioOutDevice"   , m_audio_output_guid);

		// 3-arg form: a settings file written before these attributes existed must
		// restore the real default, not the 0 the 2-arg overload leaves behind
		// (0 Hz / a 0 ms buffer would reach the audio drivers).
		s("SampleRate"       , sampleRate_, 44100.f);
		s("AudioDriverBufferSizeMs", AudioDriverBufferSizeMs, int32_t{100});

		s("LatencyCompensation", latencyCompensation);

		s("CurrentMidiInDevs", CurrentMidiInDevs);
		s("CurrentMidiOutDev", CurrentMidiOutDev);

		// Pane widths use the 3-arg form so missing attributes restore the
		// 270 default rather than collapsing to 0 (existing 2-arg overloads
		// reset to T{} on a missing attribute).
		s("ModuleBrowserWidth", ModuleBrowserWidth, 270);
		s("PropertiesBrowserWidth", PropertiesBrowserWidth, 270);
		s("PropertiesLabelColumnFraction", PropertiesLabelColumnFraction, 0.4f);

		// Order matters: the tear-out set falls back to whatever the main window
		// just loaded, so a settings file written before the two were split (or by
		// a front end that only knows about the main window) gives torn-out windows
		// the same chrome the user already had, rather than silently reinstating
		// panes they had hidden. Once the two differ the difference is written out.
		MainWindowChrome.Serialize(s, "", ChromeVisibility{});
		TearOutWindowChrome.Serialize(s, "TearOut", MainWindowChrome);
	}
};

// BACKLOG V4. Which slice of the module list the browser should offer.
//
// Jeff's ruling 2026-08-24: in the rack view only rack modules are relevant --
// "prefab from the rack folder, plus modules of a specific category", where the
// category is anything starting "Rack" (so Rack, Rack/VCV, Rack/Cardinal).
// Drilling into the structure view still offers everything.
//
// The TiDE category is deliberately NOT rack-relevant: Panel, knob and the two
// patch points are the parts a rack module is BUILT FROM, not things that stand
// alone in a rack. The two namespaces are disjoint on purpose, so a module never
// has to choose between being a rack item and being usable inside one.
enum class ModuleScope
{
	Everything,   // structure view -- every module and prefab, as before
	RackOnly,     // rack view
};

class CSynthEditAppBase : public gmpi::hosting::interThreadQueUser, public ApplicationBase
{
	friend class SeAudioMaster;
public:
	ApplicationSettings settings;

	CSynthEditAppBase();
	virtual ~CSynthEditAppBase()
	{
		assert(!has_observers());
	}
	virtual bool snapToGrid() { return false; }

	// Persist 'settings' to disk. Concrete apps (SynthEditApp / SynthEditAppCl) override
	// with a platform-specific writer; the base no-op lets cross-platform code (e.g. the
	// EditorLib PropertiesBrowser) request a save through the base pointer.
	virtual void SaveSettings() {}

	bool IsRegisteredVersion() override;												 // used by save as vst to enable extra features
	void reportFeedbackError(FeedbackTrace* feedbackTrace);
	void reportFeedbackErrorUi(std::list< std::pair<SE2::feedbackPinUi, SE2::feedbackPinUi> >& feedbackConnectors) override;
	bool ApplyHighlights(int flags, std::vector<class CUG*>* modules) override;
	gmpi::api::IDialogHost* getCurrentDialogHost() override;
	void onDocumentChanged() override;
	void OnRunPlay();
	void OnRunStop() override;
	// Blocks until the background audio thread finishes (e.g. a Wave
	// Recorder's Time Limit expires). Used by SynthEditCL to render a
	// full WAV before tearing down the engine.
	void WaitForRenderComplete();
	void OnToggleRun();
	void OpenView(CContainer* p_object, int view_flag) override;
	void CloseAllViews() override;
	void SetQuiet()
	{
		// Inhibit blocking UI (e.g. dialogs). For automated scenarios.
		quiet = true;
		SeMessageBox(L"Logging dialogs to stderr, and keeping them for --dialogs.\n", L"", 0);
	}
	float GetSampleRate();
	bool OnTimer() override;

	int32_t resolveFilename(const wchar_t* shortFilename, int32_t maxChars, wchar_t* returnFullFilename) override;
	std::wstring ResolveFilename(const std::wstring& name, const std::wstring& extension) override;
	std::string ShortenFilename(const std::string name, const std::string extension) override;

	int SnapPixels() override;
	virtual bool InitInstance();
	void ExitInstance();

	// Application
	void invalidateDsp() override { dspDirty = true; }

	void SetCancellationMode()
	{
		synthRuntime.SetCancellationMode();
	}
	void AnalyseCancellation(const std::wstring& filenameA, const std::wstring& filenameB);
	std::wstring getSettingString(const wchar_t* name) override;

	class ISEAppManaged* m_app_user_interface{};

	// standalone only
	bool dspDirty = true;

public:
	std::filesystem::path getLiveModuleUpdateStagingFolder();
	void OnLiveModuleUpdate();
	void UpdateLiveModules();
	void MonitorFileSystem(std::filesystem::path modulesFolder);

	void SetUndoEnabled(bool enabled);
	void SetGpuDisabled(bool disabled);
	void SetDeepColorDisabled(bool disabled);
	// No-op in release: the software renderer isn't compiled in there.
	void SetSoftwareRenderer(bool enabled);
	// Re-export & redraw any open structure views (e.g. after the "Default Line Style"
	// preference changes, so connector lines set to CLine2::DEFAULT_STYLE re-resolve).
	void refreshAllStructureViews();

	// Apply a new Browser Font Size preference: moves the global the two browser panes
	// take their metrics from, records it in settings, and rebuilds the open panes
	// (they size their rows in Body(), so a repaint alone would not pick it up).
	void setBrowserFontSize(SynthEdit::BrowserFontSize size);

	std::vector<class IO_output_info*> getAudioDriversInfo();

	ElatencyContraintType getLatencyCompensation()
	{
		return (ElatencyContraintType) settings.latencyCompensation;
	}
	// Structure-view lines set to CLine2::DEFAULT_STYLE follow this preference.
	bool defaultLinesCurved() override
	{
		return settings.DefaultLineStyle == "Curved";
	}
	void setLatencyCompensation(ElatencyContraintType l);
	IO_base* GetAudioDriver();
	void SetSampleRate(float rate);
	void SetLatency(int ms);
	void ReloadMenu() override;
	void OnSynthStopped();
	void DoImmediateRestartAsync();
	void GetRegistrationInfo(std::wstring& p_user_email, std::wstring& p_serial) override;
	std::string setVendor4charCodeSanitized(std::string p_code) override;
	std::string getVendor4charCode() override;
	bool SetRegistrationInfo(const std::wstring& p_vendor, const std::wstring& p_serial) override;
	std::wstring getVendorWebsite() override { return settings.VendorWebsite; }
	void setVendorWebsite(const std::wstring& p_url) override { settings.VendorWebsite = p_url; }
	std::wstring getVendorCopyright() override { return settings.VendorCopyright; }
	void setVendorCopyright(const std::wstring& p_text) override { settings.VendorCopyright = p_text; }
	void setTemporaryRegistration(const std::wstring& p_vendor, const std::wstring& p_serial);
	void UpdateUndoMenus(bool CanUndo, bool CanRedo, std::wstring undo_description, std::wstring redo_description);
#ifdef _WIN32
	HWND MainWindowhandle() override;
#endif
	void DeferredMessageBox(const wchar_t* msg, int flags) override;
	std::tuple<float, float> GetCpuLoad();
	void DoPreferences();
	void OnFileClose();
	virtual bool CloseDoc();
	void OnFileNew2();
	void OnDspMsg(int p_msg_id, gmpi::hosting::my_input_stream& p_stream);

	bool BuildSynth();
	void DoHelp(std::wstring p_url, int p_cmd = 0) override; // HH_DISPLAY_TOPIC

	void ExportModules(std::list< std::wstring >& moduleList, bool includePrefabs = true,
	                   ModuleScope scope = ModuleScope::Everything);

	// BACKLOG V4. The browser is a single app-level observer, and viewType lives
	// per-view on MfcDocPresenter -- so nothing told it which view was active, and
	// nothing woke it when the view CHANGED (its only trigger was ReloadMenu(),
	// i.e. the module list changing). Both gaps are why "just one filter" was not.
	ModuleScope m_browser_scope = ModuleScope::Everything;
	void setBrowserScope(ModuleScope scope);
	ModuleScope browserScope() const { return m_browser_scope; }
	void SetAudioOutput(const std::wstring& p_id);
	void DoExit();
	
	gmpi::hosting::IWriteableQue* MessageQueToDspOrNull() override;
	gmpi::hosting::QueuedUsers* PendingDspClients() override; // or null if processor not running

	int32_t sendSdkMessageToAudio(int32_t handle, int32_t id, int32_t size, const void* messageData) override;

	bool onQueMessageReady( int handle, int msg_id, gmpi::hosting::my_input_stream& p_stream ) override;
	std::wstring getVendor();
	bool SynthRunning();
	bool BackgroundThreadRunning() { return dspThreadRunning; }

	int Run();  // Bring the noise!

	SynthRuntime_editor synthRuntime;
	std::vector<std::unique_ptr<IO_base>> m_audio_drivers;
	std::unique_ptr<IMidiDriver> m_midi_driver;

	std::vector<driverInfo> getMidiDriversInfo();

protected:
	float medianCpu = 0.0f;
	float peakCpu = 0.0f;
	float cpuRunningAverage = 0.0f;
	float cpuConversionConstant = 0.0f;
	bool resetPeakCpu = false;
	bool liveModuleUpdateFlag = false;
	std::atomic<bool> immediateRestartFlag;
	bool dspThreadRunning = false; // for notification to the UI only.

	static const int timerPeriodMs = 24;
	ProcessorWatchdog processorWatchdog{timerPeriodMs};

	bool ignore_recursion; // in OnTimer()

	std::vector<class CUG*> highLightedModules_;
	
	static std::multimap<std::wstring, menuinfo> m_menu_to_module_map;
	
	AppTimerHelper* timerhelper = {};
};
