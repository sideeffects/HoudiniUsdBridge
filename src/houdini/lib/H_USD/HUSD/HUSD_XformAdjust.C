/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
 *
 * Produced by:
 *	Side Effects Software Inc.
 *	123 Front Street West, Suite 1401
 *	Toronto, Ontario
 *      Canada   M5J 2M2
 *	416-504-9876
 *
 */

#include "HUSD_XformAdjust.h"
#include "HUSD_TimeCode.h"
#include "XUSD_Data.h"
#include "XUSD_Utils.h"
#include <UT/UT_Map.h>
#include <pxr/usd/usdGeom/boundable.h>
#include <pxr/usd/usdGeom/xformCache.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/sdf/attributeSpec.h>
#include <pxr/usd/sdf/primSpec.h>

PXR_NAMESPACE_USING_DIRECTIVE
 
class husd_PrimInfo {
public:
                                 husd_PrimInfo()
                                     : myXform(1.0),
                                       myBaseXform(1.0),
                                       myHasBaseXform(false),
                                       myResetsXformStack(false),
                                       myTimeSampling(HUSD_TimeSampling::NONE)
                                 { }

    GfMatrix4d			 myXform;
    GfMatrix4d			 myBaseXform;
    std::vector<UsdGeomXformOp>	 myXformOps;
    bool			 myHasBaseXform;
    bool			 myResetsXformStack;
    HUSD_TimeSampling		 myTimeSampling;
};
typedef UT_Map<SdfPath, husd_PrimInfo> husd_PrimInfoMap;

class HUSD_XformAdjust::husd_XformAdjustPrivate {
public:
    husd_PrimInfoMap	 myPrimInfoMap;
    UsdTimeCode		 myTimeCode;
    HUSD_TimeSampling	 myTimeSampling;
};

HUSD_XformAdjust::HUSD_XformAdjust(HUSD_AutoAnyLock &lock,
	const HUSD_TimeCode &timecode)
    : myPrivate(new HUSD_XformAdjust::husd_XformAdjustPrivate()),
      myAuthorDefaultValues(false)
{
    static TfToken       theBaseXformToken = UsdGeomXformOp::GetOpName(
                                UsdGeomXformOp::TypeTransform);
    auto                 indata = lock.constData();
    UsdTimeCode          querytimecode = HUSDgetNonDefaultUsdTimeCode(timecode);

    // Store a map of prim path to xform for all primitives on the stage.
    myPrivate->myTimeCode = HUSDgetUsdTimeCode(timecode);
    myPrivate->myTimeSampling = HUSD_TimeSampling::NONE;
    if (indata && indata->isStageValid())
    {
	auto			 stage = indata->stage();
	auto			&map = myPrivate->myPrimInfoMap;
	UsdGeomXformCache	 xform_cache(querytimecode);

        for (auto prim : stage->Traverse())
	{
	    UsdGeomXformable	 xformable(prim);

            if (xformable)
            {
                husd_PrimInfo           &priminfo = map[prim.GetPath()];

                priminfo.myXform =
                    xform_cache.GetLocalToWorldTransform(prim);
                priminfo.myXformOps = xformable.GetOrderedXformOps(
                    &priminfo.myResetsXformStack);
                // Record the value of the xformOp:transform attribute, if
                // there is one being used in our xform.
                for (auto &&op : priminfo.myXformOps)
                {
                    if (op.GetName() == theBaseXformToken)
                    {
                        op.GetAs(&priminfo.myBaseXform, querytimecode);
                        priminfo.myHasBaseXform = true;
                        break;
                    }
                }
                priminfo.myTimeSampling =
		    HUSDgetWorldTransformTimeSampling(prim);
            }
	}
    }
}

HUSD_XformAdjust::~HUSD_XformAdjust()
{
}

static bool
hasXformAttribute(const SdfPrimSpecHandle &primspec)
{
    bool     hasxform = false;

    for (auto &&attrib : primspec->GetAttributes())
    {
        if (UsdGeomXformOp::IsXformOp(attrib->GetPath().GetNameToken()))
        {
            hasxform = true;
            break;
        }
    }

    return hasxform;
}

static void
adjustXformsForAuthoredPrim(
	const SdfPrimSpecHandle &primspec,
	const UsdStageRefPtr &stage,
	const husd_PrimInfoMap &map,
	const UsdTimeCode &timecode,
	HUSD_TimeSampling &used_time_sampling)
{
    static GfMatrix4d	 theIdentity(1.0);
    static TfToken       theBaseXformToken = UsdGeomXformOp::GetOpName(
                                UsdGeomXformOp::TypeTransform);
    bool                 adjust_children = true;

    // We only want to adjust transforms on prims where we have authored
    // at least one attribute.
    if (primspec->GetPath().IsPrimPath() &&
        !primspec->GetAttributes().empty())
    {
	// We only want to adjust transformable primitives
	UsdGeomXformable xformable(stage->GetPrimAtPath(primspec->GetPath()));

	if (xformable)
	{
	    auto	 priminfo = map.find(primspec->GetPath());
	    GfMatrix4d	 localxform;
	    bool	 resetsXformStack;
            bool         has_xform_attrib = hasXformAttribute(primspec);

            // Once we hit an xformable with an authored opinion on the layer,
            // stop traversing into children. We can't deal with nested prims
            // being authored with xforms anyway.
            adjust_children = false;

            // We only want to adjust xforms if we have authored xform
            // attributes on the primspec. We don't do the hasXformAttribute
            // check until those point because we still want to stop our
            // traversal as soon as we hit a primspec with any attributes
            // (be they transform attributes or not).
	    if (has_xform_attrib &&
                priminfo != map.end() &&
                priminfo->second.myXformOps.size() > 0)
	    {
		// We have transform info, including some local transforms,
		// for this exact prim. We want to preserve this existing
		// transform info.
		if (xformable.GetLocalTransformation(
			&localxform, &resetsXformStack, timecode))
		{
		    GfMatrix4d oldxform(priminfo->second.myXform);
		    GfMatrix4d oldxforminv(oldxform.GetInverse());
                    GfMatrix4d deltaxform = oldxforminv * localxform;
		    GfMatrix4d newxform = oldxform * deltaxform * oldxforminv;
                    UT_String xformsuffix;

		    xformable.SetXformOpOrder(priminfo->second.myXformOps,
			priminfo->second.myResetsXformStack);
                    // If the original xform had an xformOp:transform entry,
                    // make sure to reset that xformop's matrix back to the
                    // original value.
                    if (priminfo->second.myHasBaseXform)
                    {
                        bool resets = false;
                        auto xformops = xformable.GetOrderedXformOps(&resets);

                        for (auto &&op : xformops)
                        {
                            if (op.GetName() == theBaseXformToken)
                            {
                                op.Set(priminfo->second.myBaseXform, timecode);
                                break;
                            }
                        }
                        // We need a new unique transform name, because the
                        // default is already in use.
                        xformsuffix = "adjust1";
                        while (xformable.GetPrim().HasAttribute(
                                UsdGeomXformOp::GetOpName(
                                    UsdGeomXformOp::TypeTransform,
                                    TfToken(xformsuffix))))
                            xformsuffix.incrementNumberedName();
                    }
                    UsdGeomXformOp xformop = xformable.AddTransformOp(
                        UsdGeomXformOp::PrecisionDouble,
                        TfToken(xformsuffix));
                    if (xformop)
                        xformop.Set(newxform, timecode);
		}
		HUSDupdateTimeSampling(used_time_sampling,
			priminfo->second.myTimeSampling);
	    }
	    else if (has_xform_attrib)
	    {
		auto parentpath = primspec->GetPath().GetParentPath();
		auto parentinfo = map.find(parentpath);

		// If we don't have a direct parent with a stashed xform, look
		// for any ancestor, as we may have added many levels of
		// hierarchy to the stage since we stashed the xforms.
		while (parentpath != SdfPath::AbsoluteRootPath() &&
		       parentinfo == map.end())
		{
		    parentpath = parentpath.GetParentPath();
		    parentinfo = map.find(parentpath);
		}

		// No adjustment necessary if we don't have an xform for the
		// parent, or the parent has an identity xform.
		if (parentinfo != map.end())
		{
		    GfMatrix4d parentxform(parentinfo->second.myXform);

		    if (!GfIsClose(parentxform, theIdentity, SYS_FTOLERANCE))
		    {
			// Make sure we can get this prim's xform, and that it
			// hasn't been instructed to reset the local xform
			// stack (in which case we don't need to make any
			// adjustment).
			if (xformable.GetLocalTransformation(
				&localxform, &resetsXformStack, timecode) &&
			    !resetsXformStack)
			{
			    GfMatrix4d parentxforminv(parentxform.GetInverse());
                            GfMatrix4d deltaxform = parentxforminv * localxform;
			    GfMatrix4d newxform = parentxform * deltaxform;

			    newxform = newxform * parentxforminv;
			    xformable.ClearXformOpOrder();
			    xformable.AddTransformOp().Set(newxform, timecode);
			}
		    }
		    HUSDupdateTimeSampling(used_time_sampling,
			    parentinfo->second.myTimeSampling);

		}
	    }
	}
    }

    if (adjust_children)
    {
	// Only go to our children if we didn't have an authored xform. We
	// don't really support adjusting xforms when multiple levels of the
	// hierarchy had authored xforms, so there is no point trying to make
	// adjustments for children of adjusted prims.
	for (auto child : primspec->GetNameChildren())
	    adjustXformsForAuthoredPrim(child, stage, map,
		timecode, used_time_sampling);
    }
}

bool
HUSD_XformAdjust::adjustXformsForAuthoredPrims(
	const HUSD_AutoWriteLock &lock,
	const UT_StringHolder &authored_layer_path,
	const UT_StringMap<UT_StringHolder> &authored_layer_args) const
{
    auto	 outdata = lock.data();
    bool	 success = false;

    if (outdata && outdata->isStageValid())
    {
	auto			 stage = outdata->stage();
        UsdTimeCode              timecode = UsdTimeCode::Default();
	SdfLayerRefPtr		 authored_layer;

        if (!myAuthorDefaultValues)
            timecode = myPrivate->myTimeCode;

        // If we are given a path to a layer, load that layer to look for
        // authored primspec. Otherwise we should look for authored primspecs
        // on the active layer from our write lock.
        if (authored_layer_path.isstring())
        {
            SdfFileFormat::FileFormatArguments	 args;

            for (auto &&it : authored_layer_args)
                args[it.first.toStdString()] = it.second.toStdString();

            authored_layer = SdfLayer::Find(
                SdfLayer::CreateIdentifier(
                    authored_layer_path.toStdString(), args));
        }
        else
            authored_layer = outdata->activeLayer();

	if (authored_layer)
	{
	    adjustXformsForAuthoredPrim(authored_layer->GetPseudoRoot(),
		stage, myPrivate->myPrimInfoMap, timecode,
		myPrivate->myTimeSampling);
	    success = true;
	}
    }

    return success;
}

bool
HUSD_XformAdjust::getIsTimeVarying() const
{
    return HUSDisTimeVarying(myPrivate->myTimeSampling);
}

