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

#include "HUSD_Xform.h"
#include "HUSD_ErrorScope.h"
#include "HUSD_FindPrims.h"
#include "HUSD_Info.h"
#include "HUSD_PathSet.h"
#include "HUSD_Utils.h"
#include "XUSD_Data.h"
#include "XUSD_PathSet.h"
#include "XUSD_Utils.h"
#include "UsdHoudini/houdiniXformCommonAPI.h"
#include <UT/UT_Interrupt.h>
#include <UT/UT_Quaternion.h>
#include <gusd/UT_Gf.h>
#include <pxr/base/tf/stringUtils.h>
#include <pxr/usd/usdGeom/xformable.h>
#include <pxr/usd/usdGeom/xformCache.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usd/primRange.h>

using namespace UT::Literal;

PXR_NAMESPACE_USING_DIRECTIVE

HUSD_XformEntry::HUSD_XformEntry()
    : myUseXformComponents(false)
{
    // Default initialization, setting matrix to identity
    myXformMatrix = UT_Matrix4D(1.0);
}

HUSD_XformEntry::HUSD_XformEntry(
        const UT_Matrix4D   &matrix,
        const HUSD_TimeCode &timecode,
        bool                 write_pivot,
        const UT_Vector3D   &pivot,
        const UT_Vector3D   &pivot_rotate,
        const UT_XformOrder &order)
    : myTimeCode(timecode)
    , myWritePivot(write_pivot)
    , myPivot(pivot)
    , myPivotRotate(pivot_rotate)
    , myOrder(order)
    , myUseXformComponents(false)
{
    myXformMatrix = matrix;
}

HUSD_XformEntry::HUSD_XformEntry(
        const HUSD_XformEntryComponents &components,
        const HUSD_TimeCode &timecode,
        bool                 write_pivot,
        const UT_Vector3D   &pivot,
        const UT_Vector3D   &pivot_rotate,
        const UT_XformOrder &order)
    : myTimeCode(timecode)
    , myWritePivot(write_pivot)
    , myPivot(pivot)
    , myPivotRotate(pivot_rotate)
    , myOrder(order)
    , myUseXformComponents(true)
{
    myXformComponents = components;
}

void
HUSD_XformEntry::setXformMatrix(const UT_Matrix4D &matrix)
{
    myXformMatrix = matrix;
    myUseXformComponents = false;
}

void
HUSD_XformEntry::setXformComponents(const HUSD_XformEntryComponents &components)
{
    myXformComponents = components;
    myUseXformComponents = true;
}

HUSD_XformEntry::HUSD_XformEntryComponents
HUSD_XformEntry::getXformComponents() const
{
    // Components-based: return a copy.
    if (myUseXformComponents)
        return myXformComponents;

    // Matrix-based: decompose using the same order and PivotSpace 
    HUSD_XformEntryComponents comp;
    myXformMatrix.explode(myOrder,
        comp.myR, comp.myS, comp.myT,
        UT_Matrix4D::PivotSpace(myPivot, myPivotRotate),
        &comp.myShear);
    return comp;
}

UT_Matrix4D
HUSD_XformEntry::getXformMatrix() const
{
    // If we have a valid matrix, directly return
    if (!myUseXformComponents)
        return myXformMatrix;

    // Convert components to a matrix using the same logic as
    // LOP_XformComponents::applyXform.
    const auto &comp = myXformComponents;
    UT_Matrix4D m(1.0);
    if (comp.myS.isZero() && comp.myShear.isZero())
    {
        m.zero();
        m(3,3) = 1.0;
        m.setTranslates(comp.myT);
    }
    else
    {
        m.xform(myOrder,
            comp.myT.x(), comp.myT.y(), comp.myT.z(),
            comp.myR.x(), comp.myR.y(), comp.myR.z(),
            comp.myS.x(), comp.myS.y(), comp.myS.z(),
            comp.myShear.x(), comp.myShear.y(), comp.myShear.z(),
            UT_Matrix4D::PivotSpace(myPivot, myPivotRotate));
    }
    return m;
}

HUSD_Xform::HUSD_Xform(HUSD_AutoWriteLock &lock)
    : myWriteLock(lock),
      myWarnBadPrimTypes(true),
      myCheckEditableFlag(false),
      myClearExistingFlag(true),
      myTimeSampling(HUSD_TimeSampling::NONE)
{
}

HUSD_Xform::~HUSD_Xform()
{
}

static GfMatrix4d
husdComputeWorldSpaceDelta(
    const UsdGeomXformable &xformable,
    const GfMatrix4d &inputXform,
    UsdTimeCode ndusdtime,
    bool clear_existing,
    HUSD_TimeSampling &used_time_sampling,
    UsdTimeCode &settime,
    UsdGeomXformOp *xformop_to_reset = nullptr)
{
    // If we are setting a transform that is affected by an
    // animated transform, then we must set the transform
    // at the current time, rather than the default time.
    // The the LOP must be sure to set this transform again
    // whenever the time changes.
    auto sampling = HUSDgetWorldTransformTimeSampling(
        xformable.GetPrim());
    if (HUSDisTimeSampled(sampling))
    {
        // Only update time sampling flag if clear_existing is
        // true, otherwise we'll think we are time varying because
        // a moment ago we wrote a time sampled value to our xform
        // (with clear_existing set to true).
        if (clear_existing)
            HUSDupdateTimeSampling(used_time_sampling, sampling);
        settime = ndusdtime;
    }

    // xformop_to_reset should be empty if we are authoring XformCommon API
    if (xformop_to_reset)
    {
        // Before calculating the current xform of the prim, set the
        // contribution of the current xformop to an identity matrix.
        xformop_to_reset->Set(GfMatrix4d(1.0), settime);
    }
    
    // We want to apply the xform in world space, so we
    // have to compensate for our current xform.
    GfMatrix4d l2w = xformable.ComputeLocalToWorldTransform(ndusdtime);
    return l2w * inputXform * l2w.GetInverse();
}

static void
husdApplyXform(const SdfPath &sdfpath,
        const UsdStageRefPtr &stage,
	const UT_StringRef &name,
	const HUSD_XformEntry *xform_entries,
	int numentries,
	HUSD_XformStyle xform_style,
        bool warn_bad_prim_types,
        bool check_editable_flag,
        bool clear_existing,
	HUSD_TimeSampling &used_time_sampling,
        UT_Map<HUSD_Path, UT_StringHolder> *suffix_map)
{
    auto	         usdprim = stage->GetPrimAtPath(sdfpath);
    if (!usdprim)
    {
        HUSD_ErrorScope::addWarning(HUSD_ERR_NOT_USD_PRIM,
            sdfpath.GetAsString().c_str());
        return;
    }

    if (check_editable_flag && !HUSDisPrimEditable(usdprim))
    {
        HUSD_ErrorScope::addWarning(HUSD_PRIM_NOT_EDITABLE,
            sdfpath.GetAsString().c_str());
        return;
    }

    UsdGeomXformable	 xformable(usdprim);
    if (!xformable)
    {
        if (warn_bad_prim_types)
            HUSD_ErrorScope::addWarning(HUSD_ERR_NOT_XFORMABLE_PRIM,
                sdfpath.GetAsString().c_str());
        return;
    }

    UT_StringHolder		 xformopsuffix;
    UsdGeomXformOp		 xformop;
    std::vector<UsdGeomXformOp>	 xformops;
    SdfPath                      suffix_map_path;
    bool			 does_reset = false;
    bool                         record_suffix = (suffix_map != nullptr);

    xformopsuffix = name;
    if (suffix_map)
    {
        // Create a path that combines the prim path and xformop suffix in case
        // we are writing several xforms on the same prim from one "edit
        // properties" node.
        if (xformopsuffix.isstring())
            suffix_map_path = sdfpath.AppendProperty(TfToken(xformopsuffix));
        else
            suffix_map_path = sdfpath;
        auto it = suffix_map->find(suffix_map_path);
        if (it != suffix_map->end())
        {
            xformopsuffix = it->second;
            record_suffix = false;
        }
    }

    if (HUSDisExtendedXformAPIStyle(xform_style) ||
        HUSDisBasicXformAPIStyle(xform_style))
    {
        auto sampling = HUSDgetLocalTransformTimeSampling(xformable.GetPrim());
        UsdHoudiniHoudiniXformCommonAPI common(usdprim);
        GfVec3d t;
        GfVec3f r, s;
        UsdTimeCode settime;
        bool xform_basic_common_api = HUSDisBasicXformAPIStyle(xform_style);

        for (int i = 0; i <numentries; i++)
        {
            UsdTimeCode ndusdtime = HUSDgetNonDefaultUsdTimeCode(
                    xform_entries[i].myTimeCode);
            UsdTimeCode usdtime = HUSDgetUsdTimeCode(
                xform_entries[i].myTimeCode);

            // Determine if USD timecode should be time varying
            if (HUSDisTimeSampled(sampling))
            {
                if (clear_existing)
                    HUSDupdateTimeSampling(used_time_sampling, sampling);

                settime = ndusdtime;
            }
            else
                settime = usdtime;

            UsdHoudiniHoudiniXformCommonAPI::RotationOrder order =
                HUSDcastRotOrder(xform_entries[i].myOrder);
            
            GfVec3f pivot(
                xform_entries[i].myPivot.x(),
                xform_entries[i].myPivot.y(),
                xform_entries[i].myPivot.z()
            );
            GfVec3f pr(
                xform_basic_common_api ? 0.0 : xform_entries[i].myPivotRotate.x(),
                xform_basic_common_api ? 0.0 : xform_entries[i].myPivotRotate.y(),
                xform_basic_common_api ? 0.0 : xform_entries[i].myPivotRotate.z()
            );

            // If the caller didn't supply a pivot, preserve whatever
            // the prim already has. GetXformVectors() returns (0,0,0) for
            // matrix-only prims
            if (!xform_entries[i].myWritePivot && common)
            {
                GfVec3d saved_t;
                GfVec3f saved_s, saved_shear;
                GfVec3f saved_pivot, saved_pr;
                UsdHoudiniHoudiniXformCommonAPI::Rotation saved_r;
                if (common.GetXformVectors(&saved_t, &saved_r, &saved_s,
                        &saved_shear, &saved_pivot, &saved_pr, settime))
                {
                    pivot = saved_pivot;
                    if (!xform_basic_common_api)
                        pr = saved_pr;
                }
            }

            UT_Matrix4D::PivotSpaceT<fpreal32> combined_pivot(
                GusdUT_Gf::Cast(pivot), GusdUT_Gf::Cast(pr));
            UT_Matrix4D combinedXform(1.0);
            GfVec3f shear(0.0, 0.0, 0.0);

            auto explode_xform_fn = [&]()
            {
                UT_Vector3F t_tmp;
                combinedXform.explode(HUSDcastRotOrder(order),
                    GusdUT_Gf::Cast(r), GusdUT_Gf::Cast(s),
                    t_tmp, combined_pivot,
                    xform_basic_common_api
                        ? nullptr
                        : &GusdUT_Gf::Cast(shear));
                GusdUT_Gf::Convert(t_tmp, t);
                r.Set(SYSradToDeg(r[0]),
                    SYSradToDeg(r[1]),
                    SYSradToDeg(r[2]));
                
                // The explode method has a tendency to output -0.0 values in
                // rotations. This is of little value, and it's kind of ugly, so
                // convert these to +0.
                for (int idx = 0; idx < 3; idx++)
                    if (r.data()[idx] == 0.0)
                        r.data()[idx] = 0.0;
            };

            if (xform_style == HUSD_XFORM_COMMON_API_WORLDSPACE)
            {
                GfMatrix4d localXform(1.0);
                xformable.GetLocalTransformation(
                    &localXform, &does_reset, ndusdtime);

                GfMatrix4d delta = husdComputeWorldSpaceDelta(
                    xformable,
                    GusdUT_Gf::Cast(xform_entries[i].getXformMatrix()),
                    ndusdtime, clear_existing,
                    used_time_sampling, settime);

                // Append the delta
                combinedXform = GusdUT_Gf::Cast(delta * localXform);
                explode_xform_fn();
            }
            else if (xform_entries[i].useXformComponents() &&
                (xform_style == HUSD_XFORM_COMMON_API_OVERWRITE ||
                 xform_style == HUSD_XFORM_BASIC_COMMON_API_OVERWRITE))
            {
                // Use component value directly
                const auto &comp = xform_entries[i].getXformComponents();
                t.Set(comp.myT.x(), comp.myT.y(), comp.myT.z());
                r.Set(comp.myR.x(), comp.myR.y(), comp.myR.z());
                s.Set(comp.myS.x(), comp.myS.y(), comp.myS.z());
                if (!xform_basic_common_api)
                    shear.Set(comp.myShear.x(), comp.myShear.y(), comp.myShear.z());
            }
            else if(xform_entries[i].useXformComponents())
            {
                GfVec3d saved_t;
                UsdHoudiniHoudiniXformCommonAPI::Rotation saved_rot;
                GfVec3f saved_s, saved_shear;
                GfVec3f saved_pivot, saved_pr;
                common.GetXformVectors(&saved_t, &saved_rot, &saved_s,
                    &saved_shear, &saved_pivot, &saved_pr, settime);

                const auto &comp = xform_entries[i].getXformComponents();
                // Combine like LOP_XformComponents::combine
                t = saved_t + GfVec3d(comp.myT.x(), comp.myT.y(), comp.myT.z());
                // Decompose to Euler for additive combination.
                GfVec3f saved_r = saved_rot.GetEulerAngles();
                r = saved_r + GfVec3f(comp.myR.x(), comp.myR.y(), comp.myR.z());
                s.Set(saved_s[0] * comp.myS.x(),
                    saved_s[1] * comp.myS.y(),
                    saved_s[2] * comp.myS.z());
                if (!xform_basic_common_api)
                    shear = saved_shear + GfVec3f(comp.myShear.x(),
                                                comp.myShear.y(),
                                                comp.myShear.z());
            }
            else
            {
                GfMatrix4d localXform(1.0);
                xformable.GetLocalTransformation(&localXform, &does_reset, settime);
                if (xform_style == HUSD_XFORM_COMMON_API_OVERWRITE ||
                    xform_style == HUSD_XFORM_BASIC_COMMON_API_OVERWRITE)
                {
                    combinedXform = xform_entries[i].getXformMatrix();
                }
                else
                {
                    combinedXform = (xform_style != HUSD_XFORM_COMMON_API_PREPEND
                        && xform_style != HUSD_XFORM_BASIC_COMMON_API_PREPEND) ?
                            xform_entries[i].getXformMatrix() *
                                GusdUT_Gf::Cast(localXform) :       //Append
                            GusdUT_Gf::Cast(localXform) *
                                xform_entries[i].getXformMatrix();  //Prepend
                }

                explode_xform_fn();
            }

            // Check if this is common, if not, clear everything in the ordered
            // xform op
            if (!common)
                xformable.ClearXformOpOrder();

            // If we were not explicitly given xform components (we were just
            // given a matrix to decompose) and we are authoring a time sample,
            // then author a quaternion for the rotation so we get the best
            // possible motion blur.
            UsdHoudiniHoudiniXformCommonAPI::Rotation rot(r, order);
            if (!xform_entries[i].useXformComponents() &&
                !settime.IsDefault())
                common.SetXformVectors(t,
                    UsdHoudiniHoudiniXformCommonAPI::Rotation(rot.GetQuaternion()),
                    s, shear, pivot, pr, settime);
            else
                common.SetXformVectors(t, rot,
                    s, shear, pivot, pr, settime);
        }
        
        std::vector<UsdGeomXformOp> xformop_ordered =
            xformable.GetOrderedXformOps(&does_reset);
        // Blocking all attributes in the "xformOp:" namespace that are not
        // contained in the xfrom op order array
        for (UsdAttribute& attr : usdprim.GetAttributes())
        {
            if (UsdGeomXformOp::IsXformOp(attr)) 
            {
                auto it = std::find_if(xformop_ordered.begin(), 
                            xformop_ordered.end(),
                            [&attr](const UsdGeomXformOp& op) 
                            {
                                return op.GetAttr() == attr;
                            });
                
                if (it == xformop_ordered.end())
                    attr.Block();
            }
        }

        if (record_suffix)
            suffix_map->emplace(suffix_map_path, xformopsuffix);

        return;
    }

    if (xform_style == HUSD_XFORM_ABSOLUTE)
        xformable.ClearXformOpOrder();

    if (xform_style == HUSD_XFORM_OVERWRITE ||
        xform_style == HUSD_XFORM_OVERWRITE_APPEND ||
        xform_style == HUSD_XFORM_OVERWRITE_PREPEND)
    {
        // Look for the existing xform op with the provided name.
        UT_StringHolder overwritesuffix =
            (suffix_map && !record_suffix) ? xformopsuffix : name;
        TfToken fullname = UsdGeomXformOp::GetOpName(
                UsdGeomXformOp::TypeTransform,
                TfToken(overwritesuffix.toStdString()));
        xformops = xformable.GetOrderedXformOps(&does_reset);
        for (auto &&testop : xformops)
        {
            if (testop.GetOpName() == fullname)
            {
                xformop = testop;
                xformopsuffix = overwritesuffix;
                break;
            }
        }
        // In overwrite-only mode we didn't find an xfrom to overwrite.
        if (!xformop && xform_style == HUSD_XFORM_OVERWRITE)
        {
            HUSD_ErrorScope::addWarning(HUSD_ERR_NO_XFORM_FOUND,
                                        sdfpath.GetAsString().c_str());
            return;
        }
    }
    else
    {
        // Deals with APPEND, PREPEND, ABSOLUTE, and WORLDSPACE.
        // Make sure we have a unique attribute name, unless there is an
        // explicit name set already in the suffix map.
        if (suffix_map && !record_suffix)
        {
            TfToken fullname = UsdGeomXformOp::GetOpName(
                UsdGeomXformOp::TypeTransform,
                TfToken(xformopsuffix.toStdString()));
            xformops = xformable.GetOrderedXformOps(&does_reset);
            for (auto &&testop : xformops)
            {
                if (testop.GetOpName() == fullname)
                {
                    xformop = testop;
                    break;
                }
            }
        }
        if (!xformop)
        {
            // If we need to generate a new unique xformop suffix, use xform1
            // as the starting point if we were passed an empty string as the
            // suffix.
            if (!xformopsuffix.isstring())
                xformopsuffix = "xform1"_sh;
            HUSDgenerateUniqueTransformOpSuffix(
                xformopsuffix, xformable, UsdGeomXformOp::TypeTransform,
                name.isEmpty());
        }
    }

    // If we don't have one yet, create an xform op (and the associated
    // attribute) either at the front or the back of the xform op
    // order.
    if (!xformop)
    {
        UT_StringHolder		 opname;

        xformop = xformable.AddTransformOp(
            UsdGeomXformOp::PrecisionDouble,
            TfToken(xformopsuffix));
        if (xformop &&
            (xform_style == HUSD_XFORM_PREPEND ||
             xform_style == HUSD_XFORM_OVERWRITE_PREPEND))
        {
            xformops = xformable.GetOrderedXformOps(&does_reset);
            xformops.pop_back();
            xformops.insert(xformops.begin(), xformop);
            xformable.SetXformOpOrder(xformops, does_reset);
        }
    }

    if (xformop)
    {
        for (int i = 0; i <numentries; i++)
        {
            UsdTimeCode usdtime = HUSDgetUsdTimeCode(
                xform_entries[i].myTimeCode);
            UsdTimeCode ndusdtime = HUSDgetNonDefaultUsdTimeCode(
                xform_entries[i].myTimeCode);
            GfMatrix4d xform = GusdUT_Gf::Cast(
                xform_entries[i].getXformMatrix());

            if (clear_existing)
                xformop.GetAttr().Clear();
            if (xform_style == HUSD_XFORM_WORLDSPACE)
            {
                UsdTimeCode settime = usdtime;

                GfMatrix4d new_xform = husdComputeWorldSpaceDelta(
                    xformable, xform, ndusdtime,
                    clear_existing, used_time_sampling,
                    settime, &xformop);

                xformop.Set(new_xform, settime);
            }
            else
                xformop.Set(xform, usdtime);
        }
    }
    if (record_suffix)
        suffix_map->emplace(suffix_map_path, xformopsuffix);
}

bool
HUSD_Xform::applyXforms(const HUSD_FindPrims &findprims,
	const UT_StringRef &name,
	const UT_Matrix4D *xform,
	const HUSD_XformEntry::HUSD_XformEntryComponents *components,
	const HUSD_TimeCode &timecode,
	HUSD_XformStyle xform_style,
        const UT_Vector3D *pivot,
        const UT_Vector3D *pivot_rotate,
        const UT_XformOrder *xform_order,
        UT_Map<HUSD_Path, UT_StringHolder> *suffix_map) const
{
    auto	 outdata = myWriteLock.data();
    bool	 success = false;

    if (outdata && outdata->isStageValid())
    {
        UT_AutoInterrupt boss("Apply transforms");
	auto		 stage = outdata->stage();
	HUSD_XformEntry	 xform_entry;
        if (components)
            xform_entry.setXformComponents(*components);
        else
            xform_entry.setXformMatrix(*xform);

        xform_entry.myTimeCode = timecode;

        if (xform_order)
            xform_entry.myOrder = *xform_order;
        
        if (pivot && pivot_rotate)
        {
            xform_entry.myPivot = *pivot;
            xform_entry.myPivotRotate = *pivot_rotate;
            xform_entry.myWritePivot = true;
        }
	HUSD_Info        info(myWriteLock);
        unsigned char    count = 0;

	for (auto &&sdfpath : findprims.getExpandedPathSet().sdfPathSet())
	{
            if (count++ == 0 && boss.wasInterrupted())
                break;

	    husdApplyXform(sdfpath, stage, name,
                &xform_entry, 1, xform_style,
                myWarnBadPrimTypes, myCheckEditableFlag,
                myClearExistingFlag, myTimeSampling, suffix_map);
	}
	success = true;
    }

    return success;
}

bool
HUSD_Xform::applyXforms(const HUSD_XformEntryMap &xform_map,
	const UT_StringRef &name,
	HUSD_XformStyle xform_style,
        UT_Map<HUSD_Path, UT_StringHolder> *suffix_map) const
{
    auto	 outdata = myWriteLock.data();
    bool	 success = false;

    if (outdata && outdata->isStageValid())
    {
        UT_AutoInterrupt boss("Apply transforms");
	auto             stage = outdata->stage();
        unsigned char    count = 0;

	for (auto it = xform_map.begin(); it != xform_map.end(); ++it)
	{
            if (count++ == 0 && boss.wasInterrupted())
                break;

	    SdfPath	 sdfpath = HUSDgetSdfPath(it->first);

	    husdApplyXform(sdfpath, stage, name,
                it->second.data(), it->second.size(), xform_style,
                myWarnBadPrimTypes, myCheckEditableFlag,
                myClearExistingFlag, myTimeSampling, suffix_map);
	}
	success = true;
    }

    return success;
}

bool
HUSD_Xform::applyLookAt(const HUSD_FindPrims &findprims,
        const UT_StringRef &lookatprim,
        const UT_Vector3D &lookatpos,
        const UT_Vector3D &upvec,
        fpreal twist,
        const HUSD_TimeCode &timecode,
        UT_Map<HUSD_Path, UT_StringHolder> *suffix_map) const
{
    auto	 outdata = myWriteLock.data();
    bool	 success = false;

    if (outdata && outdata->isStageValid())
    {
        UT_AutoInterrupt boss("Apply transforms");
	auto             stage = outdata->stage();
        unsigned char    count = 0;

	for (auto &&sdfpath : findprims.getExpandedPathSet().sdfPathSet())
	{
            if (count++ == 0 && boss.wasInterrupted())
                break;

            UT_Matrix4D          targetprimxform(0.0);
            UT_Matrix4D          prelookatxform(0.0);
            UT_Matrix4D          xform(1.0);
            HUSD_TimeSampling    lookat_ts = HUSD_TimeSampling::NONE;
            HUSD_TimeSampling    this_ts = HUSD_TimeSampling::NONE;
            HUSD_TimeCode        timecode_copy(timecode);

            // Gather information from our stage.
            {
                HUSD_Info            info(myWriteLock);

                // Get the xform of the target prim if there is one.
                if (lookatprim.isstring())
                {
                    targetprimxform = info.getWorldXform(
                        lookatprim, timecode, &lookat_ts);
		    HUSDupdateTimeSampling(myTimeSampling, lookat_ts);
                }
                if (targetprimxform.isZero())
                    targetprimxform.identity();

                // If the input transforms we rely on are time varying, we
                // need to author a time sample for the lookat.
                this_ts = HUSDgetWorldTransformTimeSampling(
                    stage->GetPrimAtPath(sdfpath));
                if (HUSDisTimeSampled(lookat_ts) || HUSDisTimeSampled(this_ts))
                    timecode_copy = timecode.getNonDefaultTimeCode();

                // Author an identity xform at the current timecode so that
                // when we get the current xform of this prim, it isn't
                // affected by this lookat xform we are trying to author.
                HUSD_XformEntry	 identity_xform_entry = {xform, timecode_copy};

                husdApplyXform(sdfpath, stage, "lookat",
                    &identity_xform_entry, 1, HUSD_XFORM_APPEND,
                    myWarnBadPrimTypes, myCheckEditableFlag,
                    myClearExistingFlag, myTimeSampling,
                    suffix_map);

                // Get the xform of this prim.
                prelookatxform = info.getWorldXform(
                    sdfpath.GetAsString(), timecode);
		HUSDupdateTimeSampling(myTimeSampling, this_ts);

                if (prelookatxform.isZero())
                    prelookatxform.identity();
            }

            UT_Vector3D      origin(0.0, 0.0, 0.0);
            UT_Vector3D      targetpos(0.0, 0.0, 0.0);
            UT_Matrix3D      lookatxform(1.0);
            UT_Matrix3D      undorotxform(1.0);

            // Get the position of the centroid of this object. This is the
            // point from which we need to look at the target.
            origin *= prelookatxform;
            // Generate the target position.
            targetpos = lookatpos;
            targetpos *= targetprimxform;
            // Generate the lookat matrix.
            lookatxform.lookat(origin, targetpos, upvec);
            // Apply the requested twist (negated because we actually want to
            // twist around the negative Z axis).
            lookatxform.prerotate(UT_Axis3::ZAXIS, -SYSdegToRad(twist));

            // There may already be rotations in the prelookatxform. We
            // need to undo these rotations so the -Z axis is pointed down -Z
            // before we apply our lookat.
            undorotxform = prelookatxform;
            undorotxform.makeRotationMatrix();
            undorotxform.invert();

            // Apply the lookat into the xform we will be adding to this prim.
            lookatxform *= undorotxform;
            xform.preMultiply(UT_Matrix4D(lookatxform));

            HUSD_XformEntry	 xform_entry = {xform, timecode_copy};

            husdApplyXform(sdfpath, stage, "lookat",
                &xform_entry, 1, HUSD_XFORM_APPEND,
                myWarnBadPrimTypes, myCheckEditableFlag,
                myClearExistingFlag, myTimeSampling,
                suffix_map);
        }
	success = true;
    }

    return success;
}

static inline UsdGeomXformOp::Type
husdGetRotateAxisType(HUSD_XformAxis xyz_axis)
{
    switch( xyz_axis )
    {
	case HUSD_XformAxis::X: return UsdGeomXformOp::TypeRotateX;
	case HUSD_XformAxis::Y: return UsdGeomXformOp::TypeRotateY;
	case HUSD_XformAxis::Z: return UsdGeomXformOp::TypeRotateZ;
	default: break;
    }

    UT_ASSERT( !"Invalid axis" );
    return UsdGeomXformOp::TypeInvalid;
}

static inline UsdGeomXformOp::Type
husdGetRotateOrderType(HUSD_XformAxisOrder xyz_order)
{
    switch( xyz_order )
    {
	case HUSD_XformAxisOrder::XYZ: return UsdGeomXformOp::TypeRotateXYZ;
	case HUSD_XformAxisOrder::XZY: return UsdGeomXformOp::TypeRotateXZY;
	case HUSD_XformAxisOrder::YXZ: return UsdGeomXformOp::TypeRotateYXZ;
	case HUSD_XformAxisOrder::YZX: return UsdGeomXformOp::TypeRotateYZX;
	case HUSD_XformAxisOrder::ZXY: return UsdGeomXformOp::TypeRotateZXY;
	case HUSD_XformAxisOrder::ZYX: return UsdGeomXformOp::TypeRotateZYX;
	default: break;
    }

    UT_ASSERT( !"Invalid axis order" );
    return UsdGeomXformOp::TypeInvalid;
}

template <typename F>
static inline bool
husdModifyXformable(HUSD_AutoWriteLock &lock, const HUSD_FindPrims &findprims, 
	F callback )
{
    auto outdata = lock.data();
    if (!outdata || !outdata->isStageValid())
	return false;

    bool ok = true;
    auto stage(outdata->stage());
    for (auto &&sdfpath : findprims.getExpandedPathSet().sdfPathSet())
    {
	UsdGeomXformable xformable(stage->GetPrimAtPath(sdfpath));
	if (!xformable)
	{
	    ok = false;
	    continue;
	}

	if( !callback( xformable ))
	    ok = false;
    }

    return ok;
}

static inline UsdTimeCode
husdGetEffectiveUsdTimeCode( const HUSD_TimeCode &tc, 
	bool is_strict, const UsdAttribute &attr )
{
    if( is_strict || !attr )
	return HUSDgetUsdTimeCode(tc);

    return HUSDgetEffectiveUsdTimeCode( tc, attr );
}


template <typename T>
static inline bool
husdAddTransform(HUSD_AutoWriteLock &lock, const HUSD_FindPrims &findprims, 
	const HUSD_TimeCode &timecode, bool is_timecode_strict,
        bool clear_existing, const UT_StringRef &name_suffix,
        UsdGeomXformOp::Type type, const T &value )
{
    return husdModifyXformable(lock, findprims, 
	    [&](UsdGeomXformable &xformable)
	    {
                bool resets_xform_op = false;
                auto precision = UsdGeomXformOp::PrecisionDouble;
                UsdGeomXformOp xform_op;
                TfToken name_suffix_token(name_suffix);
                TfToken opname = UsdGeomXformOp::GetOpName(type,
                    name_suffix_token);
                std::vector<UsdGeomXformOp> xform_ops =
                    xformable.GetOrderedXformOps(&resets_xform_op);

                // Set an existing xform_op if available, and clear_existing
                // is false (clear_existing is overridden a bit here to allow
                // setting multiple time samples on the same xformOp in a
                // single VEX Wrangle cook).
                if (!clear_existing)
                {
                    for (auto op : xform_ops)
                    {
                        if (op.GetOpName() == opname &&
                            op.GetOpType() == type &&
                            op.GetPrecision() == precision)
                        {
                            xform_op = op;
                            break;
                        }
                    }
                }
                // Otherwise create a new xform op.
                if (!xform_op)
                    xform_op = xformable.AddXformOp(type,
                        precision, name_suffix_token);
                else if (clear_existing)
                    xform_op.GetAttr().Clear();

		if(!xform_op)
		    return false;

		UsdTimeCode usd_timecode = husdGetEffectiveUsdTimeCode( 
			timecode, is_timecode_strict, xform_op.GetAttr() );
		return xform_op.Set(value, usd_timecode);
	    });
}

bool
HUSD_Xform::addXform(const HUSD_FindPrims &findprims,
	const UT_StringRef &name_suffix, const UT_Matrix4D &xform,
	const HUSD_TimeCode &timecode, bool is_timecode_strict) const
{
    const GfMatrix4d &gf_xform = GusdUT_Gf::Cast(xform);
    return husdAddTransform(myWriteLock, findprims, timecode, 
        is_timecode_strict, myClearExistingFlag, name_suffix,
        UsdGeomXformOp::TypeTransform, gf_xform);
}

bool
HUSD_Xform::addTranslate(const HUSD_FindPrims &findprims,
	const UT_StringRef &name_suffix, const UT_Vector3D &xform,
	const HUSD_TimeCode &timecode, bool is_timecode_strict) const
{
    const GfVec3d &gf_t = GusdUT_Gf::Cast(xform);
    return husdAddTransform(myWriteLock, findprims, timecode, 
        is_timecode_strict, myClearExistingFlag, name_suffix,
        UsdGeomXformOp::TypeTranslate, gf_t);
}

bool
HUSD_Xform::addRotate(const HUSD_FindPrims &findprims,
	const UT_StringRef &name_suffix, HUSD_XformAxisOrder order, 
	const UT_Vector3D &r, const HUSD_TimeCode &timecode, 
	bool is_timecode_strict) const
{
    UsdGeomXformOp::Type type = husdGetRotateOrderType(order);
    if( type == UsdGeomXformOp::TypeInvalid )
	return false;

    const GfVec3d &gf_r = GusdUT_Gf::Cast(r);
    return husdAddTransform(myWriteLock, findprims, timecode, 
        is_timecode_strict, myClearExistingFlag, name_suffix,
	type, gf_r);
}

bool
HUSD_Xform::addRotate(const HUSD_FindPrims &findprims,
	const UT_StringRef &name_suffix, HUSD_XformAxis xyz_axis, double angle,
	const HUSD_TimeCode &timecode, bool is_timecode_strict) const
{
    UsdGeomXformOp::Type type = husdGetRotateAxisType(xyz_axis);
    if( type == UsdGeomXformOp::TypeInvalid )
	return false;
    
    return husdAddTransform(myWriteLock, findprims, timecode, 
        is_timecode_strict, myClearExistingFlag, name_suffix,
	type, angle );
}

bool
HUSD_Xform::addScale(const HUSD_FindPrims &findprims,
	const UT_StringRef &name_suffix, const UT_Vector3D &s,
	const HUSD_TimeCode &timecode, bool is_timecode_strict) const
{
    const GfVec3d &gf_s = GusdUT_Gf::Cast(s);
    return husdAddTransform(myWriteLock, findprims, timecode, 
        is_timecode_strict, myClearExistingFlag, name_suffix,
	UsdGeomXformOp::TypeScale, gf_s );
}

bool
HUSD_Xform::addOrient(const HUSD_FindPrims &findprims,
	const UT_StringRef &name_suffix, const UT_QuaternionD &o,
	const HUSD_TimeCode &timecode, bool is_timecode_strict) const
{
    GfQuatd gf_q;
    GusdUT_Gf::Convert(o, gf_q);
    return husdAddTransform(myWriteLock, findprims, timecode, 
        is_timecode_strict, myClearExistingFlag, name_suffix,
        UsdGeomXformOp::TypeOrient, gf_q );
}

static inline bool
husdAddToXformOrder(HUSD_AutoWriteLock &lock, 
	const HUSD_FindPrims &findprims, 
	const UT_StringRef &attribname, bool is_inverse )
{
    return husdModifyXformable( lock, findprims, 
	    [&](UsdGeomXformable &xformable)
	    {
		HUSD_XformType  type;
		UT_StringHolder suffix;
		if( !HUSDgetXformTypeAndSuffix( type, suffix, attribname ))
		    return false;

		auto op_type = UsdGeomXformOp::Type( type );
		xformable.AddXformOp( op_type, UsdGeomXformOp::PrecisionDouble,
			TfToken(suffix), is_inverse );
		return true;
	    });
}

bool
HUSD_Xform::addToXformOrder(const HUSD_FindPrims &findprims,
	const UT_StringRef &attribname) const
{
    return husdAddToXformOrder(myWriteLock, findprims, 
	    attribname, false );
}

bool
HUSD_Xform::addInverseToXformOrder(const HUSD_FindPrims &findprims,
	const UT_StringRef &attribname) const
{
    return husdAddToXformOrder(myWriteLock, findprims, 
	    attribname, true );
}

bool
HUSD_Xform::setXformOrder(const HUSD_FindPrims &findprims,
	const UT_StringArray &xform_order) const
{
    return husdModifyXformable(myWriteLock, findprims,
	    [&](UsdGeomXformable &xformable)
	    {
		std::vector<UsdGeomXformOp> xform_ops;

		for (auto &&x : xform_order)
		    xform_ops.emplace_back( UsdGeomXformOp( UsdAttribute( 
			    xformable.GetPrim().GetAttribute( TfToken(x) ))));

		return xformable.SetXformOpOrder(xform_ops);
	    });
}

bool
HUSD_Xform::clearXformOrder(const HUSD_FindPrims &findprims) const
{
    return husdModifyXformable(myWriteLock, findprims,
	    [&](UsdGeomXformable &xformable)
	    {
		return xformable.ClearXformOpOrder();
	    });
}

bool
HUSD_Xform::setXformReset( const HUSD_FindPrims &findprims,
	bool reset) const
{
    return husdModifyXformable(myWriteLock, findprims,
	    [&](UsdGeomXformable &xformable)
	    {
		return xformable.SetResetXformStack(reset);
	    });
}

bool
HUSD_Xform::getIsTimeVarying() const
{
    return HUSDisTimeVarying(myTimeSampling);
}

