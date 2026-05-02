//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef USDHOUDINI_GENERATED_HOUDINIAPEXSHAPEDEFORMAPI_H
#define USDHOUDINI_GENERATED_HOUDINIAPEXSHAPEDEFORMAPI_H

/// \file usdHoudini/houdiniApexShapeDeformAPI.h

#include "pxr/pxr.h"
#include "./api.h"
#include "pxr/usd/usd/apiSchemaBase.h"
#include "pxr/usd/usd/prim.h"
#include "pxr/usd/usd/stage.h"
#include "./tokens.h"

#include "pxr/base/vt/value.h"

#include "pxr/base/gf/vec3d.h"
#include "pxr/base/gf/vec3f.h"
#include "pxr/base/gf/matrix4d.h"

#include "pxr/base/tf/token.h"
#include "pxr/base/tf/type.h"

PXR_NAMESPACE_OPEN_SCOPE

class SdfAssetPath;

// -------------------------------------------------------------------------- //
// HOUDINIAPEXSHAPEDEFORMAPI                                                  //
// -------------------------------------------------------------------------- //

/// \class UsdHoudiniHoudiniApexShapeDeformAPI
///
/// Houdini API schema for describing input data for deformers
/// (e.g. capture weights) on a point-based prim.
/// 
///
class
USDHOUDINI_API
UsdHoudiniHoudiniApexShapeDeformAPI : public UsdAPISchemaBase
{
public:
    /// Compile time constant representing what kind of schema this class is.
    ///
    /// \sa UsdSchemaKind
    static const UsdSchemaKind schemaKind = UsdSchemaKind::SingleApplyAPI;

    /// Construct a UsdHoudiniHoudiniApexShapeDeformAPI on UsdPrim \p prim .
    /// Equivalent to UsdHoudiniHoudiniApexShapeDeformAPI::Get(prim.GetStage(), prim.GetPath())
    /// for a \em valid \p prim, but will not immediately throw an error for
    /// an invalid \p prim
    explicit UsdHoudiniHoudiniApexShapeDeformAPI(const UsdPrim& prim=UsdPrim())
        : UsdAPISchemaBase(prim)
    {
    }

    /// Construct a UsdHoudiniHoudiniApexShapeDeformAPI on the prim held by \p schemaObj .
    /// Should be preferred over UsdHoudiniHoudiniApexShapeDeformAPI(schemaObj.GetPrim()),
    /// as it preserves SchemaBase state.
    explicit UsdHoudiniHoudiniApexShapeDeformAPI(const UsdSchemaBase& schemaObj)
        : UsdAPISchemaBase(schemaObj)
    {
    }

    /// Destructor.
    virtual ~UsdHoudiniHoudiniApexShapeDeformAPI() override;

    /// Return a vector of names of all pre-declared attributes for this schema
    /// class and all its ancestor classes.  Does not include attributes that
    /// may be authored by custom/extended methods of the schemas involved.
        static const TfTokenVector &
    GetSchemaAttributeNames(bool includeInherited=true);

    /// Return a UsdHoudiniHoudiniApexShapeDeformAPI holding the prim adhering to this
    /// schema at \p path on \p stage.  If no prim exists at \p path on
    /// \p stage, or if the prim at that path does not adhere to this schema,
    /// return an invalid schema object.  This is shorthand for the following:
    ///
    /// \code
    /// UsdHoudiniHoudiniApexShapeDeformAPI(stage->GetPrimAtPath(path));
    /// \endcode
    ///
        static UsdHoudiniHoudiniApexShapeDeformAPI
    Get(const UsdStagePtr &stage, const SdfPath &path);


    /// Returns true if this <b>single-apply</b> API schema can be applied to 
    /// the given \p prim. If this schema can not be a applied to the prim, 
    /// this returns false and, if provided, populates \p whyNot with the 
    /// reason it can not be applied.
    /// 
    /// Note that if CanApply returns false, that does not necessarily imply
    /// that calling Apply will fail. Callers are expected to call CanApply
    /// before calling Apply if they want to ensure that it is valid to 
    /// apply a schema.
    /// 
    /// \sa UsdPrim::GetAppliedSchemas()
    /// \sa UsdPrim::HasAPI()
    /// \sa UsdPrim::CanApplyAPI()
    /// \sa UsdPrim::ApplyAPI()
    /// \sa UsdPrim::RemoveAPI()
    ///
        static bool 
    CanApply(const UsdPrim &prim, std::string *whyNot=nullptr);

    /// Applies this <b>single-apply</b> API schema to the given \p prim.
    /// This information is stored by adding "HoudiniApexShapeDeformAPI" to the 
    /// token-valued, listOp metadata \em apiSchemas on the prim.
    /// 
    /// \return A valid UsdHoudiniHoudiniApexShapeDeformAPI object is returned upon success. 
    /// An invalid (or empty) UsdHoudiniHoudiniApexShapeDeformAPI object is returned upon 
    /// failure. See \ref UsdPrim::ApplyAPI() for conditions 
    /// resulting in failure. 
    /// 
    /// \sa UsdPrim::GetAppliedSchemas()
    /// \sa UsdPrim::HasAPI()
    /// \sa UsdPrim::CanApplyAPI()
    /// \sa UsdPrim::ApplyAPI()
    /// \sa UsdPrim::RemoveAPI()
    ///
        static UsdHoudiniHoudiniApexShapeDeformAPI 
    Apply(const UsdPrim &prim);

protected:
    /// Returns the kind of schema this class belongs to.
    ///
    /// \sa UsdSchemaKind
        UsdSchemaKind _GetSchemaKind() const override;

private:
    // needs to invoke _GetStaticTfType.
    friend class UsdSchemaRegistry;
        static const TfType &_GetStaticTfType();

    static bool _IsTypedSchema();

    // override SchemaBase virtuals.
        const TfType &_GetTfType() const override;

public:
    // --------------------------------------------------------------------- //
    // JOINTS 
    // --------------------------------------------------------------------- //
    /// An array of tokens defining the list of joints to which
    /// jointIndices apply. This attribute is required if jointIndices is
    /// defined.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token[] houdini:apex:deform:joints` |
    /// | C++ Type | VtArray<TfToken> |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->TokenArray |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
        UsdAttribute GetJointsAttr() const;

    /// See GetJointsAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
        UsdAttribute CreateJointsAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // JOINTINDICES 
    // --------------------------------------------------------------------- //
    /// Indices into the *joints* attribute that affect each point of
    /// a PointBased gprim. The primvar can have either *constant* or *vertex*
    /// interpolation. This primvar's *elementSize* will determine how many
    /// joint influences apply to each point. Indices must point be valid. Null
    /// influences should be defined by setting values in jointWeights to zero.
    /// See UsdGeomPrimvar for more information on interpolation and
    /// elementSize.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `int[] primvars:houdini:apex:deform:jointIndices` |
    /// | C++ Type | VtArray<int> |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->IntArray |
        UsdAttribute GetJointIndicesAttr() const;

    /// See GetJointIndicesAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
        UsdAttribute CreateJointIndicesAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // JOINTWEIGHTS 
    // --------------------------------------------------------------------- //
    /// Weights for the joints that affect each point of a PointBased
    /// gprim. The primvar can have either *constant* or *vertex* interpolation.
    /// This primvar's *elementSize* will determine how many joints influences
    /// apply to each point. The length, interpolation, and elementSize of
    /// *jointWeights* must match that of *jointIndices*. See UsdGeomPrimvar
    /// for more information on interpolation and elementSize.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `float[] primvars:houdini:apex:deform:jointWeights` |
    /// | C++ Type | VtArray<float> |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->FloatArray |
        UsdAttribute GetJointWeightsAttr() const;

    /// See GetJointWeightsAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
        UsdAttribute CreateJointWeightsAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // ===================================================================== //
    // Feel free to add custom code below this line, it will be preserved by 
    // the code generator. 
    //
    // Just remember to: 
    //  - Close the class declaration with }; 
    //  - Close the namespace with PXR_NAMESPACE_CLOSE_SCOPE
    //  - Close the include guard with #endif
    // ===================================================================== //
    // --(BEGIN CUSTOM CODE)--
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
