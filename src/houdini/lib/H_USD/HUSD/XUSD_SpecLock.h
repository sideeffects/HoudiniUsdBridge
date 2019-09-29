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

#ifndef __HUSD_SpecLock_h__
#define __HUSD_SpecLock_h__

#include "HUSD_API.h"
#include "XUSD_Data.h"
#include "XUSD_Utils.h"
#include "HUSD_SpecHandle.h"
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/sdf/primSpec.h>
#include <string>

PXR_NAMESPACE_OPEN_SCOPE

class XUSD_AutoSpecLock : UT_NonCopyable
{
public:
		 XUSD_AutoSpecLock(const HUSD_SpecHandle &spec)
		    : mySpecHandle(spec)
		 {
		    auto layer = SdfLayer::Find(
			mySpecHandle.identifier().toStdString());

		    if (layer)
			mySpec = layer->GetPrimAtPath(SdfPath(
			    mySpecHandle.path().toStdString()));
		 }
		~XUSD_AutoSpecLock()
		 { }

    const SdfPrimSpecHandle &spec() const
		 { return mySpec; }

private:
    const HUSD_SpecHandle	&mySpecHandle;
    SdfPrimSpecHandle		 mySpec;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif

