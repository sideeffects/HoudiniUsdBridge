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

#include "HUSD_Path.h"
#include "XUSD_Utils.h"
#include <SYS/SYS_StaticAssert.h>
#include <PY/PY_InterpreterAutoLock.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/base/tf/pyUtils.h>
#include BOOST_HEADER(python.hpp)

PXR_NAMESPACE_USING_DIRECTIVE

SYS_STATIC_ASSERT(sizeof(SdfPath) == sizeof(HUSD_Path));
SYS_STATIC_ASSERT(sizeof(SdfPath) == sizeof(int64));

const HUSD_Path HUSD_Path::theRootPrimPath(SdfPath::AbsoluteRootPath());

HUSD_Path::HUSD_Path(const HUSD_Path &path)
{
    new (mySdfPathData) SdfPath(path.sdfPath());
}

HUSD_Path::HUSD_Path(const PXR_NS::SdfPath &path)
{
    new (mySdfPathData) SdfPath(path);
}

HUSD_Path::HUSD_Path(const UT_StringRef &path)
{
    new (mySdfPathData) SdfPath(HUSDgetSdfPath(path));
}

HUSD_Path::HUSD_Path()
{
    new (mySdfPathData) SdfPath();
}

HUSD_Path::~HUSD_Path()
{
    reinterpret_cast<SdfPath *>(mySdfPathData)->~SdfPath();
}

const HUSD_Path &
HUSD_Path::operator=(const HUSD_Path &path)
{
    reinterpret_cast<SdfPath *>(mySdfPathData)->operator=(path.sdfPath());

    return *this;
}

const HUSD_Path &
HUSD_Path::operator=(const SdfPath &path)
{
    reinterpret_cast<SdfPath *>(mySdfPathData)->operator=(path);

    return *this;
}

bool
HUSD_Path::operator==(const HUSD_Path &path) const
{
    return sdfPath() == path.sdfPath();
}

bool
HUSD_Path::operator<(const HUSD_Path &path) const
{
    return sdfPath() < path.sdfPath();
}

bool
HUSD_Path::isEmpty() const
{
    return reinterpret_cast<const SdfPath *>(mySdfPathData)->IsEmpty();
}

bool
HUSD_Path::isPrimPath() const
{
    return reinterpret_cast<const SdfPath *>(mySdfPathData)->IsPrimPath();
}

bool
HUSD_Path::hasPrefix(const HUSD_Path &prefix) const
{
    return reinterpret_cast<const SdfPath *>(mySdfPathData)->HasPrefix(
        *reinterpret_cast<const SdfPath *>(prefix.mySdfPathData));
}

const PXR_NS::SdfPath &
HUSD_Path::sdfPath() const
{
    return *reinterpret_cast<const SdfPath *>(mySdfPathData);
}

HUSD_Path
HUSD_Path::parentPath() const
{
    return HUSD_Path(sdfPath().GetParentPath());
}

HUSD_Path
HUSD_Path::primPath() const
{
    return HUSD_Path(sdfPath().GetPrimPath());
}

HUSD_Path
HUSD_Path::appendChild(const UT_StringRef &name) const
{
    return HUSD_Path(sdfPath().AppendChild(TfToken(name.toStdString())));
}

HUSD_Path
HUSD_Path::appendProperty(const UT_StringRef &name) const
{
    return HUSD_Path(sdfPath().AppendProperty(TfToken(name.toStdString())));
}

void
HUSD_Path::pathStr(UT_WorkBuffer &outpath) const
{
    std::string str = sdfPath().GetAsString();

    outpath.strcpy(str);
}

UT_StringHolder
HUSD_Path::pathStr() const
{
    std::string str = sdfPath().GetAsString();

    return UT_StringHolder(str);
}

UT_StringHolder
HUSD_Path::nameStr() const
{
    return reinterpret_cast<const SdfPath *>(mySdfPathData)->GetName();
}

void *
HUSD_Path::getPythonPath() const
{
    PY_InterpreterAutoLock	 pylock;

    return BOOST_NS::python::incref(TfPyObject<SdfPath>(sdfPath()).ptr());
}
