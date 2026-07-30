#pragma once
#include "backends/JuceGfx.h"
#include "../modules/se_sdk3/Drawing.h" // GmpiDrawing_API, needed by GmpiUiToSDK3.h
#include "GmpiUiToSDK3.h"

// Universal factory for the JUCE graphics backend (e.g. Linux) that dispatches
// queries to either the new GMPI drawing factory or the legacy SDK3 adapter,
// mirroring DrawingFrame2_win.h / DrawingFrame2_mac.h.
struct UniversalFactory : public gmpi::api::IUnknown
{
    gmpi::jucegfx::Factory gmpiFactory;
    se::GmpiToSDK3Factory sdk3Factory;

    UniversalFactory() : sdk3Factory(&gmpiFactory) // SDK3 factory borrows the guts from the GMPI factory.
    {
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
