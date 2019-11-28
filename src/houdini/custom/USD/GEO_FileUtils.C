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

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PUBLIC_TOKENS(GEO_HandleOtherPrimsTokens,
                        GEO_HANDLE_OTHER_PRIMS_TOKENS);

void
GEOconvertTokenToEnum(const TfToken &str_value, GEO_HandleOtherPrims &value)
{
    if (str_value == GEO_HandleOtherPrimsTokens->define)
        value = GEO_OTHER_DEFINE;
    else if (str_value == GEO_HandleOtherPrimsTokens->overlay)
        value = GEO_OTHER_OVERLAY;
    else if (str_value == GEO_HandleOtherPrimsTokens->xform)
    {
        value = GEO_OTHER_XFORM;
    }
}

PXR_NAMESPACE_CLOSE_SCOPE
