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

#include "BRAY_HdMaterialNetwork.h"
#include "BRAY_HdMaterial.h"
#include "BRAY_HdUtil.h"
#include "BRAY_HdFormat.h"
#include "BRAY_HdTokens.h"

#include <UT/UT_ErrorLog.h>
#include <UT/UT_JSONWriter.h>
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/sdr/registry.h>
#include <pxr/usd/sdr/shaderNode.h>
#include <pxr/usd/usdLux/tokens.h>

#include <UT/UT_Debug.h>

PXR_NAMESPACE_OPEN_SCOPE

namespace
{
    using ParmNameMap = BRAY_HdMaterialNetwork::ParmNameMap;

    static bool
    setParmValue(BRAY::OptionSet &options, int prop, const VtValue &val)
    {
#define HANDLE_OPTSET_SCALAR(FTYPE) \
    if (val.IsHolding<FTYPE>()) { \
	options.set(prop, &val.UncheckedGet<FTYPE>(), 1); \
	return true; \
    } \
    /* end macro */
#define HANDLE_OPTSET_VECTOR_T(TYPE, METHOD, SIZE) \
    if (val.IsHolding<TYPE>()) { \
	options.set(prop, val.UncheckedGet<TYPE>().METHOD(), SIZE); \
	return true; \
    }
    /* end macro */
#define HANDLE_OPTSET_VECTOR(TYPE, METHOD, SIZE) \
    HANDLE_OPTSET_VECTOR_T(TYPE##f, METHOD, SIZE); \
    HANDLE_OPTSET_VECTOR_T(TYPE##d, METHOD, SIZE); \
    /* end macro */
#define HANDLE_OPTSET_STRING(TYPE, METHOD) \
    if (val.IsHolding<TYPE>()) { \
	options.set(prop, val.UncheckedGet<TYPE>()METHOD); \
	return true; \
    } \
    /* end macro */
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
            BRAY_HdUtil::appendVexArg(tmp, UT_StringHolder::theEmptyString, val);
            options.set(prop, tmp[1]);
            return true;
        }
        return false;
    }

    static void
    setNodeParams(BRAY::ShaderGraphPtr &outgraph,
	    BRAY_ShaderInstance *braynode,
	    const HdMaterialNode &usdnode,
            const ParmNameMap *parm_name_map)
    {
	BRAY::OptionSet optionset = outgraph.nodeParams(braynode);
	// HdMaterialNode.parameters is of type std::map< TfToken, VtValue >
	for (auto &&p : usdnode.parameters)
	{
            UT_StringHolder     name = BRAY_HdUtil::toStr(p.first);
	    int idx = optionset.find(name);
            if (idx == -1 && parm_name_map)
            {
                auto it = parm_name_map->find(p.first);
                if (it != parm_name_map->end())
                    idx = optionset.find(it->second);
            }
	    if (idx == -1)
            {
                //UTdebugFormat("Missing parameter {}", p.first);
		continue;
            }
	    const VtValue &val = p.second;
            if (!setParmValue(optionset, idx, val))
                UTdebugFormat("Error setting {}", p.first);
	}
    };

    static UT_StringHolder
    brayNodeName(const TfToken &token)
    {
#define DECL_ALIAS(USD, KARMA) \
        { UsdLuxTokens->USD, UTmakeUnsafeRef(KARMA) } \
        /* end macro */
        static UT_Map<TfToken, UT_StringHolder> aliasMap({
            DECL_ALIAS(cylinderLight,   "USDcylinderLight"),
            DECL_ALIAS(diskLight,       "USDdiskLight"),
            DECL_ALIAS(distantLight,    "USDdistantLight"),
            DECL_ALIAS(domeLight,       "USDdomeLight"),
            DECL_ALIAS(rectLight,       "USDrectLight"),
            DECL_ALIAS(sphereLight,     "USDsphereLight"),
        });
        auto it = aliasMap.find(token);
        return it == aliasMap.end() ? BRAY_HdUtil::toStr(token) : it->second;
    }

    static BRAY_ShaderInstance *
    addNode(BRAY::ShaderGraphPtr &graph,
	    const HdMaterialNode &node,
	    BRAY_HdMaterial::ShaderType type,
            const ParmNameMap *parm_name_map)
    {
	BRAY_ShaderInstance	*braynode = nullptr;
	if (node.identifier == BRAYHdTokens->UsdPreviewSurface)
	{
	    UT_WorkBuffer	name;
	    name.strcpy(BRAY_HdUtil::toStr(node.identifier));
            name.append('_');
            name.append(BRAY_HdMaterial::shaderType(type));
	    braynode = graph.createNode(name, BRAY_HdUtil::toStr(node.path));
	}
        else if (node.identifier == BRAYHdTokens->LightFilter)
        {
            // For LightFilter's we use the generic LightFilter container, but
            // use the `inputs:karma:info:id` to determine the root node type.
            UT_WorkBuffer       name;
            VtValue             karma_id;

            for (auto &&p : node.parameters)
            {
                if (p.first == BRAYHdTokens->karma_info_id)
                {
                    karma_id = p.second;
                    break;
                }
            }
            if (karma_id.IsEmpty())
                return nullptr;
            braynode = graph.createNode(BRAY_HdUtil::toStr(karma_id),
                                    BRAY_HdUtil::toStr(node.path));
        }
	else
	{
	    braynode = graph.createNode(brayNodeName(node.identifier),
				BRAY_HdUtil::toStr(node.path));
	}
	if (braynode)
	    setNodeParams(graph, braynode, node, parm_name_map);
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

namespace
{
    static const UT_StringView &
    inputsPrefix()
    {
        static UT_StringView    theInputs("inputs:");
        return theInputs;
    }

    TfToken
    stripInputs(const TfToken &token)
    {
        if (UT_StringWrap(token.GetText()).startsWith(inputsPrefix()))
            return TfToken(token.GetText()+inputsPrefix().length());
        return token;
    }
}

BRAY_HdMaterialNetwork::usdTokenAlias::usdTokenAlias(const TfToken &token)
    : myToken(token)
    , myBaseToken(stripInputs(token))
{
    // Since the tokens are all Immortal, we can have an unsafe reference
    myAlias = UTmakeUnsafeRef(myBaseToken.GetText());
}

BRAY_HdMaterialNetwork::usdTokenAlias::usdTokenAlias(const TfToken &token,
        const char *str)
    : myToken(token)
    , myBaseToken(stripInputs(token))
{
    myAlias = UTmakeUnsafeRef(str);
}

BRAY_HdMaterialNetwork::usdTokenAlias::usdTokenAlias(const char *str)
    : myToken(str, TfToken::Immortal)
    , myAlias(UTmakeUnsafeRef(str))
{
    myBaseToken = myToken;
}

bool
BRAY_HdMaterialNetwork::convert(BRAY::ShaderGraphPtr &outgraph,
        const HdMaterialNetwork &net,
        BRAY_HdMaterial::ShaderType type,
        const ParmNameMap *parm_name_map)
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
	if (!addNode(outgraph, net.nodes[i], type, parm_name_map)
                && i == net.nodes.size()-1)
        {
	    return false;
        }
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
                BRAY_HdMaterial::shaderType(type),
                node.path);
    }
    return true;
}

void
BRAY_HdMaterialNetwork::dump(const HdMaterialNetwork2 &mat)
{
    UT_AutoJSONWriter   w(std::cerr, false);
    dump(*w, mat);
}

namespace
{
    static void
    dumpConnection(UT_JSONWriter &w, const HdMaterialConnection2 &c)
    {
        w.jsonBeginMap();
        w.jsonKeyValue("upstreamNode",
                BRAY_HdUtil::toStr(c.upstreamNode));
        w.jsonKeyValue("upstreamOutputName",
                BRAY_HdUtil::toStr(c.upstreamOutputName));
        w.jsonEndMap();
    }

    static void
    dumpNode(UT_JSONWriter &w, const HdMaterialNode2 &node)
    {
        w.jsonBeginMap();
        w.jsonKeyValue("type", BRAY_HdUtil::toStr(node.nodeTypeId));
        w.jsonKeyToken("parameters");
        w.jsonBeginMap();
        for (auto &&item : node.parameters)
        {
            UT_OStringStream    os;
            os << item.second << std::ends;
            w.jsonKeyValue(item.first.GetText(), os.str());
        }
        w.jsonEndMap(); // parameters
        w.jsonKeyToken("inputs");
        w.jsonBeginMap();
        for (auto &&item : node.inputConnections)
        {
            w.jsonKeyToken(BRAY_HdUtil::toStr(item.first));
            w.jsonBeginArray();
            for (auto &&c : item.second)
                dumpConnection(w, c);
            w.jsonEndArray();
        }
        w.jsonEndMap(); // inputs
        w.jsonEndMap(); // node
    }
}

void
BRAY_HdMaterialNetwork::dump(UT_JSONWriter &w, const HdMaterialNetwork2 &mat)
{
    w.jsonBeginMap();

    w.jsonKeyToken("nodes");
    w.jsonBeginMap();
    for (auto &&item : mat.nodes)
    {
        w.jsonKeyToken(BRAY_HdUtil::toStr(item.first));
        dumpNode(w, item.second);
    }
    w.jsonEndMap();     // nodes

    w.jsonKeyToken("terminals");
    w.jsonBeginMap();
    for (auto &&item : mat.terminals)
    {
        w.jsonKeyToken(BRAY_HdUtil::toStr(item.first));
        dumpConnection(w, item.second);
    }
    w.jsonEndMap();     // terminals

    w.jsonKeyToken("primvars");
    w.jsonBeginArray();
    for (auto &&pv : mat.primvars)
        w.jsonValue(BRAY_HdUtil::toStr(pv));
    w.jsonEndArray();

    w.jsonEndMap();     // network
}

PXR_NAMESPACE_CLOSE_SCOPE
