//
// Copyright 2017 Pixar
//
// Licensed under the Apache License, Version 2.0 (the "Apache License")
// with the following modification; you may not use this file except in
// compliance with the Apache License and the following modification to it:
// Section 6. Trademarks. is deleted and replaced with:
//
// 6. Trademarks. This License does not grant permission to use the trade
//    names, trademarks, service marks, or product names of the Licensor
//    and its affiliates, except as required to comply with Section 4(c) of
//    the License and to reproduce the content of the NOTICE file.
//
// You may obtain a copy of the Apache License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the Apache License with the above modification is
// distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied. See the Apache License for the specific
// language governing permissions and limitations under the Apache License.
//
/**
 * \file houdinipkg/gusd/plugin.cpp
 * \brief main plugin file
 */

#include "gusd.h"

#include "GEO_IOTranslator.h"
#include "GT_PackedUSD.h"
#include "GT_PointInstancer.h"
#include "GT_Utils.h"
#include "GU_PackedUSD.h"
#include "NURBSCurvesWrapper.h"
#include "USD_Traverse.h"
#include "coneWrapper.h"
#include "cubeWrapper.h"
#include "curvesWrapper.h"
#include "cylinderWrapper.h"
#include "gsplatWrapper.h"
#include "instancerWrapper.h"
#include "meshWrapper.h"
#include "nurbsPatchWrapper.h"
#include "packedUsdWrapper.h"
#include "planeWrapper.h"
#include "pointsWrapper.h"
#include "scopeWrapper.h"
#include "sphereWrapper.h"
#include "tetMeshWrapper.h"
#include "xformWrapper.h"

#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdSkel/tokens.h>
#include <pxr/usd/usdVol/tokens.h>
#include <pxr/usd/kind/registry.h>
#include <pxr/base/plug/registry.h>

#include <GT/GT_PrimitiveTypes.h>
#include <OP/OP_OperatorTable.h>
#include <UT/UT_IOTable.h>
#include <UT/UT_PathSearch.h>

PXR_NAMESPACE_OPEN_SCOPE

static bool libInitialized = false;

void
GusdInit() 
{
    if( libInitialized )
        return;

    // Register plugins in the HOUDINI_USD_DSO_PATH. This defaults to the
    // usd_plugins subdirectory of every DSO path.
    //
    // We do this here instead of HUSDinitialize because the gusd library
    // is initialized by Houdini plugin loading before HUSDinitialize is
    // called by the LOP table creation code. We have to add these extra
    // plugin dirs before we add our GEOio plugin, because that plugin
    // accesses the SdfFileFormat registry, which uses the result of the
    // USD plugin registration, and becomes locked in, so additional plugins
    // found through RegisterPlugins do not show up in the SdfFileFormat
    // registry.
    const UT_PathSearch *usddsopath =
        UT_PathSearch::getInstance(UT_HOUDINI_USD_DSO_PATH);
    if (usddsopath)
    {
        std::vector<std::string> pluginpaths;

        for (int i = 0, n = usddsopath->getEntries(); i < n; i++)
            pluginpaths.push_back(usddsopath->getPathComponent(i));

        PlugRegistry::GetInstance().RegisterPlugins(pluginpaths);
    }

    // register GT_USD conversion functions keyed on GT type id
    GusdPrimWrapper::registerPrimDefinitionFuncForWrite(
            GT_PRIM_CURVE_MESH, 
            &GusdCurvesWrapper::defineForWrite);
    GusdPrimWrapper::registerPrimDefinitionFuncForWrite(
            GT_PRIM_POINT_MESH,
            &GusdPointsWrapper::defineForWrite);
    GusdPrimWrapper::registerPrimDefinitionFuncForWrite(
            GT_PRIM_PARTICLE,
            &GusdPointsWrapper::defineForWrite);
    GusdPrimWrapper::registerPrimDefinitionFuncForWrite(
            GT_PRIM_POLYGON_MESH,
            &GusdMeshWrapper::defineForWrite);
    GusdPrimWrapper::registerPrimDefinitionFuncForWrite(
            GT_PRIM_SUBDIVISION_MESH,
            &GusdMeshWrapper::defineForWrite);
    GusdPrimWrapper::registerPrimDefinitionFuncForWrite(
            GT_GEO_PACKED,
            &GusdXformWrapper::defineForWrite);
    GusdPrimWrapper::registerPrimDefinitionFuncForWrite(
            GusdGT_PackedUSD::getStaticPrimitiveType(),
            &GusdPackedUsdWrapper::defineForWrite);
    GusdPrimWrapper::registerPrimDefinitionFuncForWrite(
            GusdGT_PointInstancer::getStaticPrimitiveType(),
            &GusdInstancerWrapper::defineForWrite);


    GusdPrimWrapper::registerPrimDefinitionFuncForRead(
            UsdGeomTokens->Mesh, &GusdMeshWrapper::defineForRead);
    GusdPrimWrapper::registerPrimDefinitionFuncForRead(
            UsdGeomTokens->TetMesh, &GusdTetMeshWrapper::defineForRead);
    GusdPrimWrapper::registerPrimDefinitionFuncForRead(
            UsdGeomTokens->Points, &GusdPointsWrapper::defineForRead);
    GusdPrimWrapper::registerPrimDefinitionFuncForRead(
            UsdGeomTokens->BasisCurves, &GusdCurvesWrapper::defineForRead);
    GusdPrimWrapper::registerPrimDefinitionFuncForRead(
            UsdGeomTokens->NurbsCurves, &GusdNURBSCurvesWrapper::defineForRead);
    GusdPrimWrapper::registerPrimDefinitionFuncForRead(
            UsdGeomTokens->NurbsPatch, &GusdNurbsPatchWrapper::defineForRead);
    GusdPrimWrapper::registerPrimDefinitionFuncForRead(
            UsdGeomTokens->Scope, &GusdScopeWrapper::defineForRead);
    GusdPrimWrapper::registerPrimDefinitionFuncForRead(
            UsdGeomTokens->Xformable, &GusdXformWrapper::defineForRead);
    GusdPrimWrapper::registerPrimDefinitionFuncForRead(
            UsdSkelTokens->SkelRoot, &GusdXformWrapper::defineForRead);
    GusdPrimWrapper::registerPrimDefinitionFuncForRead(
            UsdGeomTokens->PointInstancer, &GusdInstancerWrapper::defineForRead);
    GusdPrimWrapper::registerPrimDefinitionFuncForRead(
            UsdGeomTokens->Plane, &GusdPlaneWrapper::defineForRead);
    GusdPrimWrapper::registerPrimDefinitionFuncForRead(
            UsdGeomTokens->Sphere, &GusdSphereWrapper::defineForRead);
    GusdPrimWrapper::registerPrimDefinitionFuncForRead(
            UsdGeomTokens->Cone, &GusdConeWrapper::defineForRead);
    GusdPrimWrapper::registerPrimDefinitionFuncForRead(
            UsdGeomTokens->Cube, &GusdCubeWrapper::defineForRead);
    GusdPrimWrapper::registerPrimDefinitionFuncForRead(
            UsdGeomTokens->Cylinder, &GusdCylinderWrapper::defineForRead);
    GusdPrimWrapper::registerPrimDefinitionFuncForRead(
            UsdVolTokens->ParticleField3DGaussianSplat,
            &GusdGSplatWrapper::defineForRead);

    GusdUSD_TraverseTable::GetInstance().SetDefault("std:components");
    libInitialized = true;
}

void 
GusdNewGeometryPrim( GA_PrimitiveFactory *f ) {

    GusdGU_PackedUSD::install(*f);
}

static bool geomIOInitialized = false;

void
GusdNewGeometryIO()
{
    if( geomIOInitialized )
        return;

    GU_Detail::registerIOTranslator(new GusdGEO_IOTranslator());

    UT_ExtensionList* geoextension;
    geoextension = UTgetGeoExtensions();
    if (!geoextension->findExtension("usd"))
       geoextension->addExtension("usd");
    if (!geoextension->findExtension("usda"))
       geoextension->addExtension("usda");
    if (!geoextension->findExtension("usdc"))
       geoextension->addExtension("usdc");
   geomIOInitialized = true;
}

static GusdPathComputeFunc gusdPathComputeFunc;

void 
GusdRegisterComputeRelativeSearchPathFunc( const GusdPathComputeFunc &func )
{
    gusdPathComputeFunc = func;
}

std::string 
GusdComputeRelativeSearchPath( const std::string &path ) 
{
    if( gusdPathComputeFunc ) {
        return gusdPathComputeFunc( path );
    }
    return path;
}

static TfToken gusdAssetKind = KindTokens->component;

void
GusdSetAssetKind( const TfToken &kind ) 
{
    gusdAssetKind = kind;
}

TfToken
GusdGetAssetKind()
{
    return gusdAssetKind;
}

static GusdUsdPrimFunc gusdUsdPrimFunc;

void 
GusdRegisterOperateOnUsdPrimFunc( const GusdUsdPrimFunc &func )
{
    gusdUsdPrimFunc = func;
}

bool
GusdOperateOnUsdPrim( const UsdPrim &prim  ) 
{
    if( gusdUsdPrimFunc ) {
        return gusdUsdPrimFunc( prim );
    }
    return false;
}


PXR_NAMESPACE_CLOSE_SCOPE
