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

#include "HUSD_LightingMode.h"
#include <UT/UT_Map.h>
#include <UT/UT_StringMap.h>

namespace
{
    // Keep these in sync with DM_DisplayOption.ui.

    constexpr UT_StringLit theNoLightingStr(
        "No Lighting");
    constexpr UT_StringLit theHeadlightOnlyStr(
        "Headlight Only");
    constexpr UT_StringLit theDomelightOnlyStr(
        "Dome Light Only");
    constexpr UT_StringLit theNormalLightingStr(
        "Normal Lighting");
    constexpr UT_StringLit theHqLightingStr(
        "High Quality Lighting");
    constexpr UT_StringLit theHqLightingAndShadowsStr(
        "High Quality Lighting and Shadows");

    UT_Map<UT_StringHolder, HUSD_LightingMode> theStringsToLightingModes({
        { theNoLightingStr.asHolder(), HUSD_LIGHTING_MODE_NO_LIGHTING },
        { theHeadlightOnlyStr.asHolder(), HUSD_LIGHTING_MODE_HEADLIGHT_ONLY },
        { theDomelightOnlyStr.asHolder(), HUSD_LIGHTING_MODE_DOMELIGHT_ONLY },
        { theNormalLightingStr.asHolder(), HUSD_LIGHTING_MODE_NORMAL },
        { theHqLightingStr.asHolder(), HUSD_LIGHTING_MODE_HQ },
        { theHqLightingAndShadowsStr.asHolder(), HUSD_LIGHTING_MODE_HQ_SHADOWS }
    });
};

bool
HUSDisHqLightingMode(int mode)
{
    return mode == HUSD_LIGHTING_MODE_HQ ||
           mode == HUSD_LIGHTING_MODE_HQ_SHADOWS;
}

HUSD_LightingMode
HUSDlightingModeFromString(const UT_StringRef &str)
{
    auto it = theStringsToLightingModes.find(str);
    if (it != theStringsToLightingModes.end())
        return it->second;

    // For a brief time during the H20 dev cycle (r431968 Nov 14, 2022 until
    // early December when this code was added), the lighting mode was saved
    // as an int (before adding the dome light mode). So accept numbers, but
    // remap them slightly from the current enum values.
    if (str == "0")
        return HUSD_LIGHTING_MODE_NO_LIGHTING;
    else if (str == "1")
        return HUSD_LIGHTING_MODE_HEADLIGHT_ONLY;
    else if (str == "2")
        return HUSD_LIGHTING_MODE_NORMAL;
    else if (str == "3")
        return HUSD_LIGHTING_MODE_HQ;
    else if (str == "4")
        return HUSD_LIGHTING_MODE_HQ_SHADOWS;

    UT_ASSERT(!"Invalid lighting mode string specified.");
    return HUSD_LIGHTING_MODE_NORMAL;
}

const UT_StringHolder &
HUSDlightingModeToString(HUSD_LightingMode mode)
{
    for (auto &&it : theStringsToLightingModes)
        if (it.second == mode)
            return it.first;

    UT_ASSERT(!"Invalid lighting mode specified.");
    return theNormalLightingStr.asHolder();
}
