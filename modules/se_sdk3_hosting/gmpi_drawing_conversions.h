#pragma once

/* Copyright (c) 2007-2025 SynthEdit Ltd
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*     * Redistributions in binary form must reproduce the above copyright
*       notice, this list of conditions and the following disclaimer in the
*       documentation and/or other materials provided with the distribution.
*     * Neither the name SEM, nor SynthEdit, nor 'Music Plugin Interface' nor the
*       names of its contributors may be used to endorse or promote products
*       derived from this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY SynthEdit Ltd ``AS IS'' AND ANY
* EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL SynthEdit Ltd BE LIABLE FOR ANY
* DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
* (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
* LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
* ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
* SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "./Drawing.h"				// SDK3
#include "./GmpiUiDrawing.h"		// GMPI-UI SDK

/*
#include "gmpi_drawing_conversions.h"

using namespace legacy_converters;
*/

namespace legacy_converters
{

inline gmpi::drawing::Point convert(GmpiDrawing_API::MP1_POINT p) { return { p.x, p.y }; }
inline GmpiDrawing_API::MP1_POINT convert(gmpi::drawing::Point p) { return { p.x, p.y }; }

inline gmpi::drawing::Size convert(GmpiDrawing_API::MP1_SIZE p) { return { p.width, p.height }; }
inline GmpiDrawing_API::MP1_SIZE convert(gmpi::drawing::Size p) { return { p.width, p.height }; }

// utilities for working with legacy graphics api.
inline GmpiDrawing::Rect toLegacy(gmpi::drawing::Rect r)
{
    return { r.left, r.top, r.right, r.bottom };
}
inline GmpiDrawing::Matrix3x2 toLegacy(gmpi::drawing::Matrix3x2 m)
{
    return { m._11, m._12, m._21, m._22, m._31, m._32 };
}
inline gmpi::drawing::Rect fromLegacy(GmpiDrawing_API::MP1_RECT r)
{
    return { r.left, r.top, r.right, r.bottom };
}
// mixing new matrix with old rect (for convinience)
inline GmpiDrawing_API::MP1_RECT operator*(GmpiDrawing_API::MP1_RECT rect, gmpi::drawing::Matrix3x2 transform)
{
    return {
        rect.left * transform._11 + rect.top * transform._21 + transform._31,
        rect.left * transform._12 + rect.top * transform._22 + transform._32,
        rect.right * transform._11 + rect.bottom * transform._21 + transform._31,
        rect.right * transform._12 + rect.bottom * transform._22 + transform._32
    };
}

}
