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

#ifndef __BRAY_SDRKARMA_H__
#define __BRAY_SDRKARMA_H__

#include "pxr/pxr.h"
#include "pxr/base/vt/value.h"
#include "pxr/usd/ndr/parserPlugin.h"
#include "pxr/usd/sdr/declare.h"

PXR_NAMESPACE_OPEN_SCOPE

// Forward declarations
class NdrNode;
struct NdrNodeDiscoveryResult;

class BRAY_SdrKarma : public NdrParserPlugin
{
public:
    BRAY_SdrKarma();
    ~BRAY_SdrKarma();

    NdrNodeUniquePtr Parse(
        const NdrNodeDiscoveryResult& discoveryResult) override;

    const NdrTokenVec& GetDiscoveryTypes() const override;

    const TfToken& GetSourceType() const override;

private:
    NdrPropertyUniquePtrVec getNodeProperties(
	    const NdrNodeDiscoveryResult& discoveryResult);
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
