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

#include "HUSD_TimeShift.h"
#include "HUSD_FindProps.h"
#include "HUSD_PathSet.h"
#include "HUSD_TimeCode.h"
#include "XUSD_Data.h"
#include "XUSD_PathSet.h"
#include "XUSD_Utils.h"
#include <pxr/base/vt/types.h>
#include <pxr/usd/ar/resolver.h>
#include <pxr/usd/sdf/attributeSpec.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/sdf/primSpec.h>
#include <pxr/usd/sdf/relationshipSpec.h>
#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/usd/prim.h>

PXR_NAMESPACE_USING_DIRECTIVE

HUSD_TimeShift::HUSD_TimeShift(HUSD_AutoLayerLock &lock)
    : myLayerLock(lock)
{
}

HUSD_TimeShift::~HUSD_TimeShift()
{
}

void
HUSD_TimeShift::shiftTime(
	const HUSD_FindProps &findprops,
	const HUSD_TimeCode &read_timecode,
        const HUSD_TimeCode &write_timecode,
        bool read_default_values,
	bool write_default_values) const
{
    auto	 outdata = myLayerLock.constData();

    if (outdata && outdata->isStageValid())
    {
	const UsdStageRefPtr &stage = outdata->stage();
	const SdfLayerRefPtr &layer = myLayerLock.layer()->layer();
	const HUSD_PathSet   &pathset = findprops.getExpandedPathSet();
        UsdTimeCode           usd_read_timecode;

        usd_read_timecode = HUSDgetUsdTimeCode(read_timecode);
	for (auto &&sdfpath : pathset.sdfPathSet())
	{
            UsdAttribute attrib = stage->GetAttributeAtPath(sdfpath);

            if (!attrib || !attrib.HasAuthoredValue())
                continue;

            if (!read_default_values &&
                HUSDgetValueTimeSampling(attrib) == HUSD_TimeSampling::NONE)
                continue;

            SdfPrimSpecHandle primspec =
                layer->GetPrimAtPath(sdfpath.GetPrimPath());
            if (!primspec)
                primspec = SdfCreatePrimInLayer(layer, sdfpath.GetPrimPath());

            SdfAttributeSpecHandle attribspec =
                layer->GetAttributeAtPath(attrib.GetPath());
            if (!attribspec)
                attribspec = SdfAttributeSpec::New(primspec,
                                    attrib.GetName(),
                                    attrib.GetTypeName(),
                                    SdfVariabilityVarying,
                                    /*custom=*/true);

            VtValue value;
            attrib.Get(&value, usd_read_timecode);

            // For relative asset paths, replace the asset path with the
            // resolved path. Because the opinion is moving to a new layer
            // (which is an anonymous layer), we can't keep the same
            // relative asset path as was authored in the layer on disk
            // holding the original opinion.
            if (value.IsHolding<SdfAssetPath>())
            {
                SdfAssetPath assetpath;

                assetpath = value.UncheckedGet<SdfAssetPath>();
                if (!assetpath.GetResolvedPath().empty())
                    value = SdfAssetPath(assetpath.GetResolvedPath());
            }

            if (write_default_values)
                attribspec->SetDefaultValue(value);
            else
                layer->SetTimeSample(attrib.GetPath(),
                    write_timecode.frame(), value);
	}
    }
}

