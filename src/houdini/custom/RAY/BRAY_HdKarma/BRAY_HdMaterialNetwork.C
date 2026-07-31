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

#include <PXL/PXL_OCIO.h>
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

    // Node-specific parm name map
    class NodeParmMap
    {
    public:
        NodeParmMap()
        {
            // For MaterialX 1.38.10 (and older) backwards compatibility:
            add("ND_atan2_float", {
                    {"in1", "iny"},
                    {"in2", "inx"}
                });
            add("ND_atan2_vector2", {
                    {"in1", "iny"},
                    {"in2", "inx"}
                });
            add("ND_atan2_vector3", {
                    {"in1", "iny"},
                    {"in2", "inx"}
                });
            add("ND_atan2_vector4", {
                    {"in1", "iny"},
                    {"in2", "inx"}
                });
        }

        // id: node identifier  parm: in/out
        void replace(const UT_StringHolder &id, UT_StringHolder &parm) const
        {
            auto it = myMap.find(id);
            if (it != myMap.end())
            {
                const ParmMap &map = it->second;
                auto itt = map.find(parm);
                if (itt != map.end())
                    parm = itt->second;
            }
        }

        // returns true if the node identifier has parameter remap
        bool hasMap(const UT_StringHolder &id) const
        {
            return myMap.contains(id);
        }

    private:
        void add(UT_StringHolder id, std::initializer_list<
            std::pair<UT_StringHolder, UT_StringHolder> > l)
        {
            ParmMap &map = myMap[id];
            for (auto &&parm : l)
                map[parm.first] = parm.second; // from -> to
        }

        using ParmMap = UT_Map<UT_StringHolder, UT_StringHolder>;
        UT_Map<UT_StringHolder, ParmMap> myMap;
    };
    static NodeParmMap theNodeParmMap;

    template <typename T> static void
    convertToSceneLinear(const T *src, T *dst, const UT_StringHolder &)
    {
        std::copy(src, src+3, dst);
    }

    template <> void
    convertToSceneLinear<fpreal32>(const fpreal32 *src, fpreal32 *dst,
            const UT_StringHolder &cspace)
    {
        static UT_StringHolder  scene_linear(PXL_OCIO::getSceneLinearRole());
        std::copy(src, src+3, dst);
        UT_VERIFY(PXL_OCIO::transform(cspace,
                    scene_linear, UT_StringHolder(), dst, 1, 3));
    }

    template <> void
    convertToSceneLinear<fpreal64>(const fpreal64 *src, fpreal64 *dst,
            const UT_StringHolder &cspace)
    {
        fpreal32        tmp[3];
        std::copy(src, src+3, tmp);
        convertToSceneLinear<fpreal32>(tmp, tmp, cspace);
        std::copy(tmp, tmp+3, dst);
    }

    static UT_StringHolder
    getColorSpace(const UT_StringRef &basename,
            const std::map<TfToken, VtValue> &usdparms)
    {
        UT_WorkBuffer   name;
        name.format("colorSpace:{}", basename);
        auto it = usdparms.find(TfToken(name.buffer()));
        if (it == usdparms.end())
            return UT_StringHolder();

        UT_StringHolder cspace = BRAY_HdUtil::toStr(it->second);
        auto cs = PXL_OCIO::lookupSpace(cspace);
        if (cs)
        {
            UT_StringHolder     alias = PXL_OCIO::getBestAlias(cs);
            if (alias)
                cspace = alias;
        }
        return cspace;
    }

    static UT_StringHolder
    getColorSpace(const BRAY::OptionSet &options, int prop,
            const std::map<TfToken, VtValue> &usdparms)
    {
        return getColorSpace(options.name(prop), usdparms);
    }

    static bool
    setParmValue(BRAY::OptionSet &options, int prop, const VtValue &val,
            const std::map<TfToken, VtValue> &usdparms)
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
        if (SIZE == 3) { \
            UT_StringHolder      cspace = getColorSpace(options, prop, usdparms); \
            if (cspace) { \
                ETYPE       clr[3]; \
                convertToSceneLinear(val.UncheckedGet<TYPE>().METHOD(), clr, cspace); \
                return options.set(prop, clr, SIZE); \
            } \
        } \
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
	// HdMaterialNode.parameters is of type std::map< TfToken, VtValue >
	BRAY::OptionSet optionset = outgraph.nodeParams(braynode);
        UT_StringHolder cspace;
        for (const auto &it : usdnode.parameters)
        {
            // Skip namespaced tokens
            if (strchr(it.first.data(), ':') != nullptr)
                continue;
            UT_WorkBuffer       typeName;
            typeName.format("typeName:{}", it.first);
            const auto &tnit = usdnode.parameters.find(TfToken(typeName.buffer()));
            // No type name info, or it's not an asset
            if (tnit == usdnode.parameters.end() || tnit->second != BRAYHdTokens->asset)
                continue;
            UT_StringHolder c = getColorSpace(it.first.data(), usdnode.parameters);
            if (c)
            {
                if (!cspace)
                    cspace = c;
                else if (c != cspace)
                {
                    UT_ErrorLog::warningOnce(
                            "Karma only supports a single color space on {}"
                            " (cannot support {} and {})",
                            usdnode.identifier, c, cspace);
                }
            }
        }

        if (cspace)
        {
            static constexpr UT_StringLit  srccolorspace("karma_srccolorspace");
            int                 idx = optionset.find(srccolorspace.asRef());
            if (idx < 0)
            {
                // In USD26.03, the sourceColorSpace parameter on the USD UV
                // Texture node was changed so that the color space is passed
                // as metadata (which is more consistent).  So, now, we take
                // that metadata and set the parameter on the internal BRAY
                // node.
                idx = optionset.find("sourceColorSpace");
            }
            if (idx >= 0)
                UT_VERIFY(optionset.set(idx, cspace));
#if UT_ASSERT_LEVEL > 0
            else
            {
                // If there's no karma_srccolorspace, parameter, then we aren't
                // handling a texture lookup properly.  It's also possible that
                // the asset referred to a non-texture file (like with pcread).
                // This check is only done in developer debug builds.
                UTdebugFormat("Unexpected color space specified for {}",
                        usdnode.identifier);
            }
#endif
        }
	for (const auto &p : usdnode.parameters)
	{
            UT_StringHolder     name = BRAY_HdUtil::toStr(p.first);
            theNodeParmMap.replace(BRAY_HdUtil::toStr(usdnode.identifier),
                name);
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
                    if (!BRAYisValidMaterialProperty(prop))
                    {
                        UT_ErrorLog::errorOnce(
                            "Property {} cannot be applied at material level",
                            pname);
                    }
                    else
                    {
                        BRAY::OptionSet matops = outgraph.createObjectProperties(scene);
                        if (!setParmValue(matops, prop, p.second, usdnode.parameters))
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
                if (!setParmValue(optionset, idx, val, usdnode.parameters))
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
            USD_DECL_ALIAS(PortalLight,         "USDdomeLight"),
            USD_DECL_ALIAS(RectLight,           "USDrectLight"),
            USD_DECL_ALIAS(SphereLight,         "USDsphereLight"),
            USD_DECL_ALIAS(MeshLight,           "USDmeshLight"),
            USD_DECL_ALIAS(VolumeLight,         "USDmeshLight"),

            BRAY_DECL_ALIAS(PxrDistantLight,    "USDdistantLight"),
            BRAY_DECL_ALIAS(PxrDomeLight,       "USDdomeLight"),

            // MaterialX 1.39.1 renames ND_normalmap -> ND_normalmap_float
            BRAY_DECL_ALIAS(ND_normalmap,       "ND_normalmap_float"),
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

    // sdf path -> node identifier mapping for nodes with parameter mapping
    UT_Map<UT_StringHolder, UT_StringHolder> pathtoid;

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

        if (theNodeParmMap.hasMap(BRAY_HdUtil::toStr(net.nodes[i].identifier)))
        {
            pathtoid[BRAY_HdUtil::toStr(net.nodes[i].path)] =
                BRAY_HdUtil::toStr(net.nodes[i].identifier);
        }

        shader_name.clear();    // Only the root shader node is overridden
    }

    // Set wires
    bool        err = false;
    for (int i = 0, n = net.relationships.size(); i < n; ++i)
    {
	const HdMaterialRelationship	&r = net.relationships[i];

        UT_StringHolder inputpath = BRAY_HdUtil::toStr(r.inputId);
        UT_StringHolder inputname = BRAY_HdUtil::toStr(r.inputName);
        {
            // parameter name mapping for input node
            auto it = pathtoid.find(inputpath);
            if (it != pathtoid.end())
                theNodeParmMap.replace(it->second, inputname);
        }

        UT_StringHolder outputpath = BRAY_HdUtil::toStr(r.outputId);
        UT_StringHolder outputname = BRAY_HdUtil::toStr(r.outputName);
        {
            // parameter name mapping for output node
            auto it = pathtoid.find(outputpath);
            if (it != pathtoid.end())
                theNodeParmMap.replace(it->second, outputname);
        }

        if (!outgraph.wireNodes(inputpath, inputname, outputpath, outputname))
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
