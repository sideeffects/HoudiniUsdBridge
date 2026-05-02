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

#include "HUSD_Utils.h"
#include "HUSD_Asset.h"
#include "HUSD_Constants.h"
#include "HUSD_ErrorScope.h"
#include "HUSD_Info.h"
#include "HUSD_LockedStage.h"
#include "HUSD_LockedStageRegistry.h"
#include "HUSD_PathSet.h"
#include "HUSD_PropertyHandle.h" // for ISCONNECTION and VALUETYPE_RAMP
#include "HUSD_TimeCode.h"
#include "HUSD_UniversalLogUsdSource.h"
#include "XUSD_AttributeUtils.h"
#include "XUSD_AutoCollection.h"
#include "XUSD_Data.h"
#include "XUSD_Utils.h"
#include <gusd/gusd.h>
#include <gusd/GU_PackedUSD.h>
#include <gusd/stageCache.h>
#include <OP/OP_Node.h>
#include <PI/PI_EditScriptedParms.h>
#include <IMG/IMG_File.h>
#include <PRM/PRM_SpareData.h>
#include <UT/UT_EnvControl.h>
#include <UT/UT_ErrorLog.h>
#include <UT/UT_Exit.h>
#include <UT/UT_Function.h>
#include <UT/UT_JSONParser.h>
#include <UT/UT_JSONValue.h>
#include <UT/UT_JSONValueArray.h>
#include <UT/UT_JSONValueMap.h>
#include <UT/UT_Lock.h>
#include <UT/UT_PathSearch.h>
#include <UT/UT_Set.h>
#include <UT/UT_StdUtil.h>
#include <UT/UT_String.h>
#include <UT/UT_StringArray.h>
#include <UT/UT_StringStream.h>
#include <UT/UT_VarEncode.h>
#include <UT/UT_WorkArgs.h>
#include <tools/henv.h>
#include <tools/hpath.h>
#include <pxr/pxr.h>
#include <pxr/base/tf/unicodeUtils.h>
#include <pxr/usd/ar/resolver.h>
#include <pxr/usd/pcp/layerStack.h> 
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/sdf/variableExpression.h>
#include <pxr/usd/usd/collectionAPI.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/tokens.h>
#include <pxr/usd/usd/primCompositionQuery.h> 
#include <pxr/usd/usdGeom/xformOp.h>
#include <pxr/usd/usdGeom/xformable.h>
#include <pxr/usd/usdShade/connectableAPI.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>
#include <pxr/usd/usdShade/material.h>
#include <pxr/usd/usdShade/shader.h>
#include <pxr/usd/usdShade/tokens.h>
#include <iostream>

PXR_NAMESPACE_USING_DIRECTIVE
using namespace UT::Literal;

namespace
{
    typedef UT_Function<UT_StringHolder(UT_StringHolder)> ModifyPathFn;
    UT_Map<UT_IStream *, HUSD_Asset *> theAssetMap;
    UT_Lock theAssetMapLock;
    HUSD_LopStageResolver theLopStageResolver = nullptr;
    UT_Set<HUSD_LockedStagePtr> theHoldLockedStages;
    UT_Lock theHoldLockedStagesLock;
    int theStageCacheReaderCounter = 0;

    UT_IStream *assetOpen(const UT_StringRef &filepath)
    {
        HUSD_Asset *asset = new HUSD_Asset(filepath);

        if(asset->isValid())
        {
            UT_IStream *is = asset->newStream();

            if(is && !is->isError())
            {
                UT_AutoLock lock(theAssetMapLock);
                theAssetMap[is] = asset;
                return is;
            }
        }
        delete asset;

        return nullptr;
    }

    void assetClose(UT_IStream *is)
    {
        HUSD_Asset *asset = nullptr;

        if (is)
        {
            UT_AutoLock lock(theAssetMapLock);

            auto entry = theAssetMap.find(is);
            if(entry != theAssetMap.end())
            {
                asset = entry->second;
                theAssetMap.erase(entry);
            }
            else
            {
                UT_ASSERT(!"Tried to close invalid HUSD_Asset.");
            }
        }
        delete is;
        delete asset;
    }
}

UT_REGISTERUNIVERSALLOGSOURCE(HUSD_UniversalLogUsdSource);

UT_StringHolder
husdLopStageResolver(const UT_StringRef &path)
{
    if (theLopStageResolver)
    {
        HUSD_LockedStagePtr locked_stage;

        // Use the LOP Stage Resolver function registered by the LOP library
        // to generate an HUSD_LockedStagePtr from the LOP node.
        locked_stage = theLopStageResolver(path);
        if (locked_stage)
        {
            // Add the locked stage pointer to a list of locked stage shared
            // pointers. These shared pointers will keep the locked stage
            // alive until all GusdStageCacheReader/Writer objects have been
            // destroyed. This is necessary to keep the locked stage alive
            // long enough for any USD packed primitives to register
            // themselves (which will create a more permanent copy of this
            // locked stage shared pointer).
            UT_AutoLock lockscope(theHoldLockedStagesLock);
            theHoldLockedStages.insert(locked_stage);
            return locked_stage->getStageCacheIdentifier();
        }
    }

    return UT_StringHolder::theEmptyString;
}

void
husdStageCacheReaderTracker(bool addreader)
{
    UT_Set<HUSD_LockedStagePtr> locked_stages;

    {
        UT_AutoLock lockscope(theHoldLockedStagesLock);

        // After deleting the last GusdStageCacheReader/Writer object, clear
        // the array of temporary Locked Stage shared pointers. Do this using
        // a swap with an empty array so that the locked stages don't get
        // destroyed until we have released theHoldLockedStageLock.
        theStageCacheReaderCounter += (addreader ? 1 : -1);
        if (theStageCacheReaderCounter == 0)
            locked_stages.swap(theHoldLockedStages);
    }
}

namespace
{
    class materialXPathHelper
    {
    public:
        materialXPathHelper()
        {
            auto s = UT_PathSearch::getInstance(UT_HOUDINI_PATH);
            UT_ASSERT_P(s);
            myMaterialX = findDirs(*s, "materialx");
            myLibraries = findDirs(*s, "materialx/libraries");
        }
        const UT_StringHolder   &materialx() const { return myMaterialX; }
        const UT_StringHolder   &libraries() const { return myLibraries; }

        void    setVariable(const char *varname, bool lib) const
        {
            const UT_StringHolder       path = lib ? myLibraries : myMaterialX;
            HoudiniSetenv(varname, path.c_str());
            UT_ErrorLog::format(8, "Setting {} to '{}'", varname, path);
        }
    private:
        UT_StringHolder findDirs(const UT_PathSearch &search,
                const char *pattern) const
        {
            UT_WorkBuffer       var;
            UT_StringArray      paths;
            search.findAllDirectories(pattern, paths);
            for (const auto &p : paths)
            {
                if (var.length())
                    var.append(PATH_SEP_CHAR);
                var.append(p);
            }
            return UT_StringHolder(var);
        }
        UT_StringHolder myMaterialX;
        UT_StringHolder myLibraries;
    };

    static const materialXPathHelper &
    materialxHelper()
    {
        static materialXPathHelper      helper;
        return helper;
    }
}

void
HUSDinitialize()
{
    static bool theInitialized = false;

    if (!theInitialized)
    {
        //UTdebugFormat("Initializing");

        // In case the user hasn't set a MATERIALX_SEARCH_PATH value, or the
        // other USD-specific MaterialX paths, set one here to point to the
        // MaterialX libraries that ship with Houdini.
        const char *MATERIALX_SEARCH_PATH =
            "MATERIALX_SEARCH_PATH";
        const char *PXR_MTLX_STDLIB_SEARCH_PATHS =
            "PXR_MTLX_STDLIB_SEARCH_PATHS";
        const char *PXR_AR_DEFAULT_SEARCH_PATH =
            "PXR_AR_DEFAULT_SEARCH_PATH";

        if (!HoudiniGetenv(MATERIALX_SEARCH_PATH))
        {
            materialxHelper().setVariable(MATERIALX_SEARCH_PATH,
                    false);
        }
        if (!HoudiniGetenv(PXR_MTLX_STDLIB_SEARCH_PATHS))
        {
            materialxHelper().setVariable(PXR_MTLX_STDLIB_SEARCH_PATHS,
                    true);
        }
        if (!HoudiniGetenv(PXR_AR_DEFAULT_SEARCH_PATH))
        {
            HoudiniSetenv(PXR_AR_DEFAULT_SEARCH_PATH,
                UT_EnvControl::getString(ENV_HFS));
        }

        // In case Gusd hasn't been initialized yet, do it here because that
        // function adds plugin registry directories to the USD library.
        GusdInit();
        GusdStageCache::SetLopStageResolver(husdLopStageResolver);
        GusdStageCache::SetStageCacheReaderTracker(husdStageCacheReaderTracker);
        GusdGU_PackedUSD::setPackedUSDTracker(
            HUSD_LockedStageRegistry::packedUSDTracker);
        UT_Exit::addExitCallback(HUSD_LockedStageRegistry::exitCallback);
        XUSD_AutoCollection::registerPlugins();
        IMG_File::setFileHooks(assetOpen, assetClose);
        theInitialized = true;
    }
}

void
HUSDsetLopStageResolver(HUSD_LopStageResolver resolver)
{
    theLopStageResolver = resolver;
}

bool
HUSDsplitLopStageIdentifier(const UT_StringRef &identifier,
        OP_Node *&lop,
        int &output_index,
        bool &split_layers,
        fpreal &t,
        UT_Options &opts)
{
    return GusdStageCache::SplitLopStageIdentifier(identifier,
        lop, output_index, split_layers, t, opts);
}

bool
HUSDisValidUsdName(const UT_StringRef &name)
{
    return SdfPath::IsValidIdentifier(name.c_str());
}

bool
HUSDmakeValidUsdName(UT_String &name, bool addwarnings)
{
    if (!name.isstring() || SdfPath::IsValidIdentifier(name.c_str()))
	return false;

    static constexpr TfUtf8CodePoint theUnderscore =
        TfUtf8CodePointFromAscii('_');
    bool changed = false;
    bool first = true;
    UT_OStringStream str;
    for (auto cp : TfUtf8CodePointView{name.c_str()})
    {
        const bool cp_allowed = first
            ? (cp == theUnderscore || TfIsUtf8CodePointXidStart(cp))
            : TfIsUtf8CodePointXidContinue(cp);
        if (!cp_allowed) {
            str << '_';
            changed = true;
        }
        else {
            str << cp;
        }

        first = false;
    }

    if (changed)
    {
        name.harden(str.str().buffer(), str.str().length());
        if (addwarnings)
            HUSD_ErrorScope::addWarning(
                HUSD_ERR_FIXED_INVALID_NAME, name.c_str());
    }
    else
    {
        UT_ASSERT(!"HUSDmakeValidUsdName handed an identifier that "
            "failed the validation check, but we didn't change it.");
    }

    return changed;
}

UT_StringHolder
HUSDgetValidUsdName(OP_Node &node)
{
    UT_String	 name(node.getName());

    HUSDmakeValidUsdName(name, false);

    return name;
}

bool
HUSDisValidUsdPath(const UT_StringRef &path)
{
    return SdfPath::IsValidPathString(path.toStdString());
}

bool
HUSDmakeValidUsdPath(UT_String &path, bool addwarnings)
{
    return HUSDmakeValidUsdPath(path, addwarnings, false);
}

bool
HUSDmakeValidUsdPath(UT_String &path, bool addwarnings, bool allow_relative)
{
    if (!path.isstring())
	return false;

    UT_WorkArgs		 args;
    UT_StringArray	 changed_components;
    UT_String		 tokenstr(path);
    bool		 changed = false;
    bool		 fixed = false;
    bool		 rebuild_path = false;
    bool                 is_relative_path = false;

    // Trim off any trailing slashes.
    while (path.length() > 1 && path.endsWith("/"))
    {
	path.removeLast();
	changed = true;
    }
    // Make sure the path starts with a "/". If not, we will rebuild it.
    if (!path.startsWith("/"))
    {
        if (allow_relative)
            is_relative_path = true;
        else
            rebuild_path = true;
    }
    // If we have any double-slashes, we need to rebuild the path.
    if (path.fcontain("//", false))
        rebuild_path = true;

    // Split the path into components so we can look for any invalid names
    // in any of the components.
    tokenstr.tokenize(args, '/');
    changed_components.setSize(args.getArgc());
    for (int i = 0, n = args.getArgc(); i < n; i++)
    {
	UT_String	 arg(args.getArg(i));

	if (arg == "." || arg == "..")
	{
	    // Subsequent "." or ".." components get stashed as a changed
	    // component. They will be handled specially when rebuilding the
	    // modified path.
	    changed_components(i) = arg;
	    rebuild_path = true;
	}
	else if (HUSDmakeValidUsdName(arg, false))
	{
	    changed_components(i) = arg;
	    rebuild_path = true;
	    fixed = true;
	}
    }

    if (rebuild_path)
    {
	UT_WorkBuffer	 outpath;

	changed = true;
	for (int i = 0, n = args.getArgc(); i < n; i++)
	{
            // Append a "/" to any path that already has a component, or an
            // empty string (unless we were passed an allowed relative path).
            if (!is_relative_path || outpath.length() > 0)
                if (outpath.length() == 0 || outpath.last() != '/')
                    outpath.append('/');

	    if (changed_components(i).isstring())
	    {
                // Do nothing with a "."... it has no effect.
		if (changed_components(i) == ".")
		{
		}
                // A ".." should erase the last path component. In a full path,
                // we back up only as far as the first "/", and never append
                // the ".." component. In a relative path, we back up as far as
                // the last "../", then append the ".." component.
		else if (changed_components(i) == ".." &&
                         (!allow_relative ||
                           (outpath.length() > 0 &&
                            (outpath.length() < 3 ||
                             strcmp(outpath.end() - 3, "../") != 0))))
		{
		    // Get rid of the trailing slash we add at the start of
		    // each path component (unless the path is exactly "/").
		    if (outpath.length() > 1)
			outpath.backup(1);
		    // Back up to the previous slash.
		    outpath.backupTo('/');
		    // Unless the path is just "/", we want to back up one
		    // more character to get rid of the "/" itself.
		    if (outpath.length() > 1)
			outpath.backup(1);
		}
                // For any component other than "." or "..", append the
                // validated component.
		else
		    outpath.append(changed_components(i));
	    }
	    else if (UTisstring(args.getArg(i)))
		outpath.append(args.getArg(i));
	}
	// Trim off any trailing slashes.
	while (outpath.length() > 1 && outpath.last() == '/')
	    outpath.backup(1);
	outpath.stealIntoString(path);
    }

    if (fixed && addwarnings)
	HUSD_ErrorScope::addWarning(
	    HUSD_ERR_FIXED_INVALID_PATH, path.c_str());

    return changed;
}

bool
HUSDmakeValidUsdPathOrDefaultPrim(UT_String &path, bool addwarnings)
{
    if (path == HUSD_Constants::getAutomaticPrimIdentifier() ||
        path == HUSD_Constants::getDefaultPrimIdentifier())
        return false;

    return HUSDmakeValidUsdPath(path, addwarnings);
}

bool
HUSDmakeUniqueUsdPath(UT_String &path, const HUSD_AutoAnyLock &lock,
	const UT_StringRef &suffix)
{
    if (!lock.constData() || !lock.constData()->isStageValid())
	return false;

    auto stage	    = lock.constData()->stage();
    auto testpath   = HUSDgetSdfPath(path);
    if (!stage->GetPrimAtPath(testpath))
	return false;

    path.append(suffix);
    do
    {
	path.incrementNumberedName();
	testpath = HUSDgetSdfPath(path);
    }
    while (stage->GetPrimAtPath(testpath));

    return true;
}

UT_StringHolder
HUSDgetValidUsdPath(OP_Node &node)
{
    UT_String	 path(node.getFullPath());

    HUSDmakeValidUsdPath(path, false);

    return path;
}

bool
HUSDmakeValidUsdPropertyName(UT_String &name, bool addwarnings)
{
    if (!name.isstring() || SdfPath::IsValidNamespacedIdentifier(name.c_str()))
        return false;

    static constexpr TfUtf8CodePoint theUnderscore =
        TfUtf8CodePointFromAscii('_');
    static constexpr TfUtf8CodePoint theColon =
        TfUtf8CodePointFromAscii(':');
    bool changed = false;
    bool first = true;
    UT_OStringStream str;

    // We can't end with a ":".
    while (name.endsWith(":"))
    {
        name.removeLast();
        changed = true;
    }
    // Replace any sequence of ":"s with a single ":".
    while (name.substitute("::", ":", 1))
        changed = true;

    for (auto cp : TfUtf8CodePointView{name.c_str()})
    {
        const bool cp_allowed = first
            ? (cp == theUnderscore || TfIsUtf8CodePointXidStart(cp))
            : (cp == theColon || TfIsUtf8CodePointXidContinue(cp));
        if (!cp_allowed) {
            str << '_';
            changed = true;
        }
        else {
            str << cp;
        }

        first = false;
    }

    if (changed)
    {
        name.harden(str.str().buffer(), str.str().length());
        if (addwarnings)
            HUSD_ErrorScope::addWarning(
                HUSD_ERR_FIXED_INVALID_NAME, name.c_str());
    }
    else
    {
        UT_ASSERT(!"HUSDmakeValidUsdPropertyName handed an identifier that "
            "failed the validation check, but we didn't change it.");
    }

    return changed;
}

bool
HUSDmakeValidVariantName(UT_String &name, bool allowexprs, bool addwarnings)
{
    if (!name.isstring() ||
        SdfSchemaBase::IsValidVariantIdentifier(name.c_str()))
	return false;

    if (allowexprs && SdfVariableExpression::IsExpression(name.toStdString()))
    {
        // If expressions are allowed, and the passed in value is an
        // expression, we never change the value. But we may still add an
        // error if the expression can't be evaluated.
        if (addwarnings)
        {
            SdfVariableExpression expr(name.toStdString());
            auto result = expr.Evaluate(VtDictionary());
            if (!result.errors.empty())
            {
                UT_StringArray errors;
                UT_WorkBuffer buf;
                UTarrayFromStdVectorOfStrings(errors, result.errors);
                buf.append(errors, "\n    ");
                HUSD_ErrorScope::addError(
                    HUSD_ERR_INVALID_VARIABLE_EXPRESSION, buf.buffer());
            }
        }

        return false;
    }

    static constexpr TfUtf8CodePoint theDot =
        TfUtf8CodePointFromAscii('.');
    static constexpr TfUtf8CodePoint theHyphen =
        TfUtf8CodePointFromAscii('-');
    static constexpr TfUtf8CodePoint thePipe =
        TfUtf8CodePointFromAscii('|');
    bool changed = false;
    bool first = true;
    UT_OStringStream str;

    for (auto cp : TfUtf8CodePointView{name.c_str()})
    {
        // Rather weird rules for variant names...
        bool cp_allowed = (
            cp == theHyphen || cp == thePipe ||
            TfIsUtf8CodePointXidContinue(cp));
        if (first && !cp_allowed)
            cp_allowed = (cp == theDot);

        if (!cp_allowed) {
            str << '_';
            changed = true;
        }
        else {
            str << cp;
        }

        first = false;
    }

    if (changed)
    {
        name.harden(str.str().buffer(), str.str().length());
        if (addwarnings)
            HUSD_ErrorScope::addWarning(
                HUSD_ERR_FIXED_INVALID_VARIANT_NAME, name.c_str());
    }
    else
    {
        UT_ASSERT(!"HUSDmakeValidVariantName handed an identifier that "
            "failed the validation check, but we didn't change it.");
    }

    return changed;
}

bool
HUSDmakeValidDefaultPrim(UT_String &default_prim, bool addwarnings)
{
    // A valid identifier is automatically accepted. A valid path is not
    // automatically accepted because if it is a root prim path, we want
    // to strip off the leading "/".
    if (!default_prim.isstring() ||
        SdfSchemaBase::IsValidIdentifier(default_prim.c_str()))
        return false;

    bool changed = default_prim.trimBoundingSpace();

    // Strip off all but one leading slash. This is to maintain exact
    // backward compatibility with the code from before default prims
    // could have non-root paths.
    while (default_prim.startsWith("//"))
    {
        default_prim.eraseHead(1);
        changed = true;
    }
    // If we are given a root prim path, remove the leading "/". This makes
    // the resulting layer more likely to work with older USD versions, and
    // prevents pointless changes to generated output compared to the old
    // method which only allowed root prims.
    if (default_prim.lastChar('/') == default_prim.c_str())
    {
        default_prim.eraseHead(1);
        changed = true;
    }
    // Note that changes made up to this point are not considered "warnable"
    // because they are inconsequential (changes to syntax, not intent). Any
    // warnings wll be added by the name/path validators below.

    // If the string starts with a "/" treat it as a path, otherwise treat
    // it as a simple identifier.
    if (default_prim.startsWith("/"))
        changed |= HUSDmakeValidUsdPath(default_prim, addwarnings);
    else
        changed |= HUSDmakeValidUsdName(default_prim, addwarnings);

    return changed;
}

UT_StringHolder
HUSDgetUsdName(const UT_StringRef &primpath)
{
    SdfPath sdf_path(primpath.toStdString());

    return UT_StringHolder( sdf_path.GetName() );
}

UT_StringHolder
HUSDgetUsdParentPath(const UT_StringRef &primpath)
{
    SdfPath sdf_path(primpath.toStdString());

    return UT_StringHolder( sdf_path.GetParentPath().GetString() );
}

UT_StringHolder
HUSDremoveInstanceId(const UT_StringHolder &selectionpath)
{
    int instidstart = selectionpath.findCharIndex('[');
    if (instidstart < 0)
        return selectionpath;

    UT_String newpath(selectionpath);
    if (newpath.findChar('['))
    {
        newpath.harden();
        *newpath.findChar('[') = '\0';
    }
    return UT_StringHolder(newpath);
}

bool
HUSDsplitInstanceIdAndPath(const UT_StringHolder &selectionpath,
        UT_StringHolder &primpath,
        int64 &instanceid)
{
    const char *cstr = selectionpath.c_str();
    const char *leftBracket = strrchr(cstr, '[');
    if (!leftBracket)
    {
        primpath = selectionpath;
        instanceid = -1;
        return false;
    }

    const std::string basePath(cstr, static_cast<size_t>(leftBracket - cstr));
    primpath = UT_StringHolder(basePath.c_str());
    instanceid = SYSatoi64(leftBracket + 1);
    return true;
}

UT_StringHolder
HUSDmakeValidPathExpression(const UT_StringHolder &path_expr)
{
    if (path_expr.findCharIndex('\n') >= 0 ||
        path_expr.findCharIndex('\t') >= 0)
    {
        UT_String pathexpressionstr(path_expr.c_str());
        pathexpressionstr.substitute('\n', ' ');
        pathexpressionstr.substitute('\t', ' ');

        return pathexpressionstr;
    }

    return path_expr;
}

void
HUSDgetMinimalPathsForInheritableProperty(
        bool skip_point_instancers,
        const HUSD_AutoAnyLock &lock,
        HUSD_PathSet &paths)
{
    if (lock.constData() && lock.constData()->isStageValid())
    {
        HUSDgetMinimalPathsForInheritableProperty(
            skip_point_instancers,
            lock.constData()->stage(),
            paths.sdfPathSet());
    }
}

UT_StringHolder
HUSDgetPrimTypeAlias(const UT_StringRef &primtype)
{
    if (primtype.isstring())
    {
	// Note, we call FindDerivedByName() instead of FindByName() so that
	// we find aliases too. Otherwise we find "UsdGeomCube" but not "Cube".
	TfType const &tfprimtype = 
	    TfType::Find<UsdSchemaBase>().FindDerivedByName(
		    primtype.toStdString());

	if (!tfprimtype.IsUnknown())
	{
	    std::vector<std::string> aliases;

	    aliases = TfType::Find<UsdSchemaBase>().GetAliases(tfprimtype);
	    if (!aliases.empty())
		return aliases.front();
	    else
		return tfprimtype.GetTypeName();
	}
    }

    return UT_StringHolder::theEmptyString;
}

bool
HUSDapplyStripLayerResponse(HUSD_StripLayerResponse response)
{
    if (response == HUSD_WARN_STRIPPED_LAYERS)
	HUSD_ErrorScope::addWarning(HUSD_ERR_LAYERS_STRIPPED);
    else if (response == HUSD_ERROR_STRIPPED_LAYERS)
	HUSD_ErrorScope::addError(HUSD_ERR_LAYERS_STRIPPED);

    return (response == HUSD_ERROR_STRIPPED_LAYERS);
}

bool
HUSDgetXformTypeAndSuffix(HUSD_XformType &type, UT_StringHolder &name_suffix, 
	const UT_StringRef& full_name)
{
    auto tokens = SdfPath::TokenizeIdentifierAsTokens(full_name.toStdString());

    UT_ASSERT(tokens.size() >= 2);
    if( tokens.size() < 2 )
	return false;

    UT_ASSERT(tokens[0].GetString() == "xformOp");
    type = (HUSD_XformType) UsdGeomXformOp::GetOpTypeEnum( tokens[1] );
    name_suffix = (tokens.size() > 2) ? UT_StringHolder( tokens[2] ) 
	: UT_StringHolder();

    return true;
}

HUSD_XformType
HUSDgetXformType(const UT_StringRef &full_name)
{
    HUSD_XformType  type;
    UT_StringHolder name_suffix;

    if( !HUSDgetXformTypeAndSuffix( type, name_suffix, full_name ))
	return HUSD_XformType::Invalid;

    return type;
}

UT_StringHolder
HUSDgetXformSuffix( const UT_StringRef &full_name )
{
    HUSD_XformType  type;
    UT_StringHolder name_suffix;

    if( !HUSDgetXformTypeAndSuffix( type, name_suffix, full_name ))
	return UT_StringHolder();

    return name_suffix;
}
    
UT_StringHolder
HUSDgetXformName( HUSD_XformType type, const UT_StringRef &name_suffix ) 
{
    auto xform_type   = (UsdGeomXformOp::Type) type;
    auto xform_suffix = TfToken( name_suffix.toStdString() );
    auto xform_name   = UsdGeomXformOp::GetOpName( xform_type, xform_suffix );

    return UT_StringHolder( xform_name.GetString() );
}

bool
HUSDisXformAttribute(const UT_StringRef &attr,
	UT_StringHolder *xform_type,
	UT_StringHolder *xform_name)
{
    if (UsdGeomXformOp::IsXformOp(TfToken(attr.toStdString())))
    {
	if (xform_type || xform_name)
	{
	    UT_String		 attrstr(attr.c_str());
	    UT_String		 typestr;
	    const char		*typeend;

	    UT_ASSERT(attrstr.startsWith("xformOp:"));
	    typestr = attrstr.findChar(':') + 1;
	    typeend = typestr.findChar(':');
	    if (typeend)
	    {
		if (xform_type)
		    *xform_type = UT_StringHolder(typestr.c_str(),
			(size_t)(typeend - typestr.c_str()));
		if (xform_name)
		    *xform_name = (typeend+1);
	    }
	    else
	    {
		if (xform_type)
		    *xform_type = typestr;
		if (xform_name)
		    xform_name->clear();
	    }
	}

	return true;
    }

    return false;
}

UT_StringHolder
HUSDmakeCollectionPath( const UT_StringRef &prim_path,
	const UT_StringRef &collection_name)
{
    SdfPath sdf_path(prim_path.toStdString());

    // Pretty much as SdfPath::JoinIdentifier().
    UT_WorkBuffer buffer;
    buffer.append( UsdTokens->collection.GetString() );
    buffer.append( SDF_PATH_NS_DELIMITER_CHAR );
    buffer.append( collection_name );

    TfToken suffix( buffer.toStdString() );
    SdfPath collection_path( sdf_path.AppendProperty( suffix ));

    return UT_StringHolder( collection_path.GetString() );
}

bool
HUSDsplitCollectionPath( UT_StringHolder &prim_path,
	UT_StringHolder &collection_name, const UT_StringRef &collection_path)
{
    if( !HUSDisValidCollectionPath( collection_path ))
	return false;

    SdfPath sdf_path(collection_path.toStdString());
    prim_path = sdf_path.GetPrimPath().GetString();
    collection_name = SdfPath::StripNamespace(sdf_path.GetToken()).GetString();
    return true;
}

bool
HUSDisValidCollectionPath(const UT_StringRef &collection_path)
{
    SdfPath sdf_path(collection_path.toStdString());
    TfToken base_name;

    return UsdCollectionAPI::IsCollectionAPIPath( sdf_path, &base_name );
}

UT_StringHolder
HUSDmakePropertyPath(const UT_StringRef &prim_path, const UT_StringRef &name)
{
    SdfPath sdf_path( prim_path.toStdString() );
    TfToken tf_name( name.toStdString() );
    SdfPath property_path( sdf_path.AppendProperty( tf_name ));

    return UT_StringHolder( property_path.GetString() );
}

UT_StringHolder
HUSDmakeAttributePath(const UT_StringRef &prim_path, const UT_StringRef &name)
{
    return HUSDmakePropertyPath(prim_path, name);
}

UT_StringHolder
HUSDmakeRelationshipPath(const UT_StringRef &prim_path, const UT_StringRef&name)
{
    return HUSDmakePropertyPath(prim_path, name);
}

std::pair<UT_StringHolder, UT_StringHolder>
HUSDsplitPropertyPath(const UT_StringRef &property_path)
{
    UT_StringHolder prim_path, prop_name;

    if (property_path)
    {
        SdfPath sdf_path(HUSDgetSdfPath(property_path));
        prim_path = sdf_path.GetPrimPath().GetString();
        prop_name = sdf_path.GetName();
    }

    return std::make_pair(prim_path, prop_name);
}

UT_StringHolder 
HUSDgetPrimvarAttribName(const UT_StringRef &primvar_name)
{
    UT_WorkBuffer buffer;
    buffer.append( "primvars" ); // primvar.cpp: _tokens->primvarsPrefix
    buffer.append( SDF_PATH_NS_DELIMITER_CHAR );
    buffer.append( primvar_name );

    return UT_StringHolder(buffer);
}

UT_StringHolder
HUSDgetAttribTypeName(const PI_EditScriptedParm &parm)
{
    if( parm.getIsRampParm() )
        return HUSD_PROPERTY_VALUETYPE_RAMP;
   
    SdfValueTypeName sdftype = HUSDgetAttribSdfTypeName( parm );
    if( sdftype != SdfValueTypeName() )
	return UT_StringHolder( sdftype.GetAsToken().GetString() );

    return UT_StringHolder();
}

HUSD_TimeCode
HUSDgetEffectiveTimeCode( const HUSD_TimeCode &timecode,
	HUSD_TimeSampling sampling )
{
    // If there was any time sampling involved (single or multiple), 
    // we want to author a value at a specific time sample. Failing to do so,
    // stiching the stages won't work if we author a default value.
    // Also, an attribute may already have time sample, so setting at default
    // time sample would have no effect (non-default trumps default time code).
    if( sampling != HUSD_TimeSampling::NONE )
	return timecode.getNonDefaultTimeCode();

    // Otherwise, a default time code is fine, so we don't meddle with timecode.
    return timecode;
}

bool
HUSDisTimeVarying(HUSD_TimeSampling time_sampling) 
{
    return time_sampling == HUSD_TimeSampling::MULTIPLE;
}

bool
HUSDisTimeSampled(HUSD_TimeSampling time_sampling)
{
    return time_sampling != HUSD_TimeSampling::NONE;
}

bool
HUSDsetParmFromProperty(const HUSD_DataHandle &data,
        const UT_StringRef &primpath,
        const UT_StringRef &attribname,
        const HUSD_TimeCode &tc,
        PRM_Parm &parm,
        HUSD_TimeSampling &timesampling)
{
    UT_UniquePtr<HUSD_AutoAnyLock>   lock(new HUSD_AutoReadLock(data));
    UsdPrim                          prim;

    if (primpath.isstring() &&
	lock->constData() && lock->constData()->isStageValid())
    {
	SdfPath sdfpath(HUSDgetSdfPath(primpath));
	prim = lock->constData()->stage()->GetPrimAtPath(sdfpath);
    }

    if (!prim)
	return false;

    auto attrib = prim.GetAttribute(TfToken(attribname.toStdString()));

    // If parmtag is not defined, assume the parm is not a connection specifier.
    const PRM_SpareData *tags = parm.getSparePtr();
    bool is_connection_parm = (tags && 
        (UT_StringWrap("1") ==
            tags->getValue(HUSD_PROPERTY_ISCONNECTION)));
    bool is_collection_parm = (tags &&
        (UT_StringWrap(HUSD_PROPERTY_VALUETYPE_COLLECTION) ==
            tags->getValue(HUSD_PROPERTY_VALUETYPE)));

    if (attrib && is_connection_parm)
    {
	UsdShadeConnectionSourceInfo src_info;

        bool ok = true;
	if (!HUSDgetFirstConnectedSrc(attrib, src_info))
	{
	    lock.reset();
	    parm.setValue( 0, "", CH_STRING_LITERAL ); // No source: clear parm
	}
        else
            ok = HUSDsetConnectionNodeParm(parm, src_info, true, &lock);

        return ok;
    }
    if (is_collection_parm)
    {
        TfToken collname;
        UsdCollectionAPI::IsCollectionAPIPath(
            prim.GetPath().AppendProperty(TfToken(attribname)), &collname);
        auto coll = UsdCollectionAPI::GetCollection(prim, collname);
        bool ok = true;

        if (coll)
        {
            auto get_parm_from_names_fn = [&](const char *suffix) {
                UT_WorkBuffer parmname(attribname);
                if (UTisstring(suffix))
                {
                    parmname.append('_');
                    parmname.append(suffix);
                }
                return parm.getOwner()->getParmPtr(
                    UT_VarEncode::encodeParm(parmname.buffer()));
            };
            bool is_path_expr = coll.IsInExpressionMode();
            SdfPathExpression path_expr;
            bool include_root = false;
            SdfPathVector include_rel_targets;
            bool has_authored_exclude_targets = false;
            SdfPathVector exclude_rel_targets;
            TfToken expansion_rule;
            PRM_Parm *collparm = nullptr;

            // Putting a collection into a parm requires reading a bunch of
            // attributes and setting values in a bunch of parameters. To make
            // this safe, we must gather all the information first, then
            // release the read lock, then make all our setValue calls.
            coll.GetExpansionRuleAttr().Get<TfToken>(&expansion_rule);
            if (is_path_expr)
            {
                if (coll.GetMembershipExpressionAttr())
                    coll.GetMembershipExpressionAttr().
                        Get<SdfPathExpression>(&path_expr);
            }
            else
            {
                if (coll.GetIncludeRootAttr())
                    coll.GetIncludeRootAttr().Get<bool>(&include_root);
                if (coll.GetIncludesRel())
                    coll.GetIncludesRel().GetTargets(&include_rel_targets);
                if (coll.GetExcludesRel())
                {
                    has_authored_exclude_targets =
                        coll.GetExcludesRel().HasAuthoredTargets();
                    coll.GetExcludesRel().GetTargets(&exclude_rel_targets);
                }
            }
            lock.reset();

            // Always allow instance proxies when setting parms from a prim.
            if ((collparm = get_parm_from_names_fn("allowinstanceproxies")))
                ok &= collparm->setValue(0.0, true, true);
            // Record whether this collection is in expression mode.
            if ((collparm = get_parm_from_names_fn("ispathexpression")))
                ok &= collparm->setValue(0.0, is_path_expr, true);
            // Set the expansion rule. Ignored by path expression mode,
            // but still worth setting the value.
            if (!expansion_rule.IsEmpty() &&
                ((collparm = get_parm_from_names_fn("expansionrule"))))
                collparm->setValue(0.0, expansion_rule.GetText(),
                    CH_STRING_LITERAL, true);

            if (is_path_expr)
            {
                if ((collparm = get_parm_from_names_fn(nullptr)))
                    collparm->setValue(0.0, path_expr.GetText().c_str(),
                        CH_STRING_LITERAL, true);
                // Turn off "exclusions" in path expression mode. We support
                // it for authoring the collection, but we can't extract a
                // value for this parameter from an existing path expression.
                if ((collparm = get_parm_from_names_fn("doexclusions")))
                    collparm->setValue(0.0, false, true);
            }
            else
            {

                if ((collparm = get_parm_from_names_fn("excludepattern")))
                    ok &= HUSDsetRelationshipNodeParm(*collparm,
                        exclude_rel_targets, true);
                if ((collparm = get_parm_from_names_fn("doexclusions")))
                    collparm->setValue(0.0, has_authored_exclude_targets, true);

                if ((collparm = get_parm_from_names_fn(nullptr)))
                    ok &= HUSDsetRelationshipNodeParm(*collparm,
                        include_rel_targets, true);
                // Special case for "includeRoot" - we express this in
                // parms by putting the root path in the patter string.
                if (include_root)
                {
                    UT_String pattern;
                    collparm->getValue(0.0, pattern, 0, false, SYSgetSTID());
                    pattern.insert(0, "/ ");
                    collparm->setValue(0.0, pattern, CH_STRING_LITERAL, true);
                }
            }
        }

        return ok;
    }

    if (attrib)
    {
        HUSDupdateValueTimeSampling(timesampling, attrib);
        UsdTimeCode usdtc = HUSDgetNonDefaultUsdTimeCode(tc);

        return HUSDsetNodeParm(parm, attrib, usdtc, true, &lock);
    }

    auto rel = prim.GetRelationship(TfToken(attribname.toStdString()));
    if (rel)
    {
        SdfPathVector rel_targets;
        rel.GetTargets(&rel_targets);
        return HUSDsetRelationshipNodeParm(parm, rel_targets, true, &lock);
    }

    return false;
}

bool
HUSDpartitionShadePrims(const HUSD_AutoAnyLock &anylock,
        const HUSD_PathSet &primpaths,
        UT_StringArray &shadeprimpaths,
        UT_StringArray &geoprimpaths,
        bool include_bound_materials,
        bool use_shader_for_mat_with_no_inputs)
{
    auto indata = anylock.constData();
    if (!indata || !indata->isStageValid())
        return false;

    auto stage = indata->stage();

    for( auto &&primpath : primpaths )
    {
        auto prim = stage->GetPrimAtPath(primpath.sdfPath());

        // Check if prim is Material or Shader (ie, one of editable
        // shading primitives).
        if (prim.IsA<UsdShadeMaterial>() || prim.IsA<UsdShadeShader>())
            shadeprimpaths.append(primpath.pathStr());
        else
            geoprimpaths.append(primpath.pathStr());

        // Note, currently this method is geared towards a workflow for
        // editing materials and shaders. To streamline that workflow,
        // we use certain heuristics to judge how editable the material is.
        // Eg, the workflow wants a list of shade prims (ie, mats or shaders)
        // whether specified directly or thru binding to a specified geo pirm.
        // But also, a material without inputs is not quite editable, so
        // we allow substituting such materials with a surface shader, which
        // should offer more input attributes for editing and customization.
        if( include_bound_materials )
        {
            // Try resolving to a bound material.
            UsdShadeMaterialBindingAPI api(prim);
            auto material = api.ComputeBoundMaterial();
            if( material )
            {
                auto inputs = material.GetInterfaceInputs();
                if (inputs.size() <= 0 && use_shader_for_mat_with_no_inputs)
                {
                    // Mat has no input attribs to edit; surf shader is better.
                    auto shader = material.ComputeSurfaceSource();
                    if (shader)
                        shadeprimpaths.append(shader.GetPath().GetAsString());
                }
                else
                {
                    // There are input attribs to edit, so add material.
                    shadeprimpaths.append(material.GetPath().GetAsString());
                }
            }
        }
    }

    return true;
}

namespace
{
    const std::map<TfType, TfTokenVector> &
    getPrimTypeToAttributeNameMap()
    {
        static std::map<TfType, TfTokenVector> thePrimTypeToAttributeNameMap =
        []() {
            std::map<TfType, TfTokenVector> map;
            UT_StringArray mapfiles;
            const UT_PathSearch *pathsearch =
                UT_PathSearch::getInstance(UT_HOUDINI_PATH);
            const char *thePrimAttribs = "UsdConnectablePrimAttribs.json";

            if (pathsearch->findAllFiles(thePrimAttribs, mapfiles) > 0)
            {
                for (auto &&mapfile : mapfiles)
                {
                    UT_IFStream is(mapfile);
                    UT_AutoJSONParser parser(is);
                    UT_JSONValue value;

                    value.parseValue(parser);
                    if (value.getMap())
                    {
                        for (auto &&it : *value.getMap())
                        {
                            if (!it.second || !it.second->getS())
                            {
                                std::cerr
                                    << "Attribute must be a string for "
                                    << it.first
                                    << " from file "
                                    << mapfile
                                    << std::endl;
                                continue;
                            }
                            TfType tftype = HUSDfindType(it.first);
                            if (tftype == TfType::GetUnknownType())
                            {
                                std::cerr
                                    << "Unknown primitive type "
                                    << it.first
                                    << " from file "
                                    << mapfile
                                    << std::endl;
                                continue;
                            }
                            map[tftype].push_back(
                                TfToken(it.second->getS()));
                        }
                    }
                }
            }

            return map;
        }();

        return thePrimTypeToAttributeNameMap;
    }

    bool
    isPrimConnectedTo(const UsdPrim &prim,
        std::map<SdfPath, bool> &testedpaths,
        const SdfPathSet &findpaths)
    {
        auto it = testedpaths.find(prim.GetPath());
        if (it != testedpaths.end())
            return it->second;

        auto connectable = UsdShadeConnectableAPI(prim);
        bool connected = false;

        testedpaths[prim.GetPath()] = connected;
        if (connectable && !connected)
        {
            const std::vector<UsdShadeInput> inputs = connectable.GetInputs();
            for (UsdShadeInput input : inputs)
            {
                UsdShadeAttributeVector attrs =
                    input.GetValueProducingAttributes();
                for (auto &&attr : attrs)
                {
                    if (findpaths.find(attr.GetPrimPath()) != findpaths.end())
                        connected = true;
                    else
                        connected = isPrimConnectedTo(
                            attr.GetPrim(), testedpaths, findpaths);
                    if (connected)
                        break;
                }
                if (connected)
                    break;
            }
        }

        if (connectable && !connected)
        {
            const std::vector<UsdShadeOutput> outputs = connectable.GetOutputs();
            for (UsdShadeOutput output : outputs)
            {
                UsdShadeAttributeVector attrs =
                    output.GetValueProducingAttributes();
                for (auto &&attr : attrs)
                {
                    if (findpaths.find(attr.GetPrimPath()) != findpaths.end())
                        connected = true;
                    else
                        connected = isPrimConnectedTo(
                            attr.GetPrim(), testedpaths, findpaths);
                    if (connected)
                        break;
                }
                if (connected)
                    break;
            }
        }
        testedpaths[prim.GetPath()] = connected;

        return connected;
    }
}

UT_StringArray
HUSDgetConnectedPrimsToBumpForHydra(
        const HUSD_AutoAnyLock &anylock,
        const UT_StringArray &modified_primpaths)
{
    UT_StringArray result;

    if (!anylock.isStageValid())
        return result;

    SdfPathSet modified_sdfprimpaths;
    HUSD_PathSet possible_connected_sdfprimpaths;
    UsdStageRefPtr stage = anylock.constData()->stage();

    for (auto &&primpath : modified_primpaths)
    {
        auto prim = stage->GetPrimAtPath(HUSDgetSdfPath(primpath));
        if (!prim)
            continue;
        modified_sdfprimpaths.insert(prim.GetPath());

        UsdPrim parentprim = prim.GetParent();
        while (parentprim && !parentprim.IsPseudoRoot())
        {
            // Add the ancestors of all connectable prims up to (and including)
            // the first prim that is not connectable. We will be scanning all
            // descendants of this first non-connectable ancestor.
            if (!possible_connected_sdfprimpaths.sdfPathSet().emplace(
                    parentprim.GetPath()).second)
                break;
            if (!UsdShadeConnectableAPI::HasConnectableAPI(
                    parentprim.GetPrimTypeInfo().GetSchemaType()))
                break;
            parentprim = parentprim.GetParent();
        }
    }
    // Eliminate any children of other entries in the set. So we are left with
    // a set of "root" prims that we can iterate through without fear of doing
    // any duplicate processing.
    possible_connected_sdfprimpaths.removeDescendants();

    // For each source root, look test each prim of an interesting type for
    // any connection to any of the modified prims.
    std::map<SdfPath, bool> testedpaths;
    for (auto &&rootpath : possible_connected_sdfprimpaths)
    {
        UsdPrim rootprim = stage->GetPrimAtPath(rootpath.sdfPath());
        for (auto &&testprim : rootprim.GetDescendants())
        {
            bool is_interesting_type = false;

            for (auto &&it : getPrimTypeToAttributeNameMap())
            {
                if (testprim.IsA(it.first))
                {
                    is_interesting_type = true;
                    break;
                }
            }
            if (!is_interesting_type)
                continue;

            if (isPrimConnectedTo(testprim, testedpaths, modified_sdfprimpaths))
                result.append(testprim.GetPath().GetAsString());
        }
    }

    return result;
}

bool
HUSDbumpPrimsForHydra(const HUSD_AutoWriteLock &writelock,
        const UT_StringArray &bump_primpaths)
{
    auto indata = writelock.data();
    if (!indata || !indata->isStageValid())
        return false;

    auto stage = indata->stage();
    UsdAttributeVector attrs;

    for( auto &&primpath : bump_primpaths )
    {
        auto prim = stage->GetPrimAtPath(HUSDgetSdfPath(primpath));
        if (prim)
        {
            for (auto &&it : getPrimTypeToAttributeNameMap())
            {
                if (prim.IsA(it.first))
                {
                    for (auto &&attrtoken : it.second)
                    {
                        auto attr = prim.GetAttribute(attrtoken);
                        if (attr)
                            attrs.push_back(attr);
                    }
                    break;
                }
            }
        }
    }
    HUSDbumpPropertiesForHydra(attrs);

    return true;
}

UT_Lock &
HUSDgetLayerReloadLock()
{
    static UT_Lock theLayerReloadLock;

    return theLayerReloadLock;
}

void
HUSDmodifyAssetPaths(const UT_StringHolder &path,
        const ModifyPathFn &modifyFn,
        const UT_StringHolder &dest)
{
    SdfLayerRefPtr root;
    if (path == dest)
        root = SdfLayer::FindOrOpen(path.toStdString());
    else
        root = SdfLayer::OpenAsAnonymous(path.toStdString());

    HUSDmodifyAssetPaths(root, [&modifyFn](std::string asset)
    {
        UT_StringHolder assetPath = asset;
        return modifyFn(assetPath).toStdString();
    });
    if (path == dest)
        root->Save();
    else
    {
        root->Export(dest.toStdString());
        HUSD_Info::reload(dest, false);
    }
}

const UT_Array<HUSD_OverridesLayerId> &
HUSDgetUserEditableOverrideLayerIds()
{
    static const UT_Array<HUSD_OverridesLayerId> theLayerIds({
        HUSD_OVERRIDES_CUSTOM_LAYER,
        HUSD_OVERRIDES_PURPOSE_LAYER,
        HUSD_OVERRIDES_SOLO_LIGHTS_LAYER,
        HUSD_OVERRIDES_SOLO_GEOMETRY_LAYER,
        HUSD_OVERRIDES_SELECTABLE_LAYER,
        HUSD_OVERRIDES_BASE_LAYER
    });

    return theLayerIds;
}

bool
HUSDareLayersEqual(const UT_StringHolder &layerIdentifierA,
        const UT_StringHolder &layerIdentifierB)
{
    SdfLayerRefPtr layerA = SdfLayer::FindOrOpen(layerIdentifierA.toStdString());
    SdfLayerRefPtr layerB = SdfLayer::FindOrOpen(layerIdentifierB.toStdString());

    if (!layerA || !layerB)
    {
        return false;
    }

    SdfChangeList changes = layerA->CreateDiff(layerB);
    // We aren't interested in only didReorderProperties being set, which happens
    // often due to re-ordering of metadata, etc when a layer re-cooks, but doesn't actually change.
    for (auto entry : changes.GetEntryList())
    {
        if ( entry.second.infoChanged.size() > 0 ||
             entry.second.subLayerChanges.size() > 0 ||

             entry.second.flags.didChangeIdentifier ||
             entry.second.flags.didChangeResolvedPath ||
             entry.second.flags.didReplaceContent ||
             entry.second.flags.didReloadContent ||

             entry.second.flags.didReorderChildren ||

             entry.second.flags.didRename ||

             entry.second.flags.didChangePrimVariantSets ||
             entry.second.flags.didChangePrimInheritPaths ||
             entry.second.flags.didChangePrimSpecializes ||
             entry.second.flags.didChangePrimReferences ||

             entry.second.flags.didChangeAttributeTimeSamples ||
             entry.second.flags.didChangeAttributeConnection ||
             entry.second.flags.didChangeRelationshipTargets ||
             entry.second.flags.didAddTarget ||
             entry.second.flags.didRemoveTarget ||

             entry.second.flags.didAddInertPrim ||
             entry.second.flags.didAddNonInertPrim ||
             entry.second.flags.didRemoveInertPrim ||
             entry.second.flags.didRemoveNonInertPrim ||

             entry.second.flags.didAddPropertyWithOnlyRequiredFields ||
             entry.second.flags.didAddProperty ||
             entry.second.flags.didRemovePropertyWithOnlyRequiredFields ||
             entry.second.flags.didRemoveProperty
            )
        {
            return false; // layers not identical...
        }
    }
    return true;  // layers are identical
}

UT_StringArray HUSDgetConflictingRelocates(
        const HUSD_AutoAnyLock &lock,
        const HUSD_PathSet &paths)
{
    UT_StringArray                    existing_relocates;

    auto indata = lock.constData();
    if (!indata || !indata->isStageValid())
        return existing_relocates;

    auto stage = indata->stage();

    for (auto &&path : paths)
    {
        SdfPath                             sdfPath = path.sdfPath();
        auto                                prim = stage->GetPrimAtPath(sdfPath);
        UsdPrimCompositionQuery::Filter     myQueryFilter;
        UsdPrimCompositionQuery             query(prim, myQueryFilter);
        auto                                arcs = query.GetCompositionArcs();
                                          
        auto                               &layerStack = arcs[0].GetTargetNode().GetLayerStack();

        // We use the incremental relocates map instead of the "resolved" one because intermediate relocates are
        // also illegal paths.
        const SdfRelocatesMap              &relocatesSrcToTarget = layerStack->GetIncrementalRelocatesSourceToTarget();

        for (auto&& relocate : relocatesSrcToTarget)
        {
            if (relocate.first == sdfPath || relocate.second == sdfPath)
                existing_relocates.append(sdfPath.GetAsString());
        }
    }
    return existing_relocates;
}
