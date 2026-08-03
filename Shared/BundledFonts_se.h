#pragma once

// The font SynthEdit ships with itself.
//
// Asking the OS for a family by name resolves to a different face on each
// platform - "Verdana" is Verdana on Windows and, through fontconfig, Noto Sans
// on a typical Linux box. The advance widths differ, so text extents differ, and
// anything sized from text follows: a module box auto-fitted to its pin labels
// comes out 12px narrower on Linux, which moves its pins and bends every cable
// attached to them. Shipping the face removes the substitution, so layout is
// identical on every platform and the Direct2D golden images mean something off
// Windows.
//
// Selawik is Microsoft's metric-compatible open replacement for Segoe UI, under
// the SIL OFL - see Resources/fonts/OFL.txt and README.md. It covers Latin only;
// everything else (Cyrillic, Greek, CJK) is left to the text engine's
// per-codepoint fallback, which reaches the system font database.
//
// Every UniversalFactory calls this, so all four backends - DirectX, Cocoa, JUCE
// and CPU - resolve the same face.

#include <filesystem>

#include "helpers/BundledFonts.h"
#include "../modules/se_sdk3_hosting/BundleInfo.h"

namespace se
{

// Idempotent: the registry is global, and several factories may be constructed.
inline void registerBundledFonts()
{
    static bool done = false;
    if (done)
        return;
    done = true;

    const std::filesystem::path fonts =
        std::filesystem::path(BundleInfo::instance()->getBundleContentsFolder()) / "Resources" / "fonts";

    using gmpi::drawing::FontWeight;
    using gmpi::drawing::registerBundledFont;

    registerBundledFont("Selawik", fonts / "selawk.ttf");
    registerBundledFont("Selawik", fonts / "selawkb.ttf",  FontWeight::Bold);
    registerBundledFont("Selawik", fonts / "selawksb.ttf", FontWeight::SemiBold);
}

} // namespace se
