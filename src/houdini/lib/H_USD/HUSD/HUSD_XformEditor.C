/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
 *
 * Produced by:
 *	Side Effects Software Inc
 *	123 Front Street West, Suite 1401
 *	Toronto, Ontario
 *	Canada   M5J 2M2
 *	416-504-9876
 *
 * NAME:	HUSD library (C++)
 *
 */

#include "HUSD_XformEditor.h"
#include "HUSD_DataHandle.h"
#include "HUSD_Info.h"
#include "HUSD_PathSet.h"
#include "HUSD_PointPrim.h"
#include "HUSD_TimeCode.h"
#include "HUSD_Utils.h"
#include "HUSD_Xform.h"
#include <XUSD_Data.h>
#include <GA/GA_Names.h>
#include <GU/GU_Detail.h>
#include <GA/GA_SplittableRange.h>
#include <UN/UN_NodeUtils.h>
#include <UT/UT_Interrupt.h>
#include <pxr/usd/sdf/layer.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace
{
    struct husdInstanceInfo
    {
	UT_Int64Array            myIds;
	UT_Matrix4DArray         myXforms;
    };
    typedef UT_SortedMap<UT_StringHolder, GA_Offset> husdPrimPathMap;
    typedef UT_Map<UT_StringHolder, husdInstanceInfo> husdInstXformMap;

    // husdGetXformWithEdits gets the prim's world space transform as it
    // will appear once each ancestor's delta is applied. This does not
    // include the prim's own delta.
    UT_Matrix4D
    husdGetXformWithEdits(HUSD_AutoAnyLock &lock,
			 const UT_StringRef &primpath,
			 const HUSD_Info &info,
			 const HUSD_TimeCode &time,
			 const husdPrimPathMap &primpathmap,
			 const GA_ROHandleM4D &xformattrib,
			 const husdInstXformMap &priminstxforms)
    {
	UT_StringArray	 parts;
	UT_WorkBuffer	 parentpath;
	UT_String	 pathcopy;
	UT_StringHolder	 pathpart;
	UT_Matrix4D      xform(1.0);
        int64            instanceid = -1;

        HUSDsplitInstanceIdAndPath(primpath, pathpart, instanceid);
        // Couldn't get a prim path. Just return.
        if (!pathpart.isstring())
            return xform;

	pathcopy.harden(pathpart);
	int numparts = pathcopy.tokenize(parts, "/");
	UT_Matrix4D localxform;

	int i = 1;
	for (auto &&part : parts)
	{
	    parentpath.append('/');
	    parentpath.append(part);

	    localxform = info.getLocalXform(parentpath, time);

            // If the local xform signals a "reset", just assign the local
            // xform to our compound xform. Otherwise, multiply the compound
            // xform by the localxform, unless the localxform is zero
            // (indicating a prim that doesn't have an xform, like a Scope).
	    if (info.isXformReset(parentpath))
		xform = localxform;
	    else if (!localxform.isZero())
		xform = localxform * xform;

	    if (i < numparts || instanceid >= 0)
	    {
		auto parentit = primpathmap.find(parentpath);
		if (parentit != primpathmap.end())
		    xform = xformattrib.get(parentit->second) * xform;
	    }
	    i++;
	}

	if (instanceid >= 0)
	{
	    auto &&xformsit = priminstxforms.find(pathpart);
	    if (xformsit != priminstxforms.end())
	    {
	        // Doing a linear search for the instance ids is far from
	        // ideal. But we only do this for the selected things that
	        // we are going to transform, so it's probably better than
	        // taking the time to build a lookup structure. Most of the
	        // time the ids will probably be empty anyway.
	        auto &ids = xformsit->second.myIds;
	        int64 idx = instanceid;
	        if (!ids.isEmpty())
	        {
                    for (int i = 0, n = ids.size(); i < n; i++)
                    {
                        if (ids[i] == instanceid)
                        {
                            idx = i;
                            break;
                        }
                    }
	        }
	        if (idx >=0 && idx < xformsit->second.myXforms.size())
	            xform = xformsit->second.myXforms[idx] * xform;
	    }
	}

	return xform;
    }
}

GU_DetailHandle
HUSD_XformEditor::getDeltaWithParmXforms(
        const UT_StringArray &changepaths,
        const HUSD_TimeCode &timecode,
        const GU_ConstDetailHandle &delta,
        const UT_Matrix4D *local_parm_xform,
        const UT_Matrix4D *local_parm_pivot_xform,
        const UT_Matrix4D *global_parm_xform,
        bool set_pivot_on_primary_prim,
        HUSD_AutoAnyLock &lock,
        bool exclude_edits)
{
    GU_DetailHandle	out_gdh;
    husdInstXformMap	priminstxforms;
    UT_SortedStringSet	paths;
    UT_StringHolder	primaryprimpath;
    UT_WorkBuffer       tmppath;

    for (int i = 0, n = changepaths.size(); i < n; i++)
    {
        const UT_StringHolder &token = changepaths[i];
        UT_StringHolder primpath;
        int64 instanceid = -1;
        HUSDsplitInstanceIdAndPath(token, primpath, instanceid);

        if (instanceid >= 0)
        {
            auto &&it = priminstxforms.find(primpath);
            if (it == priminstxforms.end())
            {
                auto &&xforms = priminstxforms[primpath];

                HUSD_PointPrim::extractTransforms(
                    lock,
                    primpath,
                    xforms.myXforms,
                    &xforms.myIds,
                    nullptr,
                    timecode);
            }

            tmppath.format("{}[{}]", primpath, instanceid);
            paths.insert(tmppath);
            // The last one is the "primary" one used for local transform mode.
            // The viewport state and LOP_XformEditor both require this.
            if (i == n-1)
                primaryprimpath = tmppath;
        }
        else
        {
            paths.insert(primpath);
            // The last one is the "primary" one used for local transform mode.
            // The viewport state and LOP_XformEditor both require this.
            if (i == n-1)
                primaryprimpath = primpath;
        }
    }

    out_gdh.allocateAndSet(new GU_Detail());
    GU_DetailHandleAutoWriteLock out_gdl(out_gdh);
    if (delta)
    {
	GU_DetailHandleAutoReadLock	 delta_gdl(delta);

	if (delta_gdl)
	    out_gdl->merge(*delta_gdl);
    }

    GA_RWHandleS primpath_attrib(out_gdl.getGdp(), GA_ATTRIB_POINT, "primpath");
    GA_RWHandleM4D xformattrib(out_gdl.getGdp(), GA_ATTRIB_POINT, "xform");
    auto path_from_attrib_fn = [&](GA_Offset ptoff)
    {
        // Get the selection path from the path attribute, and simplify
        // it to remove any ":prototype" path in the instance id.
        UT_StringHolder path = primpath_attrib.get(ptoff);
        UT_StringHolder primpath;
        int64 instanceid = -1;
        HUSDsplitInstanceIdAndPath(path, primpath, instanceid);
        if (instanceid >= 0)
            path.format("{}[{}]", primpath, instanceid);
        return path;
    };

    if (!primpath_attrib.isValid())
	primpath_attrib = out_gdl->addStringTuple(
	    GA_ATTRIB_POINT, "primpath", 1);
    if (!xformattrib.isValid())
	xformattrib = out_gdl->addFloatTuple(
	    GA_ATTRIB_POINT, "xform", 16, GA_Defaults(GA_Defaults::matrix4()));
    GA_RWHandleI pivotsetattrib(out_gdl.getGdp(), GA_ATTRIB_POINT, "pset");
    GA_RWHandleV3D pivotattrib(out_gdl.getGdp(), GA_ATTRIB_POINT, "p");
    GA_RWHandleV3D pivotrotattrib(out_gdl.getGdp(), GA_ATTRIB_POINT, "pr");
    if (set_pivot_on_primary_prim)
    {
        if (!pivotsetattrib.isValid())
            pivotsetattrib = out_gdl->addIntTuple(GA_ATTRIB_POINT, "pset", 1);
        if (!pivotattrib.isValid())
            pivotattrib = out_gdl->addFloatTuple(GA_ATTRIB_POINT, "p", 3);
        if (!pivotrotattrib.isValid())
            pivotrotattrib = out_gdl->addFloatTuple(GA_ATTRIB_POINT, "pr", 3);
    }

    if (paths.empty())
	return out_gdh;

    husdPrimPathMap		 primpathmap;
    for (GA_Offset off : out_gdl->getPointRange())
    {
	UT_StringHolder path = path_from_attrib_fn(off);
	primpathmap[path] = off;
    }

    HUSD_Info	 		 info(lock);
    UT_Matrix4D			 deltaxform(1.0);
    UT_Matrix4D			 xform(1.0);
    UT_Matrix4D			 inputworldxform(1.0);
    UT_Matrix4D			 parmxform(1.0);
    UT_Matrix4D			 localpivotxform(1.0);
    UT_Vector3D                  set_primary_prim_p(1.0);
    UT_Vector3D                  set_primary_prim_pr(1.0);

    if (local_parm_pivot_xform)
    {
        // If we have been passed a local pivot xform, it is either
        // because we are using a local parm xform, or we have been
        // asked to set the pivot on the primary prim (whether the
        // xform we are dealing with is a world or local xform).
        UT_ASSERT(local_parm_xform || set_pivot_on_primary_prim);
        // build full primitive xform
        UT_Matrix4D primworldixform = husdGetXformWithEdits(lock,
            primaryprimpath,
            info,
            timecode,
            primpathmap,
            xformattrib,
            priminstxforms);

        // ...including local delta
        auto ptit = primpathmap.find(primaryprimpath);
        if (ptit != primpathmap.end())
        {
            GA_Offset ptoff = ptit->second;
            primworldixform = xformattrib.get(ptoff) * primworldixform;
        }

        // pivot in primitive's local space
        primworldixform.invert();
        if (local_parm_pivot_xform)
            localpivotxform = *local_parm_pivot_xform * primworldixform;

        if (set_pivot_on_primary_prim)
        {
            UT_Vector3D s;
            // Get the pivots as UT_Vector3Ds.
            localpivotxform.explode(UT_XformOrder(UT_XformOrder::SRT),
                set_primary_prim_pr, s, set_primary_prim_p);
            set_primary_prim_pr.radToDeg();
        }
    }

    // Get the local transform
    if (local_parm_xform)
    {
	parmxform = *local_parm_xform;
        if (!exclude_edits)
        {
            UT_Matrix4D invlocalpivotxform = localpivotxform;
            invlocalpivotxform.invert();
            parmxform = invlocalpivotxform * parmxform * localpivotxform;
        }
    }
    else
    {
        if (global_parm_xform)
            parmxform = *global_parm_xform;
    }

    for (auto &&primpath : paths)
    {
        GA_Offset   ptoff;
        auto        ptit = primpathmap.find(primpath);

        if (ptit == primpathmap.end())
        {
            ptoff = out_gdl->appendPoint();
            primpath_attrib.set(ptoff, primpath);
        }
        else
            ptoff = ptit->second;
        deltaxform = xformattrib.get(ptoff);

	// Get the local transform
	if (local_parm_xform)
	{
	    // pre-multiply parmxform in local pivot space
	    xform = parmxform * deltaxform;
	}
	else
	{
	    // Since deltas are stored in local space, and the parmxform is in
	    // world space, we first get the world space transform of the prim,
	    // transform by parmxform and transform back to local space.
	    if (exclude_edits)
                inputworldxform = info.getParentXform(primpath, timecode);
            else
                inputworldxform = husdGetXformWithEdits(lock,
                    primpath,
                    info,
                    timecode,
                    primpathmap,
                    xformattrib,
                    priminstxforms);

            // premultiply the prim's own deltaxform.
            xform = deltaxform * inputworldxform * parmxform;
            // transform back to local space
            inputworldxform.invert();
            xform *= inputworldxform;
	}

        // Set the pivot data on the primary prim if requested. We don't
        // support setting pivot information on instances right now.
        if (set_pivot_on_primary_prim &&
            primpath == primaryprimpath &&
            primpath.findCharIndex('[') < 0)
        {
            pivotsetattrib.set(ptoff, 1);
            pivotattrib.set(ptoff, set_primary_prim_p);
            pivotrotattrib.set(ptoff, set_primary_prim_pr);
        }
	xformattrib.set(ptoff, xform);
    }

    return out_gdh;
}

UT_StringHolder
HUSD_XformEditor::applyEdits(HUSD_AutoWriteLock &writelock,
        const GU_Detail *delta,
        bool apply_inverse_xforms,
        const UT_StringRef &xform_description,
        const HUSD_XformStyle xform_style,
        const HUSD_TimeCode &timecode,
        std::function<bool(const UT_StringRef &path)> allow_edit_fn,
        HUSD_PathSet &modified_paths,
        bool &time_varying)
{
    UT_AutoInterrupt         boss("Applying Transforms");
    HUSD_Xform		     xformer(writelock);
    const GA_ROHandleS	     primpath_attrib(delta, GA_ATTRIB_POINT, "primpath");
    const GA_ROHandleM4D     xformattrib(delta, GA_ATTRIB_POINT, "xform");
    const GA_ROHandleI       pivotsetattrib(delta, GA_ATTRIB_POINT, "pset");
    const GA_ROHandleV3D     pivotattrib(delta, GA_ATTRIB_POINT, "p");
    const GA_ROHandleV3D     pivotrotattrib(delta, GA_ATTRIB_POINT, "pr");

    // Missing the primpath or xform attribute means there are
    // no edits to apply.
    time_varying = false;
    if (primpath_attrib.isInvalid())
        return UT_StringHolder::theEmptyString;
    if (xformattrib.isInvalid())
	return UT_StringHolder::theEmptyString;

    UT_Map<UT_StringHolder, husdInstanceInfo> priminstids;
    HUSD_XformEntryMap xform_map;
    bool has_pset = pivotsetattrib.isValid();
    bool apply_p = has_pset && pivotattrib.isValid();
    bool apply_pr = has_pset && pivotrotattrib.isValid();

    UTserialFor(GA_SplittableRange(delta->getPointRange()),
        [&](const GA_SplittableRange &r)
        {
            GA_Offset startptoff, endptoff;

            for (GA_Iterator ptit(r);
                 ptit.blockAdvance(startptoff, endptoff); )
            {
                UT_WorkBuffer pathbuf;
                if (boss.wasInterrupted())
                    return;

                for (GA_Offset ptoff = startptoff;
                     ptoff < endptoff;
                     ++ptoff)
                {
                    UT_StringHolder ptpath = primpath_attrib.get(ptoff);
                    UT_StringHolder primpath;
                    int64 instanceid = -1;
                    UT_Matrix4D xform = xformattrib.get(ptoff);
                    HUSDsplitInstanceIdAndPath(ptpath, primpath, instanceid);

                    if (allow_edit_fn(primpath))
                    {
                        if (apply_inverse_xforms)
                            xform.invert();

                        if (instanceid >= 0)
                        {
                            auto &&instinfo = priminstids[primpath];
                            modified_paths.insert(primpath);
                            instinfo.myIds.append(instanceid);
                            instinfo.myXforms.append(xform);
                        }
                        else
                        {
                            xform_map[primpath].append({
                                xform, timecode,
                                has_pset && pivotsetattrib.get(ptoff),
                                (apply_p && pivotsetattrib.get(ptoff))
                                    ? pivotattrib.get(ptoff)
                                    : UT_Vector3D(0.0),
                                (apply_pr && pivotsetattrib.get(ptoff))
                                    ? pivotrotattrib.get(ptoff)
                                    : UT_Vector3D(0.0)});
                            modified_paths.insert(primpath);
                        }
                    }
                }
            }
        });

    if (boss.wasInterrupted())
        return "Operation interrupted";

    for (auto &&pair : priminstids)
    {
	HUSD_PointPrim::transformInstances(
	    writelock,
	    pair.first,
	    pair.second.myIds,
	    pair.second.myXforms,
	    timecode);
    }

    if (boss.wasInterrupted())
	return "Operation interrupted";

    xformer.applyXforms(xform_map, xform_description, xform_style);
    time_varying = xformer.getIsTimeVarying();

    if (boss.wasInterrupted())
        return "Operation interrupted";

    return UT_StringHolder::theEmptyString;
}
