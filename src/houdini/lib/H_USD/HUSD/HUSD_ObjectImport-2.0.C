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

#include "HUSD_Constants.h"
#include "HUSD_CreateMaterial.h"
#include "HUSD_CreatePrims.h"
#include "HUSD_EditLayers.h"
#include "HUSD_EditReferences.h"
#include "HUSD_FindPrims.h"
#include "HUSD_ObjectImport-2.0.h"
#include "HUSD_Preferences.h"
#include "HUSD_TimeCode.h"
#include "HUSD_Utils.h"
#include "XUSD_AttributeUtils.h"
#include "XUSD_Data.h"
#include "XUSD_PathSet.h"
#include "XUSD_Utils.h"
#include <OBJ/OBJ_Node.h>
#include <SHOP/SHOP_Node.h>
#include <SOP/SOP_Node.h>
#include <SYS/SYS_FormatNumber.h>
#include <UT/UT_OpUtils.h>
#include <VOP/VOP_Node.h>
#include <gusd/UT_Gf.h>
#include <initializer_list>
#include <pxr/base/vt/types.h>
#include <pxr/usd/sdf/attributeSpec.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/usd/primDefinition.h>
#include <pxr/usd/usd/relationship.h>
#include <pxr/usd/usd/schemaRegistry.h>

#define ADDPARMINDEX(index)                                                    \
    if (index != -1)                                                           \
        parmindices->insert(index);

PXR_NAMESPACE_USING_DIRECTIVE

HUSD_ObjectImport2::HUSD_ObjectImport2(HUSD_AutoWriteLock &lock)
    : myWriteLock(lock)
{
}

HUSD_ObjectImport2::~HUSD_ObjectImport2() {}

namespace
{
const PRM_Parm *
husdGetParm(
        const PRM_ParmList *parmlist,
        const UT_StringHolder &parmname,
        UT_Set<int> *parmindices)
{
    int index = parmlist->getParmIndex(parmname);
    if (index == -1)
    {
        // UTformat("Parm \"{}\" not found\n", parmname);

        return nullptr;
    }

    const PRM_Parm *parm = parmlist->getParmPtr(index);

    if (parmindices != nullptr)
        parmindices->insert(index);

    return parm;
}

template <typename UT_VALUE_TYPE>
void
husdGetParmValue(const PRM_Parm *parm, const fpreal time, UT_VALUE_TYPE &value)
{
    exint d = UT_VALUE_TYPE::tuple_size;

    if (parm == nullptr)
    {
        for (int i = 0; i < d; i++)
            value[i] = 0.0;
        return;
    }

    exint n = SYSmax(parm->getVectorSize(), d);

    UT_Array<fpreal64> data(n, n);
    parm->getValues(time, data.data(), SYSgetSTID());

    for (int i = 0; i < d; i++)
        value[i] = data[i];
}

template <>
void
husdGetParmValue<fpreal>(const PRM_Parm *parm, const fpreal time, fpreal &value)
{
    if (parm != nullptr)
        parm->getValue(time, value, 0, SYSgetSTID());
    else
        value = 0.0;
}

template <>
void
husdGetParmValue<int>(const PRM_Parm *parm, const fpreal time, int &value)
{
    if (parm != nullptr)
        parm->getValue(time, value, 0, SYSgetSTID());
    else
        value = 0.0;
}

template <>
void
husdGetParmValue<bool>(const PRM_Parm *parm, const fpreal time, bool &value)
{
    int intvalue;
    husdGetParmValue(parm, time, intvalue);
    value = intvalue != 0;
}

template <>
void
husdGetParmValue<UT_StringHolder>(
        const PRM_Parm *parm,
        const fpreal time,
        UT_StringHolder &value)
{
    if (parm != nullptr)
        parm->getValue(time, value, 0, true, SYSgetSTID());
    else
        value = "";
}

template <typename UT_VALUE_TYPE>
int
husdGetParmValue(
        const PRM_ParmList *parmlist,
        const UT_StringHolder &parmname,
        const fpreal time,
        UT_VALUE_TYPE &value)
{
    int index = parmlist->getParmIndex(parmname);

    if (index == -1)
        return -1;

    const PRM_Parm *parm = parmlist->getParmPtr(parmname);

    husdGetParmValue(parm, time, value);

    return index;
}

UsdTimeCode
husdGetTimeCode(bool timedep, const UsdTimeCode &timecode)
{
    return timedep ? timecode : UsdTimeCode::Default();
}

bool
husdAnyParmTimeDependent(const std::initializer_list<const PRM_Parm *> parms)
{
    bool timedep = false;
    for (auto &&parm : parms)
    {
        if (!parm)
            continue;

        if (parm->isTimeDependent())
        {
            timedep = true;
        }
    }

    return timedep;
}

bool
husdSetRelationship(
        const UsdRelationship &rel,
        const UT_StringHolder &value,
        const UsdTimeCode &usdtimecode)
{
    SdfPathVector targets;

    targets.push_back(HUSDgetSdfPath(value.toStdString()));
    return rel.SetTargets(targets);
}

int
husdSetRelationshipToParmValue(
        const UsdRelationship &attr,
        const UsdTimeCode &usdtimecode,
        const PRM_ParmList *parmlist,
        const UT_StringHolder &parmname,
        const fpreal time,
        bool firsttime)
{
    int index = parmlist->getParmIndex(parmname);
    if (index == -1)
        return -1;

    const PRM_Parm *parm = parmlist->getParmPtr(parmname);

    bool timedep = parm->isTimeDependent();

    if (firsttime || timedep)
    {
        UT_StringHolder parmvalue;
        husdGetParmValue(parm, time, parmvalue);

        if (parmvalue.isstring())
        {
            husdSetRelationship(
                    attr, parmvalue, husdGetTimeCode(timedep, usdtimecode));
        }
    }

    return index;
}

}

UT_StringHolder
HUSD_ObjectImport2::getPrimKindForObject(const OP_Node *node)
{
    OBJ_Node *object = node->castToOBJNode();

    if (!object)
        return UT_StringHolder::theEmptyString;

    auto objtype = object->getObjectType();
    if (objtype & OBJ_NULL)
    {
        return HUSD_Constants::getKindGroup();
    }
    else if (objtype & OBJ_SUBNET)
    {
        return HUSD_Constants::getKindGroup();
    }

    return UT_StringHolder::theEmptyString;
}

bool
HUSD_ObjectImport2::importPrim(
        const OBJ_Node *object,
        const UT_StringHolder &primpath,
        const UT_StringHolder &primtype,
        const UT_StringHolder &primkind) const
{
    auto outdata = myWriteLock.data();

    if (!outdata || !outdata->isStageValid())
        return false;

    HUSD_AutoLayerLock layerlock(myWriteLock);
    HUSD_CreatePrims creator(layerlock);

    if (!creator.createPrim(
                primpath, primtype, primkind,
                HUSD_Constants::getPrimSpecifierDefine(),
                HUSD_Constants::getXformPrimType()))
        return false;

    auto prim = outdata->stage()->GetPrimAtPath(HUSDgetSdfPath(primpath));

    HUSDsetSourceNode(prim, object->getUniqueId());

    return true;
}

void
HUSD_ObjectImport2::recordSOPForImport(
        SOP_Node *sop,
        OP_Context &context,
        const UT_StringMap<UT_StringHolder> &args,
        const UT_StringRef &refprimpath,
        const UT_StringRef &primpath)
{
    UT_String sopfilepath;
    sopfilepath.sprintf("%s%s.sop", OPREF_PREFIX, sop->getFullPath().c_str());
    GU_DetailHandle gdh = sop->getCookedGeoHandle(context);
    
    mySopImportArgs.emplace_back(args);
    mySopImportGDHs.emplace_back(gdh);
    mySopImportFilePaths.emplace_back(sopfilepath);
    mySopImportPrimPaths.emplace_back(primpath);
    mySopImportRefPrimPaths.emplace_back(refprimpath);
}

void 
HUSD_ObjectImport2::importAllRecordedSOPs(bool asreference /*=true*/)
{
    if (asreference)
    {
        HUSD_EditReferences addref(myWriteLock);
        addref.setRefType(HUSD_Constants::getReferenceTypePayload());
        for (int i = 0; i < mySopImportPrimPaths.size(); ++i)
            addref.addReference(
                mySopImportPrimPaths(i),
                mySopImportFilePaths(i), 
                mySopImportRefPrimPaths(i), 
                HUSD_LayerOffset(), 
                mySopImportArgs(i),
                mySopImportGDHs(i));
    }
    else
    {
        HUSD_EditLayers addlayer(myWriteLock);
        addlayer.addLayers(mySopImportFilePaths,
                           UT_Array<HUSD_LayerOffset>(),
                           mySopImportArgs,
                           mySopImportGDHs);
    }
}

bool
HUSD_ObjectImport2::importMaterial(
        VOP_Node *vop,
        const UT_StringHolder &primpath) const
{
    auto outdata = myWriteLock.data();

    if (!outdata || !outdata->isStageValid())
        return false;

    HUSD_CreateMaterial husdmat(myWriteLock);
    husdmat.setParentPrimType(HUSD_Constants::getScopePrimType());
    if (!husdmat.createMaterial(*vop, primpath, /*gen_preview_shader=*/true))
        return false;

    auto prim = outdata->stage()->GetPrimAtPath(HUSDgetSdfPath(primpath));

    HUSDsetSourceNode(prim, vop->getUniqueId());

    return true;
}
