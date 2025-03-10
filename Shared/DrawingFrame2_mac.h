#pragma once
#include "Cocoa_Gfx.h"

class UniversalFactory : public DrawingFactory
{
    se::directx::Factory_base sdk3Factory;

public:
    UniversalFactory() : gmpi::directx::Factory(nullptr), sdk3Factory(info, (gmpi::IMpUnknown*) static_cast<gmpi::api::IUnknown*>(this))
    {
    }

    gmpi::ReturnCode queryInterface(const gmpi::api::Guid* iid, void** returnInterface) override
    {
        *returnInterface = {};
        if (*iid == gmpi::drawing::api::IFactory::guid)
        {
            return gmpi::directx::Factory_base::queryInterface(iid, returnInterface);
        }
        if (
            *iid == *reinterpret_cast<const gmpi::api::Guid*>(&GmpiDrawing_API::SE_IID_FACTORY2_MPGUI) ||
            *iid == *reinterpret_cast<const gmpi::api::Guid*>(&GmpiDrawing_API::SE_IID_FACTORY_MPGUI)
            )
        {
            return (gmpi::ReturnCode)sdk3Factory.queryInterface(*reinterpret_cast<const gmpi::MpGuid*>(iid), returnInterface);
        }
        if (*iid == gmpi::api::IUnknown::guid)
        {
            *returnInterface = this;
            addRef();
            return gmpi::ReturnCode::Ok;
        }
        return gmpi::ReturnCode::NoSupport;
    }
    GMPI_REFCOUNT_NO_DELETE;
};