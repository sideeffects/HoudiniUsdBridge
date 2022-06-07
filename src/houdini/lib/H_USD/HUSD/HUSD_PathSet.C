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

#include "HUSD_PathSet.h"
#include "HUSD_Path.h"
#include "XUSD_PathSet.h"
#include "XUSD_Utils.h"
#include <PY/PY_InterpreterAutoLock.h>
#include <UT/UT_Swap.h>
#include <UT/UT_WorkBuffer.h>
#include <pxr/base/tf/pyContainerConversions.h>
#include BOOST_HEADER(python.hpp)
#include BOOST_HEADER(python/stl_iterator.hpp)

PXR_NAMESPACE_USING_DIRECTIVE

HUSD_PathSet::HUSD_PathSet()
    : myPathSet(new XUSD_PathSet())
{
}

HUSD_PathSet::HUSD_PathSet(const HUSD_PathSet &src)
    : myPathSet(new XUSD_PathSet(src.sdfPathSet()))
{
}

HUSD_PathSet::HUSD_PathSet(const PXR_NS::XUSD_PathSet &src)
    : myPathSet(new XUSD_PathSet(src))
{
}

const HUSD_PathSet &
HUSD_PathSet::getEmptyPathSet()
{
    static const HUSD_PathSet theEmptyPathSet;

    return theEmptyPathSet;
}

HUSD_PathSet::~HUSD_PathSet()
{
    delete myPathSet;
}

const HUSD_PathSet &
HUSD_PathSet::operator=(const HUSD_PathSet &src)
{
    *myPathSet = *src.myPathSet;
    return *this;
}

bool
HUSD_PathSet::operator==(const HUSD_PathSet &other) const
{
    return (*myPathSet == *other.myPathSet);
}

bool
HUSD_PathSet::operator!=(const HUSD_PathSet &other) const
{
    return (*myPathSet != *other.myPathSet);
}

const HUSD_PathSet &
HUSD_PathSet::operator=(const PXR_NS::XUSD_PathSet &src)
{
    *myPathSet = src;
    return *this;
}

bool
HUSD_PathSet::operator==(const PXR_NS::XUSD_PathSet &other) const
{
    return (*myPathSet == other);
}

bool
HUSD_PathSet::operator!=(const PXR_NS::XUSD_PathSet &other) const
{
    return (*myPathSet != other);
}

bool
HUSD_PathSet::empty() const
{
    return myPathSet->empty();
}

size_t
HUSD_PathSet::size() const
{
    return myPathSet->size();
}

bool
HUSD_PathSet::contains(const UT_StringRef &path) const
{
    SdfPath sdfpath(path.toStdString());

    return myPathSet->contains(sdfpath);
}

bool
HUSD_PathSet::contains(const HUSD_Path &path) const
{
    return myPathSet->contains(path.sdfPath());
}

bool
HUSD_PathSet::contains(const HUSD_PathSet &paths) const
{
    return myPathSet->contains(paths.sdfPathSet());
}

bool
HUSD_PathSet::containsPathOrAncestor(const UT_StringRef &path) const
{
    SdfPath sdfpath(path.toStdString());

    return myPathSet->containsPathOrAncestor(sdfpath);
}

bool
HUSD_PathSet::containsPathOrAncestor(const HUSD_Path &path) const
{
    return myPathSet->containsPathOrAncestor(path.sdfPath());
}

bool
HUSD_PathSet::containsAncestor(const HUSD_Path &path) const
{
    return myPathSet->containsAncestor(path.sdfPath());
}

bool
HUSD_PathSet::containsPathOrDescendant(const UT_StringRef &path) const
{
    SdfPath sdfpath(path.toStdString());

    return myPathSet->containsPathOrDescendant(sdfpath);
}

bool
HUSD_PathSet::containsPathOrDescendant(const HUSD_Path &path) const
{
    return myPathSet->containsPathOrDescendant(path.sdfPath());
}

bool
HUSD_PathSet::containsDescendant(const HUSD_Path &path) const
{
    return myPathSet->containsDescendant(path.sdfPath());
}

void
HUSD_PathSet::clear()
{
    myPathSet->clear();
}

void
HUSD_PathSet::insert(const HUSD_PathSet &other)
{
    myPathSet->insert(other.myPathSet->begin(), other.myPathSet->end());
}

bool
HUSD_PathSet::insert(const HUSD_Path &path)
{
    return myPathSet->insert(path.sdfPath()).second;
}

bool
HUSD_PathSet::insert(const UT_StringRef &path)
{
    return myPathSet->insert(HUSDgetSdfPath(path)).second;
}

void
HUSD_PathSet::insert(const UT_StringArray &paths)
{
    for (auto &&path : paths)
        myPathSet->insert(HUSDgetSdfPath(path));
}

void
HUSD_PathSet::erase(const HUSD_PathSet &other)
{
    for (auto &&path : *other.myPathSet)
        myPathSet->erase(path);
}

bool
HUSD_PathSet::erase(const HUSD_Path &path)
{
    return myPathSet->erase(path.sdfPath());
}

bool
HUSD_PathSet::erase(const UT_StringRef &path)
{
    return myPathSet->erase(SdfPath(path.toStdString()));
}

void
HUSD_PathSet::erase(const UT_StringArray &paths)
{
    for (auto &&path : paths)
        myPathSet->erase(SdfPath(path.toStdString()));
}

void
HUSD_PathSet::swap(HUSD_PathSet &other)
{
    UTswap(myPathSet, other.myPathSet);
}

void
HUSD_PathSet::removeDescendants()
{
    myPathSet->removeDescendants();
}

void
HUSD_PathSet::removeAncestors()
{
    myPathSet->removeAncestors();
}

void *
HUSD_PathSet::getPythonPathList() const
{
    PY_InterpreterAutoLock	 pylock;

    return TfPySequenceToPython<SdfPathSet>::convert(sdfPathSet());
}

bool
HUSD_PathSet::setPythonPaths(void *primpaths)
{
    PY_InterpreterAutoLock	 pylock;
    BOOST_NS::python::object     primpathsobject;

    clear();
    try {
        primpathsobject = BOOST_NS::python::extract<BOOST_NS::python::object>(
            (PyObject *)primpaths);
        BOOST_NS::python::stl_input_iterator<SdfPath> it(primpathsobject);
        BOOST_NS::python::stl_input_iterator<SdfPath> end;

        for (; it != end; ++it)
            insert(HUSD_Path(*it));
    }
    catch(...)
    {
        clear();
        return false;
    }

    return true;
}

void
HUSD_PathSet::getPathsAsStrings(UT_StringArray &paths) const
{
    for (auto &&path : *myPathSet)
        paths.append(HUSD_Path(path).pathStr());
}

UT_StringHolder
HUSD_PathSet::getFirstPathAsString() const
{
    if (!myPathSet->empty())
        return HUSD_Path(*myPathSet->begin()).pathStr();

    return UT_StringHolder::theEmptyString;
}

size_t
HUSD_PathSet::getMemoryUsage() const
{
    return size() * sizeof(SdfPath);
}

HUSD_PathSet::iterator::iterator()
    : myInternalIterator(nullptr)
{
}

HUSD_PathSet::iterator::iterator(void *internal_iterator)
    : myInternalIterator(internal_iterator)
{
}

HUSD_PathSet::iterator::iterator(const iterator &src)
    : myInternalIterator(new XUSD_PathSet::iterator(
        *(XUSD_PathSet::iterator *)src.myInternalIterator))
{
}

HUSD_PathSet::iterator::iterator(iterator &&src)
{
    myInternalIterator = src.myInternalIterator;
    src.myInternalIterator = nullptr;
}

HUSD_PathSet::iterator::~iterator()
{
    if (myInternalIterator)
        delete (XUSD_PathSet::iterator *)myInternalIterator;
}

bool
HUSD_PathSet::iterator::operator==(const iterator &other) const
{
    if (!myInternalIterator || !other.myInternalIterator)
        return (!myInternalIterator && !other.myInternalIterator);

    return (*(XUSD_PathSet::iterator *)myInternalIterator ==
        *(XUSD_PathSet::iterator *)other.myInternalIterator);
}

bool
HUSD_PathSet::iterator::operator!=(const iterator &other) const
{
    if (!myInternalIterator || !other.myInternalIterator)
        return (myInternalIterator || other.myInternalIterator);

    return (*(XUSD_PathSet::iterator *)myInternalIterator !=
        *(XUSD_PathSet::iterator *)other.myInternalIterator);
}

HUSD_Path
HUSD_PathSet::iterator::operator*() const
{
    // Make sure this iterator is legal.
    UT_ASSERT(myInternalIterator);

    return HUSD_Path(*(*(XUSD_PathSet::iterator *)myInternalIterator));
}

HUSD_PathSet::iterator &
HUSD_PathSet::iterator::operator++()
{
    ++(*(XUSD_PathSet::iterator *)myInternalIterator);

    return *this;
}

HUSD_PathSet::iterator &
HUSD_PathSet::iterator::operator=(const iterator &src)
{
    delete (XUSD_PathSet::iterator *)myInternalIterator;
    myInternalIterator = new XUSD_PathSet::iterator(
        *(XUSD_PathSet::iterator *)src.myInternalIterator);

    return *this;
}

HUSD_PathSet::iterator &
HUSD_PathSet::iterator::operator=(iterator &&src)
{
    delete (XUSD_PathSet::iterator *)myInternalIterator;
    myInternalIterator = src.myInternalIterator;
    src.myInternalIterator = nullptr;

    return *this;
}

HUSD_PathSet::iterator
HUSD_PathSet::begin() const
{
    return iterator(new XUSD_PathSet::iterator(myPathSet->begin()));
}

HUSD_PathSet::iterator
HUSD_PathSet::end() const
{
    return iterator(new XUSD_PathSet::iterator(myPathSet->end()));
}

