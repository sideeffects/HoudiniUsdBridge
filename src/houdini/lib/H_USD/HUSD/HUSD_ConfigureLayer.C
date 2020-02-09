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

#include "HUSD_ConfigureLayer.h"
#include "HUSD_Constants.h"
#include "XUSD_Data.h"
#include "XUSD_Utils.h"
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdRender/tokens.h>

PXR_NAMESPACE_USING_DIRECTIVE

HUSD_ConfigureLayer::HUSD_ConfigureLayer(HUSD_AutoWriteLock &lock)
    : myWriteLock(lock)
{
}

HUSD_ConfigureLayer::~HUSD_ConfigureLayer()
{
}

bool
HUSD_ConfigureLayer::setSavePath(const UT_StringRef &save_path) const
{
    auto		 outdata = myWriteLock.data();

    if (outdata && outdata->isStageValid())
    {
	std::string	 save_control;

	// When we set a save path, we also want to set the "explicit save
	// control" descriptor on the layer if it isn't already set.
	HUSDsetSavePath(outdata->activeLayer(), save_path);
	if (!HUSDgetSaveControl(outdata->activeLayer(), save_control))
	    HUSDsetSaveControl(outdata->activeLayer(),
		HUSD_Constants::getSaveControlExplicit());

	return true;
    }

    return false;
}

bool
HUSD_ConfigureLayer::setSaveControl(const UT_StringRef &save_control) const
{
    auto		 outdata = myWriteLock.data();

    if (outdata && outdata->isStageValid())
    {
	HUSDsetSaveControl(outdata->activeLayer(), save_control);

	return true;
    }

    return false;
}

bool
HUSD_ConfigureLayer::setStartTime(fpreal64 start_time) const
{
    auto		 outdata = myWriteLock.data();

    if (outdata && outdata->isStageValid())
    {
	outdata->activeLayer()->SetStartTimeCode(start_time);

	return true;
    }

    return false;
}

bool
HUSD_ConfigureLayer::setEndTime(fpreal64 end_time) const
{
    auto		 outdata = myWriteLock.data();

    if (outdata && outdata->isStageValid())
    {
	outdata->activeLayer()->SetEndTimeCode(end_time);

	return true;
    }

    return false;
}

bool
HUSD_ConfigureLayer::setTimePerSecond(fpreal64 time_per_second) const
{
    auto		 outdata = myWriteLock.data();

    if (outdata && outdata->isStageValid())
    {
	outdata->activeLayer()->SetTimeCodesPerSecond(time_per_second);

	return true;
    }

    return false;
}

bool
HUSD_ConfigureLayer::setFramesPerSecond(fpreal64 frames_per_second) const
{
    auto		 outdata = myWriteLock.data();

    if (outdata && outdata->isStageValid())
    {
	outdata->activeLayer()->SetFramesPerSecond(frames_per_second);

	return true;
    }

    return false;
}

bool
HUSD_ConfigureLayer::setDefaultPrim(const UT_StringRef &primpath) const
{
    auto		 outdata = myWriteLock.data();

    if (outdata && outdata->isStageValid())
    {
	if (primpath.isstring())
	    outdata->activeLayer()->
		SetDefaultPrim(TfToken(primpath.toStdString()));
	else
	    outdata->activeLayer()->ClearDefaultPrim();

	return true;
    }

    return false;
}

bool
HUSD_ConfigureLayer::setComment(const UT_StringRef &comment) const
{
    auto		 outdata = myWriteLock.data();

    if (outdata && outdata->isStageValid())
    {
        outdata->activeLayer()->SetComment(TfToken(comment.toStdString()));

	return true;
    }

    return false;
}

bool
HUSD_ConfigureLayer::setUpAxis(const UT_StringRef &upaxis) const
{
    auto		 outdata = myWriteLock.data();

    if (outdata && outdata->isStageValid())
    {
	if (upaxis.isstring())
	    outdata->activeLayer()->GetPseudoRoot()->SetInfo(
		UsdGeomTokens->upAxis, VtValue(TfToken(upaxis.toStdString())));
	else
	    outdata->activeLayer()->GetPseudoRoot()->
		ClearInfo(UsdGeomTokens->upAxis);

	return true;
    }

    return false;
}

bool
HUSD_ConfigureLayer::setMetersPerUnit(fpreal metersperunit) const
{
    auto		 outdata = myWriteLock.data();

    if (outdata && outdata->isStageValid())
    {
	if (metersperunit != 0.0)
	    outdata->activeLayer()->GetPseudoRoot()->SetInfo(
		UsdGeomTokens->metersPerUnit, VtValue(metersperunit));
	else
	    outdata->activeLayer()->GetPseudoRoot()->
		ClearInfo(UsdGeomTokens->metersPerUnit);

	return true;
    }

    return false;
}

bool
HUSD_ConfigureLayer::setRenderSettings(const UT_StringRef &primpath) const
{
    auto		 outdata = myWriteLock.data();

    if (outdata && outdata->isStageValid())
    {
	if (primpath.isstring())
	    outdata->activeLayer()->GetPseudoRoot()->SetInfo(
		UsdRenderTokens->renderSettingsPrimPath,
                VtValue(primpath.toStdString()));
	else
	    outdata->activeLayer()->GetPseudoRoot()->
		ClearInfo(UsdRenderTokens->renderSettingsPrimPath);

	return true;
    }

    return false;
}

bool
HUSD_ConfigureLayer::clearStandardMetadata() const
{
    auto		 outdata = myWriteLock.data();

    if (outdata && outdata->isStageValid())
	return HUSDclearLayerMetadata(outdata->activeLayer());

    return false;
}

