//*********************************************************************
//* C_Base64 - a simple base64 encoder and decoder.
//*
//*     Copyright (c) 1999, Bob Withers - bwit@pobox.com
//*
//* This code may be freely used for any purpose, either personal
//* or commercial, provided the authors copyright notice remains
//* intact.
//*********************************************************************
//
// The implementation moved to GMPI (Core/base64.h) because the preset
// writer and reader down there need it too, and one base64 is enough. This
// header stays as a thin forwarder so the existing std::string-based call
// sites (UPlug, RawConversions, MyTypeTraits, SynthEdit2/PatchParameter)
// keep working unchanged.

#ifndef Base64_H
#define Base64_H

#include <string>

#include "Core/base64.h"

class Base64
{
public:
    static std::string encode(const std::string data)
    {
        return gmpi::base64Encode(std::string_view(data));
    }

    static std::string decode(std::string data)
    {
        const auto bytes = gmpi::base64Decode(data);
        return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }
};

#endif
