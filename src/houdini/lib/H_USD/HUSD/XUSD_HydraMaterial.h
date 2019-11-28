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
 *	Side Effects Software Inc
 *	123 Front Street West, Suite 1401
 *	Toronto, Ontario
 *	Canada   M5J 2M2
 *	416-504-9876
 *
 * NAME:	XUSD_HydraMaterial.h (HUSD Library, C++)
 *
 * COMMENTS:	Evaluator and sprim for a material
 */
#ifndef XUSD_HydraMaterial_h
#define XUSD_HydraMaterial_h

#include <pxr/pxr.h>
#include <pxr/imaging/hd/material.h>

#include <UT/UT_Pair.h>
#include <UT/UT_StringMap.h>

#include "HUSD_HydraMaterial.h"

PXR_NAMESPACE_OPEN_SCOPE

class SdfPath;

class XUSD_HydraMaterial : public HdMaterial
{
public:
	     XUSD_HydraMaterial(SdfPath const& primId,
				HUSD_HydraMaterial &mat);

    virtual void Sync(HdSceneDelegate *sceneDelegate,
                      HdRenderParam *renderParam,
                      HdDirtyBits *dirtyBits) override;
    virtual void Reload() override;
    virtual HdDirtyBits GetInitialDirtyBitsMask() const override;

    static bool isAssetMap(const UT_StringRef &filename);

private:
    void	syncPreviewMaterial(HdSceneDelegate *scene_del,
				    const SdfPath &nodepath,
				    const std::map<TfToken,VtValue> &parms);
    
    void	syncUVTexture(HUSD_HydraMaterial::map_info &info,
			      HdSceneDelegate *scene_del,
			      const SdfPath &nodepath,
			      const std::map<TfToken,VtValue> &parms);
    HUSD_HydraMaterial &myMaterial;
};

PXR_NAMESPACE_CLOSE_SCOPE
#endif
