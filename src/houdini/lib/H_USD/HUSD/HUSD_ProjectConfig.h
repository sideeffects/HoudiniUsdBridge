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

#ifndef __HUSD_Token_h__
#define __HUSD_Token_h__

#include "HUSD_API.h"
#include <UT/UT_StringHolder.h>
#include <UT/UT_Function.h>

/// Global methods for hooking LOP behavior back to a higher level project
/// configuration tool.

/// Add a callback that is invoked right before a layer is written to disk.
/// Return false from the callback if the layer should not be written to disk.
using HUSD_DoLayerDiffFn =
    UT_Function<bool(const UT_StringHolder &)>;
HUSD_API void HUSDsetDoLayerDiffCallback(HUSD_DoLayerDiffFn cb);
HUSD_API HUSD_DoLayerDiffFn HUSDgetDoLayerDiffCallback();

/// Add a callback that is invoked during a USD save operation. In particular,
/// if a USD file is being saved one frame at a time, and the asset resolver
/// for the output file automatically creates new file versions, we use the
/// value returned here to make sure that ever subsequent frame data is saved
/// to the version of the file created by the first save operation.
using HUSD_PathWithVersionSpecifierFn =
    UT_Function<UT_StringHolder(const UT_StringHolder &)>;
HUSD_API void HUSDsetPathWithVersionSpecifierCallback(
    HUSD_PathWithVersionSpecifierFn cb);
HUSD_API HUSD_PathWithVersionSpecifierFn
    HUSDgetPathWithVersionSpecifierCallback();

/// Global setting to control the context option that is used to identify the
/// "current shot".
using HUSD_GetShotContextOptionFn =
    UT_Function<UT_StringHolder()>;
HUSD_API void HUSDsetActiveShotContextOption(HUSD_GetShotContextOptionFn cb);
HUSD_API UT_StringHolder HUSDgetActiveShotContextOption();

/// Add a callback that is run to determine if the supplied unit is active
/// given the provided "current unit". The test function is passed
/// (shot match pattern, current shot). Return true if the current shot
/// matches the supplied pattern.
using HUSD_ActiveShotTestFn =
    UT_Function<bool(const UT_StringHolder &, const UT_StringHolder &)>;
HUSD_API void HUSDsetActiveShotTest(HUSD_ActiveShotTestFn test);
HUSD_API HUSD_ActiveShotTestFn HUSDgetActiveShotTest();

#endif

