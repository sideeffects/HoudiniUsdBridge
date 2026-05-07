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
 * Produced by:
 *	Side Effects Software Inc.
 *	123 Front Street West, Suite 1401
 *	Toronto, Ontario
 *      Canada   M5J 2M2
 *	416-504-9876
 *
 */

#ifndef __HUSD_Xform_h__
#define __HUSD_Xform_h__

#include "HUSD_API.h"
#include "HUSD_DataHandle.h"
#include "HUSD_Path.h"
#include "HUSD_TimeCode.h"
#include <UT/UT_Matrix4.h>
#include <UT/UT_StringHolder.h>
#include <UT/UT_StringMap.h>
#include <UT/UT_XformOrder.h>

class HUSD_FindPrims;
enum class HUSD_XformAxis;
enum class HUSD_XformAxisOrder;
enum class HUSD_TimeSampling;

enum HUSD_XformStyle
{
    HUSD_XFORM_APPEND = 0x00,
    HUSD_XFORM_PREPEND = 0x01,
    HUSD_XFORM_OVERWRITE = 0x02,
    HUSD_XFORM_OVERWRITE_APPEND = 0x03,
    HUSD_XFORM_OVERWRITE_PREPEND = 0x04,
    HUSD_XFORM_WORLDSPACE = 0x05,
    HUSD_XFORM_ABSOLUTE = 0x06,
    HUSD_XFORM_BASIC_COMMON_API_APPEND = 0x07,
    HUSD_XFORM_BASIC_COMMON_API_PREPEND = 0x08,
    HUSD_XFORM_BASIC_COMMON_API_OVERWRITE = 0x09,
    HUSD_XFORM_COMMON_API_APPEND = 0x0A,
    HUSD_XFORM_COMMON_API_PREPEND = 0x0B,
    HUSD_XFORM_COMMON_API_OVERWRITE = 0x0C,
    HUSD_XFORM_COMMON_API_WORLDSPACE = 0x0D
};

inline bool HUSDisBasicXformAPIStyle(HUSD_XformStyle style)
{
    return (style == HUSD_XFORM_BASIC_COMMON_API_APPEND ||
        style == HUSD_XFORM_BASIC_COMMON_API_PREPEND ||
        style == HUSD_XFORM_BASIC_COMMON_API_OVERWRITE);
}

inline bool HUSDisExtendedXformAPIStyle(HUSD_XformStyle style)
{
    return (style == HUSD_XFORM_COMMON_API_APPEND ||
        style == HUSD_XFORM_COMMON_API_PREPEND ||
        style == HUSD_XFORM_COMMON_API_OVERWRITE ||
        style == HUSD_XFORM_COMMON_API_WORLDSPACE);
}

class HUSD_API HUSD_XformEntry
{
public:
    struct HUSD_XformEntryComponents
    {
        UT_Vector3D         myT;
        UT_Vector3D         myR;
        UT_Vector3D         myS;
        UT_Vector3D         myShear;
    };

    // Default-construct: identity matrix
    HUSD_XformEntry();

    // Matrix constructor
    HUSD_XformEntry(
        const UT_Matrix4D   &matrix,
        const HUSD_TimeCode &timecode,
        bool                 write_pivot  = false,
        const UT_Vector3D   &pivot        = UT_Vector3D(0.0),
        const UT_Vector3D   &pivot_rotate = UT_Vector3D(0.0),
        const UT_XformOrder &order        = UT_XformOrder(
                                                UT_XformOrder::SRT,
                                                UT_XformOrder::XYZ)
        );

    // Component constructor
    HUSD_XformEntry(
        const HUSD_XformEntryComponents &components,
        const HUSD_TimeCode &timecode,
         bool                write_pivot  = false,
        const UT_Vector3D   &pivot        = UT_Vector3D(0.0),
        const UT_Vector3D   &pivot_rotate = UT_Vector3D(0.0),
        const UT_XformOrder &order        = UT_XformOrder(
                                                UT_XformOrder::SRT,
                                                UT_XformOrder::XYZ)
       );

    // Set the active member and the mode flag
    void                 setXformMatrix(const UT_Matrix4D &matrix);
    void                 setXformComponents(
                             const HUSD_XformEntryComponents &components);

    // Returns a matrix regardless of the underlying representation. If the
    // entry holds components, compose them with myOrder and
    // PivotSpace(myPivot, myPivotRotate).
    UT_Matrix4D          getXformMatrix() const;

    // Returns components regardless of the underlying representation. If
    // the entry holds a matrix, decompose it using explode() 
    // with the same order and PivotSpace
    HUSD_XformEntryComponents
                         getXformComponents() const;

    bool                 useXformComponents() const
                         { return myUseXformComponents; }

    HUSD_TimeCode        myTimeCode;
    UT_XformOrder        myOrder    = UT_XformOrder(UT_XformOrder::SRT,
                                               UT_XformOrder::XYZ);
    UT_Vector3D          myPivot{0.,0.,0.};
    UT_Vector3D          myPivotRotate{0.,0.,0.};
    // If true,  overwrite with myPivot/myPivotRotate
    // If false, preserve the pivot already authored on the prim
    bool                 myWritePivot = false;

private:
    union
    {
        UT_Matrix4D                 myXformMatrix;
        HUSD_XformEntryComponents   myXformComponents;
    };
    bool                 myUseXformComponents = false;
};
typedef UT_Array<HUSD_XformEntry> HUSD_XformEntryArray;
typedef UT_StringMap<HUSD_XformEntryArray> HUSD_XformEntryMap;

class HUSD_API HUSD_LookAtEntry
{
public:
    HUSD_LookAtEntry();
    HUSD_LookAtEntry(
        const UT_StringHolder &lookat_prim,
        const UT_Vector3D     &lookat_pos,
        const UT_Vector3D     &up_vec,
        fpreal                 twist,
        const HUSD_TimeCode   &timecode);

    UT_StringHolder      myLookAtPrim;
    UT_Vector3D          myLookAtPos{0., 0., 0.};
    UT_Vector3D          myUpVec{0., 1., 0.};
    fpreal               myTwist = 0.0;
    HUSD_TimeCode        myTimeCode;
};
typedef UT_Array<HUSD_LookAtEntry> HUSD_LookAtEntryArray;
typedef UT_StringMap<HUSD_LookAtEntryArray> HUSD_LookAtEntryMap;

class HUSD_API HUSD_Xform
{
public:
			 HUSD_Xform(HUSD_AutoWriteLock &dest);
			~HUSD_Xform();

    // Append an entry for each prim matched by findprims to xform_map.
    // Used to accumulate entries across multiple calls (e.g. one per
    // time sample), to be committed by a single applyXforms call.
    void                 appendToXformMap(const HUSD_FindPrims &findprims,
                                const UT_Matrix4D *xform,
                                const HUSD_XformEntry::HUSD_XformEntryComponents
                                    *components,
                                const HUSD_TimeCode &timecode,
                                const UT_Vector3D *pivot,
                                const UT_Vector3D *pivot_rotate,
                                const UT_XformOrder *xform_order,
                                HUSD_XformEntryMap &xform_map) const;

    // For each primpath apply the corresponding xform
    bool		 applyXforms(const HUSD_XformEntryMap &xform_map,
				const UT_StringRef &name_suffix,
				HUSD_XformStyle xform_style) const;

    // Append a lookat entry for each prim matched by findprims to lookat_map.
    // Used to accumulate entries across multiple calls (e.g. one per
    // time sample), to be committed by a single applyLookAts call.
    void                 appendToLookAtMap(const HUSD_FindPrims &findprims,
                                const UT_StringRef &lookat_prim,
                                const UT_Vector3D &lookat_pos,
                                const UT_Vector3D &up_vec,
                                fpreal twist,
                                const HUSD_TimeCode &timecode,
                                HUSD_LookAtEntryMap &lookat_map) const;

    // Author a "lookat" xformOp for every (prim, timecode) entry in
    // lookat_map. All world-xform reads happen before any authoring, so
    // entries observe a consistent pre-author state and no identity-reset
    // of the lookat op is needed.
    bool                 applyLookAts(
                                const HUSD_LookAtEntryMap &lookat_map) const;

    /// @{ Add a given transform operation to the given primitives.
    /// The @p name_suffix is used to construct the transform operation full 
    /// name. The transform name is equivalent to the full attribute name 
    /// and the the entry in the transform order string array attribute.
    ///
    /// When @p is_timecode_strict is true, the op attriubte is set at a given
    /// time code, otherwise the given time code, if it's the default value, 
    /// may be cast to a specific frame time code, if the attribute 
    /// has some time samples already.
    ///
    /// See HUSD_Info::getXformName().
    bool		 addXform(const HUSD_FindPrims &findprims,
				const UT_StringRef &name_suffix,
				const UT_Matrix4D &xform,
				const HUSD_TimeCode &timecode,
				bool is_timecode_strict = true) const;
    bool		 addTranslate(const HUSD_FindPrims &findprims,
				const UT_StringRef &name_suffix,
				const UT_Vector3D &t,
				const HUSD_TimeCode &timecode,
				bool is_timecode_strict = true) const;
    bool		 addRotate(const HUSD_FindPrims &findprims,
				const UT_StringRef &name_suffix,
				HUSD_XformAxisOrder xyz_order, 
				const UT_Vector3D &r,
				const HUSD_TimeCode &timecode,
				bool is_timecode_strict = true) const;
    bool		 addRotate(const HUSD_FindPrims &findprims,
				const UT_StringRef &name_suffix,
				HUSD_XformAxis xyz_axis, double angle,
				const HUSD_TimeCode &timecode,
				bool is_timecode_strict = true) const;
    bool		 addScale(const HUSD_FindPrims &findprims,
				const UT_StringRef &name_suffix,
				const UT_Vector3D &s,
				const HUSD_TimeCode &timecode,
				bool is_timecode_strict = true) const;
    bool		 addOrient(const HUSD_FindPrims &findprims,
				const UT_StringRef &name_suffix,
				const UT_QuaternionD &o,
				const HUSD_TimeCode &timecode,
				bool is_timecode_strict = true) const;
    /// @}

    /// @{ Appends the given transform to the given primitives.
    /// The @p full_name is the transform operation full name, 
    /// which is equivalent to the full attribute name and the entry in
    /// the transform order string array attribute.
    /// See HUSD_Info::getXformName().
    bool		 addToXformOrder(const HUSD_FindPrims &findprims,
				const UT_StringRef &full_name) const;
    bool		 addInverseToXformOrder(const HUSD_FindPrims &findprims,
				const UT_StringRef &full_name) const;
    /// @}
		
    /// Sets the transform order attribute to the given sequence of transform
    /// operations. The @order contains transforms full names (see above).
    bool		 setXformOrder(const HUSD_FindPrims &findprims,
				const UT_StringArray &order) const;

    /// Clears the primitive's xform order string array attribute, 
    /// effectively earasing the local transform.
    bool		 clearXformOrder(const HUSD_FindPrims &findprims) const;

    /// Sets the flag to ignore primitive parent's transform, when calculating
    /// world transform of this primitive. Ie, if the reset flag is set,
    /// the primitive does not inherit the transformation from the parent.
    bool		 setXformReset( const HUSD_FindPrims &findprims,
				bool reset) const;

    /// Control whether or not warnings should be added if this object is
    /// told to transform a prim that is not xformable. Defaults to true.
    void                 setWarnBadPrimTypes(bool warn_bad_prim_types)
                         { myWarnBadPrimTypes = warn_bad_prim_types; }
    bool                 warnBadPrimTypes() const
                         { return myWarnBadPrimTypes; }

    /// Control whether or not this operation should check for the
    /// "houdini:editable" attribute on primitives before transforming them.
    /// Warnings are added for prims with this flag set to false.
    void                 setCheckEditableFlag(bool check_editable_flag)
                         { myCheckEditableFlag = check_editable_flag; }
    bool                 checkEditableFlag() const
                         { return myCheckEditableFlag; }

    /// Control whether or not this operation should check for the
    /// "houdini:editable" attribute on primitives before transforming them.
    /// Warnings are added for prims with this flag set to false.
    void                 setClearExistingFlag(bool clear_existing_flag)
                         { myClearExistingFlag = clear_existing_flag; }
    bool                 clearExistingFlag() const
                         { return myClearExistingFlag; }

    /// Returns true if the transform that was set on primitives
    /// may be time-varying.
    bool		 getIsTimeVarying() const;

private:
    HUSD_AutoWriteLock		&myWriteLock;
    bool                         myWarnBadPrimTypes;
    bool                         myCheckEditableFlag;
    bool                         myClearExistingFlag;
    mutable HUSD_TimeSampling	 myTimeSampling;
};

#endif

