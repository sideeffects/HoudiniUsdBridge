//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef USDHOUDINI_GENERATED_HOUDINIAPEXXFORMBINDINGAPI_H
#define USDHOUDINI_GENERATED_HOUDINIAPEXXFORMBINDINGAPI_H

/// \file usdHoudini/houdiniApexXformBindingAPI.h

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
// HOUDINIAPEXXFORMBINDINGAPI                                                 //
// -------------------------------------------------------------------------- //

/// \class UsdHoudiniHoudiniApexXformBindingAPI
///
/// Houdini API schema for binding a USD primitive's transform to a
/// skeleton joint from an APEX character. This is intended for use with the
/// HoudiniApexCharacterAPI schema.
/// 
///
class
USDHOUDINI_API
UsdHoudiniHoudiniApexXformBindingAPI : public UsdAPISchemaBase
{
public:
    /// Compile time constant representing what kind of schema this class is.
    ///
    /// \sa UsdSchemaKind
    static const UsdSchemaKind schemaKind = UsdSchemaKind::MultipleApplyAPI;

    /// Construct a UsdHoudiniHoudiniApexXformBindingAPI on UsdPrim \p prim with
    /// name \p name . Equivalent to
    /// UsdHoudiniHoudiniApexXformBindingAPI::Get(
    ///    prim.GetStage(),
    ///    prim.GetPath().AppendProperty(
    ///        "houdini:apex:xform:name"));
    ///
    /// for a \em valid \p prim, but will not immediately throw an error for
    /// an invalid \p prim
    explicit UsdHoudiniHoudiniApexXformBindingAPI(
        const UsdPrim& prim=UsdPrim(), const TfToken &name=TfToken())
        : UsdAPISchemaBase(prim, /*instanceName*/ name)
    { }

    /// Construct a UsdHoudiniHoudiniApexXformBindingAPI on the prim held by \p schemaObj with
    /// name \p name.  Should be preferred over
    /// UsdHoudiniHoudiniApexXformBindingAPI(schemaObj.GetPrim(), name), as it preserves
    /// SchemaBase state.
    explicit UsdHoudiniHoudiniApexXformBindingAPI(
        const UsdSchemaBase& schemaObj, const TfToken &name)
        : UsdAPISchemaBase(schemaObj, /*instanceName*/ name)
    { }

    /// Destructor.
    virtual ~UsdHoudiniHoudiniApexXformBindingAPI() override;

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

    /// Return a UsdHoudiniHoudiniApexXformBindingAPI holding the prim adhering to this
    /// schema at \p path on \p stage.  If no prim exists at \p path on
    /// \p stage, or if the prim at that path does not adhere to this schema,
    /// return an invalid schema object.  \p path must be of the format
    /// <path>.houdini:apex:xform:name .
    ///
    /// This is shorthand for the following:
    ///
    /// \code
    /// TfToken name = SdfPath::StripNamespace(path.GetToken());
    /// UsdHoudiniHoudiniApexXformBindingAPI(
    ///     stage->GetPrimAtPath(path.GetPrimPath()), name);
    /// \endcode
    ///
        static UsdHoudiniHoudiniApexXformBindingAPI
    Get(const UsdStagePtr &stage, const SdfPath &path);

    /// Return a UsdHoudiniHoudiniApexXformBindingAPI with name \p name holding the
    /// prim \p prim. Shorthand for UsdHoudiniHoudiniApexXformBindingAPI(prim, name);
        static UsdHoudiniHoudiniApexXformBindingAPI
    Get(const UsdPrim &prim, const TfToken &name);

    /// Return a vector of all named instances of UsdHoudiniHoudiniApexXformBindingAPI on the 
    /// given \p prim.
        static std::vector<UsdHoudiniHoudiniApexXformBindingAPI>
    GetAll(const UsdPrim &prim);

    /// Checks if the given name \p baseName is the base name of a property
    /// of HoudiniApexXformBindingAPI.
        static bool
    IsSchemaPropertyBaseName(const TfToken &baseName);

    /// Checks if the given path \p path is of an API schema of type
    /// HoudiniApexXformBindingAPI. If so, it stores the instance name of
    /// the schema in \p name and returns true. Otherwise, it returns false.
        static bool
    IsHoudiniApexXformBindingAPIPath(const SdfPath &path, TfToken *name);

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
    /// This information is stored by adding "HoudiniApexXformBindingAPI:<i>name</i>" 
    /// to the token-valued, listOp metadata \em apiSchemas on the prim.
    /// For example, if \p name is 'instance1', the token 
    /// 'HoudiniApexXformBindingAPI:instance1' is added to 'apiSchemas'.
    /// 
    /// \return A valid UsdHoudiniHoudiniApexXformBindingAPI object is returned upon success. 
    /// An invalid (or empty) UsdHoudiniHoudiniApexXformBindingAPI object is returned upon 
    /// failure. See \ref UsdPrim::ApplyAPI() for 
    /// conditions resulting in failure. 
    /// 
    /// \sa UsdPrim::GetAppliedSchemas()
    /// \sa UsdPrim::HasAPI()
    /// \sa UsdPrim::CanApplyAPI()
    /// \sa UsdPrim::ApplyAPI()
    /// \sa UsdPrim::RemoveAPI()
    ///
        static UsdHoudiniHoudiniApexXformBindingAPI 
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
    // OUTPUT 
    // --------------------------------------------------------------------- //
    /// Name of the rig output which contains the skeleton.
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
    // JOINT 
    // --------------------------------------------------------------------- //
    /// Name of the joint which the prim's transform is bound to.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform string joint = ""` |
    /// | C++ Type | std::string |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->String |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
        UsdAttribute GetJointAttr() const;

    /// See GetJointAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
        UsdAttribute CreateJointAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // BINDING 
    // --------------------------------------------------------------------- //
    /// Relationship to a primitive whose transform will be bound to a joint.
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
