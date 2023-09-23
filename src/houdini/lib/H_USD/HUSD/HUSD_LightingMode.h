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

#ifndef __HUSD_LightingModes_h__
#define __HUSD_LightingModes_h__

#include "HUSD_API.h"
#include <UT/UT_StringHolder.h>

// Available viewport lighting modes.
enum HUSD_LightingMode {
    HUSD_LIGHTING_MODE_NO_LIGHTING = 0,
    HUSD_LIGHTING_MODE_HEADLIGHT_ONLY = 1,
    HUSD_LIGHTING_MODE_DOMELIGHT_ONLY = 2,
    HUSD_LIGHTING_MODE_NORMAL = 3,
    HUSD_LIGHTING_MODE_HQ = 4,
    HUSD_LIGHTING_MODE_HQ_SHADOWS = 5,
    HUSD_LIGHTING_MODE_COUNT = 6
};

HUSD_API extern bool
HUSDisHqLightingMode(int mode);

HUSD_API extern HUSD_LightingMode
HUSDlightingModeFromString(const UT_StringRef &str);

HUSD_API extern const UT_StringHolder &
HUSDlightingModeToString(HUSD_LightingMode mode);

#endif
