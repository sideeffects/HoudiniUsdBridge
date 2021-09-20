/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
 *
 * NAME:	HUSD_RenderBuffer.h (HUSD Library, C++)
 *
 * COMMENTS:    Simple interface to access HdRenderBuffer
 */

#include "HUSD_RenderBuffer.h"
#include "XUSD_HydraRenderBuffer.h"
#include <UT/UT_Options.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace
{
    bool
    isXUSDBuffer(const HdRenderBuffer *b)
    {
        return dynamic_cast<const XUSD_HydraRenderBuffer *>(b) != nullptr;
    }

    XUSD_HydraRenderBuffer *
    xusdBuffer(HdRenderBuffer *b)
    {
        return UTverify_cast<XUSD_HydraRenderBuffer *>(b);
    }
}


HUSD_RenderBuffer::HUSD_RenderBuffer()
    : myBuffer(nullptr)
    , myIndex(-1)
    , myIsXUSD(false)
    , myIsMapped(false)
{
}

HUSD_RenderBuffer::HUSD_RenderBuffer(HdRenderBuffer *b)
    : myBuffer(b)
    , myIndex(-1)
    , myIsXUSD(isXUSDBuffer(b))
    , myIsMapped(false)
{
}

HUSD_RenderBuffer::HUSD_RenderBuffer(HUSD_RenderBuffer &base, exint index)
    : myBuffer(base.myBuffer)
    , myIndex(index)
    , myIsXUSD(false)   // No nested XUSD AOVs
    , myIsMapped(false)
{
}

HUSD_RenderBuffer::~HUSD_RenderBuffer()
{
    if (myIsMapped)
        unmap();
}

void
HUSD_RenderBuffer::setBuffer(PXR_NS::HdRenderBuffer *b)
{
    if (b != myBuffer)
    {
        if (myIsMapped)
            unmap();
        myBuffer = b;
        myIsXUSD = isXUSDBuffer(b);
    }
}

PXL_Packing
HUSD_RenderBuffer::packing() const
{
    if (myBuffer)
    {
        HdFormat    format = myIndex < 0 ? myBuffer->GetFormat()
                                : xusdBuffer(myBuffer)->GetFormatExtra(myIndex);
        switch (HdGetComponentCount(format))
        {
            case 1: return PACK_SINGLE;
            case 2: return PACK_UV;
            case 3: return PACK_RGB;
            case 4: return PACK_RGBA;
        }
    }
    UT_ASSERT(0);
    return PACK_UNKNOWN;
}

PXL_DataFormat
HUSD_RenderBuffer::dataFormat() const
{
    if (myBuffer)
    {
        HdFormat    format = myIndex < 0 ? myBuffer->GetFormat()
                                : xusdBuffer(myBuffer)->GetFormatExtra(myIndex);
        switch (HdGetComponentFormat(format))
        {
            case HdFormatUNorm8:
            case HdFormatSNorm8:
                return PXL_INT8;
            case HdFormatFloat16:
                return PXL_FLOAT16;
            case HdFormatFloat32:
                return PXL_FLOAT32;
            case HdFormatInt32:
                return PXL_INT32;
            default:
                break;
        }
    }
    UT_ASSERT(0);
    return PXL_INT8;
}

exint
HUSD_RenderBuffer::xres() const
{
    return myBuffer ? myBuffer->GetWidth() : 0;
}

exint
HUSD_RenderBuffer::yres() const
{
    return myBuffer ? myBuffer->GetHeight() : 0;
}

const void *
HUSD_RenderBuffer::map()
{
    UT_ASSERT(!myIsMapped);
    if (myIsMapped)
        return nullptr;
    myIsMapped = true;
    return myIndex < 0 ? myBuffer->Map()
        : xusdBuffer(myBuffer)->MapExtra(myIndex);
}

void
HUSD_RenderBuffer::unmap()
{
    UT_ASSERT(myBuffer && myIsMapped);
    myIsMapped = false;
    if (myIndex < 0)
        myBuffer->Unmap();
    else
        xusdBuffer(myBuffer)->UnmapExtra(myIndex);
}

void
HUSD_RenderBuffer::mergeMetaData(UT_Options &metadata) const
{
    if (myIsXUSD)
        metadata.merge(xusdBuffer(myBuffer)->GetMetadata());
}

int
HUSD_RenderBuffer::numExtraBuffers() const
{
    if (!myIsXUSD)
        return 0;
    return xusdBuffer(myBuffer)->NumExtra();
}

HUSD_RenderBuffer
HUSD_RenderBuffer::extraBuffer(int idx)
{
    if (!myIsXUSD || idx < 0 || idx >= xusdBuffer(myBuffer)->NumExtra())
        return HUSD_RenderBuffer();
    return HUSD_RenderBuffer(*this, idx);
}

const UT_StringHolder &
HUSD_RenderBuffer::extraName(int idx) const
{
    UT_ASSERT(myBuffer && myIsXUSD);
    return xusdBuffer(myBuffer)->GetPlaneName(idx);
}
