#pragma once
#include "Cocoa_Gfx.h"
#include "backends/CocoaGfx.h"

// Universal factory for macOS that dispatches queries to either the new GMPI
// drawing factory or the legacy SDK3 factory, mirroring the Windows pattern
// in DrawingFrame2_win.h.
struct UniversalFactory : public gmpi::api::IUnknown
{
    gmpi::cocoa::Factory gmpiFactory;
    se::cocoa::DrawingFactory sdk3Factory;

    UniversalFactory() : sdk3Factory(gmpiFactory.info) // SDK3 factory borrows the guts from the GMPI factory.
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
