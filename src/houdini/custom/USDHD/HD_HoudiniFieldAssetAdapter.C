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
#include "HD_HoudiniFieldAssetAdapter.h"
#include <HUSD/UsdHoudini/houdiniFieldAsset.h>
#include <HUSD/UsdHoudini/tokens.h>
#include <HUSD/XUSD_Tokens.h>

#include "pxr/imaging/hd/volumeFieldSchema.h"
#include "pxr/usd/usdVol/tokens.h"
#include "pxr/usdImaging/usdVolImaging/dataSourceFieldAsset.h"

#include "pxr/base/tf/type.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_REGISTRY_FUNCTION_WITH_TAG(TfType, USD_HD_HoudiniFieldAssetAdapter)
{
    typedef HD_HoudiniFieldAssetAdapter Adapter;
    TfType t = TfType::Define<Adapter, TfType::Bases<Adapter::BaseAdapter> >();
    t.SetFactory< UsdImagingPrimAdapterFactory<Adapter> >();
}

HD_HoudiniFieldAssetAdapter::~HD_HoudiniFieldAssetAdapter() = default;

TfTokenVector
HD_HoudiniFieldAssetAdapter::GetImagingSubprims(UsdPrim const& prim)
{
    return { TfToken() };
}

TfToken
HD_HoudiniFieldAssetAdapter::GetImagingSubprimType(
        UsdPrim const& prim,
        TfToken const& subprim)
{
    if (subprim.IsEmpty()) {
        return HusdHdPrimTypeTokens->houdiniFieldAsset;
    }
    return TfToken();
}

HdContainerDataSourceHandle
HD_HoudiniFieldAssetAdapter::GetImagingSubprimData(
        UsdPrim const& prim,
        TfToken const& subprim,
        const UsdImagingDataSourceStageGlobals &stageGlobals)
{
    if (subprim.IsEmpty()) {
        return UsdImagingDataSourceFieldAssetPrim::New(
            prim.GetPath(),
            prim,
            stageGlobals);
    }
    return nullptr;
}

HdDataSourceLocatorSet
HD_HoudiniFieldAssetAdapter::InvalidateImagingSubprim(
        UsdPrim const& prim,
        TfToken const& subprim,
        TfTokenVector const& properties,
        UsdImagingPropertyInvalidationType invalidationType)
{
    if (subprim.IsEmpty())
    {
        // We can't directly use UsdImagingDataSourceFieldAssetPrim::Invalidate()
        // since it is specialized for UsdVolOpenVDBAsset and UsdVolField3DAsset
        // so we need to effectively duplicate the logic here.
        
        // BEGIN (SPECIALIZED) DUPLICATION

        auto _ConcatenateAttributeNames = [&](
            const TfTokenVector& left,const TfTokenVector& right)
        {
            TfTokenVector result;
            result.reserve(left.size() + right.size());
            result.insert(result.end(), left.begin(), left.end());
            result.insert(result.end(), right.begin(), right.end());
            return result;
        };
        static TfTokenVector fieldNames = _ConcatenateAttributeNames(
                UsdVolVolumeFieldAsset::GetSchemaAttributeNames(
                    /* includeInherited = */ false),
                UsdHoudiniHoudiniFieldAsset::GetSchemaAttributeNames(
                    /* includeInherited = */ false));
        
        HdDataSourceLocatorSet locators =
            UsdImagingDataSourcePrim::Invalidate(
                prim, subprim, properties, invalidationType);

        for (const TfToken &propertyName : properties)
        {
            if (std::find(fieldNames.begin(), fieldNames.end(), propertyName)
                    != fieldNames.end())
            {
                locators.insert(HdVolumeFieldSchema::GetDefaultLocator());
                break;
            }
        }

        // END (SPECIALIZED) DUPLICATION

        return locators;
    }
    
    return HdDataSourceLocatorSet();
}

VtValue
HD_HoudiniFieldAssetAdapter::Get(
    UsdPrim const& prim,
    SdfPath const& cachePath,
    TfToken const& key,
    UsdTimeCode time,
    VtIntArray *outIndices) const
{
    if ( key == UsdVolTokens->filePath ||
         key == UsdVolTokens->fieldName ||
         key == UsdVolTokens->fieldIndex ||
         key == UsdVolTokens->fieldDataType ||
         key == UsdVolTokens->vectorDataRoleHint ||
         key == UsdVolTokens->fieldClass) {
        
        if (UsdAttribute const &attr = prim.GetAttribute(key)) {
            VtValue value;
            if (attr.Get(&value, time)) {
                return value;
            }
        }

        if (key == UsdVolTokens->filePath) {
            return VtValue(SdfAssetPath());
        }
        if (key == UsdVolTokens->fieldIndex) {
            constexpr int def = 0;
            return VtValue(def);
        }

        return VtValue(TfToken());
    }
    
    return
        BaseAdapter::Get(
            prim,
            cachePath,
            key,
            time,
            outIndices);
}

TfToken
HD_HoudiniFieldAssetAdapter::GetPrimTypeToken() const
{
    return HusdHdPrimTypeTokens->houdiniFieldAsset;
}

PXR_NAMESPACE_CLOSE_SCOPE
