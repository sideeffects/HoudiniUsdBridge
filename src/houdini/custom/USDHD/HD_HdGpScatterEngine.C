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

#include "HD_HdGpScatterEngine.h"

PXR_NAMESPACE_USING_DIRECTIVE

bool
HD_HdGpScatterEngine::loadGraph(const std::string &path)
{
    if (myGraphPath == path)
        return false;
    myGraphPath = path;
    myParms = {};
    loadGraphImpl(path);
    return true;
}

bool
HD_HdGpScatterEngine::setParms(const UT_Options &parms)
{
    if (myParms == parms)
        return false;
    myParms = parms;
    setParmsImpl(parms);
    return true;
}

bool
HD_HdGpScatterEngine::setScatterSurface(
        const std::string &id,
        const HdSceneIndexPrim &prim,
        const UT_Array<GfMatrix4d> &xforms)
{
    setScatterSurfaceImpl(id, prim, xforms);
    return true;
}

bool
HD_HdGpScatterEngine::setMaskSurface(
        const std::string &id,
        const HdSceneIndexPrim &prim,
        const UT_Array<GfMatrix4d> &xforms)
{
    setMaskSurfaceImpl(id, prim, xforms);
    return true;
}

bool
HD_HdGpScatterEngine::setMaskPoints(
        const std::string &id,
        const HdSceneIndexPrim &prim,
        const UT_Array<GfMatrix4d> &xforms)
{
    setMaskPointsImpl(id, prim, xforms);
    return true;
}

bool
HD_HdGpScatterEngine::setCamera(
        const std::string &id,
        const HdSceneIndexPrim &prim)
{
    setCameraImpl(id, prim);
    return true;
}

HD_HdGpScatterEngine::Result
HD_HdGpScatterEngine::cook()
{
    return cookImpl();
}
