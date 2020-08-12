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

HUSD_ExpansionState::HUSD_ExpansionState()
{
    // Always start with the root node expanded.
    setExpanded(HUSD_Path("/"), true);
}

HUSD_ExpansionState::~HUSD_ExpansionState()
{
}

bool
HUSD_ExpansionState::isExpanded(const HUSD_Path &path) const
{
    return myExpandedPaths.contains(path);
}

void
HUSD_ExpansionState::setExpanded(const HUSD_Path &path, bool expanded)
{
    if (expanded)
        myExpandedPaths.insert(path);
    else
        myExpandedPaths.erase(path);
}

exint
HUSD_ExpansionState::getMemoryUsage() const
{
    return myExpandedPaths.size() * sizeof(HUSD_Path);
}

void
HUSD_ExpansionState::clear()
{
    myExpandedPaths.clear();
}

void
HUSD_ExpansionState::copy(const HUSD_ExpansionState &src)
{
    myExpandedPaths = src.myExpandedPaths;
}

bool
HUSD_ExpansionState::save(UT_JSONWriter &writer,
        HUSD_PathSet::iterator &iter) const
{
    bool	 success = true;

    success &= writer.jsonBeginMap();
    if (iter != myExpandedPaths.end())
    {
        success &= writer.jsonKeyToken(theExpandedKey.asRef());
        success &= writer.jsonBool(true);

        HUSD_PathSet::iterator   prev = iter;
        HUSD_Path                prevpath = *prev;
        bool                     foundchild = false;

        ++iter;
        while(iter != myExpandedPaths.end() && (*iter).parentPath() == *prev)
        {
            // Any direct children we want to write out.
            if (!foundchild)
            {
                success &= writer.jsonKeyToken(theChildrenKey.asRef());
                success &= writer.jsonBeginMap();
                foundchild = true;
            }
            // Note that calling save is guaranteed to increment iter at
            // least once (since we already check ed that we aren't at the
            // end of the set).
            success &= writer.jsonKeyToken((*iter).nameStr());
            success &= save(writer, iter);
        }
        if (foundchild)
            success &= writer.jsonEndMap();

        // Any descendants that aren't direct children, we want to skip
        // over. We don't need to save expanded children inside collapsed
        // children. We only care about fully expanded paths.
        while(iter != myExpandedPaths.end() && (*iter).hasPrefix(*prev))
            ++iter;

        // When we hit the end of a path that isn't a descendant, return to
        // our parent level to test the relationship of iter to our parent.
    }
    success &= writer.jsonEndMap();

    return success;
}

bool
HUSD_ExpansionState::save(std::ostream &os, bool binary) const
{
    UT_AutoJSONWriter        writer(os, binary);
    HUSD_PathSet::iterator   iter = myExpandedPaths.begin();

    return save(*writer, iter);
}

bool
HUSD_ExpansionState::load(const UT_JSONValue &value,
        const HUSD_Path &path)
{
    const UT_JSONValueMap	*map = value.getMap();

    if (!map)
	return false;

    const UT_JSONValue	*expanded_value = map->get(theExpandedKey.asRef());
    const UT_JSONValue	*children_value = map->get(theChildrenKey.asRef());

    if (expanded_value && expanded_value->getB())
        myExpandedPaths.insert(path);

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

            HUSD_Path            childpath = path.appendChild(childname);

            if (!load(*child_value, childpath))
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

    return load(rootvalue, HUSD_Path("/"));
}

