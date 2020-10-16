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
#include "HUSD_Constants.h"
#include "HUSD_ErrorScope.h"
#include "HUSD_LockedStage.h"
#include "HUSD_LockedStageRegistry.h"
#include "HUSD_TimeCode.h"
#include "XUSD_AttributeUtils.h"
#include "XUSD_AutoCollection.h"
#include "XUSD_Data.h"
#include "XUSD_Utils.h"
#include <gusd/gusd.h>
#include <gusd/GU_PackedUSD.h>
#include <gusd/stageCache.h>
#include <OP/OP_Node.h>
#include <UT/UT_Exit.h>
#include <UT/UT_Lock.h>
#include <UT/UT_Set.h>
#include <UT/UT_String.h>
#include <UT/UT_StringArray.h>
#include <UT/UT_WorkArgs.h>
#include <pxr/pxr.h>
#include <pxr/base/work/threadLimits.h>
#include <pxr/usd/ar/resolver.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/usd/collectionAPI.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/tokens.h>
#include <pxr/usd/usdGeom/xformOp.h>
#include <pxr/usd/usdGeom/xformable.h>
#include <iostream>

PXR_NAMESPACE_USING_DIRECTIVE

static HUSD_LopStageResolver theLopStageResolver = nullptr;
static UT_Set<HUSD_LockedStagePtr> theHoldLockedStages;
static UT_Lock theHoldLockedStagesLock;
static int theStageCacheReaderCounter = 0;

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

void
HUSDinitialize()
{
    // In case Gusd hasn't been initialized yet, do it here becuase that
    // function adds plugin registry directories to the USD library.
    GusdInit();
    GusdStageCache::SetLopStageResolver(
        husdLopStageResolver);
    GusdStageCache::SetStageCacheReaderTracker(
        husdStageCacheReaderTracker);
    GusdGU_PackedUSD::setPackedUSDTracker(
        HUSD_LockedStageRegistry::packedUSDTracker);
    UT_Exit::addExitCallback(
        HUSD_LockedStageRegistry::exitCallback);
    WorkSetConcurrencyLimitArgument(UT_Thread::getNumProcessors());
    ArSetPreferredResolver("FS_ArResolver");
    XUSD_AutoCollection::registerPlugins();
}

void
HUSDsetLopStageResolver(HUSD_LopStageResolver resolver)
{
    theLopStageResolver = resolver;
}

bool
HUSDsplitLopStageIdentifier(const UT_StringRef &identifier,
        OP_Node *&lop,
        bool &split_layers,
        fpreal &t)
{
    return GusdStageCache::SplitLopStageIdentifier(identifier,
        lop, split_layers, t);
}

bool
HUSDisValidUsdName(const UT_StringRef &name)
{
    return TfIsValidIdentifier(name.toStdString());
}

bool
HUSDmakeValidUsdName(UT_String &name, bool addwarnings)
{
    if (!name.isstring())
	return false;

    bool	 changed = (name.forceValidVariableName() != 0);

    if (changed && addwarnings)
	HUSD_ErrorScope::addWarning(
	    HUSD_ERR_FIXED_INVALID_NAME, name.c_str());

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
    if (!name.isstring())
	return false;

    // Property names are like prim names, but they allow namespacing with ":".
    bool	 changed = (name.forceValidVariableName(":") != 0);

    // We can't end with a ":".
    while (name.endsWith(":"))
    {
	name.removeLast();
	changed = true;
    }
    // Replace any sequence of ":"s with a single ":".
    while (name.substitute("::", ":", false))
	changed = true;

    if (changed && addwarnings)
	HUSD_ErrorScope::addWarning(
	    HUSD_ERR_FIXED_INVALID_NAME, name.c_str());

    return changed;
}

bool
HUSDmakeValidVariantName(UT_String &name, bool addwarnings)
{
    if (!name.isstring())
	return false;

    bool	 changed = false;

    // This logic is copied from USD/pxr/usd/lib/sdf/schema.cpp,
    // SdfSchemaBase::IsValidVariantIdentifier.
    for (char *c = name; *c; c++)
    {
	if (isalnum(*c) || *c == '_' || *c == '|' || *c == '-')
	    continue;

	if (c == name && *c == '.')
	    continue;

	*c = '_';
	changed = true;
    }

    if (changed && addwarnings)
	HUSD_ErrorScope::addWarning(
	    HUSD_ERR_FIXED_INVALID_VARIANT_NAME, name.c_str());

    return changed;
}

bool
HUSDmakeValidDefaultPrim(UT_String &default_prim, bool addwarnings)
{
    // If no primitive name is specified, do nothing.
    if (default_prim.isstring())
    {
	// Eliminate any spaces at the start or end of the string.
	default_prim.trimBoundingSpace();
	// Strip off any leading slashes. These are so common it is best
	// to just deal with them.
	while (default_prim.startsWith("/"))
	    default_prim.eraseHead(1);

	UT_String	 default_prim_copy(default_prim);

	// If the resulting prim name isn't valid, this is an error.
	if (HUSDmakeValidUsdName(default_prim_copy, false))
	{
	    if (addwarnings)
		HUSD_ErrorScope::addError(HUSD_ERR_INVALID_DEFAULTPRIM,
		    default_prim.c_str());
	    return false;
	}
    }

    return true;
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

UT_StringHolder 
HUSDgetPrimvarAttribName(const UT_StringRef &primvar_name)
{
    UT_WorkBuffer buffer;
    buffer.append( "primvars" ); // primvar.cpp: _tokens->primvarsPrefix
    buffer.append( SDF_PATH_NS_DELIMITER_CHAR );
    buffer.append( primvar_name );

    return UT_StringHolder(buffer);
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
HUSDsetParmFromProperty(HUSD_AutoAnyLock &lock,
        const UT_StringRef &primpath,
        const UT_StringRef &attribname,
        const HUSD_TimeCode &tc,
        PRM_Parm &parm,
        HUSD_TimeSampling &timesampling)
{
    UsdPrim prim;

    if (primpath.isstring() &&
	lock.constData() && lock.constData()->isStageValid())
    {
	SdfPath sdfpath(HUSDgetSdfPath(primpath));
	prim = lock.constData()->stage()->GetPrimAtPath(sdfpath);
    }

    if (!prim)
	return false;

    auto attrib = prim.GetAttribute(TfToken(attribname.toStdString()));
    if (attrib)
    {
        HUSDupdateValueTimeSampling(timesampling, attrib);
        UsdTimeCode usdtc = HUSDgetNonDefaultUsdTimeCode(tc);

        return HUSDsetNodeParm(parm, attrib, usdtc, true);
    }

    auto rel = prim.GetRelationship(TfToken(attribname.toStdString()));
    if (rel)
        return HUSDsetNodeParm(parm, rel, true);

    return false;
}

