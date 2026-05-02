/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
*/

#include "attribListsBuilder.h"

#include "primvarUtils.h"

#include <GT/GT_AttributeList.h>
#include <GT/GT_DAIndexedString.h>
#include <GT/GT_DAIndirect.h>
#include <GT/GT_DANumeric.h>
#include <UT/UT_Assert.h>
#include <UT/UT_WorkBuffer.h>

#include <pxr/usd/sdf/types.h>

#include <algorithm>

PXR_NAMESPACE_OPEN_SCOPE

static GT_DataArrayHandle
gusdCreateConstantIndirect(exint n, const GT_DataArrayHandle &constant_data)
{
    auto indirect = UTmakeIntrusive<GT_DANumeric<exint>>(n, 1);
    std::fill(indirect->data(), indirect->data() + n, 0);

    return UTmakeIntrusive<GT_DAIndirect>(indirect, constant_data);
}

static GT_DataArrayHandle
gusdBuildAttribPattern(const UT_StringArray &attrib_list)
{
    if (attrib_list.isEmpty())
        return nullptr;

    UT_WorkBuffer pattern;
    pattern.append(attrib_list, " ");

    auto gt_pattern = UTmakeIntrusive<GT_DAIndexedString>(1);
    gt_pattern->setString(0, 0, std::move(pattern));
    return gt_pattern;
}

void
GusdAttribListsBuilder::enablePointAttribs(exint num_points)
{
    myPointAttribs.enable(num_points);
}

void
GusdAttribListsBuilder::enableVertexAttribs(exint num_vertices)
{
    myVertexAttribs.enable(num_vertices);
}

void
GusdAttribListsBuilder::enableUniformAttribs(exint num_uniform)
{
    myUniformAttribs.enable(num_uniform);
}

void
GusdAttribListsBuilder::enableDetailAttribs()
{
    myDetailAttribs.enable(/*min_entries=*/1);
}

bool
GusdAttribListsBuilder::add(
        const GusdPrimvarInfo &info,
        const GT_DataArrayHandle &data)
{
    UT_ASSERT(data != nullptr);
    if (!data)
        return false;

    // TODO - support an indirect array for expanding per-segment primvars
    // to point attribs.
    if (info.myOwner == GT_OWNER_POINT)
    {
        if (!myPointAttribs.add(info, data))
            return false;
    }
    else if (info.myOwner == GT_OWNER_VERTEX)
    {
        if (!myVertexAttribs.add(info, data))
            return false;
    }
    else if (info.myOwner == GT_OWNER_PRIMITIVE)
    {
        if (!myUniformAttribs.add(info, data))
            return false;
    }
    else if (info.myOwner == GT_OWNER_DETAIL)
    {
        // Promote down to a prim / point attribute if possible.
        // GU_MergeUtils might do this anyways, so it's better to have it
        // happen consistently so that attributes don't move around
        // unexpectedly. We record these attributes in
        // usdconfigconstantattribs to round-trip them back to constant USD
        // primvars.
        if (myUniformAttribs.isEnabled())
        {
            GT_DataArrayHandle indirect = gusdCreateConstantIndirect(
                    myUniformAttribs.getMinEntries(), data);
            if (!myUniformAttribs.add(info, indirect))
                return false;
        }
        else if (myPointAttribs.isEnabled())
        {
            GT_DataArrayHandle indirect = gusdCreateConstantIndirect(
                    myPointAttribs.getMinEntries(), data);
            if (!myPointAttribs.add(info, indirect))
                return false;
        }
        else
        {
            if (!myDetailAttribs.add(info, data))
                return false;
        }

        if (myUniformAttribs.isEnabled() || myPointAttribs.isEnabled())
        {
            if (info.myFlattenedValue.IsArrayValued())
                myConstantAttribs.append(info.myName);
            else
                myScalarConstantAttribs.append(info.myName);
        }
    }
    else
    {
        UT_ASSERT_MSG(false, "Unexpected GT_Owner value");
        return false;
    }

    // Record the attribute in the appropriate usdconfig* attrib lists.
    const SdfValueTypeName scalar_type =
            SdfGetValueTypeNameForValue(info.myFlattenedValue).GetScalarType();
    if (scalar_type == SdfValueTypeNames->Bool)
        myBoolAttribs.append(info.myName);
    else if (scalar_type == SdfValueTypeNames->UInt)
        myUIntAttribs.append(info.myName);
    else if (scalar_type == SdfValueTypeNames->UInt64)
        myUInt64Attribs.append(info.myName);
    else if (scalar_type == SdfValueTypeNames->Asset)
        myAssetPathAttribs.append(info.myName);

    if (info.myIsIndexed)
        myIndexedAttribs.append(info.myName);

    return true;
}

GT_AttributeListHandle
GusdAttribListsBuilder::buildPointAttribs() const
{
    return myPointAttribs.makeList();
}

GT_AttributeListHandle
GusdAttribListsBuilder::buildVertexAttribs() const
{
    return myVertexAttribs.makeList();
}

GT_AttributeListHandle
GusdAttribListsBuilder::buildUniformAttribs() const
{
    return myUniformAttribs.makeList();
}

GT_AttributeListHandle
GusdAttribListsBuilder::buildDetailAttribs()
{
    if (!myDetailAttribs.isEnabled())
        return nullptr;

    // Add the usdconfig* attributes before building the final GT attribute
    // list.
    auto add_config_attrib = [this](const UT_StringArray &attrib_list,
                                    const UT_StringHolder &config_attrib_name)
    {
        if (attrib_list.isEmpty())
            return;

        GT_DataArrayHandle config_attrib_data
                = gusdBuildAttribPattern(attrib_list);

        GusdPrimvarInfo config_attrib_info;
        config_attrib_info.myName = config_attrib_name;
        config_attrib_info.myOwner = GT_OWNER_DETAIL;

        UT_VERIFY(myDetailAttribs.add(config_attrib_info, config_attrib_data));
    };

    add_config_attrib(myConstantAttribs, "usdconfigconstantattribs"_UTsh);
    add_config_attrib(
            myScalarConstantAttribs, "usdconfigscalarconstantattribs"_UTsh);
    add_config_attrib(myBoolAttribs, "usdconfigboolattribs"_UTsh);
    add_config_attrib(myUIntAttribs, "usdconfiguintattribs"_UTsh);
    add_config_attrib(myUInt64Attribs, "usdconfiguint64attribs"_UTsh);
    add_config_attrib(myAssetPathAttribs, "usdconfigassetpathattribs"_UTsh);
    add_config_attrib(myIndexedAttribs, "usdconfigindexattribs"_UTsh);

    return myDetailAttribs.makeList();
}

void
GusdAttribListsBuilder::ListBuilder::enable(exint min_entries)
{
    UT_ASSERT(min_entries >= 1);

    myEnabled = true;
    myMinEntries = min_entries;
}

bool
GusdAttribListsBuilder::ListBuilder::add(
        const GusdPrimvarInfo &info,
        const GT_DataArrayHandle &data)
{
    UT_ASSERT(data != nullptr);

    if (!myEnabled)
        return false;

    if (data->entries() < myMinEntries)
    {
        TF_WARN("<%s>: not enough values for attribute %s (%zd, expected %zd)",
                info.myPrimPath.GetText(), info.myOrigName.GetText(),
                size_t(data->entries()), size_t(myMinEntries));
        return false;
    }

    if (!myMap)
        myMap = UTmakeIntrusive<GT_AttributeMap>();

    myMap->add(info.myName, /*replace_existing=*/true);
    myAttribs.append(data);
    return true;
}

GT_AttributeListHandle
GusdAttribListsBuilder::ListBuilder::makeList() const
{
    const exint num_attribs = myAttribs.size();
    if (num_attribs == 0)
        return nullptr;

    UT_ASSERT(myMap->entries() == num_attribs);

    auto list = UTmakeIntrusive<GT_AttributeList>(myMap, 1);
    for (exint i = 0; i < num_attribs; ++i)
        list->set(i, myAttribs[i]);

    return list;
}

PXR_NAMESPACE_CLOSE_SCOPE
