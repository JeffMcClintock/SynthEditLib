#pragma once

// SPDX-License-Identifier: ISC
// Copyright 2007-2026 Jeff McClintock.

// Mono-directional pins for gmpi::editor2 modules - anything inheriting gmpi::api::IEditor2 (the
// PluginEditor / PluginEditorNoGui / GraphicsProcessor family). Such modules are strictly one-way:
// data flows IN via input pins and OUT via output pins, never both on the same pin.
//
// The SDK's gmpi::editor2::Pin<T> is BIDIRECTIONAL - setFromHost() reads it AND operator= sends it.
// That's right for an audio plug, but in an editor module it lets you accidentally drive an input
// or read an output. These make the direction part of the type, so the compiler enforces it:
//
//   In<T>        - read-only value pin   (host writes .value; no operator=)
//   Out<T>       - send-only value pin   (operator= sends to the host; host never writes it)
//   ObjectIn<T>  - read-only object pin  (queryInterface'd to T* inside the pin)
//   ObjectOut<T> - send-only object pin
//
// A module uses the mono-directional set OR the bidirectional Pin<T>, not both.

#include "helpers/GmpiPluginEditor2.h"

namespace gmpi
{
namespace editor2
{

// Read-only value pin: the host writes 'value'; there is deliberately no operator= to send.
template<typename T>
class In : public PinBase
{
    void setFromHost(int32_t /*voice*/, int32_t size, const uint8_t* data) override
    {
        dirty = true;
        valueFromData({ data, static_cast<size_t>(size) }, value);
        if (onUpdate)
            onUpdate(this);
    }

public:
    T value{};
};

// Send-only value pin: assigning sends to the host; the host never drives it.
template<typename T>
class Out : public PinBase
{
    void setFromHost(int32_t /*voice*/, int32_t /*size*/, const uint8_t* /*data*/) override
    {
        // N/A - an output is never driven by the host.
    }

public:
    T value{};

    const T& operator=(const T& pvalue)
    {
        if (pvalue != value)
        {
            value = pvalue;
            host->setPin(id, 0, dataSize(value), dataPtr(value));
        }
        return value;
    }
};

// Read-only object pin templated on its interface: queryInterface happens inside the pin, so user
// code just reads pinFoo.value (a gmpi::shared_ptr<T>).
template<typename T>
class ObjectIn : public PinBase
{
    void setFromHost(int32_t /*voice*/, int32_t size, const uint8_t* data) override
    {
        dirty = true;

        gmpi::api::IUnknown* temp{};

        if (size)
            valueFromData({ data, static_cast<size_t>(size) }, temp);

        if (temp)
            temp->queryInterface(&T::guid, (void**)value.put_void());
        else
            value = nullptr;

        if (onUpdate)
            onUpdate(this);
    }

public:
    gmpi::shared_ptr<T> value;

    operator bool() const
    {
        return !value.isNull();
    }
};

// Send-only object pin.
template<typename T>
class ObjectOut : public PinBase
{
    void setFromHost(int32_t /*voice*/, int32_t /*size*/, const uint8_t* /*data*/) override
    {
        // N/A - an output is never driven by the host.
    }

public:
    gmpi::shared_ptr<T> value;

    void send()
    {
        const auto& ptr = value.get();
        host->setPin(id, 0, dataSize(ptr), dataPtr(ptr));
    }

    // assign the object and send it out the pin in one step.
    T* operator=(T* pvalue)
    {
        value = pvalue;
        send();
        return pvalue;
    }

    operator bool() const
    {
        return !value.isNull();
    }
};

} // namespace editor2
} // namespace gmpi
