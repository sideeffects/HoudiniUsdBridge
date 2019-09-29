/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
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
void set##NAME##IsMapAsset (bool a) { my##NAME##Map.asset = a; }   \
bool is##NAME##MapAsset () const {return my##NAME##Map.asset;}     \
void set##NAME##Swizzle (TextureSwizzle s) { my##NAME##Map.swizzle = s; }   \
HUSD_HydraMaterial::TextureSwizzle get##NAME##Swizzle() const \
    {return my##NAME##Map.swizzle;}                               \
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
    
    static bool isAssetMap(const UT_StringRef &filename);
  
    struct map_info
    {
	map_info() : wrapS(true), wrapT(true), asset(false),
		     scale(1.0F, 1.0F, 1.0F, 1.0F),
		     bias(0.0F, 0.0F, 0.0F, 0.0F),
		     swizzle(TEXCOMP_RGB) {}
	UT_StringHolder name;
	UT_StringHolder uv;
	int		wrapS; // Maps to RE_TexClampType in RE_TextureTypes.h
	int		wrapT; // 0: rep 1: bord (black) 2: clamp 3: mirror
	UT_Vector4F	scale;
	UT_Vector4F	bias;
	TextureSwizzle  swizzle;
	bool		asset;
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
    fpreal myRoughness;
    bool myUseSpecularWorkflow;
    bool myUseGeometryColor;

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
