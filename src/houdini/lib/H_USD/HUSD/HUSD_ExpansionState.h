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

#ifndef __HUSD_ExpansionState_h__
#define __HUSD_ExpansionState_h__

#include "HUSD_API.h"
#include "HUSD_Path.h"
#include "HUSD_PathSet.h"
#include <UT/UT_IntrusivePtr.h>
#include <UT/UT_NonCopyable.h>
#include <iosfwd>

class UT_IStream;
class UT_JSONValue;
class UT_JSONWriter;
class HUSD_ExpansionState;

typedef UT_IntrusivePtr<HUSD_ExpansionState> HUSD_ExpansionStateHandle;

class HUSD_API HUSD_ExpansionState :
    public UT_IntrusiveRefCounter<HUSD_ExpansionState>,
    public UT_NonCopyable
{
public:
				 HUSD_ExpansionState();
				~HUSD_ExpansionState();

    const HUSD_PathSet          &expandedPinnedPaths() const
                                 { return myExpandedPinnedPaths; }
    const HUSD_PathSet          &expandedScenePaths() const
                                 { return myExpandedScenePaths; }
    void			 setExpanded(const HUSD_Path &path,
                                        bool pinned,
                                        bool expanded);

    exint			 getMemoryUsage() const;

    void			 clear();
    void			 copy(const HUSD_ExpansionState &src);
    bool			 save(std::ostream &os, bool binary) const;
    bool			 load(UT_IStream &is);

private:
    bool			 save(UT_JSONWriter &writer,
                                        const HUSD_PathSet &paths,
                                        bool allow_saving_indirect_descendants,
                                        HUSD_PathSet::iterator &iter) const;
    bool			 load(const UT_JSONValue &value,
                                        const HUSD_Path &path,
                                        HUSD_PathSet &paths);

    HUSD_PathSet                 myExpandedPinnedPaths;
    HUSD_PathSet                 myExpandedScenePaths;
};

#endif
