#pragma once

#if defined(_WIN32) // avoid on Apple AND Linux.

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <SDKDDKVer.h>

#define WIN32_LEAN_AND_MEAN		// Exclude rarely-used stuff from Windows headers
#include <windows.h>

// C4355: 'this' : used in base member initializer list
#pragma warning( disable : 4355 )

#endif // WIN32