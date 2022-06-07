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

XUSD_SelectionRuleAutoCollection::XUSD_SelectionRuleAutoCollection(
        const UT_StringHolder &collectionname,
        const UT_StringArray &orderedargs,
        const UT_StringMap<UT_StringHolder> &namedargs,
        HUSD_AutoAnyLock &lock,
        HUSD_PrimTraversalDemands demands,
        int nodeid,
        const HUSD_TimeCode &timecode)
     : XUSD_AutoCollection(collectionname, orderedargs, namedargs,
          lock, demands, nodeid, timecode),
       mySelectionRule(
          (orderedargs.size() > 0) ? orderedargs[0] : UT_StringHolder()),
       myMayBeTimeVarying(false)
{
    if (!getSelectionRule())
        myTokenParsingError = "Couldn't find the specified selection rule.";
}

XUSD_SelectionRuleAutoCollection::~XUSD_SelectionRuleAutoCollection()
{
}

const LOP_SelectionRule *
XUSD_SelectionRuleAutoCollection::getSelectionRule() const
{
    LOP_Node *lopnode = CAST_LOPNODE(OP_Node::lookupNode(myNodeId));

    if (lopnode)
    {
        LOP_Network *lopnet =
            dynamic_cast<LOP_Network *>(lopnode->getCreator());

        if (lopnet)
        {
            auto it = lopnet->selectionRules().find(mySelectionRule);

            if (it != lopnet->selectionRules().end())
                return &it->second;
        }
    }

    return nullptr;
}

void
XUSD_SelectionRuleAutoCollection::matchPrimitives(XUSD_PathSet &matches) const
{
    const LOP_SelectionRule *rule = getSelectionRule();

    if (rule)
    {
        HUSD_PathSet     pathset;

        rule->getExpandedPathSet(myLock, myNodeId, myHusdTimeCode,
            pathset, &myMayBeTimeVarying);
        matches.swap(pathset.sdfPathSet());
    }
}

bool
XUSD_SelectionRuleAutoCollection::getMayBeTimeVarying() const
{
    return myMayBeTimeVarying;
}

PXR_NAMESPACE_CLOSE_SCOPE

