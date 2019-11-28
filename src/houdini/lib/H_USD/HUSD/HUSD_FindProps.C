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

#include "HUSD_FindProps.h"
#include "XUSD_Data.h"
#include "XUSD_PathSet.h"
#include "XUSD_Utils.h"
#include <UT/UT_String.h>
#include <UT/UT_StringMMPattern.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/property.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/base/tf/token.h>

PXR_NAMESPACE_USING_DIRECTIVE

class HUSD_FindProps::husd_FindPropsPrivate
{
public:
    husd_FindPropsPrivate()
	: myExpandedPathSetCalculated(false)
    { }

    XUSD_PathSet			 myExpandedPathSet;
    bool				 myExpandedPathSetCalculated;
};

HUSD_FindProps::HUSD_FindProps(HUSD_AutoAnyLock &lock,
	HUSD_PrimTraversalDemands demands)
    : myPrivate(new husd_FindPropsPrivate()),
      myAnyLock(lock),
      myFindPrims(lock, demands)
{
}

HUSD_FindProps::HUSD_FindProps(HUSD_AutoAnyLock &lock,
	const UT_StringRef &primpath,
	const UT_StringRef &propname,
	HUSD_PrimTraversalDemands demands)
    : myPrivate(new husd_FindPropsPrivate()),
      myAnyLock(lock),
      myFindPrims(lock, primpath, demands),
      myPropertyPattern(propname)
{
}

HUSD_FindProps::~HUSD_FindProps()
{
}

void
HUSD_FindProps::setPropertyPattern(const UT_StringHolder &pattern)
{
    myPropertyPattern = pattern;
    myPrivate->myExpandedPathSetCalculated = false;
}

const XUSD_PathSet &
HUSD_FindProps::getExpandedPathSet() const
{
    if (myPrivate->myExpandedPathSetCalculated || !myPropertyPattern.isstring())
	return myPrivate->myExpandedPathSet;

    auto		 outdata = myAnyLock.constData();

    if (outdata && outdata->isStageValid())
    {
	auto			 stage(outdata->stage());
	UT_StringMMPattern	 compiled_pattern;
	UT_StringMMPattern	*compiled_pattern_ptr = nullptr;
	TfToken			 propname;

	if (UT_String::multiMatchCheck(myPropertyPattern.c_str()))
	{
	    compiled_pattern.compile(myPropertyPattern.c_str());
	    compiled_pattern_ptr = &compiled_pattern;
	}
	else
	    propname = TfToken(myPropertyPattern.toStdString());

	for (auto &&primpath : myFindPrims.getExpandedPathSet())
	{
	    UsdPrim		 prim = stage->GetPrimAtPath(primpath);

	    if (prim)
	    {
		if (compiled_pattern_ptr)
		{
		    std::vector<UsdProperty>	 properties;

		    properties = prim.GetProperties(
			[&](const TfToken &propname) {
			    return (UT_String(propname.GetText()).
				multiMatch(*compiled_pattern_ptr) != 0);
			});
		    for (auto &&property : properties)
		    {
			if (property)
			    myPrivate->myExpandedPathSet.
				insert(property.GetPath());
		    }
		}
		else
		{
		    UsdProperty	 property = prim.GetProperty(propname);

		    if (property)
			myPrivate->myExpandedPathSet.
			    insert(property.GetPath());
		}
	    }
	}
    }

    myPrivate->myExpandedPathSetCalculated = true;
    return myPrivate->myExpandedPathSet;
}

void
HUSD_FindProps::getExpandedPaths(UT_StringArray &paths) const
{
    auto sdfpaths = getExpandedPathSet();

    paths.setSize(0);
    paths.setCapacity(sdfpaths.size());
    for( auto &&sdfpath : sdfpaths )
    {
	paths.append(sdfpath.GetText());
    }
}

