#pragma once
#include "backends/CpuGfx.h"
#include "helpers/CpuTextEngine.h"
#include "helpers/DecodeImage.h"
#include "helpers/FontProvider.h"
#include "../modules/se_sdk3/Drawing.h" // GmpiDrawing_API, needed by GmpiUiToSDK3.h
#include "GmpiUiToSDK3.h"

// Universal factory for the pure-software CPU backend (e.g. headless Linux)
// that dispatches queries to either the new GMPI drawing factory or the legacy
// SDK3 adapter, mirroring DrawingFrame2_win.h / DrawingFrame2_mac.h.
//
// The point of this one is what it does NOT need: no GPU, no display server,
// no JUCE, no X11 — which is what makes SynthEditCL runnable on a headless box
// or in CI. The three platform-shaped pieces the backend deliberately leaves
// out are wired in here: font discovery (fontconfig), image decoding (libpng),
// and shaping (HarfBuzz, inside CpuTextEngine).
struct UniversalFactory : public gmpi::api::IUnknown
{
    gmpi::cpugfx::Factory        gmpiFactory;
    gmpi::drawing::CpuTextEngine textEngine{ gmpi::drawing::findFont };
    se::GmpiToSDK3Factory        sdk3Factory;

    UniversalFactory() : sdk3Factory(&gmpiFactory) // SDK3 factory borrows the guts from the GMPI factory.
    {
        // Colour-emoji glyphs arrive as PNG blobs inside the font, so the text
        // engine needs a decoder of its own, separate from the factory's.
        textEngine.imageDecoder = gmpi::drawing::decodeImageMemory;
        gmpiFactory.imageDecoder = gmpi::drawing::decodeImageFile;
        gmpiFactory.textEngine = &textEngine;
    }

    // dispatch queries to correct factory
    gmpi::ReturnCode queryInterface(const gmpi::api::Guid* iid, void** returnInterface) override
    {
        if (
            *iid == *reinterpret_cast<const gmpi::api::Guid*>(&GmpiDrawing_API::SE_IID_FACTORY2_MPGUI) ||
            *iid == *reinterpret_cast<const gmpi::api::Guid*>(&GmpiDrawing_API::SE_IID_FACTORY_MPGUI)
            )
        {
            return (gmpi::ReturnCode)sdk3Factory.queryInterface(*reinterpret_cast<const gmpi::MpGuid*>(iid), returnInterface);
        }

        return gmpiFactory.queryInterface(iid, returnInterface);
    }

    GMPI_REFCOUNT_NO_DELETE;
};
