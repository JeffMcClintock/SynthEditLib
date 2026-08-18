#pragma once

#include <string>
#include <map>
#include <list>
#include <vector>
#include <set>
#include <memory>
#include <limits>
#include "datatype_to_id.h"
#include "./ui_msg_target.h"
#include "./IGuiHost.h"
#include "modules/se_sdk3_hosting/IGuiHost2.h"
#include "HostControls.h"
#include "CUG.h"
#include "IPluginGui.h"

#define defaultPolyphony 6
#define defaultPolyphonyReserve 3
const int maxXmlAttributeBytes = static_cast<int>((std::numeric_limits<int32_t>::max)()); // 10240

namespace SE2
{
	class IPresenter;
}

typedef std::vector<IPluginGui*> guis_type;

enum {	  SE_CHUNK_MAGIC = 0xd00fa
		, SE_CHUNK_MAGIC2 =0xd00ba
		, SE_CHUNK_MAGIC3 =0xd00ca
		, SE_CHUNK_MAGIC4
		, SE_CHUNK_MAGIC5
		, SE_CHUNK_MAGIC6   // V1.1
		, SE_CHUNK_MAGIC6B  // V1.1 (with MIDI Learn saved)
	 };

struct paramSummary
{
	std::string name;
	int datatype;
};

// one DAW-automatable parameter, as exposed by a GMPI plugin export.
struct GmpiParamExport
{
	int index;               // DAW parameter index (VST parameter number)
	std::string name;
	float defaultNormalized; // patch 0 value, normalized 0..1
};

// True for host-controls whose parameter exists only because a module references it via a pin
// (oversampling, user shared-parameters, ...), and which should therefore be culled when no live
// module references it. Structural host-controls owned by the container or document (polyphony,
// voice-allocation, presets, patch-cables, ...) return false - they must persist regardless.
bool isModuleOwnedHostControl(HostControls hostControlId);

class CPatchManager : public IGuiHost2
{
	friend class PmParameterIterator;

	struct paramUpdate2
	{
		std::wstring name;
		std::vector<std::string> paramValues;
		std::string rangeHi;
		std::string rangeLo;
	};
	static std::map< int, paramUpdate2 > parameterUpgrades;

public:

	CPatchManager(class CContainer* p_container = 0);
	virtual ~CPatchManager();
	void RegisterHandles( UniqueSnowflakeOwner* uniqueIdDatabase, bool paste_mode );
	void SetContainer(CContainer* p_container)
	{
		m_container=p_container;
	}
	CContainer* Container()
	{
		return m_container;
	}
	void SetProgram(int p_program);
	int GetProgram();
	int getNumPrograms();
	std::wstring getProgramNameIndexed( int p_index = -1 );
	std::wstring getProgramCategoryIndexed(int p_index = -1);
	void UpdateProgramCategory();
	void UpdateProgramCategoryList();
	void updateProgramCategoryListItem(std::wstring p_category, int p_index = -1);
	void setProgramCategoryIndexed(std::wstring p_category, int p_index = -1);
	void setProgramNameIndexed( std::wstring p_program_name, int p_index = -1);

	// IGuiHost interface
	int32_t GetParameterIterator( int subPathNodehandle, class IGuiHostParameterIterator** returnValue);

	// IGuiHost2. New simplified way. No iterator crap.
	virtual int32_t RegisterGui2(gmpi::api::IParameterObserver* gui) override;
	void DeregisterAllGuiPatchAutomators();
	virtual int32_t UnRegisterGui2(gmpi::api::IParameterObserver* gui) override;
	// testing new idea from Controller
	void OnSetHostControl(int hostControl, int32_t paramField, int32_t size, const void * data, int32_t voice);
	RawView getParameterValue(int32_t parameterHandle, int32_t moduleFieldId = gmpi::MP_FT_VALUE, int32_t voice = 0) override;
	void setParameterValue(RawView value, int32_t parameterHandle, gmpi::FieldType moduleFieldId = gmpi::MP_FT_VALUE, int32_t voice = 0) override;
	void initializeGui(gmpi::api::IParameterObserver* gui, int32_t parameterHandle, gmpi::Field FieldId) override;
	int32_t sendSdkMessageToAudio(int32_t handle, int32_t id, int32_t size, const void* messageData) override;
	virtual int32_t getParameterHandle(int32_t moduleHandle, int32_t moduleParameterId) override;
	virtual int32_t getParameterModuleAndParamId(int32_t parameterHandle, int32_t* returnModuleHandle, int32_t* returnModuleParameterId) override;
	virtual int32_t resolveFilename(const wchar_t* shortFilename, int32_t maxChars, wchar_t* returnFullFilename) override;
	virtual int32_t getController(int32_t handle, gmpi::IMpController** returnController) override;
	virtual void serviceGuiQueue() override;
	void setMainPresenter(SE2::IPresenter* /* presenter */) override
	{
		assert(false);
	}
	void undoTransanctionStart() override
	{
		assert(false);
	}	void undoTransanctionEnd() override
	{
		assert(false);
	}
	void DoHostCommand(int p_command_id);
	PatchParameter_base* RegisterParameter( class CUG* module, struct parameter_description& descriptor );
	void UnRegister( class CUG* module, int moduleParameterId = -1 );

	void TransferPatchData(CPatchManager* destPatchManager = nullptr);
	void TransferOldPatchData(CUG* p_old, CUG* p_new);
	void TransferParameter( CUG* from, CUG* to );
//	void CleanupAttachedHostControls( CContainer *container, bool* flaggedHostControls );

	void ExportXml(class TiXmlElement* XmlParent, ExportFormatType targetType );
	void Export(tinyxml2::XMLElement * object_json, ExportFormatType targetType);
	void Import(tinyxml2::XMLElement* moduleElement, ExportFormatType targetType, std::vector< std::pair<PatchParameter_base*, int>>& parameterModuleHandles);
	void InitModulePointers(std::map<int32_t, CUG*>& uniqueIds, std::vector< std::pair<PatchParameter_base*, int>>& parameterModuleHandles);

	void ExportGetSortedParameters(std::list<PatchParameter_base*>& sortedList, ExportFormatType targetType);
	void ExportAssignParamIndexes(ExportFormatType targetType);
	std::map<int32_t, paramSummary> ExportParameterEnums();
	std::vector<GmpiParamExport> ExportGmpiParameters();

	std::string exportVst3Preset(int32_t pluginId, int patch);
	void ExportPresetXml(int32_t pluginId, class TiXmlNode* XmlParent, bool isSinglePreset, int presetIndex = -1);
	void ImportPresetXml(class TiXmlNode* XmlParent, bool isSinglePreset, int presetIndex = -1, std::string overridingCategory = "");
	void ExportSubPresetXml(class TiXmlElement* XmlParent, CContainer* container);
	void ImportSubPresetXml(TiXmlElement* XmlParent, CContainer* container, bool strictMatching);
	void setModified(bool presetIsModified);
	std::list<PatchParameter_base*> GetSubParameters(CContainer* container);
	void AddParameter(PatchParameter_base* p_param);

	// Delete any module-owned host-control parameter in this patch manager that isn't in
	// 'referenced' (the set of parameters still wired to a live module pin). See
	// CContainer::RemoveOrphanedHostControls().
	void RemoveUnreferencedModuleOwnedHostControls(const std::set<PatchParameter_base*>& referenced);

	PatchParameter_base* GetParameter( int p_unique_id );
	PatchParameter_base* GetParameter( CUG* module, int moduleParameterId );
	PatchParameter_base* GetHostGeneratedParameter(HostControls, bool auto_create, CContainer* parentContainer, const wchar_t* userDefinedName = L"");

	void CopyPatch( int to_patch_lo, int to_patch_hi);
	void OnParameterUpdate( PatchParameter_base* param, enum ParameterFieldType field, int voice, const void* data, int32_t size );
#if defined( _DEBUG )
	void DebugDump();
	void Verify();
#endif
	void ChangeParametersAttachedContainer(CContainer* from, CContainer* to);
	void onContainerIgnoreProgramChangeUpdate();

	int getMidiChannel();
	void setMidiChannel( int p_chan );

	class ApplicationBase* Application();

private:
	int m_program;
	int midiChannel;
	std::map<int, std::wstring> m_program_names;
	std::vector<PatchParameter_base*> m_parameters;
	CContainer* m_container;
	std::vector<gmpi::api::IParameterObserver*> m_guis2;
};

class PmParameterIterator : public IGuiHostParameterIterator
{
public:
	PmParameterIterator(CPatchManager* p_patch_mgr, int subPathNodehandle ) : m_patch_mgr(p_patch_mgr), moduleHandle_(subPathNodehandle) {}

	virtual int32_t First();
	virtual int32_t Next();
	virtual int32_t IsDone( bool* returnValue);
	virtual int32_t Release()
	{
		delete this;
		return 0;
	} // possibly should be ref counted
	virtual PatchParameter_base* Current();

private:
	void SkipUnqualified();

	int moduleHandle_;
	CPatchManager* m_patch_mgr;
	std::vector<PatchParameter_base*>::iterator m_it;
};
