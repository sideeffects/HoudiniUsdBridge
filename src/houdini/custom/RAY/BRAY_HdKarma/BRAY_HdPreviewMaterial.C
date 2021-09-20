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
 *      Side Effects Software Inc.
 *      123 Front Street West, Suite 1401
 *      Toronto, Ontario
 *      Canada   M5J 2M2
 *      416-504-9876
 *
 */

#include "BRAY_HdPreviewMaterial.h"
#include "BRAY_HdUtil.h"

#include <HUSD/XUSD_Format.h>
#include <HUSD/XUSD_Tokens.h>
#include <UT/UT_ErrorLog.h>
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/sdr/registry.h>
#include <pxr/usd/sdr/shaderNode.h>

#include <UT/UT_Debug.h>

PXR_NAMESPACE_OPEN_SCOPE

namespace
{
    static void
    setNodeParams(BRAY::ShaderGraphPtr &outgraph,
	    BRAY_ShaderInstance *braynode,
	    const HdMaterialNode &usdnode)
    {
	BRAY::OptionSet optionset = outgraph.nodeParams(braynode);
	// HdMaterialNode.parameters is of type std::map< TfToken, VtValue >
	for (auto &&p : usdnode.parameters)
	{
	    int idx = optionset.find(BRAY_HdUtil::toStr(p.first));
	    if (idx == -1)
		continue;
	    const VtValue &val = p.second;

#define HANDLE_OPTSET_SCALAR(FTYPE) \
    if (val.IsHolding<FTYPE>()) { \
	optionset.set(idx, &p.second.UncheckedGet<FTYPE>(), 1); \
	continue; \
    }
#define HANDLE_OPTSET_VECTOR_T(TYPE, METHOD, SIZE) \
    if (val.IsHolding<TYPE>()) { \
	optionset.set(idx, p.second.UncheckedGet<TYPE>().METHOD(), SIZE); \
	continue; \
    }
#define HANDLE_OPTSET_VECTOR(TYPE, METHOD, SIZE) \
    HANDLE_OPTSET_VECTOR_T(TYPE##f, METHOD, SIZE); \
    HANDLE_OPTSET_VECTOR_T(TYPE##d, METHOD, SIZE);
#define HANDLE_OPTSET_STRING(TYPE, METHOD) \
    if (val.IsHolding<TYPE>()) { \
	optionset.set(idx, val.UncheckedGet<TYPE>()METHOD); \
	continue; \
    }
	    HANDLE_OPTSET_SCALAR(fpreal32);
	    HANDLE_OPTSET_SCALAR(fpreal64);
	    HANDLE_OPTSET_SCALAR(int32);
	    HANDLE_OPTSET_SCALAR(int64);
	    HANDLE_OPTSET_SCALAR(bool);
	    HANDLE_OPTSET_VECTOR(GfVec2, data, 2);
	    HANDLE_OPTSET_VECTOR(GfVec3, data, 3);
	    HANDLE_OPTSET_VECTOR(GfVec4, data, 4);
	    HANDLE_OPTSET_VECTOR(GfMatrix4, data, 16);
	    HANDLE_OPTSET_STRING(std::string, .c_str());
	    HANDLE_OPTSET_STRING(TfToken, .GetText());
	    HANDLE_OPTSET_STRING(UT_StringHolder,)
#undef HANDLE_OPTSET_STRING
#undef HANDLE_OPTSET_VECTOR
#undef HANDLE_OPTSET_VECTOR_T
#undef HANDLE_OPTSET_SCALAR
	    if (val.IsHolding<SdfAssetPath>())
	    {
		// TODO: clean this up
		UT_StringArray tmp;
		BRAY_HdUtil::appendVexArg(tmp,
		    UT_StringHolder::theEmptyString, val);
		optionset.set(idx, tmp[1]);
	    }
	}
    };

    static BRAY_ShaderInstance *
    addNode(BRAY::ShaderGraphPtr &graph,
	    const HdMaterialNode &node,
	    BRAY_HdMaterial::ShaderType type)
    {
	BRAY_ShaderInstance	*braynode = nullptr;
	if (node.identifier == HusdHdMaterialTokens()->usdPreviewMaterial)
	{
	    UT_WorkBuffer	name;
	    name.strcpy(BRAY_HdUtil::toStr(node.identifier));
	    if (type == BRAY_HdMaterial::SURFACE)
		name.append("_surface");
	    else if (type == BRAY_HdMaterial::DISPLACE)
		name.append("_displace");
	    braynode = graph.createNode(name, BRAY_HdUtil::toStr(node.path));
	}
	else
	{
	    braynode = graph.createNode(BRAY_HdUtil::toStr(node.identifier),
				BRAY_HdUtil::toStr(node.path));
	}
	if (braynode)
	    setNodeParams(graph, braynode, node);
	else
	{
            UTdebugFormat("Unhandled Node Type: {}", node.path);
            UT_ErrorLog::error("Unhandled node type {} in material",
                    node.path, node.identifier);
	    UT_ASSERT(0 && "Unhandled Node Type");
	}
	return braynode;
    };

}

bool
BRAY_HdPreviewMaterial::convert(BRAY::ShaderGraphPtr &outgraph,
				const HdMaterialNetwork &net,
				BRAY_HdMaterial::ShaderType type)
{
    // The root node will be the last node in the array
    int num = net.nodes.size();
    if (!num)
	return false;

    // Add nodes backwards -- Hydra will put the root node at the end of the
    // list.

    // TODO: ignore irrelevant/unwired nodes (though Hydra may prune these already)
    for (int i = num; i-- > 0; )
    {
	// If we can't add a node, and we're the leaf node, we fail.
	if (!addNode(outgraph, net.nodes[i], type) && i == net.nodes.size()-1)
	    return false;
    }

    // Set wires
    bool        err = false;
    for (int i = 0, n = net.relationships.size(); i < n; ++i)
    {
	const HdMaterialRelationship	&r = net.relationships[i];
        if (!outgraph.wireNodes(BRAY_HdUtil::toStr(r.inputId),
                BRAY_HdUtil::toStr(r.inputName),
                BRAY_HdUtil::toStr(r.outputId),
                BRAY_HdUtil::toStr(r.outputName)))
        {
            err = true;
        }
    }
    if (err)
    {
        const HdMaterialNode    &node = net.nodes[num-1];
        UT_ErrorLog::error("Error wiring nodes for {} shader graph {}",
                type == BRAY_HdMaterial::SURFACE ? "surface" : "displacement",
                node.path);
    }
    return true;
}

PXR_NAMESPACE_CLOSE_SCOPE
