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
 *	Side Effects Software Inc
 *	123 Front Street West, Suite 1401
 *	Toronto, Ontario
 *	Canada   M5J 2M2
 *	416-504-9876
 *
 * NAME:	XUSD_HydraMaterial.h (HUSD Library, C++)
 *
 * COMMENTS:	Evaluator and sprim for a material
 */

#include "XUSD_HydraMaterial.h"

#include "XUSD_Format.h"
#include "XUSD_HydraUtils.h"
#include "XUSD_Tokens.h"
#include "XUSD_ViewerDelegate.h"
#include "HUSD_Scene.h"
#include <gusd/UT_Gf.h>

#include <UT/UT_Debug.h>
#include <UT/UT_StringArray.h>

#include <pxr/imaging/hd/material.h>
#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/ar/packageUtils.h>

#include <utility>


static UT_StringHolder theShaderDiffuse("Cd");
static UT_StringHolder theHydraDisplayColor("displayColor");
static UT_StringHolder theShaderNormal("N");
static UT_StringHolder theShaderAlpha("Alpha");
static UT_StringHolder theSwizzleRGBA("rgba");
static UT_StringHolder theSwizzleRGB("rgb");
static UT_StringHolder theSwizzleR("r");
static UT_StringHolder theSwizzleG("g");
static UT_StringHolder theSwizzleB("b");
static UT_StringHolder theSwizzleA("a");

PXR_NAMESPACE_OPEN_SCOPE

XUSD_HydraMaterial::XUSD_HydraMaterial(SdfPath const& primId,
				       HUSD_HydraMaterial &mat)
    : HdMaterial(primId),
      myMaterial(mat)
{
}

HdDirtyBits
XUSD_HydraMaterial::GetInitialDirtyBitsMask() const
{
    return AllDirty;
}

HUSD_HydraMaterial::TextureSwizzle
getSwizzle(const UT_StringHolder &mask)
{
    if(mask == theSwizzleRGB)
	return HUSD_HydraMaterial::TEXCOMP_RGB;
    if(mask == theSwizzleRGBA)
	return HUSD_HydraMaterial::TEXCOMP_RGBA;
    if(mask == theSwizzleR)
	return HUSD_HydraMaterial::TEXCOMP_RED;
    if(mask == theSwizzleG)
	return HUSD_HydraMaterial::TEXCOMP_GREEN;
    if(mask == theSwizzleB)
	return HUSD_HydraMaterial::TEXCOMP_BLUE;
    if(mask == theSwizzleA)
	return HUSD_HydraMaterial::TEXCOMP_ALPHA;

    return HUSD_HydraMaterial::TEXCOMP_RGB;
}

#define MATCHES(NAME) (type == HusdHdMaterialTokens-> NAME .GetText())

void
XUSD_HydraMaterial::resolveTransform(
    const UT_StringRef                     &node,
    UT_StringMap<UT_StringHolder>          &primvar_node,
    UT_StringMap<UT_Matrix3F>              &transform_node,
    UT_StringMap<UT_StringMap<StringPair>> &in_out_map,
    HUSD_HydraMaterial::map_info           &info,
    UT_Matrix3F                            &xform)
{
    auto xformentry = transform_node.find(node);
    if(xformentry != transform_node.end())
    {
        // Connected to a UV Transform
        auto pentry = in_out_map.find(node);
        if(pentry != in_out_map.end())
        {
            auto stentry = pentry->second.find("in");
            if(stentry != pentry->second.end())
            {
                // Connected to a UV primvar reader
                auto uventry = primvar_node.find(stentry->second.first);
                if(uventry != primvar_node.end())
                {
                    // UV set
                    info.uv = uventry->second;
                }
                else
                {
                    // possibly another UV transform
                    resolveTransform(stentry->second.first,
                                     primvar_node, transform_node, in_out_map,
                                     info, xform);
                }
            }
        }
        xform *= xformentry->second;
    }
}

void
XUSD_HydraMaterial::resolveMap(
    const UT_StringRef                     &parmname,
    const UT_StringRef                     &mapnode,
    UT_StringMap<UT_StringHolder>          &primvar_node,
    UT_StringMap<UT_Matrix3F>              &transform_node,
    UT_StringMap<UT_StringMap<StringPair>> &in_out_map,
    HUSD_HydraMaterial                     &mat,
    HUSD_HydraMaterial::map_info           &info)
{
    //UTdebugPrint("Resolve ", parmname, mapnode);
    auto texentry = in_out_map.find(mapnode);
    if(texentry != in_out_map.end())
    {
        auto stentry = texentry->second.find("st");
        if(stentry != texentry->second.end())
        {
            auto uventry=primvar_node.find(stentry->second.first);
            if(uventry != primvar_node.end())
            {
                // Connected to a UV primvar reader
                info.uv = uventry->second;
                info.transform.identity();
            }
            else
            {
                UT_Matrix3F xform(1.0);
                resolveTransform(stentry->second.first,
                                 primvar_node, transform_node, in_out_map,
                                 info, xform);
                info.transform = xform;
            }
        }
        auto fileentry = texentry->second.find("file");
        if(fileentry != texentry->second.end())
        {
            auto fentry=primvar_node.find(fileentry->second.first);
            if(fentry != primvar_node.end())
            {
                info.name = fentry->second;
                info.name.substitute("\\","/");
                mat.addShaderParm(parmname, fentry->second);
            }
        }
    }
}

#define ASSIGN_MAT_INFO(NAME)                                           \
    resolveMap(#NAME "Map", mapnode, primvar_node, transform_node,      \
               in_out_map, mat, info);                                  \
    mat.set##NAME##Map(info.name);                                      \
    mat.set##NAME##UVSet(info.uv);                                      \
    mat.set##NAME##UVTransform(info.transform);                         \
    if(info.uv.isstring())                                              \
        myMaterial.addUVSet(info.uv);                                   \
    mat.set##NAME##Swizzle( getSwizzle( mapinput ) );                   \
    mat.set##NAME##WrapS(info.wrapS );                                  \
    mat.set##NAME##WrapT(info.wrapT );                                  \
    mat.set##NAME##Scale(info.scale );                                  \
    mat.set##NAME##Bias(info.bias );                                    \
    mat.set##NAME##Fallback(info.fallback)


#define CHECK_FOR_OVERRIDE2(hydra, shader)                              \
{                                                                       \
    auto var = primvar->second.find(HUSD_HydraMaterial::hydra##Token()); \
    if(var != primvar->second.end())                                    \
    {                                                                   \
	auto ovrvol=primvar_node.find(var->second.first);             \
	if(ovrvol != primvar_node.end())                                \
        {                                                               \
            if(ovrvol->second != HUSD_HydraMaterial::hydra##Token())    \
                mat.addAttribOverride(shader, ovrvol->second);          \
            mat.addShaderParm(HUSD_HydraMaterial::hydra##Token(),       \
                              ovrvol->second);                          \
        }                                                               \
    }                                                                   \
}

#define CHECK_FOR_OVERRIDE(NAME) \
    CHECK_FOR_OVERRIDE2(NAME, HUSD_HydraMaterial::NAME##Token())


#define UPDATE_WRAP(MAPNAME)    \
    if(myMaterial.get##MAPNAME##WrapS() == -1)  \
        myMaterial.set##MAPNAME##WrapS(wrap_s); \
    if(myMaterial.get##MAPNAME##WrapT() == -1)  \
        myMaterial.set##MAPNAME##WrapT(wrap_t)

// Uncomment to print out debug info for the preview material network
// #define DEBUG_MATERIAL

void
XUSD_HydraMaterial::Sync(HdSceneDelegate *scene_del,
			 HdRenderParam *rparms,
			 HdDirtyBits *dirty_bits)
{
    const SdfPath &id = GetId();
    UT_StringArray parms;

    auto srparm = static_cast<XUSD_ViewerRenderParam *>(rparms);
    //UTdebugPrint("Sync", id.GetText(), srparm->scene().isDeferredUpdate());
    if(srparm->scene().isDeferredUpdate())
    {
	myMaterial.setDeferredBits(*dirty_bits | myMaterial.deferredBits());
	*dirty_bits = (*dirty_bits & HdChangeTracker::Varying);
        return;
    }
    // HdMaterialParamVector mparms = scene_del->GetMaterialParams(id);
    // TfTokenVector mprimvars = scene_del->GetMaterialPrimvars(id);
    myMaterial.setNeedsTangents(false);
    
    VtValue mapval = scene_del->GetMaterialResource(id);
    if(mapval.IsHolding<HdMaterialNetworkMap>())
    {
	HdMaterialNetworkMap map = mapval.UncheckedGet<HdMaterialNetworkMap>();

	for(auto &it : map.map)
	{
	    UT_StringMap<UT_StringMap<StringPair>> in_out_map;
            enum
            {
                SURFACE_SHADER,
                DISPLACEMENT_SHADER
            } shader_type;
            
            if(it.first == HdMaterialTerminalTokens->surface)
                shader_type = SURFACE_SHADER;
            else if(it.first == HdMaterialTerminalTokens->displacement)
                shader_type = DISPLACEMENT_SHADER;
            else
                continue;
            
	    for(auto &rt : it.second.relationships)
	    {
#ifdef DEBUG_MATERIAL
		UTdebugPrint("Rel: ", rt.inputId, rt.inputName,
		  	     rt.outputId, rt.outputName);
#endif
		in_out_map[rt.outputId.GetText()][rt.outputName.GetText()] =
		    StringPair(rt.inputId.GetText(), rt.inputName.GetText());
	    }

	    // [ vopnode ] [ vopinput] = file   TODO: = ( file, rgbamask }
	    UT_StringMap<HUSD_HydraMaterial::map_info> texmaps;
	    UT_StringArray materials;
	    UT_StringMap<UT_StringHolder> primvar_node;
	    UT_StringMap<UT_Matrix3F> transform_node;
            UT_StringMap<GT_MaterialNodePtr> matx_node;
            UT_Options fallbacks;
            UT_StringHolder preview_path;
            bool has_preview = false;
            bool has_matx = false;
	    
	    for(auto &nt : it.second.nodes)
	    {
                HUSD_Path npath(nt.path);
                auto nodepath = npath.pathStr();
#ifdef DEBUG_MATERIAL
		UTdebugPrint("Node: ", nodepath,
		    nt.identifier.GetText());
		for (auto &&pt : nt.parameters)
		    UTdebugPrint("    Parm ",pt.first);
#endif
		
		if(nt.identifier == HusdHdMaterialTokens->UsdPreviewSurface)
		{
		    syncPreviewMaterial(scene_del, nt.parameters);
		    materials.append(nodepath);
                    preview_path = nodepath;
                    has_preview = true;
		}
		else if(!strncmp(nt.identifier.GetText(),
			 HusdHdMaterialTokens->UsdPrimvarReader.GetText(),
				 16))
		{
		    auto var_it = nt.parameters.find(
			HusdHdMaterialTokens->varname);

		    if (var_it != nt.parameters.end())
                    {
                        if(var_it->second.IsHolding<TfToken>())
                        {
                            primvar_node[nodepath] =
                               var_it->second.UncheckedGet<TfToken>().GetText();
                        }
                        else if(var_it->second.IsHolding<std::string>())
                        {
                            primvar_node[nodepath] =
                                var_it->second.UncheckedGet<std::string>();
                        }
                    }
                    
		    var_it=nt.parameters.find(HusdHdMaterialTokens->fallback);
		    if (var_it != nt.parameters.end())
                    {
                        XUSD_HydraUtils::addToOptions(fallbacks, var_it->second,
                                                      nodepath);
                    }
		}
		else if(nt.identifier == HusdHdMaterialTokens->UsdUVTexture)
		{
		    syncUVTexture(texmaps[nodepath],
				  scene_del, nt.parameters);
		}
		else if(nt.identifier == HusdHdMaterialTokens->UsdTransform2d)
		{
		    syncUVTransform(transform_node[nodepath],
                                    scene_del, nt.parameters);
		}
                else
                {
                    if(nt.identifier.GetString().rfind("ND_", 0) == 0)
                    {
                        if(!has_matx)
                            myMaterial.clearMaps();

                        UT_StringHolder type(nt.identifier);
                        GT_MaterialNodePtr mat = new GT_MaterialNode(type);
                        matx_node.emplace(nodepath, mat);
                        //UTdebugPrint("MatX: ", nodepath);
                        mat->setName(nodepath);
                        syncMatXNode(mat, scene_del, nt.parameters);
                        has_matx = true;
                    }
                }
	    }

            if(has_matx)
            {
                myMaterial.setValid(true);
                myMaterial.setIsMatX(true);

                UT_StringMap<bool> input_nodes;
            
                for(auto &mx_node : matx_node)
                {
                    auto &&mat_name = mx_node.first;
#ifdef DEBUG_MATERIAL
                    UTdebugPrint("material", mat_name);
#endif
                    auto && mat = myMaterial;
                    mat.clearOverrides();
                    
                    auto matx_entry = matx_node.find(mat_name);
                    if(matx_entry != matx_node.end())
                    {
                        GT_MaterialNode *node = matx_entry->second.get();
                        
                        auto entry = in_out_map.find(mat_name);
                        if(entry != in_out_map.end())
                        {
                            for(auto &input : entry->second)
                            {
                                auto &&input_name = input.first;
                                auto &&connect = input.second;
                                auto &&mapnode = connect.first;
                                auto &&output_name = connect.second;

                                auto ientry = matx_node.find(mapnode);
                                if(ientry != matx_node.end())
                                {
                                    node->addInput(input_name, output_name,
                                                   ientry->second);
                                    input_nodes[mapnode] = true;
                                    handleSpecialMatXNodes(ientry->second);
                                    //UTdebugPrint("  -Input", input_name,
                                    //            mapnode, ":", output_name);
                                }
                            }
                        }
                        //node->parms().dump();
                    }
                }

                bool found = false;
                for(auto &mx_node : matx_node)
                    if(input_nodes.find(mx_node.first) == input_nodes.end())
                    {
                        GT_MaterialNodePtr prev_node;
                        UT_Set<int> visited;
                        if(shader_type == SURFACE_SHADER)
                            prev_node = myMaterial.getMatXNode();
                        else
                            prev_node = myMaterial.getMatXDisplaceNode();

                        GT_MaterialNodePtr node = mx_node.second.get();
                        while(node &&
                              (node->type() == "ND_dot_surfaceshader" ||
                               node->type() == "ND_dot_displacementshader"))
                        {
                            node = node->getInput("in");
                        }
                        if(!node)
                            continue;
                        
                        if(prev_node &&
                           prev_node->networkMatch(node.get(), visited))
                        {
                            visited.clear();
                            prev_node->copyParms(node.get(), visited);
                        }
                        else
                        {
                            //UTdebugPrint("Node network differs");
                            myMaterial.bumpMatXNodeVersion();
                            if(shader_type == SURFACE_SHADER)
                                myMaterial.setMatXNode(node);
                            else
                                myMaterial.setMatXDisplaceNode(node);
                        }
                        found = true;
                        break;
                    }

                if(found)
                {
                    myMaterial.setMaterialVersion(
                        myMaterial.getMaterialVersion()+1);
                    has_preview = false;
                }
                else
                    has_matx = false;
            }

            // USD Preview shader support.
            // If matX falls through because there were some MatX nodes used in
            // a preview shader, we may get here even if has_matx was true.
            if(has_preview)
            {
                myMaterial.setValid(true);
                myMaterial.setIsMatX(false);

                auto base_node = in_out_map.find(preview_path);
                if(base_node != in_out_map.end())
                {
                    auto diff = base_node->second.find(
                        HUSD_HydraMaterial::diffuseColorToken());
                    if(diff != base_node->second.end())
                    {
                        auto &primvar_reader_name = diff->second.first;
                        UT_Vector3F col;
                        if(fallbacks.importOption(primvar_reader_name, col))
                            myMaterial.DiffuseColor(col);
                    }
                    auto opac = base_node->second.find(
                        HUSD_HydraMaterial::opacityToken());
                    if(opac != base_node->second.end())
                    {
                        auto &primvar_reader_name = opac->second.first;
                        fpreal alpha;
                        if(fallbacks.importOption(primvar_reader_name, alpha))
                            myMaterial.Opacity(alpha);
                    }
                }

                for(auto &mat_name : materials)
                {
#ifdef DEBUG_MATERIAL
                    UTdebugPrint("material", mat_name);
#endif
                    auto && mat = myMaterial;
                    mat.clearOverrides();
                    mat.clearMaps();

                    {
                        mat.UseGeometryColor(false);
                    
                        auto primvar = in_out_map.find(mat_name);
                        if(primvar != in_out_map.end())
                        {
                            CHECK_FOR_OVERRIDE2(normal, theShaderNormal);
                            CHECK_FOR_OVERRIDE2(opacity,theShaderAlpha);
                            CHECK_FOR_OVERRIDE(metallic);
                            CHECK_FOR_OVERRIDE(specularColor);
                            CHECK_FOR_OVERRIDE(emissiveColor);
                            CHECK_FOR_OVERRIDE(occlusion);
                            CHECK_FOR_OVERRIDE(roughness);
                            CHECK_FOR_OVERRIDE(ior);
                            CHECK_FOR_OVERRIDE(clearcoat);
                            CHECK_FOR_OVERRIDE(clearcoatRoughness);
                        
                            auto cvar = primvar->second.find(
                                HUSD_HydraMaterial::diffuseColorToken());
                            if(cvar != primvar->second.end())
                            {
                                auto ovrvol=primvar_node.find(cvar->second.first);
                                if(ovrvol != primvar_node.end())
                                {
                                    if(ovrvol->second != theHydraDisplayColor)
                                        mat.addAttribOverride(theShaderDiffuse,
                                                              ovrvol->second);
                                    mat.addShaderParm(
                                        HUSD_HydraMaterial::diffuseColorToken(),
                                        ovrvol->second);
                                    mat.UseGeometryColor(true);
                                }
                            }
                        }
                    }
		
                    auto entry = in_out_map.find(mat_name);
                    if(entry != in_out_map.end())
                    {
                        for(auto &input : entry->second)
                        {
                            auto &&type = input.first;
                            auto &&connect = input.second;
                            auto &&mapnode = connect.first;
                            auto &&mapinput = connect.second;
                            auto info = texmaps[connect.first];

                            if(MATCHES(diffuseColor))
                            {
                                ASSIGN_MAT_INFO(Diff);
                            }
                            else if(MATCHES(emissiveColor))
                            {
                                ASSIGN_MAT_INFO(Emit);
                            }
                            else if(MATCHES(specularColor))
                            {
                                ASSIGN_MAT_INFO(Spec);
                            }
                            else if(MATCHES(clearcoat))
                            {
                                ASSIGN_MAT_INFO(CoatInt);
                            }
                            else if(MATCHES(clearcoatRoughness))
                            {
                                ASSIGN_MAT_INFO(CoatRough);
                            }
                            else if(MATCHES(displacement))
                            {
                                ASSIGN_MAT_INFO(Displace);
                            }
                            else if(MATCHES(metallic))
                            {
                                ASSIGN_MAT_INFO(Metal);
                            }
                            else if(MATCHES(occlusion))
                            {
                                ASSIGN_MAT_INFO(Occlusion);
                            }
                            else if(MATCHES(opacity))
                            {
                                ASSIGN_MAT_INFO(Opacity);
                            }
                            else if(MATCHES(roughness))
                            {
                                ASSIGN_MAT_INFO(Rough);
                            }
                            else if(MATCHES(normal))
                            {
                                ASSIGN_MAT_INFO(Normal);
                                if(info.name.isstring())
                                    myMaterial.setNeedsTangents(true);
                            }
                        }
                    }
                }
	    }
            else if(!has_matx) // no known mat type
                myMaterial.setValid(false);
	}
    }

    // TEMP (hopefully): Update texture wrap with the diffuse texture wrap
    {
        int wrap_s = myMaterial.getDiffWrapS();
        int wrap_t = myMaterial.getDiffWrapT();

        if(wrap_s == -1)
            wrap_s = 1; // black
        if(wrap_t == -1)
            wrap_t = 1;

        UPDATE_WRAP(Spec);
        UPDATE_WRAP(Emit);
        UPDATE_WRAP(CoatInt);
        UPDATE_WRAP(CoatRough);
        UPDATE_WRAP(Displace);
        UPDATE_WRAP(Metal);
        UPDATE_WRAP(Occlusion);
        UPDATE_WRAP(Opacity);
        UPDATE_WRAP(Rough);
        UPDATE_WRAP(Normal);
    }
    
    *dirty_bits = Clean;
}

void
XUSD_HydraMaterial::syncUVTransform(UT_Matrix3F &xform,
                                    HdSceneDelegate *scene_del,
                                    const std::map<TfToken,VtValue> &parms)
{
    UT_Vector2F s(1,1), t(0,0);
    float       r = 0; 
    for(auto &pt : parms)
    {
	auto &&parm = pt.first;

        if(parm == HusdHdMaterialTokens->translation)
        {
            if(pt.second.IsHolding<GfVec2d>())
                t = GusdUT_Gf::Cast(pt.second.UncheckedGet<GfVec2d>());
            else if(pt.second.IsHolding<GfVec2f>())
                t = GusdUT_Gf::Cast(pt.second.UncheckedGet<GfVec2f>());
        }
        else if(parm == HusdHdMaterialTokens->scale)
        {
            if(pt.second.IsHolding<GfVec2d>())
                s = GusdUT_Gf::Cast(pt.second.UncheckedGet<GfVec2d>());
            else if(pt.second.IsHolding<GfVec2f>())
                s = GusdUT_Gf::Cast(pt.second.UncheckedGet<GfVec2f>());
        }
        else if(parm == HusdHdMaterialTokens->rotation)
        {
            if(pt.second.IsHolding<float>())
                r = pt.second.UncheckedGet<float>();
            else if(pt.second.IsHolding<double>())
                r = pt.second.UncheckedGet<double>();
        }
    }
    xform.identity();
    xform.scale(s.x(), s.y(), 1.0);
    xform.rotate(0, 0, SYSdegToRad(r), UT_XformOrder());
    xform.translate(t);
    //UTdebugPrint("Scale =", s, "rot = ", r, "trans = ", t);
}
    
    
void
XUSD_HydraMaterial::syncUVTexture(HUSD_HydraMaterial::map_info &info,
				  HdSceneDelegate *scene_del,
				  const std::map<TfToken,VtValue> &parms)
{
    for(auto &pt : parms)
    {
	auto &&parm = pt.first;

	if(parm == HusdHdMaterialTokens->file &&
	   pt.second.IsHolding<SdfAssetPath>())
	{
	    SdfAssetPath file = pt.second.UncheckedGet<SdfAssetPath>();
	    std::string filename, unres_filename;

            unres_filename = pt.second.UncheckedGet<std::string>();
            if(unres_filename.rfind("op:", 0) == 0 ||
               unres_filename.rfind("opdef:", 0) == 0)
            {
                // Let JEDI resolve the op/opdef
                filename = unres_filename;
            }
            else
            {
                filename = file.GetResolvedPath();
                if(filename.length() == 0)
                    filename = unres_filename;
            }

	    if(filename.length() > 0)
	    {
		info.name = filename;
                info.name.substitute("\\","/");
	    }
	}
	else if(parm == HusdHdMaterialTokens->scale &&
	        pt.second.IsHolding<GfVec4f>())
	{
	    GfVec4f sc = pt.second.UncheckedGet<GfVec4f>();
	    info.scale = GusdUT_Gf::Cast(sc);
	}
	else if(parm == HusdHdMaterialTokens->bias &&
	        pt.second.IsHolding<GfVec4f>())
	{
	    GfVec4f bias = pt.second.UncheckedGet<GfVec4f>();
	    info.bias = GusdUT_Gf::Cast(bias);
	}
	else if(parm == HusdHdMaterialTokens->fallback &&
	        pt.second.IsHolding<GfVec4f>())
	{
	    GfVec4f fallback = pt.second.UncheckedGet<GfVec4f>();
	    info.fallback = GusdUT_Gf::Cast(fallback);
	}
	else if(parm == HusdHdMaterialTokens->wrapS &&
	        pt.second.IsHolding<TfToken>())
	{
	    TfToken wrap = pt.second.UncheckedGet<TfToken>();
	    int mode = 0; // repeat
	    if(wrap == "black")
		mode = 1;
	    else if(wrap == "repeat")
		mode = 0;
	    else if(wrap == "clamp")
		mode = 2;
	    else if(wrap == "mirror")
		mode = 3;
	    info.wrapS = mode;
	}
	else if(parm == HusdHdMaterialTokens->wrapT &&
	        pt.second.IsHolding<TfToken>())
	{
	    TfToken wrap = pt.second.UncheckedGet<TfToken>();
	    int mode = 0; // repeat
	    if(wrap == "black")
		mode = 1;
	    else if(wrap == "repeat")
		mode = 0;
	    else if(wrap == "clamp")
		mode = 2;
	    else if(wrap == "mirror")
		mode = 3;
	    info.wrapT = mode;

	}
	//else if(parm == HusdHdMaterialTokens->fallback)
	//{
	//   Our mat repr. doesn't support this (yet?).
	//}
    }
}

void
XUSD_HydraMaterial::syncPreviewMaterial(HdSceneDelegate *scene_del,
					const std::map<TfToken,VtValue> &parms)
{
    int use_spec = 0;
    auto use_spec_it = parms.find(HusdHdMaterialTokens->useSpecularWorkflow);

    myMaterial.DiffuseColor({0.18f, 0.18f, 0.18f});
    myMaterial.EmissiveColor({0.0f, 0.0f, 0.0f});
    myMaterial.SpecularColor({0.0f, 0.0f, 0.0f});
    myMaterial.Occlusion(1.0);
    myMaterial.Opacity(1.0);
    myMaterial.OpacityThreshold(0.0);
    myMaterial.Roughness(0.5);
    myMaterial.Metallic(0.0);
    myMaterial.Clearcoat(0.0);
    myMaterial.ClearcoatRoughness(0.01);
    myMaterial.IOR(1.5);
	
    if (use_spec_it != parms.end() &&
	use_spec_it->second.IsHolding<int>())
	use_spec = use_spec_it->second.UncheckedGet<int>();

    myMaterial.UseSpecularWorkflow(use_spec);
    if(!use_spec)
    {
	UT_Vector3F col(1.0, 1.0, 1.0);
	myMaterial.SpecularColor(col);
    }
    else
	myMaterial.Metallic(0.0);

    for(auto &pt : parms)
    {
	auto &&parm = pt.first;

	if(parm == HusdHdMaterialTokens->diffuseColor &&
	   pt.second.IsHolding<GfVec3f>())
	{
	    GfVec3f color = pt.second.UncheckedGet<GfVec3f>();
	    UT_Vector3F col(color[0], color[1], color[2]);
	    myMaterial.DiffuseColor(col);
	}
	else if(parm == HusdHdMaterialTokens->emissiveColor &&
	        pt.second.IsHolding<GfVec3f>())
	{
	    GfVec3f color = pt.second.UncheckedGet<GfVec3f>();
	    UT_Vector3F col(color[0], color[1], color[2]);
	    myMaterial.EmissiveColor(col);
	}
	else if(parm == HusdHdMaterialTokens->specularColor &&
	        pt.second.IsHolding<GfVec3f>())
	{
	    if(use_spec)
	    {
		GfVec3f color = pt.second.UncheckedGet<GfVec3f>();
		UT_Vector3F col(color[0], color[1], color[2]);
		myMaterial.SpecularColor(col);
	    }
	}
	else if(parm == HusdHdMaterialTokens->metallic &&
	        pt.second.IsHolding<fpreal32>())
	{
	    if(!use_spec)
	    {
		fpreal32 metal = pt.second.UncheckedGet<fpreal32>();
		myMaterial.Metallic(metal);
	    }
	}
	else if(parm == HusdHdMaterialTokens->clearcoat &&
	        pt.second.IsHolding<fpreal32>())
	{
	    fpreal32 cc = pt.second.UncheckedGet<fpreal32>();
	    myMaterial.Clearcoat(cc);
	}
	else if(parm == HusdHdMaterialTokens->clearcoatRoughness &&
	        pt.second.IsHolding<fpreal32>())
	{
	    fpreal32 ccr = pt.second.UncheckedGet<fpreal32>();
	    myMaterial.ClearcoatRoughness(ccr);
	}
	else if(parm == HusdHdMaterialTokens->displacement &&
	        pt.second.IsHolding<fpreal32>())
	{
	    fpreal32 d = pt.second.UncheckedGet<fpreal32>();
	    myMaterial.Displacement(d);
	}
	else if(parm == HusdHdMaterialTokens->ior &&
	        pt.second.IsHolding<fpreal32>())
	{
	    fpreal32 ior = pt.second.UncheckedGet<fpreal32>();
	    myMaterial.IOR(ior);
	}
	else if(parm == HusdHdMaterialTokens->occlusion &&
	        pt.second.IsHolding<fpreal32>())
	{
	    fpreal32 occ = pt.second.UncheckedGet<fpreal32>();
	    myMaterial.Occlusion(occ);
	}
	else if(parm == HusdHdMaterialTokens->opacity &&
	        pt.second.IsHolding<fpreal32>())
	{
	    fpreal32 op = pt.second.UncheckedGet<fpreal32>();
	    myMaterial.Opacity(op);
	}
	else if(parm == HusdHdMaterialTokens->opacityThreshold &&
	        pt.second.IsHolding<fpreal32>())
	{
	    fpreal32 op = pt.second.UncheckedGet<fpreal32>();
	    myMaterial.OpacityThreshold(op);
	}
	else if(parm == HusdHdMaterialTokens->roughness &&
	        pt.second.IsHolding<fpreal32>())
	{
	    fpreal32 r = pt.second.UncheckedGet<fpreal32>();
	    myMaterial.Roughness(r);
	}
    }

    myMaterial.setMaterialVersion(myMaterial.getMaterialVersion()+1);
}

void
XUSD_HydraMaterial::syncMatXNode(const GT_MaterialNodePtr &mat,
                                 HdSceneDelegate *scene_del,
                                 const std::map<TfToken,VtValue> &parms)
{
    UT_Options &opts = mat->parms();
    
    for(auto &pt : parms)
    {
        auto &&parmvalue = pt.second;
        UT_StringRef parmname(pt.first.GetText());
        XUSD_HydraUtils::addToOptions(opts, parmvalue, parmname);
    }
}

void
XUSD_HydraMaterial::handleSpecialMatXNodes(const GT_MaterialNodePtr &node)
{
    auto &type = node->type();
    if(type.startsWith("ND_texcoord_"))
    {
        UT_WorkBuffer uv;
        uv.strcpy("uv");
        int index = 0;
        node->parms().importOption("index", index);
        if(index > 0)
            uv.appendSprintf("%d", index+1);

        myMaterial.addUVSet(std::move(uv));
    }
    else if(type.startsWith("ND_geompropvalue_"))
    {
        UT_StringHolder attrib;
        node->parms().importOption("geomprop", attrib);
        myMaterial.addUVSet(attrib);
    }
    else if(type.startsWith("ND_UsdPrimvarReader_"))
    {
        UT_StringHolder attrib;
        node->parms().importOption("varname", attrib);
        myMaterial.addUVSet(attrib);
    }
    else if(type.startsWith("ND_tangent") ||
            type.startsWith("ND_bitangent"))
    {
        myMaterial.setNeedsTangents(true);
    }

    if(node->parms().hasOption("tangent") ||
       node->parms().hasOption("bitangent"))
    {
        myMaterial.setNeedsTangents(true);
    }
       
    // Implicit UVs if there is no input to texcoord.
    if(node->parms().hasOption("texcoord") && !node->getInput("texcoord"))
        myMaterial.addUVSet("uv");

    UT_StringHolder space;
    if(node->parms().importOption("space", space))
    {
        if(space == "object")
            myMaterial.setNeedsObjectSpace(true);
    }
}

PXR_NAMESPACE_CLOSE_SCOPE
