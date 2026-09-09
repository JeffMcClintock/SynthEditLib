#pragma once
/*
#include "modules/se_sdk3_hosting/LegacyFileDialogAdapter.h"
*/

#include <string>
#include "../se_sdk3/legacy_sdk_gui2.h"
#include "helpers/NativeUi.h"
#include "RefCountMacros.h"
#include "GmpiSdkCommon.h" // gmpi::shared_ptr

// Implements the old IMpFileDialog interface by delegating to a new-API IFileDialog.
// All platform file-dialog implementations expose only IFileDialog; callers that need
// the legacy interface obtain it via this adapter.
struct LegacyFileDialogAdapter : gmpi_gui::legacy::IMpFileDialog
{
    gmpi::shared_ptr<gmpi::api::IFileDialog> inner;
    std::string selectedFilename;

    // Heap-allocated bridge with independent lifecycle from adapter.
    // Lifetime rules as for LegacyTextEditAdapter: oldCb is a member of the legacy
    // caller and dies with it, so it is forgotten once the caller's ref on us goes.
    struct CompletionBridge : gmpi::api::IFileDialogCallback
    {
        LegacyFileDialogAdapter* adapter;       // null once completion has been delivered
        gmpi_gui::ICompletionCallback* oldCb;   // null once the legacy caller has let go
        int32_t refCount_ = 1;

        CompletionBridge(LegacyFileDialogAdapter* a, gmpi_gui::ICompletionCallback* cb)
            : adapter(a), oldCb(cb) {}

        void onComplete(gmpi::ReturnCode result, const char* selectedPath) override
        {
            auto* a = adapter;
            if (!a)
                return; // already delivered
            adapter = nullptr;
            if (a->activeBridge == this)
                a->activeBridge = nullptr;

            a->selectedFilename = selectedPath ? selectedPath : "";
            if (oldCb)
                oldCb->OnComplete(result == gmpi::ReturnCode::Ok ? gmpi::MP_OK : gmpi::MP_CANCEL);
            oldCb = nullptr;

            a->release(); // balance the addRef in ShowAsync
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

    CompletionBridge* activeBridge{}; // the open dialog's bridge, if any (non-owning)

    explicit LegacyFileDialogAdapter(gmpi::api::IFileDialog* dialog)
    {
        inner.attach(dialog); // caller transfers ownership (refcount already incremented by caller)
    }

    ~LegacyFileDialogAdapter()
    {
        if (activeBridge)
            activeBridge->adapter = nullptr;
    }

    int32_t MP_STDCALL AddExtension(const char* extension, const char* description = "") override
    {
        // Legacy callers commonly pass description="" and expect a default like
        // "wav Files" or "All Files". gmpi_ui's IFileDialog leaves descriptions
        // verbatim, so do the defaulting here in the adapter.
        std::string ext(extension ? extension : "");
        std::string desc(description ? description : "");
        if (desc.empty())
        {
            desc = (ext == "*") ? "All" : ext;
            desc += " Files";
        }
        inner->addExtension(ext.c_str(), desc.c_str());
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
        activeBridge = bridge; // before showAsync: a modal platform completes synchronously
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
    int refCount2_ = 1;
    int32_t addRef() override { return ++refCount2_; }
    int32_t release() override
    {
        if (--refCount2_ == 0) { delete this; return 0; }
        // Only ShowAsync's self-ref left while the dialog is open: the legacy caller has
        // let go (typically its module was destroyed with the view). Its callback lives
        // inside it, so it must never be called again; the dialog runs to completion on
        // its own and the result is discarded.
        if (refCount2_ == 1 && activeBridge)
            activeBridge->oldCb = nullptr;
        return refCount2_;
    }
};
