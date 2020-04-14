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

#include "BRAY_SdrKarma.h"
#include <pxr/usd/ndr/debugCodes.h>
#include <pxr/usd/ndr/nodeDiscoveryResult.h>
#include <pxr/usd/sdr/shaderNode.h>
#include <pxr/usd/sdr/shaderProperty.h>

PXR_NAMESPACE_OPEN_SCOPE

NDR_REGISTER_PARSER_PLUGIN(BRAY_SdrKarma)

TF_DEFINE_PRIVATE_TOKENS(
    theTokens,

    ((discoveryTypeVex, "vex"))
    ((discoveryTypeVfl, "vfl"))
    ((sourceType, "VEX"))
);

const NdrTokenVec&
BRAY_SdrKarma::GetDiscoveryTypes() const
{
    static const NdrTokenVec _DiscoveryTypes = {
	theTokens->discoveryTypeVex,
	theTokens->discoveryTypeVfl,
    };
    return _DiscoveryTypes;
}

const TfToken&
BRAY_SdrKarma::GetSourceType() const
{
    return theTokens->sourceType;
}

BRAY_SdrKarma::BRAY_SdrKarma()
{
}

BRAY_SdrKarma::~BRAY_SdrKarma()
{
}

NdrNodeUniquePtr
BRAY_SdrKarma::Parse(const NdrNodeDiscoveryResult& discoveryResult)
{
    return NdrNodeUniquePtr(
        new SdrShaderNode(
            discoveryResult.identifier,
            discoveryResult.version,
            discoveryResult.name,
            discoveryResult.family,
            theTokens->sourceType,
            theTokens->sourceType,
            discoveryResult.uri,
            discoveryResult.resolvedUri,
            NdrPropertyUniquePtrVec(),
            NdrTokenMap(),
            discoveryResult.sourceCode
        )
    );
}

PXR_NAMESPACE_CLOSE_SCOPE
