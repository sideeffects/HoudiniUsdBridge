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

#include "HUSD_PerfMonAutoCookEvent.h"
#include <OP/OP_Node.h>
#include <UT/UT_IntArray.h>
#include <UT/UT_ThreadSpecificValue.h>

namespace
{
    UT_ThreadSpecificValue<UT_IntArray>      theCookingNodeIds;
}

HUSD_PerfMonAutoCookBlock::HUSD_PerfMonAutoCookBlock(int nodeid)
    : myNodeId(nodeid)
{
    theCookingNodeIds.get().append(myNodeId);
}

HUSD_PerfMonAutoCookBlock::~HUSD_PerfMonAutoCookBlock()
{
    UT_ASSERT(!theCookingNodeIds.get().isEmpty() &&
              theCookingNodeIds.get().last() == myNodeId);
    theCookingNodeIds.get().removeLast();
}

HUSD_PerfMonAutoCookEvent::HUSD_PerfMonAutoCookEvent(
        const char *msg,
        int msg_nodeid)
{
    UT_Performance *perfmon = UTgetPerformance();

    if (!theCookingNodeIds.get().isEmpty() && perfmon->isRecordingCookStats())
    {
        OP_Node *node = OP_Node::lookupNode(theCookingNodeIds.get().last());

        if (node && node->isCooking(false))
        {
            if (msg_nodeid != OP_INVALID_NODE_ID)
            {
                static constexpr UT_StringLit theNoNodeString = "Unknown Node";
                UT_WorkBuffer msgbuf;
                OP_Node *msg_node = OP_Node::lookupNode(msg_nodeid);

                msgbuf.format(msg, msg_node
                    ? msg_node->getFullPath()
                    : theNoNodeString);
                setTimedEventId_(perfmon->startTimedCookEvent(
                    theCookingNodeIds.get().last(), msgbuf.buffer()));
            }
            else
                setTimedEventId_(perfmon->startTimedCookEvent(
                    theCookingNodeIds.get().last(), msg));
        }
    }
}

HUSD_PerfMonAutoCookEvent::~HUSD_PerfMonAutoCookEvent()
{
}
