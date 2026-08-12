#pragma once

#include <list>
#include <memory>
#include "DocOb.h"
#include "ElatencyContraintType.h"

class CSynthEditAppBase;
class ApplicationBase;

// m_vst_flags
#define VST_ISSYNTH 0x0001
#define VST_ISSTEREO 0x0002
#define VST_SE_REGISTERED 0x0004
#define VST_EXPORT_VST2 0x20
// RARE CASE OF PLUGIN SENDING TIMEINFO TO HOST
#define VST_SENDS_TIMEINFO 0x0008

enum ESerializationType { SERT_UNSET, SERT_VST_DOC_DEPRECATED, SERT_SEM_CACHE, SERT_SE1_DOC, SERT_UNDO_SYSTEM};
#if defined( _DEBUG )
struct cancelCompare2
{
	int32_t handle = {};
	int32_t voice = {};
	int pin = {};
	float errorMax = {};
	float errorAvr = {};
};
#endif

class CSynthEditDocBase
{
protected: // create from concrete class only
	CSynthEditDocBase();

public:
	virtual ~CSynthEditDocBase();
	void SetApplication(ApplicationBase* p_application)
	{
		m_application = p_application;
	}
	ApplicationBase* Application()
	{
		return m_application;
	}
	void SyncTitle();

	virtual void DeleteContents();
	virtual bool OnNewDocument();
	bool OnOpenDocument2(const wchar_t* lpszPathName);
	bool OnSaveDocument2(const wchar_t* lpszPathName);
	void Snapshot(tinyxml2::XMLDocument&);
	virtual bool OnSaveDocument(const wchar_t* lpszPathName);
	virtual bool doCloseDoc();
	bool PostLoad();
	bool isDeletingContents();
	void SetLoadingVersion(int ver);
	bool isModified()
	{
		return isModified_;
	}
	virtual void SetModified(bool p_modified_flag = true);
	virtual void setPathName(const std::wstring& p_path_name) = 0;
	CContainer* GetFirstContainer();
	virtual void OpenView(class CContainer* p_object, int view_flag);// , const std::wstring& p_title, int p_window_icon = -1);
	void setSelectedView(int32_t containerHandle, int32_t view_flag)
	{
		SelectedViewHandle = containerHandle;
		SelectedViewType = view_flag;
	}
	int GetVersion();
	void SetOpenComment(std::wstring c);
	std::wstring GetOpenComment();

	//DWORD UniqueID();
	bool isGraphInitialised()
	{
		return m_graph_initialised;
	}
	void setGraphInitialised(bool p_new_state)
	{
		m_graph_initialised = p_new_state;
	}
	static void CantLoad( const std::wstring& p_id );
	static void CancelCantLoad(const std::wstring& p_id);
	static void Upgrade(const std::wstring& p_name);
	static std::wstring makeCantLoadErrorMessage(std::wstring msg);
	void showUpgradeMessage();
	
	std::wstring getTitle();
	std::wstring getFileName()
	{
		return pathName;
	}
	virtual void OnProperties() = 0;

	void OnFileSave2();// override;
	void OnFileSaveAs2(const std::wstring& filename);// , bool bReplace = false);// override;

	void UpGradeIncompatibleModules();

	void ExportXml(tinyxml2::XMLElement* XmlParent, ExportFormatType targetType); // tinyxml2
	int ImportXml(tinyxml2::XMLElement* XmlParent, ExportFormatType targetType);
	void ExportXmlProject(std::wstring filename);
	void ExportXmlPrefab(const std::wstring& filename); // writes the paste-fragment format used by .syntheditprefab
	void ImportXmlDocument(std::wstring filename);
	void ImportModules(tinyxml2::XMLElement* pRoot, ExportFormatType targetType);
	void Export(std::wstring filename);// override;

	static std::vector<std::wstring> cantLoadList;  // not in VST
	static std::vector<std::wstring> upgradeLoadList;  // not in VST

	template< class Serializer >
	void Serialise2(Serializer& s)
	{
		// CSynthEditDocBase
		s("OpenComment", OpenComment);
		s("AutoPlay", AutoPlay);
		s("reverseGuiPins", reverseGuiPins_deprecated);
		s("vst_user_email", m_vst_user_email);
		s("vst_processor_id", m_vst_processor_id);
		s("vst_controller_id", m_vst_controller_id);
		s("vst_embedded_file_list", m_vst_embedded_file_list);
		s("vst_unregistered", m_vst_unregistered);
		s("SelectedViewHandle", SelectedViewHandle);
		s("SelectedViewType", SelectedViewType);
		//		s("vst_latencyCompensation", m_vst_latencyCompensation); // ref CSynthEditDoc::ExportXml()

		s("autoplay", AutoPlay);
		s("vst_4char_id", m_vst_4_Char_id);
		s("vst_dll_name", m_vst_dll_name);
		s("vst_product", m_vst_product);
		s("vst_about_text", m_vst_about_text);
		s("vst_mono_use_ok", m_vst_mono_use_ok);
		s("emulate_ignore_pc", m_vst_emulate_ignore_pc);
		s("vst_panel_size_w", vst_panel_size.width);
		s("vst_panel_size_h", vst_panel_size.height);
		s("vst_flags", m_vst_flags);
		s("vst_preset_count", m_vst_patches);
		s("vst_version", m_vst_version);
		s("vst_website", m_vst_website);
		s("vst_copyright", m_vst_copyright);
		s("rack_mode", rackMode);
	}
	
	CContainer* MasterContainer;

	bool AutoPlay;
	bool reverseGuiPins_deprecated; // only for backward file compatibility
	std::wstring m_vst_4_Char_id;
	std::wstring m_vst_dll_name;
	bool m_vst_unregistered;
	bool m_vst_mono_use_ok;
	bool m_vst_emulate_ignore_pc = true;
	ElatencyContraintType m_vst_latencyCompensation;
	std::wstring m_vst_about_text;
	std::wstring m_vst_serial;
	std::wstring m_vst_product;
	std::wstring m_vst_user_email;
	std::wstring m_vst_embedded_file_list;
	std::wstring m_vst_version{ L"1.0.0" };
	std::wstring m_vst_website;
	std::wstring m_vst_copyright;
	std::string m_vst_processor_id;
	std::string m_vst_controller_id;
	int m_vst_flags;
	int m_vst_patches;
  gmpi::drawing::SizeL vst_panel_size; //used during vst save/load
	UniqueSnowflakeOwner uniqueIdDatabase;

	static int serializingMode; // save skin as part of file. not in VST.
#if defined( _DEBUG )
	bool uncompilingDll_;
	std::vector<cancelCompare2> cancellationResults;
#endif
	bool load_failed_update_project_file = {};

	// If the module loaded from project file is significantly different than the
	// local one, need to replace entire module rather to prevent crashing.
	std::vector< class CUG* > m_upgrade_replace_modules;
	int32_t SelectedViewHandle = -1;
	int32_t SelectedViewType{};

	// Lay out and draw the top-level panel view as a Eurorack case (experimental).
	bool rackMode = {};

protected:
	std::wstring OpenComment;
	int Version;
	ApplicationBase* m_application;
	bool m_graph_initialised;
	bool isModified_ = false;
	std::wstring title;
	std::wstring pathName;
};

