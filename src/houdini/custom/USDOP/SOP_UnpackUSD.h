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

#ifndef __GUSD_SOP_USDUNPACK_H__
#define __GUSD_SOP_USDUNPACK_H__

#include <PRM/PRM_Template.h>
#include <SOP/SOP_Node.h>

#include "gusd/defaultArray.h"
#include "gusd/purpose.h"
#include "gusd/USD_Traverse.h"

#include <pxr/pxr.h>
#include "pxr/usd/usd/prim.h"

PXR_NAMESPACE_OPEN_SCOPE

class GusdUSD_Traverse;


class SOP_UnpackUSD : public SOP_Node
{
public:
    static OP_Node*         Create(OP_Network* net,
                                   const char* name,
                                   OP_Operator* op);

    void                    UpdateTraversalParms();
    
protected:
    SOP_UnpackUSD(OP_Network* net, const char* name, OP_Operator* op);
    
    virtual ~SOP_UnpackUSD() {}

    virtual OP_ERROR    cookMySop(OP_Context& ctx) override;

    virtual OP_ERROR    cookInputGroups(OP_Context& ctx, int alone) override;

    OP_ERROR            _Cook(OP_Context& ctx);

    bool _Traverse(const UT_String& traversal,
                   const fpreal time,
                   const UT_Array<UsdPrim>& prims,
                   const GusdDefaultArray<UsdTimeCode>& times,
                   const GusdDefaultArray<GusdPurposeSet>& purposes,
                   bool skipRoot,
                   UT_Array<GusdUSD_Traverse::PrimIndexPair>& traversed);


    /** Add micro nodes of all traversal parms as dependencies
        to this node's data micro node.*/
    void                _AddTraversalParmDependencies();

    virtual void        finishedLoadingNetwork(bool isChildCall) override;

    virtual void        syncNodeVersion(const char *old_version,
                                        const char *cur_version,
                                        bool *node_deleted) override;

private:
    UT_Array<PRM_Template>  _templates;
    PRM_Default             _tabs[2];
    const GA_Group*         _group;

public:
    static void         Register(OP_OperatorTable* table);
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // __GUSD_SOP_USDUNPACK_H__
