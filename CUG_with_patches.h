#pragma once
#if !defined(AFX_TL_PATCHDEVICE_H__3458A031_15E1_11D6_AF11_000103170662__INCLUDED_)
#define AFX_TL_PATCHDEVICE_H__3458A031_15E1_11D6_AF11_000103170662__INCLUDED_

#include "CUG.h"
#include "mp_gui.h"
#include "GmpiSdkCommon.h"
#include "helpers/NativeUi.h"

class CUG_with_patches : public CUG
{
public:
	CUG_with_patches( Module_Info* p_type = 0 );
	virtual ~CUG_with_patches();
//	virtual void OnPatchChanged() {}
	virtual bool ProgramsAreChunks()
	{
		return true;
	}
	void DoPatchCopy();
	virtual void DoHostCommand(int p_command_id);
	void OnMenuCommand( int p_view_type, uint32_t p_command_id, gmpi::drawing::PointL mouse_pos );
//	virtual void ShowControlPanel();
	virtual void CopyPatch(int to_patch_lo,int to_patch_hi) {}
	virtual void SaveParam(bool isPreset, const std::wstring& p_filename) {}
	virtual void LoadParam( bool isPreset, const std::wstring& p_filename ) {}
	virtual	int VstUniqueID(bool vstWrapperMode = false)
	{
		return 0;
	}
	virtual int FileVersion()
	{
		return 0;
	}
	virtual int GetProgram()
	{
		return 0;
	}
	virtual void SetProgram(int p_program)
	{
	}
	virtual void setProgramName(const std::wstring& p_name)
	{
	}
	virtual	std::wstring getProgramName()
	{
		return (L"");
	}
	virtual std::wstring GetProgramNameIndexed(int p_index )
	{
		return (L"");
	}
	std::wstring getProgramNameList();
	virtual int GetnumPrograms()
	{
		return 128;
	}

	// Load/Save patches directly to disk.
	//FILE* OpenFile(const std::wstring& extension, bool write_mod, const std::wstring& p_filename);
	virtual int GetChunk(void** chunk_ptr, bool isPreset)
	{
		return 0;
	}
	virtual int SetChunk(void* data, int byteSize, bool isPreset)
	{
		return 0;
	}
	void LoadPatches(bool isPreset, const std::wstring& p_filename);
	void SavePatches(bool isPreset, const std::wstring& p_filename);
	void SaveChunk(bool isPreset, const std::wstring& p_filename);
	void LoadChunk( bool isPreset, const std::wstring& p_filename );

	virtual void ImportPreset(const std::string& filename, bool isPreset) {}
	virtual void ExportPreset(const std::string& filename, bool isPreset, int presetIndex = -1) {}

	virtual bool IsResizeable() { return false; }

protected:
	GmpiGui::FileDialog nativeFileDialog_legacy;
	gmpi::shared_ptr<gmpi::api::IFileDialog> nativeFileDialog2;
};

#endif // !defined(AFX_TL_PATCHDEVICE_H__3458A031_15E1_11D6_AF11_000103170662__INCLUDED_)
