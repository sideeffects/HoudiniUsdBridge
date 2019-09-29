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
 * NAME:	XUSD_HydraLight.h (HUSD Library, C++)
 *
 * COMMENTS:	A hydra light prim (HdSprim)
 */
#ifndef XUSD_HydraLight_h
#define XUSD_HydraLight_h

#include <pxr/pxr.h>
#include <pxr/imaging/hd/light.h>
#include <UT/UT_StringHolder.h>

class HUSD_HydraLight;

PXR_NAMESPACE_OPEN_SCOPE
class UsdTimeCode;

class XUSD_HydraLight : public HdLight
{
public:
	     XUSD_HydraLight(TfToken const& typeId,
			     SdfPath const& primId,
			     HUSD_HydraLight &light);
    virtual ~XUSD_HydraLight();
    
    virtual void Sync(HdSceneDelegate *sceneDelegate,
                      HdRenderParam *renderParam,
                      HdDirtyBits *dirtyBits) override;

protected:
    virtual HdDirtyBits GetInitialDirtyBitsMask() const override;

private:
    HUSD_HydraLight &myLight;
    bool	     myDirtyFlag;
};

PXR_NAMESPACE_CLOSE_SCOPE
#endif
