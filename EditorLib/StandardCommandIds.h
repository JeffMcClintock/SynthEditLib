// SPDX-License-Identifier: ISC
// Copyright 2007-2026 Jeff McClintock.
#pragma once

// The handful of standard command IDs this editor takes from MFC, without
// taking MFC.
//
// WHY THIS FILE EXISTS. `CContainer.cpp` and `MfcDocPresenter.cpp` used to
// `#include "afxres.h"` under `#ifdef _WIN32`. That header ships only with
// Visual Studio's **MFC component**, so a contributor whose Visual Studio does
// not have it -- Build Tools does not, by default -- could not compile this
// library at all, and the failure named these two files rather than the missing
// component:
//
//     CContainer.cpp(8,10): error C1083: Cannot open include file: 'afxres.h'
//
// Both files are in the PUBLIC repo, so that was a hard MFC requirement on
// anyone cloning it. BACKLOG P3.
//
// WHAT WAS ACTUALLY NEEDED, measured rather than assumed: of the twelve
// `ID*` symbols those two files reference, `afxres.h` supplies exactly the four
// below. `IDOK` and `IDYES` come from `<winuser.h>`, and the remaining six --
// ID_EDIT_CONTAIN, ID_EDIT_UNCONTAIN, ID_EDIT_MOVEBACK, ID_EDIT_MOVEFRONT,
// ID_EDIT_DELETE and ID_INS_PREFAB -- are this application's own and already
// live in `resource.h`.
//
// THE VALUES ARE MFC'S, COPIED EXACTLY, AND THEY MUST STAY THAT WAY. They are
// not free to choose: `.rc` files elsewhere in the tree are compiled by the
// resource compiler WITH `afxres.h`, and Windows itself routes some of these as
// standard commands. A value that disagreed with MFC's would not fail to build
// -- it would silently wire a menu item to nothing, which is the worst
// available outcome. Cross-check against
// `VC\Tools\MSVC\<ver>\atlmfc\include\afxres.h` before touching any line here.
//
// TO RE-CHECK THE VALUES, on a machine that has MFC -- one file, one command,
// and it fails loudly on a single wrong digit (verified both ways):
//
//     #include "StandardCommandIds.h"
//     #include <afxres.h>
//     static_assert(ID_EDIT_COPY == 0xE122, "ID_EDIT_COPY diverged from MFC");
//     ...
//     cl /std:c++20 /W4 /c /I<EditorLib> idcheck.cpp
//
// It is deliberately NOT a build target: it needs the very component this file
// exists to stop requiring, so it would fail on exactly the toolchain P3 was
// filed to support.
//
// THE GUARDS ARE LOAD-BEARING, not defensive habit. SynthEdit's own MFC
// application still includes `afxres.h`, and a translation unit that sees both
// it and this header must end up with one definition rather than a redefinition
// warning -- so where MFC is present, MFC wins and this file adds nothing.

#ifndef ID_EDIT_COPY
#define ID_EDIT_COPY                    0xE122
#endif

#ifndef ID_EDIT_CUT
#define ID_EDIT_CUT                     0xE123
#endif

#ifndef ID_EDIT_PASTE
#define ID_EDIT_PASTE                   0xE125
#endif

#ifndef ID_EDIT_SELECT_ALL
#define ID_EDIT_SELECT_ALL              0xE12A
#endif
