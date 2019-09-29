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

#include "HUSD_SetRelationships.h"
#include "HUSD_ErrorScope.h"
#include "HUSD_FindPrims.h"
#include "XUSD_Data.h"
#include "XUSD_Utils.h"
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/relationship.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/base/tf/token.h>

PXR_NAMESPACE_USING_DIRECTIVE

HUSD_SetRelationships::HUSD_SetRelationships(HUSD_AutoWriteLock &lock)
    : myWriteLock(lock)
{
}

HUSD_SetRelationships::~HUSD_SetRelationships()
{
}

template <typename F>
static inline bool
husdEditRel(HUSD_AutoWriteLock &lock, const HUSD_FindPrims &findprims,
	F config_fn)
{
    auto outdata = lock.data();
    if (!outdata || !outdata->isStageValid())
	return false;

    auto stage(outdata->stage());
    bool success(true);
    for (auto &&sdfpath : findprims.getExpandedPathSet())
    {
	UsdPrim prim = stage->OverridePrim(sdfpath);
	if (!prim || !config_fn(prim))
	    success = false;
    }

    return success;
}
bool
HUSD_SetRelationships::setRelationship(const UT_StringRef &primpath,
	const UT_StringRef &rel_name,
	const UT_StringArray &target_paths) const
{
    HUSD_FindPrims findprims(myWriteLock, primpath);
    
    return setRelationship(findprims, rel_name, target_paths);
}

bool
HUSD_SetRelationships::setRelationship(const HUSD_FindPrims &findprims,
	const UT_StringRef &rel_name,
	const UT_StringArray &target_paths) const
{
    TfToken		rel(rel_name.toStdString());
    SdfPathVector	paths(HUSDgetSdfPaths(target_paths));
    const XUSD_PathSet &foundset = findprims.getExpandedPathSet();

    for (auto &&path : paths)
    {
        if (foundset.find(path) != foundset.end())
        {
            HUSD_ErrorScope::addError(HUSD_ERR_RELATIONSHIP_CANT_TARGET_SELF,
                path.GetString().c_str());
            return false;
        }
    }

    return husdEditRel(myWriteLock, findprims, [&](UsdPrim &prim)
	{
	    return prim.CreateRelationship(rel).SetTargets(paths);
	});
}

bool
HUSD_SetRelationships::blockRelationship(const HUSD_FindPrims &findprims,
	const UT_StringRef &rel_name) const
{
    TfToken		rel(rel_name.toStdString());

    return husdEditRel(myWriteLock, findprims, [&](UsdPrim &prim)
	{
	    return prim.CreateRelationship(rel).BlockTargets();
	});
}

bool
HUSD_SetRelationships::addRelationshipTarget(const HUSD_FindPrims &findprims,
	const UT_StringRef &rel_name,
	const UT_StringRef &target_path) const
{
    TfToken	        rel(rel_name.toStdString());
    SdfPath	        path(HUSDgetSdfPath(target_path));
    const XUSD_PathSet &foundset = findprims.getExpandedPathSet();

    if (foundset.find(path) != foundset.end())
    {
        HUSD_ErrorScope::addError(HUSD_ERR_RELATIONSHIP_CANT_TARGET_SELF,
            path.GetString().c_str());
        return false;
    }

    return husdEditRel(myWriteLock, findprims, [&](UsdPrim &prim)
	{
	    return prim.CreateRelationship(rel).AddTarget(path);
	});
}

bool
HUSD_SetRelationships::removeRelationshipTarget(const HUSD_FindPrims &findprims,
	const UT_StringRef &rel_name,
	const UT_StringRef &target_path) const
{
    TfToken	        rel(rel_name.toStdString());
    SdfPath	        path(HUSDgetSdfPath(target_path));

    return husdEditRel(myWriteLock, findprims, [&](UsdPrim &prim)
	{
	    return prim.CreateRelationship(rel).RemoveTarget(path);
	});
}

