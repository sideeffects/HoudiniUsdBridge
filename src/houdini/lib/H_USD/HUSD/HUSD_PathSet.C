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
#include "XUSD_PathSet.h"
#include <UT/UT_Swap.h>
#include <UT/UT_WorkBuffer.h>
#include <pxr/base/tf/pyContainerConversions.h>

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

    return (myPathSet->count(sdfpath) > 0);
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

void
HUSD_PathSet::insert(const UT_StringRef &path)
{
    myPathSet->insert(SdfPath(path.toStdString()));
}

void
HUSD_PathSet::insert(const UT_StringArray &paths)
{
    for (auto &&path : paths)
        myPathSet->insert(SdfPath(path.toStdString()));
}

void
HUSD_PathSet::swap(HUSD_PathSet &other)
{
    UTswap(myPathSet, other.myPathSet);
}

void *
HUSD_PathSet::getPythonPathList() const
{
    return TfPySequenceToPython<SdfPathSet>::convert(sdfPathSet());
}

void
HUSD_PathSet::getPathsAsStrings(UT_StringArray &paths) const
{
    for (auto &&path : *myPathSet)
        paths.append(path.GetText());
}

void
HUSD_PathSet::getPathsAsWorkBuffer(UT_WorkBuffer &buf) const
{
    for (auto &&path : *myPathSet)
    {
        if (buf.isstring())
            buf.append(' ');
        buf.append(path.GetString());
    }
}

UT_StringHolder
HUSD_PathSet::getFirstPathAsString() const
{
    if (!myPathSet->empty())
        return myPathSet->begin()->GetText();

    return UT_StringHolder::theEmptyString;
}

size_t
HUSD_PathSet::getMemoryUsage() const
{
    return size() * sizeof(SdfPath);
}

HUSD_PathSet::iterator::iterator()
    : myInternalIterator(nullptr),
      myPathStringSet(false)
{
}

HUSD_PathSet::iterator::iterator(void *internal_iterator)
    : myInternalIterator(internal_iterator),
      myPathStringSet(false)
{
}

HUSD_PathSet::iterator::iterator(const iterator &src)
    : myInternalIterator(new XUSD_PathSet::iterator(
        *(XUSD_PathSet::iterator *)src.myInternalIterator)),
      myPathStringSet(false)
{
}

HUSD_PathSet::iterator::iterator(iterator &&src)
    : myPathStringSet(false)
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

const UT_StringHolder &
HUSD_PathSet::iterator::operator*() const
{
    // Make sure this iterator is legal.
    UT_ASSERT(myInternalIterator);

    if (!myPathStringSet)
    {
        if (myInternalIterator)
            myPathString =
                (*(XUSD_PathSet::iterator *)myInternalIterator)->GetText();
        else
            myPathString.clear();
        myPathStringSet = true;
    }

    return myPathString;
}

HUSD_PathSet::iterator &
HUSD_PathSet::iterator::operator++()
{
    ++(*(XUSD_PathSet::iterator *)myInternalIterator);
    myPathStringSet = false;

    return *this;
}

HUSD_PathSet::iterator &
HUSD_PathSet::iterator::operator=(const iterator &src)
{
    delete (XUSD_PathSet::iterator *)myInternalIterator;
    myInternalIterator = new XUSD_PathSet::iterator(
        *(XUSD_PathSet::iterator *)src.myInternalIterator);
    myPathStringSet = false;

    return *this;
}

HUSD_PathSet::iterator &
HUSD_PathSet::iterator::operator=(iterator &&src)
{
    delete (XUSD_PathSet::iterator *)myInternalIterator;
    myInternalIterator = src.myInternalIterator;
    myPathStringSet = false;
    src.myInternalIterator = nullptr;
    src.myPathStringSet = false;

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

