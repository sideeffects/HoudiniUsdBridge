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
    
    virtual OP_ERROR    cookMySop(OP_Context& ctx);
    virtual void	getDescriptiveParmName(UT_String &name) const
			{ name = "loppath"; }

    OP_ERROR            _Cook(OP_Context& ctx);

    OP_ERROR            _CreateNewPrims(OP_Context& ctx,
                                        const GusdUSD_Traverse* traverse);

    /** Add micro nodes of all traversal parms as dependencies
        to this node's data micro node.*/
    void                _AddTraversalParmDependencies();

    virtual void        finishedLoadingNetwork(bool isChildCall);

private:
    UT_Array<PRM_Template>  myTemplates;
    PRM_Default             myTabs[2];
    HUSD_LockedStagePtr	    myLockedStage;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif /*_GUSD_SOP_LOP_H_*/
