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
#include <UT/UT_JSONValue.h>
#include <UT/UT_JSONWriter.h>
#include <UT/UT_JSONPath.h>
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

    bool
    getStringMap(const UT_StringHolder &name,
            UT_StringMap<UT_StringHolder> &map,
            const UT_Options &opts,
            const UT_StringRef &key)
    {
        map.clear();

        if (!opts.hasOption(key))
            return true;        // No error

        // ATM, UT_Options doesn't import properly from a JSON dictionary
        UT_OptionsHolder        dict;
        if (opts.importOption(key, dict))
        {
            UT_StringHolder     str;
            for (auto it = dict->begin(), end = dict->end(); it != end; ++it)
            {
                if (it.entry()->importOption(str))
                {
                    map[it.name()] = str;
                }
                else
                {
                    UTdebugFormat("Invalid value for string map key '{}'",
                            it.name());
                }
            }
            return true;
        }

        // Fall back to an array of pairs of strings
        UT_StringArray          arr;
        if (opts.importOption(key, arr))
        {
            if (arr.size() % 2 != 0)
                return false;

            for (exint i = 0, n = arr.size(); i < n; i += 2)
                map[arr[i]] = arr[i+1];

            return true;
        }
        UTformat(stderr, "{}: Error processing {}\n", name, key);
        return false;
    }
}

HUSD_RendererInfo
HUSD_RendererInfo::getRendererInfo(const UT_StringHolder &name,
        const UT_StringHolder &displayname)
{
    UT_StringMap<UT_OptionEntryPtr> custom_info;
    return getRendererInfo(name, displayname, custom_info);
}

HUSD_RendererInfo
HUSD_RendererInfo::getRendererInfo(const UT_StringHolder &name,
	const UT_StringHolder &displayname,
        UT_StringMap<UT_OptionEntryPtr> &custom)
{
    UT_WorkBuffer	 expr;
    UT_String            displayname_safe(displayname.c_str());
    PY_Result		 result;

    displayname_safe.substitute("'", "\\'");
    expr.sprintf("__import__('usdrenderers').getRendererInfo('%s', '%s')",
	name.c_str(), displayname_safe.c_str());
    result = PYrunPythonExpression(expr.buffer(), PY_Result::OPTIONS);

    if (result.myResultType == PY_Result::ERR)
    {
        UTformat(stderr, "Error loading UsdRenderers.json:\n{}",
                result.myDetailedErrValue);
    }

    const UT_Options	&options = result.myOptions;
    UT_StringHolder	 menulabel = displayname;
    UT_StringArray	 defaultpurposes({ "proxy" });
    UT_StringArray	 restartrendersettings;
    UT_StringArray	 restartcamerasettings;
    UT_StringArray	 renderstats;
    HuskMetadata         husk_metadata;
    StatsDataPaths       statsdatapaths;
    UT_StringHolder      husk_verbose_script;
    fpreal               husk_verbose_interval = 0;
    int			 menupriority = 0;
    fpreal		 multiplier = 1.0;
    HUSD_DepthStyle	 depthstyle = HUSD_DEPTH_OPENGL;
    bool		 needsdepth = false;
    bool		 needsselection = false;
    bool		 allowbackgroundupdate = true;
    bool		 aovsupport = true;
    bool		 viewportrenderer = false;
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
	if (options.hasOption("viewportrenderer"))
	    viewportrenderer = options.getOptionI("viewportrenderer");
	if (options.hasOption("drawmodesupport"))
	    drawmodesupport = options.getOptionI("drawmodesupport");
	if (options.hasOption("husk.fast-exit"))
	    husk_fastexit = options.getOptionI("husk.fast-exit");
        getStringMap(name, husk_metadata, options, "husk.metadata");
        getStringMap(name, statsdatapaths, options, "statsdatapaths");

        options.importOption("husk.verbose_callback", husk_verbose_script);
        options.importOption("husk.verbose_interval", husk_verbose_interval);

        for (auto &&it : custom)
        {
            if (options.hasOption(it.first))
            {
                it.second = options.getOptionEntry(it.first)->clone();
            }
        }

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
            husk_metadata,
            statsdatapaths,
            husk_verbose_script,
            husk_verbose_interval,
	    needsdepth,
	    needsselection,
	    allowbackgroundupdate,
            aovsupport,
            viewportrenderer,
            drawmodesupport,
	    husk_fastexit
	);
    }

    return HUSD_RendererInfo();
}

namespace
{
    static const UT_JSONValue *
    findJSONValue(const UT_JSONValue &dict,
            const HUSD_RendererInfo::StatsDataPaths &dpaths,
            const char *key)
    {
        auto it = dpaths.find(key);
        if (it != dpaths.end())
            key = it->second.c_str();

        UT_Set<const UT_JSONValue *>    matches;
        UT_JSONPath::find(matches, dict, key);
        if (matches.size() != 1)
            return nullptr;
        for (auto &it : matches)
            return it;
        return nullptr;
    }

    template <typename T>
    static bool
    valueFromJSON(const UT_JSONValue &dict,
            const HUSD_RendererInfo::StatsDataPaths &dpaths,
            const char *key,
            T &value)
    {
        const UT_JSONValue      *found = findJSONValue(dict, dpaths, key);
        return found ? found->import(value) : false;
    }
}

const UT_JSONValue *
HUSD_RendererInfo::findStatsData(const UT_JSONValue &stats_dict,
                                        const char *key) const
{
    if (!stats_dict.getMap())
        return nullptr;

    return findJSONValue(stats_dict, myStatsDataPaths, key);
}

static UT_StringHolder
prettyPrint(const UT_JSONValue &value)
{
    UT_WorkBuffer       tmp;
    if (value.getType() == UT_JSONValue::JSON_INT)
    {
        UT_String       str;
        str.itoaPretty(value.getI());
        return str;
    }
    else if (value.getType() == UT_JSONValue::JSON_REAL)
    {
        tmp.sprintf("%.3f", value.getF());
    }
    else
    {
        UT_AutoJSONWriter   w(tmp);
        w->setPrettyPrint(true);
        value.save(*w);
    }
    return tmp;
}

void
HUSD_RendererInfo::extractStatsData(UT_Options &opts,
        const UT_JSONValue &stats) const
{
    if (!stats.getMap())
        return;

    if (myStatsDataPaths.empty())       // Delegate hasn't set up paths yet
    {
        // Just convert the JSON dictionary to UT_Options verbatim
        opts.load(*stats.getMap(),
                true,   // do_clear
                false,  // allow_type
                true);  // allow_dict
        return;
    }

    // Since rendererName is referenced multiple times, pull out to a static
    static const char   *rendererName = "rendererName";

    // First, pull out the data needed by the viewport and husk
    double fval;
    for (const char *f_key : {
                                "percentDone",
                                "totalClockTime",
                                "totalUTime",
                                "totalSTime",
                             })
    {
        if (valueFromJSON(stats, myStatsDataPaths, f_key, fval))
            opts.setOptionF(UTmakeUnsafeRef(f_key), fval);
    }
    int64 ival;
    for (const char *i_key : {
                                "peakMemory",
                             })
    {
        if (valueFromJSON(stats, myStatsDataPaths, i_key, ival))
            opts.setOptionI(UTmakeUnsafeRef(i_key), ival);
    }
    UT_StringHolder     sval;
    for (const char *s_key : {
                                rendererName,
                                "rendererStage",
                                "renderProgressAnnotation",
                                "renderStatsAnnotation",
                             })
    {
        if (valueFromJSON(stats, myStatsDataPaths, s_key, sval))
            opts.setOptionS(UTmakeUnsafeRef(s_key), sval);
    }

    // Now, we go through the list of options the delegate has asked to display
    // in the viewport.
    for (auto &&key : myRenderViewStats)
    {
        const UT_JSONValue      *item = findJSONValue(stats, myStatsDataPaths, key);
        if (item)
        {
            const UT_StringHolder       *s = item->getStringHolder();
            opts.setOptionS(key, s ? *s : prettyPrint(*item));
        }
    }

    // Now, if there isn't a rendererName defined, we can stick in the
    // menu defined in the settings.
    if (!opts.hasOption(rendererName))
        opts.setOptionS(rendererName, menuLabel());
}
