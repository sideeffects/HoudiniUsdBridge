//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef USDHOUDINI_GENERATED_HOUDINIHAIRDEFORMAPI_H
#define USDHOUDINI_GENERATED_HOUDINIHAIRDEFORMAPI_H

/// \file usdHoudini/houdiniHairDeformAPI.h

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
// HOUDINIHAIRDEFORMAPI                                                       //
// -------------------------------------------------------------------------- //

/// \class UsdHoudiniHoudiniHairDeformAPI
///
/// API for deforming hair curves.
///
class
USDHOUDINI_API
UsdHoudiniHoudiniHairDeformAPI : public UsdAPISchemaBase
{
public:
    /// Compile time constant representing what kind of schema this class is.
    ///
    /// \sa UsdSchemaKind
    static const UsdSchemaKind schemaKind = UsdSchemaKind::SingleApplyAPI;

    /// Construct a UsdHoudiniHoudiniHairDeformAPI on UsdPrim \p prim .
    /// Equivalent to UsdHoudiniHoudiniHairDeformAPI::Get(prim.GetStage(), prim.GetPath())
    /// for a \em valid \p prim, but will not immediately throw an error for
    /// an invalid \p prim
    explicit UsdHoudiniHoudiniHairDeformAPI(const UsdPrim& prim=UsdPrim())
        : UsdAPISchemaBase(prim)
    {
    }

    /// Construct a UsdHoudiniHoudiniHairDeformAPI on the prim held by \p schemaObj .
    /// Should be preferred over UsdHoudiniHoudiniHairDeformAPI(schemaObj.GetPrim()),
    /// as it preserves SchemaBase state.
    explicit UsdHoudiniHoudiniHairDeformAPI(const UsdSchemaBase& schemaObj)
        : UsdAPISchemaBase(schemaObj)
    {
    }

    /// Destructor.
    virtual ~UsdHoudiniHoudiniHairDeformAPI() override;

    /// Return a vector of names of all pre-declared attributes for this schema
    /// class and all its ancestor classes.  Does not include attributes that
    /// may be authored by custom/extended methods of the schemas involved.
        static const TfTokenVector &
    GetSchemaAttributeNames(bool includeInherited=true);

    /// Return a UsdHoudiniHoudiniHairDeformAPI holding the prim adhering to this
    /// schema at \p path on \p stage.  If no prim exists at \p path on
    /// \p stage, or if the prim at that path does not adhere to this schema,
    /// return an invalid schema object.  This is shorthand for the following:
    ///
    /// \code
    /// UsdHoudiniHoudiniHairDeformAPI(stage->GetPrimAtPath(path));
    /// \endcode
    ///
        static UsdHoudiniHoudiniHairDeformAPI
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
    /// This information is stored by adding "HoudiniHairDeformAPI" to the 
    /// token-valued, listOp metadata \em apiSchemas on the prim.
    /// 
    /// \return A valid UsdHoudiniHoudiniHairDeformAPI object is returned upon success. 
    /// An invalid (or empty) UsdHoudiniHoudiniHairDeformAPI object is returned upon 
    /// failure. See \ref UsdPrim::ApplyAPI() for conditions 
    /// resulting in failure. 
    /// 
    /// \sa UsdPrim::GetAppliedSchemas()
    /// \sa UsdPrim::HasAPI()
    /// \sa UsdPrim::CanApplyAPI()
    /// \sa UsdPrim::ApplyAPI()
    /// \sa UsdPrim::RemoveAPI()
    ///
        static UsdHoudiniHoudiniHairDeformAPI 
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
    // PRESERVESHAPEENABLE 
    // --------------------------------------------------------------------- //
    /// Enable preserve shape physics solve which targets the hair's original shape, while keeping deformation.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform bool houdini:hairdeform:preserveshapeenable = 0` |
    /// | C++ Type | bool |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Bool |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
        UsdAttribute GetPreserveShapeEnableAttr() const;

    /// See GetPreserveShapeEnableAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
        UsdAttribute CreatePreserveShapeEnableAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // PRESERVESHAPEITERATIONS 
    // --------------------------------------------------------------------- //
    /// Number of preserve shape physics solve iterations.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform int houdini:hairdeform:preserveshapeiterations = 10` |
    /// | C++ Type | int |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Int |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
        UsdAttribute GetPreserveShapeIterationsAttr() const;

    /// See GetPreserveShapeIterationsAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
        UsdAttribute CreatePreserveShapeIterationsAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // PRESERVESHAPELOCKROOTS 
    // --------------------------------------------------------------------- //
    /// Keep curve root points fixed during preserve shape solve.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform bool houdini:hairdeform:preserveshapelockroots = 1` |
    /// | C++ Type | bool |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Bool |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
        UsdAttribute GetPreserveShapeLockRootsAttr() const;

    /// See GetPreserveShapeLockRootsAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
        UsdAttribute CreatePreserveShapeLockRootsAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // PRESERVESHAPEKSTRETCH 
    // --------------------------------------------------------------------- //
    /// Stretch stiffness for preserve shape solve (Cosserat model).
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform float houdini:hairdeform:preserveshapekstretch = 0.01` |
    /// | C++ Type | float |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Float |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
        UsdAttribute GetPreserveShapeKStretchAttr() const;

    /// See GetPreserveShapeKStretchAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
        UsdAttribute CreatePreserveShapeKStretchAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // PRESERVESHAPEKBEND 
    // --------------------------------------------------------------------- //
    /// Bend stiffness for preserve shape solve (Cosserat model).
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform float houdini:hairdeform:preserveshapekbend = 0.001` |
    /// | C++ Type | float |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Float |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
        UsdAttribute GetPreserveShapeKBendAttr() const;

    /// See GetPreserveShapeKBendAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
        UsdAttribute CreatePreserveShapeKBendAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // PRESERVESHAPEREFPOSSTRENGTH 
    // --------------------------------------------------------------------- //
    /// Strength of reference position constraint in preserve shape solve.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform float houdini:hairdeform:preserveshaperefposstrength = 0` |
    /// | C++ Type | float |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Float |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
        UsdAttribute GetPreserveShapeRefPosStrengthAttr() const;

    /// See GetPreserveShapeRefPosStrengthAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
        UsdAttribute CreatePreserveShapeRefPosStrengthAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // PRESERVECLUMPSSTIFFNESS 
    // --------------------------------------------------------------------- //
    /// Clump stiffness for volume preservation in preserve shape solve.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform float houdini:hairdeform:preserveclumpsstiffness = 0` |
    /// | C++ Type | float |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Float |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
        UsdAttribute GetPreserveClumpsStiffnessAttr() const;

    /// See GetPreserveClumpsStiffnessAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
        UsdAttribute CreatePreserveClumpsStiffnessAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // PRESERVECLUMPSDAMPING 
    // --------------------------------------------------------------------- //
    /// Damping for clump forces in preserve shape solve.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform float houdini:hairdeform:preserveclumpsdamping = 1` |
    /// | C++ Type | float |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Float |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
        UsdAttribute GetPreserveClumpsDampingAttr() const;

    /// See GetPreserveClumpsDampingAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
        UsdAttribute CreatePreserveClumpsDampingAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // PRESERVECLUMPSENABLE 
    // --------------------------------------------------------------------- //
    /// Enable clump constraints during the solve.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform bool houdini:hairdeform:preserveclumpsenable = 0` |
    /// | C++ Type | bool |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Bool |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
        UsdAttribute GetPreserveClumpsEnableAttr() const;

    /// See GetPreserveClumpsEnableAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
        UsdAttribute CreatePreserveClumpsEnableAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // PRESERVECLUMPSMAXNEIGHBORS 
    // --------------------------------------------------------------------- //
    /// Maximum number of candidate neighbors to search per point for clumping.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform int houdini:hairdeform:preserveclumpsmaxneighbors = 50` |
    /// | C++ Type | int |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Int |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
        UsdAttribute GetPreserveClumpsMaxNeighborsAttr() const;

    /// See GetPreserveClumpsMaxNeighborsAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
        UsdAttribute CreatePreserveClumpsMaxNeighborsAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // PRESERVECLUMPSMAXCONSTRAINTS 
    // --------------------------------------------------------------------- //
    /// Maximum number of cross-curve clump constraints to keep per point.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform int houdini:hairdeform:preserveclumpsmaxconstraints = 3` |
    /// | C++ Type | int |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Int |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
        UsdAttribute GetPreserveClumpsMaxConstraintsAttr() const;

    /// See GetPreserveClumpsMaxConstraintsAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
        UsdAttribute CreatePreserveClumpsMaxConstraintsAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // CAPTURERADIUS 
    // --------------------------------------------------------------------- //
    /// Search radius for point capture when capture attributes are not present.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform float houdini:hairdeform:captureradius = 1` |
    /// | C++ Type | float |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Float |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
        UsdAttribute GetCaptureRadiusAttr() const;

    /// See GetCaptureRadiusAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
        UsdAttribute CreateCaptureRadiusAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // CAPTUREMAXPOINTS 
    // --------------------------------------------------------------------- //
    /// Maximum number of deformer points to capture per groom point.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform int houdini:hairdeform:capturemaxpoints = 10` |
    /// | C++ Type | int |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Int |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
        UsdAttribute GetCaptureMaxPointsAttr() const;

    /// See GetCaptureMaxPointsAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
        UsdAttribute CreateCaptureMaxPointsAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // CAPTUREMINPOINTS 
    // --------------------------------------------------------------------- //
    /// Minimum number of deformer points to capture per groom point (radius will expand if needed).
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform int houdini:hairdeform:captureminpoints = 1` |
    /// | C++ Type | int |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Int |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
        UsdAttribute GetCaptureMinPointsAttr() const;

    /// See GetCaptureMinPointsAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
        UsdAttribute CreateCaptureMinPointsAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // DEFORMMETHOD 
    // --------------------------------------------------------------------- //
    /// Method to use for the basic deformation. Currently either 'pointdeform' or 'surfacedeform'.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform string houdini:hairdeform:deformmethod = ""` |
    /// | C++ Type | std::string |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->String |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
        UsdAttribute GetDeformMethodAttr() const;

    /// See GetDeformMethodAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
        UsdAttribute CreateDeformMethodAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // SMOOTHCAPTURE 
    // --------------------------------------------------------------------- //
    /// Enable smooth RBF-based capture instead of BVH point capture.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform bool houdini:hairdeform:smoothcapture = 0` |
    /// | C++ Type | bool |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Bool |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
        UsdAttribute GetSmoothCaptureAttr() const;

    /// See GetSmoothCaptureAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
        UsdAttribute CreateSmoothCaptureAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // SMOOTHCAPTURERADIUS 
    // --------------------------------------------------------------------- //
    /// Capture radius for smooth RBF capture method.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform float houdini:hairdeform:smoothcaptureradius = 0` |
    /// | C++ Type | float |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Float |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
        UsdAttribute GetSmoothCaptureRadiusAttr() const;

    /// See GetSmoothCaptureRadiusAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
        UsdAttribute CreateSmoothCaptureRadiusAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // KERNELTYPE 
    // --------------------------------------------------------------------- //
    /// Kernel type for smooth capture weight falloff.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform string houdini:hairdeform:kerneltype = "exponentialbump"` |
    /// | C++ Type | std::string |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->String |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    /// | \ref UsdHoudiniTokens "Allowed Values" | exponentialbump, truncatedgaussian, quadratic, linear |
        UsdAttribute GetKernelTypeAttr() const;

    /// See GetKernelTypeAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
        UsdAttribute CreateKernelTypeAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // SMOOTHINGMETHOD 
    // --------------------------------------------------------------------- //
    /// Smoothing method for smooth capture subdivision.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform string houdini:hairdeform:smoothingmethod = "approximating"` |
    /// | C++ Type | std::string |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->String |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    /// | \ref UsdHoudiniTokens "Allowed Values" | approximating, interpolating, none |
        UsdAttribute GetSmoothingMethodAttr() const;

    /// See GetSmoothingMethodAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
        UsdAttribute CreateSmoothingMethodAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // SMOOTHINGLEVEL 
    // --------------------------------------------------------------------- //
    /// Number of smoothing subdivisions for smooth capture.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform int houdini:hairdeform:smoothinglevel = 1` |
    /// | C++ Type | int |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Int |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
        UsdAttribute GetSmoothingLevelAttr() const;

    /// See GetSmoothingLevelAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
        UsdAttribute CreateSmoothingLevelAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // TETMESHTREATMENT 
    // --------------------------------------------------------------------- //
    /// How to treat tet meshes in smooth capture geometry.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform string houdini:hairdeform:tetmeshtreatment = "as_surface"` |
    /// | C++ Type | std::string |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->String |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    /// | \ref UsdHoudiniTokens "Allowed Values" | as_surface, as_solid, none |
        UsdAttribute GetTetMeshTreatmentAttr() const;

    /// See GetTetMeshTreatmentAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
        UsdAttribute CreateTetMeshTreatmentAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // USEORIENTATTRIB 
    // --------------------------------------------------------------------- //
    /// Use orient/restorient quaternion attributes on the deformer
    /// instead of computing transforms from neighbour topology.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform bool houdini:hairdeform:useorientattrib = 0` |
    /// | C++ Type | bool |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Bool |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
        UsdAttribute GetUseOrientAttribAttr() const;

    /// See GetUseOrientAttribAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
        UsdAttribute CreateUseOrientAttribAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // ORIENTBLEND 
    // --------------------------------------------------------------------- //
    /// Blend between position-only guide deformation and
    /// orient-augmented guide deformation for BasisCurves point deform.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform float houdini:hairdeform:orientblend = 1` |
    /// | C++ Type | float |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Float |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
        UsdAttribute GetOrientBlendAttr() const;

    /// See GetOrientBlendAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
        UsdAttribute CreateOrientBlendAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // GSIMAXCANDIDATES 
    // --------------------------------------------------------------------- //
    /// Guide Shape Interpolation: maximum number of candidate guide
    /// roots to consider in the spatial search.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform int houdini:hairdeform:gsimaxcandidates = 32` |
    /// | C++ Type | int |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Int |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
        UsdAttribute GetGsiMaxCandidatesAttr() const;

    /// See GetGsiMaxCandidatesAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
        UsdAttribute CreateGsiMaxCandidatesAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // GSISEARCHRADIUS 
    // --------------------------------------------------------------------- //
    /// Guide Shape Interpolation: spatial search radius around each
    /// groom curve root.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform float houdini:hairdeform:gsisearchradius = 0.2` |
    /// | C++ Type | float |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Float |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
        UsdAttribute GetGsiSearchRadiusAttr() const;

    /// See GetGsiSearchRadiusAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
        UsdAttribute CreateGsiSearchRadiusAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // GSINSAMPLES 
    // --------------------------------------------------------------------- //
    /// Guide Shape Interpolation: number of arc-length samples used
    /// to compute shape distance between curves.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform int houdini:hairdeform:gsinsamples = 20` |
    /// | C++ Type | int |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Int |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
        UsdAttribute GetGsiNSamplesAttr() const;

    /// See GetGsiNSamplesAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
        UsdAttribute CreateGsiNSamplesAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // GSIMINGUIDES 
    // --------------------------------------------------------------------- //
    /// Guide Shape Interpolation: minimum number of guide influences
    /// kept per groom curve regardless of weight threshold.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform int houdini:hairdeform:gsiminguides = 1` |
    /// | C++ Type | int |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Int |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
        UsdAttribute GetGsiMinGuidesAttr() const;

    /// See GetGsiMinGuidesAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
        UsdAttribute CreateGsiMinGuidesAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // GSIMAXGUIDES 
    // --------------------------------------------------------------------- //
    /// Guide Shape Interpolation: maximum number of guide influences
    /// per groom curve.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform int houdini:hairdeform:gsimaxguides = 8` |
    /// | C++ Type | int |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Int |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
        UsdAttribute GetGsiMaxGuidesAttr() const;

    /// See GetGsiMaxGuidesAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
        UsdAttribute CreateGsiMaxGuidesAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // GSISIGMASCALE 
    // --------------------------------------------------------------------- //
    /// Guide Shape Interpolation: Gaussian sigma scale factor applied
    /// to the median shape distance.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform float houdini:hairdeform:gsisigmascale = 0.1` |
    /// | C++ Type | float |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Float |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
        UsdAttribute GetGsiSigmaScaleAttr() const;

    /// See GetGsiSigmaScaleAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
        UsdAttribute CreateGsiSigmaScaleAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // GSIWEIGHTTHRESHOLD 
    // --------------------------------------------------------------------- //
    /// Guide Shape Interpolation: minimum normalised weight below
    /// which a guide influence is discarded.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform float houdini:hairdeform:gsiweightthreshold = 0.1` |
    /// | C++ Type | float |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Float |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
        UsdAttribute GetGsiWeightThresholdAttr() const;

    /// See GetGsiWeightThresholdAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
        UsdAttribute CreateGsiWeightThresholdAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // GSILENGTHPENALTYSCALE 
    // --------------------------------------------------------------------- //
    /// Guide Shape Interpolation: scale applied to the squared
    /// relative length difference between groom and guide curves.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform float houdini:hairdeform:gsilengthpenaltyscale = 0` |
    /// | C++ Type | float |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Float |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
        UsdAttribute GetGsiLengthPenaltyScaleAttr() const;

    /// See GetGsiLengthPenaltyScaleAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
        UsdAttribute CreateGsiLengthPenaltyScaleAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // SKINPRIM 
    // --------------------------------------------------------------------- //
    /// Hair roots will stick to this mesh.
    ///
        UsdRelationship GetSkinPrimRel() const;

    /// See GetSkinPrimRel(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create
        UsdRelationship CreateSkinPrimRel() const;

public:
    // --------------------------------------------------------------------- //
    // DEFORMERPRIM 
    // --------------------------------------------------------------------- //
    /// Deformer curves or guides used for point deform and guide interpolation mesh methods.
    ///
        UsdRelationship GetDeformerPrimRel() const;

    /// See GetDeformerPrimRel(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create
        UsdRelationship CreateDeformerPrimRel() const;

public:
    // --------------------------------------------------------------------- //
    // GUIDEINTERPMESHPRIM 
    // --------------------------------------------------------------------- //
    /// Guide interpolation mesh with 'guides' and 'weights' array attributes for guide interpolation mesh deformation method.
    ///
        UsdRelationship GetGuideInterpMeshPrimRel() const;

    /// See GetGuideInterpMeshPrimRel(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create
        UsdRelationship CreateGuideInterpMeshPrimRel() const;

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
