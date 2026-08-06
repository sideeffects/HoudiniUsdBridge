/*
 * Copyright 2025 Side Effects Software Inc.
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
 */

#ifndef __HD_HdGpEngine__
#define __HD_HdGpEngine__

#include <UT/UT_Options.h>

#include <pxr/pxr.h>
#include <pxr/base/gf/quath.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/vt/array.h>
#include <pxr/imaging/hd/dataSource.h>
#include <pxr/imaging/hd/sceneIndex.h>

#include <string>

PXR_NAMESPACE_OPEN_SCOPE

class HD_HdGpScatterEngine
{
public:
    struct Result
    {
        VtVec3fArray translations;
        VtVec3fArray scales;
        VtQuathArray rotations;
        VtIntArray protoindexes;

        // Optional per-instance primvars beyond the canonical four
        // above. The procedural composes this as a sibling of the
        // retained-four overlay via HdOverlayContainerDataSource.
        // Engines that don't populate it leave it null.
        HdContainerDataSourceHandle extraPrimvars;
    };
    
    virtual ~HD_HdGpScatterEngine() = default;
    bool loadGraph(const std::string &path);
    bool setParms(const UT_Options &parms);
    bool setScatterSurface(const std::string &id, const HdSceneIndexPrim &prim,
        const UT_Array<GfMatrix4d> &xforms);
    bool setMaskSurface(const std::string &id, const HdSceneIndexPrim &prim,
        const UT_Array<GfMatrix4d> &xforms);
    bool setMaskPoints(const std::string &id, const HdSceneIndexPrim &prim,
        const UT_Array<GfMatrix4d> &xforms);
    bool setCamera(const std::string &id, const HdSceneIndexPrim &prim);
    Result cook();

protected:
    virtual bool loadGraphImpl(const std::string &path) = 0;
    virtual bool setParmsImpl(const UT_Options &parms) = 0;
    virtual bool setScatterSurfaceImpl(
        const std::string &id, const HdSceneIndexPrim &prim,
        const UT_Array<GfMatrix4d> &xforms) = 0;
    virtual bool setMaskSurfaceImpl(
        const std::string &id, const HdSceneIndexPrim &prim,
        const UT_Array<GfMatrix4d> &xforms) = 0;
    virtual bool setMaskPointsImpl(
        const std::string &id, const HdSceneIndexPrim &prim,
        const UT_Array<GfMatrix4d> &xforms) = 0;
    virtual bool setCameraImpl(
        const std::string &id, const HdSceneIndexPrim &prim) = 0;
    virtual Result cookImpl() = 0;

private:
    UT_Options myParms;
    std::string myGraphPath;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
