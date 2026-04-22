#pragma once
/*
#include "modules/se_sdk3_hosting/LegacyMenuAdapter.h"
*/

#include "../se_sdk3/legacy_sdk_gui2.h"
#include "helpers/NativeUi.h"
#include "RefCountMacros.h"

// Implements the old IMpPlatformMenu interface by delegating to a new-API IPopupMenu.
// All platform popup-menu implementations expose only IPopupMenu; callers that need
// the legacy interface obtain it via this adapter.
struct LegacyMenuAdapter : gmpi_gui::legacy::IMpPlatformMenu
{
    gmpi::shared_ptr<gmpi::api::IPopupMenu> inner;
    int32_t selectedId = -1;

    // Heap-allocated bridge: independent lifecycle from adapter, safe for async use.
    // Platform stores it as callback2; when the menu closes, onComplete fires and
    // releases the adapter's ShowAsync ref.
    struct CompletionBridge : gmpi::api::IPopupMenuCallback
    {
        LegacyMenuAdapter* adapter;
        gmpi_gui::ICompletionCallback* oldCb;
        int32_t refCount_ = 1;

        CompletionBridge(LegacyMenuAdapter* a, gmpi_gui::ICompletionCallback* cb)
            : adapter(a), oldCb(cb) {}

        void onComplete(gmpi::ReturnCode result, int32_t id) override
        {
            adapter->selectedId = id;
            oldCb->OnComplete(result == gmpi::ReturnCode::Ok ? gmpi::MP_OK : gmpi::MP_CANCEL);
            adapter->release(); // balance the addRef in ShowAsync
        }

        gmpi::ReturnCode queryInterface(const gmpi::api::Guid* iid, void** r) override
        {
            *r = {};
            if (*iid == gmpi::api::IPopupMenuCallback::guid || *iid == gmpi::api::IUnknown::guid)
            {
                *r = static_cast<gmpi::api::IPopupMenuCallback*>(this);
                addRef();
                return gmpi::ReturnCode::Ok;
            }
            return gmpi::ReturnCode::NoSupport;
        }
        int32_t addRef() override { return ++refCount_; }
        int32_t release() override
        {
            if (--refCount_ == 0)
            {
                delete this; return 0;
            }
            return refCount_;
        }
    };

    explicit LegacyMenuAdapter(gmpi::api::IPopupMenu* menu)
    {
        inner.attach(menu); // caller transfers ownership (refcount already incremented by caller)
    }

    int32_t MP_STDCALL AddItem(const char* text, int32_t id, int32_t flags) override
    {
        inner->addItem(text ? text : "", id, flags, nullptr);
        return gmpi::MP_OK;
    }

    int32_t MP_STDCALL SetAlignment(int32_t alignment) override
    {
        inner->setAlignment(alignment);
        return gmpi::MP_OK;
    }

    int32_t MP_STDCALL ShowAsync(gmpi_gui::ICompletionCallback* cb) override
    {
        addRef(); // keep adapter alive until bridge fires onComplete

        // Bridge is heap-allocated so its lifecycle is independent from the adapter.
        auto* bridge = new CompletionBridge(this, cb); // refCount_ = 1
        inner->showAsync(static_cast<gmpi::api::IPopupMenuCallback*>(bridge));
        bridge->release(); // release our creation ref; platform holds its QI ref
        return gmpi::MP_OK;
    }

    int32_t MP_STDCALL GetSelectedId() override { return selectedId; }

    gmpi::ReturnCode queryInterface(const gmpi::api::Guid* iid, void** r) override
    {
        *r = {};
        if (*iid == gmpi_gui::legacy::IMpPlatformMenu::guid || *iid == gmpi::api::IUnknown::guid)
        {
            *r = static_cast<gmpi_gui::legacy::IMpPlatformMenu*>(this);
            addRef();
            return gmpi::ReturnCode::Ok;
        }
        return gmpi::ReturnCode::NoSupport;
    }
    GMPI_REFCOUNT
};
