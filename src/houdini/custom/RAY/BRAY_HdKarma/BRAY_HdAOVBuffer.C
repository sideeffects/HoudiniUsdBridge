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
#include "BRAY_HdIO.h"
#include <UT/UT_Debug.h>
#include <HUSD/XUSD_Format.h>

PXR_NAMESPACE_OPEN_SCOPE

static HdFormat
getHdFormat(const PXL_DataFormat format, const PXL_Packing packing)
{
    switch (format)
    {
	case PXL_INT8:
	    switch (packing)
	    {
		case PACK_SINGLE:	return HdFormatUNorm8;
		case PACK_DUAL:		return HdFormatUNorm8Vec2;
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
		case PACK_DUAL:		return HdFormatFloat16Vec2;
		case PACK_RGB:		return HdFormatFloat16Vec3;
		case PACK_RGBA:		return HdFormatFloat16Vec4;
		default:		break;
	    }
	    break;
	case PXL_FLOAT32:
	    switch (packing)
	    {
		case PACK_SINGLE:	return HdFormatFloat32;
		case PACK_DUAL:		return HdFormatFloat32Vec2;
		case PACK_RGB:		return HdFormatFloat32Vec3;
		case PACK_RGBA:		return HdFormatFloat32Vec4;
		default:		break;
	    }
	    break;
	case PXL_INT32:
	    switch (packing)
	    {
		case PACK_SINGLE:	return HdFormatInt32;
		case PACK_DUAL:		return HdFormatInt32Vec2;
		case PACK_RGB:		return HdFormatInt32Vec3;
		case PACK_RGBA:		return HdFormatInt32Vec4;
		default:		break;
	    }
	    break;
	default:
	    break;
    }
    return HdFormatInvalid;
}

BRAY_HdAOVBuffer::BRAY_HdAOVBuffer(const SdfPath &id)
    : XUSD_HydraRenderBuffer(id)
    , myConverged(0)
    , myMultiSampled(false)
    , myWidth(0)
    , myHeight(0)
    , myFormat(HdFormatInvalid)
{
    BRAYformat(4, "New AOV: {}", id);
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
	BRAYwarning("AOV Buffer dimensions: {}, depth must be 1", dimensions);
	return false;
    }

    myWidth = dimensions[0];
    myHeight = dimensions[1];
    myFormat = format;
    myMultiSampled = multiSampled;

    BRAYformat(8, "Allocate AOV buffer: {}", dimensions);
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

void
BRAY_HdAOVBuffer::Resolve()
{
}

void
BRAY_HdAOVBuffer::_Deallocate()
{
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
	memset(myTempbuf.get(), 0, bufsize);
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

int
BRAY_HdAOVBuffer::NumExtra() const
{
    if (!myAOVBuffer)
	return 0;
    return myAOVBuffer.getNumExtra();
}

HdFormat
BRAY_HdAOVBuffer::GetFormatExtra(int idx) const
{
    if (!myAOVBuffer)
	return HdFormatInvalid;

    return getHdFormat(myAOVBuffer.getFormatExtra(idx),
	myAOVBuffer.getPackingExtra(idx));
}

const UT_StringHolder &
BRAY_HdAOVBuffer::GetPlaneName(int idx) const
{
    if (!myAOVBuffer)
	return UT_StringHolder::theEmptyString;

    return myAOVBuffer.nameExtra(idx);
}

void*
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
    if (!myAOVBuffer)
	return;

    return myAOVBuffer.unmapExtra(idx);
}

const UT_Options &
BRAY_HdAOVBuffer::GetMetadata() const
{
    static UT_Options theEmptyOptions;
    if (!myAOVBuffer)
	return theEmptyOptions;

    return myAOVBuffer.getMetadata();
}

PXR_NAMESPACE_CLOSE_SCOPE
