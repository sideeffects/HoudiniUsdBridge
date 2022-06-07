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
    enum ShaderType
    {
        SURFACE,
        DISPLACE,
        LIGHT,
        LIGHT_FILTER,
    };
    BRAY_HdMaterial(const SdfPath &id);
    ~BRAY_HdMaterial() override;

    void        Finalize(HdRenderParam *rparm) override;

    static const char   *shaderType(ShaderType type)
    {
        switch (type)
        {
            case SURFACE:       return "surface";
            case DISPLACE:      return "displace";
            case LIGHT:         return "light";
            case LIGHT_FILTER:  return "light_filter";
        }
        return "unknown";
    }

    void	Sync(HdSceneDelegate *sceneDelegate,
			HdRenderParam *renderParam,
			HdDirtyBits *dirtyBits) override final;
    HdDirtyBits	GetInitialDirtyBitsMask() const override final;

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
