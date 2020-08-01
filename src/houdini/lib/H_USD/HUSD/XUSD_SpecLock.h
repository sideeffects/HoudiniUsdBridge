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
			mySpec = layer->GetPrimAtPath(
                            mySpecHandle.path().sdfPath());
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

