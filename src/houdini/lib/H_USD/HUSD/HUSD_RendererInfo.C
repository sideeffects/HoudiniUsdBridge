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

	return HUSD_DEPTH_NORMALIZED;
    }
}

HUSD_RendererInfo
HUSD_RendererInfo::getRendererInfo(const UT_StringHolder &name,
	const UT_StringHolder &displayname)
{
    UT_WorkBuffer	 expr;
    PY_Result		 result;

    expr.sprintf("__import__('usdrenderers').getRendererInfo('%s', '%s')",
	name.c_str(), displayname.c_str());
    result = PYrunPythonExpression(expr.buffer(), PY_Result::OPTIONS);

    const UT_Options	&options = result.myOptions;
    UT_StringHolder	 menulabel = displayname;
    UT_StringArray	 defaultpurposes({ "proxy" });
    int			 menupriority = 0;
    fpreal		 multiplier = 1.0;
    HUSD_DepthStyle	 depthstyle = HUSD_DEPTH_NORMALIZED;
    bool		 needsdepth = false;
    bool		 needsselection = false;
    bool		 allowbackgroundupdate = true;
    bool		 aovsupport = true;
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
	if (options.hasOption("needsdepth"))
	    needsdepth = options.getOptionI("needsdepth");
	if (options.hasOption("needsselection"))
	    needsselection = options.getOptionI("needsselection");
	if (options.hasOption("allowbackgroundupdate"))
	    allowbackgroundupdate = options.getOptionI("allowbackgroundupdate");
	if (options.hasOption("aovsupport"))
	    aovsupport = options.getOptionI("aovsupport");

	return HUSD_RendererInfo(
	    name,
	    displayname,
	    menulabel,
	    menupriority,
	    multiplier,
	    isnative,
	    depthstyle,
	    defaultpurposes,
	    needsdepth,
	    needsselection,
	    allowbackgroundupdate,
            aovsupport
	);
    }

    return HUSD_RendererInfo();
}

