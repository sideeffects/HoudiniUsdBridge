//
// Copyright 2017 Pixar
//
// Licensed under the Apache License, Version 2.0 (the "Apache License")
// with the following modification; you may not use this file except in
// compliance with the Apache License and the following modification to it:
// Section 6. Trademarks. is deleted and replaced with:
//
// 6. Trademarks. This License does not grant permission to use the trade
//    names, trademarks, service marks, or product names of the Licensor
//    and its affiliates, except as required to comply with Section 4(c) of
//    the License and to reproduce the content of the NOTICE file.
//
// You may obtain a copy of the Apache License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the Apache License with the above modification is
// distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied. See the Apache License for the specific
// language governing permissions and limitations under the Apache License.
//
#include "gusd/USD_StdTraverse.h"

#include "gusd/USD_Traverse.h"
#include "gusd/USD_TraverseSimple.h"

#include "pxr/base/arch/hints.h"

#include "pxr/usd/kind/registry.h"
#include "pxr/usd/usd/modelAPI.h"

#include "pxr/usd/usdGeom/boundable.h"

#include "pxr/usd/usdVol/fieldBase.h"

PXR_NAMESPACE_OPEN_SCOPE

using GusdUSD_ThreadedTraverse::DefaultImageablePrimVisitorT;

namespace GusdUSD_StdTraverse {

    namespace {

        struct _VisitGroups
        {
            bool    operator()(const UsdPrim& prim,
                UsdTimeCode time,
                GusdUSD_TraverseControl& ctl)
            {
                if(ARCH_UNLIKELY(prim.IsGroup())) {
                    ctl.PruneChildren();
                    return true;
                }
                return false;
            }
        };
        typedef DefaultImageablePrimVisitorT<
            _VisitGroups>
            _VisitImageableGroups;

        struct _VisitComponentsAndBoundablesAndFields
        {
            bool    operator()(const UsdPrim& prim,
                UsdTimeCode time,
                GusdUSD_TraverseControl& ctl)
            {

                if(ARCH_UNLIKELY(prim.IsA<UsdGeomBoundable>())) {
                    ctl.PruneChildren();
                    return true;
                }
                if(ARCH_UNLIKELY(prim.IsA<UsdVolFieldBase>())) {
                    ctl.PruneChildren();
                    return true;
                }
                UsdModelAPI model(prim);
                if(ARCH_UNLIKELY((bool)model)) {
                    TfToken kind;
                    model.GetKind(&kind);
                    if(ARCH_UNLIKELY(
                        KindRegistry::IsA(kind, KindTokens->component) ||
                        KindRegistry::IsA(kind, KindTokens->subcomponent))) {
                        ctl.PruneChildren();
                        return true;
                    }
                }
                return false;
            }
        };
        typedef DefaultImageablePrimVisitorT<
            _VisitComponentsAndBoundablesAndFields>
            _VisitImageableComponentsAndBoundablesAndFields;

        struct _VisitBoundablesAndFields
        {
            bool    operator()(const UsdPrim& prim,
                               UsdTimeCode time,
                               GusdUSD_TraverseControl& ctl)
                    {
                        if(ARCH_UNLIKELY(prim.IsA<UsdGeomBoundable>())) {
                            ctl.PruneChildren();
                            return true;
                        }
                        if(ARCH_UNLIKELY(prim.IsA<UsdVolFieldBase>())) {
                            ctl.PruneChildren();
                            return true;
                        }
                        return false;
                    }
        };
        typedef DefaultImageablePrimVisitorT<
            _VisitBoundablesAndFields>
            _VisitImageableBoundablesAndFields;

    } /*namespace*/

    #define _DECLARE_STATIC_TRAVERSAL(name,visitor)         \
        const GusdUSD_Traverse& name()                      \
        {                                                   \
            static visitor v;                               \
            static GusdUSD_TraverseSimpleT<visitor> t(v);   \
            return t;                                       \
        }

    _DECLARE_STATIC_TRAVERSAL(GetGroupTraversal,
        _VisitImageableGroups);
    _DECLARE_STATIC_TRAVERSAL(GetComponentAndBoundableAndFieldTraversal,
        _VisitImageableComponentsAndBoundablesAndFields);
    _DECLARE_STATIC_TRAVERSAL(GetBoundableAndFieldTraversal,
        _VisitImageableBoundablesAndFields);

    namespace {

        GusdUSD_TraverseType stdTypes[] = {
            GusdUSD_TraverseType(&GetComponentAndBoundableAndFieldTraversal(),
                "std:components",
                "Components", NULL,
                "Returns default-imageable models with a "
                "component-derived kind."),
            GusdUSD_TraverseType(&GetGroupTraversal(),
                "std:groups",
                "Groups", NULL,
                "Returns default-imageable groups (of any kind)."),
            GusdUSD_TraverseType(&GetBoundableAndFieldTraversal(),
                "std:boundables",
                "Gprims", NULL,
                "Return leaf geometry primitives, instances, and procedurals."),
        };

    } /*namespace*/

} /*namespace GusdUSD_StdTraverse */

PXR_NAMESPACE_CLOSE_SCOPE
