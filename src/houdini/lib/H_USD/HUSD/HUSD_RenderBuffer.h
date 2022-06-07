/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
 *
 * NAME:	HUSD_RenderBuffer.h (HUSD Library, C++)
 *
 * COMMENTS:    Simple interface to access HdRenderBuffer
 */

#ifndef __HUSD_RenderBuffer__
#define __HUSD_RenderBuffer__

#include "HUSD_API.h"
#include <pxr/pxr.h>
#include <PXL/PXL_Common.h>
#include <UT/UT_HUSDExtraAOVResource.h>
#include <UT/UT_NonCopyable.h>

PXR_NAMESPACE_OPEN_SCOPE
class HdRenderBuffer;
PXR_NAMESPACE_CLOSE_SCOPE

class UT_Options;
class UT_StringHolder;

/// Simple interface around HdRenderBuffer
class HUSD_API HUSD_RenderBuffer : UT_NonCopyable
{
public:
    HUSD_RenderBuffer();
    HUSD_RenderBuffer(PXR_NS::HdRenderBuffer *b);
    HUSD_RenderBuffer(HUSD_RenderBuffer &&src);
    ~HUSD_RenderBuffer();

    HUSD_RenderBuffer   &operator=(HUSD_RenderBuffer &&src);

    /// Check whether the buffer is valid or not
    bool        isValid() const { return myBuffer != nullptr; }

    /// Simple bool operator for validity check
    SYS_SAFE_BOOL       operator bool() const { return isValid(); }

    /// Set the render buffer
    void        setBuffer(PXR_NS::HdRenderBuffer *b);

    /// Query the pixel packing for the buffer
    PXL_Packing         packing() const;
    /// Query the pixel data format for the buffer
    PXL_DataFormat      dataFormat() const;

    /// @{
    /// Resolution information
    exint               xres() const;
    exint               yres() const;
    /// @}

    /// Map the data from the buffer.  This can only be called if the buffer is
    /// not currently mapped.
    const void          *map();
    /// Unmap the buffer's data.  Note that the destructor for this class will
    /// automatically unmap the buffer if it's been mapped.
    void                 unmap();

    /// Test if the buffer is mapped (for debugging)
    bool                 isMapped() const { return myIsMapped; }

    /// This method will merge in any additional extra metadata available if
    /// the buffer has a cryptomatte resource
    void        mergeMetaData(UT_Options &metadata) const;

    /// Return the number of extra buffers associated with this render buffer
    int         numExtraBuffers() const;

    /// Return an HUSD_RenderBuffer for the extra buffer
    HUSD_RenderBuffer   extraBuffer(int idx);

    /// Return the name of the extra buffer
    UT_StringHolder     extraName(int idx) const;

private:
    HUSD_RenderBuffer(HUSD_RenderBuffer &base, exint index);
    int hdFormat() const;

    PXR_NS::HdRenderBuffer      *myBuffer;
    UT_HUSDExtraAOVResourcePtr   myExtraAOVs;
    int                          myIndex;
    bool                         myIsMapped;
};

#endif
