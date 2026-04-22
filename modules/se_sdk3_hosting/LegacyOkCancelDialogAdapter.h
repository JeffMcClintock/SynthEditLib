#pragma once
/*
#include "modules/se_sdk3_hosting/LegacyOkCancelDialogAdapter.h"
*/

#include "../se_sdk3/legacy_sdk_gui2.h"
#include "helpers/NativeUi.h"
#include "RefCountMacros.h"
#include "gmpi_gui_hosting.h"

// Implements the old IMpOkCancelDialog interface by delegating to a new-API IStockDialog.
// Title and text are accumulated via SetTitle/SetText; the IStockDialog is constructed
// with those values when ShowAsync is called (IStockDialog has no setters).
struct LegacyOkCancelDialogAdapter : gmpi_gui::legacy::IMpOkCancelDialog
{
    HWND parentWnd;
    int32_t dialogType;
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

    LegacyOkCancelDialogAdapter(int32_t type, HWND hwnd) : dialogType(type), parentWnd(hwnd) {}

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

        auto* inner = new GmpiGuiHosting::Gmpi_Win_OkCancelDialog(parentWnd, dialogType, title.c_str(), text.c_str());
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
