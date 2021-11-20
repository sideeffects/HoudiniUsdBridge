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

#ifndef __HUSD_Path_h__
#define __HUSD_Path_h__

#include "HUSD_API.h"
#include <UT/UT_WorkBuffer.h>
#include <stddef.h>
#include <pxr/pxr.h>

PXR_NAMESPACE_OPEN_SCOPE
class SdfPath;
PXR_NAMESPACE_CLOSE_SCOPE

class HUSD_API HUSD_Path
{
public:
                                 HUSD_Path(const HUSD_Path &path);
                                 HUSD_Path(const PXR_NS::SdfPath &path);
                                 HUSD_Path(const UT_StringRef &path);
                                 HUSD_Path();
                                ~HUSD_Path();

    const PXR_NS::SdfPath       &sdfPath() const;

    const HUSD_Path             &operator=(const HUSD_Path &path);
    const HUSD_Path             &operator=(const PXR_NS::SdfPath &path);
    bool                         operator==(const HUSD_Path &path) const;
    bool                         operator<(const HUSD_Path &path) const;

    bool                         isEmpty() const;
    bool                         isPrimPath() const;
    bool                         hasPrefix(const HUSD_Path &prefix) const;

    HUSD_Path                    parentPath() const;
    HUSD_Path                    primPath() const;
    HUSD_Path                    appendChild(const UT_StringRef &name) const;
    HUSD_Path                    appendProperty(const UT_StringRef &name) const;

    void                         pathStr(UT_WorkBuffer &outpath) const;
    UT_StringHolder              pathStr() const;
    UT_StringHolder              nameStr() const;

    // Return a python object holding an SdfPath python object.
    void                        *getPythonPath() const;

    static const HUSD_Path       theRootPrimPath;

private:
    // The size of an SdfPath object is 8. We create a block of data that
    // we will treat as an SdfPath object within the implementation.
    char                         mySdfPathData[8];
};

#endif

