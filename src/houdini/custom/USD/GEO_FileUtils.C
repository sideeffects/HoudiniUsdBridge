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

#include "GEO_FileUtils.h"

#include "pxr/base/tf/staticTokens.h"

PXR_NAMESPACE_OPEN_SCOPE

ARCH_PRAGMA_PUSH
ARCH_PRAGMA_MACRO_TOO_FEW_ARGUMENTS
TF_DEFINE_PRIVATE_TOKENS(
    theTokens,

    // usdconfigotherprims tokens
    (define)
    (overlay)
    (xform)

    // usdconfigpackedprims tokens
    (xforms)
    (pointinstancer)
    (nativeinstances)
    (unpack)
);
ARCH_PRAGMA_POP

void
GEOconvertTokenToEnum(const TfToken &str, GEO_HandlePackedPrims &value)
{
    if (str == theTokens->xforms)
        value = GEO_PACKED_XFORMS;
    else if (str == theTokens->pointinstancer)
        value = GEO_PACKED_POINTINSTANCER;
    else if (str == theTokens->nativeinstances)
        value = GEO_PACKED_NATIVEINSTANCES;
    else if (str == theTokens->unpack)
        value = GEO_PACKED_UNPACK;
}

void
GEOconvertTokenToEnum(const TfToken &str, GEO_HandleOtherPrims &value)
{
    if (str == theTokens->define)
        value = GEO_OTHER_DEFINE;
    else if (str == theTokens->overlay)
        value = GEO_OTHER_OVERLAY;
    else if (str == theTokens->xform)
        value = GEO_OTHER_XFORM;
}

PXR_NAMESPACE_CLOSE_SCOPE
