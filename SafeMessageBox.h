#pragma once

#ifndef _WIN32
inline static int32_t MB_OK = 0;
inline static int32_t MB_ICONSTOP = 0x10;
#endif

// A safe way to present a messagebox, that will divert to console output on CI (without blocking)
void SafeMessagebox(
    void* hWnd,
    const wchar_t* lpText,
    const wchar_t* lpCaption = L"",
    int uType = 0
);
