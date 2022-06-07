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
 *	Side Effects Software Inc.
 *	123 Front Street West, Suite 1401
 *	Toronto, Ontario
 *      Canada   M5J 2M2
 *	416-504-9876
 *
 */

#include "BRAY_HdAOVBuffer.h"
#include "BRAY_HdFormat.h"
#include <UT/UT_Debug.h>
#include <UT/UT_ErrorLog.h>
#include <UT/UT_HUSDExtraAOVResource.h>      // header only

PXR_NAMESPACE_OPEN_SCOPE

static HdFormat
getHdFormat(const PXL_DataFormat format, const PXL_Packing packing)
{
    UT_ASSERT(packing != PACK_DUAL);
    switch (format)
    {
	case PXL_INT8:
	    switch (packing)
	    {
		case PACK_SINGLE:	return HdFormatUNorm8;
		case PACK_UV:		return HdFormatUNorm8Vec2;
		case PACK_RGB:		return HdFormatUNorm8Vec3;
		case PACK_RGBA:		return HdFormatUNorm8Vec4;
		default:		break;
	    }
	    break;
	    return HdFormatInvalid;
	case PXL_FLOAT16:
	    switch (packing)
	    {
		case PACK_SINGLE:	return HdFormatFloat16;
		case PACK_UV:		return HdFormatFloat16Vec2;
		case PACK_RGB:		return HdFormatFloat16Vec3;
		case PACK_RGBA:		return HdFormatFloat16Vec4;
		default:		break;
	    }
	    break;
	case PXL_FLOAT32:
	    switch (packing)
	    {
		case PACK_SINGLE:	return HdFormatFloat32;
		case PACK_UV:		return HdFormatFloat32Vec2;
		case PACK_RGB:		return HdFormatFloat32Vec3;
		case PACK_RGBA:		return HdFormatFloat32Vec4;
		default:		break;
	    }
	    break;
	case PXL_INT32:
	    switch (packing)
	    {
		case PACK_SINGLE:	return HdFormatInt32;
		case PACK_UV:		return HdFormatInt32Vec2;
		case PACK_RGB:		return HdFormatInt32Vec3;
		case PACK_RGBA:		return HdFormatInt32Vec4;
		default:		break;
	    }
	    break;
	default:
	    break;
    }
    UT_ASSERT(0);
    return HdFormatInvalid;
}

BRAY_HdAOVBuffer::BRAY_HdAOVBuffer(const SdfPath &id)
    : HdRenderBuffer(id)
    , myConverged(0)
    , myMultiSampled(false)
    , myWidth(0)
    , myHeight(0)
    , myFormat(HdFormatInvalid)
{
    if (!id.IsEmpty())
        UT_ErrorLog::format(4, "New AOV: {}", id);
}

BRAY_HdAOVBuffer::~BRAY_HdAOVBuffer()
{
    UT_ASSERT(!IsMapped());
}

bool
BRAY_HdAOVBuffer::Allocate(const GfVec3i &dimensions,
	HdFormat format, bool multiSampled)
{
    UT_ASSERT(format != HdFormatInvalid);
    if (format == HdFormatInvalid)
	return false;

    if (dimensions[0] == myWidth
	    && dimensions[1] == myHeight
	    && format == myFormat
	    && multiSampled == myMultiSampled)
    {
	return false;	// Already allocated
    }
    if (dimensions[2] != 1)
    {
        UT_ErrorLog::warning("AOV Buffer dimensions: {}, depth must be 1", dimensions);
	return false;
    }

    myWidth = dimensions[0];
    myHeight = dimensions[1];
    myFormat = format;
    myMultiSampled = multiSampled;

    UT_ErrorLog::format(8, "Allocate AOV buffer: {} {}", dimensions, format);
    _Deallocate();	// Clear the raster

    return true;
}

bool
BRAY_HdAOVBuffer::IsConverged() const
{
    if (!myAOVBuffer)
	return (myConverged.load() != 0);
    return myAOVBuffer.isConverged();
}

void
BRAY_HdAOVBuffer::setConverged()
{
    if (!myAOVBuffer)
	myConverged.exchangeAdd(1);
    myAOVBuffer.setConverged();
}

void
BRAY_HdAOVBuffer::clearConverged()
{
    if (!myAOVBuffer)
	myConverged.exchange(0);
    myAOVBuffer.clearConverged();
}

HdFormat
BRAY_HdAOVBuffer::GetFormat() const
{
    if (!myAOVBuffer)
	return myFormat;

    return getHdFormat(myAOVBuffer.getFormat(), myAOVBuffer.getPacking());
}

uint
BRAY_HdAOVBuffer::GetWidth() const
{
    if (!myAOVBuffer)
	return myWidth;
    return myAOVBuffer.getXres();
}

uint
BRAY_HdAOVBuffer::GetHeight() const
{
    if (!myAOVBuffer)
	return myHeight;
    return myAOVBuffer.getYres();
}

void *
BRAY_HdAOVBuffer::Map()
{
    if (!myAOVBuffer)
    {
	// Mapped before BRAY::AOVBufferPtr set. Mallocing tmp buffer.
	int bufsize = myWidth * myHeight * HdDataSizeOfFormat(myFormat);
	UT_ASSERT(!myTempbuf);
	myTempbuf = UTmakeUnique<uint8_t[]>(bufsize);
        float defval = myAOVBuffer.getDefaultValue();
        if (defval != 0.0f &&
            HdGetComponentFormat(myFormat) == HdFormatFloat32)
        {
            float *dst = (float *)myTempbuf.get();
            int ncomp = HdGetComponentCount(myFormat);
            std::fill(dst, dst + myWidth * myHeight * ncomp, defval);
        }
        else if (defval != 0.0f &&
            HdGetComponentFormat(myFormat) == HdFormatInt32)
        {
            int ncomp = HdGetComponentCount(myFormat);
            int *dst = (int *)myTempbuf.get();
            std::fill(dst, dst + myWidth * myHeight * ncomp, (int)defval);
        }
        else
        {
            UT_ASSERT(defval == 0);
            memset(myTempbuf.get(), 0, bufsize);
        }
	return myTempbuf.get();
    }

    return myAOVBuffer.map();
}

void
BRAY_HdAOVBuffer::Unmap()
{
    if (myTempbuf)
    {
	myTempbuf.reset(nullptr);	// Just in case
    }
    else
    {
	UT_ASSERT(myAOVBuffer);
	myAOVBuffer.unmap();
    }
}

bool
BRAY_HdAOVBuffer::IsMapped() const
{
    if (!myAOVBuffer)
	return false;
    return myAOVBuffer.isMapped();
}

void *
BRAY_HdAOVBuffer::MapExtra(int idx)
{
    if (!myAOVBuffer)
    {
	// shouldn't have to worry about making temporary buffers since unset
	// AOV implies no extra channels, so nothing should be trying to map
	// it.
	return nullptr;
    }

    return myAOVBuffer.mapExtra(idx);
}

void
BRAY_HdAOVBuffer::UnmapExtra(int idx)
{
    if (myAOVBuffer)
        myAOVBuffer.unmapExtra(idx);
}

VtValue
BRAY_HdAOVBuffer::GetResource(bool) const
{
    if (!myAOVBuffer || myAOVBuffer.getNumExtra() <= 0)
        return VtValue();

    auto resource = std::make_shared<UT_HUSDExtraAOVResource>(
            [this](int idx) { return SYSconst_cast(this)->MapExtra(idx); },
            [this](int idx) { SYSconst_cast(this)->UnmapExtra(idx); }
    );

    for (int i = 0, n = myAOVBuffer.getNumExtra(); i < n; ++i)
    {
        int     hd_format = int(getHdFormat(myAOVBuffer.getFormatExtra(i),
                                          myAOVBuffer.getPackingExtra(i)));
        resource->addPlane(myAOVBuffer.nameExtra(i).toStdString(), hd_format);
    }
    for (auto it = myAOVBuffer.getMetadata().begin(),
              n = myAOVBuffer.getMetadata().end(); it != n; ++it)
    {
        const UT_StringHolder   &value = (*it)->getOptionS();
        if (value)
        {
            const UT_StringHolder   &key = it.name();
            resource->addMetadata(key.toStdString(), value.toStdString());
        }
    }

    return VtValue(resource);
}

PXR_NAMESPACE_CLOSE_SCOPE
