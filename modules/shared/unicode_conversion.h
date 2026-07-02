#pragma once
/*
#include "../shared/unicode_conversion.h"

using namespace JmUnicodeConversions;
*/

#include <string>
#include <assert.h>
#include <stdlib.h>	 // wcstombs() on Linux.
#if defined(_WIN32)
#undef  WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#undef  NOMINMAX
#define NOMINMAX
#include "windows.h"
#endif

#include "unicode_conversion2.h"  // FastUnicode - deterministic scalar fallback (non-Windows)

/*
Windows uses the OS UTF-8 codec (MultiByteToWideChar/WideCharToMultiByte). Every
other platform uses the hand-rolled FastUnicode scalar converters. simdutf was
removed: its runtime CPU dispatch produced intermittent empty conversions on some
CI kernels (icelake), and none of these paths need SIMD.
*/

namespace JmUnicodeConversions
{

inline std::string WStringToUtf8(const std::wstring& p_cstring )
{
    std::string res;

#if defined(_WIN32)
    const size_t size = WideCharToMultiByte(
		CP_UTF8,
		0,
		p_cstring.data(),
		static_cast<int>(p_cstring.size()),
		0,
		0,
		NULL,
		NULL
	);
    
	res.resize(size);

	WideCharToMultiByte(
		CP_UTF8,
		0,
		p_cstring.data(),
		static_cast<int>(p_cstring.size()),
		const_cast<LPSTR>(res.data()),
		static_cast<int>(size),
		NULL,
		NULL
	);
#else
	// wchar_t is UTF-32 here; FastUnicode::WStringToUtf8 handles both UTF-16
	// (surrogate pairs) and UTF-32 (direct codepoints) code units.
	res = FastUnicode::WStringToUtf8(p_cstring);
#endif
	return res;
}

inline std::wstring Utf8ToWstring(const char* pstr, size_t psize)
{
	std::wstring res;

#if defined(_WIN32)
	const size_t size = MultiByteToWideChar(
		CP_UTF8,
		0,
		pstr,
		static_cast<int>(psize),
		0,
		0
	);

	res.resize(size);

	MultiByteToWideChar(
		CP_UTF8,
		0,
		pstr,
		static_cast<int>(psize),
		const_cast<LPWSTR>(res.data()),
		static_cast<int>(size)
	);
#else
	// wchar_t is UTF-32 here; decode to codepoints (no surrogate pairs) and copy.
	const std::u32string u32 = FastUnicode::Utf8ToUtf32(std::string_view(pstr, psize));
	res.assign(u32.begin(), u32.end());
#endif
	return res;
}

inline std::wstring Utf8ToWstring(const std::string& p_string)
{
	return Utf8ToWstring(p_string.data(), p_string.size());
}

inline std::wstring Utf8ToWstring(const char* p_string)
{
	return Utf8ToWstring(p_string, strlen(p_string));
}

#ifdef _WIN32
    
    inline std::wstring ToUtf16( const std::wstring& s )
    {
        assert( sizeof(wchar_t) == 2 );
        return s;
    }

    inline std::wstring ToUtf16(const std::string& s)
    {
        return ToUtf16(Utf8ToWstring(s));
    }

#else
/*
#ifdef __INTEL__
    typedef char16_t TChar;
#else
    typedef unsigned short TChar;
#endif
 */
#if __cplusplus <= 199711L // condition for new mac
    typedef unsigned short TChar;
#else
    typedef char16_t TChar;
#endif
    
    typedef std::basic_string<TChar, std::char_traits<TChar>, std::allocator<TChar> > utf16_string;
    
    inline utf16_string ToUtf16( const std::wstring& s )
    {
        // On Mac. Wide-string is 32-bit.Lame conversion.
        utf16_string r;
        r.resize( s.size() );
        
        TChar* dest = (TChar*) r.data();
        for( size_t i = 0 ; i < s.size() ; ++i )
        {
            *dest++ = (TChar) s[i];
        }
        return r;
    }
    
#endif
}
