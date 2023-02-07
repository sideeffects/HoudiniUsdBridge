/*
 * Copyright 2019 Side Effects Software Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * transmitted, or disclosed in any way without writ
 * ten permission.
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

#include "HUSD_HydraMaterial.h"
#include "XUSD_HydraMaterial.h"
#include "HUSD_Path.h"

#include <SYS/SYS_AtomicInt.h>
#include <pxr/usd/sdf/path.h> 

UT_StringHolder HUSD_HydraMaterial::thediffuseColorToken("diffuseColor");
UT_StringHolder HUSD_HydraMaterial::thespecularColorToken("specularColor");
UT_StringHolder HUSD_HydraMaterial::theemissiveColorToken("emissiveColor");
UT_StringHolder HUSD_HydraMaterial::theocclusionToken("occlusion");
UT_StringHolder HUSD_HydraMaterial::theroughnessToken("roughness");
UT_StringHolder HUSD_HydraMaterial::themetallicToken("metallic");
UT_StringHolder HUSD_HydraMaterial::theopacityToken("opacity");
UT_StringHolder HUSD_HydraMaterial::theiorToken("ior");
UT_StringHolder HUSD_HydraMaterial::theclearcoatToken("clearcoat");
UT_StringHolder HUSD_HydraMaterial::theclearcoatRoughnessToken("clearcoatRoughness");
UT_StringHolder HUSD_HydraMaterial::thenormalToken("normal");
UT_StringHolder HUSD_HydraMaterial::thedisplacementToken("displacement");

UT_StringHolder HUSD_HydraMaterial::theDiffMapToken("DiffMap");
UT_StringHolder HUSD_HydraMaterial::theSpecMapToken("SpecMap");
UT_StringHolder HUSD_HydraMaterial::theEmitMapToken("EmitMap");
UT_StringHolder HUSD_HydraMaterial::theOcclusionMapToken("OcclusionMap");
UT_StringHolder HUSD_HydraMaterial::theRoughMapToken("RoughMap");
UT_StringHolder HUSD_HydraMaterial::theMetalMapToken("MetalMap");
UT_StringHolder HUSD_HydraMaterial::theOpacityMapToken("OpacityMap");
UT_StringHolder HUSD_HydraMaterial::theCoatIntMapToken("CoatIntMap");
UT_StringHolder HUSD_HydraMaterial::theCoatRoughMapToken("CoatRoughMap");
UT_StringHolder HUSD_HydraMaterial::theNormalMapToken("NormalMap");
UT_StringHolder HUSD_HydraMaterial::theDisplaceMapToken("DisplaceMap");

static SYS_AtomicInt64 theMatXVersion;

HUSD_HydraMaterial::HUSD_HydraMaterial(const PXR_NS::SdfPath &matId,
				       HUSD_Scene &scene)
    : HUSD_HydraPrim(scene, matId),
      myMatID(0),
      myMatVersion(0),
      myClearcoat(0.0),
      myClearcoatRoughness(0.01),
      myDiffuseColor(0.18,0.18,0.18),
      myDisplacement(0.0),
      myEmissiveColor(0,0,0),
      myIOR(1.5),
      myMetallic(0.0),
      myOcclusion(1.0),
      myOpacity(1.0),
      myOpacityThreshold(0.0),
      myRoughness(0.01),
      mySpecularColor(1,1,1), 
      myUseSpecularWorkflow(false),
      myUseGeometryColor(false),
      myMatXNeedsTangents(false),
      myIsValid(false),
      myIsMatX(false)
{
    myHydraMat = new PXR_NS::XUSD_HydraMaterial(matId, *this);
    bumpMatXNodeVersion();
}

void
HUSD_HydraMaterial::bumpMatXNodeVersion()
{
    myMatXNodeVersion = theMatXVersion.add(1);
}

void
HUSD_HydraMaterial::clearMaps()
{
    myUVs.clear();
    myDiffMap.name.clear();
    mySpecMap.name.clear();
    myEmitMap.name.clear();
    myDisplaceMap.name.clear();
    myMetalMap.name.clear();
    myRoughMap.name.clear();
    myCoatIntMap.name.clear();
    myCoatRoughMap.name.clear();
    myOpacityMap.name.clear();
    myOcclusionMap.name.clear();
}
  

void
HUSD_HydraMaterial::addShaderParm(const UT_StringHolder &mat_attrib,
                                  const UT_StringHolder &varname)
{
    myShaderParms[mat_attrib] = varname;
}
void
HUSD_HydraMaterial::clearShaderParms()
{
    myShaderParms.clear();
}

bool
HUSD_HydraMaterial::hasShaderParm(const UT_StringRef &mat_attrib_name,
                                  UT_StringHolder &varname) const
{
    auto entry = myShaderParms.find(mat_attrib_name); 
    if(entry != myShaderParms.end())
    {
        varname = entry->second;
        return true;
    }
    return false;
}

