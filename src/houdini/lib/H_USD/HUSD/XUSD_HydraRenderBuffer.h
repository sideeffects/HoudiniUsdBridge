/*
 * Copyright 2019 Side Effects Software Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Produced by:
 *	Side Effects Software Inc
 *	123 Front Street West, Suite 1401
 *	Toronto, Ontario
 *	Canada   M5J 2M2
 *	416-504-9876
 *
 * NAME:	XUSD_HydraRenderBuffer.h (HUSD Library, C++)
 *
 * COMMENTS:	A hydra renderBuffer bprim (HdBprim)
 *		Extension of HdRenderBuffer to enable husk to write out a
 *		multi-plane AOV (e.g. Cryptomatte).
 */
#ifndef XUSD_HydraRenderBuffer_h
#define XUSD_HydraRenderBuffer_h

#include <pxr/pxr.h>
#include <pxr/imaging/hd/renderBuffer.h>
#include <UT/UT_Options.h>
#include <UT/UT_StringHolder.h>

PXR_NAMESPACE_OPEN_SCOPE

/// This is an extension to HdRenderBuffer to allow creation of multi-plane
/// AOVs (e.g. for Cryptomatte support).
class XUSD_HydraRenderBuffer : public HdRenderBuffer
{
public:
    XUSD_HydraRenderBuffer(SdfPath const& primId)
         : HdRenderBuffer(primId)
     {
     }
    ~XUSD_HydraRenderBuffer() override
     {
     }

    /// Return the number of extra image planes.
    virtual int NumExtra() const = 0;

    /// Get the extra buffer's per-pixel format.
    virtual HdFormat GetFormatExtra(int idx) const = 0;

    /// Get the extra buffer's plane name.
    virtual const UT_StringHolder &GetPlaneName(int idx) const = 0;

    /// Map the extra buffer for reading.
    virtual void* MapExtra(int idx) = 0;

    /// Unmap the extra buffer. It is no longer safe to read from the buffer.
    virtual void UnmapExtra(int idx) = 0;

#if 0 // Currently IsMapped() applies to both primary and extra buffers
    /// Return whether the extra buffer is currently mapped by anybody.
    virtual bool IsMappedExtra(int idx) const = 0;
#endif

    /// Return arbitrary metadata associated with this AOV.
    /// Only string values are allowed at the moment.
    virtual const UT_Options &GetMetadata() const = 0;
};

PXR_NAMESPACE_CLOSE_SCOPE
#endif
