#pragma once
/*
#include "modules/se_sdk3_hosting/LegacyFileDialogAdapter.h"
*/

#include "../se_sdk3/legacy_sdk_gui2.h"
#include "helpers/NativeUi.h"
#include "RefCountMacros.h"

// Implements the old IMpFileDialog interface by delegating to a new-API IFileDialog.
// All platform file-dialog implementations expose only IFileDialog; callers that need
// the legacy interface obtain it via this adapter.
struct LegacyFileDialogAdapter : gmpi_gui::legacy::IMpFileDialog
{
    gmpi::shared_ptr<gmpi::api::IFileDialog> inner;
    std::string selectedFilename;

    // Heap-allocated bridge with independent lifecycle from adapter.
    struct CompletionBridge : gmpi::api::IFileDialogCallback
    {
        LegacyFileDialogAdapter* adapter;
        gmpi_gui::ICompletionCallback* oldCb;
        int32_t refCount_ = 1;

        CompletionBridge(LegacyFileDialogAdapter* a, gmpi_gui::ICompletionCallback* cb)
            : adapter(a), oldCb(cb) {}

        void onComplete(gmpi::ReturnCode result, const char* selectedPath) override
        {
            adapter->selectedFilename = selectedPath ? selectedPath : "";
            oldCb->OnComplete(result == gmpi::ReturnCode::Ok ? gmpi::MP_OK : gmpi::MP_CANCEL);
            adapter->release(); // balance the addRef in ShowAsync
        }

        gmpi::ReturnCode queryInterface(const gmpi::api::Guid* iid, void** r) override
        {
            *r = {};
            if (*iid == gmpi::api::IFileDialogCallback::guid || *iid == gmpi::api::IUnknown::guid)
            {
                *r = static_cast<gmpi::api::IFileDialogCallback*>(this);
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

    explicit LegacyFileDialogAdapter(gmpi::api::IFileDialog* dialog)
    {
        inner.attach(dialog); // caller transfers ownership (refcount already incremented by caller)
    }

    int32_t MP_STDCALL AddExtension(const char* extension, const char* description = "") override
    {
        inner->addExtension(extension ? extension : "", description ? description : "");
        return gmpi::MP_OK;
    }

    int32_t MP_STDCALL SetInitialFilename(const char* text) override
    {
        inner->setInitialFilename(text ? text : "");
        return gmpi::MP_OK;
    }

    int32_t MP_STDCALL SetInitialDirectory(const char* text) override
    {
        inner->setInitialDirectory(text ? text : "");
        return gmpi::MP_OK;
    }

    int32_t MP_STDCALL ShowAsync(gmpi_gui::ICompletionCallback* cb) override
    {
        addRef(); // keep adapter alive until bridge fires onComplete

        auto* bridge = new CompletionBridge(this, cb); // refCount_ = 1
        inner->showAsync(nullptr, static_cast<gmpi::api::IFileDialogCallback*>(bridge));
        bridge->release(); // release our creation ref; platform holds its QI ref
        return gmpi::MP_OK;
    }

    int32_t MP_STDCALL GetSelectedFilename(gmpi::api::IUnknown* returnString) override
    {
        gmpi::IString* s{};
        if (gmpi::ReturnCode::Ok != returnString->queryInterface(&gmpi::legacy::IString::guid, reinterpret_cast<void**>(&s)))
            return gmpi::MP_NOSUPPORT;
        s->setData(selectedFilename.data(), (int32_t)selectedFilename.size());
        return gmpi::MP_OK;
    }

    gmpi::ReturnCode queryInterface(const gmpi::api::Guid* iid, void** r) override
    {
        *r = {};
        if (*iid == gmpi_gui::legacy::IMpFileDialog::guid || *iid == gmpi::api::IUnknown::guid)
        {
            *r = static_cast<gmpi_gui::legacy::IMpFileDialog*>(this);
            addRef();
            return gmpi::ReturnCode::Ok;
        }
        return gmpi::ReturnCode::NoSupport;
    }
    GMPI_REFCOUNT
};
