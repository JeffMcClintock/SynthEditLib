#pragma once
#if !defined(AFX_DOCOB_H__6747F064_9023_11D5_AE85_000103170662__INCLUDED_)
#define AFX_DOCOB_H__6747F064_9023_11D5_AE85_000103170662__INCLUDED_

#include "notify.h"
#include "./ui_msg_target.h"
#include "GmpiUiDrawing.h"
#include "PresenterCommands.h"

class Module_Info;
class CSynthEditDocBase;
class CContainer;
class CPatchManager;

namespace Json
{
class Value;
}

#define DECLARE_BUILD_FUNC(xx)	static CDocOb * Make(Module_Info *p_type){return new xx(p_type);}

class CDocOb : public Notifier, public ui_msg_target
{
	inline static bool clear_sel_on_key_up = {};
public:
	CDocOb(Module_Info* p_type = 0);
	virtual void RegisterHandles(bool pasteMode);
	virtual void Initialise(bool loaded_from_file = false); // called right after constructor (except during load from disk)
	virtual void GetTimingRequirements(int& /*p_flags*/) {}
	virtual CSynthEditDocBase* Document();
	class ApplicationBase* GetApp();

	void MoveFront();
	virtual void Serialize2(Archive2& ar)
	{
		// defer to older less streamlined versions.
		if (ar.isSaving)
		{
			Export(ar.xmlElement, ar.targetType);
		}
		else
		{
			Import(*ar.uniqueIds, ar.xmlElement, ar.targetType);
		}
	}

	template< class Serializer >
	void SerialiseC(Serializer& s)
	{
		s("handle", m_unique_handle);
	}

	virtual void Export(Json::Value& JsonParent, ExportFormatType targetType);
	virtual void Export(tinyxml2::XMLElement* moduleElement, ExportFormatType targetType);
	virtual void Import(std::map<int32_t, class CUG*>& uniqueIds, tinyxml2::XMLElement * moduleElement, ExportFormatType targetType);

	// The exporters substitute stock modules ("SE Slider", "SE Background Image", ...) that
	// every export REQUIRES in the module database. They ship in external plugin libraries
	// (e.g. libControls.gmpi), so an incomplete plugin scan can lose them - that is a broken
	// installation, not an optional extra. Flags the module for serialisation; a missing one
	// is reported loudly, naming the module and the broken module database, and returns
	// false so the caller does not dereference null. The export is broken either way; the
	// diagnostic points at what to repair.
	bool FlagRequiredModuleForExport(const std::wstring& moduleTypeId);
	virtual void OnHelp();
	virtual void DoAboutBox();
	void Highlight(int highlightType);
	void Locate();
	void OnProperty();
	void MoveTo(gmpi::drawing::PointL p_point, int p_view_type);
	virtual void OnMenuCommand( int p_view_type, uint32_t p_command_id, gmpi::drawing::PointL mouse_pos = {-1,-1} );
	virtual gmpi::drawing::RectL GetViewPos(int p_type);
	virtual void SetViewPos( int p_type, gmpi::drawing::RectL p_pos);
	void OnDspMsg(int p_msg_id, gmpi::hosting::my_input_stream& p_stream) override;
	virtual void OnDelete();
	bool OnAdded(CContainer* container);
	virtual void OnRemoved(CContainer* container);

	inline CContainer* Container()
	{
		return m_container;
	}
	void SetContainer(CContainer* c)
	{
		m_container = c;
	}
	virtual void SetParentPointersRecursive( CContainer* p_container );

	virtual gmpi::drawing::RectL getViewObRect(int /*p_view_type*/)
	{
		return {};
	}
	virtual void setViewObRect(int /*p_view_type*/, gmpi::drawing::RectL& /*p_rect*/) {}
	virtual void offsetViewObRect(int /*p_view_type*/, int /*dx*/, int /*dy*/) {}
	virtual void Drag(int p_view_type, int dx, int dy) {offsetViewObRect(p_view_type,dx,dy);}

	virtual std::wstring GetName()
	{
		return {};
	}
	virtual void SetName(const std::wstring& /*new_name*/) {}
	virtual void OnSaveVST(int /*targetType*/) {}
	virtual bool EditEnabled();
	virtual void Upgrade(int /*from_version*/) {}
	bool GetSelected()
	{
		return m_selected;
	}
	virtual void SetSelected(bool p_state, bool clear_selection = false);
	void OnClicked(int32_t flags);
	virtual int GetFlags();
	void SetModifiedFlag();
	static int LoadingVersion();
	virtual bool IsCopyTagged();

	static bool serialise_copy_mode; // not in vst version
	static bool serialise_all_mode;  // when true (in addition to serialise_copy_mode), IsCopyTagged() returns true for every descendant of m_copy_container — used when saving the whole document as a prefab
	static int m_loading_version; // should be ok global (loading multiple instances will all have same file version anyway)

	Module_Info* getType()
	{
		return m_type;
	}
	void setType(Module_Info* p_type)
	{
		m_type = p_type;
	}
	virtual void AdjustModuleTypePointer();
	virtual CPatchManager* get_patch_manager();
	static CContainer* m_copy_container; // not in VST

	bool IsChildOf(CContainer* p_container);
	bool getErrorFlag() const
	{
		return (m_error_flags & (PinHighlightFlag_Feedback | PinHighlightFlag_UiFeedback)) != 0;
	}
	int getErrorFlags() const
	{
		return m_error_flags;
	}

	virtual bool isRackModule() const
	{
		return false;
	}

	inline static int32_t exportFlags = 0; // ref enum ExportFormatTypeFlags

	virtual void preSaveState() {}
	class ApplicationBase* Application();

private:
	bool m_selected;
	Module_Info* m_type = {};
	CContainer* m_container = {};
	int m_error_flags;
};

#endif // !defined(AFX_DOCOB_H__6747F064_9023_11D5_AE85_000103170662__INCLUDED_)
