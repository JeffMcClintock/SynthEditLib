#if defined( _WIN32 )
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h> // for OPENFILENAME
#include <io.h>
#include <direct.h>
#include <iostream>
#include "Shlobj.h"
#endif

#include <algorithm>
#include <chrono>
#include <filesystem>
#include "SynthEditAppBase.h"
#include "SynthEditDocBase.h"
#include "ISEAppManaged.h"
#include "UgDatabase.h"
#include "SuspendDSP.h"
#include "tinyxml/tinyxml.h"
#include "CContainer.h"
#include "MfcDocPresenter.h"
#include "SkinMgr.h"
#include "BundleInfo.h"
#include "SeException.h"

#include "modules/se_sdk3_hosting/GmpiResourceManager.h"
#include "UniqueSnowflake.h"
#include "ui_msg_target.h"
#include "CUG.h"
#include "it_doc_ob_recursive.h"
#include "CancellationAnalyse.h"
#include "backends/DrawingFrameWin.h"
#include "Shared/SoftwareRendererOption.h"
#include "Hosting/message_queues.h"
#include "PatchParameter.h"
#include "PresenterCommands.h"

using namespace gmpi::hosting;

bool AppTimerHelper::OnTimer()
{
	return app->OnTimer();
}

using namespace std;
namespace fs = std::filesystem;

std::multimap<std::wstring, menuinfo> CSynthEditAppBase::m_menu_to_module_map;

CSynthEditAppBase::CSynthEditAppBase() :
	ignore_recursion(false)
	,synthRuntime(this)
{
#if 0
	/* Attach to parent console if available so output will be visible */
	if (AttachConsole(ATTACH_PARENT_PROCESS)) {

		/* make sure stdout is not already redirected before redefining */
		if (_fileno(stdout) == -1 || _get_osfhandle(fileno(stdout)) == -1)
			freopen("CON", "w", stdout);
	}
#endif
	TiXmlBase::SetCondenseWhiteSpace(false); // ensure text parameters preserve multiple spaces. e.g. "A     B" (else it collapses to "A B")
}

void CSynthEditAppBase::ExitInstance()
{
	delete timerhelper;
	timerhelper = {};
	synthRuntime.Clear(); // fix crash on exit by destroying stuff BEFORE the destructor.

#if 0
	FreeConsole();
#endif

	SkinMgr::Instance()->UnloadSkins(); // trying to fix crash at shutdown.
}

bool CSynthEditAppBase::InitInstance()
{
	// TODO!!!
	// on mac, skins folder is: /Users/jeffmcclintock/SynthEdit Projects/skins
	// but export folder is: /Users/jeffmcclintock/Documents/SynthEdit Projects/
	// which feels complicated and inconsistent/
	{
		auto& resourceFolders = GmpiResourceManager::Instance()->resourceFolders;

		resourceFolders[GmpiResourceType::Midi] = getDefaultPath(L"mid");
		resourceFolders[GmpiResourceType::Image] = SkinMgr::Instance()->SkinFolder();
		resourceFolders[GmpiResourceType::Audio] = getDefaultPath(L"wav");
		resourceFolders[GmpiResourceType::Soundfont] = getDefaultPath(L"sf2");
	}

	// set up default folder paths
	refreshFolderLocations();

	timerhelper = new AppTimerHelper(this);

	return true;
}

void CSynthEditAppBase::onDocumentChanged()
{
	// Set up processor watchdog callback when document becomes available
	if (Document())
	{
		processorWatchdog.setCallback([this](bool isOffline) {
			if (auto masterContainer = Document()->MasterContainer; masterContainer)
			{
//				_RPTN(0, "ProcessorWatchdog: HC_PROCESSOR_OFFLINE is now %s\n", isOffline ? "OFFLINE" : "ONLINE");
				const auto offlineParam = masterContainer->get_patch_manager()->GetHostGeneratedParameter(HC_PROCESSOR_OFFLINE, true, masterContainer);
				offlineParam->SetValue(RawView(isOffline));
			}
		});
	}
	else
	{
		// Clear callback when document is destroyed
		processorWatchdog.setCallback({});
	}
}

// This version directly compatible with SE SDK.
int32_t CSynthEditAppBase::resolveFilename(const wchar_t* shortFilename, int32_t maxChars, wchar_t* returnFullFilename)
{
	auto full_filename = ResolveFilename(std::wstring(shortFilename), L"");

	if (full_filename.size() >= maxChars)
	{
		// return empty string (if room).
		if (maxChars > 0)
			returnFullFilename[0] = 0;

		return gmpi::MP_FAIL;
	}

	WStringToWchars(full_filename, returnFullFilename, maxChars);
	return gmpi::MP_OK;
}

bool isSkinFile(const std::wstring& ext)
{
	return ext == L"bmp" || ext == L"png" || ext == L"jpg" || ext == L"svg";
}

// Matches "scheme://..." per RFC 3986: scheme starts with a letter, then letters/digits/+/-/.
bool isAbsoluteUrl(const std::wstring& s)
{
	const auto pos = s.find(L"://");
	if (pos == std::wstring::npos || pos == 0)
		return false;

	auto isAlpha = [](wchar_t c) { return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z'); };
	if (!isAlpha(s[0]))
		return false;

	for (size_t i = 1; i < pos; ++i)
	{
		const auto c = s[i];
		const bool ok = isAlpha(c) || (c >= L'0' && c <= L'9') || c == L'+' || c == L'-' || c == L'.';
		if (!ok)
			return false;
	}
	return true;
}

std::wstring CSynthEditAppBase::ResolveFilename(const std::wstring& name, const std::wstring& extension)
{
	// URLs (http://, file://, etc.) are absolute references — return unmolested.
	if (isAbsoluteUrl(name))
		return name;

	fs::path filepath(name);

	// Attempt to determine file type. First by supplied extension, then by examining filename.
	auto fileExt = filepath.has_extension() ? filepath.extension().wstring().substr(1) : std::wstring{};

	std::wstring fileType = extension.empty() ? fileExt : extension;

	// Add stock file extension if filename has none.
	if (fileExt.empty())
	{
		if (extension.empty())
			return name; // BUG: on x64 a blank filename returns plugin directory, on x86 nothing ("")

		filepath.replace_extension(extension);
	}

	// need to add path? (a Windows-authored project opened on macOS still holds "C:\..."
	// references, which is_absolute() alone would take for relative names)
	if (!isAbsolutePathAnyPlatform(filepath.wstring()))
	{
		if (isSkinFile(fileType))
		{
			// Delegate to GmpiResourceManager which knows about project-specific skin folders
			filepath = GmpiResourceManager::Instance()->ResolveResourceUri(filepath, L"default");
		}
		else
		{
			// Project-specific resources folder first (e.g. "mysynth.resources/"), if the
			// file actually exists there. Otherwise fall back to the user-configured folder
			// for this file type (Audio, MIDI, SoundFont, etc.).
			if (auto prf = GmpiResourceManager::Instance()->projectResourcesFolder(); !prf.empty())
			{
				auto candidate = prf / filepath;
				if (fs::exists(candidate))
					return candidate.wstring();
			}
			filepath = getDefaultPath(fileType) / filepath;
		}
	}

	return filepath.wstring();
}


std::string CSynthEditAppBase::ShortenFilename(std::string name, std::string extension)
{
	fs::path path(name);

	// figure out the extension.
	auto ext = path.has_extension() ? path.extension().wstring().substr(1) : Utf8ToWstring(extension);

	if (isSkinFile(ext))
	{
		// Delegate to GmpiResourceManager which knows about project-specific skin folders
		return GmpiResourceManager::Instance()->ShortenResourceUri(name);
	}

	// Project-specific resources folder (e.g. "mysynth.resources/") — store as bare
	// filename so the document remains portable when moved alongside its .resources folder.
	if (auto prf = GmpiResourceManager::Instance()->projectResourcesFolder(); !prf.empty())
	{
		if (name.starts_with(prf.string()))
			return path.filename().string();
	}

	const auto defaultFolder = getFolderInfo(ext)->current_folder;
	if (!defaultFolder.empty())
	{
		const auto relativePath = fs::relative(path, defaultFolder);
		if (!relativePath.empty() && relativePath.begin()->string() != "..")
			return relativePath.string();
	}

	return path.string();
}

void CSynthEditAppBase::OnRunStop()
{
	synthRuntime.Stop();
}

void CSynthEditAppBase::WaitForRenderComplete()
{
	// OnRunPlay() detaches the audio render thread and returns immediately;
	// we have to wait for that thread to finish naturally (typically when a
	// Wave Recorder's Time Limit elapses and calls end_run()). dspThreadRunning
	// is flipped false when the UI thread processes the "done" message the
	// audio thread sends at the end of RunGenerator, after the engine has
	// fully torn down — so we must keep pumping OnTimer() to drain the
	// DSP→UI queue, otherwise the audio thread blocks on a full queue and
	// never reaches "done".
	while (BackgroundThreadRunning())
	{
		OnTimer();
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
}

void CSynthEditAppBase::OnToggleRun() // only nesc in standalone !!
{
	if(SynthRunning())
	{
		OnRunStop();
	}
	else
	{
		OnRunPlay();
	}
}

// WARNING. Only DS Samplerate, don't account for Oversampling.
float CSynthEditAppBase::GetSampleRate()
{
	return settings.sampleRate_;
}

gmpi::hosting::IWriteableQue* CSynthEditAppBase::MessageQueToDspOrNull()
{
	if (synthRuntime.SynthRunning())
	{
		return synthRuntime.MessageQueToDsp();
	}
	return {};
}

gmpi::hosting::QueuedUsers* CSynthEditAppBase::PendingDspClients()
{
	if (synthRuntime.SynthRunning())
	{
		return &synthRuntime.pendingProcessorQueueClients;
	}
	return {};
}

int32_t CSynthEditAppBase::sendSdkMessageToAudio(int32_t handle, int32_t id, int32_t size, const void* messageData)
{
	return synthRuntime.sendSdkMessageToAudio(handle, id, size, messageData);
}

bool CSynthEditAppBase::onQueMessageReady( int handle, int msg_id, my_input_stream& p_stream )
{
	// Processor watchdog.
	processorWatchdog.onDspMessage();

	// _RPTW2(_CRT_WARN, L"OnDspMsg: id = %S, handle = %x \n", long_to_id(msg_id).c_str(), handle );
	if( handle == UniqueSnowflake::APPLICATION )
	{
		OnDspMsg( msg_id, p_stream );
		return true;
	}
	else
	{
		auto target = dynamic_cast<ui_msg_target*>(Document()->uniqueIdDatabase.HandleToObjectWithNull(handle) );
		if( target )
		{
			target->OnDspMsg( msg_id, p_stream );
			return true;
		}
	}

	return false;
}

bool CSynthEditAppBase::ApplyHighlights(int flags, std::vector<class CUG*>* modules)
{
	bool foundSomeLine{};
	CUG* prev{};

	auto& moduleList = modules ? *modules : highLightedModules_;

	for (auto u : moduleList)
	{
		if (u)
		{
			if (prev)
				foundSomeLine |= prev->HighlightLineTo(u, flags);

			prev = u;
		}
	}

	if(modules)
		highLightedModules_.insert(highLightedModules_.end(), modules->begin(), modules->end()); // add to the list of highlighted modules.

	// erase any highLightedModules_ with no error flags.
	std::erase_if(highLightedModules_, [](const auto& u) { return !u->getErrorFlags(); });

	return foundSomeLine;
}

gmpi::api::IDialogHost* CSynthEditAppBase::getCurrentDialogHost()
{
	if (m_app_user_interface)
	{
		return m_app_user_interface->getCurrentDialogHost();
	}
	return nullptr;
}

void CSynthEditAppBase::reportFeedbackError(FeedbackTrace* e)
{
	e->DebugDump();

	std::wstring highlighted;
	std::vector<class CUG*> feedbackModules;

	// Gather the handles.
	for (auto& it : e->feedbackConnectors)
	{
		auto u = dynamic_cast<CUG*>(Document()->uniqueIdDatabase.HandleToObjectWithNull(it.second.moduleHandle));
		if (u)
			feedbackModules.push_back(u);
	}
	feedbackModules.push_back(feedbackModules[0]); // repeat the first one to create a circle.

	const auto highlightedSomeModule = ApplyHighlights(PinHighlightFlag_Feedback, &feedbackModules);

	if (highlightedSomeModule)
	{
		highlighted = L" (highlighted in red)";

		highLightedModules_.back()->Locate();
	}

	std::wostringstream oss;
	if (e->reason_ == SE_FEEDBACK_TO_NOTESOURCE)
	{
		oss << L"This patch contains a Polyphonic path between two MIDI-CVs (or similar)" << highlighted << L".  Please remove.";
	}
	else
	{
		oss << L"This patch contains a FEEDBACK path" << highlighted << L".  Please remove, or consider using a 'Feedback' module.";
	}
	oss << L"\nSee 'Tutorials - Tricks and Traps' in the Help File <F1>.";

	SeMessageBox(oss.str().c_str(), L"", MB_OK);
}

void CSynthEditAppBase::reportFeedbackErrorUi(std::list< std::pair<SE2::feedbackPinUi, SE2::feedbackPinUi> >& feedbackConnectors)
{
	std::wstring highlighted;

	// Gather the handles.
	std::vector<class CUG*> feedbackModules;
	for (auto& it : feedbackConnectors)
	{
		auto u = dynamic_cast<CUG*>(Document()->uniqueIdDatabase.HandleToObjectWithNull(it.second.moduleHandle));
		if(u)
			feedbackModules.push_back(u);
	}
	feedbackModules.push_back(feedbackModules[0]); // repeat the first one to create a circle.

	const auto highlightedSomeModule = ApplyHighlights(PinHighlightFlag_UiFeedback, &feedbackModules);

	if (highlightedSomeModule)
	{
		highlighted = L" (highlighted in red)";

		highLightedModules_.back()->Locate();
	}

	std::wostringstream oss;
	//if (e->reason_ == SE_FEEDBACK_TO_NOTESOURCE)
	//{
	//	oss << L"This patch contains a Polyphonic path between two MIDI-CVs (or similar)" << highlighted << L".  Please remove.";
	//}
	//else
	{
		oss << L"This patch contains a FEEDBACK path" << highlighted << L".  Please remove, or consider using a 'Feedback' module.";
	}
	oss << L"\nSee 'Tutorials - Tricks and Traps' in the Help File <F1>.";

	//	SeMessageBox(oss.str().c_str(), L"", MB_OK);}
}

int CSynthEditAppBase::SnapPixels()
{
	if( snapToGrid() )
	{
		return 8;
	}

	return 1;
}

int CSynthEditAppBase::Run()
{
	ApplyHighlights(~PinHighlightFlag_Feedback, nullptr);

	// _RPT0(_CRT_WARN, "CSynthEditAppBase::Run()\n" );
	// 3 Possible conditions:
	// 1) synth completely stopped, no generator. Build generator and start BG thread
	// 2) synth stopped, but generator not destroyed yet. Call stop() to destroy generator, then proceed.
	// 3) Synth Running.  Call stop() to stop BG thread, and destroy generator, then proceed.

	synthRuntime.Stop();

	if( !BuildSynth() )
    {
        std::cout << "Run(): Failed to BuildSynth()"<< std::endl;
        return 0;
    }
    
	dspThreadRunning = true;
	synthRuntime.StartBackgroundProcessing();

	return 0;
}

bool CSynthEditAppBase::SynthRunning()
{
	return synthRuntime.SynthRunning();
}

std::wstring CSynthEditAppBase::getVendor()
{
	std::wstring user_email, serial;
	GetRegistrationInfo(user_email, serial);

	if( !user_email.empty() )
	{
		return user_email;
	}
	else
	{
		return (L"SynthEdit www.synthedit.com");
	}
}

bool CSynthEditAppBase::IsRegisteredVersion()
{
#if 1
	return true;
#else
	//	create_key();
	std::wstring user_email, serial;
	GetRegistrationInfo(user_email, serial);

	if( verify_signature( WStringToUtf8(user_email), WStringToUtf8(serial), IDR_BLOB2 ) )
	{
		_RPT0(_CRT_WARN, "NEW SERIAL\n" );
		return true;
	}

	return false;
#endif
}

void CSynthEditAppBase::AnalyseCancellation(const std::wstring& filenameA, const std::wstring& filenameB)
{
	CancellationAnalyse(this, filenameA, filenameB);
}


///////////////////////////////////

fs::path CSynthEditAppBase::getLiveModuleUpdateStagingFolder()
{
	return fs::path(getPlatformPluginsFolder()) / L"SynthEdit" / L"modules-staged";
}

void CSynthEditAppBase::OnLiveModuleUpdate() // !! called from background thread !!!
{
//	_RPT0(_CRT_WARN, "MODULE CHANGE!!\n");
	liveModuleUpdateFlag = true; // see: UpdateLiveModules()
}

void CSynthEditAppBase::UpdateLiveModules()
{
	std::vector<fs::path> modules;

	// gather module paths from live-update folder.
	const auto stagingDir = getLiveModuleUpdateStagingFolder();
	std::error_code ec;
	for (const auto& entry : fs::directory_iterator(stagingDir, ec))
	{
		if (ec)
			break;

		if (!entry.is_regular_file())
			continue;

		const auto extension = entry.path().extension();
		if (extension == L".sem" || extension == L".gmpi")
			modules.push_back(entry.path().filename());
	}

	// do not interrupt cancellation
	if (modules.empty() || synthRuntime.cancellationMode)
		return;

	const auto processorRunning = synthRuntime.SynthRunning();
	if(processorRunning)
		OnRunStop();

	// leave views open but close contents, esp sems
	if (m_app_user_interface) // null at startup
		m_app_user_interface->CloseAllViews();

	for (const auto& filename : modules)
	{
		const auto sourceFilename = stagingDir / filename;

		// Unload existing sem
		fs::path destPath = UnloadDll(filename.wstring());

		if (destPath.empty())
			destPath = fs::path(getSettingString(L"ModulePath")) / filename;

		// move new sem over existing. A module held open by another process (a running
		// DAW, or a plugin scanner) fails with a sharing violation, which must not be
		// fatal - leave it staged and retry on the next update.
		std::error_code copyError;
		if (!fs::copy_file(sourceFilename, destPath, fs::copy_options::overwrite_existing, copyError))
			continue;

		fs::remove(sourceFilename, copyError);

		// set aside the old module descriptions, then rescan the new dll into the database.
		SetAsidePluginData(destPath);
		ScanFile(L"", destPath);
	}

	// keep any old descriptions possibly needed by current project.
	RetainMissingModuleDescriptions();

	if (Document() && Document()->MasterContainer)
	{
		// flag set-aside descriptions whose rescanned replacement has a different pin layout.
		// AdjustModuleTypePointer can't re-point those (pin descriptions no longer match);
		// the placed modules must be replaced wholesale instead, same as upgrade-on-load.
		for (auto& [id, oldInfo] : m_in_use_old_module_list)
		{
			if (auto newInfo = ModuleFactory()->GetById(id); newInfo && !isCompatibleWith(oldInfo.get(), newInfo))
				oldInfo->m_incompatible_with_current_module = true;
		}

		// point document to new descriptions before the old ones are deleted.
		Document()->MasterContainer->AdjustModuleTypePointer();

		// replace modules left behind on an incompatible description.
		it_doc_ob_recursive it(Document()->MasterContainer);
		for (it.First(); !it.IsDone(); it.Next())
		{
			if (auto ug = dynamic_cast<CUG*>(it.CurrentItem()); ug && ug->getType()->m_incompatible_with_current_module)
				Document()->m_upgrade_replace_modules.push_back(ug);
		}
		Document()->UpGradeIncompatibleModules();
	}

	// remove old descriptions.
	DeleteTemporaryModuleDescriptions();

	// refresh all views.
	if (m_app_user_interface) // null at startup
		m_app_user_interface->ReloadAllViews();

	// module descriptions may have changed: rebuild the module browser menu and refresh the on-disk cache.
	ReloadMenu();
	StoreModuleData();

	if(processorRunning)
		OnRunPlay();
}

void CSynthEditAppBase::MonitorFileSystem(std::filesystem::path modulesFolder)
{
#ifdef _WIN32
	DWORD dwWaitStatus;
	HANDLE dwChangeHandle;

	// Watch the directory for file creation and deletion.

	dwChangeHandle = FindFirstChangeNotification(
		modulesFolder.c_str(),         // directory to watch 
		FALSE,                         // do not watch subtree 
		FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE); // watch file name changes (plus creation/deletion)

	if (dwChangeHandle == INVALID_HANDLE_VALUE)
	{
		printf("\n ERROR: FindFirstChangeNotification function failed.\n");
		// ExitProcess(GetLastError());
		return;
	}

	// Make a final validation check on our handles.
	if ((dwChangeHandle == nullptr))
	{
		printf("\n ERROR: Unexpected nullptr from FindFirstChangeNotification.\n");
		return;
	}

	// Change notification is set. Now wait on both notification 
	// handles and refresh accordingly. 

	while (true)
	{
		// Wait for notification.

		// printf("\nWaiting for notification...\n");

		dwWaitStatus = WaitForSingleObject(dwChangeHandle, INFINITE);

		switch (dwWaitStatus)
		{
		case WAIT_OBJECT_0:

			// A file was created, renamed, or deleted in the directory.
			// Refresh this directory and restart the notification.

			OnLiveModuleUpdate();
			if (FindNextChangeNotification(dwChangeHandle) == FALSE)
			{
				printf("\n ERROR: MonitorFileSystem() - FindNextChangeNotification function failed.\n");
				//ExitProcess(GetLastError());
				return;
			}
			break;

		case WAIT_TIMEOUT:

			// A timeout occurred, this would happen if some value other 
			// than INFINITE is used in the Wait call and no changes occur.
			// In a single-threaded environment you might not want an
			// INFINITE wait.

			printf("\nNo changes in the timeout period.\n");
			break;

		default:
			printf("\n ERROR: Unhandled dwWaitStatus.\n");
			return;
			break;
		}
	}
#endif
}

void CSynthEditAppBase::SetSampleRate(float rate)
{
	settings.sampleRate_ = rate;
//	saveSetting(L"SRATE", static_cast<int>(rate));
}

void CSynthEditAppBase::SetLatency(int ms)
{
//	saveSetting(L"Latency", ms);

	settings.AudioDriverBufferSizeMs = ms;

	// only affects directsound driver
	for (auto& driver : m_audio_drivers)
	{
		driver->setLatency_ms(ms);
	}
}

void CSynthEditAppBase::setLatencyCompensation(ElatencyContraintType l)
{
	settings.latencyCompensation = (int32_t) l;
//	saveSetting(L"latencyCompensation", (int)l);
}

void CSynthEditAppBase::OnRunPlay()
{
//	CSynthEditAppBase::OnRunPlay();
	if (Document()) // might be closed
	{
		Run();
	}

	const wstring EngineRunning(L"EngineRunning");
	m_app_user_interface->AppPropertyChanged(EngineRunning);
}

void CSynthEditAppBase::OnFileNew2()
{
	if (CloseDoc())
		return;

	createNewDocument();
	
	Document()->SetApplication(this);
	Document()->OnNewDocument();

	m_app_user_interface->OpenView(
		Document()->MasterContainer, CF_STRUCTURE_VIEW);
}

void CSynthEditAppBase::OpenView(CContainer* p_object, int view_flag)
{
	p_object->ViewOpenFlags |= view_flag;
	m_app_user_interface->OpenView(p_object, view_flag/*, p_object->GetName(), 0*/);
}

void CSynthEditAppBase::CloseAllViews()
{
	if (m_app_user_interface)
		m_app_user_interface->CloseAllViews();
}

bool CSynthEditAppBase::CloseDoc()
{
	bool ret = false;

	// Consolidate any torn-out tab windows back into the main window before the
	// document's containers are destroyed, so their view observers unregister
	// while the document is still intact (else container destructors notify into
	// freed presenters -> AV). No-op for headless/CL hosts. Deliberately NOT in
	// CloseAllViews(), which the SEM-reload path also calls and which must keep
	// torn-out windows open.
	if (m_app_user_interface)
		m_app_user_interface->CloseTornOutWindows();

	if (Document())
	{
		if (quiet && Document()->isModified()) // suppress 'save changes?' dialog.
		{
//			_RPT0(0, "'Save Changes?' - SUPPRESSED by /quiet argument.");
			Document()->SetModified(false);
		}

		ret = Document()->doCloseDoc();
	}

	return ret;
}

void CSynthEditAppBase::DoExit()
{
	//	OnAppExit(); // protected so call indeirect.
	// Ensure Document closed before App, else Doc tries to access app object.
	CloseDoc();
	//	ExitInstance();
	m_app_user_interface = 0;
}

void CSynthEditAppBase::SetUndoEnabled(bool enabled)
{
	CommandManager()->m_enable_undo = enabled;

	if(Document())
		CommandManager()->ClearHistory();
}

void CSynthEditAppBase::SetGpuDisabled(bool disabled)
{
#ifdef _WIN32
	gmpi::hosting::tempSharedD2DBase::m_disable_gpu = disabled;
#endif
}

void CSynthEditAppBase::SetDeepColorDisabled(bool disabled)
{
#ifdef _WIN32
	gmpi::hosting::tempSharedD2DBase::m_disable_deep_color = disabled;
#endif
}

void CSynthEditAppBase::SetSoftwareRenderer(bool enabled)
{
	// Cross-platform by construction: se::setSoftwareRendererEnabled is an empty
	// inline in release, so no #ifdef _WIN32 dance like the two setters above.
	se::setSoftwareRendererEnabled(enabled);
}

void CSynthEditAppBase::OnSynthStopped()
{
	m_app_user_interface->AppPropertyChanged(wstring(L"EngineRunning"));
}

void CSynthEditAppBase::ReloadMenu()
{
	m_menu_to_module_map.clear(); // will rebuild list next time menu used.
	NotifyFast(OM_UPDATE_MODULE_BROWSER);
}

void CSynthEditAppBase::DoImmediateRestartAsync()
{
	immediateRestartFlag.store(true, std::memory_order_relaxed);
}

bool CSynthEditAppBase::OnTimer()
{
	//#if defined( _DEBUG )
	//	static FrameRateLogger l;
	//	l.OnFrame();
	//#endif

		/*
		//TODO
		if( SynthRunning() )
		{
			if( cpu_update_counter-- < 0 )// update cpu load
			{
				cpu_update_counter = 20;
				((CMainFrame*)m_pMainWnd)->SetCpuLoad(GetCpuLoad());
			}
		}
		*/

	if (liveModuleUpdateFlag)
	{
		liveModuleUpdateFlag = false;

		UpdateLiveModules();
	}

	// if any code called from here throws up a message box, Windows will
	// recursivly call back in here (WM_TIMER to dialog), causes problems for dsp_to_ui_que::pollMessage()
	if (ignore_recursion)
		return true;

	ignore_recursion = true;

	synthRuntime.serviceQueues();

	if (dspDirty)
	{
		dspDirty = false;

		if (SynthRunning())
		{
			int realtime_flags = 0;
			Document()->MasterContainer->GetTimingRequirements(realtime_flags);

			// if realtime flags changed, choose and restart audio driver !!!
			if (realtime_flags != synthRuntime.realtime_flags)
			{
				Run(); // full restart
			}
			else
			{
//				_RPT0(0, "SOFT restart DSP\n");

				// Serialise DSP graph to XML.

				// Create empty XML Document.
				auto doc2 = std::make_unique<TiXmlDocument>();
				doc2->LinkEndChild(new TiXmlDeclaration("1.0", "", ""));

				auto document_element = new TiXmlElement("Document");
				doc2->LinkEndChild(document_element);

				auto element = new TiXmlElement("DSP");
				document_element->LinkEndChild(element);

				CModuleFactory::Instance()->ClearSerialiseFlags();
				Document()->MasterContainer->ExportXml(element, SAT_SYNTHEDIT_DSP);

				synthRuntime.pendingDspXml = std::move(doc2);

				synthRuntime.DoAsyncRestart();
			}
		}
	}

	processorWatchdog.onTimerTick();

	ignore_recursion = false;
	return true;
}

std::vector<IO_output_info*> CSynthEditAppBase::getAudioDriversInfo()
{
	std::vector<IO_output_info*> ret;

	for(auto& driver : m_audio_drivers)
	{
		driver->MakeDriverList();
		for (auto& info : driver->m_driver_list)
		{
			ret.push_back(info);
		}
	}

	return ret;
}

std::vector<driverInfo> CSynthEditAppBase::getMidiDriversInfo()
{
	if (m_midi_driver)
		return m_midi_driver->GetDriverList();

	return {};
}

IO_base* CSynthEditAppBase::GetAudioDriver()
{
	auto audioDrivers = getAudioDriversInfo();
	for (auto& io : audioDrivers)
	{
		if (io->getUniqueID() == settings.m_audio_output_guid)
		{
			return io->Driver();
		}
	}

	return {};
}

void CSynthEditAppBase::SetAudioOutput(const std::wstring& p_id)
{
	settings.m_audio_output_guid = p_id;
//	saveSetting( (L"Audio Out GUID"), p_id.c_str()); // not in VST.
}

bool CSynthEditAppBase::SetRegistrationInfo(const std::wstring& p_vendor, const std::wstring& p_serial)
{
#ifndef COMPILE_DEMO_VERSION

	if (settings.Registration != p_vendor)
	{
		settings.Registration = p_vendor;
		// registration_key = p_serial;
	}

	return true;
#else
	return false;
#endif
}

void CSynthEditAppBase::setTemporaryRegistration(const std::wstring& p_vendor, const std::wstring& p_serial)
{
	settings.Registration = p_vendor;
//	registration_key = p_serial;
}

void CSynthEditAppBase::GetRegistrationInfo(std::wstring& p_user_email, std::wstring& p_serial)
{
#ifdef COMPILE_DEMO_VERSION
	p_user_email = L"";
	p_serial = L"";
#else
	p_user_email = settings.Registration;
//	p_serial = registration_key;
#endif
}

std::string SanitizeVendor4charCode(std::string code, std::string vendorName)
{
	// determin if 'code' has any non-alphanumeric characters.
	bool hasNonAlphaNumeric = false;
	bool hasCapital = false;
	for (auto c : code)
	{
		if (!isalnum(c))
		{
			hasNonAlphaNumeric = true;
			break;
		}
		if (isupper(c))
		{
			hasCapital = true;
		}
	}

	bool regenerate = code.size() != 4 || hasNonAlphaNumeric || !hasCapital;

	if (regenerate)
	{
		code = vendorName;

		// Remove non-alpha characters and spaces.
		code.erase(std::remove_if(code.begin(), code.end(), [](char c) { return !isalpha(c); }), code.end());

		// Remove excess vowels
		while (code.size() > 4)
		{
			auto p = code.find_last_of("aeiou");
			if (p != std::string::npos)
			{
				code.erase(p, 1);
			}
			else
			{
				break;
			}
		}

		// ensure it's at *least* 4-chars long.
		code += "xxxx";
		code = code.substr(0, 4);

		// Manufacturer codes must contain at least one uppercase character.
		if (!isupper(code[0]) && !isupper(code[1]) && !isupper(code[2]) && !isupper(code[3]))
			code[0] = static_cast<char>(toupper(code[0]));
	}

	return code;
}

std::string CSynthEditAppBase::setVendor4charCodeSanitized(std::string p_code)
{
	settings.Vendor4charCode = SanitizeVendor4charCode(p_code, WStringToUtf8(getVendor()));

//	WriteProfileString(SYNTHEDIT_REGISTRY_KEY, L"Manufacturer_4char", Utf8ToWstring(code).c_str());
//	saveSetting(L"Manufacturer_4char", Utf8ToWstring(code).c_str());

	return settings.Vendor4charCode;
}

std::string CSynthEditAppBase::getVendor4charCode()
{
	return SanitizeVendor4charCode(settings.Vendor4charCode, WStringToUtf8(getVendor()));
}

std::wstring CSynthEditAppBase::getSettingString(const wchar_t* name)
{
	if (wcscmp(name, L"AudioPath") == 0)
	{
		return settings.AudioPath;
	}

	if (wcscmp(name, L"MidiPath") == 0)
	{
		return settings.MidiPath;
	}

	if (wcscmp(name, L"ModulePath") == 0)
	{
		return settings.ModulePath;
	}

	return ApplicationBase::getSettingString(name);
}

void CSynthEditAppBase::refreshAllStructureViews()
{
	// Broadcast to the open structure-view presenter(s). MfcDocPresenter::OnNotify maps
	// OM_REFRESH_PRESENTERS to DirtyView(), which re-exports the view JSON and so re-runs
	// CLine2::Export — re-resolving any DEFAULT_STYLE lines against the current preference.
	if (Document() && Document()->MasterContainer)
		Document()->MasterContainer->NotifyAllViews2(OM_REFRESH_PRESENTERS);
}

void CSynthEditAppBase::DoHelp(std::wstring help_url, int p_cmd)
{
	help_url = GetHomeDir() + L"synthedit.chm::/" + help_url;
	OpenWebPage(help_url);
}

std::tuple<float, float> CSynthEditAppBase::GetCpuLoad()
{
	resetPeakCpu = true;
	return { medianCpu, peakCpu };
}

void CSynthEditAppBase::OnDspMsg(int p_msg_id, my_input_stream& p_stream)
{
	switch (p_msg_id)
	{
	case code_to_long('d', 'o', 'n', 'e'): // "done" background thread has exited.
	{
		dspThreadRunning = false;

//		_RPT0(0, "CSynthEditApp::OnDspMsg - done\n");
		// Step 2 - Rebuild DSP and re-engage processing.
		if (synthRuntime.buildFailed())
			reportFeedbackError(&synthRuntime.feedbackTrace);

		OnSynthStopped(); // update power button. (for when stopped by module, not user)

		// ASIO driver requested immediate restart (no time for fade-down)
		if (immediateRestartFlag.exchange(false, std::memory_order_relaxed))
		{
			// io_manager and audiomaster have already stopped and been deleted. Restart audio.
			OnRunPlay();
		}
	}
	break;

	case code_to_long('c', 'p', 'u', 't'): // "cput" (total CPU).
	{
		uint16_t cpuConsumption[CPU_BATCH_SIZE]; // per block
		p_stream.Read(&cpuConsumption, sizeof(cpuConsumption));

		// leave peak CPU until meter reads it, then we can reset it.
		if (resetPeakCpu)
		{
			resetPeakCpu = false;
			peakCpu = 0.0f;
		}

		for (auto& c : cpuConsumption)
		{
			float cpu = c * cpuConversionConstant;

			peakCpu = (std::max)(cpu, peakCpu);

			cpuRunningAverage += (cpu - cpuRunningAverage) * 0.1f; // rough running average.
			medianCpu += (float)copysign(cpuRunningAverage * 0.005f, cpu - medianCpu);
		}
	}
	break;
	case code_to_long('m', 'b', 'o', 'x'): // "mbox"
	{
		std::wstring msg;
		int32_t type;
		p_stream >> msg;
		p_stream >> type;
		SeMessageBox(msg.c_str(), L"", type);
	}
	break;

	case code_to_long('r', 'e', 'f', 'r'): // "refr"
	{
		if (synthRuntime.ProcessingCompleted())
		{
			synthRuntime.OnSynthThreadExit();
		}
	}
	break;

	case id_to_long2("wdog"): // watchdog ping
		break;

	default:
		break;
	}
}

bool CSynthEditAppBase::BuildSynth()
{
	if (Document()->MasterContainer == 0) // check there is a doc open
	{
		return false;
	}

	try
	{
		CDocOb::exportFlags = EXP_EDITOR | EXP_DSP;

//		_RPT0(0, "HARD start DSP\n");
		// Serialise DSP graph to XML.
		auto target = SAT_SYNTHEDIT_DSP;
		ModuleFactory()->ClearSerialiseFlags();

		// Create empty XML Document.
		auto doc2 = std::make_unique<TiXmlDocument>();
		doc2->LinkEndChild(new TiXmlDeclaration("1.0", "", ""));

		auto document_element = new TiXmlElement("Document");
		doc2->LinkEndChild(document_element);

		TiXmlElement* element = new TiXmlElement("DSP");
		document_element->LinkEndChild(element);

		Document()->MasterContainer->ExportXml(element, target);

#ifdef _DEBUG
		{
			TiXmlPrinter printer;
			printer.SetIndent("  ");
			doc2->Accept(&printer);

			[[maybe_unused]] auto chunk = printer.CStr();
			[[maybe_unused]] int x = 9;
		}
#endif

		dspDirty = false;

		IO_base* audio_driver = GetAudioDriver();

		int realtime_flags = 0;
		{
			// do we need a hardware audio driver?
			Document()->MasterContainer->GetTimingRequirements(realtime_flags);

			// If we're not using audio in or out, switch to fake driver. Else soundcard will crash. (also needed on macOS where we have no audio driver available)
			if (audio_driver == nullptr || (realtime_flags & (SER_SOUNDCARD_IN | SER_SOUNDCARD_OUT)) == 0)
			{
				audio_driver = m_audio_drivers[0].get();
				// determin high speed offline file render OR realtime execution (without audio/MIDI IO)
				audio_driver->SetRealTime((realtime_flags & SER_FILE) == 0);
			}
		}

		CDocOb::exportFlags = 0;

		if (!audio_driver)
		{
			return false;
		}

		synthRuntime.Clear(); // resets persistant host controls, so they don't override changes to oversampling made when engine off.

		synthRuntime.pendingDspXml = std::move(doc2);

		audio_driver->setPreferredSampleRate(static_cast<int>(GetSampleRate()));

		synthRuntime.Init(
			audio_driver,
			settings.m_audio_output_guid,// GetAudioOutput(),
			realtime_flags,
			getLatencyCompensation()
		);

		if (!synthRuntime.prepareToPlay())
		{
		return false;
		}

		constexpr double cpu_clock_rate = static_cast<double>(std::chrono::steady_clock::period::den) / std::chrono::steady_clock::period::num;
		cpuConversionConstant = GetSampleRate() / (static_cast<float>(cpu_clock_rate) * static_cast<float>(synthRuntime.audio_driver->getBufferSize()));

		return true;
	}

	catch (FeedbackTrace* e)
	{
		CDocOb::exportFlags = 0;

		reportFeedbackError(e);

		synthRuntime.OnSynthThreadExit();

		return false;
	}
	catch (SeException e)
	{
		CDocOb::exportFlags = 0;

		if (e.problem_code == SE_MULTIPLE_NOTESOURCES)
		{
			SeMessageBox((L"ERROR: Multiple 'MIDI to CV's can't be in same container, nor share a 'Patch automator'. (Also applies to 'Soundfont Player' and 'Drum Trigger')."), L"", MB_OK);
		}

		synthRuntime.OnSynthThreadExit();

		return false;
	}
}

void SplitSubMenuString(std::wstring sub_menu, vector< wstring >& subMenus)
{
	subMenus.clear();

	while (sub_menu.size() > 0)
	{
		auto p = sub_menu.find_first_of(L"\\/");

		if (p == string::npos)
		{
			subMenus.push_back(sub_menu);
			return;
		}
		else
		{
			std::wstring sub_menu_item = Left(sub_menu, p);
			subMenus.push_back(sub_menu_item);
			auto remainder_char = sub_menu.size() - p - 1;
			// unsigned. remainder_char = max(0,remainder_char);
			sub_menu = Right(sub_menu, remainder_char);
		}
	}
}

void CSynthEditAppBase::ExportModules(std::list< std::wstring >& moduleList, bool includePrefabs)
{
	if (m_menu_to_module_map.empty())
	{
		m_menu_to_module_map = ExportModuleNames();
	}

	std::wstring cur_groupname, next_groupname;

	for (auto it = m_menu_to_module_map.begin(); it != m_menu_to_module_map.end(); ++it)
	{
		if (!includePrefabs && Left((*it).second.unique_id, 3) == (L"*P="))
			continue;

		// split off sub dir names
		std::wstring	sub_menu = (*it).second.group;
		// simplify synths in own folder i.e "rainbow\rainbow"
		std::wstring nm = L"\\" + (*it).second.name;

		if (Lowercase(nm) == Lowercase(Right(sub_menu, nm.size())))
		{
			sub_menu = Left(sub_menu, sub_menu.size() - nm.size());
		}

		if (sub_menu != cur_groupname)
		{
			vector< wstring > cur_subMenus;
			vector< wstring > subMenus;
			SplitSubMenuString(sub_menu, subMenus);
			SplitSubMenuString(cur_groupname, cur_subMenus);
			int matchLevel = 0;

			while (matchLevel < cur_subMenus.size() && matchLevel < subMenus.size())
			{
				if (cur_subMenus[matchLevel] != subMenus[matchLevel])
				{
					break;
				}

				++matchLevel;
			}

			// back out till menus match.
			for (int i = 0; i < cur_subMenus.size() - matchLevel; ++i)
			{
				moduleList.push_back(L"-");
			}

			// go deep as required.
			for (size_t i = matchLevel; i < subMenus.size(); ++i)
			{
				wstring s = L"+:" + (subMenus[i]);
				moduleList.push_back(s);
			}

			cur_groupname = sub_menu;
		}

		// name, uniqueID, isgui. separated by \r char.
		std::wostringstream oss;
		oss << (*it).second.name << L"\r" << (*it).second.unique_id << L"\r" << (*it).second.flavor;
		moduleList.push_back( oss.str() );
	}
}

void CSynthEditAppBase::OnFileClose()
{
	OnFileNew2();
}

void CSynthEditAppBase::UpdateUndoMenus(bool CanUndo, bool CanRedo, std::wstring undo_description, std::wstring redo_description)
{
	m_app_user_interface->UpdateUndoMenus(CanUndo, CanRedo, undo_description, redo_description);
}

#ifdef _WIN32
HWND CSynthEditAppBase::MainWindowhandle()
{
	if (!m_app_user_interface)
		return 0;

	return m_app_user_interface->MainWindowhandle();
}
#endif

void CSynthEditAppBase::DeferredMessageBox(const wchar_t* msg, int flags)
{
	if (quiet)
	{
		SeMessageBox(msg, L"", flags);
	}
	else
	{
		m_app_user_interface->DeferredMessageBox(msg, flags);
	}
}


