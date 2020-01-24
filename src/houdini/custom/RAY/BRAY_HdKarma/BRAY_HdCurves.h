/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
 *
 * NAME:	BRAY_HdCurves.h (RAY Library, C++)
 *
 * COMMENTS:
 */

#ifndef __BRAY_HdCurves__
#define __BRAY_HdCurves__

#include <pxr/pxr.h>
#include <pxr/imaging/hd/basisCurves.h>
#include <pxr/imaging/hd/enums.h>
#include <pxr/base/gf/matrix4f.h>

#include <BRAY/BRAY_Interface.h>

PXR_NAMESPACE_OPEN_SCOPE

class BRAY_HdParam;

class BRAY_HdCurves : public HdBasisCurves
{
public:
    BRAY_HdCurves(SdfPath const &id,
	    SdfPath const &instancerId = SdfPath());
    virtual ~BRAY_HdCurves();

    /// Release any resources this class is holding onto - in this case,
    /// destroy the geometry object in the karma scene graph.
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
    void	updateGTCurves(BRAY_HdParam &rparm,
			HdSceneDelegate *sceneDelegate,
			HdDirtyBits *dirtyBits,
			HdBasisCurvesReprDesc const &desc);

    BRAY::ObjectPtr	    myInstance;
    BRAY::ObjectPtr	    myMesh;
    UT_Array<GfMatrix4d>    myXform;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
