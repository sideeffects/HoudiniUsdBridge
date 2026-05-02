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

#include "HUSD_EditRelocates.h"
#include "HUSD_ErrorScope.h"
#include "XUSD_Data.h"
#include "XUSD_Utils.h"
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/base/vt/types.h>

PXR_NAMESPACE_USING_DIRECTIVE

HUSD_EditRelocates::HUSD_EditRelocates(HUSD_AutoWriteLock &lock) 
    : myWriteLock(lock)
{
}

HUSD_EditRelocates::~HUSD_EditRelocates()
{
}

bool
HUSD_EditRelocates::addRelocateError(const HUSD_Path &from,
        const HUSD_Path &to,
        const char *msg) const
{
    if (myReportingSeverity > UT_ERROR_NONE)
    {
        UT_WorkBuffer msgbuf;
        msgbuf.sprintf("'%s' to '%s':\n%s",
            from.pathStr().c_str(), to.pathStr().c_str(), msg);
        if (myReportingSeverity >= UT_ERROR_ABORT)
            HUSD_ErrorScope::addError(HUSD_ERR_CANT_RELOCATE, msgbuf.c_str());
        else if (myReportingSeverity >= UT_ERROR_WARNING)
            HUSD_ErrorScope::addWarning(HUSD_ERR_CANT_RELOCATE, msgbuf.c_str());
        else
            HUSD_ErrorScope::addMessage(HUSD_ERR_CANT_RELOCATE, msgbuf.c_str());
    }

    return false;
}

bool
HUSD_EditRelocates::validateRelocate(const HUSD_Path &from,
        const HUSD_Path &to) const
{
    if (from.isEmpty())
        return addRelocateError(from, to,
            "Source path cannot be empty.");
    // NOTE: the destination path _can_ be empty ("deletes" the source prim).
    if (from.isAbsoluteRootPath())
        return addRelocateError(from, to,
            "Source path cannot be '/'.");
    if (to.isAbsoluteRootPath())
        return addRelocateError(from, to,
            "Destination path cannot be '/'.");
    if (from.sdfPath().IsRootPrimPath())
        return addRelocateError(from, to,
            "Source path cannot be a root prim.");
    // NOTE: the destination path _can_ be a root prim.

    // Now the tricky one... If the source prim is defined in the root layer
    // stack, trying to relocate it effectively deletes the source prim but
    // doesn't put anything in the dest prim location. This is very confusing
    // and unexpected, so don't allow it.
    auto    outdata = myWriteLock.data();
    UsdPrim fromprim = outdata->stage()->GetPrimAtPath(from.sdfPath());
    if (!fromprim)
        return addRelocateError(from, to, "Source prim doesn't exist.");
    auto primindex = fromprim.GetPrimIndex();
    auto rootnode = primindex.GetRootNode();
    auto childrenrange = rootnode.GetChildrenRange();
    if (childrenrange.first == childrenrange.second)
        return addRelocateError(from, to,
            "Source prim only exists on root layer stack.");

    return true;
}

bool
HUSD_EditRelocates::setRelocates(
        const UT_StringMap<UT_StringHolder>& relocatesMap) const
{
    auto                           outdata = myWriteLock.data();
    bool                           success = false;
    std::vector<SdfRelocate>       existingrelocates;
    std::vector<SdfRelocate>       newrelocates;

    if (outdata && outdata->isStageValid())
    {
        SdfLayerRefPtr          activelayer = outdata->activeLayer();

        existingrelocates = activelayer->GetRelocates();
        for (auto &relocate: relocatesMap)
        {
            SdfPath sdfsourcepath;
            SdfPath sdfdestpath;

            sdfsourcepath = SdfPath(relocate.first.toStdString());
            if (relocate.second.isstring())
                sdfdestpath = SdfPath(relocate.second.toStdString());

            if (validateRelocate(sdfsourcepath, sdfdestpath))
            {
                SdfRelocate relocatespair = std::pair<SdfPath, SdfPath>(
                        sdfsourcepath, sdfdestpath);
                newrelocates.push_back(relocatespair);
            }
        }

        // Find union of the new and existing relocates. We can safely assume that
        // this union will not cause issues because of the input validation done in
        // the LOP_Relocate cookMyLop method.
        std::vector<SdfRelocate>   unionrelocates;
        std::set_union(existingrelocates.cbegin(), existingrelocates.cend(),
                       newrelocates.cbegin(), newrelocates.cend(),
                       std::back_inserter(unionrelocates));

        activelayer->SetRelocates(SdfRelocates(unionrelocates));
        success = true;
    }

    return success;
}
