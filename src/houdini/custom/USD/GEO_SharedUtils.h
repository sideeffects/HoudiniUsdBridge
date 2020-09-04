/*
 * Copyright 2020 Side Effects Software Inc.
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

#ifndef __GEO_SHARED_UTILS_H__
#define __GEO_SHARED_UTILS_H__

#include "GEO_HAPIPart.h"
#include <GT/GT_Primitive.h>
#include <UT/UT_ArrayStringSet.h>
#include <UT/UT_Matrix4.h>
#include <pxr/pxr.h>

PXR_NAMESPACE_OPEN_SCOPE

class GEO_ImportOptions;

UT_Matrix4D GEOcomputeStandardPointXform(
    const GEO_HAPIPart &geo,
    UT_ArrayStringSet &processed_attribs);

UT_Matrix4D GEOcomputeStandardPointXform(
        const GT_Primitive &geo,
        const GEO_ImportOptions &options,
        UT_ArrayStringSet &processed_attribs);

PXR_NAMESPACE_CLOSE_SCOPE

#endif //__GEO_SHARED_UTILS_H__