#pragma once

#include <set>
#include <utility>
#include <vector>
#include "CUG_with_patches.h"

class IPlug;
class PatchParameter_base;
class CPatchManager;
class CLine2;
class ug_container;
class SkinInfo;
class CSynthEditDocBase;

typedef std::list<CDocOb*> DO_LIST;

// P3 moved these ids into StandardCommandIds.h, which defines them as macros
// on every platform. This file used to declare its own enum for them under
// #ifndef _WIN32; once CContainer.cpp included StandardCommandIds.h first, the
// macros expanded inside the enum and it stopped compiling off Windows --
// "expected identifier". Windows never saw it because the enum was compiled
// out there. Same four values, one definition now.
#include "StandardCommandIds.h"

// Forward declaration so the free-function export can be granted friend access.
// Keep in step with ExportAsPlugin.h — the friend declaration below matches by
// signature, so a parameter added there silently withdraws the friendship.
bool ExportAsPlugin(CSynthEditDocBase* doc, int autoSave, std::wstring presetsFolder, std::string* errorOut,
                    std::vector<std::pair<std::string, std::string>>* outputsOut);

class CContainer : public CUG_with_patches
{
	friend class it_doc_ob;
	friend class ug_container;
	friend class CSynthEditDocBase;
	friend class dlg_connect_ug;
	friend class CSynthEditDoc;
	friend bool ::ExportAsPlugin(CSynthEditDocBase*, int, std::wstring, std::string*,
	                             std::vector<std::pair<std::string, std::string>>*);

#ifdef _DEBUG
	friend class CLine2; // for extra checking code
#endif

public:
	void OnPlugDefaultChange(IPlug* plug) override;
	bool IsCopyTagged() override;
	CContainer* getVoiceControlContainer();
	virtual void GetTimingRequirements( int& p_flags ) override;
	int32_t VstUniqueID(bool vstWrapperMode = false) override;
	int SetChunk(void* data, int byteSize, bool isPreset) override;
	int GetChunk(void** chunk_ptr, bool isPreset) override;
	void ImportPreset(const std::string& filename, bool isPreset) override;
	void ExportPreset(const std::string& filename, bool isPreset, int presetIndex = -1) override;
	void SetProgramNameIndexed(int p_index,const std::wstring& p_name);
	bool ExpandInline();
	bool hasPatchSelector();
	bool hasPatches();
	CContainer( Module_Info* p_type = 0 );
	static CDocOb* Make(Module_Info* p_type = 0);
	~CContainer();
	CSynthEditDocBase* Document() override;
	int CountPlugs(EDirection p_direction);
	void setSkin(SkinInfo* p_skin);
	SkinInfo* getSkin();
	bool IsImbeddedView(int view_type);
	void setViewObRect(int p_view_type, gmpi::drawing::RectL& p_rect) override;
	virtual void offsetViewObRect(int p_view_type, int dx, int dy) override;
	gmpi::drawing::RectL getViewObRect(int p_view_type) override;
	auto getPanelWndOffset()
	{
		return PanelWndOffset;
	}
	void setPanelWndOffset(gmpi::drawing::SizeL s);
	void SetProgram(int p_program) override;
	int GetProgram() override;
	void setProgramName(const std::wstring& p_name) override;
	std::wstring getProgramName() override;
	std::wstring GetProgramNameIndexed(int p_index ) override;
	virtual void OnMenuCommand(int p_view_type, uint32_t p_command_id, gmpi::drawing::PointL mouse_pos = { -1,-1 }) override;
	virtual void DoHostCommand(int p_command_id) override;
	void LoadSubPreset(bool strictMatching);
	void SaveSubPreset();
	CDocOb* OnNewUG(int p_view_type, const std::wstring& module_id, int32_t handle, gmpi::drawing::PointL p_point = {-1,-1} );
	void OnEditPaste(gmpi::drawing::PointL point, int p_view_type, tinyxml2::XMLDocument* pasteDoc = {});
	void SerialiseSelectedModules(tinyxml2::XMLDocument& doc, bool andModuleInfo);
	bool OnEditCopy();
	void OnEditClone();
	void OnNewConnection(CLine2* p_line) override;
	CDocOb* AddReplacementUg( CDocOb* oldUg, CDocOb* newUg );
	void ExportMain(Json::Value& object_json, ExportFormatType targetType);
	virtual void Export(Json::Value& JsonParent, ExportFormatType targetType) override;
	void Import(std::map<int32_t, CUG*>& uniqueIds, tinyxml2::XMLElement * moduleElement, ExportFormatType targetType) override;
	virtual void Export(tinyxml2::XMLElement* moduleElement, ExportFormatType targetType) override;
	void ExportChildren(tinyxml2::XMLElement * object_json, ExportFormatType targetType);
	void ImportChildren(std::map<int32_t, CUG*>& uniqueIds, tinyxml2::XMLElement * object_json, ExportFormatType targetType);
	void ExportChildren(Json::Value& object_json, ExportFormatType targetType);
	// True when the connector can be written to file. Otherwise tells the user which
	// connector is being left out and why, then returns false so the caller skips it.
	// Silently writing an unresolvable pin index is what made connectors vanish.
	bool reportIfUnserializable(class CLine2* line);
	virtual TiXmlElement* ExportXml(TiXmlElement* XmlParent, ExportFormatType targetType) override;
	virtual void ExportXml_Pt2( TiXmlElement* XmlParent, ExportFormatType targetType );
	// Replacing a slider with Fixed Values can instead write the slider's value into
	// the destination pins' defaults and delete it - but only when the slider is the
	// sole source. Which of the two happens is the USER's choice, so it is passed in:
	// the model does not ask questions. Asking meant a blocking message box, the last
	// one in the codebase, and a nested modal loop is hostile under Wayland.
	enum class ReplaceModuleAction
	{
		Replace,        // create the replacement module (always valid)
		SetPinDefaults, // write the value into destination pin defaults; returns null
	};

	// True when SetPinDefaults is actually on offer for this pair.
	bool canSetPinDefaultsInstead(CUG* old_module, const std::wstring& replacementModuleType);

	CUG* ReplaceModule( CUG* old_module, const std::wstring& replacementModuleType,
	                    ReplaceModuleAction action = ReplaceModuleAction::Replace );

	// Offers the choice (asynchronously) when it applies, then replaces. onComplete
	// receives the new module, or null if the pin defaults were set instead.
	void ReplaceModuleAsync( CUG* old_module, const std::wstring& replacementModuleType,
	                         std::function<void(CUG*)> onComplete );
	CUG* FindNextModule( CUG* old_module, bool downward = true );
	CUG* FindPrevModule( CUG* old_module, bool downward = true );
	gmpi::drawing::PointL getViewVisibleCenter(int p_view_type);

	CDocOb* Remove(CDocOb* o);
	CLine2* AddLine(IPlug* from,IPlug* to, int32_t handle = -1);
	CLine2* AddLineQuiet(IPlug* from, IPlug* to);
	void PickupLine(int32_t moduleHandle, bool isFromEnd);
	void ClearDragLine();

	void addToContainer(CDocOb* o);
	void LoadPrefab(int p_view_type, std::wstring filename, gmpi::drawing::PointL p);

	void AddSorted(CDocOb* doc_ob);
	void OnEditSelectAll();
	void OnEditDelete();
	bool DeleteSelection();
	gmpi::drawing::RectL GetPanelRect();
	void SetPanelRect(gmpi::drawing::RectL pos);
	void SetViewPos(int view_type, gmpi::drawing::RectL pos) override;
	gmpi::drawing::RectL GetViewPos(int view_type) override;
	void onViewClosed(int32_t viewType);
	gmpi::drawing::Point GetViewCenter(int view_type) const;
	void SetViewCenter(int view_type, gmpi::drawing::Point center);
	void SetPanZoom(int view_type, gmpi::drawing::Point center, float zoomFactor);
	gmpi::drawing::RectL getVisibleRect(int view_type) const;
    void SetZoom(int view_type, float zoomFactor);
	float GetZoom(int view_type) const;
	void OpenViews();
	CContainer* OnEditContain();
	void OnEditToPrefab();
	void OnEditUnContain();
	void GetSelection(DO_LIST& p_list);

	void CopyPatch(int to_patch_lo,int to_patch_hi) override;
	void debugpoly() override;
	bool GetNoteSource();

	void UpgradeAll(int version);
	void Upgrade(int from_version) override;
	void setAllSelected(bool p_selected, CDocOb* p_exception = 0);
	void DeleteAll();
	void OnEditMoveFront();
	void OnEditMoveBack();
	void MoveChildToFront(CDocOb* child);
	void MoveChildToBack(CDocOb* child);
	CUG* GetIoModule(EDirection direction = DR_IN);
	bool getLocked() const
	{
		return m_locked;
	}
	bool isRackModule()const override 
	{
		return m_is_rack_module;
	}
	void setLocked(bool p_locked);
	void toggleLocked();
	bool PatchSlavedToParent();
	void RegisterHandles( bool pasteMode ) override;
	void Initialise(bool loaded_from_file=false) override; // called right after constructor (except during load from disk)
	void SetParentPointersRecursive( CContainer* p_container ) override;
	bool EditEnabled() override
	{
		return !getLocked();
	}
	void OffsetChildren(int p_view_type, gmpi::drawing::SizeL p_offset);
	void DragSelection(int p_view_type, int dx, int dy);
	virtual void OnSaveVST(int targetType) override;
	void OnHasPatchSelectionChanged();
	void SendIntValueToDsp(const char* messageId, int value );
	bool getIgnoreProgramChange();
	void NotifyAllViews2( int lHint, void* pHint = 0 );

	// Polyphony.
	void setVoiceAllocationMode( int mm );
	int getPolyphony();
	int getPolyphonyReserve();

	// Oversampling.
	int GetHostControlInt(HostControls hostControl);
	int GetOversamplingRate();
	int GetOversamplingFilterPoles();

	int ViewOpenFlags;
	CSynthEditDocBase* m_document; // only mastercontainer knows it

protected:
	template< class Serializer >
	void Serialise2(Serializer& s)
	{
		s("PanelLocationCenter" , PanelLocationCenter);
		s("StructLocationCenter", StructLocationCenter);
		s("PanelLocationZoom"   , PanelLocationZoom);
		s("StructLocationZoom"  , StructLocationZoom);

		s("show_controls_on_panel_legacy", m_show_controls_on_panel_legacy);
		s("PanelWndPosition", PanelWndPosition);
		s("PanelLocation", PanelLocation);
		s("StructLocation", StructLocation);
		s("PanelScroll", PanelScroll);
		s("StructScroll", StructScroll);
		s("ViewOpenFlags", ViewOpenFlags);
		s("m_locked", m_locked);
		s("PanelWndOffset", PanelWndOffset);
		s("panel_rect", m_panel_rect);
		s("rack_module", m_is_rack_module);
	}

	virtual void preSaveState() override;

private:
	// One recursive pass for RemoveOrphanedHostControls(): gather the module-owned host-control
	// parameters still wired to a live pin (whole subtree) and the patch managers that hold them.
	void gatherModuleOwnedHostControls(std::set<PatchParameter_base*>& referenced, std::vector<CPatchManager*>& managers);

	CPatchManager* my_patch_manager();
	gmpi::drawing::RectL m_panel_rect;		// user set panel extent used in VST.
	gmpi::drawing::RectL PanelWndPosition;
	gmpi::drawing::SizeL PanelWndOffset;		// map sub-panel to parent when embedded in parent
	// Legacy serialization fields - read from old files, synthesized on save.
	// Not used at runtime; ground truth is PanelLocationCenter/Zoom below.
	gmpi::drawing::PointL PanelScroll;
	gmpi::drawing::PointL StructScroll;
	gmpi::drawing::RectL PanelLocation;
	gmpi::drawing::RectL StructLocation;

	// Ground truth: view center (document coords) and zoom factor.
	// Both model and view use this representation; scrollPos is derived on demand.
	gmpi::drawing::Point PanelLocationCenter;
    gmpi::drawing::Point StructLocationCenter;
	float PanelLocationZoom;
	float StructLocationZoom;

	SkinInfo* m_skin;
	DO_LIST BaseList; // see also it_doc_ob
	static tinyxml2::XMLDocument PasteBuffer; // used for cut and paste. not in VST
	bool m_locked;
	bool m_is_rack_module = {};
	bool m_show_controls_on_panel_legacy; // the old pin (now superceded).
	CPatchManager* m_patch_manager;
	bool m_initialising = false;

	int32_t draggingLineFromMod = -1;
	int32_t draggingLineFromPin = -1;
	int32_t draggingLineToMod = -1;
	int32_t draggingLineToPin = -1;

public:
	void NotifyParameterChange( int action, class IGuiHostParameter* parameter );
	void RemoveOrphanedHostControls(); // drop module-owned host-controls left behind by deleted modules (call on the document's top container).
	bool has_own_patch_mgr()
	{
		return m_patch_manager != 0;
	}
	CPatchManager* get_patch_manager() override;
	void AdjustModuleTypePointer() override;
};

