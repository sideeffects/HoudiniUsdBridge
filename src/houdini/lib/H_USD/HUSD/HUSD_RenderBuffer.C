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
#include "XUSD_Tokens.h"
#include "XUSD_Format.h"
#include <pxr/imaging/hd/aov.h>
#include <pxr/imaging/hd/renderBuffer.h>
#include <UT/UT_Options.h>
#include <UT/UT_HUSDExtraAOVResource.h>
#include <UT/UT_Debug.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace
{
    UT_HUSDExtraAOVResourcePtr
    extraAOVResource(const HdRenderBuffer *b)
    {
        if (b)
        {
            VtValue     resource = b->GetResource(true);

            // Quick check to see if the resource is holding the exact value
            if (resource.IsHolding<UT_HUSDExtraAOVResourcePtr>())
                return resource.UncheckedGet<UT_HUSDExtraAOVResourcePtr>();

            // Otherwise, the resource could be holding an HdAovSettingsMap
            // that's storing the resource ptr behind an opaque
            // shared void * pointer.  If this is the case, we static cast.
            if (!resource.IsHolding<HdAovSettingsMap>())
                return UT_HUSDExtraAOVResourcePtr();

            const auto &map = resource.UncheckedGet<HdAovSettingsMap>();
            auto it = map.find(HusdHuskTokens->extra_aov_resource);
            if (it == map.end())
                return UT_HUSDExtraAOVResourcePtr();

            resource = it->second;
            if (!resource.IsHolding<std::shared_ptr<void>>())
                return UT_HUSDExtraAOVResourcePtr();

            auto data = resource.UncheckedGet<std::shared_ptr<void>>();
            return std::static_pointer_cast<UT_HUSDExtraAOVResource>(data);
        }
        return UT_HUSDExtraAOVResourcePtr();
    }
}


HUSD_RenderBuffer::HUSD_RenderBuffer()
    : myBuffer(nullptr)
    , myIndex(-1)
    , myIsMapped(false)
{
}

HUSD_RenderBuffer::HUSD_RenderBuffer(HdRenderBuffer *b)
    : myBuffer(b)
    , myExtraAOVs(extraAOVResource(b))
    , myIndex(-1)
    , myIsMapped(false)
{
}

HUSD_RenderBuffer::HUSD_RenderBuffer(HUSD_RenderBuffer &base, exint index)
    : myBuffer(base.myBuffer)
    , myExtraAOVs(base.myExtraAOVs)
    , myIndex(index)
    , myIsMapped(false)
{
}

HUSD_RenderBuffer::HUSD_RenderBuffer(HUSD_RenderBuffer &&src)
    : myBuffer(src.myBuffer)
    , myExtraAOVs(std::move(src.myExtraAOVs))
    , myIndex(src.myIndex)
    , myIsMapped(src.myIsMapped)
{
    src.myIsMapped = false;
}

HUSD_RenderBuffer &
HUSD_RenderBuffer::operator=(HUSD_RenderBuffer &&src)
{
    myBuffer = src.myBuffer;
    myExtraAOVs = std::move(src.myExtraAOVs);
    myIndex = src.myIndex;
    myIsMapped = src.myIsMapped;

    src.myIsMapped = false;     // Make sure src isn't mapped
    return *this;
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
        myExtraAOVs = extraAOVResource(b);
    }
}

int
HUSD_RenderBuffer::hdFormat() const
{
    if (myIndex < 0)
        return myBuffer->GetFormat();
    UT_ASSERT(myExtraAOVs);
    return HdFormat(myExtraAOVs->myFormats[myIndex]);
}

PXL_Packing
HUSD_RenderBuffer::packing() const
{
    if (myBuffer)
    {
        switch (HdGetComponentCount(HdFormat(hdFormat())))
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
        switch (HdGetComponentFormat(HdFormat(hdFormat())))
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
    if (myIndex < 0)
        return myBuffer->Map();
    UT_ASSERT(myExtraAOVs);
    return myExtraAOVs->myMap(myIndex);
}

void
HUSD_RenderBuffer::unmap()
{
    UT_ASSERT(myBuffer && myIsMapped);
    myIsMapped = false;
    if (myIndex < 0)
        myBuffer->Unmap();
    else
    {
        UT_ASSERT(myExtraAOVs);
        myExtraAOVs->myUnmap(myIndex);
    }
}

void
HUSD_RenderBuffer::mergeMetaData(UT_Options &metadata) const
{
    if (myExtraAOVs)
    {
        for (auto &&item : myExtraAOVs->myMetadata)
        {
            metadata.setOptionS(UT_StringHolder(item.first),
                                UT_StringHolder(item.second));
        }
    }
}

int
HUSD_RenderBuffer::numExtraBuffers() const
{
    if (myExtraAOVs)
        return myExtraAOVs->myNames.size();
    return 0;
}

HUSD_RenderBuffer
HUSD_RenderBuffer::extraBuffer(int idx)
{
    if (idx >= 0 && idx < numExtraBuffers())
        return HUSD_RenderBuffer(*this, idx);
    return HUSD_RenderBuffer();
}

UT_StringHolder
HUSD_RenderBuffer::extraName(int idx) const
{
    UT_ASSERT(myBuffer && myExtraAOVs);
    return UT_StringHolder(myExtraAOVs->myNames[idx]);
}
