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

#include "BRAY_HdMaterial.h"
#include "BRAY_HdParam.h"
#include "BRAY_HdMaterialNetwork.h"
#include "BRAY_HdUtil.h"
#include "BRAY_HdFormat.h"
#include "BRAY_HdTokens.h"

#include <UT/UT_Debug.h>
#include <UT/UT_DirUtil.h>
#include <UT/UT_ErrorLog.h>
#include <UT/UT_JSONWriter.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/usdImaging/usdImaging/tokens.h>
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/sdr/registry.h>
#include <pxr/usd/sdr/shaderNode.h>

#include <iostream>

PXR_NAMESPACE_OPEN_SCOPE

namespace
{
    static constexpr UT_StringLit       theBinding(":binding");

    static bool processVEX(bool for_surface, BRAY::ScenePtr &scene,
            BRAY::MaterialPtr &bmat, const UT_StringHolder &name,
	    const HdMaterialNetwork &net, const HdMaterialNode &node,
            HdSceneDelegate &delegate,
            bool preload);


    static SdfPath
    getPathForUsd(HdSceneDelegate *del, const SdfPath &path)
    {
	const SdfPath	&delId = del->GetDelegateID();
	if (delId == SdfPath::AbsoluteRootPath())
	    return path;
	return path.ReplacePrefix(delId, SdfPath::AbsoluteRootPath());
    }

    // TODO: Log changes in change tracker - some kind of cache?
    static bool
    isParamsDirty(const HdDirtyBits &dirtyBits)
    {
	return (dirtyBits & HdMaterial::DirtyParams) != 0;
    }

    static bool
    isResourceDirty(const HdDirtyBits &dirtyBits)
    {
	return (dirtyBits & HdMaterial::DirtyResource) != 0;
    }

    static void
    dumpValue(UT_JSONWriter &w, const VtValue &value)
    {
	w.jsonBeginMap();
	w.jsonKeyValue("IsArrayValued", value.IsArrayValued());
	w.jsonKeyValue("GetArraySize", exint(value.GetArraySize()));
	w.jsonKeyValue("GetTypeName", value.GetTypeName());
	if (!value.IsArrayValued())
	{
	    UT_WorkBuffer	buf;
	    const char *vextype = BRAY_HdUtil::valueToVex(buf, value);
	    w.jsonKeyValue("vextype", vextype);
	    w.jsonKeyValue("valueAsString", buf);
	}
	w.jsonEndMap();
    }

    static void
    dumpNode(UT_JSONWriter &w, const HdMaterialNode &node)
    {
	w.jsonBeginMap();
	w.jsonKeyValue("path", BRAY_HdUtil::toStr(node.path));
	w.jsonKeyValue("identifier", BRAY_HdUtil::toStr(node.identifier));
	w.jsonKeyToken("parameters");
	w.jsonBeginMap();
	for (auto &&p : node.parameters)
	{
	    w.jsonKeyToken(BRAY_HdUtil::toStr(p.first));
	    dumpValue(w, p.second);
	}
	w.jsonEndMap();
	w.jsonEndMap();
    }

    static void
    dumpNode(const HdMaterialNode &node)
    {
	UT_AutoJSONWriter	w(std::cout, false);
	dumpNode(w, node);
	std::cout.flush();
    }

    static void
    dumpRelationship(UT_JSONWriter &w, const HdMaterialRelationship &r)
    {
	w.jsonBeginMap();
	w.jsonKeyValue("inputId", BRAY_HdUtil::toStr(r.inputId));
	w.jsonKeyValue("inputName", BRAY_HdUtil::toStr(r.inputName));
	w.jsonKeyValue("outputId", BRAY_HdUtil::toStr(r.outputId));
	w.jsonKeyValue("outputName", BRAY_HdUtil::toStr(r.outputName));
	w.jsonEndMap();
    }

    UT_StringHolder
    stringHolder(const VtValue &val)
    {
	if (val.IsHolding<std::string>())
	    return UT_StringHolder(val.UncheckedGet<std::string>());
	if (val.IsHolding<TfToken>())
	    return UT_StringHolder(val.UncheckedGet<TfToken>());
	return UT_StringHolder::theEmptyString;
    }

    static bool
    processInput(bool for_surface,
            const HdMaterialNode &inputNode,
	    const TfToken &inputName,
	    const TfToken &outputName,
	    UT_Array<BRAY::MaterialInput> &inputMap,
	    UT_StringArray &args,
            BRAY::ScenePtr &scene,
            const HdMaterialNetwork &net,
            HdSceneDelegate &delegate)

    {
	const std::map<TfToken, VtValue> &parms = inputNode.parameters;

        if (inputNode.identifier == UsdImagingTokens->UsdPreviewSurface)
            return false;

        // If the input node is a VEX shader, we need to create a new material
        // and preload it.
        SdrRegistry &sdrreg = SdrRegistry::GetInstance();
        SdrShaderNodeConstPtr sdrnode =
            sdrreg.GetShaderNodeByIdentifier(inputNode.identifier);
        if (sdrnode && sdrnode->GetSourceType() == BRAYHdTokens->VEX)
        {
            static constexpr UT_StringLit       karmaImport("karma:import:");
            static constexpr UT_StringLit       vexImport("vex:import:");
            UT_StringHolder     name = BRAY_HdUtil::toStr(outputName);
            if (name.startsWith(vexImport))
                name = UT_StringHolder(name.c_str()+vexImport.length());
            else if (name.startsWith(karmaImport))
                name = UT_StringHolder(name.c_str()+karmaImport.length());
            else
                name = BRAY_HdUtil::toStr(inputNode.path);
            BRAY::MaterialPtr   bmat = scene.createMaterial(
                    BRAY_HdUtil::toStr(inputNode.path));
            return processVEX(for_surface, scene, bmat, name,
                        net, inputNode, delegate, true);
        }

	UT_StringHolder	primvar;
	auto vit = parms.find(BRAYHdTokens->varname);
	auto fit = parms.find(BRAYHdTokens->fallback);

	if (vit == parms.end() || fit == parms.end())
	{
	    UT_ErrorLog::error("Invalid VEX material input {} {}",
                    inputNode.path, inputName);
            UT_IF_ASSERT(dumpNode(inputNode));
	    return false;
	}
	primvar = stringHolder(vit->second);
	if (!primvar)
	{
	    UT_ErrorLog::error("Expected string 'varname' parameter of {}",
		    inputNode.path);
	    return false;
	}
	return BRAY_HdUtil::addInput(primvar, fit->second, outputName,
		inputMap, args);
    }

    static bool
    gatherInputs(bool for_surface,
            const HdMaterialNetwork &net,
	    const HdMaterialNode &vexnode,
	    UT_Array<BRAY::MaterialInput> &inputMap,
	    UT_StringArray &args,
            BRAY::ScenePtr &scene,
            HdSceneDelegate &delegate)
    {
	// Throw into a map for faster lookup
	UT_Map<SdfPath, int>	nodemap;
	for (exint i = 0, n = net.nodes.size()-1; i < n; ++i)
	    nodemap.emplace(net.nodes[i].path, i);

	for (auto &&rel : net.relationships)
	{
	    if (rel.outputId == vexnode.path)
	    {
		auto it = nodemap.find(rel.inputId);
		if (it == nodemap.end())
		{
		    UT_ErrorLog::error("Invalid material input {}:{}",
			    rel.inputId, rel.inputName);
		}
		else
		{
		    processInput(for_surface,
                            net.nodes[it->second], rel.inputName,
			    rel.outputName, inputMap, args,
                            scene, net, delegate);
		}
	    }
	    else
	    {
		UT_ErrorLog::warning(
		    "Invalid binding input for VEX shaders: {}:{}->{}:{} {}",
			    rel.inputId, rel.inputName, rel.outputId, rel.outputName,
			    "not handled");
	    }
	}
	return true;
    }

    static void
    shaderParameters(bool for_surface,
            UT_StringArray &args,
            UT_Array<BRAY::MaterialInput> &inputMap,
	    const HdMaterialNetwork &net,
	    const HdMaterialNode &node,
            BRAY::ScenePtr &scene,
            HdSceneDelegate &delegate)
    {
        static constexpr UT_StringLit       karmaHDA("karma:hda:");     // Deprecated
        static constexpr UT_StringLit       vexHDA("vex:hda:");
        for (auto &&p : node.parameters)
        {
            UT_StringHolder     pname = BRAY_HdUtil::toStr(p.first);
            if (pname.startsWith(vexHDA) || pname.startsWith(karmaHDA))
            {
                // Special parameter that indicates we need an import from an HDA
                UT_ASSERT(p.second.IsHolding<SdfAssetPath>());
                UT_StringHolder hda = BRAY_HdUtil::toStr(p.second);
                if (!hda)
                {
                    UT_ErrorLog::error("Unable to resolve HDA path for: {}",
                            p.first);
                }
                else
                {
                    scene.loadHDA(hda);
                }
            }
            else
            {
                BRAY_HdUtil::appendVexArg(args, BRAY_HdUtil::toStr(p.first), p.second);
            }
        }
        if (net.nodes.size() > 1)
        {
            gatherInputs(for_surface, net, node, inputMap, args,
                    scene, delegate);
        }
    }

    static bool
    processVEX(bool for_surface,
            BRAY::ScenePtr &scene,
            BRAY::MaterialPtr &bmat,
            const UT_StringHolder &name,
	    const HdMaterialNetwork &net,
	    const HdMaterialNode &node,
            HdSceneDelegate &delegate,
            bool preload)
    {
        SdrRegistry &sdrreg = SdrRegistry::GetInstance();
        SdrShaderNodeConstPtr sdrnode =
            sdrreg.GetShaderNodeByIdentifier(node.identifier);

        if (!sdrnode || sdrnode->GetSourceType() != BRAYHdTokens->VEX)
            return false;

        const std::string &code = sdrnode->GetSourceCode();
        UT_Array<BRAY::MaterialInput>   inputMap;
        UT_StringArray                  args;

        if (code.length())
        {
            args.append(name);
            // Gather the parameters to the shader
            shaderParameters(for_surface, args, inputMap, net, node,
                    scene, delegate);

            if (for_surface)
            {
                bmat.updateSurfaceCode(scene, name, code, preload);
                bmat.updateSurface(scene, args);
            }
            else
            {
                bmat.updateDisplaceCode(scene, name, code, preload);
                bmat.updateDisplace(scene, args);
            }
            bmat.setInputs(scene, inputMap, for_surface);
        }
        else
        {
            // Try to get the resolved URI, but try the raw source URI
            // if it couldn't be resolved or the resolved path isn't a
            // valid regular file.
            std::string asset = sdrnode->GetResolvedImplementationURI();
            if (asset.empty() || !UTisValidRegularFile(asset.c_str()))
                asset = sdrnode->GetResolvedDefinitionURI();
            if (asset.empty())
            {
                UT_ErrorLog::error("Missing filename for VEX code {}",
                        node.path);
                // Although we have no file, we were still a VEX shader, so
                // return true.
                return true;
            }
            args.append(asset);	// Shader name
            shaderParameters(for_surface, args, inputMap, net, node,
                    scene, delegate);
            if (for_surface)
            {
                bmat.updateSurface(scene, args);
            }
            else
            {
                bmat.updateDisplace(scene, args);
            }
        }
        bmat.setInputs(scene, inputMap, for_surface);
        return true;
    }

    // dump the contents of the shade graph hierarchy for debugging purposes
    static void
    updateShaders(bool for_surface,
	    BRAY::ScenePtr &scene,
	    BRAY::MaterialPtr &bmat,
	    const UT_StringHolder &name,
	    const HdMaterialNetwork &net,
	    HdSceneDelegate &delegate)
    {
	if (net.nodes.size() == 0)
        {
            if (for_surface)
            {
                bmat.updateSurface(scene, UT_StringArray());
            }
            else
            {
                // Remove displacement if it was enabled previously
                bmat.updateDisplace(scene, UT_StringArray());
            }
	    return;
        }

        // Find the terminal node
        const HdMaterialNode &node = net.nodes[net.nodes.size()-1];

        if (processVEX(for_surface, scene, bmat,
                    name, net, node, delegate, false))
        {
            // Handled VEX input, so just return
            return;
	}

        BRAY::ShaderGraphPtr shadergraph = scene.createShaderGraph(name);

        // There wasn't a pre-built VEX shader, so lets try to convert the
        // shader network.
	if (for_surface)
	{
	    BRAY_HdMaterialNetwork::convert(scene, shadergraph, net,
		BRAY_HdMaterial::SURFACE);
	    bmat.updateSurfaceGraph(scene, name, shadergraph);
	}
	else
	{
	    BRAY_HdMaterialNetwork::convert(scene, shadergraph, net,
		BRAY_HdMaterial::DISPLACE);
	    bmat.updateDisplaceGraph(scene, name, shadergraph);
	}
    }

    static void
    dumpShaderNodes()
    {
        SdrRegistry &sdrreg = SdrRegistry::GetInstance();
        auto shaders = sdrreg.GetShaderNodesByFamily();
        UTdebugFormat("Shader Nodes");
        for (const auto &sh : shaders)
        {
            UT_WorkBuffer       msg;
            const TfToken       &src_type = sh->GetSourceType();
#if 0
            if (src_type != BRAYHdTokens->mtlx)
                continue;
#endif
            msg.format("{}:\n", sh->GetIdentifier());
            msg.appendFormat("      name = {}\n", sh->GetName());
            msg.appendFormat("    family = {}\n", sh->GetFamily());
            if (src_type != BRAYHdTokens->mtlx
                    && src_type != BRAYHdTokens->unknown_src_type)
            {
                // many mtlx nodes have bad data ptrs for the category
                // this is triggered with the mtlx or unknown source types
                msg.appendFormat("  category = {}\n", sh->GetCategory());
            }
            msg.appendFormat("   context = {}\n", sh->GetContext());
            msg.appendFormat("  src_type = {}\n", src_type);
            msg.appendFormat("    defURI = {}\n", sh->GetResolvedDefinitionURI());
            msg.appendFormat("    impURI = {}\n", sh->GetResolvedImplementationURI());
            UTdebugFormat("{}", msg);
        }
    }
}


BRAY_HdMaterial::BRAY_HdMaterial(const SdfPath &id)
    : HdMaterial(id)
{
#if 0
    static bool first = true;
    if (first)
    {
        dumpShaderNodes();
        first = false;
    }
#endif
}

BRAY_HdMaterial::~BRAY_HdMaterial()
{
}

void
BRAY_HdMaterial::Finalize(HdRenderParam *renderParam)
{
    BRAY_HdParam	*rparm = UTverify_cast<BRAY_HdParam *>(renderParam);
    BRAY::ScenePtr	&scene = rparm->getSceneForEdit();
    scene.destroyMaterial(BRAY_HdUtil::toStr(GetId()));
}

static UT_StringView
findShortSpaceName(const UT_StringView &full)
{
    auto        it = full.rfind(':');

    if (it == full.end())
        return UT_StringView();

    // CoordSys names now come through with an additional :binding token, so if
    // we find this, we need to back up a little further.
    if (it > full.begin() && theBinding.asRef() == it)
    {
        // We end with ":binding, so we need to back up one more colon
        it = full.rfind(':', it-1);
        if (it == full.end())
            return UT_StringView();
    }
    return UT_StringView(it+1, full.end());
}

void
BRAY_HdMaterial::Sync(HdSceneDelegate *sceneDelegate,
		    HdRenderParam *renderParam,
		    HdDirtyBits *dirtyBits)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    const SdfPath	&id = GetId();
    BRAY::ScenePtr	&scene =
	UTverify_cast<BRAY_HdParam *>(renderParam)->getSceneForEdit();
    //UTdebugFormat("material: sync() {}", id);
#if 0
    HdRenderIndex	&renderIndex = sceneDelegate->GetRenderIndex();
    const HdResourceRegistrySharedPtr	&resourceRegistry =
	    renderIndex.GetResourceRegistry();
#endif
    // Update the resource first, because this causes the material adapter
    // to execute its UpdateForTime code. Other dirty bits don't cause the
    // UpdateForTime. In other words the adapter code assumes that the
    // resource dirty bit will be addressed first.
    UT_StringHolder     name = BRAY_HdUtil::toStr(id);
    BRAY::MaterialPtr	bmat = scene.createMaterial(name);
    bool                do_update = false;
    if (isResourceDirty(*dirtyBits))
    {
	VtValue val = sceneDelegate->GetMaterialResource(id);
	HdMaterialNetworkMap netmap;
	netmap = val.Get<HdMaterialNetworkMap>();

        UT_UniquePtr<UT_Map<UT_StringHolder, UT_StringHolder>>      spaceMap;

        HdIdVectorSharedPtr path = sceneDelegate->GetCoordSysBindings(GetId());
        if (path)       // Path is a shared ptr to an SdfPathVector
        {
            for (auto &&p : *path)
            {
                UT_StringHolder full = BRAY_HdUtil::toStr(p);
                UT_StringView   alias = findShortSpaceName(full);
                if (alias)
                {
                    if (!spaceMap)
                    {
                        spaceMap = UTmakeUnique<UT_Map<UT_StringHolder,
                                            UT_StringHolder>>();
                    }
                    UT_StringHolder     aname(alias);
                    spaceMap->emplace(alias, full);
                    // Check if the alias ends with :binding, and if so, add
                    // another alias for the "short" name (without the
                    // binding).
                    if (aname.endsWith(theBinding))
                    {
                        aname = UT_StringHolder(aname.c_str(),
                                aname.length()-theBinding.length());
                        spaceMap->emplace(aname, full);
                    }
                    UT_ErrorLog::format(8,
                            "Material {}: CoordSys '{}' -> '{}'",
                            id, alias, full);
                }
            }
        }
        bmat.setCoordSysAliases(scene, std::move(spaceMap));

        //dump(netmap);

	// Handle the surface shader
	HdMaterialNetwork net = netmap.map[HdMaterialTerminalTokens->surface];
        // If there's no surface shader, check for volume (currently we don't
        // allow having surface and volume shader at the same time)
        if (!net.nodes.size())
            net = netmap.map[HdMaterialTerminalTokens->volume];
	updateShaders(true, scene, bmat, name, net, *sceneDelegate);

	// Handle the displacement shader
	net = netmap.map[HdMaterialTerminalTokens->displacement];
	updateShaders(false, scene, bmat, name, net, *sceneDelegate);
	setShaders(sceneDelegate);
        do_update = true;
    }
    if (isParamsDirty(*dirtyBits))
    {
	setParameters(sceneDelegate);
        do_update = true;
    }

    // handle update events:
    // BRAY_EVENT_NEW + BRAY_EVENT_DEL events automatically handled by the
    // scene under the hood, so we can ignore those
    // But is BRAY_EVENT_MATERIAL the correct update flag type in this case?
    if (do_update)
    {
        scene.updateMaterial(bmat, BRAY_EVENT_MATERIAL);
    }

    *dirtyBits &= ~HdChangeTracker::AllSceneDirtyBits;
}

HdDirtyBits
BRAY_HdMaterial::GetInitialDirtyBitsMask() const
{
    return AllDirty;
}

void
BRAY_HdMaterial::setShaders(HdSceneDelegate *delegate)
{
#if 0
    UTdebugFormat("Surf: {}\n'''{}'''",
	    GetId(),
	    delegate->GetSurfaceShaderSource(GetId()));
    // TODO: This generates USD errors.
    UT_StringRef surf(delegate->GetSurfaceShaderSource(GetId()));
    UT_StringRef disp(delegate->GetDisplacementShaderSource(GetId()));
    if (mySurfaceSource != surf)
    {
	mySurfaceSource = surf;
	// TODO: Notify users that surface shader is dirty
    }
    if (myDisplaceSource != disp)
    {
	myDisplaceSource = disp;
	// TODO: Notify users that displacement shader is dirty (redice)
    }
#endif
}

void
BRAY_HdMaterial::setParameters(HdSceneDelegate *delegate)
{
#if 0 && UT_ASSERT_LEVEL > 0
    auto &&parms = delegate->GetMaterialParams(GetId());
    UTdebugFormat("Update {} parameters:", parms.size());
    for (auto &&item : parms)
    {
	UTdebugFormat(" parm {}: {}, primvar: {}, fallback: {}, ",
		item.GetName(),
		item.IsTexture(),
		item.IsPrimvar(),
		item.IsFallback());
    }
    auto &&pvars = delegate->GetMaterialPrimvars(GetId());
    UTdebugFormat("Update {} primvars:", pvars.size());
    for (auto &&item : pvars)
	UTdebugFormat(" {}", item);
#endif
}

void
BRAY_HdMaterial::dump(const HdMaterialNetwork &net)
{
    UT_AutoJSONWriter	w(std::cout, false);
    dump(*w, net);
    std::cout.flush();
}

void
BRAY_HdMaterial::dump(const HdMaterialNetworkMap &netmap)
{
    UT_AutoJSONWriter	w(std::cout, false);
    dump(*w, netmap);
    std::cout.flush();
}

void
BRAY_HdMaterial::dump(UT_JSONWriter &w, const HdMaterialNetwork &net)
{
    w.jsonBeginMap();
    w.jsonKeyToken("primvars");
    w.jsonBeginArray();
    for (auto &&p : net.primvars)
	w.jsonKeyToken(BRAY_HdUtil::toStr(p));
    w.jsonEndArray();

    w.jsonKeyToken("nodes");
    w.jsonBeginArray();
    for (auto &&n : net.nodes)
	dumpNode(w, n);
    w.jsonEndArray();

    w.jsonKeyToken("relationships");
    w.jsonBeginArray();
    for (auto &&r : net.relationships)
	dumpRelationship(w, r);
    w.jsonEndArray();
    w.jsonEndMap();
}

void
BRAY_HdMaterial::dump(UT_JSONWriter &w, const HdMaterialNetworkMap &nmap)
{
    w.jsonBeginMap();
    w.jsonKeyToken("map");
    w.jsonBeginMap();
    for (const auto &it : nmap.map)
    {
	w.jsonKeyToken(BRAY_HdUtil::toStr(it.first));
	dump(w, it.second);
    }
    w.jsonEndMap();
    w.jsonKeyToken("terminals");
    w.jsonBeginArray();
    for (const auto &it : nmap.terminals)
        w.jsonString(BRAY_HdUtil::toStr(it));
    w.jsonEndArray();
    w.jsonEndMap();
}


PXR_NAMESPACE_CLOSE_SCOPE
