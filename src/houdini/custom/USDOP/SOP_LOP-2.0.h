/*
 * Copyright 2021 Side Effects Software Inc.
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

#ifndef __SOP_LOP2_h__
#define __SOP_LOP2_h__

#include <SOP/SOP_Node.h>

#include <pxr/pxr.h>

PXR_NAMESPACE_OPEN_SCOPE

class SOP_LOP2 : public SOP_Node
{
public:
    static OP_Operator *createOperator();
    static PRM_Template *buildTemplates();

    static OP_Node *myConstructor(
            OP_Network *net,
            const char *name,
            OP_Operator *op)
    {
        return new SOP_LOP2(net, name, op);
    }

    const SOP_NodeVerb *cookVerb() const override;

protected:
    SOP_LOP2(OP_Network *net, const char *name, OP_Operator *op);

    OP_ERROR cookMySop(OP_Context &context) override;

    void checkTimeDependencies(int do_parms, int do_inputs, int do_extras)
            override;

    void getDescriptiveParmName(UT_String &name) const override;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
