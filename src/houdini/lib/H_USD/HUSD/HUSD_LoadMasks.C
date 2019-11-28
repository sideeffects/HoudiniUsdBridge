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

#include "HUSD_LoadMasks.h"
#include <UT/UT_JSONParser.h>
#include <UT/UT_JSONValue.h>
#include <UT/UT_JSONWriter.h>
#include <UT/UT_StringHolder.h>
#include <UT/UT_WorkArgs.h>

static constexpr UT_StringLit	 thePopulateAllKey("populateall");
static constexpr UT_StringLit	 thePopulatePathsKey("populatepaths");
static constexpr UT_StringLit	 theLoadAllKey("loadall");
static constexpr UT_StringLit	 theLoadPathsKey("loadpaths");
static constexpr UT_StringLit	 theMuteLayersKey("mutelayers");

HUSD_LoadMasks::HUSD_LoadMasks()
    : myPopulateAll(true),
      myLoadAll(true)
{
}

HUSD_LoadMasks::~HUSD_LoadMasks()
{
}

bool
HUSD_LoadMasks::operator==(const HUSD_LoadMasks&other) const
{
    if ((!myPopulateAll || !other.myPopulateAll) &&
	myPopulatePaths != other.myPopulatePaths)
	return false;

    if ((!myLoadAll || !other.myLoadAll) &&
	myLoadPaths != other.myLoadPaths)
	return false;

    if (myMuteLayers != other.myMuteLayers)
	return false;

    return true;
}

void
HUSD_LoadMasks::save(std::ostream &os) const
{
    UT_AutoJSONWriter	 writer(os, false);
    UT_JSONWriter	&w(writer.writer());

    w.jsonBeginMap();
	// Save out stage the stage populate paths.
	w.jsonKeyToken(thePopulateAllKey.asRef());
	w.jsonValue(myPopulateAll);

	w.jsonKeyToken(thePopulatePathsKey.asRef());
	w.jsonBeginArray();
	for (auto &&path : myPopulatePaths)
	    w.jsonValue(path);
	w.jsonEndArray();

	// Save out the layer muting.
	w.jsonKeyToken(theMuteLayersKey.asRef());
	w.jsonBeginArray();
	for (auto &&identifier : myMuteLayers)
	    w.jsonValue(identifier);
	w.jsonEndArray();

	// Save out the payload paths.
	w.jsonKeyToken(theLoadAllKey.asRef());
	w.jsonValue(myLoadAll);

	w.jsonKeyToken(theLoadPathsKey.asRef());
	w.jsonBeginArray();
	for (auto &&path : myLoadPaths)
	    w.jsonValue(path);
	w.jsonEndArray();
    w.jsonEndMap();
}

bool
HUSD_LoadMasks::load(UT_IStream &is)
{
    UT_AutoJSONParser	 parser(is);
    UT_JSONValue	 value;

    myPopulatePaths.clear();
    myMuteLayers.clear();
    myLoadPaths.clear();
    myPopulateAll = true;
    myLoadAll = true;
    if (!value.parseValue(parser.parser()) || !value.getMap())
	return false;

    UT_JSONValueMap	*map = value.getMap();
    UT_JSONValue	*populateall = map->get(thePopulateAllKey.asRef());
    UT_JSONValue	*populatepaths = map->get(thePopulatePathsKey.asRef());

    if (populateall)
	myPopulateAll = populateall->getB();
    if (populatepaths && populatepaths->getArray())
    {
	for (int i = 0, n = populatepaths->getArray()->size(); i < n; i++)
	{
	    UT_JSONValue	*path = populatepaths->getArray()->get(i);

	    if (path && path->getStringHolder())
		myPopulatePaths.insert(*path->getStringHolder());
	}
    }

    UT_JSONValue	*mutelayers = map->get(theMuteLayersKey.asRef());

    if (mutelayers && mutelayers->getArray())
    {
	for (int i = 0, n = mutelayers->getArray()->size(); i < n; i++)
	{
	    UT_JSONValue	*identifier = mutelayers->getArray()->get(i);

	    if (identifier && identifier->getStringHolder())
		myMuteLayers.insert(*identifier->getStringHolder());
	}
    }

    UT_JSONValue	*loadall = map->get(theLoadAllKey.asRef());
    UT_JSONValue	*loadpaths = map->get(theLoadPathsKey.asRef());

    if (!loadall || (loadpaths && !loadpaths->getArray()))
	return false;

    if (loadall)
	myLoadAll = loadall->getB();
    if (loadpaths && loadpaths->getArray())
    {
	for (int i = 0, n = loadpaths->getArray()->size(); i < n; i++)
	{
	    UT_JSONValue	*path = loadpaths->getArray()->get(i);

	    if (path && path->getStringHolder())
		myLoadPaths.insert(*path->getStringHolder());
	}
    }

    return true;
}

void
HUSD_LoadMasks::setPopulateAll()
{
    myPopulateAll = true;
    myPopulatePaths.clear();
}

bool
HUSD_LoadMasks::populateAll() const
{
    return myPopulateAll;
}

void
HUSD_LoadMasks::addPopulatePath(const UT_StringHolder &path)
{
    myPopulateAll = false;
    myPopulatePaths.insert(path);
}

void
HUSD_LoadMasks::removePopulatePath(const UT_StringHolder &path,
        bool remove_children)
{
    myPopulateAll = false;
    if (remove_children)
    {
        UT_String	 pathcopy(path);
        if (!pathcopy.endsWith("/"))
            pathcopy.append('/');
        auto             lowerbound = myPopulatePaths.lower_bound(pathcopy);

        while (lowerbound != myPopulatePaths.end() &&
               lowerbound->startsWith(pathcopy))
            lowerbound = myPopulatePaths.erase(lowerbound);
    }
    myPopulatePaths.erase(path);
}

void
HUSD_LoadMasks::removeAllPopulatePaths()
{
    myPopulateAll = false;
    myPopulatePaths.clear();
}

bool
HUSD_LoadMasks::isPathPopulated(const UT_StringHolder &path,
	HUSD_LoadMasksMatchStyle match) const
{
    if (myPopulatePaths.contains(path))
	return true;

    if (match == HUSD_MATCH_EXACT)
	return false;

    if (myPopulateAll)
	return true;

    if (match == HUSD_MATCH_SELF_OR_PARENT)
    {
        UT_String	 pathcopy(path);
        char	        *lastslash = nullptr;

        // Check each ancestor and see if it shows up in the path set.
        while ((lastslash = pathcopy.lastChar('/')))
        {
            *lastslash = '\0';
            if (myPopulatePaths.contains(pathcopy))
                return true;
        }
    }
    else if (match == HUSD_MATCH_SELF_OR_CHILD)
    {
        UT_String	 pathcopy(path);
        pathcopy.append('/');

        // Find the first path in the set that alphabetically follows the
        // requested path with a slash appended. If the result starts with
        // that exact string, it must be a child of the requested path.
        // Otherwise no children of this path are in the set.
        auto             lowerbound = myPopulatePaths.lower_bound(pathcopy);
        if (lowerbound != myPopulatePaths.end() &&
            lowerbound->startsWith(pathcopy))
            return true;
    }

    return false;
}
    
void
HUSD_LoadMasks::addMuteLayer(const UT_StringHolder &identifier)
{
    myMuteLayers.insert(identifier);
}

void
HUSD_LoadMasks::removeMuteLayer(const UT_StringHolder &identifier)
{
    myMuteLayers.erase(identifier);
}

void
HUSD_LoadMasks::removeAllMuteLayers()
{
    myMuteLayers.clear();
}

void
HUSD_LoadMasks::setLoadAll()
{
    myLoadAll = true;
    myLoadPaths.clear();
}

bool
HUSD_LoadMasks::loadAll() const
{
    return myLoadAll;
}

void
HUSD_LoadMasks::addLoadPath(const UT_StringHolder &path)
{
    myLoadAll = false;
    myLoadPaths.insert(path);
}

void
HUSD_LoadMasks::removeLoadPath(const UT_StringHolder &path,
        bool remove_children)
{
    myLoadAll = false;
    if (remove_children)
    {
        UT_String	 pathcopy(path);
        if (!pathcopy.endsWith("/"))
            pathcopy.append('/');
        auto             lowerbound = myLoadPaths.lower_bound(pathcopy);

        while (lowerbound != myLoadPaths.end() &&
               lowerbound->startsWith(pathcopy))
            lowerbound = myLoadPaths.erase(lowerbound);
    }
    myLoadPaths.erase(path);
}

void
HUSD_LoadMasks::removeAllLoadPaths()
{
    myLoadAll = false;
    myLoadPaths.clear();
}

bool
HUSD_LoadMasks::isPathLoaded(const UT_StringHolder &path,
	HUSD_LoadMasksMatchStyle match) const
{
    if (myLoadPaths.contains(path))
	return true;

    if (match == HUSD_MATCH_EXACT)
	return false;

    if (myLoadAll)
	return true;

    if (match == HUSD_MATCH_SELF_OR_PARENT)
    {
        UT_String	 pathcopy(path);
        char	        *lastslash = nullptr;

        // Check each ancestor and see if it shows up in the path set.
        while ((lastslash = pathcopy.lastChar('/')))
        {
            *lastslash = '\0';
            if (myLoadPaths.contains(pathcopy))
                return true;
        }
    }
    else if (match == HUSD_MATCH_SELF_OR_CHILD)
    {
        UT_String	 pathcopy(path);
        pathcopy.append('/');

        // Find the first path in the set that alphabetically follows the
        // requested path with a slash appended. If the result starts with
        // that exact string, it must be a child of the requested path.
        // Otherwise no children of this path are in the set.
        auto             lowerbound = myLoadPaths.lower_bound(pathcopy);
        if (lowerbound != myLoadPaths.end() &&
            lowerbound->startsWith(pathcopy))
            return true;
    }

    return false;
}

void
HUSD_LoadMasks::merge(const HUSD_LoadMasks &other)
{
    myPopulatePaths.insert(other.myPopulatePaths.begin(),
	other.myPopulatePaths.end());
    myMuteLayers.insert(other.myMuteLayers.begin(),
	other.myMuteLayers.end());
    myLoadPaths.insert(other.myLoadPaths.begin(),
	other.myLoadPaths.end());
    myPopulateAll = (myPopulateAll && other.myPopulateAll);
    myLoadAll = (myLoadAll && other.myLoadAll);
}

