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

#ifndef __aovBuffer__
#define __aovBuffer__

#include <pxr/pxr.h>
#include <pxr/imaging/hd/renderBuffer.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec4f.h>
#include <BRAY/BRAY_Interface.h>
#include <SYS/SYS_AtomicInt.h>
#include <UT/UT_UniquePtr.h>

PXR_NAMESPACE_OPEN_SCOPE

class BRAY_HdAOVBuffer final
    : public HdRenderBuffer
{
public:
    BRAY_HdAOVBuffer(const SdfPath &id);
    ~BRAY_HdAOVBuffer() override;

    bool                Allocate(const GfVec3i &dimensions,
				HdFormat format,
				bool multiSampled) override;

    HdFormat            GetFormat() const override;
    uint                GetDepth() const override { return 1; }
    uint                GetWidth() const override;
    uint                GetHeight() const override;
    bool                IsMultiSampled() const override
				{ return myMultiSampled; }

    void                *Map() override;
    void                Unmap() override;
    bool                IsMapped() const override;

    void                *MapExtra(int idx);
    void                UnmapExtra(int idx);

    bool                IsConverged() const override;
    void		setConverged();
    void		clearConverged();

    void                Resolve() override {}

    bool		isValid() const { return myAOVBuffer.isValid(); }
    const BRAY::AOVBufferPtr	&aovBuffer() const { return myAOVBuffer; }
    void		setAOVBuffer(const BRAY::AOVBufferPtr &aov)
    {
	myAOVBuffer = aov;
    }

    VtValue     GetResource(bool multiSampled) const override;

private:
    void        _Deallocate() override {};

    BRAY::AOVBufferPtr		myAOVBuffer;
    UT_UniquePtr<uint8_t[]>	myTempbuf;
    SYS_AtomicInt32		myConverged;
    int				myWidth, myHeight;
    HdFormat			myFormat;
    bool			myMultiSampled;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
