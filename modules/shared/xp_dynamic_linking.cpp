#include "xp_dynamic_linking.h"

// Provide a cross-platform loading of dlls.

#if defined(_WIN32)
#undef  WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#undef  NOMINMAX
#define NOMINMAX
#include "windows.h"
#else
#include <dlfcn.h>
#include <filesystem>
#include "unicode_conversion.h"
using namespace JmUnicodeConversions;
#endif

namespace gmpi_dynamic_linking
{

#if defined(_WIN32)
	typedef HINSTANCE MP_DllHandle;
#else
	typedef void* MP_DllHandle;
#endif

#ifndef SE_NO_EXTERNAL_MODULES   // BACKLOG S1b -- these three are the dlopen/dlsym/dlclose
	// importers, and the Accept is that a Release TIDE binary imports none of them.
	// dladdr (below, in the bundle-path helper) is NOT part of this and stays.
	int32_t MP_DllLoad(DLL_HANDLE* dll_handle, const wchar_t* dll_filename)
	{
#if defined( _WIN32)
		*dll_handle = (DLL_HANDLE) LoadLibraryW(dll_filename);
#else
		*dll_handle = (DLL_HANDLE) dlopen(WStringToUtf8(dll_filename).c_str(), RTLD_LAZY); // glibc rejects mode 0 ("invalid mode"); macOS merely tolerated it.
#endif
		return *dll_handle == 0;
	}

	int32_t MP_DllUnload(DLL_HANDLE dll_handle)
	{
		int32_t r = 0;
		if (dll_handle)
		{
#if defined( _WIN32)
			r = FreeLibrary((HMODULE)dll_handle);
#else
			r = dlclose((MP_DllHandle)dll_handle);
#endif
		}
		return r == 0;
	}

	int32_t MP_DllSymbol(DLL_HANDLE dll_handle, const char* symbol_name, void** returnFunction)
	{
#if defined( _WIN32)
		*returnFunction = (void*) GetProcAddress((HMODULE)dll_handle, symbol_name);
#else
		*returnFunction = dlsym((MP_DllHandle) dll_handle, symbol_name);
#endif
		return *returnFunction == 0;
	}
#endif // SE_NO_EXTERNAL_MODULES

    // Provide a static function to allow GetModuleHandleExA() to find dll name.
    void localFuncWithUNlikelyName3456()
    {
    }

#if defined(_WIN32)
	int32_t MP_GetDllHandle(DLL_HANDLE* returnDllHandle)
	{
		HMODULE hmodule = 0;
		GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)&localFuncWithUNlikelyName3456, &hmodule);
		*returnDllHandle = ( DLL_HANDLE) hmodule;
		return 1;
	}

	std::wstring MP_GetDllFilename()
	{
		DLL_HANDLE hmodule = 0;
		MP_GetDllHandle(&hmodule);

		wchar_t full_path[MAX_PATH] = L"";
		GetModuleFileNameW((HMODULE)hmodule, full_path, std::size(full_path));
		return std::wstring(full_path);
	}
    
#else
    // Mac
    std::wstring MP_GetDllFilename()
    {
        Dl_info info;
        int rv = dladdr((void *)&localFuncWithUNlikelyName3456, &info);
        assert(rv != 0);

#if defined(__APPLE__)
        return Utf8ToWstring(info.dli_fname);
#else
        // On Linux dladdr reports the main executable exactly as it was invoked, so
        // launching './SynthEdit' would make everything derived from it (PlugIns,
        // Resources) relative to the current working directory. Resolve it once, at
        // first use, before anything has a chance to change directory.
        static const std::wstring resolved =
            Utf8ToWstring(std::filesystem::absolute(info.dli_fname).lexically_normal().string());

        return resolved;
#endif
    }
#endif
}
