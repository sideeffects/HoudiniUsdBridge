//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef USDHOUDINI_GENERATED_HOUDINIAPEXCHARACTERBINDINGAPI_H
#define USDHOUDINI_GENERATED_HOUDINIAPEXCHARACTERBINDINGAPI_H

/// \file usdHoudini/houdiniApexCharacterBindingAPI.h

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
// HOUDINIAPEXCHARACTERBINDINGAPI                                             //
// -------------------------------------------------------------------------- //

/// \class UsdHoudiniHoudiniApexCharacterBindingAPI
///
/// Houdini API schema for adding a character to an APEX animation
/// scene. This is intended for use with HoudiniApexScene prims.
/// 
/// The schema's instance name (e.g. 'electra') is used as the character's
/// name in the APEX scene (e.g. '/electra.char')
/// 
///
class
USDHOUDINI_API
UsdHoudiniHoudiniApexCharacterBindingAPI : public UsdAPISchemaBase
{
public:
    /// Compile time constant representing what kind of schema this class is.
    ///
    /// \sa UsdSchemaKind
    static const UsdSchemaKind schemaKind = UsdSchemaKind::MultipleApplyAPI;

    /// Construct a UsdHoudiniHoudiniApexCharacterBindingAPI on UsdPrim \p prim with
    /// name \p name . Equivalent to
    /// UsdHoudiniHoudiniApexCharacterBindingAPI::Get(
    ///    prim.GetStage(),
    ///    prim.GetPath().AppendProperty(
    ///        "character:name"));
    ///
    /// for a \em valid \p prim, but will not immediately throw an error for
    /// an invalid \p prim
    explicit UsdHoudiniHoudiniApexCharacterBindingAPI(
        const UsdPrim& prim=UsdPrim(), const TfToken &name=TfToken())
        : UsdAPISchemaBase(prim, /*instanceName*/ name)
    { }

    /// Construct a UsdHoudiniHoudiniApexCharacterBindingAPI on the prim held by \p schemaObj with
    /// name \p name.  Should be preferred over
    /// UsdHoudiniHoudiniApexCharacterBindingAPI(schemaObj.GetPrim(), name), as it preserves
    /// SchemaBase state.
    explicit UsdHoudiniHoudiniApexCharacterBindingAPI(
        const UsdSchemaBase& schemaObj, const TfToken &name)
        : UsdAPISchemaBase(schemaObj, /*instanceName*/ name)
    { }

    /// Destructor.
    virtual ~UsdHoudiniHoudiniApexCharacterBindingAPI() override;

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

    /// Return a UsdHoudiniHoudiniApexCharacterBindingAPI holding the prim adhering to this
    /// schema at \p path on \p stage.  If no prim exists at \p path on
    /// \p stage, or if the prim at that path does not adhere to this schema,
    /// return an invalid schema object.  \p path must be of the format
    /// <path>.character:name .
    ///
    /// This is shorthand for the following:
    ///
    /// \code
    /// TfToken name = SdfPath::StripNamespace(path.GetToken());
    /// UsdHoudiniHoudiniApexCharacterBindingAPI(
    ///     stage->GetPrimAtPath(path.GetPrimPath()), name);
    /// \endcode
    ///
        static UsdHoudiniHoudiniApexCharacterBindingAPI
    Get(const UsdStagePtr &stage, const SdfPath &path);

    /// Return a UsdHoudiniHoudiniApexCharacterBindingAPI with name \p name holding the
    /// prim \p prim. Shorthand for UsdHoudiniHoudiniApexCharacterBindingAPI(prim, name);
        static UsdHoudiniHoudiniApexCharacterBindingAPI
    Get(const UsdPrim &prim, const TfToken &name);

    /// Return a vector of all named instances of UsdHoudiniHoudiniApexCharacterBindingAPI on the 
    /// given \p prim.
        static std::vector<UsdHoudiniHoudiniApexCharacterBindingAPI>
    GetAll(const UsdPrim &prim);

    /// Checks if the given name \p baseName is the base name of a property
    /// of HoudiniApexCharacterBindingAPI.
        static bool
    IsSchemaPropertyBaseName(const TfToken &baseName);

    /// Checks if the given path \p path is of an API schema of type
    /// HoudiniApexCharacterBindingAPI. If so, it stores the instance name of
    /// the schema in \p name and returns true. Otherwise, it returns false.
        static bool
    IsHoudiniApexCharacterBindingAPIPath(const SdfPath &path, TfToken *name);

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
    /// This information is stored by adding "HoudiniApexCharacterBindingAPI:<i>name</i>" 
    /// to the token-valued, listOp metadata \em apiSchemas on the prim.
    /// For example, if \p name is 'instance1', the token 
    /// 'HoudiniApexCharacterBindingAPI:instance1' is added to 'apiSchemas'.
    /// 
    /// \return A valid UsdHoudiniHoudiniApexCharacterBindingAPI object is returned upon success. 
    /// An invalid (or empty) UsdHoudiniHoudiniApexCharacterBindingAPI object is returned upon 
    /// failure. See \ref UsdPrim::ApplyAPI() for 
    /// conditions resulting in failure. 
    /// 
    /// \sa UsdPrim::GetAppliedSchemas()
    /// \sa UsdPrim::HasAPI()
    /// \sa UsdPrim::CanApplyAPI()
    /// \sa UsdPrim::ApplyAPI()
    /// \sa UsdPrim::RemoveAPI()
    ///
        static UsdHoudiniHoudiniApexCharacterBindingAPI 
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
    // RIG 
    // --------------------------------------------------------------------- //
    /// Specifies which rig to use for the character (for example,
    /// "Base.rig"). If authored, this value takes precedence over the
    /// 'houdini:apex:character:rig' attribute from HoudiniApexCharacterAPI.
    /// 
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform string rig = ""` |
    /// | C++ Type | std::string |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->String |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
        UsdAttribute GetRigAttr() const;

    /// See GetRigAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
        UsdAttribute CreateRigAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // BINDING 
    // --------------------------------------------------------------------- //
    /// Relationship specifying the character to bind to the scene (a
    /// primitive with the HoudiniApexCharacterAPI schema).
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
