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
 *      Side Effects Software Inc.
 *      123 Front Street West, Suite 1401
 *      Toronto, Ontario
 *      Canada   M5J 2M2
 *      416-504-9876
 *
 */

#pragma once

#include <pxr/pxr.h>
#include <pxr/imaging/hd/perfLog.h>
#include "HDFormat.h"   // Also includes many POD types
#include "HDParam.h"    // Also includes many POD types
#include "HDOptions.h"  // Debug options
#include <UT/UT_Assert.h>
#include <UT/UT_Debug.h>
#include <UT/UT_StringHolder.h>
#include <UT/UT_Map.h>


PXR_NAMESPACE_OPEN_SCOPE        // [

class HdSceneDelegate;
class HdRenderParam;
class HdMeshTopology;

#define HDEBUG_ASSERT(cond) UT_ASSERT(cond);
#define HDEBUG_VERIFY(cond) UT_VERIFY(cond);

namespace HDUtil
{
    /// Clear memory tables
    void        clearTables();

    struct Primvars
    {
        ~Primvars() { clear(); }

        struct Sample
        {
	    int64	computeMemory(const TfToken &name);

	    std::unique_ptr<VtValue[]>		myValues;
	    std::unique_ptr<VtIntArray[]>	myIndirect;
	    size_t				myMemory = 0;
	    int					mySize = 0;
        };
        void    setId(const SdfPath &id);
        void    clear();

        UT_StringHolder toString() const;

        SdfPath                 myId;
        UT_StringHolder         myName; // primvars:<id>
	UT_Map<TfToken, Sample> myVars;
	size_t			myMemory = 0;
    };

    /// Load primvars
    void        load(Primvars &primvars,
                    bool sample_time,
                    HdSceneDelegate &sd,
                    HDParam &parm,
                    const SdfPath &id,
                    const HdInterpolation *interp,
                    int ninterp);

    /// Load topology into primvars
    void        load(Primvars &primvars,
                    const SdfPath &id,
                    const HdMeshTopology &top);

    /// Resolve materials
    void        resolveMaterial(HdSceneDelegate &sd,
                    HDParam &parm,
                    const SdfPath &id);

    /// Return the memory consumed by primvars for the object.  This tracks
    /// primvar memory by primvar name
    void        primvarMemory(bool sample_time,
                        HdSceneDelegate &sd,
                        HDParam &parm,
                        const SdfPath &id,
                        const HdInterpolation *interp,
                        int ninterp);
    /// Return the memory consumed by primvars for the object
    inline void primvarMemory(bool sample_time,
                        HdSceneDelegate &sd,
                        HDParam &parm,
                        const SdfPath &id,
                        const HdInterpolation interp)
                {
                    primvarMemory(sample_time, sd, parm, id, &interp, 1);
                }
    UT_StringHolder     dumpPrimvarMemory();

    /// Track object counts
    void                trackObjects(const UT_StringHolder &label, int64 count);
    UT_StringHolder     dumpObjects();

    /// Track allocated memory
    void                trackMemory(const UT_StringHolder &label, int64 amount);
    UT_StringHolder     dumpMemory();

    /// Print system memory
    UT_StringHolder     systemUsage();
};

#define HDEBUG_TRACE_FUNCTION() \
    if (HDOptions::trace()) UTformat(stderr, "Call: {}\n", __ARCH_PRETTY_FUNCTION__); \
    HD_TRACE_FUNCTION() \
    /* end macro */

#define HDEBUG_CTOR()        \
    HDEBUG_TRACE_FUNCTION() \
    if (HDOptions::object()) HDUtil::trackObjects(class_name, 1); \
    if (HDOptions::memory()) HDUtil::trackMemory(class_name, sizeof(*this)); \
    /* end macro */
#define HDEBUG_DTOR()        \
    HDEBUG_TRACE_FUNCTION() \
    if (HDOptions::object()) HDUtil::trackObjects(class_name, -1); \
    if (HDOptions::memory()) HDUtil::trackMemory(class_name, -sizeof(*this)); \
    /* end macro */

PXR_NAMESPACE_CLOSE_SCOPE       // ]
