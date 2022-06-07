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

#include "HUSD_ExpansionState.h"
#include <UT/UT_IStream.h>
#include <UT/UT_JSONParser.h>
#include <UT/UT_JSONValue.h>
#include <UT/UT_JSONWriter.h>

static constexpr UT_StringLit	 theExpandedKey("expanded");
static constexpr UT_StringLit	 theChildrenKey("children");
static constexpr UT_StringLit	 thePinnedKey("pinned");

HUSD_ExpansionState::HUSD_ExpansionState()
{
    // Always start with the root node expanded.
    setExpanded(HUSD_Path::theRootPrimPath, false, true);
}

HUSD_ExpansionState::~HUSD_ExpansionState()
{
}

void
HUSD_ExpansionState::setExpanded(const HUSD_Path &path,
        bool pinned,
        bool expanded)
{
    if (pinned)
    {
        if (expanded)
            myExpandedPinnedPaths.insert(path);
        else
            myExpandedPinnedPaths.erase(path);
    }
    else
    {
        if (expanded)
            myExpandedScenePaths.insert(path);
        else
            myExpandedScenePaths.erase(path);
    }
}

exint
HUSD_ExpansionState::getMemoryUsage() const
{
    return myExpandedPinnedPaths.size() * sizeof(HUSD_Path) +
            myExpandedScenePaths.size() * sizeof(HUSD_Path);
}

void
HUSD_ExpansionState::clear()
{
    myExpandedPinnedPaths.clear();
    myExpandedScenePaths.clear();
}

void
HUSD_ExpansionState::copy(const HUSD_ExpansionState &src)
{
    myExpandedPinnedPaths = src.myExpandedPinnedPaths;
    myExpandedScenePaths = src.myExpandedScenePaths;
}

bool
HUSD_ExpansionState::save(UT_JSONWriter &writer,
        const HUSD_PathSet &paths,
        bool allow_saving_indirect_descendants,
        HUSD_PathSet::iterator &iter) const
{
    bool	 success = true;

    if (iter != paths.end())
    {
        success &= writer.jsonKeyToken(theExpandedKey.asRef());
        success &= writer.jsonBool(true);

        HUSD_PathSet::iterator   prev = iter;
        HUSD_Path                prevpath = *prev;
        bool                     foundchild = false;

        ++iter;
        while(iter != paths.end() &&
              (allow_saving_indirect_descendants ||
               (*iter).parentPath() == *prev))
        {
            // Any direct children we want to write out.
            if (!foundchild)
            {
                success &= writer.jsonKeyToken(theChildrenKey.asRef());
                success &= writer.jsonBeginMap();
                foundchild = true;
            }
            // Note that calling save is guaranteed to increment iter at
            // least once (since we already checked that we aren't at the
            // end of the set).
            //
            // If we are saving an indirect child, save the full path as
            // the key. This will only happen for top level children of the
            // pinned primitives branch.
            if ((*iter).parentPath() == *prev)
                success &= writer.jsonKeyToken((*iter).nameStr());
            else
                success &= writer.jsonKeyToken((*iter).pathStr());
            success &= writer.jsonBeginMap();
            // After the top level, we only want to save direct descendants.
            success &= save(writer, paths, false, iter);
            success &= writer.jsonEndMap();
        }
        if (foundchild)
            success &= writer.jsonEndMap();

        // Any descendants that aren't direct children, we want to skip
        // over. We don't need to save expanded children inside collapsed
        // children. We only care about fully expanded paths.
        while(iter != paths.end() && (*iter).hasPrefix(*prev))
            ++iter;

        // When we hit the end of a path that isn't a descendant, return to
        // our parent level to test the relationship of iter to our parent.
    }

    return success;
}

bool
HUSD_ExpansionState::save(std::ostream &os, bool binary) const
{
    UT_AutoJSONWriter        writer(os, false /*binary*/);
    HUSD_PathSet::iterator   sceneiter = myExpandedScenePaths.begin();
    HUSD_PathSet::iterator   pinnediter = myExpandedPinnedPaths.begin();
    bool                     success = true;

    success &= writer->jsonBeginMap();
    success &= save(*writer, myExpandedScenePaths, false, sceneiter);
    if (!myExpandedPinnedPaths.empty())
    {
        success &= writer->jsonKeyToken(thePinnedKey.asRef());
        success &= writer->jsonBeginMap();
        // The pinned prims may not be root prims, so we have to allow saving
        // indirect descendants at this top level. This may accidentally
        // capture expansion information that is strictly inside a non-expanded
        // pinned primitive, but better to save too much expansion information
        // than not enough. Atthis level we don't know the pinned roots which
        // would be necessary to do this more intelligently.
        success &= save(*writer, myExpandedPinnedPaths, true, pinnediter);
        success &= writer->jsonEndMap();
    }
    success &= writer->jsonEndMap();

    return success;
}

bool
HUSD_ExpansionState::load(const UT_JSONValue &value,
        const HUSD_Path &path,
        HUSD_PathSet &paths)
{
    const UT_JSONValueMap	*map = value.getMap();

    if (!map)
	return false;

    const UT_JSONValue	*expanded_value = map->get(theExpandedKey.asRef());
    const UT_JSONValue	*children_value = map->get(theChildrenKey.asRef());

    if (expanded_value && expanded_value->getB())
        paths.insert(path);

    if (children_value)
    {
        const UT_JSONValueMap   *children_map = children_value->getMap();
        UT_StringArray           childnames;

        if (!children_map)
            return false;

        children_map->getKeyReferences(childnames);
        for (auto &&childname : childnames)
        {
            const UT_JSONValue  *child_value = children_map->get(childname);

            if (!child_value)
                return false;

            // The childname may be a full path or a single path component to
            // be appended to the current path. The full path should only
            // happen for "root" pinned primitive paths.
            HUSD_Path            childpath = childname.startsWith("/")
                ? HUSD_Path(childname) : path.appendChild(childname);

            if (!load(*child_value, childpath, paths))
                return false;
        }
    }

    return true;
}

bool
HUSD_ExpansionState::load(UT_IStream &is)
{
    UT_AutoJSONParser	 parser(is);
    UT_JSONValue	 rootvalue;

    clear();

    if (!rootvalue.parseValue(parser))
	return false;

    if (!load(rootvalue, HUSD_Path::theRootPrimPath,
            myExpandedScenePaths))
        return false;

    if (rootvalue.getMap())
    {
        const UT_JSONValueMap	*map = rootvalue.getMap();
        const UT_JSONValue      *pinnedvalue = map->get(thePinnedKey.asRef());

        if (pinnedvalue &&
            !load(*pinnedvalue, HUSD_Path::theRootPrimPath,
                myExpandedPinnedPaths))
            return false;
    }

    return true;

}

