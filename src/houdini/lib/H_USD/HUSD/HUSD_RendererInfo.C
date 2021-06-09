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

#include "HUSD_RendererInfo.h"
#include "HUSD_Constants.h"
#include <PY/PY_Python.h>
#include <PY/PY_Result.h>
#include <UT/UT_Options.h>
#include <UT/UT_Debug.h>
#include <UT/UT_StringHolder.h>
#include <UT/UT_WorkBuffer.h>

namespace
{
    HUSD_DepthStyle
    getDepthStyleFromStr(const UT_StringHolder &str)
    {
	if (str == "linear")
	    return HUSD_DEPTH_LINEAR;
	else if (str == "opengl")
	    return HUSD_DEPTH_OPENGL;
	else if (str == "none")
	    return HUSD_DEPTH_NONE;
	else if (str == "normalized")
	    return HUSD_DEPTH_NORMALIZED;

        // Default to [0,1] GL depth as per USD 20.02 spec.
	return HUSD_DEPTH_OPENGL;
    }
}

HUSD_RendererInfo
HUSD_RendererInfo::getRendererInfo(const UT_StringHolder &name,
	const UT_StringHolder &displayname)
{
    UT_WorkBuffer	 expr;
    UT_String            displayname_safe(displayname.c_str());
    PY_Result		 result;

    displayname_safe.substitute("'", "\\'");
    expr.sprintf("__import__('usdrenderers').getRendererInfo('%s', '%s')",
	name.c_str(), displayname_safe.c_str());
    result = PYrunPythonExpression(expr.buffer(), PY_Result::OPTIONS);

    const UT_Options	&options = result.myOptions;
    UT_StringHolder	 menulabel = displayname;
    UT_StringArray	 defaultpurposes({ "proxy" });
    UT_StringArray	 restartrendersettings;
    UT_StringArray	 restartcamerasettings;
    UT_StringArray	 renderstats;
    int			 menupriority = 0;
    fpreal		 multiplier = 1.0;
    HUSD_DepthStyle	 depthstyle = HUSD_DEPTH_OPENGL;
    bool		 needsdepth = false;
    bool		 needsselection = false;
    bool		 allowbackgroundupdate = true;
    bool		 aovsupport = true;
    bool		 drawmodesupport = false;
    bool		 husk_fastexit = false;
    bool		 isnative = false;
    bool		 isvalid = true;

    isnative = (name == HUSD_Constants::getHoudiniRendererPluginName());
    if (options.hasOption("valid"))
	isvalid = options.getOptionB("valid");

    if (isvalid)
    {
	if (options.hasOption("menulabel"))
	    menulabel = options.getOptionS("menulabel");
	if (options.hasOption("menupriority"))
	    menupriority = options.getOptionI("menupriority");
	if (options.hasOption("complexitymultiplier"))
	    multiplier = options.getOptionF("complexitymultiplier");
	if (options.hasOption("depthstyle"))
	    depthstyle = getDepthStyleFromStr(options.getOptionS("depthstyle"));
	if (options.hasOption("defaultpurposes"))
	    defaultpurposes = options.getOptionSArray("defaultpurposes");
	if (options.hasOption("restartrendersettings"))
	    restartrendersettings =
                options.getOptionSArray("restartrendersettings");
	if (options.hasOption("restartcamerasettings"))
	    restartcamerasettings =
                options.getOptionSArray("restartcamerasettings");
	if (options.hasOption("viewstats"))
	    renderstats = options.getOptionSArray("viewstats");
	if (options.hasOption("needsdepth"))
	    needsdepth = options.getOptionI("needsdepth");
	if (options.hasOption("needsselection"))
	    needsselection = options.getOptionI("needsselection");
	if (options.hasOption("allowbackgroundupdate"))
	    allowbackgroundupdate = options.getOptionI("allowbackgroundupdate");
	if (options.hasOption("aovsupport"))
	    aovsupport = options.getOptionI("aovsupport");
	if (options.hasOption("drawmodesupport"))
	    drawmodesupport = options.getOptionI("drawmodesupport");
	if (options.hasOption("husk.fast-exit"))
	    husk_fastexit = options.getOptionI("husk.fast-exit");

	return HUSD_RendererInfo(
	    name,
	    displayname,
	    menulabel,
	    menupriority,
	    multiplier,
	    isnative,
	    depthstyle,
	    defaultpurposes,
            restartrendersettings,
            restartcamerasettings,
            renderstats,
	    needsdepth,
	    needsselection,
	    allowbackgroundupdate,
            aovsupport,
            drawmodesupport,
	    husk_fastexit
	);
    }

    return HUSD_RendererInfo();
}

