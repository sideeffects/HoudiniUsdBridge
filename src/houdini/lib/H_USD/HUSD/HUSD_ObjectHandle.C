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

#include "HUSD_ObjectHandle.h"
#include <UT/UT_String.h>

PXR_NAMESPACE_USING_DIRECTIVE

using namespace UT::Literal;

const UT_StringHolder	 HUSD_ObjectHandle::theRootPrimPath = "/"_sh;
const UT_StringHolder	 HUSD_ObjectHandle::theRootPrimName = "/"_sh;

HUSD_ObjectHandle::HUSD_ObjectHandle(
        OverridesHandling overrides_handling)
    : myOverridesHandling(overrides_handling)
{
}

HUSD_ObjectHandle::HUSD_ObjectHandle(const UT_StringHolder &path,
	const UT_StringHolder &name,
        OverridesHandling overrides_handling)
    : myPath(path),
      myName(name),
      myOverridesHandling(overrides_handling)
{
    if (!myPath.isstring())
        myPath = theRootPrimPath;

    if (!myName.isstring())
    {
        // Figure out our name by looking at the last component of our path.
        if (myPath == theRootPrimPath)
        {
            myName = theRootPrimName;
        }
        else
        {
            UT_String    pathstr(path);
            UT_String    dirstr;
            UT_String    filestr;

            pathstr.splitPath(dirstr, filestr);
            myName = filestr;
        }
    }
}

HUSD_ObjectHandle::~HUSD_ObjectHandle()
{
}

