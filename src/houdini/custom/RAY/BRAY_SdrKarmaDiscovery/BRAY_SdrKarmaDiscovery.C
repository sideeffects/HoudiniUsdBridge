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
 */

#include "BRAY_SdrKarmaDiscovery.h"
#include <BRAY/BRAY_Interface.h>
#include <VCC/VCC_Utils.h>
#include <VEX/VEX_Types.h>
#include <HUSD/XUSD_Format.h>
#include <UT/UT_Debug.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/gf/matrix2d.h>
#include <pxr/base/gf/matrix3d.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/vt/array.h>
#include <pxr/usd/sdf/types.h>
#include <pxr/usd/ndr/debugCodes.h>
#include <pxr/usd/ndr/nodeDiscoveryResult.h>
#include <pxr/usd/sdr/shaderNode.h>
#include <pxr/usd/sdr/shaderProperty.h>

namespace
{
    using NodeDecl = BRAY::ShaderGraphPtr::NodeDecl;
}

PXR_NAMESPACE_OPEN_SCOPE

NDR_REGISTER_DISCOVERY_PLUGIN(BRAY_SdrKarmaDiscovery)

TF_DEFINE_PRIVATE_TOKENS(
    theTokens,
    ((karmaToken, "karma"))        // Built-in Karma shader node
);

BRAY_SdrKarmaDiscovery::BRAY_SdrKarmaDiscovery()
{
    TF_DEBUG(NDR_DISCOVERY).Msg("SdrKarmaDiscovery c-tor");
}

BRAY_SdrKarmaDiscovery::~BRAY_SdrKarmaDiscovery()
{
}

static void
makeShaderNode(NdrNodeDiscoveryResultVec &nodes, const NodeDecl &node)
{
    static const std::string    uri("karma");   // Token for built-in node
    TfToken      family;        // Empty token

    std::string name = node.name().toStdString();
    nodes.emplace_back(
            NdrIdentifier(name),
            NdrVersion().GetAsDefault(),
            name,
            family,
            theTokens->karmaToken,      // discovery type
            theTokens->karmaToken,      // source type
            uri,
            uri                         // Identify as a built-in node
    );

}

NdrNodeDiscoveryResultVec
BRAY_SdrKarmaDiscovery::DiscoverNodes(const Context &)
{
    NdrNodeDiscoveryResultVec   result;

    const UT_Array<const NodeDecl *>    &nodes = BRAY::ShaderGraphPtr::allNodes();
    for (auto &&n : nodes)
    {
        makeShaderNode(result, *n);
        TF_DEBUG(NDR_DISCOVERY).Msg("SdrKarmaDiscovery: %s", n->name().c_str());
#if 0
        const NdrNodeDiscoveryResult &d = result[result.size()-1];
        UTdebugFormat("Define:");
        UTdebugFormat("   id  {}", d.identifier);
        UTdebugFormat("   ver {}", d.version.GetString());
        UTdebugFormat("   nm  {}", d.name);
        UTdebugFormat("   fam {}", d.family);
        UTdebugFormat("   dty {}", d.discoveryType);
        UTdebugFormat("   sty {}", d.sourceType);
        UTdebugFormat("   uri {}", d.uri);
        UTdebugFormat("   Uri {}", d.resolvedUri);
#endif
    }
    return result;
}

const NdrStringVec &
BRAY_SdrKarmaDiscovery::GetSearchURIs() const
{
    static NdrStringVec theURIs;
    return theURIs;
}

PXR_NAMESPACE_CLOSE_SCOPE
