#include <assert.h>
#include <iostream>
#include <fstream>
#include <filesystem>
#if defined( _WIN32 )
#include "Windows.h"
#endif
#include "Application.h"
#include "helpers/openurl.h"
#include "helpers/NativeUi.h"
#include "conversion.h"
#include "xp_dynamic_linking.h"
#include "it_enum_list.h"
#include "ModuleFactory_Editor.h"
#include "BundleInfo.h"
#include "SynthEditDocBase.h"
#include "SynthEditDoc2.h"
#include "CContainer.h"
#include "CUG.h"
#include "Notify_msg.h"
#include "Hosting/message_queues.h"
#include "se_version.h"

using namespace gmpi::hosting;

namespace
{
class FileDialogCompletionCallback : public gmpi::api::IFileDialogCallback
{
	ApplicationBase::FileDialogCompletion callback_;

public:
	explicit FileDialogCompletionCallback(ApplicationBase::FileDialogCompletion callback)
		: callback_(std::move(callback))
	{}

	void onComplete(gmpi::ReturnCode result, const char* selectedPath) override
	{
		if (callback_)
		{
			callback_(
				result == gmpi::ReturnCode::Ok ? IDOK : IDCANCEL,
				result == gmpi::ReturnCode::Ok && selectedPath ? Utf8ToWstring(selectedPath) : std::wstring{}
			);
		}
	}

	GMPI_QUERYINTERFACE_METHOD(gmpi::api::IFileDialogCallback);
	GMPI_REFCOUNT;
};

class StockDialogCompletionCallback : public gmpi::api::IStockDialogCallback
{
	ApplicationBase::MessageBoxCompletion callback_;

public:
	explicit StockDialogCompletionCallback(ApplicationBase::MessageBoxCompletion callback)
		: callback_(std::move(callback))
	{}

	void onComplete(gmpi::api::StockDialogButton button) override
	{
		if (!callback_)
			return;

		switch (button)
		{
		case gmpi::api::StockDialogButton::Ok:     callback_(IDOK);     break;
		case gmpi::api::StockDialogButton::Yes:    callback_(IDYES);    break;
		case gmpi::api::StockDialogButton::No:     callback_(IDNO);     break;
		default:                                   callback_(IDCANCEL); break;
		}
	}

	GMPI_QUERYINTERFACE_METHOD(gmpi::api::IStockDialogCallback);
	GMPI_REFCOUNT;
};
}

void CopyInitialPrefabs()
{
	const std::filesystem::path prefabsPath = BundleInfo::instance()->getCommonDocumentFolder() / "SynthEdit Projects" / "Prefabs";
	const auto testFile = prefabsPath / "Controls" / "Detuner.syntheditprefab";

	// Check if version changed, requiring a rescan
	const auto versionFile = BundleInfo::instance()->getCommonDocumentFolder() / "SynthEdit Projects" / ".resource_version";
	std::error_code ec;
	std::wifstream file(versionFile);
	int storedVersion = 0;

	if (file.is_open())
	{
		file >> storedVersion;
		file.close();
	}

	bool versionChanged = (storedVersion != SE_APP_BUILD_NUMBER);
	bool shouldCopy = versionChanged || !std::filesystem::exists(testFile);

	if (!shouldCopy)
		return;

	const std::filesystem::path sourcePrefabsPath = std::filesystem::path{ GetHomeDir() } / "Resources" / "Prefabs";
	if (!std::filesystem::exists(sourcePrefabsPath))
		return;

	ec.clear();
	std::filesystem::create_directories(prefabsPath, ec);
	ec.clear();
	std::filesystem::copy(
		sourcePrefabsPath,
		prefabsPath,
		std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing,
		ec
	);
}

ApplicationBase::ApplicationBase() :
	m_command_manager(this)
{}

ApplicationBase::~ApplicationBase() = default;

void ApplicationBase::createNewDocument()
{
	document_ = std::make_unique<SynthEditDoc>();
	document_->SetApplication(this);
	onDocumentChanged();
}

void ApplicationBase::BuildCodeSkeleton(int32_t handle, const std::wstring moduleName)
{
	if(auto mod = dynamic_cast<CUG*>(Document()->uniqueIdDatabase.HandleToObject(handle)); mod)
	{
		mod->BuildSkeletonCode(moduleName);
	}
}

std::wstring ApplicationBase::getSettingString(const wchar_t* name)
{
    // TODO
	return BundleInfo::instance()->getUserDocumentFolder().wstring();
}

void ApplicationBase::refreshFolderLocations()
{
	const auto homeFolder = GetHomeDir();
	const auto MyDocuments = BundleInfo::instance()->getUserDocumentFolder();
	const auto MyPrefabs = BundleInfo::instance()->getCommonDocumentFolder() / "SynthEdit Projects" / "Prefabs";
	const auto samplesFolder = overrideSamplesFolder.empty() ? getSettingString(L"AudioPath") : overrideSamplesFolder;

	// default folder
	// "All Files" defaults to My Documents
	m_folder_settings.push_back(std::unique_ptr<folder_info>(new folder_info{ {L"*"},	L"All Files",							MyDocuments.wstring()    , {} }));
	m_folder_settings.push_back(std::unique_ptr<folder_info>(new folder_info{ {L"wav", L"mp3", L"flac", L"ogg"}, L"Audio Files", samplesFolder , {} }));
	m_folder_settings.push_back(std::unique_ptr<folder_info>(new folder_info{ {L"mid"}, L"MIDI Files",							getSettingString(L"MidiPath") , {} }));
	m_folder_settings.push_back(std::unique_ptr<folder_info>(new folder_info{ {L"sfz", L"sf2"}, L"SoundFont Files",				samplesFolder  , {} }));
	m_folder_settings.push_back(std::unique_ptr<folder_info>(new folder_info{ {L"bmp",	L"png"}, L"Image Files",				homeFolder     , {} }));
	m_folder_settings.push_back(std::unique_ptr<folder_info>(new folder_info{ {L"syntheditprefab"},	L"Prefabs",					MyPrefabs.wstring()      , {} }));
	m_folder_settings.push_back(std::unique_ptr<folder_info>(new folder_info{ {L"sem"}, L"Modules",								getSettingString(L"ModulePath") , {} }));

	for (auto& fs : m_folder_settings)
		fs->current_folder = fs->default_folder;
}

folder_info* ApplicationBase::getFolderInfo(const std::wstring& p_extension)
{
	const auto searchExtension = Lowercase(p_extension);

	for (auto& fi : m_folder_settings)
	{
		for (auto& ext : fi->extensions)
		{
			if (ext == searchExtension)
			{
				return fi.get();
			}
		}
	}

	// not a recognized type, generate folder info.
	m_folder_settings.push_back(std::unique_ptr<folder_info>(new folder_info{ { p_extension }, p_extension + std::wstring(L" Files"), m_folder_settings[0]->current_folder, m_folder_settings[0]->current_folder }));

	return m_folder_settings.back().get();
}

std::wstring ApplicationBase::getDefaultPath(const std::wstring& p_file_extension)
{
	const auto extension = Lowercase(p_file_extension);

	for (const auto& fi : m_folder_settings)
	{
		for (const auto& ext : fi->extensions)
		{
			if (ext == extension)
				return fi->default_folder;
		}
	}

	return GetHomeDir();
}

std::wstring GetHomeDir()  
{  
   static std::wstring home_directory = []()->std::wstring  
   {  
       std::filesystem::path home(gmpi_dynamic_linking::MP_GetDllFilename());
       // Chop off exe filename
	   home = home.parent_path();
#if 0
       // If running on Jeff's PC, working dir is 'C:\SE\SEnn\SynthEdit  
       // e.g. "C:\S E\SE16\x64\Debug\SynthEdit2\AppX\SynthEdit2.exe" => "C:\SE\SE16\SynthEdit\"  
       if (auto p = home.wstring().find(L"C:\\S E\\") ; p != std::string::npos)
	   {
		   p = home.wstring().find_first_of(L'\\', p + 6); // "\SE16\...  
		   home = Left(home.wstring(), p + 1) + L"SynthEdit2\\";
       }
#endif

#if __APPLE__
       // return root of bundle on Mac, e.g. "/Applications/SynthEdit2.app/"
       if(home.filename() == "MacOS")
       {
           home = home.parent_path();
           if (home.filename() == "Contents")
               home = home.parent_path();
       }
#endif
       
       return home.wstring();
   }();

   return home_directory;  
}

/*
std::filesystem::path GetAssetsDir() // e.g. C://SE/SE16/SynthEdit2/Assets
{
	std::filesystem::path home(gmpi_dynamic_linking::MP_GetDllFilename());

	// Chop off trailing filename 'SynthEdit2.exe'
	auto result = home.parent_path();

	return result / L"Assets";
}
*/
void ApplicationBase::FileDialogAsync(bool load_or_save, std::wstring extension, std::wstring filename, FileDialogCompletion onComplete, bool absolutePath)
{
	std::vector<std::wstring> extensions;

	if (extension == (L"bmp")) // gets both .bmp and .png
		extension = L"bmp,png,svg";

	extensions.push_back(extension);

	if (extension == (L"syntheditprefab") && load_or_save)
		extensions.push_back(L"synthedit");

    FileDialogAsync(load_or_save, std::move(extensions), std::move(filename), std::move(onComplete), absolutePath);
}

void ApplicationBase::FileDialogAsync(bool load_or_save, std::vector<std::wstring> extensions, std::wstring filename, FileDialogCompletion onComplete, bool absolutePath)
{
	auto* host = getCurrentDialogHost();
	assert(host && "no IDialogHost — override getCurrentDialogHost() in your ApplicationBase subclass");
	if (!host)
   {
		if (onComplete)
			onComplete(IDCANCEL, std::move(filename));
		return;
	}

	extensions.push_back(L"*");

	folder_info* primaryFolderInfo = nullptr;

	const int dialogType = load_or_save
		? static_cast<int>(gmpi::api::FileDialogType::Open)
		: static_cast<int>(gmpi::api::FileDialogType::Save);

	gmpi::shared_ptr<gmpi::api::IFileDialog> dialog;
	host->createFileDialog(dialogType, reinterpret_cast<gmpi::api::IUnknown**>(dialog.put()));
	if (!dialog)
   {
		if (onComplete)
			onComplete(IDCANCEL, std::move(filename));
		return;
	}

	for (auto& extGroup : extensions)
	{
		it_enum_list it2(extGroup);
		it2.First();
		folder_info* fi = nullptr;
		if (!it2.IsDone())
			fi = getFolderInfo(it2.CurrentItem()->text);
		if (!primaryFolderInfo)
			primaryFolderInfo = fi;

		const std::string groupDesc = fi ? WStringToUtf8(fi->description) : "All Files";
		for (; !it2.IsDone(); it2.Next())
			dialog->addExtension(WStringToUtf8(it2.CurrentItem()->text).c_str(), groupDesc.c_str());
	}

	// Split filename into directory and bare name (cross-platform).
	namespace fs = std::filesystem;
	const fs::path fspath(filename);
	fs::path dirPath  = fspath.parent_path();
	std::wstring initial_filename = fspath.filename().wstring();

	const auto lastUsedPath = primaryFolderInfo ? primaryFolderInfo->current_folder : std::wstring{};
	if (filename.empty())
	{
		dirPath = lastUsedPath;
	}
	else if (!fspath.is_absolute())
	{
		if (primaryFolderInfo && !primaryFolderInfo->extensions.empty()
		    && primaryFolderInfo->extensions[0] == L"bmp")
			dirPath = fs::path(lastUsedPath) / "skins" / "default" / dirPath;
		else
			dirPath = fs::path(lastUsedPath) / dirPath;
	}

	dialog->setInitialDirectory(WStringToUtf8(dirPath.wstring()).c_str());
	if (!initial_filename.empty())
		dialog->setInitialFilename(WStringToUtf8(initial_filename).c_str());

    const std::wstring primaryExtension = primaryFolderInfo && !primaryFolderInfo->extensions.empty()
		? primaryFolderInfo->extensions.front()
		: std::wstring{};

	auto* cb = new FileDialogCompletionCallback(
		[this, primaryExtension, absolutePath, filename = std::move(filename), onComplete = std::move(onComplete)](int result, std::wstring callbackFilename) mutable
		{
			if (result != IDOK)
			{
				if (onComplete)
					onComplete(IDCANCEL, std::move(filename));
				return;
			}

			filename = std::move(callbackFilename);

			folder_info* completedFolderInfo = primaryExtension.empty() ? nullptr : getFolderInfo(primaryExtension);

			// Remember the folder for next time.
			std::wstring file, resultPath, extension;
			decompose_filename(filename, file, resultPath, extension);
			if (completedFolderInfo)
				completedFolderInfo->current_folder = resultPath;

			// Trim the default-path prefix so the stored filename stays relative.
			// Not for a document: the caller opens or saves it by absolute path,
			// and a relative one is resolved against the process' working
			// directory - which is wherever the app happened to be launched from.
			if (completedFolderInfo && !absolutePath)
			{
				const auto& defaultPath = completedFolderInfo->default_folder;
				if (!defaultPath.empty()
					&& Lowercase(defaultPath) == Lowercase(Left(filename, defaultPath.size())))
				{
					filename = Right(filename, filename.size() - defaultPath.size());

					// Trimming the prefix leaves behind the separator that joined
					// it to the rest, so what remains still looks absolute. Drop
					// it - but ONLY here. Doing it unconditionally ate the root of
					// every POSIX path that was not under the default folder, and
					// the save then failed silently against the process' working
					// directory instead of the folder the user picked.
					if (!filename.empty() && (filename[0] == L'/' || filename[0] == L'\\'))
						filename = Right(filename, filename.size() - 1);

					if (extension == L"bmp")
						filename = StripPath(filename);
				}
			}

			if (onComplete)
				onComplete(IDOK, std::move(filename));
		}
	);
	dialog->showAsync(nullptr, cb);
	cb->release();
}

void OpenWebPage(const std::wstring& p_web_url)
{
	gmpi::open_url(p_web_url);
}

int32_t ApplicationBase::SeMessageBox(const wchar_t* msg, const wchar_t* title, int flags)
{
	if (quiet)
	{
		// NARROW, deliberately. C streams take an orientation from their first
		// use, and on glibc a wide write here silently poisons every later
		// narrow write to the same stream — which cost the CLI its entire JSONL
		// output on Linux (verbs ran, files were written, stdout was empty).
		// SynthEditCL's protocol is UTF-8 JSON on stdout, so nothing may ever
		// wide-orient it.
		//
		// Indent the continuation lines. A dialog is one message, but printed
		// raw it arrives as several unrelated-looking lines, so a reader
		// scraping this stream keeps the first and drops the rest — which is
		// how "VST3 plugins folder not found:" reached the MCP client without
		// the path that was the whole point of it. Leading whitespace is the
		// convention that marks them as belonging to the line above.
		auto text = WStringToUtf8(msg ? msg : L"");
		while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
			text.pop_back();

		for (size_t pos = 0, line = 0; pos <= text.size(); ++line)
		{
			const auto eol = text.find('\n', pos);
			const auto len = (eol == std::string::npos ? text.size() : eol) - pos;

			// A blank separator line would carry no indent and so would break
			// the run; the indent alone keeps the message together.
			if (len > 0)
				std::cout << (line == 0 ? "" : "  ") << text.substr(pos, len) << "\n";

			if (eol == std::string::npos)
				break;
			pos = eol + 1;
		}
		std::cout << std::flush;
		return MB_OK;
	}
	else
	{
#ifdef _WIN32
		return ::MessageBox(MainWindowhandle(), msg, title, flags);
#else
		if (messageBoxCallback)
			return messageBoxCallback(msg, title, flags);

		// No blocking dialog on this platform. Show it asynchronously rather than
		// throwing it away: of the ~58 call sites only one consumes the answer, and
		// silently swallowing the other 57 means module XML errors and friends never
		// reach the user at all. Callers that need the answer use SeMessageBoxAsync.
		if (getCurrentDialogHost())
		{
			SeMessageBoxAsync(msg, title, flags, {});
			return IDOK;
		}

		return IDCANCEL;
#endif
    }
}

void ApplicationBase::SeMessageBoxAsync(const wchar_t* msg, const wchar_t* title, int flags,
                                        MessageBoxCompletion onComplete)
{
	if (quiet)
	{
		// narrow, deliberately - see the comment in SeMessageBox
		std::cout << WStringToUtf8(msg ? msg : L"") << std::endl;
		if (onComplete)
			onComplete(MB_OK);
		return;
	}

	// gmpi_ui already models a message box: IDialogHost::createStockDialog plus
	// IStockDialog::showAsync. Going through it means a host implements one thing
	// and gets file dialogs, menus, colour pickers and message boxes from the same
	// seam - rather than that seam plus a private message-box callback beside it.
	if (auto* host = getCurrentDialogHost())
	{
		// low nibble picks the button set, same values as the Win32 MB_ flags
		auto dialogType = gmpi::api::StockDialogType::Ok;
		switch (flags & 0x0f)
		{
		case 1:                dialogType = gmpi::api::StockDialogType::OkCancel;    break;
		case MB_YESNOCANCEL:   dialogType = gmpi::api::StockDialogType::YesNoCancel; break;
		case MB_YESNO:         dialogType = gmpi::api::StockDialogType::YesNo;       break;
		default:               dialogType = gmpi::api::StockDialogType::Ok;          break;
		}

		const auto titleUtf8 = WStringToUtf8(title ? title : L"");
		const auto textUtf8  = WStringToUtf8(msg ? msg : L"");

		gmpi::shared_ptr<gmpi::api::IStockDialog> dialog;
		host->createStockDialog(static_cast<int32_t>(dialogType), titleUtf8.c_str(), textUtf8.c_str(),
		                        reinterpret_cast<gmpi::api::IUnknown**>(dialog.put()));

		if (dialog)
		{
			auto* cb = new StockDialogCompletionCallback(std::move(onComplete));
			dialog->showAsync(static_cast<gmpi::api::IStockDialogCallback*>(cb));
			return;
		}
	}

	// A platform with only a blocking dialog (Windows ::MessageBox, the macOS nested
	// loop) satisfies the async contract by completing before it returns.
	const int32_t result = SeMessageBox(msg, title, flags);
	if (onComplete)
		onComplete(result);
}

bool ApplicationBase::LoadOrScanModuleData()
{
//	_RPT0(_CRT_WARN, "LoadOrScanModuleData\n");
	CopyInitialPrefabs();

	if (!LoadModuleData())
	{
		// no cache so, re-generate.
		RefreshModuleData(true, rescanIncludesVsts, true);
	}

	ReloadMenu();

	return true;
}

// add prefabs/ vst plugins
void ApplicationBase::RefreshModuleData(bool refresh_sems, bool refresh_vsts, bool refresh_prefabs)
{
	std::cout << "RESCAN: start..." << std::endl;

	if (refresh_vsts)
	{
		SetAsideAllPluginData(true); // clear wrapper plugins.

		// VST Wrappers.
		ScanFolder(getSettingString(L"ModulePath"), ".sem,.gmpi", L"", true);
	}

	if (refresh_prefabs)
	{
		ModuleFactory()->ClearPrefabs();
		ScanFolder(getDefaultPath(L"syntheditprefab"), ".synthedit,.syntheditprefab,.seprefab", L"");
	}

	if (refresh_sems)
	{
		SetAsideAllPluginData();

		std::cout << "Scanning for 3rd-party SEMs in: " << WStringToUtf8(getSettingString(L"ModulePath")) << std::endl;

		ScanFolder(getSettingString(L"ModulePath"), ".sem,.gmpi", L"");

#ifdef _WIN32
		// 3rd Party Mac SEMs (Windows)
		{
			auto mac_sems_path = getSettingString(L"ModulePath");
			auto p = mac_sems_path.find_last_not_of(L"/\\") + 1;
			mac_sems_path = mac_sems_path.substr(0, p) + L"_mac";
			ScanFolder(mac_sems_path, ".sem,.gmpi", L"");
		}
#endif
		// Also Scan factory modules.
		// scans SynthEdit2/PlugIns folder (which contains universal modules with mac and Windows SEMs also).
        auto factorypath = BundleInfo::instance()->getSemFolder();
		std::cout << "Scanning for factory SEMs in: " << WStringToUtf8(factorypath) << std::endl;
		ScanFolder(factorypath, ".sem,.gmpi", L"");
	}

	// keep any old descriptions possibly needed by current project.
	RetainMissingModuleDescriptions();

	// point document to new descriptions.
	if (Document() && Document()->MasterContainer)
	{
		// GUI Modules will have pointer to dll objects about to be deleted by DeleteTemporaryModuleDescriptions()

		// Re-open views.
	//no, don't work with dialog open.	
		NotifyFast(OM_CLOSE_REOPEN_VIEWS_ASYNC);

		Document()->MasterContainer->AdjustModuleTypePointer();
	}

	// remove old descriptions.
	DeleteTemporaryModuleDescriptions();

	//	Document()->MasterContainer->OpenViews();

	// update menu module map. Next time menu used.
	ReloadMenu();
	
	StoreModuleData();

	std::cout << "RESCAN: end." << std::endl;
}

void ApplicationBase::setHoverScopePin(int32_t moduleHandle_watched, int32_t moduleHandle_original, int32_t dspHoverPinIdx)
{
	auto que = MessageQueToDspOrNull();
	if (!que)
		return;

	my_msg_que_output_stream strm(que, (int) UniqueSnowflake::APPLICATION, "hvsc"); // hover-scope
	const int32_t messageLength = sizeof(int32_t) * 3;
	strm << messageLength;
	strm << moduleHandle_watched;
	strm << moduleHandle_original;
	strm << dspHoverPinIdx;
	strm.Send();
}
