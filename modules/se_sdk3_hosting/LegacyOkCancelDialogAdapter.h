#pragma once
/*
#include "modules/se_sdk3_hosting/LegacyOkCancelDialogAdapter.h"
*/

#include <functional>
#include <string>
#include "../se_sdk3/legacy_sdk_gui2.h"
#include "helpers/NativeUi.h"
#include "RefCountMacros.h"

// Implements the old IMpOkCancelDialog interface by delegating to a new-API IStockDialog.
// Title and text are accumulated via SetTitle/SetText; the IStockDialog is constructed
// (via the caller-supplied platform builder) when ShowAsync is called — IStockDialog has
// no setters, so we can't build it eagerly.
struct LegacyOkCancelDialogAdapter : gmpi_gui::legacy::IMpOkCancelDialog
{
    // Builds a fresh, addRef'd IStockDialog* given dialog type, title and text.
    // Caller owns the returned ref.
    using Builder = std::function<gmpi::api::IStockDialog*(int32_t type, const char* title, const char* text)>;

    Builder builder;
    std::string title;
    std::string text;

    struct CompletionBridge : gmpi::api::IStockDialogCallback
    {
        LegacyOkCancelDialogAdapter* adapter;
        gmpi_gui::ICompletionCallback* oldCb;
        int32_t refCount_ = 1;

        CompletionBridge(LegacyOkCancelDialogAdapter* a, gmpi_gui::ICompletionCallback* cb)
            : adapter(a), oldCb(cb) {}

        void onComplete(gmpi::api::StockDialogButton button) override
        {
            oldCb->OnComplete(button == gmpi::api::StockDialogButton::Ok ? gmpi::MP_OK : gmpi::MP_CANCEL);
            adapter->release(); // balance the addRef in ShowAsync
        }

        gmpi::ReturnCode queryInterface(const gmpi::api::Guid* iid, void** r) override
        {
            *r = {};
            if (*iid == gmpi::api::IStockDialogCallback::guid || *iid == gmpi::api::IUnknown::guid)
            {
                *r = static_cast<gmpi::api::IStockDialogCallback*>(this);
                addRef();
                return gmpi::ReturnCode::Ok;
            }
            return gmpi::ReturnCode::NoSupport;
        }
        int32_t addRef() override { return ++refCount_; }
        int32_t release() override
        {
            if (--refCount_ == 0) { delete this; return 0; }
            return refCount_;
        }
    };

    // dialogType is intentionally ignored: the legacy IMpOkCancelDialog contract is
    // always a two-button OK/Cancel dialog (enforced in ShowAsync). Kept in the
    // signature so existing call sites need no change.
    LegacyOkCancelDialogAdapter(int32_t /*dialogType*/, Builder b)
        : builder(std::move(b)) {}

    int32_t MP_STDCALL SetTitle(const char* t) override
    {
        title = t ? t : "";
        return gmpi::MP_OK;
    }

    int32_t MP_STDCALL SetText(const char* t) override
    {
        text = t ? t : "";
        return gmpi::MP_OK;
    }

    int32_t MP_STDCALL ShowAsync(gmpi_gui::ICompletionCallback* cb) override
    {
        addRef(); // keep adapter alive until bridge fires onComplete

        // Always request OK/Cancel: the original Gmpi_Win_OkCancelDialog hardcoded
        // MB_OKCANCEL and ignored dialogType. Legacy callers pass dialogType=0, which in
        // the new StockDialogType enum means Ok (a single button) — forcing OkCancel here
        // restores the historical two-button behaviour. Other button sets use createStockDialog.
        auto* inner = builder(static_cast<int32_t>(gmpi::api::StockDialogType::OkCancel), title.c_str(), text.c_str());
        auto* bridge = new CompletionBridge(this, cb); // refCount_ = 1
        inner->showAsync(static_cast<gmpi::api::IUnknown*>(bridge));
        bridge->release(); // release our creation ref; inner holds its QI ref
        inner->release();
        return gmpi::MP_OK;
    }

    gmpi::ReturnCode queryInterface(const gmpi::api::Guid* iid, void** r) override
    {
        *r = {};
        if (*iid == gmpi_gui::legacy::IMpOkCancelDialog::guid || *iid == gmpi::api::IUnknown::guid)
        {
            *r = static_cast<gmpi_gui::legacy::IMpOkCancelDialog*>(this);
            addRef();
            return gmpi::ReturnCode::Ok;
        }
        return gmpi::ReturnCode::NoSupport;
    }
    GMPI_REFCOUNT
};
