/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
 *
 * NAME:	BRAY_HdLight.h (RAY Library, C++)
 *
 * COMMENTS:
 */

#ifndef __BRAY_HdLight__
#define __BRAY_HdLight__

#include <pxr/pxr.h>
#include <pxr/imaging/hd/light.h>
#include <pxr/imaging/hd/enums.h>
#include <pxr/base/gf/matrix4f.h>
#include <BRAY/BRAY_Interface.h>

PXR_NAMESPACE_OPEN_SCOPE

class BRAY_HdLight : public HdLight
{
public:
    BRAY_HdLight(const TfToken& typeId,
		const SdfPath &id);
    virtual ~BRAY_HdLight();

    virtual void	Finalize(HdRenderParam *renderParam) override final;
    virtual void	Sync(HdSceneDelegate *sceneDelegate,
				HdRenderParam *renderParam,
				HdDirtyBits *dirtyBits) override final;
    virtual HdDirtyBits	GetInitialDirtyBitsMask() const override final;

    BRAY::LightPtr &GetLightPtr() { return myLight; }
    const BRAY::LightPtr &GetLightPtr() const { return myLight; }

    const SdfPath &GetAreaLightGeometryPath() const 
				{ return myAreaLightGeometryPath; }

private:
    TfToken		myLightType;
    BRAY::LightPtr	myLight;
    SdfPath		myAreaLightGeometryPath;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif

