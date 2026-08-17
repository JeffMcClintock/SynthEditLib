#if defined( _WIN32 )
#undef  WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#undef  NOMINMAX
#define NOMINMAX
#include <Windows.h>
#include "Shlobj.h"
#include "shellapi.h"
#endif

#include <algorithm>
#include <sstream>
#include <numeric>
#include <regex> 
#include <fstream> 
#include <iostream>
#include <filesystem>
#include "Application.h"
#include "SynthEditDocBase.h"
#include "SkinMgr.h"
#include "UgDatabase.h"
#include "CUG.h"
#include "CContainer.h"
#include "se_file_format_version.h"
#include "conversion.h"
#include "resource.h"
#include "imbedded_file.h"
#include "PatchManager.h"
#include "Notify_msg.h"
#include "ModuleFactory_Editor.h"
#include "MfcDocPresenter.h"
#include "SynthEditAppBase.h"
#include "ISEAppManaged.h"
#include "Shared/VstPreset.h"
#include "Shared/AuPreset.h"
#include "BundleInfo.h"
#include "tinyxml/tinyxml.h"

#ifdef _WIN32
#include "legacyExternalApp.h"
#elif defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#endif

using namespace std;
using namespace tinyxml2;
namespace fs = std::filesystem;

std::vector<std::wstring> CSynthEditDocBase::cantLoadList;
std::vector<std::wstring> CSynthEditDocBase::upgradeLoadList;
int CSynthEditDocBase::serializingMode = SERT_UNSET;

std::wstring CSynthEditDocBase::makeCantLoadErrorMessage(std::wstring msg)
{
	bool first = true;
	for (auto& id : CSynthEditDocBase::cantLoadList)
	{
		if (first)
		{
			first = false;
		}
		else
		{
			msg += L", ";
		}

		msg += id;
//		_RPTW1(_CRT_WARN, L"%s\n", id.c_str());
	}

	CSynthEditDocBase::cantLoadList.clear();

	return msg;
}


CSynthEditDocBase::CSynthEditDocBase() :
	MasterContainer(nullptr)
	,Version(FILE_FORMAT_VERSION_NUM)
	,AutoPlay(false)
	,m_application(0)
	,m_vst_unregistered(false)
	,m_vst_mono_use_ok(false)
	,m_vst_latencyCompensation(ElatencyContraintType::Constrained)
	,m_vst_flags(0)
	,m_vst_patches(64)
	,m_graph_initialised(false)
	,reverseGuiPins_deprecated(false)
#if defined( _DEBUG )
	,uncompilingDll_( false )
#endif
{
}

CSynthEditDocBase::~CSynthEditDocBase()
{
	DeleteContents();
}

bool CSynthEditDocBase::OnNewDocument()
{
	assert( MasterContainer == nullptr);
	MasterContainer = new CContainer(ModuleFactory()->GetById(L"Container"));
	MasterContainer->SetName(L"Main");
	MasterContainer->m_document = this; // hack

	MasterContainer->setHandle(uniqueIdDatabase.GenerateUniqueHandleValue());
	MasterContainer->Initialise();
	setGraphInitialised(true);
	SetModified(false); // counteract SetName()

	Application()->NotifyFast(OM_SHOW_PROPERTIES, nullptr);
	Application()->CommandManager()->ClearHistory(); // clear undo list
	GmpiResourceManager::Instance()->ClearAllResourceUris();
	
	//	pathName = L"Project1.synthedit";
//	SetTitle(pathName.c_str());
	Application()->NotifyFast(OM_OPEN_DOC);

	return true;
}

void CSynthEditDocBase::SyncTitle()
{
	Application()->NotifyFast(OM_DOC_TITLE_CHANGED);
}

std::wstring CSynthEditDocBase::getTitle()
{
	std::filesystem::path p(pathName);
	return p.stem().wstring();
}

void CSynthEditDocBase::DeleteContents()
{
	if (!Application())
		return;

	GmpiResourceManager::Instance()->setProjectFile({});

	Application()->NotifyFast(OM_SHOW_PROPERTIES, nullptr);
	Application()->CloseAllViews();

	Application()->OnRunStop();

	if(MasterContainer)
	{
		Application()->VO_Notify( OM_DELETE_DOC );
		CContainer* m = MasterContainer;
		MasterContainer = 0; // deleting it closes main wnd, which in turn tries again to delete it, so clear pointer 1st
		int uniqueId = m->Handle();
		delete m;
		uniqueIdDatabase.Unregister(uniqueId);
	}

	SyncTitle();
	SetModified(false);
}

bool CSynthEditDocBase::isDeletingContents()
{
	return MasterContainer == 0;
}

std::wstring CSynthEditDocBase::GetOpenComment()
{
	return OpenComment;
}

void CSynthEditDocBase::SetOpenComment(std::wstring c)
{
	if( !(OpenComment == c) )
	{
		OpenComment = c;
		SetModified();
	}
}

int CSynthEditDocBase::GetVersion()
{
	return Version;
}

bool CSynthEditDocBase::OnSaveDocument(const wchar_t* lpszPathName)
{
	return 0;
}

void CSynthEditDocBase::SetLoadingVersion(int ver)
{
	Version = ver;
}

// when running as VST plugin, use first container in file as basis for plugin
CContainer* CSynthEditDocBase::GetFirstContainer()
{
	// when file load fails, MasterContainer is nullptr.
	if( MasterContainer == 0 )
		return 0;

	for( auto it = MasterContainer->BaseList.begin() ; it != MasterContainer->BaseList.end() ; ++it )
	{
		CUG* model_ug = dynamic_cast<CUG*>(*it);;
		CContainer* child_container = dynamic_cast<CContainer*>( model_ug );

		if( child_container != nullptr && child_container->doExport() )
		{
			return child_container;
		}
	}

	return 0;
}

void CSynthEditDocBase::SetModified(bool p_modified_flag)
{
}

bool CSynthEditDocBase::doCloseDoc()
{
	return false;
}

void CSynthEditDocBase::CantLoad( const std::wstring& p_id )
{
	if(std::find(cantLoadList.begin(), cantLoadList.end(), p_id) == cantLoadList.end())
	{
		cantLoadList.push_back(p_id);
	}
}

void CSynthEditDocBase::CancelCantLoad(const std::wstring& p_id)
{
	cantLoadList.erase(std::remove(cantLoadList.begin(), cantLoadList.end(), p_id),
		cantLoadList.end());
}

// Register a module that will be replaced with the local (newer) version.
// Collected here and reported as a single consolidated dialog by showUpgradeMessage(),
// rather than one modal box per module.
void CSynthEditDocBase::Upgrade(const std::wstring& p_name)
{
	if (std::find(upgradeLoadList.begin(), upgradeLoadList.end(), p_name) == upgradeLoadList.end())
	{
		upgradeLoadList.push_back(p_name);
	}
}

void CSynthEditDocBase::showUpgradeMessage()
{
	if (!upgradeLoadList.empty())
	{
		const auto modulesString = std::accumulate(
			std::next(upgradeLoadList.begin()),
			upgradeLoadList.end(),
			upgradeLoadList[0],
			[](std::wstring a, std::wstring b) {
			return a + L", " + b;
		}
		);

		const auto msg = std::wstring(L"Warning: This project was created with a different version of some modules.\nThese modules will be replaced with your local version:\n" + modulesString);

		Application()->SeMessageBox(msg.c_str(), L"", MB_OK);
	}

	upgradeLoadList.clear();
}

// Open a new child window showing contents of a container
void CSynthEditDocBase::OpenView(CContainer* p_object, int view_flag)
{
	Application()->OpenView(p_object, view_flag);
}

void CSynthEditDocBase::ExportXml(XMLElement* XmlParent, ExportFormatType targetType)
{
	XmlParent->SetAttribute("file_format", (int)XML_FILE_FORMAT_VERSION_NUM);
	if ((int)m_vst_latencyCompensation != 0)
	{
		XmlParent->SetAttribute("vst_latencyCompensation", (int)m_vst_latencyCompensation);
	}

	XmlSaveHelper helper(XmlParent);
	Serialise2(helper);
}

int CSynthEditDocBase::ImportXml(XMLElement* XmlParent, ExportFormatType targetType)
{
	int fileFormatVersion = 0;
	/*auto eResult =*/ XmlParent->QueryIntAttribute("file_format", &fileFormatVersion);

	XmlLoadHelper helper(XmlParent);
	Serialise2(helper);

	{
		int temp = 0;
		/*auto eResult =*/ XmlParent->QueryIntAttribute("vst_latencyCompensation", &temp);
		m_vst_latencyCompensation = (ElatencyContraintType)temp;
	}

	return fileFormatVersion;
}

void CSynthEditDocBase::ExportXmlProject(std::wstring filename)
{
	const ExportFormatType targetType = SAT_SYNTHEDIT_DOCUMENT;

	// warn modules of imminent save, so wrappers can sync their state if needed
	MasterContainer->preSaveState();

	// catch-all: cull module-owned host-controls orphaned by any edit path that didn't sweep
	// itself (cut, containerise, scripting, ...). Harmless if there's nothing to cull.
	MasterContainer->RemoveOrphanedHostControls();

	tinyxml2::XMLDocument xmlDocument;
	xmlDocument.LinkEndChild(xmlDocument.NewDeclaration());

	auto doc_xml = xmlDocument.NewElement("Document");
	xmlDocument.LinkEndChild(doc_xml);

	ExportXml(doc_xml->ToElement(), targetType);

	// Editor window arrangement. Written here rather than from ExportXml because
	// this function is reached only by a real save of a .synthedit file, whereas
	// ExportXml is also how the undo system builds each snapshot - and neither
	// bloating every snapshot with the layout nor letting undo rearrange the
	// user's windows is wanted. Capture immediately before writing, so the file
	// records where the windows are now.
	if (auto* ui = editorUserInterface())
		ui->CaptureWindowLayout();
	m_windowLayout.Export(doc_xml);

	ModuleFactory()->ClearSerialiseFlags();

	auto mastercontainer_xml = xmlDocument.NewElement("master_container");
	doc_xml->LinkEndChild(mastercontainer_xml);
	MasterContainer->Export(mastercontainer_xml, targetType);

	ExportModuleInfo(doc_xml, targetType);

	xmlDocument.SaveFile(WStringToUtf8(filename).c_str());
}

void CSynthEditDocBase::ImportXmlDocument(std::wstring filename)
{
	DeleteContents();
	SetModified();  // dirty during de-serialize

	const ExportFormatType targetType = SAT_SYNTHEDIT_DOCUMENT;

	tinyxml2::XMLDocument xmlDocument;
	auto eResult = xmlDocument.LoadFile(WStringToUtf8(filename).c_str());

	if (eResult != tinyxml2::XML_SUCCESS) return;

	// "Document"
	auto pRoot = xmlDocument.FirstChildElement();
	if (pRoot == nullptr) return;// XML_ERROR_FILE_READ_ERROR;

	// Document properties.
	CDocOb::m_loading_version = ImportXml(pRoot, targetType);

	// Symmetric with ExportXmlProject: read here, not in ImportXml, so that
	// restoring an undo snapshot (which has no <WindowLayout>) can't clear the
	// arrangement this document was opened with.
	m_windowLayout.Import(pRoot);

	// load module database info for unavail modules
	ImportModuleInfo(pRoot, targetType, CDocOb::m_loading_version);

	ImportModules(pRoot, targetType);
}

void CSynthEditDocBase::ImportModules(XMLElement* pRoot, ExportFormatType targetType)
{
	// Full-doc layout wraps content in <master_container>.
	// Prefab layout omits the wrapper — <PatchManager>/<modules>/<lines> are direct children of <Document>.
	auto containerXml = pRoot->FirstChildElement("master_container");
	const bool isPrefab = (containerXml == nullptr);

	if (isPrefab)
	{
		// Recognise the prefab layout by the presence of <modules>; otherwise this file isn't loadable.
		if (pRoot->FirstChildElement("modules") == nullptr) return;
		containerXml = pRoot;
	}

	assert(MasterContainer == nullptr);

	MasterContainer = dynamic_cast<CContainer*>(CreateDocObject(L"Container")); // adds default plugs.
	MasterContainer->m_document = this;

/* ? already done in baseclass IMport
	int h = -1;
	containerXml->QueryIntAttribute("handle", &h);
	MasterContainer->setHandle(h);
	uniqueIdDatabase.Register(MasterContainer);
*/

	MasterContainer->setType(ModuleFactory()->GetById(L"Container"));
	MasterContainer->m_skin = SkinMgr::Instance()->getSkin(L"default");
//	MasterContainer->Initialise(true);

	std::map<int32_t, CUG*> uniqueIds;
	if (isPrefab)
	{
		// <Document> has no CUG-level attributes for the master container — give it a
		// fresh handle (the file has no handle to preserve) and import only the children.
		// Document-level fields like vst_latencyCompensation are absent in prefab files;
		// defaults remain in place.
		uniqueIdDatabase.setHandleAutoGenerated(MasterContainer);
		MasterContainer->ImportChildren(uniqueIds, containerXml, targetType);
	}
	else
	{
		MasterContainer->Import(uniqueIds, containerXml, targetType);
	}

	MasterContainer->AdjustModuleTypePointer();

	if (!cantLoadList.empty())
	{
		const auto msg = CSynthEditDocBase::makeCantLoadErrorMessage(L"Sorry, don't have some required SEM Modules for this project.\nThese modules will be inactive. They may be available from the internet:\n");

		Application()->SeMessageBox(msg.c_str(), L"", MB_OK);
	}
	showUpgradeMessage();

	// Before any new objects created, register all existing handles as in-use.
	// Otherwise parameters may get their handles re-assigned, and therfore fail to load bank and patch files correctly.
	// New handles are created during Upgrade, and also plug initialise when non-stateful parameters (note-pitch, patch-name etc) are created.
	constexpr bool loading_prefab = false;
	MasterContainer->RegisterHandles(loading_prefab);

	// no. 3rd party may need upgrade regardless.	if( Version < FILE_FORMAT_VERSION_NUM )
	MasterContainer->Upgrade(Version); // notify ugs of version in case upgrade needed

	// moved back, caused too many problems when called before upgrade
	MasterContainer->Initialise(true); // notify ugs of load completed

	setGraphInitialised(true);
	Version = FILE_FORMAT_VERSION_NUM; // after postload
	CDocOb::m_loading_version = FILE_FORMAT_VERSION_NUM; // important

#if defined( _DEBUG )
	//_RPT0(_CRT_WARN, "UN-COMPILE: serializingMode reset to false (skin won't save in future)\n" );
	serializingMode = SERT_UNSET;
#endif

	UpGradeIncompatibleModules();
	DeleteTemporaryModuleDescriptions();

	// reset modified flag, inadvertantly set during serialise
	SetModified(false);

#if defined( _DEBUG )
	// verify uniqueIdDatabase
	uniqueIdDatabase.debugVerify();
#endif
}

void CSynthEditDocBase::Export(std::wstring filename)
{
	std::wstring file, path, extension;
	decompose_filename(filename, file, path, extension);

	if (extension == L"syntheditprefab")
	{
		ExportXmlPrefab(filename);
		return;
	}

#ifdef _DEBUG
	assert(extension == L"synthedit"); // don't think we export JSON document anymore.
#endif

	ExportXmlProject(filename);
}

void CSynthEditDocBase::ExportXmlPrefab(const std::wstring& filename)
{
	if (!MasterContainer)
		return;

	// Warn modules of imminent save, so wrappers can sync their state if needed.
	MasterContainer->preSaveState();

	// Force IsCopyTagged() to return true for every descendant of MasterContainer,
	// so SerialiseSelectedModules treats the whole document as the prefab payload.
	// Document-level fields (vst_latencyCompensation, OpenComment, etc.) are intentionally
	// not written — they're not meaningful when the file is consumed by Insert Prefab.
	struct SerialiseAllGuard
	{
		SerialiseAllGuard()  { CDocOb::serialise_all_mode = true;  }
		~SerialiseAllGuard() { CDocOb::serialise_all_mode = false; }
	} guard;

	tinyxml2::XMLDocument buffer;
	MasterContainer->SerialiseSelectedModules(buffer, true); // andModuleInfo = true

	buffer.SaveFile(WStringToUtf8(filename).c_str());
}

// Allow App to access protected members indirectly.
void CSynthEditDocBase::OnFileSave2()
{
	OnFileSaveAs2(getFileName());
}

void CSynthEditDocBase::OnFileSaveAs2(const std::wstring& filename) //, bool bReplace)
{
	const auto extension = GetExtension(filename);

	if (extension == L"se1")
	{
		return;
	}

	Export(filename.c_str());

//?	if (bReplace)
	{
		setPathName(filename);
		GmpiResourceManager::Instance()->setProjectFile(filename);
		SyncTitle();
	}

	SetModified(false);
}

void CSynthEditDocBase::UpGradeIncompatibleModules()
{
	for (auto it = m_upgrade_replace_modules.begin(); it != m_upgrade_replace_modules.end(); ++it)
	{
		// batch upgrade - never prompts, and a module is replaced by its own type so
		// the pin-defaults case cannot arise anyway
		(*it)->Container()->ReplaceModule(*it, (*it)->getType()->UniqueId(),
		                                  CContainer::ReplaceModuleAction::Replace);
	}
	m_upgrade_replace_modules.clear();
}

// The editor front end, when there is one. Null in headless hosts (SynthEditCL, the
// plugin runtime, unit tests), which is why both callers below are guarded.
// Application() is typed as the base, so the cast is how the doc reaches the UI - the
// same route CpuMeterGui already takes.
ISEAppManaged* CSynthEditDocBase::editorUserInterface()
{
	auto* app = dynamic_cast<CSynthEditAppBase*>(Application());
	return app ? app->m_app_user_interface : nullptr;
}

bool CSynthEditDocBase::PostLoad()
{
	SetModified(false);

	serializingMode = SERT_UNSET;

	if (MasterContainer == nullptr)	// load failed due to wrong file version number
	{
		return false;
	}

	GmpiResourceManager::Instance()->setProjectFile(getFileName());

	BundleInfo::instance()->setPluginId(id_to_long(WStringToUtf8 /*WstringToString*/(m_vst_4_Char_id)));

	Application()->NotifyFast(OM_OPEN_DOC);

	// OpenViews() selects each tab as it opens it; selecting a tab fires
	// SelectionChanged -> tabChanged() -> new MfcDocPresenter, whose ctor calls
	// setSelectedView() and overwrites SelectedViewHandle/Type with the just-
	// opened (rightmost) view. Snapshot the persisted selection first so the
	// restore below brings the saved front tab forward, not the rightmost one.
	const auto savedSelectedViewHandle = SelectedViewHandle;
	const auto savedSelectedViewType = SelectedViewType;

	MasterContainer->OpenViews();

	auto container = dynamic_cast<CContainer*>(uniqueIdDatabase.HandleToObjectWithNull(savedSelectedViewHandle));
	if(container)
		OpenView(container, savedSelectedViewType);

	// Every tab now exists in the main window. Hand the saved arrangement to the UI,
	// which moves the torn-out ones into their own windows. Only reached on a real
	// document open - undo/redo re-runs OpenViews() from checkpoint.cpp, not from
	// here, so undoing an edit never rearranges the user's windows.
	if (auto* ui = editorUserInterface())
		ui->ApplyWindowLayout();

	SyncTitle();

	// reset modified flag, inadvertantly set during serialise
	SetModified(false);

	return true;
}

bool CSynthEditDocBase::OnOpenDocument2(const wchar_t* lpszPathName)
{
	serializingMode = SERT_SE1_DOC;

	std::wstring file, path, extension;
	decompose_filename(lpszPathName, file, path, extension);

	if (extension == L"synthedit" || extension == L"syntheditprefab")
	{
		ImportXmlDocument(lpszPathName);

		if (!MasterContainer)
			return false;
	}
	else
	{
#ifdef _WIN32
		// For older incompatible projects, user is offered option to auto-update using an installation of SE 1.5
		if (auto synthEdit15App = legacyExternalApp::create(); synthEdit15App)
		{
			synthEdit15App->UpgradeProjectFile(lpszPathName);

			// SE 1.5 writes .se1 → .synthedit and .seprefab → .synthedit.
			const std::wstring newExt = L".synthedit";
			const auto newPathName = combinePathAndFile(path, file + newExt);

			return OnOpenDocument2(newPathName.c_str());
		}
#endif
        return false;
	}

	Application()->CommandManager()->ClearHistory(); // clear undo list

	return PostLoad();
}

bool CSynthEditDocBase::OnSaveDocument2(const wchar_t* lpszPathName)
{
#ifdef _WIN32
	::SetFocus(0); // save the contents of any edit box with focus
#endif
	std::wstring saveFilename(lpszPathName);
	std::wstring extension = GetExtension(saveFilename);

	serializingMode = SERT_SE1_DOC;

	assert(extension != L"se1");

	Export(saveFilename.c_str());
	SetModified(false);

	serializingMode = SERT_UNSET;
	Application()->CommandManager()->ClearHistory(); // clear undo list

	SyncTitle();

	return true;
}

//#define DELETE_EXCEPTION(e) do { if(e) { e->Delete(); } } while (0)
// Used by Undo/Redo system.
void CSynthEditDocBase::Snapshot(tinyxml2::XMLDocument& xmlDocument)
{
	const ExportFormatType targetType = SAT_SYNTHEDIT_DOCUMENT;

	xmlDocument.LinkEndChild(xmlDocument.NewDeclaration());

	auto doc_xml = xmlDocument.NewElement("Document");
	xmlDocument.LinkEndChild(doc_xml);

	ExportXml(doc_xml->ToElement(), targetType);

	ModuleFactory()->ClearSerialiseFlags();

	auto mastercontainer_xml = xmlDocument.NewElement("master_container");
	doc_xml->LinkEndChild(mastercontainer_xml);
	MasterContainer->Export(mastercontainer_xml, targetType);

	ExportModuleInfo(doc_xml, targetType);
}

