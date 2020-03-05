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

#ifndef _GUSD_SOP_LOP_H_
#define _GUSD_SOP_LOP_H_

#include <PRM/PRM_Template.h>
#include <SOP/SOP_Node.h>
#include <HUSD/HUSD_LockedStage.h>
#include <pxr/pxr.h>

PXR_NAMESPACE_OPEN_SCOPE

class GusdUSD_Traverse;

class SOP_LOP : public SOP_Node
{
public:
    static void         Register(OP_OperatorTable* table);
    static OP_Node*     Create(OP_Network* net,
                               const char* name,
                               OP_Operator* op);

    void                UpdateTraversalParms();

protected:
    SOP_LOP(OP_Network* net, const char* name, OP_Operator* op);
    virtual ~SOP_LOP();
    
    virtual OP_ERROR    cookMySop(OP_Context& ctx) override;
    virtual void	getDescriptiveParmName(UT_String &name) const override
			{ name = "loppath"; }

    OP_ERROR            _Cook(OP_Context& ctx);

    OP_ERROR            _CreateNewPrims(OP_Context& ctx,
                                        const GusdUSD_Traverse* traverse);

    /** Add micro nodes of all traversal parms as dependencies
        to this node's data micro node.*/
    void                _AddTraversalParmDependencies();

    virtual void        finishedLoadingNetwork(bool isChildCall) override;

    virtual void        syncNodeVersion(const char *old_version,
                                        const char *cur_version,
                                        bool *node_deleted) override;

private:
    UT_Array<PRM_Template>  myTemplates;
    PRM_Default             myTabs[2];
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif /*_GUSD_SOP_LOP_H_*/
