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

#ifndef __BRAY_HdMaterialNetwork_H__
#define __BRAY_HdMaterialNetwork_H__

#include <initializer_list>
#include <UT/UT_StringHolder.h>
#include <UT/UT_StringMap.h>
#include <pxr/imaging/hd/material.h>
#include "BRAY_HdMaterial.h"

class UT_JSONWriter;

namespace BRAY
{
    class ShaderGraphPtr;
}

PXR_NAMESPACE_OPEN_SCOPE

class HdSceneDelegate;

class BRAY_HdMaterialNetwork
{
public:
    using ParmNameMap = UT_Map<TfToken, UT_StringHolder>;

    // In some cases, we need to map TfTokens to a different name (for example,
    // "texture:file" -> "textureFile").  In addition, some tokens are prefixed
    // with "inputs:" which needs to be stripped off when translating to a
    // Karma shader node parameter name.
    class usdTokenAlias
    {
    public:
        // Strip off "inputs:" for the alias
        usdTokenAlias(const TfToken &token);

        // Explicitly create an alias.  This holds an unsafe reference to the
        // string.
        usdTokenAlias(const TfToken &token, const char *str);

        // Explicitly create a alias by creating the token from the string
        // NB: This holds and unsafe reference to the string
        usdTokenAlias(const char *str);

        const TfToken           &token() const { return myToken; }
        const TfToken           &baseToken() const { return myBaseToken; }
        const UT_StringHolder   &alias() const { return myAlias; }

    private:
        TfToken         myToken;
        TfToken         myBaseToken;
        UT_StringHolder myAlias;
    };
    class ParmNameMapCreator
    {
    public:
        ParmNameMapCreator(const std::initializer_list<usdTokenAlias> &parms)
        {
            for (const auto &it : parms)
                myMap.insert({it.baseToken(), it.alias()});
        }
        const ParmNameMap       &map() const { return myMap; }
        ParmNameMap     myMap;
    };

    // Enumerated types used for shader conversions
    /// Convert a preview material to BRAY shader graph.
    /// Returns true if successful.
    static bool convert(BRAY::ShaderGraphPtr &shadergraph,
			const HdMaterialNetwork &network,
			BRAY_HdMaterial::ShaderType type,
                        const ParmNameMap *parm_name_map=nullptr);

    /// @{
    /// Help debugging a material network
    static void dump(const HdMaterialNetwork2 &mat);
    static void dump(UT_JSONWriter &w, const HdMaterialNetwork2 &mat);
    /// @}
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
