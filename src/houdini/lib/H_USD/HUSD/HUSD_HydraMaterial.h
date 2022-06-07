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
 * NAME:	HUSD_HydraMaterial.h (HUSD Library, C++)
 *
 * COMMENTS:	Container for a set of material parameters
 */
#ifndef HUSD_HydraMaterial_h
#define HUSD_HydraMaterial_h

#include <pxr/pxr.h>

#include "HUSD_API.h"
#include "HUSD_HydraPrim.h"
#include <GT/GT_MaterialNode.h>
#include <UT/UT_StringMap.h>
#include <UT/UT_Vector3.h>
#include <UT/UT_Vector4.h>

PXR_NAMESPACE_OPEN_SCOPE
class XUSD_HydraMaterial;
class SdfPath;
PXR_NAMESPACE_CLOSE_SCOPE

#define HUSD_MAP(NAME)	\
void set##NAME##Map (UT_StringHolder map) { my##NAME##Map.name = map; } \
const UT_StringHolder &get##NAME##Map () const { return my##NAME##Map.name; } \
void set##NAME##UVSet (UT_StringHolder map) { my##NAME##Map.uv = map; }	\
const UT_StringHolder &get##NAME##UVSet () const { return my##NAME##Map.uv; } \
void set##NAME##WrapS (int wrap) { my##NAME##Map.wrapS = wrap; }   \
int get##NAME##WrapS () const {return my##NAME##Map.wrapS;} \
void set##NAME##WrapT (int wrap) { my##NAME##Map.wrapT = wrap; }   \
int get##NAME##WrapT () const {return my##NAME##Map.wrapT;} \
void set##NAME##Scale (const UT_Vector4F &s) { my##NAME##Map.scale = s; }   \
UT_Vector4F get##NAME##Scale () const {return my##NAME##Map.scale;} \
void set##NAME##Bias (const UT_Vector4F &b) { my##NAME##Map.bias = b; }   \
UT_Vector4F get##NAME##Bias () const {return my##NAME##Map.bias;} \
void set##NAME##Swizzle (TextureSwizzle s) { my##NAME##Map.swizzle = s; }   \
HUSD_HydraMaterial::TextureSwizzle get##NAME##Swizzle() const \
    {return my##NAME##Map.swizzle;}                               \
void set##NAME##UVTransform(const UT_Matrix3F &t) { my##NAME##Map.transform=t; } \
UT_Matrix3F get##NAME##UVTransform() const { return my##NAME##Map.transform; } \
    static const UT_StringHolder &NAME##MapToken() { return the##NAME##MapToken; } \
    static UT_StringHolder the##NAME##MapToken

#define HUSD_TOKENNAME(NAME) \
public:                                                                 \
    static const UT_StringHolder &NAME##Token() { return the##NAME##Token; } \
private:                                                                \
    static UT_StringHolder the##NAME##Token

class HUSD_API HUSD_HydraMaterial : public HUSD_HydraPrim
{
public:
    HUSD_HydraMaterial(PXR_NS::SdfPath const &matId,
		       HUSD_Scene &scene);

    PXR_NS::XUSD_HydraMaterial *hydraMaterial() const { return myHydraMat; }
    
    // GL material id (RE_Material::getUniqueID())
    int	 getMaterialID() const { return myMatID; }
    void setMaterialID(int id) { myMatID = id; }

    int64 getMaterialVersion() const { return myMatVersion; }
    void setMaterialVersion(int64 v) { myMatVersion = v; }

    // 
    bool isValid() const      { return myIsValid; }
    void setValid(bool valid) { myIsValid = valid; }

    bool isMatX() const       { return myIsMatX; }
    void setIsMatX(bool mtx)  { myIsMatX = mtx; }

    void setMatXNode(const GT_MaterialNodePtr &node)
                              { myMatX = node; }
    void setMatXDisplaceNode(const GT_MaterialNodePtr &node)
                              { myMatXDisplace = node; }
    const GT_MaterialNodePtr &getMatXNode() const { return myMatX; }
    const GT_MaterialNodePtr &getMatXDisplaceNode() const
                              { return myMatXDisplace; }
    void  bumpMatXNodeVersion();
    int64 getMatXNodeVersion() const { return myMatXNodeVersion; }

    void setNeedsTangents(bool tan) { myMatXNeedsTangents = tan; }
    bool needsTangents() const { return myMatXNeedsTangents; }

    HUSD_PARM(DiffuseColor, UT_Vector3F);
    HUSD_PARM(EmissiveColor, UT_Vector3F);
    HUSD_PARM(SpecularColor, UT_Vector3F);
    
    HUSD_PARM(Clearcoat, fpreal);
    HUSD_PARM(ClearcoatRoughness, fpreal);
    HUSD_PARM(Displacement, fpreal);
    HUSD_PARM(Metallic, fpreal);
    HUSD_PARM(IOR, fpreal);
    HUSD_PARM(Occlusion, fpreal);
    HUSD_PARM(Opacity, fpreal);
    HUSD_PARM(OpacityThreshold, fpreal);
    HUSD_PARM(Roughness, fpreal);
    HUSD_PARM(UseSpecularWorkflow, bool);
    HUSD_PARM(UseGeometryColor, bool);

    // UV sets
    const UT_StringMap<int> &requiredUVs() const { return myUVs; }
    void  addUVSet(const UT_StringHolder &uvset) { myUVs[uvset] = 1; }

    // vertex attrib overrides
    const UT_StringMap<UT_StringHolder> &attribOverrides() const
			{ return myAttribOverrides; }
    void  addAttribOverride(const UT_StringHolder &attrib,
			    const UT_StringHolder &override)
			{ myAttribOverrides[attrib] = override; }
    void  clearOverrides() { myAttribOverrides.clear(); }

    void  addShaderParm(const UT_StringHolder &mat_attrib,
                        const UT_StringHolder &varname);
    void  clearShaderParms();
    bool  hasShaderParm(const UT_StringRef &mat_attrib_name,
                        UT_StringHolder &varname) const;
    const UT_StringMap<UT_StringHolder> &shaderParms() const
			{ return myShaderParms; }

    enum TextureSwizzle
    {
	TEXCOMP_LUM,
	TEXCOMP_RED,
	TEXCOMP_GREEN,
	TEXCOMP_BLUE,
	TEXCOMP_ALPHA,
	TEXCOMP_RGB,
	TEXCOMP_RGBA
    };

    HUSD_MAP(Diff);
    HUSD_MAP(Spec);
    HUSD_MAP(Emit);
    HUSD_MAP(CoatInt);
    HUSD_MAP(CoatRough);
    HUSD_MAP(Displace);
    HUSD_MAP(Metal);
    HUSD_MAP(Occlusion);
    HUSD_MAP(Opacity);
    HUSD_MAP(Rough);
    HUSD_MAP(Normal);

    void	clearMaps();
    
    struct map_info
    {
	map_info() : wrapS(-1), wrapT(-1),
                     transform(1.0f), uv("st"),
		     scale(1.0F, 1.0F, 1.0F, 1.0F),
		     bias(0.0F, 0.0F, 0.0F, 0.0F),
		     swizzle(TEXCOMP_RGB) {}
	UT_StringHolder name;
	UT_StringHolder uv;
        UT_Matrix3F     transform;
	int		wrapS; // Maps to RE_TexClampType in RE_TextureTypes.h
	int		wrapT; // 0: rep 1: bord (black) 2: clamp 3: mirror
	UT_Vector4F	scale;
	UT_Vector4F	bias;
	TextureSwizzle  swizzle;
    };

    HUSD_TOKENNAME(diffuseColor);
    HUSD_TOKENNAME(specularColor);
    HUSD_TOKENNAME(emissiveColor);
    HUSD_TOKENNAME(occlusion);
    HUSD_TOKENNAME(roughness);
    HUSD_TOKENNAME(metallic);
    HUSD_TOKENNAME(opacity);
    HUSD_TOKENNAME(ior);
    HUSD_TOKENNAME(clearcoat);
    HUSD_TOKENNAME(clearcoatRoughness);
    HUSD_TOKENNAME(normal);
    HUSD_TOKENNAME(displacement);

private:
    PXR_NS::XUSD_HydraMaterial  *myHydraMat;
    int myMatID;
    int64 myMatVersion;
    bool myIsValid;
    bool myIsMatX;
    GT_MaterialNodePtr myMatX, myMatXDisplace;
    int64 myMatXNodeVersion;

    // parms
    UT_Vector3F myEmissiveColor;
    UT_Vector3F myDiffuseColor;
    UT_Vector3F mySpecularColor;
    fpreal myMetallic;
    fpreal myClearcoat;
    fpreal myClearcoatRoughness;
    fpreal myDisplacement;
    fpreal myIOR;
    fpreal myOcclusion;
    fpreal myOpacity;
    fpreal myOpacityThreshold;
    fpreal myRoughness;
    bool myUseSpecularWorkflow;
    bool myUseGeometryColor;
    bool myMatXNeedsTangents;

    UT_StringMap<int> myUVs;
    UT_StringMap<UT_StringHolder> myAttribOverrides;
    UT_StringMap<UT_StringHolder> myShaderParms;
    map_info myDiffMap;
    map_info mySpecMap;
    map_info myEmitMap;
    map_info myDisplaceMap;
    map_info myMetalMap;
    map_info myRoughMap;
    map_info myCoatIntMap;
    map_info myCoatRoughMap;
    map_info myOpacityMap;
    map_info myOcclusionMap;
    map_info myNormalMap;
};

#undef HUSD_MAP

#endif
