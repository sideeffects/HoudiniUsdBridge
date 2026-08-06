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

#pragma once

#include <pxr/pxr.h>
#include <pxr/imaging/hd/renderBuffer.h>
#include <SYS/SYS_Types.h>

PXR_NAMESPACE_OPEN_SCOPE        // [

class HDAOV final : public HdRenderBuffer
{
public:
    static constexpr const char *class_name = "HDAOV";

    HDAOV(const SdfPath &id);
    ~HDAOV() override;

    bool Allocate(const GfVec3i &dims, HdFormat fmt, bool multi) override;
    void _Deallocate() override;

    HdFormat    GetFormat() const override { return myFormat; }
    uint        GetDepth() const override { return myDepth; }
    uint        GetWidth() const override { return myWidth; }
    uint        GetHeight() const override { return myHeight; }
    bool        IsMultiSampled() const override { return myMultiSampled; }

    void        *Map() override;
    void         Unmap() override;
    bool         IsMapped() const override;
    bool         IsConverged() const override;
    void         Resolve() override;

    /// Cryptomatte interface (and other custom AOVs)
    VtValue      GetResource(bool multiSampled) const override;
    void        *MapExtra(int idx);
    void         UnmapExtra(int idx);

    void         setConverged() { myIsConverged = true; }

private:
    size_t      bufferBytes() const
    {
        return HdDataSizeOfFormat(myFormat)
                            * size_t(myWidth)
                            * size_t(myHeight)
                            * size_t(myDepth);
    }
    HdFormat                    myFormat = HdFormatInvalid;
    uint                        myWidth = 0;
    uint                        myHeight = 0;
    uint                        myDepth = 0;
    bool                        myMultiSampled = false;
    bool                        myIsConverged = false;
    std::unique_ptr<char[]>     myBuffer;
    std::atomic<int>            myMapCount = 0;
};

PXR_NAMESPACE_CLOSE_SCOPE       // ]
