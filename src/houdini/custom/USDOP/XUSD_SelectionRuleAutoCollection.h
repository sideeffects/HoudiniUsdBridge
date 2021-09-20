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

#include <HUSD/XUSD_AutoCollection.h>

class LOP_SelectionRule;

PXR_NAMESPACE_OPEN_SCOPE

class XUSD_SelectionRuleAutoCollection : public XUSD_AutoCollection
{
public:
                         XUSD_SelectionRuleAutoCollection(
                                const UT_StringHolder &collectionname,
                                const UT_StringArray &orderedargs,
                                const UT_StringMap<UT_StringHolder> &namedargs,
                                HUSD_AutoAnyLock &lock,
                                HUSD_PrimTraversalDemands demands,
                                int nodeid,
                                const HUSD_TimeCode &timecode);
                        ~XUSD_SelectionRuleAutoCollection() override;

    bool                 randomAccess() const override
                         { return false; }
    void                 matchPrimitives(XUSD_PathSet &matches) const override;

private:
    const LOP_SelectionRule *getSelectionRule() const;

    UT_StringHolder      mySelectionRule;
};

PXR_NAMESPACE_CLOSE_SCOPE

