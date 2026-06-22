#pragma once

// std::filesystem is only available in Apple's libc++ from macOS 10.15 onward
// (the <filesystem> headers are annotated "introduced in macOS 10.15"). When the
// deployment target is older, SELIB_HAS_FILESYSTEM is left undefined by CMake and
// we substitute ghc::filesystem - a header-only, drop-in implementation with an
// identical API that compiles its own operations against POSIX, so it carries no
// dependency on the 10.15+ libc++ runtime.
//
// All SynthEditLib code refers to the filesystem library through the `se_fs`
// namespace alias instead of `std::filesystem` directly, so the same source works
// on both modern and older systems.

#ifdef SELIB_HAS_FILESYSTEM
    #include <filesystem>
    namespace se_fs = std::filesystem;
#else
    #include <ghc/filesystem.hpp>
    namespace se_fs = ghc::filesystem;
#endif
