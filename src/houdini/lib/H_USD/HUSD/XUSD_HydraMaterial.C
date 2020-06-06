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
#include "XUSD_HydraUtils.h"
#include "XUSD_Tokens.h"

#include <gusd/UT_Gf.h>
#include <pxr/imaging/hd/material.h>
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/ar/packageUtils.h>

#include <UT/UT_Debug.h>
#include <UT/UT_Pair.h>
#include <UT/UT_StringArray.h>
#include "XUSD_Format.h"

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

void
XUSD_HydraMaterial::Reload()
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

#define MATCHES(NAME) (type == HusdHdMaterialTokens()-> NAME .GetText())

#define ASSIGN_MAT_INFO(NAME)                                           \
    auto texentry = in_out_map.find(mapnode);                           \
    if(texentry != in_out_map.end())                                    \
    {                                                                   \
        auto stentry = texentry->second.find("st");                     \
        if(stentry != texentry->second.end())                           \
        {                                                               \
            auto uventry=primvar_node.find(stentry->second.myFirst);	\
            if(uventry != primvar_node.end())                           \
                info.uv = uventry->second;                              \
        }                                                               \
        auto fileentry = texentry->second.find("file");                 \
        if(fileentry != texentry->second.end())                         \
        {                                                               \
            auto fentry=primvar_node.find(fileentry->second.myFirst);	\
            if(fentry != primvar_node.end())                            \
            {                                                           \
                info.name = fentry->second;                             \
                mat.addShaderParm(#NAME "Map", fentry->second);         \
            }                                                           \
        }                                                               \
    }                                                                   \
    mat.set##NAME##Map(info.name);                                      \
    mat.set##NAME##UVSet(info.uv);                                      \
    if(info.uv.isstring())                                              \
        myMaterial.addUVSet(info.uv);                                   \
    mat.set##NAME##Swizzle( getSwizzle( mapinput ) );                   \
    mat.set##NAME##WrapS(info.wrapS );                                  \
    mat.set##NAME##WrapT(info.wrapT );                                  \
    mat.set##NAME##Scale(info.scale );                                  \
    mat.set##NAME##Bias(info.bias );                                    \
    mat.set##NAME##IsMapAsset(info.asset )


#define CHECK_FOR_OVERRIDE2(hydra, shader)                              \
{                                                                       \
    auto var = primvar->second.find(HUSD_HydraMaterial::hydra##Token()); \
    if(var != primvar->second.end())                                    \
    {                                                                   \
	auto ovrvol=primvar_node.find(var->second.myFirst);             \
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

    // HdMaterialParamVector mparms = scene_del->GetMaterialParams(id);
    // TfTokenVector mprimvars = scene_del->GetMaterialPrimvars(id);
    // UTdebugPrint("Sync material", id, mparms.size(), mprimvars.size());
    
    VtValue mapval = scene_del->GetMaterialResource(id);
    if(mapval.IsHolding<HdMaterialNetworkMap>())
    {
	HdMaterialNetworkMap map = mapval.UncheckedGet<HdMaterialNetworkMap>();

	for(auto &it : map.map)
	{
	    UT_StringMap< UT_StringMap<
		UT_Pair<UT_StringHolder, UT_StringHolder> > > in_out_map;

	    for(auto &rt : it.second.relationships)
	    {
#ifdef DEBUG_MATERIAL
		UTdebugPrint("Rel: ", rt.inputId, rt.inputName,
		  	     rt.outputId, rt.outputName);
#endif
		in_out_map[rt.outputId.GetText()][rt.outputName.GetText()] =
		    UT_Pair<UT_StringHolder,UT_StringHolder>
		       (rt.inputId.GetText(),
			rt.inputName.GetText());
	    }

	    // [ vopnode ] [ vopinput] = file   TODO: = ( file, rgbamask }
	    UT_StringMap<HUSD_HydraMaterial::map_info> texmaps;
	    UT_StringArray materials;
	    UT_StringMap<UT_StringHolder> primvar_node;
	    
	    for(auto &nt : it.second.nodes)
	    {
		auto nodepath = nt.path;
#ifdef DEBUG_MATERIAL
		UTdebugPrint("Node: ", nodepath.GetText(),
		    nt.identifier.GetText());
		for (auto &&pt : nt.parameters)
		    UTdebugPrint("    Parm ",pt.first);
#endif
		
		if(nt.identifier == HusdHdMaterialTokens()->usdPreviewMaterial)
		{
		    syncPreviewMaterial(scene_del, nodepath, nt.parameters);
		    materials.append(nodepath.GetText());
		}
		else if(!strncmp(nt.identifier.GetText(),
			 HusdHdMaterialTokens()->usdPrimvarReader.GetText(),
				 16))
		{
		    auto var_it = nt.parameters.find(
			HusdHdMaterialTokens()->varname);

		    if (var_it != nt.parameters.end()  &&
			var_it->second.IsHolding<TfToken>())
			primvar_node[nodepath.GetText()] =
			    var_it->second.UncheckedGet<TfToken>().GetText();
		}
		else if(nt.identifier == HusdHdMaterialTokens()->usdUVTexture)
		{
		    syncUVTexture(texmaps[nodepath.GetText()],
				  scene_del, nodepath, nt.parameters);
		}
	    }

            myMaterial.setValid(materials.entries() > 0);

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
                            auto ovrvol=primvar_node.find(cvar->second.myFirst);
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
			auto &&mapnode = connect.myFirst;
			auto &&mapinput = connect.mySecond;
			auto info = texmaps[connect.myFirst];

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
			}
		    }
		}
	    }
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

bool
XUSD_HydraMaterial::isAssetMap(const UT_StringRef &filename)
{
    return ArIsPackageRelativePath(filename.toStdString());
}



void
XUSD_HydraMaterial::syncUVTexture(HUSD_HydraMaterial::map_info &info,
				  HdSceneDelegate *scene_del,
				  const SdfPath &nodepath,
				  const std::map<TfToken,VtValue> &parms)
{
    for(auto &pt : parms)
    {
	auto &&parm = pt.first;

	if(parm == HusdHdMaterialTokens()->file &&
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
		info.asset = ArIsPackageRelativePath(filename);
	    }
	}
	else if(parm == HusdHdMaterialTokens()->scale &&
	        pt.second.IsHolding<GfVec4f>())
	{
	    GfVec4f sc = pt.second.UncheckedGet<GfVec4f>();
	    info.scale = GusdUT_Gf::Cast(sc);
	}
	else if(parm == HusdHdMaterialTokens()->bias &&
	        pt.second.IsHolding<GfVec4f>())
	{
	    GfVec4f bias = pt.second.UncheckedGet<GfVec4f>();
	    info.bias = GusdUT_Gf::Cast(bias);
	}
	else if(parm == HusdHdMaterialTokens()->wrapS &&
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
	else if(parm == HusdHdMaterialTokens()->wrapT &&
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
	//else if(parm == HusdHdMaterialTokens()->fallback)
	//{
	//   Our mat repr. doesn't support this (yet?).
	//}
    }
}

void
XUSD_HydraMaterial::syncPreviewMaterial(HdSceneDelegate *scene_del,
					const SdfPath &nodepath,
					const std::map<TfToken,VtValue> &parms)
{
    int use_spec = 0;
    auto use_spec_it = parms.find(HusdHdMaterialTokens()->useSpecWorkflow);

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
		    
	if(parm == HusdHdMaterialTokens()->diffuseColor &&
	   pt.second.IsHolding<GfVec3f>())
	{
	    GfVec3f color = pt.second.UncheckedGet<GfVec3f>();
	    UT_Vector3F col(color[0], color[1], color[2]);
	    myMaterial.DiffuseColor(col);
	}
	else if(parm == HusdHdMaterialTokens()->emissiveColor &&
	        pt.second.IsHolding<GfVec3f>())
	{
	    GfVec3f color = pt.second.UncheckedGet<GfVec3f>();
	    UT_Vector3F col(color[0], color[1], color[2]);
	    myMaterial.EmissiveColor(col);
	}
	else if(parm == HusdHdMaterialTokens()->specularColor &&
	        pt.second.IsHolding<GfVec3f>())
	{
	    if(use_spec)
	    {
		GfVec3f color = pt.second.UncheckedGet<GfVec3f>();
		UT_Vector3F col(color[0], color[1], color[2]);
		myMaterial.SpecularColor(col);
	    }
	}
	else if(parm == HusdHdMaterialTokens()->metallic &&
	        pt.second.IsHolding<fpreal32>())
	{
	    if(!use_spec)
	    {
		fpreal32 metal = pt.second.UncheckedGet<fpreal32>();
		myMaterial.Metallic(metal);
	    }
	}
	else if(parm == HusdHdMaterialTokens()->clearcoat &&
	        pt.second.IsHolding<fpreal32>())
	{
	    fpreal32 cc = pt.second.UncheckedGet<fpreal32>();
	    myMaterial.Clearcoat(cc);
	}
	else if(parm == HusdHdMaterialTokens()->clearcoatRoughness &&
	        pt.second.IsHolding<fpreal32>())
	{
	    fpreal32 ccr = pt.second.UncheckedGet<fpreal32>();
	    myMaterial.ClearcoatRoughness(ccr);
	}
	else if(parm == HusdHdMaterialTokens()->displacement &&
	        pt.second.IsHolding<fpreal32>())
	{
	    fpreal32 d = pt.second.UncheckedGet<fpreal32>();
	    myMaterial.Displacement(d);
	}
	else if(parm == HusdHdMaterialTokens()->ior &&
	        pt.second.IsHolding<fpreal32>())
	{
	    fpreal32 ior = pt.second.UncheckedGet<fpreal32>();
	    myMaterial.IOR(ior);
	}
	else if(parm == HusdHdMaterialTokens()->occlusion &&
	        pt.second.IsHolding<fpreal32>())
	{
	    fpreal32 occ = pt.second.UncheckedGet<fpreal32>();
	    myMaterial.Occlusion(occ);
	}
	else if(parm == HusdHdMaterialTokens()->opacity &&
	        pt.second.IsHolding<fpreal32>())
	{
	    fpreal32 op = pt.second.UncheckedGet<fpreal32>();
	    myMaterial.Opacity(op);
	}
	else if(parm == HusdHdMaterialTokens()->roughness &&
	        pt.second.IsHolding<fpreal32>())
	{
	    fpreal32 r = pt.second.UncheckedGet<fpreal32>();
	    myMaterial.Roughness(r);
	}
    }

    myMaterial.setMaterialVersion(myMaterial.getMaterialVersion()+1);
}

PXR_NAMESPACE_CLOSE_SCOPE
