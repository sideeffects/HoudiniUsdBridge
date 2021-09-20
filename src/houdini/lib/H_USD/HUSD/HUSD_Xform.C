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
#include <UT/UT_Quaternion.h>
#include <gusd/UT_Gf.h>
#include <pxr/usd/usdGeom/xformable.h>
#include <pxr/usd/usdGeom/xformCache.h>
#include <pxr/usd/usd/primRange.h>

using namespace UT::Literal;

PXR_NAMESPACE_USING_DIRECTIVE

HUSD_Xform::HUSD_Xform(HUSD_AutoWriteLock &lock)
    : myWriteLock(lock),
      myWarnBadPrimTypes(true),
      myCheckEditableFlag(false),
      myTimeSampling(HUSD_TimeSampling::NONE)
{
}

HUSD_Xform::~HUSD_Xform()
{
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
	HUSD_TimeSampling &used_time_sampling)
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

    UT_StringHolder		 xformopsuffix = name.isEmpty()
                                        ? "xform1"_sh : name;
    UsdGeomXformOp		 xformop;
    std::vector<UsdGeomXformOp>	 xformops;
    bool			 does_reset = false;

    if (xform_style == HUSD_XFORM_ABSOLUTE)
        xformable.ClearXformOpOrder();

    if (xform_style == HUSD_XFORM_OVERWRITE ||
        xform_style == HUSD_XFORM_OVERWRITE_APPEND ||
        xform_style == HUSD_XFORM_OVERWRITE_PREPEND)
    {
        // Look for the existing xform op with the provided name.
        xformops = xformable.GetOrderedXformOps(&does_reset);
        TfToken fullname = UsdGeomXformOp::GetOpName(
                UsdGeomXformOp::TypeTransform,
                TfToken(name.toStdString()));
        for (auto &&testop : xformops)
        {
            if (testop.GetOpName() == fullname)
            {
                xformop = testop;
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
        // Make sure we have a unique attribute name.
        HUSDgenerateUniqueTransformOpSuffix(
            xformopsuffix, xformable, UsdGeomXformOp::TypeTransform,
            name.isEmpty());
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
            GfMatrix4d xform =
                GusdUT_Gf::Cast(xform_entries[i].myXform);

            xformop.GetAttr().Clear();
            if (xform_style == HUSD_XFORM_WORLDSPACE)
            {
                // We want to apply the xform in world space, so we
                // have to compensate for our current xform.
                GfMatrix4d l2w_xform = xformable.
                    ComputeLocalToWorldTransform(ndusdtime);
                GfMatrix4d new_xform =
                    l2w_xform * xform * l2w_xform.GetInverse();

                // If we are setting a transform that is affected by an
                // animated transform, then we must set the transform
                // at the current time, rather than the default time.
                // The the LOP must be sure to set this transform again
                // whenever the time changes.
                auto sampling = HUSDgetWorldTransformTimeSampling(
                        xformable.GetPrim());
                if (HUSDisTimeSampled(sampling))
                {
                    xformop.Set(new_xform, ndusdtime);
                    HUSDupdateTimeSampling(used_time_sampling,sampling);
                }
                else
                    xformop.Set(new_xform, usdtime);
            }
            else
                xformop.Set(xform, usdtime);
        }
    }
}

bool
HUSD_Xform::applyXforms(const HUSD_FindPrims &findprims,
	const UT_StringRef &name,
	const UT_Matrix4D &xform,
	const HUSD_TimeCode &timecode,
	HUSD_XformStyle xform_style) const
{
    auto	 outdata = myWriteLock.data();
    bool	 success = false;

    if (outdata && outdata->isStageValid())
    {
	auto		 stage = outdata->stage();
	HUSD_XformEntry	 xform_entry = {xform, timecode};
        HUSD_Info        info(myWriteLock);

	for (auto &&sdfpath : findprims.getExpandedPathSet().sdfPathSet())
	{
	    husdApplyXform(sdfpath, stage, name,
                &xform_entry, 1, xform_style,
                myWarnBadPrimTypes, myCheckEditableFlag, myTimeSampling);
	}
	success = true;
    }

    return success;
}

bool
HUSD_Xform::applyXforms(const HUSD_XformEntryMap &xform_map,
	const UT_StringRef &name,
	HUSD_XformStyle xform_style) const
{
    auto	 outdata = myWriteLock.data();
    bool	 success = false;

    if (outdata && outdata->isStageValid())
    {
	auto				 stage = outdata->stage();

	for (auto it = xform_map.begin(); it != xform_map.end(); ++it)
	{
	    SdfPath	 sdfpath = HUSDgetSdfPath(it->first);

	    husdApplyXform(sdfpath, stage, name,
                it->second.data(), it->second.size(), xform_style,
                myWarnBadPrimTypes, myCheckEditableFlag, myTimeSampling);
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
        const HUSD_TimeCode &timecode) const
{
    auto	 outdata = myWriteLock.data();
    bool	 success = false;

    if (outdata && outdata->isStageValid())
    {
	auto                 stage = outdata->stage();

	for (auto &&sdfpath : findprims.getExpandedPathSet().sdfPathSet())
	{
            UT_Matrix4D          targetprimxform(0.0);
            UT_Matrix4D          prelookatxform(0.0);
            UT_Matrix4D          xform(1.0);
            HUSD_TimeSampling    lookat_ts = HUSD_TimeSampling::NONE;
            HUSD_TimeSampling    this_ts = HUSD_TimeSampling::NONE;

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

                // Get the xform of this prim.
                prelookatxform = info.getWorldXform(
                    sdfpath.GetAsString(), timecode, &this_ts);
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

            // If the input transforms we rely on are time varying, we need
            // to author a time sample for the lookat.
            HUSD_TimeCode        timecode_copy(timecode);
            if (HUSDisTimeSampled(lookat_ts) || HUSDisTimeSampled(this_ts))
                timecode_copy = timecode.getNonDefaultTimeCode();

            HUSD_XformEntry	 xform_entry = {xform, timecode_copy};

            husdApplyXform(sdfpath, stage, "lookat",
                &xform_entry, 1, HUSD_XFORM_APPEND,
                myWarnBadPrimTypes, myCheckEditableFlag, myTimeSampling);
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
	const UT_StringRef &name_suffix, UsdGeomXformOp::Type type,
	const T &value )
{
    return husdModifyXformable(lock, findprims, 
	    [&](UsdGeomXformable &xformable)
	    {
		UsdGeomXformOp xform_op = xformable.AddXformOp( type,
			UsdGeomXformOp::PrecisionDouble, TfToken(name_suffix));

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
	    is_timecode_strict, name_suffix, UsdGeomXformOp::TypeTransform,
	    gf_xform);
}

bool
HUSD_Xform::addTranslate(const HUSD_FindPrims &findprims,
	const UT_StringRef &name_suffix, const UT_Vector3D &xform,
	const HUSD_TimeCode &timecode, bool is_timecode_strict) const
{
    const GfVec3d &gf_t = GusdUT_Gf::Cast(xform);
    return husdAddTransform(myWriteLock, findprims, timecode, 
	    is_timecode_strict, name_suffix, UsdGeomXformOp::TypeTranslate, 
	    gf_t);
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
	    is_timecode_strict, name_suffix, type, gf_r);
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
	    is_timecode_strict, name_suffix, type, angle );
}

bool
HUSD_Xform::addScale(const HUSD_FindPrims &findprims,
	const UT_StringRef &name_suffix, const UT_Vector3D &s,
	const HUSD_TimeCode &timecode, bool is_timecode_strict) const
{
    const GfVec3d &gf_s = GusdUT_Gf::Cast(s);
    return husdAddTransform(myWriteLock, findprims, timecode, 
	    is_timecode_strict, name_suffix, UsdGeomXformOp::TypeScale, gf_s );
}

bool
HUSD_Xform::addOrient(const HUSD_FindPrims &findprims,
	const UT_StringRef &name_suffix, const UT_QuaternionD &o,
	const HUSD_TimeCode &timecode, bool is_timecode_strict) const
{
    GfQuatd gf_q;
    GusdUT_Gf::Convert(o, gf_q);
    return husdAddTransform(myWriteLock, findprims, timecode, 
	    is_timecode_strict, name_suffix, UsdGeomXformOp::TypeOrient, gf_q );
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

