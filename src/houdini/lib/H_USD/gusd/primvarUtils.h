/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
*/

#ifndef GUSD_PRIMVARUTILS_H
#define GUSD_PRIMVARUTILS_H

#include "api.h"

#include <GT/GT_Handles.h>
#include <GT/GT_Types.h>
#include <UT/UT_Optional.h>
#include <UT/UT_StringHolder.h>

#include <pxr/base/vt/value.h>
#include <pxr/pxr.h>
#include <pxr/usd/sdf/path.h>

PXR_NAMESPACE_OPEN_SCOPE

/// Contains the primvar's name, value, and related metadata. This can be
/// populated from various sources (e.g. USD or Hydra prims) to share common
/// code for converting primvars to GT attributes.
struct GUSD_API GusdPrimvarInfo
{
    explicit operator bool() const { return !myFlattenedValue.IsEmpty(); }

    /// Path of the associated prim, used for error reporting.
    SdfPath myPrimPath;
    /// The attribute name to use in Houdini (e.g. `P` rather than `points`).
    UT_StringHolder myName;
    /// The original USD name for the primvar.
    TfToken myOrigName;
    /// Flattened value of the primvar.
    VtValue myFlattenedValue;
    /// Records whether the primvar was an indexed primvar.
    bool myIsIndexed = false;
    /// The primvar's element size.
    int myElementSize = 1;
    /// The GT equivalent of the primvar's interpolation mode.
    GT_Owner myOwner = GT_OWNER_INVALID;
    /// The GT equivalent of the primvar's role, e.g. normal or color.
    GT_Type myTypeInfo = GT_TYPE_NONE;
    /// Scale to apply when translating the values to Houdini. This can be used
    /// for converting diameter to radius, radians to degrees, etc.
    UT_Optional<fpreal> myValueScale;
};

/// Converts the primvar to a GT_DataArray. The caller is responsible for
/// choosing how to insert it into the appropriate GT_AttributeList that it
/// manages.
GUSD_API
GT_DataArrayHandle
GusdConvertPrimvarData(const GusdPrimvarInfo &primvar);

/// Index-pair attributes are a special case, which are represented in USD with
/// separate primvars for the indices and weights.
GUSD_API
GT_DataArrayHandle
GusdConvertPrimvarsToIndexPairData(
        const GusdPrimvarInfo &indices_primvar,
        const GusdPrimvarInfo &weights_primvar,
        const VtTokenArray &joint_names);

/// SOP array attributes (arrays of arrays) are represented in USD with a
/// constant primvar containing the concatenated values for all of the arrays,
/// and a separate ':lengths' primvar containing the length of each array.
/// The interpolation of the ':lengths' primvar determines the interpolation
/// of the array attribute.
/// Returns the combined array of arrays, or nullptr if e.g. the lengths are
/// invalid.
GUSD_API
GT_DataArrayHandle
GusdConvertPrimvarsToArrayData(
        const GusdPrimvarInfo &values_primvar,
        const GusdPrimvarInfo &lengths_primvar);

/// Returns whether the primvar holds the array lengths for an array attribute.
GUSD_API
bool
GusdIsArrayLengthsPrimvar(const TfToken &primvar_name);

/// Returns the name of the primvar holding the array lengths for an array
/// attribute.
GUSD_API
TfToken
GusdGetArrayLengthsPrimvarName(const TfToken &values_primvar_name);

PXR_NAMESPACE_CLOSE_SCOPE

#endif
