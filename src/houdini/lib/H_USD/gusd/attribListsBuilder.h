/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
*/

#ifndef GUSD_ATTRIBLISTSBUILDER_H
#define GUSD_ATTRIBLISTSBUILDER_H

#include "api.h"

#include <GT/GT_Handles.h>
#include <GT/GT_Types.h>
#include <UT/UT_SmallArray.h>
#include <UT/UT_StringArray.h>
#include <UT/UT_StringHolder.h>

#include <pxr/pxr.h>

PXR_NAMESPACE_OPEN_SCOPE

struct GusdPrimvarInfo;

/// Utility for efficiently building the GT_AttributeList for each owner type.
class GUSD_API GusdAttribListsBuilder
{
public:
    /// Enable collecting point attributes and configure the required number of
    /// entries for the arrays.
    void enablePointAttribs(exint num_points);

    /// Enable collecting vertex attributes and configure the required number of
    /// entries for the arrays.
    void enableVertexAttribs(exint num_vertices);

    /// Enable collecting uniform attributes and configure the required number
    /// of entries for the arrays.
    void enableUniformAttribs(exint num_uniform);

    /// Enable collecting constant / detail attributes.
    void enableDetailAttribs();

    /// Add an attribute to the appropriate owner's list.
    /// (Note, for example, that constant attribs may be promoted down to prim
    /// or point attributes for merging multiple prims).
    /// This may fail if, for example, the data array doesn't match the
    /// required number of entries, or if the owner type is not enabled.
    bool add(const GusdPrimvarInfo &info, const GT_DataArrayHandle &data);

    /// Build the GT_AttributeList for point attributes. This may return nullptr
    /// if there aren't any attributes.
    GT_AttributeListHandle buildPointAttribs() const;

    /// Build the GT_AttributeList for vertex attributes. This may return
    /// nullptr if there aren't any attributes.
    GT_AttributeListHandle buildVertexAttribs() const;

    /// Build the GT_AttributeList for uniform attributes. This may return
    /// nullptr if there aren't any attributes.
    GT_AttributeListHandle buildUniformAttribs() const;

    /// Build the GT_AttributeList for detail attributes. This may return
    /// nullptr if there aren't any attributes.
    GT_AttributeListHandle buildDetailAttribs();

private:
    /// Record the attribute in the appropriate usdconfig* attribute(s).
    void recordUsdConfigAttribs(const GusdPrimvarInfo &info);

    /// Builder implementation for a single GT_AttributeList.
    class GUSD_API ListBuilder
    {
    public:
        void enable(exint min_entries);

        bool isEnabled() const { return myEnabled; }
        exint getMinEntries() const { return myMinEntries; }

        bool add(const GusdPrimvarInfo &info, const GT_DataArrayHandle &data);

        GT_AttributeListHandle makeList() const;

    private:
        bool myEnabled = false;
        exint myMinEntries = 0;
        GT_AttributeMapHandle myMap;
        UT_SmallArray<GT_DataArrayHandle> myAttribs;
    };

    ListBuilder myPointAttribs;
    ListBuilder myVertexAttribs;
    ListBuilder myUniformAttribs;
    ListBuilder myDetailAttribs;

    /// @{
    /// To correctly round-trip certain attribute types back to USD, we add
    /// configuration attributes such as `usdconfigboolattribs` to the geometry
    /// to indicate how they should be handled by the SOP Import LOP.
    UT_StringArray myConstantAttribs;
    UT_StringArray myScalarConstantAttribs;
    UT_StringArray myBoolAttribs;
    UT_StringArray myUIntAttribs;
    UT_StringArray myUInt64Attribs;
    UT_StringArray myAssetPathAttribs;
    UT_StringArray myIndexedAttribs;
    /// @}
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
