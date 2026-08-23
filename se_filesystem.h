#pragma once

// std::filesystem is available on every platform SynthEditLib targets EXCEPT
// Apple's libc++ before macOS 10.15 (the <filesystem> symbols are annotated
// "introduced in macOS 10.15"). So assume it is present by default - correct on
// Windows, Linux and macOS >= 10.15 - and only fall back to the header-only
// ghc::filesystem when CMake explicitly signals it is missing by defining
// SELIB_HAS_FILESYSTEM=0 (done for older macOS deployment targets). ghc is a
// drop-in implementation with an identical API that builds its operations on
// POSIX directly, so it carries no dependency on the 10.15+ libc++ runtime.
//
// Defaulting to std::filesystem (rather than requiring every target to define the
// macro) means a target that includes this header without inheriting SynthEditLib's
// compile settings - e.g. the standalone plugin modules and EditorLib - still
// compiles on the common platforms; only the exceptional old-macOS build relies on
// the CMake-provided define.
//
// All SynthEditLib code refers to the filesystem library through the `se_fs`
// namespace alias instead of `std::filesystem` directly, so the same source works
// on both modern and older systems.

#if defined(SELIB_HAS_FILESYSTEM) && !SELIB_HAS_FILESYSTEM
    #include <ghc/filesystem.hpp>
    namespace se_fs = ghc::filesystem;
#else
    #include <filesystem>
    namespace se_fs = std::filesystem;
#endif

#include <string>

// A project is authored on one platform and exported to another, so the file references
// stored inside it arrive in whichever form the author's machine used. se_fs::path::
// is_absolute() only recognises the *host's* convention: on macOS and Linux it answers
// false for "C:\skins\duck.png", and on Windows it answers false for "/Users/me/duck.png".
//
// Any code that classifies a path which came out of a document - rather than one it built
// from the local filesystem - must use this instead. Mistaking a foreign absolute path for
// a relative one silently appends it to a search folder, and the resource is never found.
inline bool isAbsolutePathAnyPlatform(const std::wstring& path)
{
    if (path.empty())
        return false;

    const auto isDriveLetter = [](wchar_t c)
        { return (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z'); };

    // Windows, drive-qualified. "C:\skins\duck.png", "c:/skins/duck.png".
    if (path.size() >= 2 && path[1] == L':' && isDriveLetter(path[0]))
        return true;

    // Windows, UNC. "\\server\share\duck.png", and the "\\?\" long-path prefix.
    if (path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\')
        return true;

    // POSIX. "/Users/me/duck.png".
    if (path[0] == L'/')
        return true;

    // Whatever else the host itself calls absolute.
    return se_fs::path(path).is_absolute();
}
