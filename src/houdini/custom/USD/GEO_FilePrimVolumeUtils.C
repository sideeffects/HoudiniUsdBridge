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
 */

#include "GEO_FilePrimVolumeUtils.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PUBLIC_TOKENS(GEO_VolumePrimTokens, GEO_VOLUME_PRIM_TOKENS);

int
GT_PrimVolumeCollection::getStaticPrimitiveType()
{
    static const int thePrimitiveType = GT_Primitive::createPrimitiveTypeId();
    return thePrimitiveType;
}

void
GT_PrimVolumeCollection::enlargeBounds(
        UT_BoundingBox boxes[], int nsegments) const
{
    for (const GT_PrimitiveHandle &field : myFieldPrims)
        field->enlargeBounds(boxes, nsegments);
}

PXR_NAMESPACE_CLOSE_SCOPE
