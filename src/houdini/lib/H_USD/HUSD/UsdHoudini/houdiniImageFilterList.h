//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef USDHOUDINI_GENERATED_HOUDINIIMAGEFILTERLIST_H
#define USDHOUDINI_GENERATED_HOUDINIIMAGEFILTERLIST_H

/// \file usdHoudini/houdiniImageFilterList.h

#include "pxr/pxr.h"
#include "./api.h"
#include "pxr/usd/usd/typed.h"
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
// HOUDINIIMAGEFILTERLIST                                                     //
// -------------------------------------------------------------------------- //

/// \class UsdHoudiniHoudiniImageFilterList
///
/// Representation of a COP Filter List to be used for slap=comp
///
/// For any described attribute \em Fallback \em Value or \em Allowed \em Values below
/// that are text/tokens, the actual token is published and defined in \ref UsdHoudiniTokens.
/// So to set an attribute to the value "rightHanded", use UsdHoudiniTokens->rightHanded
/// as the value.
///
class
USDHOUDINI_API
UsdHoudiniHoudiniImageFilterList : public UsdTyped
{
public:
    /// Compile time constant representing what kind of schema this class is.
    ///
    /// \sa UsdSchemaKind
    static const UsdSchemaKind schemaKind = UsdSchemaKind::ConcreteTyped;

    /// Construct a UsdHoudiniHoudiniImageFilterList on UsdPrim \p prim .
    /// Equivalent to UsdHoudiniHoudiniImageFilterList::Get(prim.GetStage(), prim.GetPath())
    /// for a \em valid \p prim, but will not immediately throw an error for
    /// an invalid \p prim
    explicit UsdHoudiniHoudiniImageFilterList(const UsdPrim& prim=UsdPrim())
        : UsdTyped(prim)
    {
    }

    /// Construct a UsdHoudiniHoudiniImageFilterList on the prim held by \p schemaObj .
    /// Should be preferred over UsdHoudiniHoudiniImageFilterList(schemaObj.GetPrim()),
    /// as it preserves SchemaBase state.
    explicit UsdHoudiniHoudiniImageFilterList(const UsdSchemaBase& schemaObj)
        : UsdTyped(schemaObj)
    {
    }

    /// Destructor.
    virtual ~UsdHoudiniHoudiniImageFilterList() override;

    /// Return a vector of names of all pre-declared attributes for this schema
    /// class and all its ancestor classes.  Does not include attributes that
    /// may be authored by custom/extended methods of the schemas involved.
        static const TfTokenVector &
    GetSchemaAttributeNames(bool includeInherited=true);

    /// Return a UsdHoudiniHoudiniImageFilterList holding the prim adhering to this
    /// schema at \p path on \p stage.  If no prim exists at \p path on
    /// \p stage, or if the prim at that path does not adhere to this schema,
    /// return an invalid schema object.  This is shorthand for the following:
    ///
    /// \code
    /// UsdHoudiniHoudiniImageFilterList(stage->GetPrimAtPath(path));
    /// \endcode
    ///
        static UsdHoudiniHoudiniImageFilterList
    Get(const UsdStagePtr &stage, const SdfPath &path);

    /// Attempt to ensure a \a UsdPrim adhering to this schema at \p path
    /// is defined (according to UsdPrim::IsDefined()) on this stage.
    ///
    /// If a prim adhering to this schema at \p path is already defined on this
    /// stage, return that prim.  Otherwise author an \a SdfPrimSpec with
    /// \a specifier == \a SdfSpecifierDef and this schema's prim type name for
    /// the prim at \p path at the current EditTarget.  Author \a SdfPrimSpec s
    /// with \p specifier == \a SdfSpecifierDef and empty typeName at the
    /// current EditTarget for any nonexistent, or existing but not \a Defined
    /// ancestors.
    ///
    /// The given \a path must be an absolute prim path that does not contain
    /// any variant selections.
    ///
    /// If it is impossible to author any of the necessary PrimSpecs, (for
    /// example, in case \a path cannot map to the current UsdEditTarget's
    /// namespace) issue an error and return an invalid \a UsdPrim.
    ///
    /// Note that this method may return a defined prim whose typeName does not
    /// specify this schema class, in case a stronger typeName opinion overrides
    /// the opinion at the current EditTarget.
    ///
        static UsdHoudiniHoudiniImageFilterList
    Define(const UsdStagePtr &stage, const SdfPath &path);

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
    // AOVS 
    // --------------------------------------------------------------------- //
    /// This filter should be applied to these AOVs.  The token should
    /// be one of "color" (all color RenderVars), "all" (all AOVs),
    /// "none" (disable this filter), or "list" (a specific list of AOVs
    /// specified by the AOVlist).
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token AOVs = "color"` |
    /// | C++ Type | TfToken |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Token |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    /// | \ref UsdHoudiniTokens "Allowed Values" | color, all, none, list |
        UsdAttribute GetAOVsAttr() const;

    /// See GetAOVsAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
        UsdAttribute CreateAOVsAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // AOVLIST 
    // --------------------------------------------------------------------- //
    /// The filter should run only on this list of AOVs.  The list of
    /// strings is only used if the "AOVs" token is set to "list".
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform string[] AOVlist = [""]` |
    /// | C++ Type | VtArray<std::string> |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->StringArray |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
        UsdAttribute GetAOVlistAttr() const;

    /// See GetAOVlistAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
        UsdAttribute CreateAOVlistAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // NORMALSCOPE 
    // --------------------------------------------------------------------- //
    /// The name associated with the AOV storing normals
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform string normalscope = "N"` |
    /// | C++ Type | std::string |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->String |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
        UsdAttribute GetNormalscopeAttr() const;

    /// See GetNormalscopeAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
        UsdAttribute CreateNormalscopeAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // DEPTHSCOPE 
    // --------------------------------------------------------------------- //
    /// The name associated with the AOV storing depth information
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform string depthscope = "depth"` |
    /// | C++ Type | std::string |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->String |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
        UsdAttribute GetDepthscopeAttr() const;

    /// See GetDepthscopeAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
        UsdAttribute CreateDepthscopeAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // ALBEDOSCOPE 
    // --------------------------------------------------------------------- //
    /// The name associated with the AOV storing albedo information
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform string albedoscope = "albedo"` |
    /// | C++ Type | std::string |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->String |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
        UsdAttribute GetAlbedoscopeAttr() const;

    /// See GetAlbedoscopeAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
        UsdAttribute CreateAlbedoscopeAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // ORDEREDFILTERS 
    // --------------------------------------------------------------------- //
    /// A list of HoudiniImageFilter objects that are run in order
    ///
        UsdRelationship GetOrderedFiltersRel() const;

    /// See GetOrderedFiltersRel(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create
        UsdRelationship CreateOrderedFiltersRel() const;

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
