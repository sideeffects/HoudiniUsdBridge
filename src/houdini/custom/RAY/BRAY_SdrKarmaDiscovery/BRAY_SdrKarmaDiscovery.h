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

#ifndef __BRAY_SDRKARMADISCOVERY_H__
#define __BRAY_SDRKARMADISCOVERY_H__

#include "pxr/pxr.h"
#include "pxr/usd/ndr/discoveryPlugin.h"

PXR_NAMESPACE_OPEN_SCOPE

// Forward declarations

class BRAY_SdrKarmaDiscovery : public NdrDiscoveryPlugin
{
public:
    BRAY_SdrKarmaDiscovery();
    ~BRAY_SdrKarmaDiscovery() override;

    NdrNodeDiscoveryResultVec    DiscoverNodes(const Context &) override;
    const NdrStringVec          &GetSearchURIs() const override;

private:
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
