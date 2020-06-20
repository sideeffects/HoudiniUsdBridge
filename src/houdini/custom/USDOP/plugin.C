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

#include "pxr/base/arch/export.h"
#include <gusd/gusd.h>
#include "HUSD_FieldWrapper.h"
#include "XUSD_SelectionRuleAutoCollection.h"
#include "OBJ_LOP.h"
#include "OBJ_LOPCamera.h"
#include "SOP_LOP.h"
#include "SOP_UnpackUSD.h"
#include <UT/UT_DSOVersion.h>
#include <SYS/SYS_Version.h>

ARCH_EXPORT
void 
newSopOperator(OP_OperatorTable* operators) 
{
    PXR_NS::GusdInit();
    PXR_NS::HUSD_FieldWrapper::registerForRead();
    PXR_NS::SOP_LOP::Register(operators);
    PXR_NS::SOP_UnpackUSD::Register(operators);
}

ARCH_EXPORT
void 
newObjectOperator(OP_OperatorTable* operators) 
{
    PXR_NS::GusdInit();
    PXR_NS::HUSD_FieldWrapper::registerForRead();
    PXR_NS::OBJ_LOP::Register(operators);
    PXR_NS::OBJ_LOPCamera::Register(operators);
}

ARCH_EXPORT
void
newGeometryPrim(GA_PrimitiveFactory *f) 
{
    PXR_NS::GusdInit();
    PXR_NS::HUSD_FieldWrapper::registerForRead();
    PXR_NS::GusdNewGeometryPrim(f);
}

ARCH_EXPORT
void
newGeometryIO(void *)
{
    PXR_NS::GusdInit();
    PXR_NS::HUSD_FieldWrapper::registerForRead();
    PXR_NS::GusdNewGeometryIO();
}

ARCH_EXPORT
void
newAutoCollection(void *)
{
    PXR_NS::XUSD_AutoCollection::registerPlugin(
        new PXR_NS::XUSD_SimpleAutoCollectionFactory
            <PXR_NS::XUSD_SelectionRuleAutoCollection>("rule:"));
}

