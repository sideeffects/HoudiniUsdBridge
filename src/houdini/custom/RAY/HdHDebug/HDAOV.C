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
 *      Side Effects Software Inc.
 *      123 Front Street West, Suite 1401
 *      Toronto, Ontario
 *      Canada   M5J 2M2
 *      416-504-9876
 *
 */

#include "HDAOV.h"
#include "HDUtil.h"

PXR_NAMESPACE_OPEN_SCOPE        // [

static constexpr UT_StringLit   theBufferLabel("AOV::Buffer");

HDAOV::HDAOV(const SdfPath &id)
    : HdRenderBuffer(id)
{
    HDEBUG_CTOR();
}

HDAOV::~HDAOV()
{
    if (myBuffer)
        _Deallocate();
    HDEBUG_DTOR();
}

bool
HDAOV::Allocate(const GfVec3i &dims, HdFormat fmt, bool multi)
{
    HDEBUG_TRACE_FUNCTION();
    HDEBUG_ASSERT(!myBuffer && "Expected unallocated buffer");
    if (fmt == HdFormatInvalid || dims[0] < 1 || dims[1] < 1 || dims[2] != 1)
        return false;
    if (fmt == myFormat
            && dims[0] == myWidth
            && dims[1] == myHeight
            && dims[2] == myDepth)
    {
        return false;   // Already allocated
    }

    if (myBuffer)
        _Deallocate();  // Free memory first

    myFormat = fmt;
    myWidth = dims[0];
    myHeight = dims[1];
    myDepth = dims[2];
    myMultiSampled = multi;
    myBuffer = std::make_unique<char[]>(bufferBytes());
    HDUtil::trackMemory(theBufferLabel.asHolder(), bufferBytes());

    return true;
}

void
HDAOV::_Deallocate()
{
    HDEBUG_TRACE_FUNCTION();
    if (myBuffer)
    {
        HDUtil::trackMemory(theBufferLabel.asHolder(), bufferBytes());
        myBuffer.reset(nullptr);
    }
}

void *
HDAOV::Map()
{
    HDEBUG_TRACE_FUNCTION();
    HDEBUG_ASSERT(myBuffer);
    myMapCount.fetch_add(1);
    return myBuffer.get();
}

void
HDAOV::Unmap()
{
    HDEBUG_TRACE_FUNCTION();
    HDEBUG_VERIFY(myMapCount.fetch_add(-1) > 0);
}

bool
HDAOV::IsMapped() const
{
    HDEBUG_TRACE_FUNCTION();
    return myMapCount.load() > 0;
}

bool
HDAOV::IsConverged() const
{
    HDEBUG_TRACE_FUNCTION();
    return myIsConverged;
}

void
HDAOV::Resolve()
{
    HDEBUG_TRACE_FUNCTION();
}

VtValue
HDAOV::GetResource(bool multiSampled) const
{
    HDEBUG_TRACE_FUNCTION();
    return VtValue();
}

void *
HDAOV::MapExtra(int idx)
{
    HDEBUG_TRACE_FUNCTION();
    return nullptr;
}

void
HDAOV::UnmapExtra(int idx)
{
    HDEBUG_TRACE_FUNCTION();
}

PXR_NAMESPACE_CLOSE_SCOPE        // ]
