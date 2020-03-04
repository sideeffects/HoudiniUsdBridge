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
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usdImaging/usdImaging/tokens.h>

#include <UT/UT_Debug.h>
#include <UT/UT_Lock.h>
#include <UT/UT_Set.h>
#include <UT/UT_VarScan.h>
#include <UT/UT_StringMap.h>
#include <UT/UT_StringSet.h>
#include <UT/UT_VarEncode.h>

PXR_NAMESPACE_OPEN_SCOPE

bool 
BRAY_HdPreviewMaterial::convert(BRAY::ShaderGraphPtr &outgraph,
				const HdMaterialNetwork &net,
				ShaderType type)
{
    auto setNodeParams = [&](BRAY_ShaderInstance *braynode, 
			     const HdMaterialNode &usdnode)
    {
	BRAY::OptionSet optionset = outgraph.nodeParams(braynode);
	// HdMaterialNode.parameters is of type std::map< TfToken, VtValue >
	for (auto &&p : usdnode.parameters)
	{
	    int idx = optionset.find(p.first.GetText());
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

    // Find root preview material node and add it first
    auto &&mtokens = HusdHdMaterialTokens();
    int rootid = -1;
    for (int i = 0, n = net.nodes.size(); i < n; ++i)
    {
	const HdMaterialNode &node = net.nodes[i];
	if (node.identifier == mtokens->usdPreviewMaterial)
	{
	    UT_StringHolder shaderdeclname = node.identifier.GetText();
	    if (type == SURFACE)
		shaderdeclname += "_surface";
	    else if (type == DISPLACE)
		shaderdeclname += "_displace";

	    BRAY_ShaderInstance *root = outgraph.createNode(shaderdeclname,
		node.path.GetText());
	    if (!root)
	    {
		UTdebugFormat("Unhandled Node Type: {}", node.identifier);
		UT_ASSERT(0 && "Unhandled Node Type");
		return false;
	    }
	    setNodeParams(root, node);
	    rootid = i;
	    break;
	}
    }
    if (rootid == -1)
	return false;

    // Add rest of nodes
    // TODO: ignore irrelevant/unwired nodes
    for (int i = 0, n = net.nodes.size(); i < n; ++i)
    {
	if (i == rootid)
	    continue;

	const HdMaterialNode &node = net.nodes[i];
	BRAY_ShaderInstance *braynode = outgraph.createNode(
	    node.identifier.GetText(), node.path.GetText());

	if (!braynode)
	{
	    UTdebugFormat("Unhandled Node Type: {}", node.identifier);
	    UT_ASSERT(0 && "Unhandled Node Type");
	    continue;
	}
	setNodeParams(braynode, node);
    }

    // Set wires
    for (int i = 0, n = net.relationships.size(); i < n; ++i)
    {
	const HdMaterialRelationship	&r = net.relationships[i];
	outgraph.wireNodes(r.inputId.GetText(), r.inputName.GetText(),
			   r.outputId.GetText(), r.outputName.GetText());
    }
    return true;
}

PXR_NAMESPACE_CLOSE_SCOPE
