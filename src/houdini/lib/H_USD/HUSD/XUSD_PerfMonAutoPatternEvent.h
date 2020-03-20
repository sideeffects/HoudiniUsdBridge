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

#ifndef __XUSD_PerfMonAutoPatternEvent_h__
#define __XUSD_PerfMonAutoPatternEvent_h__

#include "HUSD_API.h"
#include <OP/OP_Node.h>
#include <UT/UT_PerfMonAutoEvent.h>
#include <pxr/pxr.h>

PXR_NAMESPACE_OPEN_SCOPE

class XUSD_PerfMonAutoPatternEvent : public UT_PerfMonAutoEvent
{
public:
    XUSD_PerfMonAutoPatternEvent(int nodeid)
    {
        UT_Performance      *perfmon = UTgetPerformance();

        if (perfmon->isRecordingCookStats())
        {
            OP_Node         *node = OP_Node::lookupNode(nodeid);

            if (node && node->isCooking(false))
                setTimedEventId_(perfmon->startTimedCookEvent(nodeid,
                    "Primitive pattern evaluation"));
        }
    }
    ~XUSD_PerfMonAutoPatternEvent()
    { }
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif

