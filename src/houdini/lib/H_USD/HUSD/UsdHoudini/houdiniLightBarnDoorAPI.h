//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef USDHOUDINI_GENERATED_HOUDINILIGHTBARNDOORAPI_H
#define USDHOUDINI_GENERATED_HOUDINILIGHTBARNDOORAPI_H

/// \file usdHoudini/houdiniLightBarnDoorAPI.h

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
// HOUDINILIGHTBARNDOORAPI                                                    //
// -------------------------------------------------------------------------- //

/// \class UsdHoudiniHoudiniLightBarnDoorAPI
///
/// Houdini API schema for describing barn door attributes on lights.
/// This should not be avoided, if possible, in favour of using Light Filters
/// instead. This schema (and the associated attributes) will be deprecated and
/// removed from Solaris in a later release of Houdini.
///
class
USDHOUDINI_API
UsdHoudiniHoudiniLightBarnDoorAPI : public UsdAPISchemaBase
{
public:
    /// Compile time constant representing what kind of schema this class is.
    ///
    /// \sa UsdSchemaKind
    static const UsdSchemaKind schemaKind = UsdSchemaKind::SingleApplyAPI;

    /// Construct a UsdHoudiniHoudiniLightBarnDoorAPI on UsdPrim \p prim .
    /// Equivalent to UsdHoudiniHoudiniLightBarnDoorAPI::Get(prim.GetStage(), prim.GetPath())
    /// for a \em valid \p prim, but will not immediately throw an error for
    /// an invalid \p prim
    explicit UsdHoudiniHoudiniLightBarnDoorAPI(const UsdPrim& prim=UsdPrim())
        : UsdAPISchemaBase(prim)
    {
    }

    /// Construct a UsdHoudiniHoudiniLightBarnDoorAPI on the prim held by \p schemaObj .
    /// Should be preferred over UsdHoudiniHoudiniLightBarnDoorAPI(schemaObj.GetPrim()),
    /// as it preserves SchemaBase state.
    explicit UsdHoudiniHoudiniLightBarnDoorAPI(const UsdSchemaBase& schemaObj)
        : UsdAPISchemaBase(schemaObj)
    {
    }

    /// Destructor.
    virtual ~UsdHoudiniHoudiniLightBarnDoorAPI() override;

    /// Return a vector of names of all pre-declared attributes for this schema
    /// class and all its ancestor classes.  Does not include attributes that
    /// may be authored by custom/extended methods of the schemas involved.
        static const TfTokenVector &
    GetSchemaAttributeNames(bool includeInherited=true);

    /// Return a UsdHoudiniHoudiniLightBarnDoorAPI holding the prim adhering to this
    /// schema at \p path on \p stage.  If no prim exists at \p path on
    /// \p stage, or if the prim at that path does not adhere to this schema,
    /// return an invalid schema object.  This is shorthand for the following:
    ///
    /// \code
    /// UsdHoudiniHoudiniLightBarnDoorAPI(stage->GetPrimAtPath(path));
    /// \endcode
    ///
        static UsdHoudiniHoudiniLightBarnDoorAPI
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
    /// This information is stored by adding "HoudiniLightBarnDoorAPI" to the 
    /// token-valued, listOp metadata \em apiSchemas on the prim.
    /// 
    /// \return A valid UsdHoudiniHoudiniLightBarnDoorAPI object is returned upon success. 
    /// An invalid (or empty) UsdHoudiniHoudiniLightBarnDoorAPI object is returned upon 
    /// failure. See \ref UsdPrim::ApplyAPI() for conditions 
    /// resulting in failure. 
    /// 
    /// \sa UsdPrim::GetAppliedSchemas()
    /// \sa UsdPrim::HasAPI()
    /// \sa UsdPrim::CanApplyAPI()
    /// \sa UsdPrim::ApplyAPI()
    /// \sa UsdPrim::RemoveAPI()
    ///
        static UsdHoudiniHoudiniLightBarnDoorAPI 
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
    // BARNDOORLEFT 
    // --------------------------------------------------------------------- //
    /// Slides in a light blocker from the left, covering part of the
    /// spotlight cone. `1.0` reaches all the way across the cone, blocking all
    /// light.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `float barndoorleft = 0` |
    /// | C++ Type | float |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Float |
        UsdAttribute GetBarndoorleftAttr() const;

    /// See GetBarndoorleftAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
        UsdAttribute CreateBarndoorleftAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // BARNDOORLEFTEDGE 
    // --------------------------------------------------------------------- //
    /// Extends the solid light blocker above by an additional soft
    /// edge. `1.0` creates a gradient as wide as the spotlight cone.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `float barndoorleftedge = 0` |
    /// | C++ Type | float |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Float |
        UsdAttribute GetBarndoorleftedgeAttr() const;

    /// See GetBarndoorleftedgeAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
        UsdAttribute CreateBarndoorleftedgeAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // BARNDOORRIGHT 
    // --------------------------------------------------------------------- //
    /// Slides in a light blocker from the right, covering part of the
    /// spotlight cone. `1.0` reaches all the way across the cone, blocking all
    /// light.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `float barndoorright = 0` |
    /// | C++ Type | float |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Float |
        UsdAttribute GetBarndoorrightAttr() const;

    /// See GetBarndoorrightAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
        UsdAttribute CreateBarndoorrightAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // BARNDOORRIGHTEDGE 
    // --------------------------------------------------------------------- //
    /// Extends the solid light blocker above by an additional soft
    /// edge. `1.0` creates a gradient as wide as the spotlight cone.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `float barndoorrightedge = 0` |
    /// | C++ Type | float |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Float |
        UsdAttribute GetBarndoorrightedgeAttr() const;

    /// See GetBarndoorrightedgeAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
        UsdAttribute CreateBarndoorrightedgeAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // BARNDOORTOP 
    // --------------------------------------------------------------------- //
    /// Slides in a light blocker from the top, covering part of the
    /// spotlight cone. `1.0` reaches all the way across the cone, blocking all
    /// light.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `float barndoortop = 0` |
    /// | C++ Type | float |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Float |
        UsdAttribute GetBarndoortopAttr() const;

    /// See GetBarndoortopAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
        UsdAttribute CreateBarndoortopAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // BARNDOORTOPEDGE 
    // --------------------------------------------------------------------- //
    /// Extends the solid light blocker above by an additional soft
    /// edge. `1.0` creates a gradient as wide as the spotlight cone.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `float barndoortopedge = 0` |
    /// | C++ Type | float |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Float |
        UsdAttribute GetBarndoortopedgeAttr() const;

    /// See GetBarndoortopedgeAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
        UsdAttribute CreateBarndoortopedgeAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // BARNDOORBOTTOM 
    // --------------------------------------------------------------------- //
    /// Slides in a light blocker from the bottom, covering part of the
    /// spotlight cone. `1.0` reaches all the way across the cone, blocking all
    /// light.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `float barndoorbottom = 0` |
    /// | C++ Type | float |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Float |
        UsdAttribute GetBarndoorbottomAttr() const;

    /// See GetBarndoorbottomAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
        UsdAttribute CreateBarndoorbottomAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // BARNDOORBOTTOMEDGE 
    // --------------------------------------------------------------------- //
    /// Extends the solid light blocker above by an additional soft
    /// edge. `1.0` creates a gradient as wide as the spotlight cone.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `float barndoorbottomedge = 0` |
    /// | C++ Type | float |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Float |
        UsdAttribute GetBarndoorbottomedgeAttr() const;

    /// See GetBarndoorbottomedgeAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
        UsdAttribute CreateBarndoorbottomedgeAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

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
