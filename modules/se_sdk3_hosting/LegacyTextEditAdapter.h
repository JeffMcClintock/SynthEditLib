#pragma once
/*
#include "modules/se_sdk3_hosting/LegacyTextEditAdapter.h"
*/

#include <string>
#include "../se_sdk3/legacy_sdk_gui2.h"
#include "helpers/NativeUi.h"
#include "RefCountMacros.h"
#include "GmpiSdkCommon.h" // gmpi::shared_ptr

// Implements the old IMpPlatformText interface by delegating to a new-API ITextEdit.
// All platform text-edit implementations expose only ITextEdit; callers that need
// the legacy interface obtain it via this adapter.
//
// The legacy contract is "after OnComplete, call GetText to read what the user typed".
// The new API instead delivers the text via per-keystroke onChanged callbacks. To
// bridge: the adapter caches the latest value seen on onChanged and serves GetText
// from that cache. The platform impl is expected to push a final onChanged before
// onComplete on the Ok path, so the cache is fresh by the time the legacy caller
// reads it.
//
// Lifetime: the legacy ICompletionCallback is not an independent object - it is a
// member of the caller (GmpiGui::TextEdit::onDialogCompleteEvent, EditWidget::
// onTextEntryCompeteEvent), so it dies with the caller's module. The platform edit,
// however, is asynchronous and can outlive that module: any document change marks
// the panel view dirty and the next timer tick rebuilds it (ViewBase::Refresh),
// destroying every module - including one whose native edit box is still open.
// The caller's only sign of life is the ref it holds on this adapter, so when that
// ref goes while an edit is open (refcount drops to just ShowAsync's self-ref) the
// bridge forgets the callback, and the eventual onComplete is swallowed instead of
// calling through a dangling pointer.
struct LegacyTextEditAdapter : gmpi_gui::legacy::IMpPlatformText
{
    gmpi::shared_ptr<gmpi::api::ITextEdit> inner;
    std::string latestText;

    // Heap-allocated bridge with independent lifecycle from adapter.
    struct CompletionBridge : gmpi::api::ITextEditCallback
    {
        LegacyTextEditAdapter* adapter;         // null once completion has been delivered
        gmpi_gui::ICompletionCallback* oldCb;   // null once the legacy caller has let go
        int32_t refCount_ = 1;

        CompletionBridge(LegacyTextEditAdapter* a, gmpi_gui::ICompletionCallback* cb)
            : adapter(a), oldCb(cb) {}

        void onChanged(const char* text) override
        {
            if (adapter)
                adapter->latestText = text ? text : "";
        }
        void onComplete(gmpi::ReturnCode result) override
        {
            // Deliver once. Platforms can call this twice - WinUI raises LosingFocus
            // again if focus is contested before LostFocus tears the box down.
            auto* a = adapter;
            if (!a)
                return;
            adapter = nullptr;
            if (a->activeBridge == this)
                a->activeBridge = nullptr;

            if (oldCb)
                oldCb->OnComplete(result == gmpi::ReturnCode::Ok ? gmpi::MP_OK : gmpi::MP_CANCEL);
            oldCb = nullptr;

            a->release(); // balance the addRef in ShowAsync
        }

        gmpi::ReturnCode queryInterface(const gmpi::api::Guid* iid, void** r) override
        {
            *r = {};
            if (*iid == gmpi::api::ITextEditCallback::guid || *iid == gmpi::api::IUnknown::guid)
            {
                *r = static_cast<gmpi::api::ITextEditCallback*>(this);
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
                if (adapter && adapter->activeBridge == this) // dropped without completing
                    adapter->activeBridge = nullptr;
                delete this;
                return 0;
            }
            return refCount_;
        }
    };

    CompletionBridge* activeBridge{}; // the open edit's bridge, if any (non-owning)

    explicit LegacyTextEditAdapter(gmpi::api::ITextEdit* textEdit)
    {
        inner.attach(textEdit); // caller transfers ownership (refcount already incremented by caller)
    }

    ~LegacyTextEditAdapter()
    {
        if (activeBridge)
            activeBridge->adapter = nullptr;
    }

    int32_t MP_STDCALL SetText(const char* text) override
    {
        latestText = text ? text : "";
        inner->setText(latestText.c_str());
        return gmpi::MP_OK;
    }

    int32_t MP_STDCALL GetText(gmpi::api::IUnknown* returnString) override
    {
        gmpi::IString* s{};
        if (gmpi::ReturnCode::Ok != returnString->queryInterface(&gmpi::legacy::IString::guid, reinterpret_cast<void**>(&s)))
            return gmpi::MP_NOSUPPORT;
        s->setData(latestText.data(), (int32_t)latestText.size());
        return gmpi::MP_OK;
    }

    int32_t MP_STDCALL ShowAsync(gmpi_gui::ICompletionCallback* cb) override
    {
        addRef(); // keep adapter alive until bridge fires onComplete

        auto* bridge = new CompletionBridge(this, cb); // refCount_ = 1
        activeBridge = bridge; // before showAsync: a modal platform completes synchronously
        inner->showAsync(static_cast<gmpi::api::ITextEditCallback*>(bridge));
        bridge->release(); // release our creation ref; platform holds its QI ref
        return gmpi::MP_OK;
    }

    int32_t MP_STDCALL SetAlignment(int32_t alignment) override
    {
        inner->setAlignment(alignment);
        return gmpi::MP_OK;
    }

    int32_t MP_STDCALL SetTextSize(float height) override
    {
        inner->setTextSize(height);
        return gmpi::MP_OK;
    }

    gmpi::ReturnCode queryInterface(const gmpi::api::Guid* iid, void** r) override
    {
        *r = {};
        if (*iid == gmpi_gui::legacy::IMpPlatformText::guid || *iid == gmpi::api::IUnknown::guid)
        {
            *r = static_cast<gmpi_gui::legacy::IMpPlatformText*>(this);
            addRef();
            return gmpi::ReturnCode::Ok;
        }
        return gmpi::ReturnCode::NoSupport;
    }

    int refCount2_ = 1;
    int32_t addRef() override { return ++refCount2_; }
    int32_t release() override
    {
        if (--refCount2_ == 0) { delete this; return 0; }
        // Only ShowAsync's self-ref left while an edit is open: the legacy caller has
        // let go (typically its module was destroyed with the view). Its callback lives
        // inside it, so it must never be called again; the platform edit runs to
        // completion on its own and the result is discarded.
        if (refCount2_ == 1 && activeBridge)
            activeBridge->oldCb = nullptr;
        return refCount2_;
    }
};
