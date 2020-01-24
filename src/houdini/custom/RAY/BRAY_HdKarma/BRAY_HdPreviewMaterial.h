/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
 *
 * NAME:	BRAY_HdPreviewMaterial.h
 *
 * COMMENTS: Contains the source code for the class responsible for
 *	     handling USD Preview Material.
 */
#ifndef __BRAY_HdPreviewMaterial_H__
#define __BRAY_HdPreviewMaterial_H__

#include <UT/UT_StringHolder.h>
#include <pxr/imaging/hd/material.h>

PXR_NAMESPACE_OPEN_SCOPE

class HdSceneDelegate;

class BRAY_HdPreviewMaterial
{
public:
    // Enumerated types used for shader conversions
    enum ShaderType
    {
	SURFACE,
	DISPLACE
    };

    /// Convert a preview material to VEX code.  This may return an empty
    /// string if there's no preview material or if there are errors when
    /// converting.
    static UT_StringHolder	 convert(const HdMaterialNetwork &network,
					ShaderType type);
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
