#pragma once
/*
#include "modules/se_sdk3_hosting/LegacyMenuAdapter.h"
*/

#include <string>
#include <vector>
#include "../se_sdk3/legacy_sdk_gui2.h"
#include "helpers/NativeUi.h"
#include "RefCountMacros.h"

// adapt SDK3 to GMPI-UI context menu callback
class ContextMenuAdaptor : public gmpi::IMpContextItemSink
{
    gmpi::api::IContextItemSink* sink{};
    gmpi::shared_ptr<gmpi::api::IUnknown> currentCallback;

public:

    void setCallback(std::function<void(int32_t selectedId)> pcallback)
    {
        currentCallback = {};
        if(pcallback)
            currentCallback = new gmpi::sdk::PopupMenuCallback(pcallback);
    }

    ContextMenuAdaptor(gmpi::api::IUnknown* psink)
    {
        psink->queryInterface(&gmpi::api::IContextItemSink::guid, (void**)&sink);
    }

    // IMpContextItemSink
    int32_t MP_STDCALL AddItem(const char* text, int32_t id, int32_t flags = 0) override
    {
        return (int32_t)sink->addItem(text, id, flags, currentCallback.get());
    }

    GMPI_QUERYINTERFACE1(gmpi::MP_IID_CONTEXT_ITEMS_SINK, gmpi::IMpContextItemSink);
    GMPI_REFCOUNT_NO_DELETE;
};

// Implements the old IMpPlatformMenu interface by delegating to a new-API IPopupMenu.
// All platform popup-menu implementations expose only IPopupMenu; callers that need
// the legacy interface obtain it via this adapter.
//
// Legacy AddItem/ShowAsync provide a single completion callback for the whole menu;
// the new IPopupMenu uses per-item callbacks. To bridge: buffer items in AddItem
// and, at ShowAsync time, replay them onto inner with a single bridge attached as
// each item's callback. Only the selected item's callback fires (no cancel notification).
struct LegacyMenuAdapter : gmpi_gui::legacy::IMpPlatformMenu
{
    gmpi::shared_ptr<gmpi::api::IPopupMenu> inner;
    int32_t selectedId = -1;

    struct BufferedItem
    {
        std::string text;
        int32_t id;
        int32_t flags;
    };
    std::vector<BufferedItem> bufferedItems;

    // Heap-allocated bridge attached to every item; only the chosen item fires onComplete.
    // Lifetime is bounded by the adapter (adapter holds inner, inner stores the bridge in
    // its per-item callback list), so the raw `adapter` pointer is safe.
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
            if (oldCb)
                oldCb->OnComplete(result == gmpi::ReturnCode::Ok ? gmpi::MP_OK : gmpi::MP_CANCEL);
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
        bufferedItems.push_back({ text ? text : "", id, flags });
        return gmpi::MP_OK;
    }

    int32_t MP_STDCALL SetAlignment(int32_t alignment) override
    {
        inner->setAlignment(alignment);
        return gmpi::MP_OK;
    }

    int32_t MP_STDCALL ShowAsync(gmpi_gui::ICompletionCallback* cb) override
    {
        // Bridge is heap-allocated; inner addRefs it via each addItem and releases when destroyed.
        auto* bridge = new CompletionBridge(this, cb); // refCount_ = 1

        for (const auto& item : bufferedItems)
        {
            inner->addItem(item.text.c_str(), item.id, item.flags,
                static_cast<gmpi::api::IPopupMenuCallback*>(bridge));
        }
        bridge->release(); // platform retained N refs via addItem; drop our creation ref

        inner->showAsync();
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
