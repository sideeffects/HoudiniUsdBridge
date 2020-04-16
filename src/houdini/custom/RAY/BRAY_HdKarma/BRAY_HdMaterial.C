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

#include <UT/UT_Debug.h>
#include <UT/UT_JSONWriter.h>
#include <HUSD/XUSD_Format.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/sdr/registry.h>
#include <pxr/usd/sdr/shaderNode.h>

#include <iostream>

PXR_NAMESPACE_OPEN_SCOPE

namespace
{
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
	w.jsonKeyValue("path", node.path.GetString());
	w.jsonKeyValue("identifier", node.identifier.GetText());
	w.jsonKeyToken("parameters");
	w.jsonBeginMap();
	for (auto &&p : node.parameters)
	{
	    w.jsonKeyToken(p.first.GetText());
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
	w.jsonKeyValue("inputId", r.inputId.GetString());
	w.jsonKeyValue("inputName", r.inputName.GetText());
	w.jsonKeyValue("outputId", r.outputId.GetString());
	w.jsonKeyValue("outputName", r.outputName.GetText());
	w.jsonEndMap();
    }

    // dump the contents of the shade graph hierarchy for debugging purposes
    static void
    updateShaders(bool for_surface,
	    BRAY::ScenePtr &scene,
	    BRAY::MaterialPtr &bmat,
	    const char *name,
	    const HdMaterialNetwork &net,
	    HdSceneDelegate &delegate)
    {
	if (net.nodes.size() == 0)
	    return;

	if (net.nodes.size() >= 1)
	{
	    // Test if there's a pre-built mantra shader
	    auto &&node = net.nodes[0];
            SdrRegistry &sdrreg = SdrRegistry::GetInstance();
            SdrShaderNodeConstPtr sdrnode =
                sdrreg.GetShaderNodeByIdentifier(node.identifier);
            
            if (sdrnode && sdrnode->GetSourceType() == TfToken("VEX"))
            {
                const std::string &code = sdrnode->GetSourceCode();

                if (code.length())
                {
                    UT_StringArray		args;
                    args.append(name);
                    for (auto &&p : node.parameters)
                        BRAY_HdUtil::appendVexArg(
                            args, p.first.GetText(), p.second);
                    if (for_surface)
                    {
                        bmat.updateSurfaceCode(scene, name, code);
                        bmat.updateSurface(scene, args);
                    }
                    else
                    {
                        bmat.updateDisplaceCode(scene, name, code);
                        if (bmat.updateDisplace(scene, args))
                            scene.forceRedice();
                    }
                    return;
                }

                const std::string &asset = sdrnode->GetSourceURI();
                if (asset.length())
                {
                    UT_StringArray	args;
                    args.append(asset);	// Shader name
                    for (auto &&p : node.parameters)
                    {
                        BRAY_HdUtil::appendVexArg(args,
                                UT_StringHolder(p.first.GetText()),
                                p.second);
                    }
                    if (for_surface)
                    {
                        bmat.updateSurface(scene, args);
                    }
                    else
                    {
                        if (bmat.updateDisplace(scene, args))
                            scene.forceRedice();
                    }
                    return;
                }
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
    BRAY::MaterialPtr	bmat = scene.createMaterial(id.GetText());
    if (isResourceDirty(*dirtyBits))
    {
	auto val = sceneDelegate->GetMaterialResource(id);
	HdMaterialNetworkMap netmap;
	netmap = val.Get<HdMaterialNetworkMap>();

	// Handle the surface shader
	HdMaterialNetwork net = netmap.map[HdMaterialTerminalTokens->surface];
	updateShaders(true, scene, bmat,
		id.GetString().c_str(), net, *sceneDelegate);

	// Handle the displacement shader
	net = netmap.map[HdMaterialTerminalTokens->displacement];
	updateShaders(false, scene, bmat,
		id.GetString().c_str(), net, *sceneDelegate);
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
	w.jsonKeyToken(p.GetText());
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
	w.jsonKeyToken(it.first.GetText());
	dump(w, it.second);
    }
    w.jsonEndMap();
}


PXR_NAMESPACE_CLOSE_SCOPE
