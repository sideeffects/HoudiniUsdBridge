/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
 *
 * Produced by:
 *	Side Effects Software Inc
 *	123 Front Street West, Suite 1401
 *	Toronto, Ontario
 *	Canada   M5J 2M2
 *	416-504-9876
 *
 * NAME:	XUSD_HydraCamera.h (HUSD Library, C++)
 *
 * COMMENTS:	Container for a hydra scene prim (HdRprim), light or camera
 */
#ifndef XUSD_HydraCamera_h
#define XUSD_HydraCamera_h

#include <pxr/pxr.h>
#include <pxr/imaging/hd/camera.h>
#include <SYS/SYS_Types.h>
#include <GT/GT_Handles.h>

class HUSD_HydraCamera;

PXR_NAMESPACE_OPEN_SCOPE

/// Container for a hydra scene prim (HdSprim) representing a camera
class XUSD_HydraCamera : public HdCamera
{
public:
	     XUSD_HydraCamera(TfToken const& typeId,
			      SdfPath const& primId,
			      HUSD_HydraCamera &cam);
    virtual ~XUSD_HydraCamera();

    virtual void Sync(HdSceneDelegate *sceneDelegate,
                      HdRenderParam *renderParam,
                      HdDirtyBits *dirtyBits) override;
    
protected:
    virtual HdDirtyBits GetInitialDirtyBitsMask() const override;
   
private:
    HUSD_HydraCamera	&myCamera;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
