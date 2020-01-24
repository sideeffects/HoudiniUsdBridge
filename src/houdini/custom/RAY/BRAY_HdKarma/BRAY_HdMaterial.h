/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
 *
 * NAME:	BRAY_HdMaterial.h (RAY Library, C++)
 *
 * COMMENTS:
 */

#ifndef __BRAY_HdMaterial__
#define __BRAY_HdMaterial__

#include <pxr/pxr.h>
#include <pxr/imaging/hd/material.h>
#include <pxr/imaging/hd/enums.h>
#include <pxr/base/gf/matrix4f.h>

#include <UT/UT_StringArray.h>

class UT_JSONWriter;

PXR_NAMESPACE_OPEN_SCOPE

class BRAY_HdMaterial : public HdMaterial
{
public:
    BRAY_HdMaterial(const SdfPath &id);
    virtual ~BRAY_HdMaterial();

    virtual void	Reload() override final;
    virtual void	Sync(HdSceneDelegate *sceneDelegate,
				HdRenderParam *renderParam,
				HdDirtyBits *dirtyBits) override final;
    virtual HdDirtyBits	GetInitialDirtyBitsMask() const override final;

    /// @{
    /// Methods to help with debugging networks
    static void	dump(const HdMaterialNetwork &network);
    static void	dump(UT_JSONWriter &w, const HdMaterialNetwork &network);
    static void	dump(const HdMaterialNetworkMap &netmap);
    static void	dump(UT_JSONWriter &w, const HdMaterialNetworkMap &netmap);
    /// @}

private:
    void	setShaders(HdSceneDelegate *delegate);
    void	setParameters(HdSceneDelegate *delegate);

    UT_StringHolder	mySurfaceSource;
    UT_StringHolder	myDisplaceSource;
    UT_StringArray	mySurfaceParms;
    UT_StringArray	myDisplaceParms;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
