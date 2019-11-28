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
husdEditRel(HUSD_AutoWriteLock &lock, const SdfPath &sdfpath,
	F config_fn)
{
    auto outdata = lock.data();
    if (!outdata || !outdata->isStageValid())
	return false;

    auto stage(outdata->stage());
    UsdPrim prim = stage->OverridePrim(sdfpath);

    if (!prim || !config_fn(prim))
        return false;

    return true;
}
bool
HUSD_SetRelationships::setRelationship(const UT_StringRef &primpath,
	const UT_StringRef &rel_name,
	const UT_StringArray &target_paths) const
{
    TfToken		rel(rel_name.toStdString());
    SdfPathVector	sdf_target_paths(HUSDgetSdfPaths(target_paths));
    SdfPath             sdf_primpath(HUSDgetSdfPath(primpath));

    for (auto &&sdf_target_path : sdf_target_paths)
    {
        if (sdf_primpath == sdf_target_path)
        {
            HUSD_ErrorScope::addError(HUSD_ERR_RELATIONSHIP_CANT_TARGET_SELF,
                sdf_target_path.GetString().c_str());
            return false;
        }
    }

    return husdEditRel(myWriteLock, sdf_primpath, [&](UsdPrim &prim)
	{
	    return prim.CreateRelationship(rel).SetTargets(sdf_target_paths);
	});
}

bool
HUSD_SetRelationships::blockRelationship(const UT_StringRef &primpath,
	const UT_StringRef &rel_name) const
{
    TfToken		rel(rel_name.toStdString());
    SdfPath             sdf_primpath(HUSDgetSdfPath(primpath));

    return husdEditRel(myWriteLock, sdf_primpath, [&](UsdPrim &prim)
	{
	    return prim.CreateRelationship(rel).BlockTargets();
	});
}

bool
HUSD_SetRelationships::addRelationshipTarget(const UT_StringRef &primpath,
	const UT_StringRef &rel_name,
	const UT_StringRef &target_path) const
{
    TfToken	        rel(rel_name.toStdString());
    SdfPath	        sdf_target_path(HUSDgetSdfPath(target_path));
    SdfPath             sdf_primpath(HUSDgetSdfPath(primpath));

    if (sdf_primpath == sdf_target_path)
    {
        HUSD_ErrorScope::addError(HUSD_ERR_RELATIONSHIP_CANT_TARGET_SELF,
            sdf_target_path.GetString().c_str());
        return false;
    }

    return husdEditRel(myWriteLock, sdf_primpath, [&](UsdPrim &prim)
	{
	    return prim.CreateRelationship(rel).AddTarget(sdf_target_path);
	});
}

bool
HUSD_SetRelationships::removeRelationshipTarget(const UT_StringRef &primpath,
	const UT_StringRef &rel_name,
	const UT_StringRef &target_path) const
{
    TfToken	        rel(rel_name.toStdString());
    SdfPath	        sdf_target_path(HUSDgetSdfPath(target_path));
    SdfPath             sdf_primpath(HUSDgetSdfPath(primpath));

    return husdEditRel(myWriteLock, sdf_primpath, [&](UsdPrim &prim)
	{
	    return prim.CreateRelationship(rel).RemoveTarget(sdf_target_path);
	});
}

