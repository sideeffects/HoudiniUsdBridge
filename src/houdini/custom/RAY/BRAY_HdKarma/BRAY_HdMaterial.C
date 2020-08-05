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
#include "BRAY_HdPreviewMaterial.h"
#include "BRAY_HdUtil.h"

#include <HUSD/XUSD_Format.h>
#include <UT/UT_Debug.h>
#include <UT/UT_DirUtil.h>
#include <UT/UT_ErrorLog.h>
#include <UT/UT_JSONWriter.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/sdr/registry.h>
#include <pxr/usd/sdr/shaderNode.h>

#include <iostream>

PXR_NAMESPACE_OPEN_SCOPE

namespace
{
    static const TfToken	theVEXToken("VEX", TfToken::Immortal);

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
	static const TfToken	theFallback("fallback", TfToken::Immortal);
	static const TfToken	theVarname("varname", TfToken::Immortal);
	const std::map<TfToken, VtValue> &parms = inputNode.parameters;

        // If the input node is a VEX shader, we need to create a new material
        // and preload it.
        SdrRegistry &sdrreg = SdrRegistry::GetInstance();
        SdrShaderNodeConstPtr sdrnode =
            sdrreg.GetShaderNodeByIdentifier(inputNode.identifier);
        if (sdrnode && sdrnode->GetSourceType() == theVEXToken)
        {
            static constexpr UT_StringLit       karmaImport("karma:import:");
            UT_StringHolder     name = BRAY_HdUtil::toStr(outputName);
            if (name.startsWith(karmaImport))
                name = UT_StringHolder(name.c_str()+karmaImport.length());
            else
                name = BRAY_HdUtil::toStr(inputNode.path);
            BRAY::MaterialPtr   bmat = scene.createMaterial(
                    BRAY_HdUtil::toStr(inputNode.path));
            return processVEX(for_surface, scene, bmat, name,
                        net, inputNode, delegate, true);
        }

	UT_StringHolder	primvar;
	auto vit = parms.find(theVarname);
	auto fit = parms.find(theFallback);

	if (vit == parms.end() || fit == parms.end())
	{
	    UT_ErrorLog::error("Invalid VEX material input {} {}",
                    inputNode.path, inputName);
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
        for (auto &&p : node.parameters)
            BRAY_HdUtil::appendVexArg(args, BRAY_HdUtil::toStr(p.first), p.second);
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

        if (!sdrnode || sdrnode->GetSourceType() != theVEXToken)
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
                if (bmat.updateDisplace(scene, args))
                    scene.forceRedice();
            }
            bmat.setInputs(inputMap, for_surface);
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
                if (bmat.updateDisplace(scene, args))
                    scene.forceRedice();
            }
        }
        bmat.setInputs(inputMap, for_surface);
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
            if (!for_surface)
            {
                // Remove displacement if it was enabled previously
                UT_StringArray emptyargs;
                if (bmat.updateDisplace(scene, emptyargs))
                    scene.forceRedice();
            }
	    return;
        }

	if (net.nodes.size() >= 1)
	{
	    // Test if there's a pre-built mantra shader
	    const HdMaterialNode &node = net.nodes[net.nodes.size()-1];

            if (processVEX(for_surface, scene, bmat, name,
                        net, node, delegate, false))
            {
                // Handled VEX input
                return;
            }
	}

	// There wasn't a pre-built VEX shader, so lets try to convert a
	// preview material.
	if (for_surface)
	{
	    BRAY::ShaderGraphPtr shadergraph = scene.createShaderGraph(name);
	    BRAY_HdPreviewMaterial::convert(shadergraph, net,
		BRAY_HdPreviewMaterial::SURFACE);
	    bmat.updateSurfaceGraph(scene, name, shadergraph);
	}
	else
	{
	    BRAY::ShaderGraphPtr shadergraph = scene.createShaderGraph(name);
	    BRAY_HdPreviewMaterial::convert(shadergraph, net,
		BRAY_HdPreviewMaterial::DISPLACE);
	    if (bmat.updateDisplaceGraph(scene, name, shadergraph))
		scene.forceRedice();
	}
    }
}


BRAY_HdMaterial::BRAY_HdMaterial(const SdfPath &id)
    : HdMaterial(id)
{
}

BRAY_HdMaterial::~BRAY_HdMaterial()
{
}

void
BRAY_HdMaterial::Reload()
{
    UTdebugFormat("material: reload()");
}

void
BRAY_HdMaterial::Sync(HdSceneDelegate *sceneDelegate,
		    HdRenderParam *renderParam,
		    HdDirtyBits *dirtyBits)
{
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
    if (isResourceDirty(*dirtyBits))
    {
	auto val = sceneDelegate->GetMaterialResource(id);
	HdMaterialNetworkMap netmap;
	netmap = val.Get<HdMaterialNetworkMap>();

	// Handle the surface shader
	HdMaterialNetwork net = netmap.map[HdMaterialTerminalTokens->surface];
	updateShaders(true, scene, bmat, name, net, *sceneDelegate);

	// Handle the displacement shader
	net = netmap.map[HdMaterialTerminalTokens->displacement];
	updateShaders(false, scene, bmat, name, net, *sceneDelegate);
	setShaders(sceneDelegate);
    }
    if (isParamsDirty(*dirtyBits))
    {
	setParameters(sceneDelegate);
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
    for (const auto &it : nmap.map)
    {
	w.jsonKeyToken(BRAY_HdUtil::toStr(it.first));
	dump(w, it.second);
    }
    w.jsonEndMap();
}


PXR_NAMESPACE_CLOSE_SCOPE
