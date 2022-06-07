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

#ifndef __HUSD_ConfigurePrims_h__
#define __HUSD_ConfigurePrims_h__

#include "HUSD_API.h"
#include "HUSD_DataHandle.h"
#include "HUSD_TimeCode.h"
#include "HUSD_Utils.h"
#include <UT/UT_StringHolder.h>

class HUSD_FindPrims;

class HUSD_API HUSD_ConfigurePrims
{
public:
			 HUSD_ConfigurePrims(HUSD_AutoWriteLock &lock);
			~HUSD_ConfigurePrims();

    bool		 setType(const HUSD_FindPrims &findprims,
				const UT_StringRef &primtype) const;
    bool		 setSpecifier(const HUSD_FindPrims &findprims,
                                const UT_StringRef &specifier) const;
    bool		 setActive(const HUSD_FindPrims &findprims,
				bool active) const;
    /// Forces the effective activation of a given set of prims by traversing
    /// the prim hierarchy and manipulating ancestor prims' active status.
    ///
    /// This is somewhat akin to MakeVisible in UsdGeomImageable.
    ///
    /// As this can be used in a corrective context, it can optionally emit
    /// a warning message if any maniption actually takes place.
    ///
    /// NOTE: Unlike the rest of the methods in this class, we do not accept
    ///       a HUSD_FindPrims as it will fail to actually find prims that have
    ///       inactive ancestors (this is by design in USD)
    ///
    /// NOTE: This function will not work if run while there is an active
    ///       Sdf change block (and there doesn't seem to be a way to check)
    bool		 makePrimsAndAncestorsActive(
			        const HUSD_PathSet &pathset,
				bool emit_warning_on_action = false) const;
    bool		 setKind(const HUSD_FindPrims &findprims,
				const UT_StringRef &kind) const;
    bool		 setDrawMode(const HUSD_FindPrims &findprims,
				const UT_StringRef &drawmode) const;
    bool		 setPurpose(const HUSD_FindPrims &findprims,
				const UT_StringRef &purpose) const;
    bool		 setProxy(const HUSD_FindPrims &findprims,
				const UT_StringRef &proxy) const;
    bool		 setInstanceable(const HUSD_FindPrims &findprims,
				bool instanceable) const;
    enum Visibility {
	VISIBILITY_INHERIT,
	VISIBILITY_INVISIBLE,
	VISIBILITY_VISIBLE
    };
    bool		 setInvisible(const HUSD_FindPrims &findprims,
				Visibility vis,
				const HUSD_TimeCode &timecode,
				bool ignore_time_varying_stage) const;
    bool		 setVariantSelection(const HUSD_FindPrims &findprims,
				const UT_StringRef &variantset,
				const UT_StringRef &variant) const;
    bool                 setComputedExtents(const HUSD_FindPrims &findprims,
                                const HUSD_TimeCode &timecode,
                                bool clear_existing) const;

    bool		 setAssetName(const HUSD_FindPrims &findprims,
				const UT_StringRef &name) const;
    bool		 setAssetIdentifier(const HUSD_FindPrims &findprims,
				const UT_StringRef &identifier) const;
    bool		 setAssetVersion(const HUSD_FindPrims &findprims,
				const UT_StringRef &version) const;
    bool		 setAssetDependencies(const HUSD_FindPrims &findprims,
				const UT_StringArray &dependencies) const;
    // This function sets the asset info on UsdModelAPI-enabled prims.
    // The UtValueType parameters can be any of:
    //    bool
    //    int
    //    int64
    //    UT_Vector2i
    //    UT_Vector3i
    //    UT_Vector4i
    //    fpreal32
    //    fpreal64
    //    UT_Vector2F
    //    UT_Vector3F
    //    UT_Vector4F
    //    UT_QuaternionF
    //    UT_QuaternionH
    //    UT_Matrix3D
    //    UT_Matrix4D
    //    UT_StringHolder
    //    UT_Array<UT_StringHolder>
    //    HUSD_AssetPath
    //    UT_Array<HUSD_AssetPath>
    //    HUSD_Token
    //    UT_Array<HUSD_Token>
    // Make sure to explicitly cast to one of these data types, even if
    // implicit conversions exist.
    template<typename UtValueType>
    bool		 setAssetInfo(const HUSD_FindPrims &findprims,
                                const UT_StringRef &key,
                                const UtValueType &value) const;
    bool		 removeAssetInfo(const HUSD_FindPrims &findprims,
                                const UT_StringRef &key) const;
    bool		 clearAssetInfo(const HUSD_FindPrims &findprims) const;

    bool                 setEditable(const HUSD_FindPrims &findprims,
                                bool editable) const;
    bool                 setSelectable(const HUSD_FindPrims &findprims,
                                bool selectable) const;
    bool                 setHideInUi(const HUSD_FindPrims &findprims,
                                bool hide) const;

    bool		 addEditorNodeId(
                                const HUSD_FindPrims &findprims,
                                int nodeid) const;
    bool		 clearEditorNodeIds(
                                const HUSD_FindPrims &findprims) const;

    bool		 applyAPI(const HUSD_FindPrims &findprims,
				const UT_StringRef &schema,
                                UT_StringSet *failedapis) const;

    bool                 getIsTimeVarying() const;

private:
    HUSD_AutoWriteLock	        &myWriteLock;
    mutable HUSD_TimeSampling    myTimeSampling;
};

#endif

