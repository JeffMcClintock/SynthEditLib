/////////////////////////////////////////////////////////////////////////////
// Class SkinMgr
#pragma once
#include <filesystem>
#include <list>
#include <string>
#include "assert.h"
#include "MySmartPtr.h"
#include "Drawing.h"

// description of font by skin and category

struct fontSpecification
{
	std::wstring category;
	std::wstring facename{ L"Arial" }; // suitable default font, Arial is a windows core font
	int size{ 12 };
	int32_t color{ 0xFFFFFF };
//	int32_t color_background{ -1 }; // transparent
	int32_t color_background{ 0 }; // transparent
	int vst3_vertical_offset{ 0 };
	int flags{ 0 };  //alignment etc
	bool verticalSnapBackwardCompatibilityMode{ true }; // reverts to classic font positioning.
};

class SkinFontInfo
{
public:
	SkinFontInfo(const fontSpecification& spec);
	GmpiDrawing::SizeL AverageCharSize();
	std::wstring GetActualFontName();
	//int pixelHeight();

	std::wstring category;
	std::wstring facename;
	int size;
	int color;
	int color_background;
	int flags;  //alignment etc
	int vst3_vertical_offset;
	bool verticalSnapBackwardCompatibilityMode = true; // reverts to classic font positioning.

	std::wstring systemfacename;
	GmpiDrawing::SizeL average_char_size;
	
protected:
	SkinFontInfo() {}
};

// font flags
#define TTL_TRANSPARENT_BACKGROUND	1
#define TTL_MUTE					2
#define TTL_CENTERED				8
#define TTL_RIGHT					16
#define TTL_LOCKED					32
#define TTL_UNDERLINE				64
#define TTL_BOLD					128
#define TTL_LIGHT					256
#define TTL_ITALIC					512

// True if this app keeps its skins in a USER-WRITABLE folder under the
// documents directory, and expects the built-in set copied there on first
// use. Full SynthEdit does.
//
// TIDE does not. Jeff's ruling, 2026-08-24: "TIDE does not support
// user-defined skins. The default Skin files should load from TIDE's own
// private resource folder only." So it answers false, is pointed at its own
// bundle, and nothing under the user's documents folder is created, copied or
// stamped. PLAN constraints 4 and 8.
//
// That bundle folder may legitimately hold nothing. TIDE has exactly one
// default look and is heading for hardcoded defaults with no skin files at
// all, so an absent folder is an expected state rather than a missing asset:
// ScanFiles() uses the error_code overload of directory_iterator, which yields
// nothing for a path that does not exist, and it always pushes the "global"
// SkinInfo first -- so getSkin() still returns a usable skin with zero files
// on disk. TIDE has in fact been running with an empty skin folder all along;
// before this change it was an empty folder in the USER'S HOME.

bool AppUsesUserSkinsFolder();

class SkinMgr;

class SkinInfo
{
	friend class SkinMgr;

public:
	SkinInfo(SkinMgr* p_skin_mgr = 0, const std::wstring& p_name = L"", int p_id = 0);
	~SkinInfo();
	void Load();
	void Load(const std::filesystem::path& globalTxtPath);
	std::wstring GetActualFontName( const std::wstring& p_font_category );
	int SkinId()
	{
		return m_skin_id;
	}
	//std::string GetPixelHeights();
	std::wstring Name;

private:
	SkinFontInfo* getFontDescription(std::wstring font_category);
	int m_skin_id;
	std::list<SkinFontInfo*> fonts;
	SkinMgr* m_skin_mgr;
};

class SkinMgr
{
public:
	static SkinMgr* Instance();
	void setSkinFolder(const std::wstring& p_skin_folder);
	std::wstring SkinFolder();
	void UnloadSkins();
	SkinInfo* getSkin(const std::wstring& p_skin_name);
	SkinInfo* getSkin(int p_idx);
	int getSkinCount()
	{
		return (int)skin_list.size();
	}
	void ScanFiles();
	std::string getEffectiveFontInfo(const wchar_t * skinName);
	void EditSkin(SkinInfo* skin);

protected:
	std::list<SkinInfo*> skin_list;

private:
	SkinMgr();					// enforce singleton
	SkinMgr(const SkinMgr&);	// enforce singleton
	~SkinMgr();

	std::wstring m_cant_find_skin;
	std::wstring m_skin_folder;
};
