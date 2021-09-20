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
 *
 * Produced by:
 *	Side Effects Software Inc.
 *	123 Front Street West, Suite 1401
 *	Toronto, Ontario
 *      Canada   M5J 2M2
 *	416-504-9876
 *
 */

#include "XUSD_RootLayerData.h"
#include <pxr/usd/usdRender/tokens.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/sdf/schema.h>

PXR_NAMESPACE_OPEN_SCOPE

namespace
{
    static TfTokenVector theRootLayerFields = {
        SdfFieldKeys->Comment,
        SdfFieldKeys->Documentation,
        SdfFieldKeys->StartTimeCode,
        SdfFieldKeys->EndTimeCode,
        SdfFieldKeys->FramesPerSecond,
        SdfFieldKeys->TimeCodesPerSecond,
        SdfFieldKeys->DefaultPrim,
        SdfFieldKeys->CustomLayerData,
        UsdGeomTokens->upAxis,
        UsdGeomTokens->metersPerUnit,
        UsdRenderTokens->renderSettingsPrimPath
    };
};

XUSD_RootLayerData::XUSD_RootLayerData()
{
}

XUSD_RootLayerData::XUSD_RootLayerData(const UsdStageRefPtr &stage)
{
    fromStage(stage);
}

XUSD_RootLayerData::XUSD_RootLayerData(const SdfLayerRefPtr &layer)
{
    fromLayer(layer);
}

XUSD_RootLayerData::~XUSD_RootLayerData()
{
}

bool
XUSD_RootLayerData::isMetadataValueSet(const TfToken &field,
        const VtValue &value) const
{
    auto it = myRootMetadata.find(field);

    if (value.IsEmpty())
    {
        // Asking about clearing the value, so we want to return true if
        // the values _isn't_ set.
        if (it != myRootMetadata.end())
            return false;
    }
    else
    {
        if (it == myRootMetadata.end())
            return false;
        else if (it->second != value)
            return false;
    }

    return true;
}


bool
XUSD_RootLayerData::setMetadataValue(const TfToken &field,
        const VtValue &value)
{
    auto it = myRootMetadata.find(field);

    if (value.IsEmpty())
    {
        // Clear the value.
        if (it != myRootMetadata.end())
        {
            myRootMetadata.erase(it);
            return true;
        }
    }
    else
    {
        if (it == myRootMetadata.end())
        {
            myRootMetadata.emplace(field, value);
            return true;
        }
        else
        {
            if (it->second != value)
            {
                it->second = value;
                return true;
            }
        }
    }

    return false;
}

void
XUSD_RootLayerData::fromStage(const UsdStageRefPtr &stage)
{
    fromLayer(stage->GetRootLayer());
}

void
XUSD_RootLayerData::fromLayer(const SdfLayerRefPtr &layer)
{
    SdfPrimSpecHandle rootspec =
        layer ? layer->GetPseudoRoot() : SdfPrimSpecHandle();

    myRootMetadata.clear();
    if (rootspec)
    {
        for (auto &&field : theRootLayerFields)
        {
            if (myRootMetadata.count(field) == 0)
            {
                VtValue value;
                bool hasfield = rootspec->HasField(field, &value);

                if (hasfield)
                    myRootMetadata.emplace(field, value);
            }
        }
    }
}

bool
XUSD_RootLayerData::toStage(const UsdStageRefPtr &stage) const
{
    return toLayer(stage->GetRootLayer());
}

bool
XUSD_RootLayerData::toLayer(const SdfLayerRefPtr &layer) const
{
    SdfPrimSpecHandle rootspec =
        layer ? layer->GetPseudoRoot() : SdfPrimSpecHandle();
    bool changed = false;

    if (rootspec)
    {
        for (auto &&field : theRootLayerFields)
        {
            auto it = myRootMetadata.find(field);

            if (it == myRootMetadata.end())
            {
                if (rootspec->HasField(field))
                {
                    rootspec->ClearField(field);
                    changed = true;
                }
            }
            else
            {
                VtValue value;

                if (!rootspec->HasField(field, &value) ||
                    value != it->second)
                {
                    rootspec->SetField(field, it->second);
                    changed = true;
                }
            }
        }
    }

    return changed;
}

PXR_NAMESPACE_CLOSE_SCOPE

