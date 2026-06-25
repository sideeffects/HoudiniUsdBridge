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

#include "GEO_FilePrim.h"

#include <GT/GT_AttributeList.h>
#include <GT/GT_PrimVDB.h>
#include <GT/GT_PrimVolume.h>
#include <gusd/USD_Utils.h>

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

static GT_AttributeListHandle
geoCopyMaterialAttribs(
        const GT_AttributeListHandle &src_list,
        const UT_StringArray &attrib_names)
{
    if (!src_list)
        return nullptr;

    GT_AttributeMapHandle dst_map;
    for (exint i = 0, n = attrib_names.entries(); i < n; ++i)
    {
        const exint src_idx = src_list->getIndex(attrib_names[i]);
        if (src_idx < 0)
            continue;

        if (!dst_map)
            dst_map = UTmakeIntrusive<GT_AttributeMap>(attrib_names.entries());

        dst_map->add(src_list->getName(src_idx), false);
    }

    if (!dst_map)
        return nullptr;

    const int nsegs = src_list->getSegments();
    auto dst_list = UTmakeIntrusive<GT_AttributeList>(dst_map, nsegs);
    for (exint i = 0, n = dst_map->entries(); i < n; ++i)
    {
        exint src_idx = src_list->getIndex(dst_map->getName(i));
        UT_ASSERT(src_idx >= 0);

        for (int seg = 0; seg < nsegs; ++seg)
            dst_list->set(i, src_list->get(src_idx, seg), seg);
    }

    return dst_list;
}

void
GT_PrimVolumeCollection::addField(
        const GEO_PathHandle &path,
        const UT_StringHolder &name,
        const GT_PrimitiveHandle &prim)
{
    static const UT_StringArray theMaterialAttribNames = UT_StringArray
    {
        GusdUSD_Utils::TokenToStringHolder(
                GEO_FilePrimTokens->usdmaterialpath),
        GusdUSD_Utils::TokenToStringHolder(
                GEO_FilePrimTokens->usdmaterialreffile),
        GusdUSD_Utils::TokenToStringHolder(
                GEO_FilePrimTokens->usdmaterialrefprim)
    };

    // Promote the usdmaterial* attributes from the first field (these attribs
    // are expected to be constant for the fields) so that the material
    // binding occurs on the Volume prim.
    if (myFieldPrims.isEmpty())
    {
        myUniformAttribs = geoCopyMaterialAttribs(
                prim->getUniformAttributes(), theMaterialAttribNames);
    }

    // Remove those attributes from the field prims since they don't need
    // material bindings.
    if (prim->getPrimitiveType() == GT_PRIM_VOXEL_VOLUME)
    {
        auto field = UTverify_cast<GT_PrimVolume *>(prim.get());
        if (auto uniform = field->getUniformAttributes())
        {
            field->setUniformAttributes(
                    uniform->removeAttributes(theMaterialAttribNames));
        }
    }
    else if (prim->getPrimitiveType() == GT_PRIM_VDB_VOLUME)
    {
        auto field = UTverify_cast<GT_PrimVDB *>(prim.get());
        if (auto uniform = field->getUniformAttributes())
        {
            field->setUniformAttributes(
                    uniform->removeAttributes(theMaterialAttribNames));
        }
    }
    else
    {
        UT_ASSERT_MSG(false, "Unexpected prim type added to volume");
    }

    myFieldPaths.append(path);
    myFieldNames.insert(name);
    myFieldPrims.append(prim);
}


PXR_NAMESPACE_CLOSE_SCOPE
