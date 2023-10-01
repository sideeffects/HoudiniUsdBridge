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
#include <pxr/usdImaging/usdImaging/tokens.h>
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/sdr/registry.h>
#include <pxr/usd/sdr/shaderNode.h>
#include <pxr/usd/usdLux/tokens.h>

#include <UT/UT_Debug.h>

PXR_NAMESPACE_OPEN_SCOPE

namespace
{
    using ParmNameMap = BRAY_HdMaterialNetwork::ParmNameMap;

    /*
     * NOTE: Until the automatic process is set, whenever this list is updated, the HDA
     *       at $SHS/otl/Vop/kma_material_properties.hda also needs to be manually updated.
     */
    static bool
    allowedMaterialProperty(BRAY_ObjectProperty prop)
    {
        static UT_Set<BRAY_ObjectProperty>      theAllowed({
                                BRAY_OBJ_DIFFUSE_SAMPLES,
                                BRAY_OBJ_REFLECT_SAMPLES,
                                BRAY_OBJ_REFRACT_SAMPLES,
                                BRAY_OBJ_VOLUME_SAMPLES,
                                BRAY_OBJ_SSS_SAMPLES,
                                BRAY_OBJ_DIFFUSE_LIMIT,
                                BRAY_OBJ_REFLECT_LIMIT,
                                BRAY_OBJ_REFRACT_LIMIT,
                                BRAY_OBJ_VOLUME_LIMIT,
                                BRAY_OBJ_SSS_LIMIT,
                                BRAY_OBJ_DIFFUSE_QUALITY,
                                BRAY_OBJ_REFLECT_QUALITY,
                                BRAY_OBJ_REFRACT_QUALITY,
                                BRAY_OBJ_VOLUME_QUALITY,
                                BRAY_OBJ_SSS_QUALITY,
                                BRAY_OBJ_VOLUME_STEP_RATE,
                                BRAY_OBJ_VOLUME_UNIFORM,
                                BRAY_OBJ_VOLUME_UNIFORM_DENSITY,
                                BRAY_OBJ_VOLUME_UNIFORM_SAMPLES,
                                BRAY_OBJ_TREAT_AS_LIGHTSOURCE,
                                BRAY_OBJ_LIGHTSOURCE_SAMPLING_QUALITY,
                                BRAY_OBJ_LIGHTSOURCE_DIFFUSE_SCALE,
                                BRAY_OBJ_LIGHTSOURCE_SPECULAR_SCALE,
                                BRAY_OBJ_LPE_TAG,
                                BRAY_OBJ_DIELECTRIC_PRIORITY,
                                BRAY_OBJ_CAUSTICS_ENABLE,
                                BRAY_OBJ_CAUSTICS_ROUGHNESS_CLAMP,
                                BRAY_OBJ_FAKECAUSTICS_BSDF_ENABLE,
                                BRAY_OBJ_FAKECAUSTICS_COLOR,
                                BRAY_OBJ_FAKECAUSTICS_OPACITY,
                                BRAY_OBJ_MTLX_IMAGE_WIDTH,
                                BRAY_OBJ_MTLX_IMAGE_BLUR,
        });
        return theAllowed.contains(prop);
    }

    static bool
    setParmValue(BRAY::OptionSet &options, int prop, const VtValue &val)
    {
#define HANDLE_OPTSET_SCALAR(FTYPE) \
    if (val.IsHolding<FTYPE>()) { \
	return options.set(prop, &val.UncheckedGet<FTYPE>(), 1); \
    } \
    if (val.IsHolding<VtArray<FTYPE>>()) { \
        const auto &array = val.UncheckedGet<VtArray<FTYPE>>(); \
        return options.set(prop, array.data(), array.size()); \
    } \
    /* end macro */
#define HANDLE_OPTSET_VECTOR_T(TYPE, ETYPE, METHOD, SIZE) \
    if (val.IsHolding<TYPE>()) { \
	return options.set(prop, val.UncheckedGet<TYPE>().METHOD(), SIZE); \
    } \
    if (val.IsHolding<VtArray<TYPE>>()) { \
        const auto &array = val.UncheckedGet<VtArray<TYPE>>(); \
        return options.set(prop, (const ETYPE *)array.data(), SIZE*array.size()); \
    } \
    /* end macro */
#define HANDLE_OPTSET_VECTOR_F(TYPE, METHOD, SIZE) \
    HANDLE_OPTSET_VECTOR_T(TYPE##f, fpreal32, METHOD, SIZE); \
    HANDLE_OPTSET_VECTOR_T(TYPE##d, fpreal64, METHOD, SIZE); \
    /* end macro */
#define HANDLE_OPTSET_VECTOR(TYPE, METHOD, SIZE) \
    HANDLE_OPTSET_VECTOR_F(TYPE, METHOD, SIZE) \
    HANDLE_OPTSET_VECTOR_T(TYPE##i, int32, METHOD, SIZE) \
    /* end macro */
#define HANDLE_OPTSET_STRING(TYPE) \
    if (val.IsHolding<TYPE>()) { \
	return options.set(prop, BRAY_HdUtil::toStr(val.UncheckedGet<TYPE>())); \
    } \
    if (val.IsHolding<VtArray<TYPE>>()) { \
        const auto &array = val.UncheckedGet<VtArray<TYPE>>(); \
        UT_StackBuffer<UT_StringHolder> buf(array.size()); \
        for (int i = 0; i < array.size(); ++i) \
            buf[i] = BRAY_HdUtil::toStr(array[i]); \
	return options.set(prop, buf.array(), array.size()); \
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
        HANDLE_OPTSET_VECTOR_F(GfMatrix3, data, 9);
        HANDLE_OPTSET_VECTOR_F(GfMatrix4, data, 16);
        HANDLE_OPTSET_STRING(std::string);
        HANDLE_OPTSET_STRING(TfToken);
        HANDLE_OPTSET_STRING(SdfAssetPath);
        HANDLE_OPTSET_STRING(SdfPath);
#undef HANDLE_OPTSET_STRING
#undef HANDLE_OPTSET_VECTOR
#undef HANDLE_OPTSET_VECTOR_F
#undef HANDLE_OPTSET_VECTOR_T
#undef HANDLE_OPTSET_SCALAR
        if (val.IsHolding<UT_StringHolder>())
        {
            return options.set(prop, val.UncheckedGet<UT_StringHolder>());
        }
        if (val.IsHolding<VtArray<UT_StringHolder>>())
        {
            const auto &array = val.UncheckedGet<VtArray<UT_StringHolder>>();
            return options.set(prop, array.data(), array.size());
        }
        return false;
    }

    static void
    setNodeParams(const BRAY::ScenePtr &scene,
            BRAY::ShaderGraphPtr &outgraph,
	    BRAY_ShaderInstance *braynode,
	    const HdMaterialNode &usdnode,
            const ParmNameMap *parm_name_map,
	    BRAY_HdMaterial::ShaderType type)
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
	    if (idx == -1 && type == BRAY_HdMaterial::SURFACE)
            {
                static constexpr UT_StringView  karmaPref("karma:");
                UT_StringRef    pname = name;
                if (pname.startsWith(karmaPref))
                    pname = UT_StringRef(name.c_str() + karmaPref.length());
                BRAY_ObjectProperty     prop =  BRAYobjectProperty(pname);
                if (prop != BRAY_OBJ_INVALID_PROPERTY)
                {
                    if (!allowedMaterialProperty(prop))
                    {
                        UT_ErrorLog::errorOnce(
                            "Property {} cannot be applied at material level",
                            pname);
                    }
                    else
                    {
                        BRAY::OptionSet matops = outgraph.createObjectProperties(scene);
                        if (!setParmValue(matops, prop, p.second))
                        {
                            UT_ErrorLog::error("{} Error setting parameter {} to {}",
                                    usdnode.path, name, p.second);
                        }
                    }
                }
                else
                {
                    //UTdebugFormat("Missing parameter {}", p.first);
                }
            }
            else if (idx >= 0)
            {
                const VtValue &val = p.second;
                if (!setParmValue(optionset, idx, val))
                {
                    UT_ErrorLog::error("{} Error setting parameter {} to {}",
                            usdnode.path, name, val);

#if UT_ASSERT_LEVEL > 0
                    UTdebugFormat("{} Error setting {}", usdnode.path, p.first);
                    UTdebugFormat("{} ({})", usdnode.path, usdnode.identifier);
                    UTdebugFormat("parameters: [");
                    for (auto &&p : usdnode.parameters)
                        UTdebugFormat("  {} := {}", p.first, p.second);
                    UTdebugFormat("]");
#endif
                }
            }
	}
    };

    static UT_StringHolder
    brayNodeName(const TfToken &token, const UT_StringHolder &override)
    {
        if (override.isstring())
            return override;
#define USD_DECL_ALIAS(USD, KARMA) \
        { UsdLuxTokens->USD, UTmakeUnsafeRef(KARMA) } \
        /* end macro */
#define BRAY_DECL_ALIAS(USD, KARMA) \
        { BRAYHdTokens->USD, UTmakeUnsafeRef(KARMA) } \
        /* end macro */

        static UT_Map<TfToken, UT_StringHolder> aliasMap({
            USD_DECL_ALIAS(CylinderLight,       "USDcylinderLight"),
            USD_DECL_ALIAS(DiskLight,           "USDdiskLight"),
            USD_DECL_ALIAS(DistantLight,        "USDdistantLight"),
            USD_DECL_ALIAS(DomeLight,           "USDdomeLight"),
            USD_DECL_ALIAS(RectLight,           "USDrectLight"),
            USD_DECL_ALIAS(SphereLight,         "USDsphereLight"),

            BRAY_DECL_ALIAS(PxrDistantLight,    "USDdistantLight"),
            BRAY_DECL_ALIAS(PxrDomeLight,       "USDdomeLight"),
        });
        auto it = aliasMap.find(token);
        return it == aliasMap.end() ? BRAY_HdUtil::toStr(token) : it->second;
    }

    static bool
    hasDisplacement(const HdMaterialNetwork &net)
    {
        const HdMaterialNode    &node = net.nodes[net.nodes.size()-1];
        // If it's a shader other than the usd preview surface, we assume
        // there's displacement.
	if (node.identifier != BRAYHdTokens->UsdPreviewSurface)
            return true;

        // First, check if there's a wire to the displacement
        for (const auto &rel : net.relationships)
        {
            if (rel.outputId == node.path
                    && rel.outputName == HdMaterialTerminalTokens->displacement)
            {
                return true;
            }
        }

        VtValue amount;
        for (auto &&p : node.parameters)
        {
            if (p.first == HdMaterialTerminalTokens->displacement)
            {
                amount = p.second;
                break;
            }
        }
        if (amount.IsEmpty())
            return false;
        if (amount.IsHolding<float>())
            return amount.UncheckedGet<float>() != 0;
        if (amount.IsHolding<double>())
            return amount.UncheckedGet<double>() != 0;

        return true;
    }

    static BRAY_ShaderInstance *
    addNode(const BRAY::ScenePtr &scene,
            BRAY::ShaderGraphPtr &graph,
	    const HdMaterialNode &node,
	    BRAY_HdMaterial::ShaderType type,
            const ParmNameMap *parm_name_map,
            const UT_StringHolder &override_name)
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
	    braynode = graph.createNode(
                    brayNodeName(node.identifier, override_name),
                    BRAY_HdUtil::toStr(node.path));
	}
	if (braynode)
	    setNodeParams(scene, graph, braynode, node, parm_name_map, type);
	else
	{
            UTdebugFormat("Unhandled Node Type: {} {}", node.path, node.identifier);
            UT_ErrorLog::error("Unhandled node type {} {} in material",
                    node.path, node.identifier);
	    UT_ASSERT(0 && "Unhandled Node Type");
	}
	return braynode;
    };
}

namespace
{
    const UT_StringHolder &
    inputsPrefix()
    {
        static constexpr UT_StringLit theInputs("inputs:");
        return theInputs.asHolder();
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
BRAY_HdMaterialNetwork::convert(const BRAY::ScenePtr &scene,
        BRAY::ShaderGraphPtr &outgraph,
        const HdMaterialNetwork &net,
        BRAY_HdMaterial::ShaderType type,
        const ParmNameMap *parm_name_map)
{
    // The root node will be the last node in the array
    int num = net.nodes.size();
    if (!num)
	return false;

    // Add nodes backwards - Hydra puts the root node at the end of the list.

    // Do a quick check to see if there's actually displacement defined
    if (type == BRAY_HdMaterial::DISPLACE && !hasDisplacement(net))
        return false;

    // TODO: ignore irrelevant/unwired nodes (though Hydra may prune these already)
    UT_StringHolder     shader_name;
    for (int i = num; i-- > 0; )
    {
	// If we can't add a node, and we're the leaf node, we fail.
	if (!addNode(scene, outgraph, net.nodes[i], type, parm_name_map, shader_name)
                && i == net.nodes.size()-1)
        {
	    return false;
        }
        shader_name.clear();    // Only the root shader node is overridden
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
