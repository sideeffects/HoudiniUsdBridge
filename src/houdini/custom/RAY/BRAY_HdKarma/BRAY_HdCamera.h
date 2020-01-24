/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
 *
 * NAME:	BRAY_HdCamera.h (RAY Library, C++)
 *
 * COMMENTS:
 */

#ifndef __BRAY_HdCamera__
#define __BRAY_HdCamera__

#include <pxr/pxr.h>
#include <pxr/imaging/hd/camera.h>
#include <pxr/imaging/hd/enums.h>
#include <pxr/base/gf/matrix4f.h>
#include <BRAY/BRAY_Interface.h>

PXR_NAMESPACE_OPEN_SCOPE

class BRAY_HdCamera : public HdCamera
{
public:
    BRAY_HdCamera(const SdfPath &id);
    virtual ~BRAY_HdCamera();

    virtual void	Finalize(HdRenderParam *renderParam) override final;
    virtual void	Sync(HdSceneDelegate *sceneDelegate,
				HdRenderParam *renderParam,
				HdDirtyBits *dirtyBits) override final;
    virtual HdDirtyBits	GetInitialDirtyBitsMask() const override final;

private:
    BRAY::CameraPtr	myCamera;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif

