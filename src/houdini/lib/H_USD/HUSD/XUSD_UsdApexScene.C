/*
 * Copyright 2026 Side Effects Software Inc.
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

#include "XUSD_UsdApexScene.h"

#include "HUSD_ErrorScope.h"
#include "UsdHoudini/houdiniApexCharacterAPI.h"
#include "UsdHoudini/houdiniApexCharacterBindingAPI.h"
#include "UsdHoudini/houdiniApexScene.h"
#include "UsdHoudini/houdiniApexShapeBindingAPI.h"
#include "UsdHoudini/houdiniApexShapeDeformAPI.h"
#include "UsdHoudini/houdiniApexXformBindingAPI.h"
#include "XUSD_FindPrimsTask.h"
#include "XUSD_Format.h"

#include <GA/GA_Names.h>
#include <GT/GT_RefineParms.h>
#include <GU/GU_Detail.h>
#include <UT/UT_Matrix4.h>
#include <UT/UT_Optional.h>
#include <UT/UT_ThreadSpecificValue.h>
#include <UT/UT_Tracing.h>
#include <UT/UT_WorkBuffer.h>
#include <gusd/GU_USD.h>
#include <gusd/UT_Gf.h>

#include <pxr/usd/usdGeom/basisCurves.h>
#include <pxr/usd/usdGeom/camera.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/pointBased.h>
#include <pxr/usd/usdGeom/points.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdGeom/xformable.h>
#include <pxr/usd/usdVol/particleField3DGaussianSplat.h>
#include <pxr/usd/usdVol/tokens.h>

PXR_NAMESPACE_OPEN_SCOPE

namespace
{
/// Collects the paths of HoudiniApexScene prims during a multithreaded
/// traversal.
class xusd_FindScenePrimsTaskData : public XUSD_FindPrimsTaskData
{
public:
    void addToThreadData(const UsdPrim &prim, bool * /*prune*/) override
    {
        if (prim.IsA<UsdHoudiniHoudiniApexScene>())
            myThreadPaths.get().push_back(prim.GetPath());
    }

    void getScenePaths(UT_SortedSet<SdfPath> &paths)
    {
        for (auto it = myThreadPaths.begin(); it != myThreadPaths.end(); ++it)
        {
            const SdfPathVector &thread_paths = it.get();
            paths.insert(thread_paths.begin(), thread_paths.end());
        }
    }

private:
    UT_ThreadSpecificValue<SdfPathVector> myThreadPaths;
};

void
xusdAddPropertyDep(
        const UsdProperty &property,
        XUSD_UsdApexSceneDependencies &dependencies)
{
    if (property)
        dependencies.addProperty(property.GetPath());
}

void
xusdAddPrimPropertyDeps(
        const UsdPrim &prim,
        XUSD_UsdApexSceneDependencies &dependencies)
{
    dependencies.addPrim(prim.GetPath());
    for (const UsdProperty &property : prim.GetProperties())
        xusdAddPropertyDep(property, dependencies);
}

UT_Optional<HUSD_ApexShapeType>
xusdGetShapeType(const UsdPrim &prim)
{
    if (UsdGeomMesh(prim))
        return HUSD_ApexShapeType::Mesh;
    if (UsdGeomPoints(prim))
        return HUSD_ApexShapeType::Points;
    if (UsdGeomBasisCurves(prim))
        return HUSD_ApexShapeType::BasisCurves;
    if (UsdVolParticleField3DGaussianSplat(prim))
        return HUSD_ApexShapeType::GSplats;
    if (UsdGeomCamera(prim))
        return HUSD_ApexShapeType::Camera;

    return UT_NULLOPT;
}

void
xusdRecordPrimvarInfo(
        const UsdAttribute &attribute,
        const TfToken &usd_name,
        const UT_StringHolder &attrib_name,
        TfToken interpolation,
        HUSD_ApexShapeType shape_type,
        UT_Array<XUSD_ApexShapePrimvarInfo> &primvars,
        XUSD_UsdApexSceneDependencies &dependencies)
{
    // We add a dependency even if the attribute is missing, so that we can
    // reload the APEX scene if the attribute is added later.
    xusdAddPropertyDep(attribute, dependencies);

    if (!attribute || !attribute.HasAuthoredValue())
        return;

    const SdfValueTypeName type_info = attribute.GetTypeName();

    const GA_AttributeOwner owner = XUSD_ApexShapePrimvarInfo::getPrimvarOwner(
            shape_type, interpolation);
    if (owner == GA_ATTRIB_INVALID)
        return;

    primvars.append({
            .myName = attrib_name,
            .myUsdName = usd_name,
            .myTypeInfo = type_info,
            .myOwner = owner,
            .myInterpolation = interpolation,
    });
}

void
xusdRecordShapePrimvarInfo(
        const UsdPrim &shape_prim,
        HUSD_ApexShapeType shape_type,
        UT_Array<XUSD_ApexShapePrimvarInfo> &primvars,
        XUSD_UsdApexSceneDependencies &dependencies)
{
    switch (shape_type)
    {
        case HUSD_ApexShapeType::Mesh:
        case HUSD_ApexShapeType::Points:
        case HUSD_ApexShapeType::BasisCurves:
        {
            const UsdGeomPointBased point_based(shape_prim);
            xusdRecordPrimvarInfo(
                    point_based.GetPointsAttr(), UsdGeomTokens->points,
                    GA_Names::P, UsdGeomTokens->vertex, shape_type, primvars,
                    dependencies);
            xusdRecordPrimvarInfo(
                    point_based.GetNormalsAttr(), UsdGeomTokens->normals,
                    GA_Names::N, point_based.GetNormalsInterpolation(),
                    shape_type, primvars, dependencies);
            break;
        }
        case HUSD_ApexShapeType::GSplats:
        {
            const UsdVolParticleField3DGaussianSplat splat(shape_prim);

            UsdAttribute positions_attr;
            splat.UsesFloatPositions(&positions_attr);
            xusdRecordPrimvarInfo(
                    positions_attr, UsdVolTokens->positions, GA_Names::P,
                    UsdGeomTokens->vertex, shape_type, primvars, dependencies);

            UsdAttribute orientations_attr;
            splat.UsesFloatOrientations(&orientations_attr);
            xusdRecordPrimvarInfo(
                    orientations_attr, UsdVolTokens->orientations,
                    GA_Names::orient, UsdGeomTokens->vertex, shape_type,
                    primvars, dependencies);
            break;
        }
        case HUSD_ApexShapeType::Camera:
            break;
    }
}

/// Extract and validate the prims which are bound to geometry shape outputs
/// from a rig.
UT_Array<XUSD_ApexShapeBinding>
xusdReadShapeBindings(
        const UsdStageRefPtr &stage,
        const UsdHoudiniHoudiniApexCharacterAPI &char_api,
        XUSD_ApexSceneValidator &validator,
        XUSD_UsdApexSceneDependencies &dependencies)
{
    UT_Array<XUSD_ApexShapeBinding> shape_bindings;

    for (const UsdHoudiniHoudiniApexShapeBindingAPI &binding_api :
         UsdHoudiniHoudiniApexShapeBindingAPI::GetAll(char_api.GetPrim()))
    {
        xusdAddPropertyDep(binding_api.GetBindingRel(), dependencies);
        xusdAddPropertyDep(binding_api.GetInputAttr(), dependencies);
        xusdAddPropertyDep(binding_api.GetOutputAttr(), dependencies);

        SdfPathVector shape_targets;
        binding_api.GetBindingRel().GetTargets(&shape_targets);
        if (shape_targets.size() != 1 || !shape_targets.front().IsPrimPath())
            continue;

        const SdfPath shape_prim_path = shape_targets.front().GetPrimPath();
        const UsdPrim shape_prim = stage->GetPrimAtPath(shape_prim_path);
        dependencies.addPrim(shape_prim_path);
        if (!shape_prim || !shape_prim.IsActive())
            continue;

        const UT_Optional<HUSD_ApexShapeType> shape_type
                = xusdGetShapeType(shape_prim);
        if (!shape_type)
        {
            UT_WorkBuffer msg;
            msg.format(
                    "Unsupported prim type '{0}' for <{1}>, skipping.",
                    shape_prim.GetTypeName(), shape_prim_path);
            HUSD_ErrorScope::addWarning(HUSD_ERR_STRING, msg.buffer());
            continue;
        }

        if (shape_prim.IsInstanceProxy())
        {
            HUSD_ErrorScope::addWarning(
                    HUSD_ERR_IGNORING_INSTANCE_PROXY,
                    shape_prim_path.GetText());
            continue;
        }

        XUSD_ApexShapeBinding binding;
        binding.myPrimPath = shape_prim_path;
        binding.myShapeType = *shape_type;
        xusdRecordShapePrimvarInfo(
                shape_prim, binding.myShapeType, binding.myPrimvars,
                dependencies);

        std::string rig_input;
        binding_api.GetInputAttr().Get(&rig_input);
        binding.myInputName = rig_input;

        std::string rig_output;
        binding_api.GetOutputAttr().Get(&rig_output);
        binding.myOutputName = rig_output;

        // Geometry and primvar properties are inputs to conversion from USD
        // prims to Houdini geometry for the APEX scene.
        // TODO - determine if a more precise set of dependencies should be used
        xusdAddPrimPropertyDeps(shape_prim, dependencies);

        if (binding.myOutputName && !validator.addOutputPrim(shape_prim_path))
            continue;

        shape_bindings.append(std::move(binding));
    }

    return shape_bindings;
}

/// Extract and validate the prims which are bound to joints from a rig's
/// skeleton output.
UT_Array<XUSD_ApexXformBinding>
xusdReadXformBindings(
        const UsdStageRefPtr &stage,
        const UsdHoudiniHoudiniApexCharacterAPI &char_api,
        XUSD_ApexSceneValidator &validator,
        XUSD_UsdApexSceneDependencies &dependencies)
{
    UT_Array<XUSD_ApexXformBinding> xform_bindings;

    for (const UsdHoudiniHoudiniApexXformBindingAPI &binding_api :
         UsdHoudiniHoudiniApexXformBindingAPI::GetAll(char_api.GetPrim()))
    {
        xusdAddPropertyDep(binding_api.GetBindingRel(), dependencies);
        xusdAddPropertyDep(binding_api.GetJointAttr(), dependencies);
        xusdAddPropertyDep(binding_api.GetOutputAttr(), dependencies);

        SdfPathVector xform_targets;
        binding_api.GetBindingRel().GetTargets(&xform_targets);
        if (xform_targets.size() != 1 || !xform_targets.front().IsPrimPath())
            continue;

        const SdfPath xform_prim_path = xform_targets.front().GetPrimPath();
        const UsdPrim xform_prim = stage->GetPrimAtPath(xform_prim_path);
        dependencies.addPrim(xform_prim_path);
        if (!xform_prim || !xform_prim.IsActive())
            continue;

        if (!UsdGeomXformable(xform_prim))
        {
            UT_WorkBuffer msg;
            msg.format(
                    "Primitive '{0}' is not {1}, skipping.", xform_prim_path,
                    UsdGeomTokens->Xformable);
            HUSD_ErrorScope::addWarning(HUSD_ERR_STRING, msg.buffer());
            continue;
        }

        if (xform_prim.IsInstanceProxy())
        {
            HUSD_ErrorScope::addWarning(
                    HUSD_ERR_IGNORING_INSTANCE_PROXY,
                    xform_prim_path.GetText());
            continue;
        }

        XUSD_ApexXformBinding binding;
        binding.myPrimPath = xform_prim_path;

        std::string joint_name;
        binding_api.GetJointAttr().Get(&joint_name);
        binding.myJointName = joint_name;

        std::string rig_output;
        binding_api.GetOutputAttr().Get(&rig_output);
        binding.mySkelOutputName = rig_output;

        if (!validator.addOutputPrim(xform_prim_path))
            continue;

        xform_bindings.append(std::move(binding));
    }

    return xform_bindings;
}

GU_ConstDetailHandle
xusdConvertPrimToGeometry(
        const UsdStageConstRefPtr &stage,
        const SdfPath &prim_path,
        const XUSD_UsdApexSceneImportOptions &import_options)
{
    const UsdPrim prim = stage->GetPrimAtPath(prim_path);
    UT_ASSERT(prim.IsValid());
    if (!prim)
        return GU_ConstDetailHandle();

    GU_DetailHandle shape_gdh;
    shape_gdh.allocateAndSet(new GU_Detail());

    GT_RefineParms refine_parms;
    // Skip adding the file path attribute, which is random (pointer
    // value) for stages from LOPs. We also don't need the usdxform
    // or usdmaterialpath point attributes which are used for SOP Modify
    // workflows.
    refine_parms.set(GUSD_REFINE_ADDXFORMATTRIB, false);
    refine_parms.set(GUSD_REFINE_ADDMATERIALPATHATTRIB, false);
    refine_parms.set(GUSD_REFINE_ADDPATHATTRIB, false);

    if (import_options.myPathAttrib)
    {
        refine_parms.set(GUSD_REFINE_ADDPRIMPATHATTRIB, true);
        refine_parms.set(
                GUSD_REFINE_PRIMPATHATTRIB, import_options.myPathAttrib);
    }
    else
        refine_parms.set(GUSD_REFINE_ADDPRIMPATHATTRIB, false);

    UT_Matrix4D bind_xform(1.0);

    const GusdPurposeSet purposes = GusdPurposeSetFromMask("*");
    if (!GusdGU_USD::ImportPrimUnpacked(
                *shape_gdh.gdpNC(), prim,
                /*time=*/UsdTimeCode::EarliestTime(),
                /*lod=*/nullptr, purposes, /*primvarPattern=*/"*"_UTsh,
                /*attributePattern=*/UT_StringHolder::theEmptyString,
                /*translateSTtoUV=*/true,
                /*nonTransformingPrimvarPattern=*/GA_Names::rest, &bind_xform,
                &refine_parms))
    {
        return GU_ConstDetailHandle();
    }

    return shape_gdh;
}
} // namespace

bool
XUSD_UsdApexSceneDependencies::hasDependency(
        const SdfPath &path,
        bool include_children) const
{
    if (include_children)
    {
        // Use FindSubtreeRange() to efficiently check whether there are any
        // paths prefixed by the provided path.
        auto range = myPathDependencies.FindSubtreeRange(path);
        return range.first != range.second;
    }
    else
    {
        // Note that with SdfPathTable ancestor paths are implicitly inserted,
        // so we need to check the node's value too.
        auto it = myPathDependencies.find(path);
        return it != myPathDependencies.end() && it->second;
    }
}

XUSD_UsdApexScene::XUSD_UsdApexScene(const SdfPath &scene_prim_path)
    : myScenePrimPath(scene_prim_path)
{
}

/* static */ UT_SortedSet<SdfPath>
XUSD_UsdApexScene::findScenePrimPaths(
        const UsdStageRefPtr &stage,
        const SdfPathVector &root_prim_paths)
{
    utZoneScopedN("APEX find scene prims");
    UT_ASSERT(stage);

    xusd_FindScenePrimsTaskData task_data;
    for (const SdfPath &root_path : root_prim_paths)
    {
        UT_ASSERT(root_path.IsAbsoluteRootOrPrimPath());

        const UsdPrim root_prim = stage->GetPrimAtPath(root_path);

        // The provided root path doesn't need to exist - this might be called
        // for resynced paths which include prims being removed.
        if (!root_prim || !UsdPrimDefaultPredicate(root_prim))
            continue;

        XUSDfindPrims(root_prim, task_data, UsdPrimDefaultPredicate);
    }

    UT_SortedSet<SdfPath> scene_paths;
    task_data.getScenePaths(scene_paths);

    return scene_paths;
}

bool
XUSD_UsdApexScene::load(
        const UsdStageRefPtr &stage,
        XUSD_ApexSceneValidator &validator,
        const XUSD_UsdApexSceneImportOptions &import_options)
{
    mySceneInfo = XUSD_ApexScene();
    mySceneGeometry.clear();
    myDependencies = XUSD_UsdApexSceneDependencies();
    myDependencies.addPrim(myScenePrimPath);

    const UsdHoudiniHoudiniApexScene scene_prim
            = UsdHoudiniHoudiniApexScene::Get(stage, myScenePrimPath);
    if (!scene_prim)
    {
        UT_WorkBuffer msg;
        msg.format(
                "Primitive '{0}' is not a {1}", myScenePrimPath,
                UsdHoudiniTokens->HoudiniApexScene);
        HUSD_ErrorScope::addWarning(HUSD_ERR_STRING, msg.buffer());
        return false;
    }
    if (!scene_prim.GetPrim().IsActive())
        return false;

    validator.setCurrentScenePath(myScenePrimPath);

    const UsdAttribute scene_files_attr = scene_prim.GetSceneFilesAttr();
    const UsdAttribute inherit_layers_attr
            = scene_prim.GetInheritAnimationLayersAttr();
    xusdAddPropertyDep(scene_files_attr, myDependencies);
    xusdAddPropertyDep(inherit_layers_attr, myDependencies);
    scene_files_attr.Get(&mySceneInfo.myFiles);
    inherit_layers_attr.Get(&mySceneInfo.myInheritLayers);

    if (mySceneInfo.myInheritLayers.size() != mySceneInfo.myFiles.size())
    {
        UT_WorkBuffer msg;
        msg.format(
                "Invalid number of values for '{0}' attribute on primitive "
                "'{1}', expected {2} but got {3}",
                UsdHoudiniTokens->inheritAnimationLayers, myScenePrimPath,
                mySceneInfo.myFiles.size(), mySceneInfo.myInheritLayers.size());
        HUSD_ErrorScope::addWarning(HUSD_ERR_STRING, msg.buffer());
        return false;
    }

    for (const UsdHoudiniHoudiniApexCharacterBindingAPI &char_binding :
         UsdHoudiniHoudiniApexCharacterBindingAPI::GetAll(scene_prim.GetPrim()))
    {
        xusdAddPropertyDep(char_binding.GetBindingRel(), myDependencies);
        xusdAddPropertyDep(char_binding.GetRigAttr(), myDependencies);

        XUSD_ApexCharacter character(char_binding.GetName().GetString());

        SdfPathVector char_targets;
        char_binding.GetBindingRel().GetForwardedTargets(&char_targets);
        if (char_targets.size() != 1 || !char_targets.front().IsPrimPath())
            continue;

        const SdfPath char_prim_path = char_targets.front().GetPrimPath();
        myDependencies.addPrim(char_prim_path);
        const UsdHoudiniHoudiniApexCharacterAPI char_api
                = UsdHoudiniHoudiniApexCharacterAPI::Get(stage, char_prim_path);
        if (!char_api)
        {
            UT_WorkBuffer msg;
            msg.format(
                    "Primitive '{0}' is missing required {1} schema",
                    char_prim_path, UsdHoudiniTokens->HoudiniApexCharacterAPI);
            HUSD_ErrorScope::addWarning(HUSD_ERR_STRING, msg.buffer());
            continue;
        }
        if (!char_api.GetPrim().IsActive())
            continue;

        const UsdAttribute char_files_attr = char_api.GetFilesAttr();
        const UsdAttribute char_rig_attr = char_api.GetRigAttr();
        xusdAddPropertyDep(char_files_attr, myDependencies);
        xusdAddPropertyDep(char_rig_attr, myDependencies);
        char_files_attr.Get(&character.myFiles);

        std::string rig_name;
        char_binding.GetRigAttr().Get(&rig_name);
        if (rig_name.empty())
            char_rig_attr.Get(&rig_name);
        character.myRigName = rig_name;

        character.myXformBindings = xusdReadXformBindings(
                stage, char_api, validator, myDependencies);
        character.myShapeBindings = xusdReadShapeBindings(
                stage, char_api, validator, myDependencies);
        mySceneInfo.myCharacters.append(std::move(character));
    }

    auto convert_prim = [&](const SdfPath &prim_path)
    {
        return xusdConvertPrimToGeometry(stage, prim_path, import_options);
    };
    mySceneGeometry = mySceneInfo.loadSceneGeometry(convert_prim);
    return mySceneGeometry.isValid();
}

void
XUSD_UsdApexScene::addOutputsToValidator(
        XUSD_ApexSceneValidator &validator) const
{
    validator.setCurrentScenePath(myScenePrimPath);
    for (const XUSD_ApexCharacter &character : mySceneInfo.myCharacters)
    {
        for (const XUSD_ApexShapeBinding &binding : character.myShapeBindings)
        {
            if (binding.myOutputName)
                validator.addOutputPrim(binding.myPrimPath);
        }
        for (const XUSD_ApexXformBinding &binding : character.myXformBindings)
            validator.addOutputPrim(binding.myPrimPath);
    }
}

PXR_NAMESPACE_CLOSE_SCOPE
