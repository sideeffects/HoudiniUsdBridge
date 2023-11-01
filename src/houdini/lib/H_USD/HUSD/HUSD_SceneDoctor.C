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
 * NAME:	HUSD_SceneDoctor.C (HUSD Library, C++)
 *
 * COMMENTS:
 */

#include "HUSD_SceneDoctor.h"
#include "HUSD_FindPrims.h"
#include "HUSD_PathSet.h"
#include "HUSD_PythonConverter.h"
#include "XUSD_Data.h"
#include "XUSD_FindPrimsTask.h"
#include <PY/PY_CPythonAPI.h>
#include <UT/UT_DirUtil.h>
#include <UT/UT_Map.h>
#include <pxr/usd/kind/registry.h>
#include <pxr/usd/sdf/layerUtils.h>
#include <pxr/usd/usd/clipsAPI.h>
#include <pxr/usd/usd/modelAPI.h>
#include <pxr/usd/usd/tokens.h>
#include <pxr/usd/usdGeom/basisCurves.h>
#include <pxr/usd/usdGeom/gprim.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/points.h>
#include <pxr/usd/usdGeom/pointInstancer.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/usd/usdGeom/xform.h>
#include <pxr/usd/usdGeom/subset.h>
#include <pxr/usd/usdVol/volume.h>
#include <pxr/usd/usdVol/fieldBase.h>

PXR_NAMESPACE_USING_DIRECTIVE

// Utility class to help with multithreaded tree traversal
class XUSD_ValidationTaskData : public XUSD_FindPrimsTaskData
{
public:
    XUSD_ValidationTaskData(HUSD_SceneDoctor::ValidationFlags &flags,
                              const XUSD_PathSet *pathSet);
    ~XUSD_ValidationTaskData() override;
    void addToThreadData(const UsdPrim &prim, bool *prune) override;
    void addToKindThreadData(const UsdPrim &prim, const UsdPrim &parentPrim);
    void addToGprimThreadData(const UsdPrim &prim, const UsdPrim &parentPrim);
    void addToPrimvarThreadData(const UsdPrim &prim);
    void addToValueClipThreadData(const UsdPrim &prim);
    void gatherPathsFromThreads(UT_Array<HUSD_SceneDoctor::ValidationError> &errors);
private:
    class XUSD_ValidationTaskThreadData
    {
    public:
        UT_Map<HUSD_Path, int> myValidationErrors;
    };
    typedef UT_ThreadSpecificValue<XUSD_ValidationTaskThreadData *>
            FindPrimKindTaskThreadDataTLS;

    FindPrimKindTaskThreadDataTLS          myThreadData;
    HUSD_SceneDoctor::ValidationFlags      myFlags;
    const XUSD_PathSet                     *myXusdPathSet;
};

XUSD_ValidationTaskData::XUSD_ValidationTaskData(HUSD_SceneDoctor::ValidationFlags &flags,
                                                     const XUSD_PathSet *pathSet)
                                                    : myFlags{flags},
                                                      myXusdPathSet{pathSet}
{
}

XUSD_ValidationTaskData::~XUSD_ValidationTaskData()
{
    for(auto it = myThreadData.begin(); it != myThreadData.end(); ++it)
    {
        if(auto* tdata = it.get())
            delete tdata;
    }
}

void
XUSD_ValidationTaskData::addToKindThreadData(const UsdPrim &prim,
                                               const UsdPrim &parentPrim)
{
    UT_StringHolder childKind, parentKind;
    TfToken childKindtk, parentKindtk;
    UsdModelAPI childModelApi(prim);
    UsdModelAPI parentModelApi(parentPrim);

    if(!(childModelApi && childModelApi.GetKind(&childKindtk)
       && parentModelApi && parentModelApi.GetKind(&parentKindtk)))
        UT_ASSERT("Couldn't properly load kind");

    if(prim.IsPseudoRoot() || prim.GetParent().IsPseudoRoot())
        return ;

    //lambda function to add a new error to the thread data object
    auto addValidationError = [this, &prim] (int errorType)
    {
        auto *&threadData = myThreadData.get();
        if(!threadData)
            threadData = new XUSD_ValidationTaskThreadData;
        threadData->myValidationErrors[prim.GetPath()] = errorType;
    };

    if(parentKindtk.IsEmpty() || childKindtk.IsEmpty())
    {
        if(parentKindtk.IsEmpty() && !childKindtk.IsEmpty() &&
            KindRegistry::IsA(childKindtk, KindTokens->model))
        {
            addValidationError(HUSD_SceneDoctor::PARENT_PRIM_IS_NONE_KIND);
        }
        return ;
    }
    // pre: both the parent and child have a kind that is not none.
    // TODO: consider writing an assert here
    else if(KindRegistry::IsA(parentKindtk, KindTokens->model))
    {
        if(KindRegistry::IsA(parentKindtk, KindTokens->component)
            && KindRegistry::IsA(childKindtk, KindTokens->model))
        {
            addValidationError(HUSD_SceneDoctor::COMPONENT_HAS_MODEL_CHILD);
        }
    }
    else
    {
        if(KindRegistry::IsA(childKindtk, KindTokens->model))
        {
            addValidationError(HUSD_SceneDoctor::SUBCOMPONENT_HAS_MODEL_CHILD);
            return ;
        }
    }
}

void
XUSD_ValidationTaskData::addToGprimThreadData(const UsdPrim &prim,
                                              const UsdPrim &parentPrim)
{
    UT_StringRef parentType = parentPrim.GetTypeName().GetText();
    if (!parentType.isstring())
        parentType = "Untyped";

    if(!parentPrim.IsA<UsdGeomGprim>() ||
            (parentPrim.IsA<UsdGeomMesh>() && prim.IsA<UsdGeomSubset>()) ||
            (parentPrim.IsA<UsdVolVolume>() && prim.IsA<UsdVolFieldBase>()))
        return ;

    auto *&threadData = myThreadData.get();
    if(!threadData)
        threadData = new XUSD_ValidationTaskThreadData;
    threadData->myValidationErrors[prim.GetPath()] =
            HUSD_SceneDoctor::GPRIM_TYPE_HAS_CHILD;
}

void
XUSD_ValidationTaskData::addToValueClipThreadData(const UsdPrim &prim)
{
    UsdClipsAPI clipsApi(prim);
    VtDictionary clips;
    clipsApi.GetClips(&clips);
    if (clips.empty())
        return;

    std::string clipSet;
    SdfLayerRefPtr layer = prim.GetStage()->GetRootLayer();
    VtDictionary::iterator it = clips.begin();
    VtDictionary::iterator end = clips.end();
    for (;it != end; it++)
    {
        clipSet = it->first;
        SdfAssetPath path;
        clipsApi.GetClipManifestAssetPath(&path, clipSet);
        const std::string manifestAssetPath =
                path.GetResolvedPath().empty() ? path.GetAssetPath() : path.GetResolvedPath();
        if (manifestAssetPath.empty())
            break;
        else
        {
            const UT_StringHolder absolutePath = SdfComputeAssetPathRelativeToLayer(
                    layer, manifestAssetPath);
            if (!UTfileExists(absolutePath))
                break;
        }
    }

    if (it != end)
    {
        auto *&threadData = myThreadData.get();
        if (!threadData)
            threadData = new XUSD_ValidationTaskThreadData;
        threadData->myValidationErrors[prim.GetPath()]
                = HUSD_SceneDoctor::MISSING_VALUECLIP_MANIFEST;
    }
}

void
XUSD_ValidationTaskData::addToPrimvarThreadData(const UsdPrim &prim)
{
    // The following code assumes the following from USD:
    // UsdGeomMesh accepts all interpolation types, treats varying same as vertex
    // UsdGeomCurve does not accept faceVarying, treats varying same as vertex
    // UsdGeomPoint and UsdGeomPointInstancer only accept constant and vertex
    // interpolation. Every other type only supports constant interpolation
    UsdGeomPrimvarsAPI primvarAPI = UsdGeomPrimvarsAPI(prim);
    auto getArrayLengthFromAttribute = []
            (const UsdAttribute &attribute) -> int
    {
        VtValue value;
        attribute.Get(&value);
        return value.IsArrayValued() ? value.GetArraySize() : 1;
    };

    auto setValidationError = [this]
            (HUSD_Path primvarPath, int errorCode = HUSD_SceneDoctor::UNDEFINED,
             int primitiveCount=-1, int primvarArraySize=1, int primvarElementSize=1)
    {
        // The UsdGeomMesh docs state: To author a uniform spherical harmonic
        // primvar on a Mesh of 42 faces, the primvar's array value would
        // contain 9*42 = 378 float elements. Basically, len(primvar array) =
        // count(primitives) * len(atomic unit in the primvar array).
        if(primitiveCount < 0 ||
           primvarArraySize != primitiveCount * primvarElementSize)
        {
            auto *&threadData = myThreadData.get();
            if(!threadData)
                threadData = new XUSD_ValidationTaskThreadData;
            threadData->myValidationErrors[primvarPath] = errorCode;
        }
    };
    auto validatePrimvar = [getArrayLengthFromAttribute, setValidationError]
            (const UsdGeomPrimvar &primvar, const UsdPrim &prim,
             TfToken &interpolation, int primvarArraySize)
    {
        const HUSD_Path primvarPath = primvar.GetAttr().GetPath();
        if(!primvar.IsValidInterpolation(interpolation))
        {
            setValidationError(primvarPath,
                               HUSD_SceneDoctor::INTERPOLATION_TYPE_MISMATCH);
            return ;
        }

        int elementSize = primvar.GetElementSize();
        int primitiveCount = -1;
        // check if single-value primvar or array-value primvar of length 1
        if(interpolation == UsdGeomTokens->constant)
            primitiveCount = 1;
            // check # of USD::faces (Houdini::polygons)
        else if(interpolation == UsdGeomTokens->uniform)
        {
            if(prim.IsA<UsdGeomMesh>())
            {
                UsdGeomMesh mesh = UsdGeomMesh(prim);
                primitiveCount = mesh.GetFaceCount();
            }
            else if(prim.IsA<UsdGeomBasisCurves>())
            {
                UsdGeomBasisCurves curves = UsdGeomBasisCurves(prim);
                primitiveCount = curves.GetCurveCount();
            }
            else
            {
                setValidationError(primvarPath,
                                   HUSD_SceneDoctor::INTERPOLATION_TYPE_MISMATCH);
                return;
            }
        }
        else if(interpolation == UsdGeomTokens->vertex)
        {
            if(prim.IsA<UsdGeomMesh>())
            {
                UsdGeomMesh mesh = UsdGeomMesh(prim);
                UsdAttribute vertexAttr = mesh.GetPointsAttr();
                primitiveCount = getArrayLengthFromAttribute(vertexAttr);
            }
            else if(prim.IsA<UsdGeomBasisCurves>())
            {
                UsdGeomBasisCurves curves = UsdGeomBasisCurves(prim);
                UsdAttribute vertexAttr = curves.GetPointsAttr();
                primitiveCount = getArrayLengthFromAttribute(vertexAttr);
            }
            else if(prim.IsA<UsdGeomPoints>())
            {
                UsdGeomPoints points = UsdGeomPoints(prim);
                primitiveCount = points.GetPointCount();
            }
            else if(prim.IsA<UsdGeomPointInstancer>())
            {
                UsdGeomPointInstancer pointInstancer = UsdGeomPointInstancer(prim);
                primitiveCount = pointInstancer.GetInstanceCount();
            }
            else
            {
                setValidationError(primvarPath,
                                   HUSD_SceneDoctor::INTERPOLATION_TYPE_MISMATCH);
                return;
            }
        }
        else if(interpolation == UsdGeomTokens->varying)
        {
            if(prim.IsA<UsdGeomMesh>())
            {
                UsdGeomMesh mesh = UsdGeomMesh(prim);
                UsdAttribute vertexAttr = mesh.GetPointsAttr();
                primitiveCount = getArrayLengthFromAttribute(vertexAttr);
            }
            else if(prim.IsA<UsdGeomBasisCurves>())
            {
                UsdGeomBasisCurves curves = UsdGeomBasisCurves(prim);
                UsdAttribute vertexAttr = curves.GetPointsAttr();
                primitiveCount = getArrayLengthFromAttribute(vertexAttr);
            }
            else
            {
                setValidationError(primvarPath,
                                   HUSD_SceneDoctor::INTERPOLATION_TYPE_MISMATCH);
                return;
            }
        }
        else // interpolation setting is USD::face-varying (Hou::vertex)
        {
            if(prim.IsA<UsdGeomMesh>())
            {
                UsdGeomMesh mesh = UsdGeomMesh(prim);
                UsdAttribute faceVertexIndicesAttr = mesh.GetFaceVertexIndicesAttr();
                primitiveCount = getArrayLengthFromAttribute(faceVertexIndicesAttr);
                VtIntArray fvi;
                faceVertexIndicesAttr.Get(&fvi);
            }
            else
            {
                setValidationError(primvarPath,
                                   HUSD_SceneDoctor::INTERPOLATION_TYPE_MISMATCH);
                return;
            }
        }
        setValidationError(primvarPath,
                           HUSD_SceneDoctor::PRIMVAR_ARRAY_LENGTH_MISMATCH, primitiveCount,
                           primvarArraySize, elementSize);
    };
    for(const UsdGeomPrimvar &primvar : primvarAPI.GetAuthoredPrimvars())
    {
        TfToken interpolation = primvar.GetInterpolation();
        // per-primvar checks
        VtIntArray indices;
        if (primvar.GetIndices(&indices))
        {
            // Primvar is indexed: the value of the attribute associated with
            // the primvar is set to an array consisting of all the unique values
            // that appear in the primvar array. The "indices" attribute is set
            // to an integer array containing indices into the array with all the
            // unique elements. The final value of the primvar is computed using
            // the indices array and the attribute value array.
            validatePrimvar(primvar, prim, interpolation, indices.size());
            // check to make sure all indices are within valid range
            UsdAttribute values = primvar.GetAttr();
            VtIntArray valuesArray;
            if(values.Get(&valuesArray))
            {
                for(const auto &index : indices)
                {
                    if(index < 0 || index >= valuesArray.size())
                    {
                        setValidationError(primvar.GetAttr().GetPath(),
                                           HUSD_SceneDoctor::INVALID_PRIMVAR_INDICES);
                        break;
                    }
                }
            }
        }
        else
        {
            // validate values as flat array
            int primvarArraySize = getArrayLengthFromAttribute(primvar.GetAttr());
            validatePrimvar(primvar, prim, interpolation, primvarArraySize);
        }
    }
    // TODO: This does not belong here. Possibly new category of validations
//    // must ensure that the sum of the vertex counts is equal to the number of points.
//    auto getArraySumFromAttribute = []
//            (const UsdAttribute &attribute) -> int
//    {
//        VtValue value;
//        attribute.Get(&value);
//        if(value.IsArrayValued())
//        {
//            int arraySum = 0;
//            for (const auto &item : value.Get<VtIntArray>())
//                arraySum += item;
//            return arraySum;
//        }
//        else
//            return -1;
//    };
//    if(prim.IsA<UsdGeomBasisCurves>())
//    {
//        UsdGeomBasisCurves curves = UsdGeomBasisCurves(prim);
//        UsdAttribute curveVertexCountsAttr = curves.GetCurveVertexCountsAttr();
//        int arraySum = getArraySumFromAttribute(curveVertexCountsAttr);
//        UsdAttribute vertexCountsAttr = curves.GetPointsAttr();
//        int pointCount = getArrayLengthFromAttribute(vertexCountsAttr);
//        if(arraySum < 0 || arraySum != pointCount)
//            setValidationError(primvarPath, HUSD_SceneDoctor::PRIM_ARRAY_LENGTH_MISMATCH);
//    }
}

void
XUSD_ValidationTaskData::addToThreadData(
        const UsdPrim &prim, bool *prune)
{
    auto validatePrim = [this] (const UsdPrim &prim)
    {
        UsdPrim parentPrim = prim.GetParent();
        if (myFlags.myValidateKind)
            addToKindThreadData(prim, parentPrim);
        if (myFlags.myValidateGprims)
            addToGprimThreadData(prim, parentPrim);
        if (myFlags.myValidatePrimvars)
            addToPrimvarThreadData(prim);
        if (myFlags.myValidateValueClips)
            addToValueClipThreadData(prim);
    };
    if(!myXusdPathSet || myXusdPathSet->contains(prim.GetPrimPath()))
        validatePrim(prim);
    if(myXusdPathSet && !myXusdPathSet->containsPathOrDescendant(prim.GetPrimPath()))
        *prune = true;
};

void
XUSD_ValidationTaskData::gatherPathsFromThreads(
        UT_Array<HUSD_SceneDoctor::ValidationError> &errors)
{
    for(auto it = myThreadData.begin(); it != myThreadData.end(); ++it)
    {
        if(const auto* tdata = it.get())
        {
            for(auto &err : tdata->myValidationErrors)
            {
                errors.emplace_back(err.first, err.second);
            }
        }
    }
    errors.sort();
}

HUSD_SceneDoctor::HUSD_SceneDoctor(HUSD_AutoAnyLock &lock, ValidationFlags &flags)
    : myLock{lock}, myFlags{flags}
{
}

HUSD_SceneDoctor::~HUSD_SceneDoctor()
{
}

void
HUSD_SceneDoctor::validate(UT_Array<ValidationError> &errors, const HUSD_FindPrims *prims)
{
    XUSD_ConstDataPtr               xusdData = myLock.constData();
    UsdStageRefPtr                  stage = xusdData->stage();
    //user can pass in a null HUSD_FindPrims ptr to save having to create one
    XUSD_ValidationTaskData       data(myFlags,
                                       (prims ? &prims->getExpandedPathSet().sdfPathSet() : nullptr));
    UsdPrim                         root;

    root = stage->GetPseudoRoot();
    if (root)
    {
        auto demands = HUSD_PrimTraversalDemands(
                HUSD_TRAVERSAL_DEFAULT_DEMANDS
                | HUSD_TRAVERSAL_ALLOW_INSTANCE_PROXIES);
        auto predicate = HUSDgetUsdPrimPredicate(demands);
        XUSDfindPrims(root, data, predicate, nullptr, nullptr);
        data.gatherPathsFromThreads(errors);
    }
}

bool
HUSD_SceneDoctor::validatePython(const HUSD_FindPrims *validationPrims,
                                 const HUSD_FindPrims *collectionPrim,
                                 const UT_String &collectionName,
                                 PY_CompiledCode &pyExpr)
{
    if(!validationPrims)
    {
        // if validationPrims is nullptr, it makes sense to assume start from the root.
        UT_UniquePtr<HUSD_FindPrims> root(new HUSD_FindPrims(myLock, "/"));
        root->addDescendants(); // implicitly, assume to also include descendants
    }
    HUSD_PythonConverter pythonConverter(myLock);
    PY_EvaluationContext pyContext;
    PY_Result pyResult;
    PY_InterpreterAutoLock interpreter_lock;
    auto&& globals = (PY_PyObject*)pyContext.getGlobalsDict();

    for(const auto prim : validationPrims->getExpandedPathSet())
    {
        void *primToPython = pythonConverter.getPrim(prim.pathStr());
        PY_PyDict_SetItemString(globals, "prim",
                                (PY_PyObject *)primToPython);
        PY_Py_DECREF((PY_PyObject *)primToPython);

        void *collectionPrimToPython = pythonConverter.getPrim(
                collectionPrim->getExpandedPathSet().getFirstPathAsString());
        PY_PyDict_SetItemString(globals, "collection_prim",
                                (PY_PyObject *)collectionPrimToPython);
        PY_Py_DECREF((PY_PyObject *)collectionPrimToPython);

        PY_PyDict_SetItemString(globals, "collection_name",
                                PY_PyString_FromString(collectionName));
        PY_Py_DECREF(PY_PyString_FromString(collectionName));

        pyExpr.evaluateInContext(PY_Result::PY_OBJECT, pyContext, pyResult);
        if (pyResult.myResultType == PY_Result::ERR)
            return false;

        pyContext.clear();
    }

    return true;
}
