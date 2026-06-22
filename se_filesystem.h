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
