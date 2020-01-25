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

#ifndef __BRAY_HdMesh__
#define __BRAY_HdMesh__

#include <pxr/pxr.h>
#include <pxr/imaging/hd/mesh.h>
#include <pxr/imaging/hd/enums.h>
#include <pxr/base/gf/matrix4f.h>

#include <BRAY/BRAY_Interface.h>

PXR_NAMESPACE_OPEN_SCOPE

class BRAY_HdParam;

class BRAY_HdMesh : public HdMesh
{
public:
    BRAY_HdMesh(SdfPath const &id,
	    SdfPath const &instancerId = SdfPath());
    virtual ~BRAY_HdMesh();

    /// Release any resources this class is holding onto - in this case,
    /// destroy the geometry object in the scene graph.
    virtual void	Finalize(HdRenderParam *renderParam) override final;

    /// Pull invalidated scene data and prepare/update the renderable
    /// representation.
    virtual void	Sync(HdSceneDelegate *sceneDelegate,
				HdRenderParam *renerParam,
				HdDirtyBits *dirtyBits,
				TfToken const &repr) override final;

    /// Inform the scene graph which state needs to be downloaded in the first
    /// Sync() call.  In this case, topology and point data.
    virtual HdDirtyBits	GetInitialDirtyBitsMask() const override final;

protected:
    /// This callback gives the prim an opportunity to set additional dirty
    /// bits based on those already set.
    virtual HdDirtyBits	_PropagateDirtyBits(HdDirtyBits bits)
				const override final;

    /// Initialize the given representation of the prim
    virtual void	_InitRepr(TfToken const &repr,
				HdDirtyBits *dirtyBits) override final;

private:
    void	updateGTMesh(BRAY_HdParam &rparm,
			HdSceneDelegate *sceneDelegate,
			HdDirtyBits *dirtyBits,
			HdMeshReprDesc const &desc);
    void	setMesh(const BRAY::ObjectPtr &ptr);

    BRAY::ObjectPtr	    myInstance;
    BRAY::ObjectPtr	    myMesh;
    bool		    myComputeN;
    bool		    myLeftHanded;
    UT_Array<GfMatrix4d>    myXform;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
