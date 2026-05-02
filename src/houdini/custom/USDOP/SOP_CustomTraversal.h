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
*/

#ifndef __GUSD_SOP_CUSTOMTRAVERSAL_H__
#define __GUSD_SOP_CUSTOMTRAVERSAL_H__

#include <PRM/PRM_Template.h>
#include <UT/UT_Array.h>
#include <pxr/pxr.h>

class PRM_ChoiceList;

#define _NOTRAVERSE_NAME "none"
#define _GPRIMTRAVERSE_NAME "std:boundables"

PXR_NAMESPACE_OPEN_SCOPE

class SOP_CustomTraversal
{
public:
    static void RegisterCustomTraversal();
    static void ConcatTemplates(UT_Array<PRM_Template>& array,
        const PRM_Template* templates);
    static PRM_ChoiceList &CreateTraversalMenu();
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // __GUSD_SOP_CUSTOMTRAVERSAL_H__
