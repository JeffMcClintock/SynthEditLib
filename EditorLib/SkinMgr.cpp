#include "helpers/browseto.h"

#include <sstream>
#include <fstream>
#include <map>
#include <filesystem>
#include "SkinMgr.h"
#include "Application.h"
#include "conversion.h"
#include "tinyxml/tinyxml.h"
#include "modules/shared/ImageMetadata.h"
#include "assert.h"
#include "SafeMessageBox.h"
#include "BundleInfo.h"
#include "GmpiResourceManager.h"
#include "se_version.h"

#ifdef __APPLE__
#include <CoreText/CoreText.h>
#endif

using namespace std;

// don't set too low, else font's start randomly reverting to 'system'
#define MAX_CACHED_FONTS 10

SkinMgr::SkinMgr()
{
    const auto commonDocuments = BundleInfo::instance()->getCommonDocumentFolder();
    setSkinFolder( (commonDocuments / L"SynthEdit Projects" / "skins" / "").wstring() );

	ScanFiles();
}

// Meyer's singleton
SkinMgr* SkinMgr::Instance()
{
	static SkinMgr obj;
	return &obj;
}

SkinMgr::~SkinMgr()
{
	UnloadSkins();
}

void SkinMgr::setSkinFolder(const std::wstring& p_skin_folder)
{
	m_skin_folder = p_skin_folder;

	// Ensure the user skin folder contains the built-in skins on first use.
	// If not, copy them recursively from the application folder.
	const auto destRoot = std::filesystem::path(m_skin_folder);
	const auto destDefault = destRoot / L"default";

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
	bool shouldCopy = versionChanged || !std::filesystem::exists(destDefault, ec);

	if (shouldCopy)
	{
		ec.clear();
		std::filesystem::create_directories(destRoot, ec);

		// Application ships with skins in "{home}/Resources/skins".
		const auto srcRoot = std::filesystem::path(GetHomeDir()) / "Resources" / L"skins";

		ec.clear();
		if (std::filesystem::exists(srcRoot, ec))
		{
			std::filesystem::create_directories(destRoot, ec);
			ec.clear();
			std::filesystem::copy(
				srcRoot,
				destRoot,
				std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing,
				ec
			);
		}
	}

	// Update stored version for next launch
	if (versionChanged || storedVersion == 0)
	{
		std::filesystem::create_directories(versionFile.parent_path(), ec);
		std::wofstream outFile(versionFile);
		if (outFile.is_open())
		{
			outFile << SE_APP_BUILD_NUMBER;
			outFile.close();
		}
	}
}

void SkinMgr::EditSkin(SkinInfo* skin)
{
    std::wstring filename = (m_skin_folder);
    filename.append( skin->Name );
    
    gmpi::browse_to(filename);
}

// scan the 'skins' directory for subdirectry names,
// these become the list of available skins
void SkinMgr::ScanFiles()
{
	const auto search_dir = std::filesystem::path(m_skin_folder);
	UnloadSkins();
	int index = 0;
	// skins being phased out. Replaced with 'global' skin
	// (skin structure still usefull for sharing bitmaps and fonts)
	skin_list.push_back( new SkinInfo(this, L"global", index++) );

	std::error_code ec;
	for (const auto& entry : std::filesystem::directory_iterator(search_dir, ec))
	{
		if (ec)
		{
			break;
		}

		if (entry.is_directory())
		{
			const auto name = entry.path().filename();
			if (name == L"." || name == L"..")
			{
				continue;
			}

			const auto SkinName = name.wstring();
			skin_list.push_back(new SkinInfo(this, SkinName, index++));
		}
	}

	if( index == 0 )
	{
		std::wstring msg( L"Error: Can't find the default skin directory.  You may need to re-install SynthEdit.  " );
        msg += search_dir.generic_wstring();
		SafeMessagebox(0, msg.c_str(), L"", 0x30 /*MB_ICONEXCLAMATION*/ );
	}
}

#ifdef _WIN32
int CALLBACK FontCallBack(LPENUMLOGFONT lpnlf, LPNEWTEXTMETRIC lpnfm, int FontType, int* p)
{
	vector< wstring > *sa = (vector< wstring > *)p;
	sa->push_back( lpnlf->elfFullName );
	/*
	 In this place you can obtain all possible values of lpszFaceName as
	lpnlf->elfLogFont.lfFaceName
	This function will being called for each font, available in system;
	*/
	return 1; // It's important to return nonzero value.
}
#endif

SkinInfo::SkinInfo(SkinMgr* p_skin_mgr, const std::wstring& p_name, int p_id) :
	Name(p_name)
	,m_skin_id(p_id)
	,m_skin_mgr(p_skin_mgr)
{
	if(p_skin_mgr)
		Load();
}

void SkinInfo::Load()
{
    const std::filesystem::path skinfolder(m_skin_mgr->SkinFolder());

	// Parse "global.txt" file for font info
    auto filename = skinfolder / Name / "global.txt";

	if( !exists(filename) ) // then try default
		filename = skinfolder / "default" / "global.txt";

	if (!exists(filename))
		return;

	Load(filename);
}

void SkinInfo::Load(const std::filesystem::path& filename)
{
	// create list of available fonts
	vector< wstring > available_fonts;
	// choose best available font
#ifdef _WIN32
	HDC hDC = GetDC(0);
	// hInstance -- instance of your application; you obtain it as first parameter of WinMain.
	FONTENUMPROC lpfnEnumProc = (FONTENUMPROC) MakeProcInstance((FARPROC) FontCallBack, hInstance);
	EnumFontFamilies(hDC, nullptr, lpfnEnumProc, (LPARAM) &available_fonts);
	FreeProcInstance((FARPROC) lpfnEnumProc);
	ReleaseDC(0, hDC);
#elif defined(__APPLE__)
	if (CFArrayRef families = CTFontManagerCopyAvailableFontFamilyNames())
	{
		const CFIndex count = CFArrayGetCount(families);
		for (CFIndex i = 0; i < count; ++i)
		{
			auto name = static_cast<CFStringRef>(CFArrayGetValueAtIndex(families, i));
			if (!name)
				continue;

			const CFIndex length = CFStringGetLength(name);
			std::wstring wname;
			wname.reserve(length);
			for (CFIndex j = 0; j < length; ++j)
				wname.push_back(static_cast<wchar_t>(CFStringGetCharacterAtIndex(name, j)));

			available_fonts.push_back(std::move(wname));
		}
		CFRelease(families);
	}
#endif

	std::vector<fontSpecification> fontSpecs;
	fontSpecs.push_back({ L"dummy", L"", 0, 0, 0}); // any orphaned font info will be harmlessly copied here

	// read image info from text file
	std::wstring s;
	vector< wstring > token;

	std::ifstream file(filename.c_str());
	std::string temp123;
	while (std::getline(file, temp123))
	{
		// sort each line into tokens
		s = Utf8ToWstring(temp123);
		int index = -1;

		while( s.size() > 0 )
		{
			int i = 0;
			std::wstring tk;

			// handle speach marks
			if( s[0] == '"' || s[0] == '\'' )
			{
				i++; // skip first quote
				auto quote_char = s[0];

				// find closing quote
				while( i < s.size() )
				{
					if( s[i] == quote_char )
					{
						break;
					}

					i++;
				}
			}

			while( i < s.size() )
			{
				if( s[i] == ' ' || s[i] == ',' )
				{
					break;
				}

				i++;
			}

			tk = Left(s,i);
			tk = Trim(tk);

			if( i > 0 )
			{
				if( tk[0] == ';' ) // comment, ignore remainder
				{
					s = L"";
					tk = L"";
					i = -1;
				}
			}

			i = min(i, static_cast<int>(s.size())-1);
			s = Right( s, s.size() - i - 1);
			s = TrimLeft(s);

			if( ! tk.empty() )
			{
				// remove speach marks if any
				if( tk[0] == '"' || tk[0] == '\'' )
				{
					tk = Right( tk, tk.size() - 1);

					if( Right(tk, 1) == L"\"" || Right(tk, 1) == L"'" )
					{
						tk = Left( tk, tk.size() - 1);
					}
				}

				token.push_back( tk );
				++index;
			}
		}

		// =================== PROCESS DESCRIPTION FILE ==================
		if( token.size() > 0 )
		{
			//				_RPT1(_CRT_WARN, "%s\n", token[0] );
			if( token[0] == L"FONT_CATEGORY" )
			{
				if( token.size() > 1 )
				{
					std::wstring categ = token[1];
					transform(categ.begin(), categ.end(), categ.begin(), towlower);
					fontSpecs.push_back({ categ });
				}
			}

			// Arial, Times New Roman, and Courier New are core fonts to Windows
			if( token[0] == L"font-family" )
			{
				// seach supplied font list for supported font
				int font_entry = 1;

				while( token.size() > font_entry ) //&& !font_is_available )
				{
					std::wstring suggested_font = token[font_entry];

					for( int i = static_cast<int>(available_fonts.size()) - 1 ; i >= 0 ; i-- )
					{
						//	_RPT1(_CRT_WARN, "%s\n", available_fonts[i] );
						if( Lowercase(available_fonts[i]) == Lowercase( suggested_font ) )
						{
							//NO suggested_font.Make Lower(); // make comparisons easier
							fontSpecs.back().facename = available_fonts[i]; // get correct Case spelling, crucial
							font_entry = 999; // break outer loop
							break;
						}
					}

					font_entry++;
				}
			}

			if( token.size() > 0 )
			{
				if (token[0] == L"font-size")
				{
					fontSpecs.back().size = StringToInt(token[1]);
				}

				if (token[0] == L"vst3-vertical-offset")
				{
					fontSpecs.back().vst3_vertical_offset = StringToInt(token[1]);
				}

				if (token[0] == L"legacy-vertical-offset")
				{
					fontSpecs.back().verticalSnapBackwardCompatibilityMode = (0 != StringToInt(token[1]));
				}

				if( token[0] == L"font-color" )
				{
					if( token[1][0] == L'#' )
					{
						fontSpecs.back().color = StringToInt( Right( token[1], token[1].size()-1), 16 );
						if (token[1].size() < 8) // no alpha specified, default to 0xff
						{
							fontSpecs.back().color |= 0xff000000;
						}
					}
					else
					{
						fontSpecs.back().color = StringToInt( token[1] );
						fontSpecs.back().color |= 0xff000000; // default alpha to 0xff
					}
				}

				if( token[0] == L"background-color" )
				{
					if( token[1][0] == L'#' )
					{
						fontSpecs.back().color_background = StringToInt( Right( token[1], token[1].size()-1), 16 );
						
						if (token[1].size() < 8) // no alpha specified, default to 0xff
						{
							fontSpecs.back().color_background |= 0xff000000;
						}
					}
					else
					{
						fontSpecs.back().color_background = StringToInt( token[1] );
						fontSpecs.back().color_background |= 0xff000000; // default alpha to 0xff
					}

					SetBit( fontSpecs.back().flags, TTL_TRANSPARENT_BACKGROUND, false );
				}

				if( token[0] == L"text-align" )
				{
					if( tolower( token[1][0] ) == L'r' ) // right justified
					{
						fontSpecs.back().flags |= TTL_RIGHT;
					}

					if( tolower( token[1][0] ) == L'c' ) // centered
					{
						fontSpecs.back().flags |= TTL_CENTERED;
					}
				}

				if( token[0] == L"text-decoration" )
				{
					if(  tolower( token[1][0] ) == L'u' ) // Underline
					{
						fontSpecs.back().flags |= TTL_UNDERLINE;
					}
				}

				if( token[0] == L"font-style" )
				{
					if(  tolower( token[1][0] ) == L'i' ) // Italic
					{
						fontSpecs.back().flags |= TTL_ITALIC;
					}
				}

				if( token[0] == L"font-weight" )
				{
					if(  tolower( token[1][0] ) == L'b' ) // Bold
					{
						fontSpecs.back().flags |= TTL_BOLD;
					}
					else
					{
						if(  tolower( token[1][0] ) == L'l' ) // Light
						{
							fontSpecs.back().flags |= TTL_LIGHT;
						}
						else // numeric spec
						{
							int fw = StringToInt(token[1]);

							if( fw != 0 )
							{
								if( fw < 301 )
									fontSpecs.back().flags |= TTL_LIGHT;
								else if( fw > 550 )
									fontSpecs.back().flags |= TTL_BOLD;
							}
						}
					}
				}
			}
		}

		token.clear();
	}

	// instansiate all font specs (except the first dummy one)
	for (auto& spec : fontSpecs)
	{
		if (!spec.facename.empty())
		{
			fonts.push_back(new SkinFontInfo(spec));
		}
	}
}

/*
; font-family, specific font name, or generic...
;   'serif' (e.g. Times)
;   'sans-serif' (e.g. Helvetica)
;   'cursive' (e.g. Zapf-Chancery)
;   'fantasy' (e.g. Western)
;   'monospace' (e.g. Courier New)
*/

SkinInfo* SkinMgr::getSkin(const std::wstring& p_skin_name)
{
	// VST version only allows one skin exactly
	SkinInfo* default_si = 0;
	for (auto& si : skin_list)
	{
		if( Lowercase(si->Name) == Lowercase( p_skin_name ) )
		{
			return si;
		}

		if( Lowercase(si->Name) == L"default" || default_si == 0 )
		{
			default_si = si;
		}
	}

	// inhibit multiple messages (can't find skin)
	if( Lowercase(m_cant_find_skin) == Lowercase(p_skin_name ) )
	{
		std::wostringstream oss;
		oss << L"Can't find skin '" << p_skin_name << L"'. Using " << (default_si ? default_si->Name : std::wstring());
		std::wstring msg = oss.str();
		// pop up error message. Not wanted in VST mode (main container will try to use DEFAULT skin, available or not)
		SafeMessagebox(0, msg.c_str(), L"", 0x30 /*MB_ICONEXCLAMATION*/ );
		m_cant_find_skin = p_skin_name;
	}

	return default_si;
	//#endif
}

#if 0
SkinFontInfo* SkinInfo::getFont Description(int p_style_id)
{
	// TODO: Convert style id into font category
	switch( p_style_id )
	{
	case SIF_HEADING_1:
		return getFont Description( L"heading 1");

	case SIF_HEADING_2:
		return getFont Description( L"heading 2");

	case SIF_HEADING_3:
		return getFont Description( L"heading 3");

	case SIF_USER_1:
		return getFont Description( L"user 1");

	case SIF_USER_2:
		return getFont Description( L"user 2");

	case SIF_USER_3:
		return getFont Description( L"user 3");

	case SIF_NORMAL:
	default:
		return getFont Description( L"normal");
	};
}
#endif

SkinFontInfo* SkinInfo::getFontDescription(std::wstring font_category)
{
	transform(font_category.begin(), font_category.end(), font_category.begin(), towlower);

	for(auto& f: fonts)
	{
		if( f->category == font_category )
		{
			return f;
		}
		else
		{
			assert( Lowercase(font_category) != Lowercase(f->category) && "you must use lowercase font style names!" ); // DOH,
		}
	}

	//_RPT1(_CRT_WARN, "!!CAN'T FIND FONT CATEGORY %s, using Arial-12, CHECK SPELLING\n", font_category );
	// WARNING Never use "System" font, it ignores height, depends on user's DPI setting.
	fontSpecification spec;
	static SkinFontInfo temp_fd(spec);
	temp_fd.flags = TTL_TRANSPARENT_BACKGROUND;
	return &temp_fd;
}

//std::string SkinInfo::GetPixelHeights()
//{
//	std::ostringstream oss;
//
//	for (auto& font : fonts)
//	{
//		auto ss = WStringToUtf8(font->category);
//		oss << ss << "|" << font->pixelHeight() << " ";
//	}
//
//	return oss.str();
//}

wstring SkinInfo::GetActualFontName( const std::wstring& p_font_category )
{
	return getFontDescription(p_font_category)->GetActualFontName();
}

SkinInfo::~SkinInfo()
{
	for (auto& font : fonts)
		delete font;
}

std::wstring SkinFontInfo::GetActualFontName()
{
	return systemfacename;
}

// used by popup skin menu
SkinInfo* SkinMgr::getSkin(int p_idx)
{
	int idx = 0;
	for (auto& si : skin_list)
	{
		if( idx++ == p_idx )
			return si;
	}

	return getSkin(L"default");
}

GmpiDrawing::SizeL SkinFontInfo::AverageCharSize()
{
	return average_char_size;
}

//int SkinFontInfo::pixelHeight()
//{
//	return average_char_size.height; // aka pixelHeight_;
//}

SkinFontInfo::SkinFontInfo(const fontSpecification& spec) :
	category(spec.category)
	,facename(spec.facename)
    ,systemfacename(spec.facename)
	,size(spec.size)
	,color(spec.color)
	,color_background(spec.color_background)
	,flags(TTL_TRANSPARENT_BACKGROUND | spec.flags)
	,vst3_vertical_offset(spec.vst3_vertical_offset)
	,verticalSnapBackwardCompatibilityMode(spec.verticalSnapBackwardCompatibilityMode)
{
// seems a mistake, stops centered font categories like Heading1 working	flags = flags & (TTL_UNDERLINE | TTL_BOLD | TTL_LIGHT | TTL_ITALIC);  // only flags relevant to typeface cache.
	std::wstring actual_facename = facename;
#ifdef _WIN32
	LOGFONT lf;                        // Used to create the CFont.
	memset(&lf, 0, sizeof(LOGFONT));   // Clear out structure.
	lf.lfHeight = -size;
	// adjust for differing screen DPI
	//	int test = GetDeviceCaps(::GetDC(nullptr), LOGPIXELSY);
	//	lf.lfHeight = -MulDiv(height, GetDeviceCaps(::GetDC(nullptr), LOGPIXELSY),96 );
	lf.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;

	if (facename == L"serif")
	{
		actual_facename = L"Times New Roman";
		lf.lfPitchAndFamily = DEFAULT_PITCH | FF_ROMAN;
	}

	if (facename == L"sans-serif")
	{
		//		actual_facename = L"Helvetica";
		actual_facename = L"Arial"; // available on all version of windows
		lf.lfPitchAndFamily = DEFAULT_PITCH | FF_SWISS;
	}

	if (facename == L"cursive")
	{
		actual_facename = L"Zapf-Chancery";
		lf.lfPitchAndFamily = DEFAULT_PITCH | FF_SCRIPT;
	}

	if (facename == L"fantasy")
	{
		actual_facename = L"Western";
		lf.lfPitchAndFamily = DEFAULT_PITCH | FF_DECORATIVE;
	}

	if (facename == L"monospace")
	{
		actual_facename = L"Courier New";
		lf.lfPitchAndFamily = DEFAULT_PITCH | FF_MODERN;
	}

	// points..   lf.lfHeight = size * dc->GetDeviceCaps(LOGPIXELSY) / 72;
	//	_tcscpy(lf.lfFaceName, actual_facename);    // face name
	wcscpy_s(lf.lfFaceName, 32, actual_facename.c_str());

	if ((/*p_desc->*/flags & TTL_UNDERLINE) != 0)
	{
		lf.lfUnderline = 1;
	}

	if ((/*p_desc->*/flags & TTL_BOLD) != 0)
	{
		lf.lfWeight = FW_BOLD;
	}

	if ((/*p_desc->*/flags & TTL_LIGHT) != 0)
	{
		lf.lfWeight = FW_LIGHT;
	}

	if ((/*p_desc->*/flags & TTL_ITALIC) != 0)
	{
		lf.lfItalic = 1;
	}

	const auto hFont = ::CreateFontIndirect(&lf);
	
	assert(!actual_facename.empty()); // earlier windows version don't handle blank
	//	_RPT1(_CRT_WARN, "actual_facename '%s'\n", actual_facename );
	// calculate average character size

	auto hDc = ::CreateCompatibleDC(nullptr);
	auto oldFont = ::SelectObject(hDc, hFont);
	
	//auto text_size1 = ::GetTextExtent(hDc, L"8");
	//auto text_size2 = ::GetTextExtent(hDc, L"M");
	SIZE s1;
	SIZE s2;
#if 0
	::GetTextExtentPointW(
		hDc,
		L"8",
		1,
		&s1
	);
	::GetTextExtentPointW(
		hDc,
		L"M",
		1,
		&s2
	);
#endif

	GetTextExtentPoint32(hDc, L"8", 1, &s1);
	GetTextExtentPoint32(hDc, L"M", 1, &s2);

	wchar_t faceNamel[100];
	GetTextFace(hDc, 100, faceNamel);

	::SelectObject(hDc, oldFont);
	::DeleteObject(hFont);
	::DeleteDC(hDc);
	
//	pixelHeight_ = s2.cy;

	// average out
	average_char_size.width = (s1.cx + s2.cx) / 2;
	average_char_size.height = (s1.cy + s2.cy) / 2;
	systemfacename = faceNamel;
#endif
}

std::string SkinMgr::getEffectiveFontInfo(const wchar_t* skinName)
{
	// Create a list of all avail fonts combined from project skin folder, specific skin, and "default" skin.
	std::map< std::wstring, FontMetadata > allFonts;

	// Check project-specific skin folder first (e.g., mydocument.skin/global.txt)
	const auto psf = GmpiResourceManager::Instance()->projectSkinFolder();
	if (!psf.empty())
	{
		auto projectGlobalTxt = psf / "global.txt";
		if (std::filesystem::exists(projectGlobalTxt))
		{
			SkinInfo projectSkin;
			projectSkin.Load(projectGlobalTxt);
			for (auto& font : projectSkin.fonts)
			{
				if (allFonts.find(font->category) == allFonts.end())
				{
					allFonts.insert(std::pair<std::wstring, FontMetadata>(font->category,
						FontMetadata(WStringToUtf8(font->category),
							WStringToUtf8(font->GetActualFontName()),
							font->size,
							font->color,
							font->color_background,
							font->flags,
							font->vst3_vertical_offset,
							font->verticalSnapBackwardCompatibilityMode)
						)
					);
				}
			}
		}
	}

	auto skin = getSkin(skinName);

	for (int i = 0; i < 2; ++i)
	{
		for (auto& font : skin->fonts)
		{
			if (allFonts.find(font->category) == allFonts.end())
			{
				//auto pOldFont = dcMem.SelectObject(skin->GetFont(font->category));
				//auto pixel_size = dcMem.GetTextExtent(L"M");

				/* ALPHA is now stored in the color value. no need to add it.
				uint32_t bg = 0;
				uint32_t t;
				if ((font->flags & TTL_TRANSPARENT_BACKGROUND) == 0 && font->color_background != -1)
				{
					t = font->color_background; // add alpha.
					bg = ((t & 0xff) << 16) | (t & 0xff00) | ((t & 0xff0000) >> 16) | 0xff000000;
				}

				t = font->color; // add alpha.
				uint32_t fg = ((t & 0xff) << 16) | (t & 0xff00) | ((t & 0xff0000) >> 16) | 0xff000000;
				*/

				allFonts.insert(std::pair<std::wstring, FontMetadata>(font->category,
					FontMetadata(WStringToUtf8(font->category),
						WStringToUtf8(font->GetActualFontName()),
						font->size,
						font->color, //fg,
						font->color_background, //bg,
						font->flags,
						font->vst3_vertical_offset,
						font->verticalSnapBackwardCompatibilityMode)
					)
				);
			}
		}
		// Next: remaining fonts from default skin
		skin = getSkin(L"default");
	}

	// Now serialise all font data.
	std::ostringstream oss;
	for (auto& it : allFonts)
	{
		auto& font = it.second;

		/* already in ABGR format
		// convert a-r-g-b to SE g-b-r (ignoring alpha)
		int32_t c = font.color_;
		int32_t fg = ((c & 0xff) << 16) | (c & 0xff00) | ((c & 0xff0000) >> 16) & 0xffffff;

		c = font.backgroundColor_;
		int32_t bg = ((c & 0xff) << 16) | (c & 0xff00) | ((c & 0xff0000) >> 16) & 0xffffff;
*/
		oss << "\nFONT_CATEGORY \"" << font.category_ << "\"\n"
			<< "font-size " << font.size_ << "\n";

		oss << std::hex;
/*
		if (fg != 0x00ffffff)
			oss << "font-color #" << setfill('0') << std::setw(6) << fg << "\n";

		if ((font.backgroundColor_ & 0xff000000) != 0) // transparent is default.
			oss << "background-color #" << setfill('0') << std::setw(6) << bg << "\n";
*/
		// out in hex, stripping alpha (assumed 0xff)
		if ((font.color_ & 0x00ffffff) != 0x00ffffff) // white is default.
			oss << "font-color #" << setfill('0') << std::setw(6) << (0x00ffffff & font.color_) << "\n";

		if ((font.backgroundColor_ & 0xff000000) != 0) // transparent is default.
			oss << "background-color #" << setfill('0') << std::setw(6) << (0x00ffffff & font.backgroundColor_) << "\n";

		oss << std::dec;

		if (font.vst3_vertical_offset_ != 0)
			oss << "vst3-vertical-offset " << font.vst3_vertical_offset_ << "\n";

		if (font.verticalSnapBackwardCompatibilityMode == 0)
			oss << "legacy-vertical-offset " << font.verticalSnapBackwardCompatibilityMode << "\n";

		if ((font.flags_ & TTL_BOLD))
		{
			oss << "font-weight bold\n";
		}
		else
		{
			if ((font.flags_ & TTL_LIGHT))
			{
				oss << "font-weight light\n";
			}
		}
		if ((font.flags_ & TTL_RIGHT))
		{
			oss << "text-align right\n";
		}
		else
		{
			if ((font.flags_ & TTL_CENTERED))
			{
				oss << "text-align center\n";
			}
		}
		if ((font.flags_ & TTL_UNDERLINE))
		{
			oss << "text-decoration underline\n";
		}
		if ((font.flags_ & TTL_ITALIC))
		{
			oss << "font-style italic\n";
		}

		oss << "font-family ";

		for (auto ff : font.faceFamilies_)
			oss << '"' << ff << "\" ";

		oss << "\n";
	}

	return oss.str();
}

void SkinMgr::UnloadSkins()
{
	for (auto& f : skin_list)
		delete f;

	skin_list.clear();
}

std::wstring SkinMgr::SkinFolder()
{
	return m_skin_folder;
}
