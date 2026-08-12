#pragma once

#include "DocOb.h"
#include "ModuleFactory_Editor.h"
#include "Plug4.h"
#include "cpu_accumulator.h"
#include "conversion.h"

class CPlugIO4;

// utility
std::wstring ModuleIdToFilename(std::wstring uniqueId);
std::wstring datatypeToString(int dt);

class CUG : public CDocOb
{
public:
	void GetTimingRequirements( int& p_flags ) override;
	gmpi::drawing::RectL getViewObRect(int p_view_type) override;
	void setViewObRect(int p_view_type, gmpi::drawing::RectL& p_rect) override;
	void offsetViewObRect(int p_view_type, int dx, int dy) override;
	gmpi::drawing::RectL GetAbsolutePanelRect();
	DECLARE_BUILD_FUNC(CUG);

protected:
	CUG( Module_Info* p_type = 0 );
	virtual ~CUG();

	template< class Serializer >
	void SerialiseC(Serializer& s)
	{
		s("structRect", m_struct_rect);
		s("muted", mute);
		s("excludeFromVst", excludeFromVst);
		s("excludeFromJUCE", excludeFromJUCE);
		s("name", name, WStringToUtf8(::GetName(getType())));
	}

public:
	void OnConnectDialog();

	void Upgrade(int from_version) override;
	int getPlugIdx(IPlug* p_plug);
	int getRuntimePinIndex(IPlug* p_plug, bool isDspPin);
	// get index of plug out of all plugs, DSP and GUI combines (GUI first).
	int getPinNumber(IPlug* p_plug);
	int getParameterIdx(IPlug* p_plug);
	void OnDelete() override;
	virtual bool show_title_on_panel();
	bool HighlightLineTo(CUG* p_ug, int32_t flags);
	void HighlightLines(int pinIdx, int highlightType);
	void UnHighlight();
	void DoReplaceDialog();
	void RemoveAllLines();

	virtual std::wstring CustomiseEnumList( IPlug* plug, std::wstring default_list );
	void OnMenuCommand(int p_view_type, uint32_t p_command_id, gmpi::drawing::PointL mouse_pos = { -1,-1 }) override;

	virtual TiXmlElement* ExportXml(TiXmlElement* XmlParent, ExportFormatType targetType);
	virtual void Export(Json::Value& JsonParent, ExportFormatType targetType) override;
	void Export(tinyxml2::XMLElement* moduleElement, ExportFormatType targetType) override;
	void Import(std::map<int32_t, CUG*>& uniqueIds, tinyxml2::XMLElement* moduleElement, ExportFormatType targetType) override;
	virtual void OnReplace(CUG* oldModule);
	std::vector< std::pair<bool, std::string> > GetPlugsText();
	CPlug4* InsertPlugSorted(CPlug4* p_plug);
	virtual void RemovePlug(IPlug* p);
	CPlugIO4* GetSparePlug();
	IPlug* GetPlug(std::wstring name);
	CPlug4* MakePlug( class InterfaceObject* i );
	void AddPlug(CPlug4* p );
	IPlug* GetPlug(EDirection p_direction, EPlugDataType p_datatype, int p_index);
	IPlug* GetPlug(int i);
	IPlug* GetPlugForSerialize(int i);
	virtual void OnPlugSetName(IPlug* p);
	virtual void OnPlugDefaultChange(IPlug* plug);
	virtual void OnDownstreamPlugChange(IPlug* p_my_plug, IPlug* p_downstream_plug, int p_msg_id);
	virtual void OnNewConnection(CLine2* /*p_line*/) {}
	virtual void OnDisconnect(IPlug* /*plug*/) {}
	void doAutoConfigureParameter( IPlug* from, IPlug* to );
	void UpgradeFrom(CUG* old);
	void MoveConnectorsFrom(CUG* old_module);

	std::unique_ptr<cpu_accumulator> cpu_meter;
	void AttachDebugger( void );
	class PatchParameter_base* getFirstPatchParam();
	void Initialise(bool loaded_from_file = false ) override; // called on first create and serialise

	void ToggleMute();
	void SetMute(bool m);
	bool doExport();
	virtual bool GetMute();
	virtual void debugpoly();
	std::wstring GetName() override;
	std::wstring getPath();
	void SetName(const std::wstring& new_name) override;
	void OnDspMsg(int p_msg_id, gmpi::hosting::my_input_stream& p_stream) override;
	void AdjustModuleTypePointer() override;

	void DeleteBypass();
	void UpdatePlugEnumLists();
	void BuildSkeletonCode(std::wstring plugin_name);
	void setHoverScopePin(int pinIdx);

#if defined( _DEBUG )
	void DebugPrintPlugName( IPlug* p );
#endif
	std::vector<CPlug4*> Plugs;

	gmpi_forms::State<std::string> name;

private:
	void AddStandardPlugs();
	static std::map<std::wstring, std::wstring> moduleUpgrades;
	bool mute;
	gmpi::drawing::RectL m_struct_rect;
	int hoverScopePin = -1;

public:
	bool excludeFromVst{};
	bool excludeFromJUCE{};
	inline static bool is_containerizing = false;
#if defined( _DEBUG )
	int DebugGetPinIndex(IPlug* pin);
	void DebugIdentify();
	static bool debugUpgradeInProgress;
#endif
};

// Utility
inline int getPinNumber(IPlug* p)
{
	return p->UG()->getPinNumber(p);
}


