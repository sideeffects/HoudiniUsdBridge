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
#include "XUSD_Tokens.h"
#include <PY/PY_Python.h>
#include <PY/PY_Result.h>
#include <FS/UT_DSO.h>
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

    HUSD_RendererInfo info;
    info.myIsValid = true;

    if (options.hasOption("valid"))
	info.myIsValid = options.getOptionB("valid");

    if (info.myIsValid)
    {
        info.myName = name;
        info.myDisplayName = displayname;
        info.myMenuLabel = displayname;
        info.myIsNativeRenderer =
            (name == HUSD_Constants::getHoudiniRendererPluginName());
        
        if (options.hasOption("menulabel"))
	    info.myMenuLabel = options.getOptionS("menulabel");
	if (options.hasOption("menupriority"))
	    info.myMenuPriority = options.getOptionI("menupriority");
	if (options.hasOption("complexitymultiplier"))
	    info.myDrawComplexityMultiplier = options.getOptionF("complexitymultiplier");
	if (options.hasOption("depthstyle"))
	    info.myDepthStyle = getDepthStyleFromStr(options.getOptionS("depthstyle"));
	if (options.hasOption("defaultpurposes"))
	    info.myDefaultPurposes = options.getOptionSArray("defaultpurposes");
	if (options.hasOption("restartrendersettings"))
	    info.myRestartRenderSettings = options.getOptionSArray("restartrendersettings");
	if (options.hasOption("restartcamerasettings"))
	    info.myRestartCameraSettings = options.getOptionSArray("restartcamerasettings");
	if (options.hasOption("viewstats"))
	    info.myRenderViewStats = options.getOptionSArray("viewstats");
	if (options.hasOption("needsdepth"))
	    info.myNeedsNativeDepthPass = options.getOptionI("needsdepth");
	if (options.hasOption("needsselection"))
	    info.myNeedsNativeSelectionPass = options.getOptionI("needsselection");
	if (options.hasOption("allowbackgroundupdate"))
	    info.myAllowBackgroundUpdate = options.getOptionI("allowbackgroundupdate");
        if (options.hasOption("destroyifdeactivated"))
            info.myDestroyIfDeactivated = options.getOptionI("destroyifdeactivated");
        if (options.hasOption("pauseonupdate"))
            info.myPauseOnUpdate = options.getOptionI("pauseonupdate");
	if (options.hasOption("aovsupport"))
	    info.myAovSupport = options.getOptionI("aovsupport");
	if (options.hasOption("viewportrenderer"))
	    info.myViewportRenderer = options.getOptionI("viewportrenderer");
	if (options.hasOption("drawmodesupport"))
	    info.myDrawModeSupport = options.getOptionI("drawmodesupport");
	if (options.hasOption("husk.fast-exit"))
	    info.myHuskFastExit = options.getOptionI("husk.fast-exit");
        if (options.hasOption("showinviewportmenu"))
            info.myShowInViewportMenu = options.getOptionI("showinviewportmenu");
        if (options.hasOption("preloadlibraries"))
            info.myPreloadLibraries = options.getOptionSArray("preloadlibraries");
        if (options.hasOption("showhydraprocedurals"))
            info.myShowHydraProcedurals = options.getOptionI("showhydraprocedurals");

        getStringMap(name, info.myHuskMetadata, options, "husk.metadata");
        getStringMap(name, info.myStatsDataPaths, options, "statsdatapaths");

        // The options will return an empty array if the option doesn't exist
        for (const auto &s : options.getOptionSArray("diskproducttypes"))
            info.myDiskProductTypes.insert(s);

        static constexpr UT_StringLit       theHuskStatsMetadataDefault("*");
        if (options.hasOption("husk.stats_metadata"))
            info.myHuskStatsMetadata = options.getOptionS("husk.stats_metadata");
        else
            info.myHuskStatsMetadata = theHuskStatsMetadataDefault.asHolder();

        options.importOption("husk.default_output", info.myHuskDefaultOutput);

        options.importOption("husk.verbose_callback", info.myHuskVerboseScript);
        options.importOption("husk.verbose_interval", info.myHuskVerboseInterval);

        for (auto &&it : custom)
        {
            if (options.hasOption(it.first))
            {
                it.second = options.getOptionEntry(it.first)->clone();
            }
        }
    }

    return info;
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
    UT_VERIFY_RETURN(myIsValid, nullptr);

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
    UT_VERIFY_RETURN_VOID(myIsValid);
    
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
    for (const PXR_NS::TfToken &f_key : {
                                PXR_NS::HusdHdRenderStatsTokens->percentDone,
                                PXR_NS::HusdHdRenderStatsTokens->totalClockTime,
                                PXR_NS::HusdHdRenderStatsTokens->totalUTime,
                                PXR_NS::HusdHdRenderStatsTokens->totalSTime,
                             })
    {
        if (valueFromJSON(stats, myStatsDataPaths, f_key.GetText(), fval))
            opts.setOptionF(UTmakeUnsafeRef(f_key.GetText()), fval);
    }
    int64 ival;
    for (const PXR_NS::TfToken &i_key : {
            PXR_NS::HusdHdRenderStatsTokens->peakMemory,
                             })
    {
        if (valueFromJSON(stats, myStatsDataPaths, i_key.GetText(), ival))
            opts.setOptionI(UTmakeUnsafeRef(i_key.GetText()), ival);
    }
    UT_StringHolder     sval;
    for (const PXR_NS::TfToken &s_key : {
                                PXR_NS::HusdHdRenderStatsTokens->rendererName,
                                PXR_NS::HusdHdRenderStatsTokens->rendererStage,
                                PXR_NS::HusdHdRenderStatsTokens->renderProgressAnnotation,
                                PXR_NS::HusdHdRenderStatsTokens->renderStatsAnnotation,
                             })
    {
        if (valueFromJSON(stats, myStatsDataPaths, s_key.GetText(), sval))
            opts.setOptionS(UTmakeUnsafeRef(s_key.GetText()), sval);
    }

    // Now, we go through the list of options the delegate has asked to display
    // in the viewport.
    for (auto &&key : myRenderViewStats)
    {
        const UT_JSONValue *item = findJSONValue(stats, myStatsDataPaths, key);
        if (item)
        {
            const UT_StringHolder *s = item->getStringHolder();
            opts.setOptionS(key, s ? *s : prettyPrint(*item));
        }
    }

    // Now, if there isn't a rendererName defined, we can stick in the
    // menu defined in the settings.
    if (!opts.hasOption(rendererName))
        opts.setOptionS(rendererName, menuLabel());
}

void
HUSD_RendererInfo::preloadLibraries() const
{
    UT_VERIFY_RETURN_VOID(myIsValid);
    
    for (auto &&lib : myPreloadLibraries)
    {
        UT_String libpath(lib.c_str());
        libpath.expandVariables();
        UT_DSO::loadDSO(libpath, true);
    }
}
