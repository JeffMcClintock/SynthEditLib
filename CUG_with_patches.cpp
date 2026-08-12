#include "Dialogs_editor.h"

#include <sstream>
#include <iomanip>
#include <filesystem>
#include "Application.h" // must be first to fix STL/MFC problems
#include "SynthEditDocBase.h"
#include "CUG_with_patches.h"
#include "UgDatabase.h"
#include "resource.h"
#include "PatchManager.h"

using namespace std;

// set control window size
#undef WE_CX
#undef WE_CY
#define WE_CX 128
#define WE_CY 50

CUG_with_patches::CUG_with_patches( Module_Info* p_type ) : CUG(p_type)
{
}

CUG_with_patches::~CUG_with_patches()
{
}

void CUG_with_patches::DoHostCommand(int p_command_id)
{
	switch(p_command_id)
	{
	case HC_CopyPatch:
		DoPatchCopy();
		break;

	case HC_LoadPatch:
		LoadPatches(true, (L""));
		break;

	// Load/Save patches directly to disk.
	case HC_SavePatch:
	{
		std::wstring filename;
		filename = filename + GetProgramNameIndexed(GetProgram());
		const auto filename_uft8 = WStringToUtf8(filename) + ".xmlpreset";

		constexpr auto dialogType = (int) gmpi::api::FileDialogType::Save;
		Application()->getCurrentDialogHost()->createFileDialog(dialogType, (gmpi::api::IUnknown**) nativeFileDialog2.put());

		if(!nativeFileDialog2)
			return;

		nativeFileDialog2->setInitialFilename(filename_uft8.c_str());

		nativeFileDialog2->addExtension("xmlpreset");
		nativeFileDialog2->addExtension("aupreset");
		nativeFileDialog2->addExtension("vstpreset");
		nativeFileDialog2->addExtension("*");

		nativeFileDialog2->showAsync(
			nullptr,
			new gmpi::sdk::FileDialogCallback(
				[this](const std::string& selectedPath) -> void
				{
					SavePatches(true, Utf8ToWstring(selectedPath));
				})
			);
	}
	break;

	case HC_LoadBank:
		LoadPatches(false, (L""));
		break;

	case HC_SaveBank:
	{
		auto* app = Document()->Application();
		const auto cugHandle = Handle();
		const std::wstring extension(L"xmlbank");

		std::filesystem::path suggested_name(GetName());
		suggested_name.replace_extension(extension);

		app->FileDialogAsync(false, extension, suggested_name.wstring(),
			[app, cugHandle, extension](int result, std::wstring filename)
			{
				if (result != IDOK)
					return;

				auto* document = app->Document();
				if (!document)
					return;

				auto* cug = dynamic_cast<CUG_with_patches*>(document->uniqueIdDatabase.HandleToObjectWithNull(cugHandle));
				if (!cug)
					return;

				std::filesystem::path path(filename);
				if(!path.has_extension())
				{
					path.replace_extension(extension);
					filename = path.wstring();
				}

				cug->SavePatches(false, filename);
			}
		);
	}
	break;

	case HC_reset:		// resetting menu
	case HC_null:
		break;

	default:
		assert(false);
	};
}

// should end up obsolete !!!
void CUG_with_patches:: OnMenuCommand( int p_view_type, uint32_t p_command_id, gmpi::drawing::PointL mouse_pos )
{
	switch( p_command_id )
	{
	case ID_VST_FILE_LOAD_BANK:
		LoadPatches(false, (L""));
		break;

	case ID_VST_FILE_SAVE_BANK:
		SavePatches(false, (L""));
		break;

	case ID_VST_FILE_LOAD_INST:
		LoadPatches(true, (L""));
		break;

	case ID_VST_FILE_SAVE_INST:
		SavePatches(true, (L""));
		break;

	case ID_PATCH_MGR:
	{
		DoPatchCopy();
	}
	break;

	default:
		if( p_command_id >= ID_VST_PROG_CHANGE && p_command_id < ID_VST_PROG_CHANGE_RANGE )
		{
			SetProgram( p_command_id - ID_VST_PROG_CHANGE );
		}
		else
		{
			CUG::OnMenuCommand(p_view_type, p_command_id, mouse_pos);
		}

		break;
	}
}

void CUG_with_patches::DoPatchCopy()
{
	doDialogPatchManager(this);
}

// Load/Save patches directly to disk.

void CUG_with_patches::LoadPatches(bool isPreset, const std::wstring& p_filename)
{
	if( ProgramsAreChunks() )
	{
		LoadChunk(isPreset, p_filename);
	}
	else
	{
		LoadParam(isPreset, p_filename);
	}
}

void CUG_with_patches::SavePatches(bool isPreset, const std::wstring& p_filename)
{
	if( ProgramsAreChunks() )
	{
		SaveChunk(isPreset, p_filename);
	}
	else
	{
		SaveParam(isPreset, p_filename);
	}
}

void CUG_with_patches::SaveChunk(bool isPreset, const std::wstring& filename)
{
	if (GetExtension(filename) == L"xmlpreset" || GetExtension(filename) == L"xmlbank" || GetExtension(filename) == L"aupreset" || GetExtension(filename) == L"vstpreset")
	{
		// Do XML Export.
		ExportPreset(WStringToUtf8(filename), isPreset);
		return;
	}
}

void CUG_with_patches::LoadChunk(bool isPreset, const std::wstring& p_filename)
{
	constexpr auto dialogType = (int)gmpi::api::FileDialogType::Open;
	Application()->getCurrentDialogHost()->createFileDialog(dialogType, (gmpi::api::IUnknown**)nativeFileDialog2.put());

	if (!nativeFileDialog2)
		return;

	if (isPreset)
	{
		nativeFileDialog2->addExtension("xmlpreset");
		nativeFileDialog2->addExtension("aupreset");
		nativeFileDialog2->addExtension("vstpreset");
	}
	else
	{
		nativeFileDialog2->addExtension("xmlbank");
	}
	nativeFileDialog2->addExtension("*");

	nativeFileDialog2->showAsync(
		nullptr,
		new gmpi::sdk::FileDialogCallback(
			[this](const std::string& filename) -> void
			{
				auto fileExtension = GetExtension(filename);

				if (fileExtension == "xmlpreset" || fileExtension == "vstpreset" || fileExtension == "aupreset")
				{
					ImportPreset(filename, true);
					return;
				}

				if (fileExtension == "xmlbank")
				{
					ImportPreset(filename, false);
					return;
				}
			})
	);
}

std::wstring CUG_with_patches::getProgramNameList()
{
	//	std::wstring temp;
	int programs = GetnumPrograms();
	std::wostringstream oss;

	for( int i = 0 ; i < programs ; i++ )
	{
		//std::wstring t;
		//t.Format((L"%s,"), GetProgramNameIndexed(i) );
		//temp += t;
		oss << GetProgramNameIndexed(i) << L",";
	}

	return oss.str();
}

