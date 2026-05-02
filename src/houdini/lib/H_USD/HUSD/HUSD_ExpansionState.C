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
#include "XUSD_PathSet.h"
#include <UT/UT_IStream.h>
#include <UT/UT_JSONParser.h>
#include <UT/UT_JSONValue.h>
#include <UT/UT_JSONValueMap.h>
#include <UT/UT_JSONWriter.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace {
    // Map keys for loading old hip files.
    static constexpr UT_StringLit	 theDeprecatedExpandedKey("expanded");
    static constexpr UT_StringLit	 theDeprecatedPinnedKey("pinned");
    static constexpr UT_StringLit	 theDeprecatedChildrenKey("children");

    // Map keys for saving and loading new hip files.
    static constexpr UT_StringLit	 theExpandedPathsKey("expandedpaths");
    static constexpr UT_StringLit	 thePinnedPathsKey("pinnedpaths");
    static constexpr UT_StringLit	 theLockedPathsKey("lockedpaths");

    // Copy the current contents of our locked path sets into the provided
    // arrays which can be preserved for undoing this operation.
    void copyPathsForUndo(const HUSD_PathSet &src, UT_Array<HUSD_Path> &dest)
    {
        dest.clear();
        dest.setCapacity(src.size());
        for (auto &&it : src)
            dest.append(it);
    }
    void copyPathsForUndo(const UT_Array<HUSD_Path> &src, HUSD_PathSet &dest)
    {
        dest.clear();
        for (auto &&it : src)
            dest.insert(it);
    }
};

HUSD_ExpansionState::HUSD_ExpansionState()
{
    // Always start with the root node expanded.
    setExpanded(HUSD_Path::theRootPrimPath, false, true);
}

HUSD_ExpansionState::~HUSD_ExpansionState()
{
}

bool
HUSD_ExpansionState::setExpanded(const HUSD_Path &path,
        bool pinned,
        bool expanded)
{
    // Return true if this request changes anything.
    if (pinned)
    {
        if (expanded)
            return myExpandedPinnedPaths.insert(path);
        else
            return myExpandedPinnedPaths.erase(path);
    }
    else
    {
        if (expanded)
            return myExpandedScenePaths.insert(path);
        else
            return myExpandedScenePaths.erase(path);
    }
}

bool
HUSD_ExpansionState::setExpansionLocked(const HUSD_Path &path, bool locked,
        const HUSD_PathSet *expanded_subpaths,
        bool use_pinned_subpaths,
        bool preserve_descendant_expansion,
        UT_Array<HUSD_Path> *undo_locked_paths,
        UT_Array<HUSD_Path> *undo_locked_expanded_paths)
{
    bool changed = false;

    // Copy the current contents of our locked path sets into the provided
    // arrays which can be preserved for undoing this operation.
    auto copy_paths_for_undo_fn = [&] {
        if (undo_locked_paths && undo_locked_expanded_paths)
        {
            copyPathsForUndo(myLockedScenePaths,
                *undo_locked_paths);
            copyPathsForUndo(myLockedExpandedScenePaths,
                *undo_locked_expanded_paths);
        }
    };

    // Whether we are locking or unlocking, if we aren't preserving
    // the descendant's lock states, clear the locking data for this
    // path and any of its descendants.
    if (!preserve_descendant_expansion)
    {
        changed |= myLockedScenePaths.eraseWithDescendants(path);
        changed |= myLockedExpandedScenePaths.eraseWithDescendants(path);
    }

    if (locked)
    {
        // Copy the current expansion state starting at the supplied path.
        // Do this even if this prim is already locked (in case we need to
        // update the expanded subpath states).
        copy_paths_for_undo_fn();

        // If we aren't passed a set of prims to mark "expanded" under
        // the locked prim, use our current set of expanded paths.
        if (!expanded_subpaths)
        {
            if (use_pinned_subpaths)
                expanded_subpaths = &myExpandedPinnedPaths;
            else
                expanded_subpaths = &myExpandedScenePaths;
        }

        auto it = expanded_subpaths->find(path);
        auto end = expanded_subpaths->end();
        myLockedScenePaths.insert(path);
        while (it != end && (*it).hasPrefix(path))
        {
            myLockedExpandedScenePaths.insert(*it);
            ++it;
            // We only want to add contiguous hierarchies in the locked
            // expanded paths (we may remember that a child of a collapsed
            // prim is expanded, but the user can't see this so we don't
            // want to record it in the list of loacked expanded paths).
            // So if the direct parent of the next expanded path is
            // not already in our locked expanded paths, skip it.
            while (it != end &&
                   (*it).hasPrefix(path) &&
                   !myLockedExpandedScenePaths.contains((*it).parentPath()))
                ++it;
        }

        changed = true;
    }
    else
    {
        // Remove the supplied path from the locked set. Do nothing if the
        // prim isn't in the locked set. Note that if we aren't preserving
        // descendants, we may have already cleared out this prim from the
        // locked paths set.
        if (myLockedScenePaths.contains(path))
        {
            HUSD_PathSet locked_descendants;
            XUSD_PathSet &sdfpaths = myLockedExpandedScenePaths.sdfPathSet();
            SdfPath sdfpath = path.sdfPath();

            copy_paths_for_undo_fn();
            myLockedScenePaths.erase(path);
            myLockedScenePaths.getDescendants(path, locked_descendants);
            // We want to remove the saved expansion states of any prims
            // that lie between the unlocked prim and any descendant locked
            // prims. This way we preserve any information we have about
            // descendants.
            for (auto it = sdfpaths.lower_bound(sdfpath); it != sdfpaths.end();)
            {
                if ((*it).HasPrefix((sdfpath)))
                {
                    HUSD_Path husdit(*it);
                    if (locked_descendants.containsPathOrAncestor(husdit))
                        ++it;
                    else
                        it = sdfpaths.erase(it);
                }
                else
                    break;
            }

            changed = true;
        }
    }

    return changed;
}

void
HUSD_ExpansionState::undoExpansionLockState(
        UT_Array<HUSD_Path> &swap_locked_paths,
        UT_Array<HUSD_Path> &swap_locked_expanded_paths)
{
    UT_Array<HUSD_Path> tmp_locked_paths;
    UT_Array<HUSD_Path> tmp_locked_expanded_paths;
    copyPathsForUndo(myLockedScenePaths, tmp_locked_paths);
    copyPathsForUndo(myLockedExpandedScenePaths, tmp_locked_expanded_paths);
    copyPathsForUndo(swap_locked_paths, myLockedScenePaths);
    copyPathsForUndo(swap_locked_expanded_paths, myLockedExpandedScenePaths);
    swap_locked_paths.swap(tmp_locked_paths);
    swap_locked_expanded_paths.swap(tmp_locked_expanded_paths);
}

exint
HUSD_ExpansionState::getMemoryUsage() const
{
    return myExpandedPinnedPaths.size() * sizeof(HUSD_Path) +
        myExpandedScenePaths.size() * sizeof(HUSD_Path) +
        myLockedScenePaths.size() * sizeof(HUSD_Path) +
        myLockedExpandedScenePaths.size() * sizeof(HUSD_Path);
}

void
HUSD_ExpansionState::clear()
{
    myExpandedPinnedPaths.clear();
    myExpandedScenePaths.clear();
    myLockedScenePaths.clear();
    myLockedExpandedScenePaths.clear();
}

void
HUSD_ExpansionState::copy(const HUSD_ExpansionState &src)
{
    myExpandedPinnedPaths = src.myExpandedPinnedPaths;
    myExpandedScenePaths = src.myExpandedScenePaths;
    myLockedScenePaths = src.myLockedScenePaths;
    myLockedExpandedScenePaths = src.myLockedExpandedScenePaths;
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
                success &= writer.jsonBeginMap();
                foundchild = true;
            }
            // Save the "child" path. This "child" may require a full path if
            // we allow indirect descendants.
            if (allow_saving_indirect_descendants)
                success &= writer.jsonKeyToken((*iter).pathStr());
            else
                success &= writer.jsonKeyToken((*iter).nameStr());
            // After the top level, we only want to save direct descendants.
            success &= save(writer, paths, false, iter);
        }
        if (foundchild)
            success &= writer.jsonEndMap();
        else
            success &= writer.jsonBool(true);

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
    bool                     success = true;

    success &= writer->jsonBeginMap();
    // No point saving the expanded scene paths if the root isn't expanded.
    if (myExpandedScenePaths.contains(HUSD_Path::theRootPrimPath))
    {
        HUSD_PathSet::iterator expandediter = myExpandedScenePaths.begin();

        success &= writer->jsonKeyToken(theExpandedPathsKey.asRef());
        success &= save(*writer, myExpandedScenePaths, false, expandediter);
    }
    // No point saving the expanded pinned paths if the root isn't expanded.
    if (myExpandedPinnedPaths.contains(HUSD_Path::theRootPrimPath))
    {
        HUSD_PathSet::iterator pinnediter = myExpandedPinnedPaths.begin();

        success &= writer->jsonKeyToken(thePinnedPathsKey.asRef());
        // The pinned prims may not be root prims, so we have to allow saving
        // indirect descendants at this top level. This may accidentally
        // capture expansion information that is strictly inside a non-expanded
        // pinned primitive, but better to save too much expansion information
        // than not enough. At this level we don't know the pinned roots which
        // would be necessary to do this more intelligently.
        success &= save(*writer, myExpandedPinnedPaths, true, pinnediter);
    }
    if (!myLockedScenePaths.empty())
    {
        // Saving locked expansion states is a little different. The locked
        // scene paths are all top level unique branches. The expanded locked
        // paths are the (possibly empty) contents of these top level branches.
        success &= writer->jsonKeyToken(theLockedPathsKey.asRef());
        success &= writer->jsonBeginMap();
        for (auto &&lockediter : myLockedScenePaths)
        {
            success &= writer->jsonKeyToken(lockediter.pathStr());
            HUSD_PathSet::iterator lockedexpandediter =
                myLockedExpandedScenePaths.find(lockediter);
            if (lockedexpandediter != myLockedExpandedScenePaths.end())
                success &= save(*writer, myLockedExpandedScenePaths, false,
                    lockedexpandediter);
            else
                writer->jsonBool(false);
        }
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
    paths.insert(path);

    const UT_JSONValueMap   *map = value.getMap();
    if (!map)
	return true;

    UT_StringArray           childnames;

    map->getKeyReferences(childnames);
    for (auto &&childname : childnames)
    {
        const UT_JSONValue  *child_value = map->get(childname);

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

    if (rootvalue.getMap())
    {
        const UT_JSONValueMap	*map = rootvalue.getMap();

        // Load the data from an older hip file (before H21 expansion locking).
        if (map->get(theDeprecatedExpandedKey.asRef()))
            return loadDeprecated(rootvalue);

        const UT_JSONValue      *expandedvalue =
            map->get(theExpandedPathsKey.asRef());
        if (expandedvalue &&
            !load(*expandedvalue, HUSD_Path::theRootPrimPath,
                  myExpandedScenePaths))
            return false;

        const UT_JSONValue      *pinnedvalue =
            map->get(thePinnedPathsKey.asRef());
        if (pinnedvalue &&
            !load(*pinnedvalue, HUSD_Path::theRootPrimPath,
                myExpandedPinnedPaths))
            return false;

        const UT_JSONValue      *lockedvalue =
            map->get(theLockedPathsKey.asRef());
        if (lockedvalue && lockedvalue->getMap())
        {
            UT_JSONValueMap *lockedmap = lockedvalue->getMap();
            for (auto &&lockediter : *lockedmap)
            {
                HUSD_Path locked_branch(lockediter.first);
                myLockedScenePaths.insert(locked_branch);

                if (!lockediter.second)
                    return false;

                // A boolean value of "false" under this key means this branch
                // is locked in a collapsed state.
                if (!lockediter.second->getMap() && !lockediter.second->getB())
                    continue;

                if (!load(*lockediter.second, locked_branch,
                        myLockedExpandedScenePaths))
                    return false;
            }
        }
    }

    return true;
}

bool
HUSD_ExpansionState::loadDeprecated(const UT_JSONValue &value,
                          const HUSD_Path &path,
                          HUSD_PathSet &paths)
{
    const UT_JSONValueMap	*map = value.getMap();

    if (!map)
        return false;

    const UT_JSONValue	*expanded_value =
        map->get(theDeprecatedExpandedKey.asRef());
    const UT_JSONValue	*children_value =
        map->get(theDeprecatedChildrenKey.asRef());

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
                                      ? HUSD_Path(childname)
                                      : path.appendChild(childname);

            if (!loadDeprecated(*child_value, childpath, paths))
                return false;
        }
    }

    return true;
}

bool
HUSD_ExpansionState::loadDeprecated(UT_JSONValue &rootvalue)
{
    if (!loadDeprecated(rootvalue, HUSD_Path::theRootPrimPath,
            myExpandedScenePaths))
        return false;

    if (rootvalue.getMap())
    {
        const UT_JSONValueMap	*map = rootvalue.getMap();
        const UT_JSONValue      *pinnedvalue =
            map->get(theDeprecatedPinnedKey.asRef());

        if (pinnedvalue &&
            !loadDeprecated(*pinnedvalue, HUSD_Path::theRootPrimPath,
                  myExpandedPinnedPaths))
            return false;
    }

    return true;

}
