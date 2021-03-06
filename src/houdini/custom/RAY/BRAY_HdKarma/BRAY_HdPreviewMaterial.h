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

#ifndef __BRAY_HdPreviewMaterial_H__
#define __BRAY_HdPreviewMaterial_H__

#include <UT/UT_StringHolder.h>
#include <pxr/imaging/hd/material.h>

namespace BRAY
{
class ShaderGraphPtr;
}

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

    /// Convert a preview material to BRAY shader graph.
    /// Returns true if successful.
    static bool convert(BRAY::ShaderGraphPtr &shadergraph,
			const HdMaterialNetwork &network,
			ShaderType type);
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
