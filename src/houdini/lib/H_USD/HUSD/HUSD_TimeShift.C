/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
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
#include "HUSD_FindPrims.h"
#include "HUSD_TimeCode.h"
#include "XUSD_Data.h"
#include "XUSD_PathSet.h"
#include "XUSD_Utils.h"
#include <pxr/base/vt/types.h>
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
	const HUSD_FindPrims &findprims,
	fpreal currentframe,
	fpreal sampleframe,
	bool setdefault) const
{
    auto	 outdata = myLayerLock.constData();

    if (outdata && outdata->isStageValid())
    {
	const UsdStageRefPtr &stage = outdata->stage();
	const SdfLayerRefPtr &layer = myLayerLock.layer()->layer();
	UT_StringArray	      paths;
	UsdTimeCode	      sampletimecode(sampleframe);

	auto		     &pathset = findprims.getExpandedPathSet();

	for (auto &&sdfpath : pathset)
	{
	    auto	  usdprim = stage->GetPrimAtPath(sdfpath);
	    auto	&&attribs = usdprim.GetAttributes();

	    if (!usdprim)
	    {
		continue;
	    }

	    for (auto &&attrib : attribs)
	    {
		if(setdefault && !HUSDvalueMightBeTimeVarying(attrib))
		    continue;

		SdfPrimSpecHandle primspec =
		    layer->GetPrimAtPath(sdfpath);
		if (!primspec)
		    primspec = SdfCreatePrimInLayer(layer, sdfpath);

		SdfAttributeSpecHandle attribspec =
		    layer->GetAttributeAtPath(attrib.GetPath());
		if (!attribspec)
		    attribspec = SdfAttributeSpec::New(primspec,
					attrib.GetName(),
					attrib.GetTypeName(),
					SdfVariabilityVarying,
					/*custom=*/true);

		VtValue value;
		attrib.Get(&value, sampletimecode);

		if (setdefault)
		{
		    attribspec->SetDefaultValue(value);
		    attrib.Set(value, sampletimecode);
		}
		else
		{
		    layer->SetTimeSample(attrib.GetPath(), currentframe, value);
		}
	    }
	}
    }
}

