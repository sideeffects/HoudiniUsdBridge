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

#include "XUSD_PathSet.h"

PXR_NAMESPACE_OPEN_SCOPE

XUSD_PathSet::XUSD_PathSet()
{
}

XUSD_PathSet::XUSD_PathSet(const SdfPathSet &src)
    : SdfPathSet(src)
{
}

XUSD_PathSet::~XUSD_PathSet()
{
}

const XUSD_PathSet &
XUSD_PathSet::operator=(const SdfPathSet &src)
{
    SdfPathSet::operator=(src);
    return *this;
}

bool
XUSD_PathSet::contains(const SdfPath &path) const
{
    return (count(path) > 0);
}

bool
XUSD_PathSet::containsPathOrAncestor(const SdfPath &path,
        bool *contains) const
{
    auto it = lower_bound(path);

    // If the path is exactly in the set, we are done.
    if (*it == path)
    {
        if (contains)
            *contains = true;
        return true;
    }
    if (contains)
        *contains = false;

    // If the first entry is "after" the specified path, there is no way any
    // ancestors of the path are in our set.
    if (it == begin())
        return false;
    // Otherwise move the iterator back be one, so we have the last iterator
    // that is strictly less than the provided path. Check if this previous
    // iterator is an ancestor of the provided path.
    --it;
    if (path.HasPrefix(*it))
        return true;

    // The last value less than path isn't an ancestor of path, so there must
    // be no ancestors of path in the set.
    return false;
}

bool
XUSD_PathSet::containsPathOrDescendant(const SdfPath &path,
        bool *contains) const
{
    auto it = lower_bound(path);

    if (contains)
        *contains = false;

    // If every entry is "before" the specified path, there is no way any
    // descendants of the path are in our set.
    if (it == end())
        return false;

    // If the path is exactly in the set, we are done.
    if (*it == path)
    {
        if (contains)
            *contains = true;
        return true;
    }

    // Otheriwse check if the first entry "after" the specified path is a
    // descendant of the path. If any entry will be a descendant, this one
    // will be.
    if (it->HasPrefix(path))
        return true;

    // The last value less than path isn't an ancestor of path, so there must
    // be no ancestors of path in the set.
    return false;
}

PXR_NAMESPACE_CLOSE_SCOPE

