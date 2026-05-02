//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef USDHOUDINI_GENERATED_HOUDININODEGRAPHCONTAINERAPI_H
#define USDHOUDINI_GENERATED_HOUDININODEGRAPHCONTAINERAPI_H

/// \file usdHoudini/houdiniNodeGraphContainerAPI.h

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
// HOUDININODEGRAPHCONTAINERAPI                                               //
// -------------------------------------------------------------------------- //

/// \class UsdHoudiniHoudiniNodeGraphContainerAPI
///
/// Houdini API schema for storing node graph container information,
/// such as input and output positions, display colors, 
/// connection wires drawing style, etc.
///
/// For any described attribute \em Fallback \em Value or \em Allowed \em Values below
/// that are text/tokens, the actual token is published and defined in \ref UsdHoudiniTokens.
/// So to set an attribute to the value "rightHanded", use UsdHoudiniTokens->rightHanded
/// as the value.
///
class
USDHOUDINI_API
UsdHoudiniHoudiniNodeGraphContainerAPI : public UsdAPISchemaBase
{
public:
    /// Compile time constant representing what kind of schema this class is.
    ///
    /// \sa UsdSchemaKind
    static const UsdSchemaKind schemaKind = UsdSchemaKind::SingleApplyAPI;

    /// Construct a UsdHoudiniHoudiniNodeGraphContainerAPI on UsdPrim \p prim .
    /// Equivalent to UsdHoudiniHoudiniNodeGraphContainerAPI::Get(prim.GetStage(), prim.GetPath())
    /// for a \em valid \p prim, but will not immediately throw an error for
    /// an invalid \p prim
    explicit UsdHoudiniHoudiniNodeGraphContainerAPI(const UsdPrim& prim=UsdPrim())
        : UsdAPISchemaBase(prim)
    {
    }

    /// Construct a UsdHoudiniHoudiniNodeGraphContainerAPI on the prim held by \p schemaObj .
    /// Should be preferred over UsdHoudiniHoudiniNodeGraphContainerAPI(schemaObj.GetPrim()),
    /// as it preserves SchemaBase state.
    explicit UsdHoudiniHoudiniNodeGraphContainerAPI(const UsdSchemaBase& schemaObj)
        : UsdAPISchemaBase(schemaObj)
    {
    }

    /// Destructor.
    virtual ~UsdHoudiniHoudiniNodeGraphContainerAPI() override;

    /// Return a vector of names of all pre-declared attributes for this schema
    /// class and all its ancestor classes.  Does not include attributes that
    /// may be authored by custom/extended methods of the schemas involved.
        static const TfTokenVector &
    GetSchemaAttributeNames(bool includeInherited=true);

    /// Return a UsdHoudiniHoudiniNodeGraphContainerAPI holding the prim adhering to this
    /// schema at \p path on \p stage.  If no prim exists at \p path on
    /// \p stage, or if the prim at that path does not adhere to this schema,
    /// return an invalid schema object.  This is shorthand for the following:
    ///
    /// \code
    /// UsdHoudiniHoudiniNodeGraphContainerAPI(stage->GetPrimAtPath(path));
    /// \endcode
    ///
        static UsdHoudiniHoudiniNodeGraphContainerAPI
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
    /// This information is stored by adding "HoudiniNodeGraphContainerAPI" to the 
    /// token-valued, listOp metadata \em apiSchemas on the prim.
    /// 
    /// \return A valid UsdHoudiniHoudiniNodeGraphContainerAPI object is returned upon success. 
    /// An invalid (or empty) UsdHoudiniHoudiniNodeGraphContainerAPI object is returned upon 
    /// failure. See \ref UsdPrim::ApplyAPI() for conditions 
    /// resulting in failure. 
    /// 
    /// \sa UsdPrim::GetAppliedSchemas()
    /// \sa UsdPrim::HasAPI()
    /// \sa UsdPrim::CanApplyAPI()
    /// \sa UsdPrim::ApplyAPI()
    /// \sa UsdPrim::RemoveAPI()
    ///
        static UsdHoudiniHoudiniNodeGraphContainerAPI 
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
    // CONTAINERINPUTPOS 
    // --------------------------------------------------------------------- //
    /// Position of the container input in the node graph.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `float2 houdini:containerInputPos` |
    /// | C++ Type | GfVec2f |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Float2 |
        UsdAttribute GetContainerInputPosAttr() const;

    /// See GetContainerInputPosAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
        UsdAttribute CreateContainerInputPosAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // CONTAINEROUTPUTPOS 
    // --------------------------------------------------------------------- //
    /// Position of the container output in the node graph.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `float2 houdini:containerOutputPos` |
    /// | C++ Type | GfVec2f |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Float2 |
        UsdAttribute GetContainerOutputPosAttr() const;

    /// See GetContainerOutputPosAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
        UsdAttribute CreateContainerOutputPosAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // CONTAINERINPUTDISPLAYCOLOR 
    // --------------------------------------------------------------------- //
    /// Display color for the container input in the node graph.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `color3f houdini:containerInputDisplayColor` |
    /// | C++ Type | GfVec3f |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Color3f |
        UsdAttribute GetContainerInputDisplayColorAttr() const;

    /// See GetContainerInputDisplayColorAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
        UsdAttribute CreateContainerInputDisplayColorAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // CONTAINEROUTPUTDISPLAYCOLOR 
    // --------------------------------------------------------------------- //
    /// Display color for the container output in the node graph.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `color3f houdini:containerOutputDisplayColor` |
    /// | C++ Type | GfVec3f |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Color3f |
        UsdAttribute GetContainerOutputDisplayColorAttr() const;

    /// See GetContainerOutputDisplayColorAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
        UsdAttribute CreateContainerOutputDisplayColorAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // CONTAINERWIRESTYLE 
    // --------------------------------------------------------------------- //
    /// Drawing style of the wire connections between nodes inside the node graph.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `token houdini:containerWireStyle = "inherited"` |
    /// | C++ Type | TfToken |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Token |
    /// | \ref UsdHoudiniTokens "Allowed Values" | inherited, straight, rounded |
        UsdAttribute GetContainerWireStyleAttr() const;

    /// See GetContainerWireStyleAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
        UsdAttribute CreateContainerWireStyleAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

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
