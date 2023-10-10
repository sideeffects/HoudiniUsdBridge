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
#include <UT/UT_Map.h>
#include <pxr/usd/kind/registry.h>
#include <pxr/usd/usd/modelAPI.h>
#include <pxr/usd/usd/tokens.h>
#include <pxr/usd/usdGeom/basisCurves.h>
#include <pxr/usd/usdGeom/gprim.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/points.h>
#include <pxr/usd/usdGeom/pointInstancer.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>
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
    void addToTypeThreadData(const UsdPrim &prim, const UsdPrim &parentPrim);
    void addToPrimvarThreadData(const UsdPrim &prim);
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
XUSD_ValidationTaskData::addToTypeThreadData(const UsdPrim &prim,
                                               const UsdPrim &parentPrim)
{
    UT_StringRef parentType = parentPrim.GetTypeName().GetText();
    if (!parentType.isstring())
        parentType = "Untyped";

    if(!parentPrim.IsA<UsdGeomGprim>())
    {
        return ;
    }
    if(parentPrim.IsA<UsdGeomMesh>() && prim.IsA<UsdGeomSubset>())
    {
        return;
    }
    if(parentPrim.IsA<UsdVolVolume>() && prim.IsA<UsdVolFieldBase>())
    {
        return;
    }
    auto *&threadData = myThreadData.get();
    if(!threadData)
        threadData = new XUSD_ValidationTaskThreadData;
    threadData->myValidationErrors[prim.GetPath()] =
            HUSD_SceneDoctor::GPRIM_TYPE_HAS_CHILD;
}

void
XUSD_ValidationTaskData::addToPrimvarThreadData(const UsdPrim &prim)
{
    // The following code assumes the following from USD:
    // UsdGeomMesh accepts all interpolation types, treats varying same as vertex
    // UsdGeomCurve does not accept faceVarying, treats varying same as vertex
    // UsdGeomPoint and UsdGeomPointInstancer only accept constant and vertex interpolation
    // Every other type only supports constant interpolation
    UsdGeomPrimvarsAPI primvarAPI = UsdGeomPrimvarsAPI(prim);
    auto getArrayLengthFromAttribute = []
            (const UsdAttribute &attribute) -> int
    {
        VtValue element;
        attribute.Get(&element);
        if(element.IsArrayValued())
            return element.GetArraySize();
        else
            return 1;
    };
    auto sumArrayElementsFromAttribute = []
            (const UsdAttribute &attribute) -> int
    {
        VtValue element;
        attribute.Get(&element);
        if(element.IsArrayValued())
        {
            int arraySum = 0;
            for (const auto &item : element.Get<VtIntArray>())
                arraySum += item;
            return arraySum;
        }
        else
            return -1;
    };
    auto determineValidationError = [this, &prim]
            (int count, int primvarArraySize=1, int elementSize=1,
             int errorCode = HUSD_SceneDoctor::PRIMVAR_ARRAY_LENGTH_MISMATCH)
    {

        // The UsdGeomMesh docs state: To author a uniform spherical harmonic primvar
        // on a Mesh of 42 faces, the primvar's array value would contain 9*42 = 378 float elements.
        // So, the user parameter "count" should be the one multiplied.
        if(count < 0 || primvarArraySize != (count * elementSize))
        {
            auto *&threadData = myThreadData.get();
            if(!threadData)
                threadData = new XUSD_ValidationTaskThreadData;
            threadData->myValidationErrors[prim.GetPath()] = errorCode;
        }
    };
    auto validatePrimvar = [getArrayLengthFromAttribute, determineValidationError]
            (const UsdGeomPrimvar &primvar, const UsdPrim &prim,
             TfToken &interpolation, int primvarArraySize)
    {
        if(!primvar.IsValidInterpolation(interpolation))
        {
            determineValidationError(-1,HUSD_SceneDoctor::INTERPOLATION_TYPE_MISMATCH);
            return ;
        }
        int elementSize = primvar.GetElementSize();
        // check if single-value primvar or array-value primvar of length 1
        if(interpolation == UsdGeomTokens->constant)
        {
            int count = 1;
            determineValidationError(count, primvarArraySize, elementSize);
        }
        // check # of USD::faces (Houdini::polygons)
        else if(interpolation == UsdGeomTokens->uniform)
        {
            if(prim.IsA<UsdGeomMesh>())
            {
                UsdGeomMesh mesh = UsdGeomMesh(prim);
                size_t faceCount = mesh.GetFaceCount();
                determineValidationError(faceCount, primvarArraySize, elementSize);
            }
            else if(prim.IsA<UsdGeomBasisCurves>())
            {
                UsdGeomBasisCurves curves = UsdGeomBasisCurves(prim);
                size_t curveCount = curves.GetCurveCount();
                determineValidationError(curveCount, primvarArraySize, elementSize);
            }
            else
                determineValidationError(-1,HUSD_SceneDoctor::INTERPOLATION_TYPE_MISMATCH);
        }
        else if(interpolation == UsdGeomTokens->vertex)
        {
            if(prim.IsA<UsdGeomMesh>())
            {
                UsdGeomMesh mesh = UsdGeomMesh(prim);
                UsdAttribute vertexCountsAttr = mesh.GetPointsAttr();
                size_t vertexCount = getArrayLengthFromAttribute(vertexCountsAttr);
                determineValidationError(vertexCount, primvarArraySize, elementSize);
            }
            else if(prim.IsA<UsdGeomBasisCurves>())
            {
                UsdGeomBasisCurves curves = UsdGeomBasisCurves(prim);
                UsdAttribute vertexCountsAttr = curves.GetPointsAttr();
                size_t vertexCount = getArrayLengthFromAttribute(vertexCountsAttr);
                determineValidationError(vertexCount, primvarArraySize, elementSize);
            }
            else if(prim.IsA<UsdGeomPoints>())
            {
                UsdGeomPoints points = UsdGeomPoints(prim);
                size_t pointCount = points.GetPointCount();
                determineValidationError(pointCount, primvarArraySize, elementSize);
            }
            else if(prim.IsA<UsdGeomPointInstancer>())
            {
                UsdGeomPointInstancer pointInstancer = UsdGeomPointInstancer(prim);
                size_t pointInstanceCount = pointInstancer.GetInstanceCount();
                determineValidationError(pointInstanceCount, primvarArraySize, elementSize);
            }
            else
                determineValidationError(-1, HUSD_SceneDoctor::INTERPOLATION_TYPE_MISMATCH);
        }
        else if(interpolation == UsdGeomTokens->varying)
        {
            if(prim.IsA<UsdGeomMesh>())
            {
                UsdGeomMesh mesh = UsdGeomMesh(prim);
                UsdAttribute vertexCountsAttr = mesh.GetPointsAttr();
                size_t vertexCount = getArrayLengthFromAttribute(vertexCountsAttr);
                determineValidationError(vertexCount, primvarArraySize, elementSize);
            }
            else if(prim.IsA<UsdGeomBasisCurves>())
            {
                UsdGeomBasisCurves curves = UsdGeomBasisCurves(prim);
                UsdAttribute vertexCountsAttr = curves.GetPointsAttr();
                size_t vertexCount = getArrayLengthFromAttribute(vertexCountsAttr);
                determineValidationError(vertexCount, primvarArraySize, elementSize);
            }
            else
                determineValidationError(-1, HUSD_SceneDoctor::INTERPOLATION_TYPE_MISMATCH);
        }
        else // interpolation setting is USD::face-varying (Hou::vertex)
        {
            if(prim.IsA<UsdGeomMesh>())
            {
                UsdGeomMesh mesh = UsdGeomMesh(prim);
                UsdAttribute faceVaryingCountsAttr = mesh.GetFaceVertexIndicesAttr();
                size_t faceVaryingCount = getArrayLengthFromAttribute(faceVaryingCountsAttr);
                determineValidationError(faceVaryingCount, primvarArraySize, elementSize);
            }
            else
                determineValidationError(-1, HUSD_SceneDoctor::INTERPOLATION_TYPE_MISMATCH);
        }
    };
    for(const UsdGeomPrimvar &primvar : primvarAPI.GetAuthoredPrimvars())
    {
        TfToken interpolation = primvar.GetInterpolation();
        // per-primvar checks
        VtIntArray indices;
        if (primvar.GetIndices(&indices))
        {
            // primvar is indexed: validate/process values and indices together
            validatePrimvar(primvar, prim, interpolation, indices.size());
            // check to make sure all indices are within valid range
            UsdAttribute values = primvar.GetAttr();
            VtIntArray valuesArray;
            if(values.Get(&valuesArray))
            {
                for(const auto &index : valuesArray)
                {
                    if(index < 0 || index >= indices.size())
                    {
                        determineValidationError(-1, HUSD_SceneDoctor::INVALID_PRIMVAR_INDICES);
                        break;
                    }
                }
            }
        }
        else
        {
            int primvarArraySize = getArrayLengthFromAttribute(primvar.GetAttr());
            validatePrimvar(primvar, prim, interpolation, primvarArraySize);
        }
    }
    // must ensure that the sum of the vertex counts is equal to the number of points.
    if(prim.IsA<UsdGeomBasisCurves>())
    {
        UsdGeomBasisCurves curves = UsdGeomBasisCurves(prim);
        UsdAttribute curveVertexCountsAttr = curves.GetCurveVertexCountsAttr();
        int arraySum = sumArrayElementsFromAttribute(curveVertexCountsAttr);
        UsdAttribute vertexCountsAttr = curves.GetPointsAttr();
        int pointCount = getArrayLengthFromAttribute(vertexCountsAttr);
        if(arraySum < 0 || arraySum != pointCount)
            determineValidationError(-1, HUSD_SceneDoctor::PRIM_ARRAY_LENGTH_MISMATCH);
    }
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
        if (myFlags.myValidateType)
            addToTypeThreadData(prim, parentPrim);
        if (myFlags.myValidatePrimvars)
            addToPrimvarThreadData(prim);
    };
    if(!myXusdPathSet || myXusdPathSet->contains(prim.GetPrimPath()))
    {
        validatePrim(prim);
    }
    if(myXusdPathSet && !myXusdPathSet->containsPathOrDescendant(prim.GetPrimPath()))
    {
        *prune = true;
    }
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

void
HUSD_SceneDoctor::validatePython(UT_Array<PythonValidationError> &pythonErrors,
                                 const HUSD_FindPrims *prims,
                                 PY_CompiledCode &pyExpr)
{
    if(!prims) // if prims is nullptr, it makes sense to assume start from the root.
    {
        UT_UniquePtr<HUSD_FindPrims> root(new HUSD_FindPrims(myLock, "/"));
        root->addDescendants(); // implicitly, assume to also include descendants
    }
    HUSD_PythonConverter pythonConverter(myLock);
    PY_EvaluationContext pyContext;
    PY_Result pyResult;
    PY_InterpreterAutoLock interpreter_lock;
    for(const auto prim : prims->getExpandedPathSet())
    {
        void *primToPython = pythonConverter.getPrim(prim.pathStr());
        PY_PyDict_SetItemString(
                (PY_PyObject *)pyContext.getGlobalsDict(),
                "prim",  (PY_PyObject *)primToPython);
        PY_Py_DECREF((PY_PyObject *)primToPython);
        pyExpr.evaluateInContext(PY_Result::PY_OBJECT, pyContext, pyResult);
        if(pyResult.myResultType == PY_Result::PY_OBJECT)
        {
            PY_PyObject *optionsObj = pyResult.myOpaquePyObject.pyObject();
            if(optionsObj != PY_Py_None())
            {
                UT_StringHolder error(PY_PyString_AsString(optionsObj));
                if(!error.isEmpty())
                    pythonErrors.emplace_back(prim, PYTHON_VALIDATION_ERROR, error);
            }
        }
        else
        {
            pythonErrors.clear();
            if(pyResult.myResultType == PY_Result::ERR)
                pythonErrors.emplace_back(prim, PYTHON_EXCEPTION, pyResult.myErrValue);
            return ;
        }
        pyContext.clear();
    }
}
