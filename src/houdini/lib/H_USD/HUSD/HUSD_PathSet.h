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

#ifndef __HUSD_PathSet_h__
#define __HUSD_PathSet_h__

#include "HUSD_API.h"
#include <UT/UT_StringArray.h>
#include <UT/UT_StringHolder.h>
#include <UT/UT_StringSet.h>
#include <pxr/pxr.h>
#include <initializer_list>
#include <stddef.h>

PXR_NAMESPACE_OPEN_SCOPE
class XUSD_PathSet;
PXR_NAMESPACE_CLOSE_SCOPE

class UT_WorkBuffer;
class HUSD_Path;

/// This class is a "safe" wrapper around an XUSD_PathSet (which is itself a
/// wrapper around an SdfPathSet). This class provides a bunch of convenient
/// signatures for operating with HUSD_Path and UT_StringRef objects.
///
/// The iterator implementation is fairly lacking, and only really useful
/// for simple walking through the set because of the need to hide the
/// SdfPathSet class and it's iterator class. So HUSD_PathSet::iterator
/// uses a void * that points to an SdfPathSet::iterator, but it allocates
/// that iterator with new(). So some nice APIs like "iterator erase(iterator)"
/// would be very inefficient with HUSD_PathSet::iterator, and so are not
/// implemented.

class HUSD_API HUSD_PathSet
{
public:
                         HUSD_PathSet();
                         HUSD_PathSet(const HUSD_PathSet &src);
                         HUSD_PathSet(const PXR_NS::XUSD_PathSet &src);
    explicit             HUSD_PathSet(std::initializer_list<HUSD_Path> init);
                        ~HUSD_PathSet();

    static const HUSD_PathSet   &getEmptyPathSet();

    const HUSD_PathSet          &operator=(const HUSD_PathSet &src);
    bool                         operator==(const HUSD_PathSet &other) const;
    bool                         operator!=(const HUSD_PathSet &other) const;
    const HUSD_PathSet          &operator=(const PXR_NS::XUSD_PathSet &src);
    bool                         operator==(const PXR_NS::XUSD_PathSet
                                        &other) const;
    bool                         operator!=(const PXR_NS::XUSD_PathSet
                                        &other) const;

    bool         empty() const;
    size_t       size() const;
    bool         contains(const UT_StringRef &path) const;
    bool         contains(const HUSD_Path &path) const;
    bool         contains(const HUSD_PathSet &paths) const;
    bool         containsPathOrAncestor(const UT_StringRef &path) const;
    bool         containsPathOrAncestor(const HUSD_Path &path) const;
    bool         containsAncestor(const HUSD_Path &path) const;
    bool         containsPathOrDescendant(const UT_StringRef &path) const;
    bool         containsPathOrDescendant(const HUSD_Path &path) const;
    bool         containsDescendant(const HUSD_Path &path) const;
    void         clear();
    void         insert(const HUSD_PathSet &other);
    bool         insert(const HUSD_Path &path);
    bool         insert(const UT_StringRef &path);
    void         insert(const UT_StringArray &paths);
    void         erase(const HUSD_PathSet &other);
    bool         erase(const HUSD_Path &path);
    bool         erase(const UT_StringRef &path);
    void         erase(const UT_StringArray &paths);
    void         swap(HUSD_PathSet &other);

    // Remove all paths where an ancestor of the path is also in the set.
    void                         removeDescendants();
    // Remove all paths where a descendant of the path is also in the set.
    void                         removeAncestors();

    PXR_NS::XUSD_PathSet        &sdfPathSet()
                                 { return *myPathSet; }
    const PXR_NS::XUSD_PathSet  &sdfPathSet() const
                                 { return *myPathSet; }

    // Return a python object holding a set of SdfPath python objects.
    void                        *getPythonPathList() const;
    // Fill this path set from a python tuple of SdfPath python objects.
    bool                         setPythonPaths(void *primpaths);

    // Fill a UT_StringArray or UT_StringSet with the paths in the SdfPathSet.
    void                         getPathsAsStrings(UT_StringArray &paths) const;
    void                         getPathsAsStrings(UT_StringSet &paths) const;
    // Return the string representation of the first path in the set.
    UT_StringHolder              getFirstPathAsString() const;

    size_t                       getMemoryUsage() const;

    class HUSD_API iterator {
    public:
        iterator();
        iterator(void *internal_iterator);
        iterator(const iterator &src);
        iterator(iterator &&src);
        ~iterator();

        bool                     operator==(const iterator &other) const;
        bool                     operator!=(const iterator &other) const;

        HUSD_Path                operator*() const;
        iterator                &operator++();
        iterator                &operator=(const iterator &src);
        iterator                &operator=(iterator &&src);

    private:
        void                    *myInternalIterator;
        friend class             HUSD_PathSet;
    };

    iterator                     begin() const;
    iterator                     end() const;

private:
    PXR_NS::XUSD_PathSet        *myPathSet;
};

#endif

