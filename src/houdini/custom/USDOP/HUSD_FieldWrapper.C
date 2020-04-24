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
*/

#include "HUSD_FieldWrapper.h"

#include <GT/GT_PrimVDB.h>
#include <GT/GT_PrimVolume.h>
#include <GT/GT_Refine.h>
#include <gusd/xformWrapper.h>
#include <HUSD/HUSD_HydraField.h>
#include <HUSD/XUSD_Tokens.h>

#include <pxr/usd/usdVol/openVDBAsset.h>
#include <HUSD/UsdHoudini/houdiniFieldAsset.h>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(_tokens,
    ((vdbFieldPrimType,  "OpenVDBAsset"))
    ((houdiniFieldPrimType, "HoudiniFieldAsset"))
    ((volumePrimType, "Volume"))
);

void
HUSD_FieldWrapper::registerForRead()
{
    static std::once_flag registered;

    std::call_once(registered, []() {
        // Register for both VDB and Houdini volumes.
        GusdPrimWrapper::registerPrimDefinitionFuncForRead(
            _tokens->vdbFieldPrimType, &HUSD_FieldWrapper::defineForRead);
        GusdPrimWrapper::registerPrimDefinitionFuncForRead(
            _tokens->houdiniFieldPrimType, &HUSD_FieldWrapper::defineForRead);

        // Also register Volume primitives so that they unpack to fields.
        GusdPrimWrapper::registerPrimDefinitionFuncForRead(
            _tokens->volumePrimType, &GusdXformWrapper::defineForRead);
    });
}

HUSD_FieldWrapper::HUSD_FieldWrapper(const UsdVolFieldAsset &usd_field,
                                     UsdTimeCode time, GusdPurposeSet purposes)
    : GusdPrimWrapper(time, purposes), myUsdField(usd_field)
{
}

HUSD_FieldWrapper::~HUSD_FieldWrapper() {}

const char *
HUSD_FieldWrapper::className() const
{
    return "HUSD_FieldWrapper";
}

void
HUSD_FieldWrapper::enlargeBounds(UT_BoundingBox boxes[], int nsegments) const
{
    UT_ASSERT_MSG(false, "HUSD_FieldWrapper::enlargeBounds not implemented");
}

int
HUSD_FieldWrapper::getMotionSegments() const
{
    return 1;
}

int64
HUSD_FieldWrapper::getMemoryUsage() const
{
    return sizeof(*this);
}

GT_PrimitiveHandle
HUSD_FieldWrapper::doSoftCopy() const
{
    return GT_PrimitiveHandle(new HUSD_FieldWrapper(*this));
}

bool
HUSD_FieldWrapper::isValid() const
{
    return static_cast<bool>(myUsdField);
}

bool
HUSD_FieldWrapper::refine(GT_Refine &refiner,
                            const GT_RefineParms *parms) const
{
    if (!isValid())
    {
        TF_WARN("Invalid prim");
        return false;
    }

    UsdAttribute file_path_attr = myUsdField.GetFilePathAttr();
    SdfAssetPath file_path;
    if (file_path_attr)
        TF_VERIFY(file_path_attr.Get(&file_path, m_time));

    UsdAttribute field_name_attr =
        myUsdField.GetPrim().GetAttribute(UsdVolTokens->fieldName);
    TfToken field_name;
    if (field_name_attr)
        TF_VERIFY(field_name_attr.Get(&field_name, m_time));

    UsdAttribute field_index_attr =
        myUsdField.GetPrim().GetAttribute(UsdVolTokens->fieldIndex);
    int field_index = -1;
    if (field_index_attr)
        TF_VERIFY(field_index_attr.Get(&field_index, m_time));

    bool is_vdb = false;
    bool is_houdini_vol = false;
    TfToken field_type;

    // Determine the field type, and the correct type to pass to
    // HUSD_HydraField::getVolumePrimitive (which expects Hydra prim types).
    const TfToken &src_field_type = myUsdField.GetPrim().GetTypeName();
    if (src_field_type == _tokens->vdbFieldPrimType)
    {
        is_vdb = true;
        field_type = HusdHdPrimTypeTokens()->openvdbAsset;
    }
    else if (src_field_type == _tokens->houdiniFieldPrimType)
    {
        is_houdini_vol = true;
        field_type = HusdHdPrimTypeTokens()->bprimHoudiniFieldAsset;
    }
    else
    {
        UT_ASSERT_MSG(false, "Unknown volume primitive type");
        return false;
    }

    // Attempt to load the volume from disk or a SOP network.
    GT_PrimitiveHandle volume = HUSD_HydraField::getVolumePrimitive(
        file_path.GetAssetPath(), field_name.GetString(), field_index,
        field_type.GetString());
    if (!volume)
    {
        UT_ASSERT_MSG(false, "Could not load volume");
        return false;
    }

    // Since we may have loaded the volume from SOPs, replace the attribute
    // list with the field's primvars so that extra attributes won't
    // unexpectedly appear.
    GT_AttributeListHandle attribs =
        new GT_AttributeList(new GT_AttributeMap());
    const UsdPrimDefinition &prim_defn = is_vdb ?
        *UsdVolOpenVDBAsset(myUsdField).GetSchemaClassPrimDefinition() :
        *UsdHoudiniHoudiniFieldAsset(myUsdField).GetSchemaClassPrimDefinition();
    loadPrimvars(prim_defn, m_time, parms, 1, 0, 0,
                 myUsdField.GetPath().GetString(), nullptr, nullptr, &attribs,
                 nullptr);

    if (is_vdb)
    {
        auto vol = UTverify_cast<GT_PrimVDB*>(volume.get());
        vol->setUniformAttributes(attribs);
    }
    else if (is_houdini_vol)
    {
        auto vol = UTverify_cast<GT_PrimVolume *>(volume.get());
        vol->setUniformAttributes(attribs);
    }

    refiner.addPrimitive(volume);
    return true;
}

GT_PrimitiveHandle
HUSD_FieldWrapper::defineForRead(const UsdGeomImageable &source_prim,
                                   UsdTimeCode time, GusdPurposeSet purposes)
{
    return new HUSD_FieldWrapper(UsdVolFieldAsset(source_prim.GetPrim()), time,
                                 purposes);
}

PXR_NAMESPACE_CLOSE_SCOPE
