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

#include "pxr/base/arch/export.h"
#include <gusd/gusd.h>
#include "OBJ_LOP.h"
#include "OBJ_LOPCamera.h"
#include "SOP_LOP.h"
#include "SOP_UnpackUSD.h"
#include <UT/UT_DSOVersion.h>
#include <SYS/SYS_Version.h>

#ifdef BUILDING_HOUDINIUSD
#define SOLARIS_ENABLED true
#else
#include <LM/LM_Solaris.h>
#define SOLARIS_ENABLED LMisSolarisEnabled()
#endif

ARCH_EXPORT
void 
newSopOperator(OP_OperatorTable* operators) 
{
    if (SOLARIS_ENABLED)
    {
	PXR_NS::GusdInit();
	PXR_NS::SOP_LOP::Register(operators);
	PXR_NS::SOP_UnpackUSD::Register(operators);
    }
}

ARCH_EXPORT
void 
newObjectOperator(OP_OperatorTable* operators) 
{
    if (SOLARIS_ENABLED)
    {
	PXR_NS::GusdInit();
	PXR_NS::OBJ_LOP::Register(operators);
	PXR_NS::OBJ_LOPCamera::Register(operators);
    }
}

ARCH_EXPORT
void
newGeometryPrim(GA_PrimitiveFactory *f) 
{
    if (SOLARIS_ENABLED)
    {
	PXR_NS::GusdInit();
	PXR_NS::GusdNewGeometryPrim(f);
    }
}

ARCH_EXPORT
void
newGeometryIO(void *)
{
    if (SOLARIS_ENABLED)
    {
	PXR_NS::GusdInit();
	PXR_NS::GusdNewGeometryIO();
    }
}

