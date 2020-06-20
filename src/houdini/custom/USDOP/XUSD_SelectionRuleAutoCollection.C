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

#include "XUSD_SelectionRuleAutoCollection.h"
#include <LOP/LOP_Node.h>
#include <LOP/LOP_Network.h>
#include <LOP/LOP_SelectionRule.h>

PXR_NAMESPACE_OPEN_SCOPE

void
XUSD_SelectionRuleAutoCollection::matchPrimitives(HUSD_AutoAnyLock &lock,
        HUSD_PrimTraversalDemands demands,
        int nodeid,
        const HUSD_TimeCode &timecode,
        XUSD_PathSet &matches,
        UT_StringHolder &error) const
{
    LOP_Node *lopnode = CAST_LOPNODE(OP_Node::lookupNode(nodeid));

    if (lopnode)
    {
        LOP_Network *lopnet =
            dynamic_cast<LOP_Network *>(lopnode->getCreator());

        if (lopnet)
        {
            auto it = lopnet->selectionRules().find(mySelectionRule);

            if (it != lopnet->selectionRules().end())
            {
                HUSD_PathSet pathset;
                it->second.getExpandedPathSet(lock, nodeid, timecode,
                    pathset, error);
                matches.swap(pathset.sdfPathSet());
            }
            else
                error = "Couldn't find the specified selection rule.";
        }
    }
}

PXR_NAMESPACE_CLOSE_SCOPE

