//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef USDHOUDINI_GENERATED_HOUDINIAPEXSHAPEBINDINGAPI_H
#define USDHOUDINI_GENERATED_HOUDINIAPEXSHAPEBINDINGAPI_H

/// \file usdHoudini/houdiniApexShapeBindingAPI.h

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
// HOUDINIAPEXSHAPEBINDINGAPI                                                 //
// -------------------------------------------------------------------------- //

/// \class UsdHoudiniHoudiniApexShapeBindingAPI
///
/// Houdini API schema for binding a USD primitive to a geometry input
/// and/or output of an APEX character's rig. This is intended for use with
/// the HoudiniApexCharacterAPI schema.
/// 
///
class
USDHOUDINI_API
UsdHoudiniHoudiniApexShapeBindingAPI : public UsdAPISchemaBase
{
public:
    /// Compile time constant representing what kind of schema this class is.
    ///
    /// \sa UsdSchemaKind
    static const UsdSchemaKind schemaKind = UsdSchemaKind::MultipleApplyAPI;

    /// Construct a UsdHoudiniHoudiniApexShapeBindingAPI on UsdPrim \p prim with
    /// name \p name . Equivalent to
    /// UsdHoudiniHoudiniApexShapeBindingAPI::Get(
    ///    prim.GetStage(),
    ///    prim.GetPath().AppendProperty(
    ///        "houdini:apex:shape:name"));
    ///
    /// for a \em valid \p prim, but will not immediately throw an error for
    /// an invalid \p prim
    explicit UsdHoudiniHoudiniApexShapeBindingAPI(
        const UsdPrim& prim=UsdPrim(), const TfToken &name=TfToken())
        : UsdAPISchemaBase(prim, /*instanceName*/ name)
    { }

    /// Construct a UsdHoudiniHoudiniApexShapeBindingAPI on the prim held by \p schemaObj with
    /// name \p name.  Should be preferred over
    /// UsdHoudiniHoudiniApexShapeBindingAPI(schemaObj.GetPrim(), name), as it preserves
    /// SchemaBase state.
    explicit UsdHoudiniHoudiniApexShapeBindingAPI(
        const UsdSchemaBase& schemaObj, const TfToken &name)
        : UsdAPISchemaBase(schemaObj, /*instanceName*/ name)
    { }

    /// Destructor.
    virtual ~UsdHoudiniHoudiniApexShapeBindingAPI() override;

    /// Return a vector of names of all pre-declared attributes for this schema
    /// class and all its ancestor classes.  Does not include attributes that
    /// may be authored by custom/extended methods of the schemas involved.
        static const TfTokenVector &
    GetSchemaAttributeNames(bool includeInherited=true);

    /// Return a vector of names of all pre-declared attributes for this schema
    /// class and all its ancestor classes for a given instance name.  Does not
    /// include attributes that may be authored by custom/extended methods of
    /// the schemas involved. The names returned will have the proper namespace
    /// prefix.
        static TfTokenVector
    GetSchemaAttributeNames(bool includeInherited, const TfToken &instanceName);

    /// Returns the name of this multiple-apply schema instance
    TfToken GetName() const {
        return _GetInstanceName();
    }

    /// Return a UsdHoudiniHoudiniApexShapeBindingAPI holding the prim adhering to this
    /// schema at \p path on \p stage.  If no prim exists at \p path on
    /// \p stage, or if the prim at that path does not adhere to this schema,
    /// return an invalid schema object.  \p path must be of the format
    /// <path>.houdini:apex:shape:name .
    ///
    /// This is shorthand for the following:
    ///
    /// \code
    /// TfToken name = SdfPath::StripNamespace(path.GetToken());
    /// UsdHoudiniHoudiniApexShapeBindingAPI(
    ///     stage->GetPrimAtPath(path.GetPrimPath()), name);
    /// \endcode
    ///
        static UsdHoudiniHoudiniApexShapeBindingAPI
    Get(const UsdStagePtr &stage, const SdfPath &path);

    /// Return a UsdHoudiniHoudiniApexShapeBindingAPI with name \p name holding the
    /// prim \p prim. Shorthand for UsdHoudiniHoudiniApexShapeBindingAPI(prim, name);
        static UsdHoudiniHoudiniApexShapeBindingAPI
    Get(const UsdPrim &prim, const TfToken &name);

    /// Return a vector of all named instances of UsdHoudiniHoudiniApexShapeBindingAPI on the 
    /// given \p prim.
        static std::vector<UsdHoudiniHoudiniApexShapeBindingAPI>
    GetAll(const UsdPrim &prim);

    /// Checks if the given name \p baseName is the base name of a property
    /// of HoudiniApexShapeBindingAPI.
        static bool
    IsSchemaPropertyBaseName(const TfToken &baseName);

    /// Checks if the given path \p path is of an API schema of type
    /// HoudiniApexShapeBindingAPI. If so, it stores the instance name of
    /// the schema in \p name and returns true. Otherwise, it returns false.
        static bool
    IsHoudiniApexShapeBindingAPIPath(const SdfPath &path, TfToken *name);

    /// Returns true if this <b>multiple-apply</b> API schema can be applied,
    /// with the given instance name, \p name, to the given \p prim. If this 
    /// schema can not be a applied the prim, this returns false and, if 
    /// provided, populates \p whyNot with the reason it can not be applied.
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
    CanApply(const UsdPrim &prim, const TfToken &name, 
             std::string *whyNot=nullptr);

    /// Applies this <b>multiple-apply</b> API schema to the given \p prim 
    /// along with the given instance name, \p name. 
    /// 
    /// This information is stored by adding "HoudiniApexShapeBindingAPI:<i>name</i>" 
    /// to the token-valued, listOp metadata \em apiSchemas on the prim.
    /// For example, if \p name is 'instance1', the token 
    /// 'HoudiniApexShapeBindingAPI:instance1' is added to 'apiSchemas'.
    /// 
    /// \return A valid UsdHoudiniHoudiniApexShapeBindingAPI object is returned upon success. 
    /// An invalid (or empty) UsdHoudiniHoudiniApexShapeBindingAPI object is returned upon 
    /// failure. See \ref UsdPrim::ApplyAPI() for 
    /// conditions resulting in failure. 
    /// 
    /// \sa UsdPrim::GetAppliedSchemas()
    /// \sa UsdPrim::HasAPI()
    /// \sa UsdPrim::CanApplyAPI()
    /// \sa UsdPrim::ApplyAPI()
    /// \sa UsdPrim::RemoveAPI()
    ///
        static UsdHoudiniHoudiniApexShapeBindingAPI 
    Apply(const UsdPrim &prim, const TfToken &name);

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
    // INPUT 
    // --------------------------------------------------------------------- //
    /// Optional name of a rig input to bind the primitive to.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform string input = ""` |
    /// | C++ Type | std::string |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->String |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
        UsdAttribute GetInputAttr() const;

    /// See GetInputAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
        UsdAttribute CreateInputAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // OUTPUT 
    // --------------------------------------------------------------------- //
    /// Optional name of a rig output which deforms the primitive.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform string output = ""` |
    /// | C++ Type | std::string |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->String |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
        UsdAttribute GetOutputAttr() const;

    /// See GetOutputAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
        UsdAttribute CreateOutputAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // BINDING 
    // --------------------------------------------------------------------- //
    /// Relationship to a primitive that will be bound to an input or
    /// output of the character's rig. 
    /// 
    ///
        UsdRelationship GetBindingRel() const;

    /// See GetBindingRel(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create
        UsdRelationship CreateBindingRel() const;

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
