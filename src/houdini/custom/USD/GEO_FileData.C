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
 */

#include "GEO_FileData.h"
#include "GEO_FilePropSource.h"
#include "GEO_FileFieldValue.h"
#include "GEO_FilePrimUtils.h"
#include "GEO_FileRefiner.h"
#include <HUSD/HUSD_Constants.h>
#include <HUSD/XUSD_TicketRegistry.h>
#include <HUSD/XUSD_Utils.h>
#include <OP/OP_Director.h>
#include <GT/GT_RefineParms.h>
#include <GU/GU_Detail.h>
#include <UT/UT_EnvControl.h>
#include <UT/UT_IStream.h>
#include <UT/UT_Format.h>
#include <UT/UT_SpinLock.h>
#include <UT/UT_WorkArgs.h>
#include <SYS/SYS_ParseNumber.h>
#include <SYS/SYS_Math.h>
#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/tf/pathUtils.h>
#include <pxr/usd/sdf/schema.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdVol/tokens.h>

PXR_NAMESPACE_OPEN_SCOPE

#define UNSUPPORTED(M) \
    TF_RUNTIME_ERROR("Houdini geometry file " #M "() not supported")

//
// GEO_FileData
//

GEO_FileData::GEO_FileData()
    : myPseudoRoot(nullptr),
      mySampleFrame(0.0),
      mySampleFrameSet(false)
{
}

GEO_FileData::~GEO_FileData()
{
}

GEO_FileDataRefPtr
GEO_FileData::New(const SdfFileFormat::FileFormatArguments &args)
{
    auto		 data = TfCreateRefPtr(new GEO_FileData);
    auto		 timeit = args.find("t");

    data->myCookArgs = args;
    if (timeit != args.end())
    {
	data->mySampleFrame = SYSatof(timeit->second.c_str());
	data->mySampleFrame = CHgetSampleFromTime(data->mySampleFrame);
	data->mySampleFrameSet = true;
	data->mySaveSampleFrame = false;
    }
    else
    {
	data->mySampleFrame = CHgetSampleFromTime(0.0);
	data->mySampleFrameSet = false;
	data->mySaveSampleFrame = false;
    }

    return data;
}

static bool
getCookOption(const SdfFileFormat::FileFormatArguments *args,
	const UT_StringRef &argname,
	const GU_Detail *gdp,
	const UT_StringRef &attrname,
	std::string &value)
{
    static SdfFileFormat::FileFormatArguments   theDefaultArgs;
    static UT_SpinLock                          theDefaultArgsLock;
    static bool                                 theDefaultArgsSet = false;

    // Make sure we have calculated the default args from the environment
    // variable. This must be done in a way that safe for multithreading.
    if (!theDefaultArgsSet)
    {
        UT_AutoSpinLock  scope(theDefaultArgsLock);

        if (!theDefaultArgsSet)
        {
            if (UTisstring(UT_EnvControl::getString(
                    ENV_HOUDINI_BGEO_TO_USD_DEFAULT_ARGS)))
            {
                std::string  argstr;
                std::string  path;

                argstr = UT_EnvControl::getString(
                    ENV_HOUDINI_BGEO_TO_USD_DEFAULT_ARGS);
                argstr.insert(0, "foo.usd:SDF_FORMAT_ARGS:");
                SdfLayer::SplitIdentifier(argstr, &path, &theDefaultArgs);
            }
            theDefaultArgsSet = true;
        }
    }

    // Top priority is given to arguments sent with the asset path.
    if (args && argname.isstring())
    {
	auto		 argit = args->find(argname.toStdString());

	if (argit != args->end())
	{
	    value = argit->second;

	    return true;
	}
    }

    // Then arguments set in the geometry file itself are considered.
    if (gdp && attrname.isstring())
    {
	GA_ROHandleS	 attr(gdp, GA_ATTRIB_DETAIL, attrname);

	if (attr.isValid())
	{
	    UT_StringHolder	 valuestr;

	    valuestr = attr->getString(GA_Offset(0));
	    value = valuestr.toStdString();

	    return true;
	}
    }

    // Default arguments are given the lowest priority.
    if (argname.isstring())
    {
	auto		 argit = theDefaultArgs.find(argname.toStdString());

	if (argit != theDefaultArgs.end())
	{
	    value = argit->second;

	    return true;
	}
    }

    return false;
}

static bool
getCookOption(const SdfFileFormat::FileFormatArguments *args,
	const UT_StringRef &argname,
	const GU_Detail *gdp,
	std::string &value)
{
    UT_String	 attrname("usdconfig");

    attrname.append(argname);

    return getCookOption(args, argname, gdp, attrname, value);
}

bool
GEO_FileData::Open(const std::string& filePath)
{
    TfAutoMallocTag2	 tag("GEO_FileData", "GEO_FileData::Open");
    GU_DetailHandle	 gdh;
    UT_String		 soppath;
    std::string		 orig_path_with_args;
    bool		 success = false;

    if (TfGetExtension(filePath) == "sop")
    {
	UT_IFStream	 is(filePath.c_str());
	UT_String	 origpath;
	UT_WorkBuffer	 buf;

	if (is.getLine(buf))
	{
	    // The asset path is the original string used to open this "file",
	    // such as "op:/object/geo1/xform1.sop". Strip off the prefix and
	    // suffix to get the full SOP path.
	    buf.copyIntoString(origpath);
	    soppath.harden(origpath);
	    if (const char *ext = soppath.fileExtension())
		soppath.eraseTail(strlen(ext));
	    soppath.eraseHead(OPREF_PREFIX_LEN);
	}

	gdh = XUSD_TicketRegistry::getGeometry(origpath, myCookArgs);
	orig_path_with_args = SdfLayer::CreateIdentifier(
	    origpath.toStdString(), myCookArgs);
	success = gdh.isValid();
    }
    else
    {
        orig_path_with_args = SdfLayer::CreateIdentifier(filePath, myCookArgs);

	gdh.allocateAndSet(new GU_Detail());
	GU_DetailHandleAutoWriteLock	 gdp_write_lock(gdh);
	GU_Detail			*gdp = gdp_write_lock.getGdp();
	auto				 status = gdp->load(filePath.c_str());

	success = status.success();
    }

    if (success)
    {
	GEO_ImportOptions	 options;

	// Make a prim for our pseudo root.
	myPseudoRoot = &myPrims[SdfPath::AbsoluteRootPath()];
	myPseudoRoot->setPath(SdfPath::AbsoluteRootPath());

	// Make a prim for holding our layer info.
	myLayerInfoPrim = &myPrims[SdfPath(
	    HUSD_Constants::getHoudiniLayerInfoPrimPath().toStdString())];
	myLayerInfoPrim->setPath(SdfPath(
	    HUSD_Constants::getHoudiniLayerInfoPrimPath().toStdString()));
	myLayerInfoPrim->setTypeName(TfToken(
	    HUSD_Constants::getHoudiniLayerInfoPrimType().toStdString()));
	myLayerInfoPrim->setInitialized();

	// Collect refinement/export options from the file format arguments
	// passed as part of our path and detail attributes from the geometry
	// itself.
	{
	    GU_DetailHandleAutoReadLock	 gdp_read_lock(gdh);
	    const GU_Detail		*gdp = gdp_read_lock.getGdp();
	    std::string			 cook_option;
	    UT_String			 path_attr_str;
	    UT_WorkArgs			 path_attr_args;

            // Only grab the sample frame from the gdp if we weren't passed
            // a value in the args used to open the file.
            if (!mySampleFrameSet)
            {
                if (getCookOption(&myCookArgs, "sampleframe", gdp, cook_option))
                {
                    mySampleFrame = SYSatof(cook_option.c_str());
                    mySampleFrameSet = true;
                    mySaveSampleFrame = true;
                }
            }

	    if (getCookOption(&myCookArgs, "pathattr", gdp, cook_option))
		path_attr_str = cook_option;
	    else
		path_attr_str = HUSD_Constants::getDefaultBgeoPathAttr();
	    path_attr_str.tokenize(path_attr_args, ", \n\t");
	    for (int i = 0; i < path_attr_args.getArgc(); i++)
		options.myPathAttrNames.append(path_attr_args.getArg(i));

	    if (getCookOption(&myCookArgs, "pathprefix", gdp, cook_option))
	    {
		options.myPrefixPath = HUSDgetSdfPath(cook_option);
		if (options.myPrefixPath.IsEmpty())
		    options.myPrefixPath = SdfPath::AbsoluteRootPath();
		else
		    options.myPrefixPath = options.myPrefixPath.
			MakeAbsolutePath(SdfPath::AbsoluteRootPath());
	    }
	    else
		options.myPrefixPath =
		    HUSDgetSdfPath(HUSD_Constants::getDefaultBgeoPathPrefix());

            bool globalauthortimesamples = true;
            if (getCookOption(&myCookArgs, "globalauthortimesamples", gdp,
                              cook_option))
            {
                globalauthortimesamples = (cook_option != "0");
            }

	    if (getCookOption(&myCookArgs, "polygonsassubd", gdp, cook_option))
		options.myPolygonsAsSubd = (cook_option != "0");

            if (getCookOption(&myCookArgs, "subdgroup", gdp, cook_option))
		options.mySubdGroup = cook_option;

	    if (getCookOption(&myCookArgs, "reversepolygons", gdp, cook_option))
		options.myReversePolygons = (cook_option != "0");

	    if (getCookOption(&myCookArgs, "topology", gdp, cook_option))
	    {
		if (cook_option == "animated")
		    options.myTopologyHandling = GEO_USD_TOPOLOGY_ANIMATED;
		else if (cook_option == "static")
		    options.myTopologyHandling = GEO_USD_TOPOLOGY_STATIC;
		else if (cook_option == "none")
		    options.myTopologyHandling = GEO_USD_TOPOLOGY_NONE;
	    }

            // Ignore user-specified topology handling if the attribs should be
            // static, unless the user requested no topology.
            if (!globalauthortimesamples &&
                options.myTopologyHandling != GEO_USD_TOPOLOGY_NONE)
            {
                options.myTopologyHandling = GEO_USD_TOPOLOGY_STATIC;
            }

	    if (getCookOption(&myCookArgs, "usdprims", gdp, cook_option))
	    {
		if (cook_option == "ignore")
		    options.myUsdHandling = GEO_USD_PACKED_IGNORE;
		else if (cook_option == "xform")
		    options.myUsdHandling = GEO_USD_PACKED_XFORM;
	    }

	    if (getCookOption(&myCookArgs, "packedprims", gdp, cook_option))
	    {
		if (cook_option == "xforms")
		    options.myPackedPrimHandling = GEO_PACKED_XFORMS;
		else if (cook_option == "pointinstancer")
		    options.myPackedPrimHandling = GEO_PACKED_POINTINSTANCER;
		else if (cook_option == "nativeinstances")
		    options.myPackedPrimHandling = GEO_PACKED_NATIVEINSTANCES;
	    }

	    if (getCookOption(&myCookArgs, "nurbscurves", gdp, cook_option))
	    {
		if (cook_option == "basiscurves")
		    options.myNurbsCurveHandling = GEO_NURBS_BASISCURVES;
		else if (cook_option == "nurbscurves")
		    options.myNurbsCurveHandling = GEO_NURBS_NURBSCURVES;
	    }

	    if (getCookOption(&myCookArgs, "kindschema", gdp, cook_option))
	    {
		if (cook_option == "none")
		    options.myKindSchema = GEO_KINDSCHEMA_NONE;
		else if (cook_option == "component")
		    options.myKindSchema = GEO_KINDSCHEMA_COMPONENT;
		else if (cook_option == "nestedgroup")
		    options.myKindSchema = GEO_KINDSCHEMA_NESTED_GROUP;
		else if (cook_option == "nestedassembly")
		    options.myKindSchema = GEO_KINDSCHEMA_NESTED_ASSEMBLY;
	    }

            if (getCookOption(&myCookArgs, "otherprims", gdp, cook_option))
            {
                GEOconvertTokenToEnum(TfToken(cook_option),
                                      options.myOtherPrimHandling);
                if (options.myOtherPrimHandling == GEO_OTHER_XFORM)
                {
                    // We don't want to author kind information when we are only
                    // asked for xform override prims.
                    options.myKindSchema = GEO_KINDSCHEMA_NONE;
                }
            }

            if (getCookOption(&myCookArgs, "defineonlyleafprims", gdp,
                              cook_option))
            {
                options.myDefineOnlyLeafPrims = (cook_option != "0");
            }

            if (getCookOption(&myCookArgs, "group", gdp, cook_option))
		options.myImportGroup = cook_option;

	    if (getCookOption(&myCookArgs, "attribs", gdp, cook_option))
		options.myAttribs.compile(cook_option.c_str());
	    else
		options.myAttribs.compile(
		    HUSD_Constants::getDefaultBgeoAttribPattern());

            if (!globalauthortimesamples)
            {
                // Ignore user-specified static attribs if all attributes
                // should be static.
                options.myStaticAttribs.compile("*");
            }
	    else if (getCookOption(&myCookArgs, "staticattribs",
                        gdp, cook_option) && !cook_option.empty())
            {
		options.myStaticAttribs.compile(cook_option.c_str());
            }

	    if (getCookOption(&myCookArgs, "constantattribs",
		    gdp, cook_option) &&
		!cook_option.empty())
		options.myConstantAttribs.compile(cook_option.c_str());

	    if (getCookOption(&myCookArgs, "indexattribs",
		    gdp, cook_option) &&
		!cook_option.empty())
		options.myIndexAttribs.compile(cook_option.c_str());

	    if (getCookOption(&myCookArgs, "customattribs", gdp,cook_option) &&
		!cook_option.empty())
            {
		options.myCustomAttribs.compile(cook_option.c_str());
            }

	    if (getCookOption(&myCookArgs, "partitionattribs",
		    gdp,cook_option) &&
		!cook_option.empty())
		options.myPartitionAttribs.compile(cook_option.c_str());

	    if (getCookOption(&myCookArgs, "subsetgroups",
		    gdp,cook_option) &&
		!cook_option.empty())
		options.mySubsetGroups.compile(cook_option.c_str());

	    if (getCookOption(&myCookArgs, "translateuvtost", gdp, cook_option))
		options.myTranslateUVToST = (cook_option != "0");

	    if (soppath.isstring())
	    {
		if (getCookOption(&myCookArgs,
                        "savepath", gdp, cook_option))
		{
		    if (UTisstring(cook_option.c_str()))
			myLayerInfoPrim->addCustomData(
                            HUSDgetSavePathToken(),
			    VtValue(cook_option));
		}

		myLayerInfoPrim->addCustomData(HUSDgetCreatorNodeToken(),
		    VtValue(soppath.toStdString()));
		myLayerInfoPrim->addCustomData(HUSDgetEditorNodesToken(),
		    VtValue(VtArray<std::string>({soppath.toStdString()})));
	    }
	}

	GT_RefineParms		 refine_parms;
	GEO_FileRefinerCollector collector;
	GEO_FileRefiner		 refiner(collector, options.myPrefixPath,
					 options.myPathAttrNames);

	refine_parms.set("refineToUSD", true);
	refine_parms.setPolysAsSubdivision(options.myPolygonsAsSubd);
	refine_parms.setCoalesceFragments(false);
	refine_parms.setCoalesceVolumes(false);
        // We always need to import facesets, so that subdivision tags like
        // "hole" can be imported correctly when subd is manually enabled by an
        // attribute.
        refine_parms.setFaceSetMode(GT_RefineParms::FACESET_NON_EMPTY);
	// Tell the refiner which primitives to refine.
	refiner.m_importGroup = options.myImportGroup;
	refiner.m_subdGroup = options.mySubdGroup;
	// Tell the refiner how to deal with USD packed prims.
	refiner.m_handleUsdPackedPrims = options.myUsdHandling;
        refiner.m_handlePackedPrims = options.myPackedPrimHandling;

	refiner.refineDetail(gdh, refine_parms);

	const GEO_FileRefiner::GEO_FileGprimArray &prims = refiner.finish();
	SdfPath				 default_prim_path;

	// No point in outputting our path attributes.
	for (auto &&path_attr_name : options.myPathAttrNames)
	    options.myProcessedAttribs.insert(path_attr_name);
	// Attributes that we never want to output as primvars.
	options.myProcessedAttribs.insert("varmap");
	options.myProcessedAttribs.insert("usdsavepath");
	// Set the default prim to the root of the prefix path, if we have one.
	if (options.myPrefixPath != SdfPath::AbsoluteRootPath())
	    default_prim_path = options.myPrefixPath;
	else if (!prims.empty())
	    default_prim_path = *(prims.begin()->path);
	else
	    default_prim_path = SdfPath::AbsoluteRootPath();

	while (default_prim_path != SdfPath::AbsoluteRootPath() &&
	       !default_prim_path.IsRootPrimPath())
	    default_prim_path = default_prim_path.GetParentPath();
	GEOinitRootPrim(*myPseudoRoot, default_prim_path.GetNameToken(),
            mySaveSampleFrame, mySampleFrame);

        GEO_HandleOtherPrims parents_primhandling = options.myOtherPrimHandling;
        GEO_KindSchema parents_kind = options.myKindSchema;
        if (options.myDefineOnlyLeafPrims)
        {
            parents_primhandling = GEO_OTHER_OVERLAY;
            parents_kind = GEO_KINDSCHEMA_NONE;
        }

	if (!prims.empty())
	{
	    // Create a GEO_FilePrim for each refined GT_Primitive.
	    for (auto &&prim : prims)
	    {
		GEO_FilePrim	&fileprim(myPrims[*prim.path]);

		fileprim.setPath(*prim.path);
                GEOinitGTPrim(fileprim, myPrims, prim.prim, prim.xform,
                              prim.topologyId, orig_path_with_args,
                              prim.agentShapeInfo, options);
            }
	}
	else if (default_prim_path != SdfPath::AbsoluteRootPath())
	{
	    GEO_FilePrim	&fileprim(myPrims[default_prim_path]);

	    // Even if we didn't get any primitives, we still want to create
	    // an Xform prim at the default prim location to avoid spurious
	    // warnings when importing from an empty SOP.
	    fileprim.setPath(default_prim_path);
            GEOinitXformPrim(fileprim, parents_primhandling, parents_kind);
        }

	// Set up parent-child relationships.
	for (auto &&it : myPrims)
	{
	    SdfPath	 parentpath = it.first.GetParentPath();

	    // We don't want to author a kind or set up a parent relationship
	    // for the pseudoroot.
	    if (!parentpath.IsEmpty())
	    {
		myPrims[parentpath].addChild(it.first.GetNameToken());

		// We don't want to author a kind for the layer info prim.
		if (&it.second != myLayerInfoPrim)
		{
		    if (!it.second.getInitialized())
                    {
                        GEOinitXformPrim(it.second, parents_primhandling,
                                         parents_kind);
                    }

		    // Special override of the Kind of root primitives. We can't
		    // set the Kind of the pseudo root prim, so don't try.
		    if (options.myOtherPrimHandling == GEO_OTHER_DEFINE &&
                        !options.myDefineOnlyLeafPrims && 
			it.first.IsRootPrimPath())
			GEOsetKind(it.second, options.myKindSchema,
			    GEO_KINDGUIDE_TOP);
		}
	    }
	}
    }

    return success;
}

void
GEO_FileData::CreateSpec(
    const SdfPath& id, 
    SdfSpecType specType)
{
    UNSUPPORTED(CreateSpec);
}

bool
GEO_FileData::HasSpec(const SdfPath& id) const
{
    if (auto prim = getPrim(id))
    {
	if (id.IsPropertyPath())
	    return (prim->getProp(id) != nullptr);
	else
	    return true;
    }

    return (id == SdfPath::AbsoluteRootPath());
}

void
GEO_FileData::EraseSpec(const SdfPath& id)
{
    UNSUPPORTED(EraseSpec);
}

void
GEO_FileData::MoveSpec(
    const SdfPath& oldId,
    const SdfPath& newId)
{
    UNSUPPORTED(MoveSpec);
}

SdfSpecType
GEO_FileData::GetSpecType(const SdfPath& id) const
{
    if (auto prim = getPrim(id))
    {
	if (id.IsPropertyPath())
	{
	    if (prim->getProp(id))
	    {
		if (prim->getProp(id)->getIsRelationship())
		    return SdfSpecTypeRelationship;
		else
		    return SdfSpecTypeAttribute;
	    }
	}
	else if (prim == myPseudoRoot)
	{
	    return SdfSpecTypePseudoRoot;
	}
	else
	{
	    return SdfSpecTypePrim;
	}
    }

    return SdfSpecTypeUnknown;
}

void
GEO_FileData::_VisitSpecs(SdfAbstractDataSpecVisitor* visitor) const
{
    for (auto primit = myPrims.begin(); primit != myPrims.end(); ++primit)
    {
	if (!visitor->VisitSpec(*this, primit->first))
	    return;

	if (&primit->second != myPseudoRoot)
	{
            const auto &props = primit->second.getProps();

	    for (auto propit = props.begin(); propit != props.end(); ++propit)
	    {
		if (!visitor->VisitSpec(*this,
		    primit->first.AppendProperty(propit->first)))
		    return;
	    }
	}
    }
}

bool
GEO_FileData::_Has(
    const SdfPath& id,
    const TfToken& fieldName,
    const GEO_FileFieldValue &value) const
{
    if (auto prim = getPrim(id))
    {
	if (id.IsPropertyPath())
	{
	    auto prop = prim->getProp(id);

	    if (prop)
	    {
		if (prop->getIsRelationship())
		{
		    // Fields specific to relationships.
		    if (fieldName == SdfFieldKeys->TargetPaths)
		    {
			return prop->copyData(value);
		    }
		}
		else
		{
		    // Fields specific to attributes.
		    if (fieldName == SdfFieldKeys->Default &&
			(!mySampleFrameSet || prop->getValueIsDefault()))
		    {
			return prop->copyData(value);
		    }
		    else if (fieldName == SdfFieldKeys->TypeName)
		    {
			return value.Set(prop->getTypeName().GetAsToken());
		    }
		    else if (fieldName == SdfFieldKeys->TimeSamples &&
			     (mySampleFrameSet && !prop->getValueIsDefault()))
		    {
			if (value)
			{
			    VtValue		 tmp;
			    GEO_FileFieldValue	 tmpval(&tmp);
			    SdfTimeSampleMap	 samples;

			    if (prop->copyData(tmpval))
				samples[mySampleFrame] = tmp;

			    return value.Set(samples);
			}
			else
			    return true;
		    }
		}

		// fields common to attributes and relationships.
		if (fieldName == SdfFieldKeys->CustomData &&
		    !prop->getCustomData().empty())
		{
		    VtDictionary custom_data;
		    for (auto &&it : prop->getCustomData())
			custom_data[it.first] = it.second;
		    return value.Set(custom_data);
		}
		else if (fieldName == SdfFieldKeys->Variability)
		{
		    if (prop->getValueIsUniform())
			return value.Set(SdfVariabilityUniform);
		    else
			return value.Set(SdfVariabilityVarying);
		}

		auto it = prop->getMetadata().find(fieldName);
		if (it != prop->getMetadata().end())
		    return value.Set(it->second);
	    }
	}
	else
	{
	    if (prim != myPseudoRoot)
	    {
		if (fieldName == SdfChildrenKeys->PropertyChildren)
		{
		    return value.Set(prim->getPropNames());
		}
		else if (fieldName == SdfFieldKeys->TypeName)
		{
                    // Don't return a prim type unless the prim is defined.
                    // If we are just creating overlay data for existing prims,
                    // we don't want to change any prim types.
                    if (prim->getIsDefined())
                        return value.Set(prim->getTypeName());
		}
		else if (fieldName == SdfFieldKeys->Specifier)
		{
		    if (prim->getIsDefined())
			return value.Set(SdfSpecifierDef);
		    else
			return value.Set(SdfSpecifierOver);
		}
	    }
	    if (fieldName == SdfChildrenKeys->PrimChildren)
	    {
		return value.Set(prim->getChildNames());
	    }
	    else if (((fieldName == SdfFieldKeys->CustomData &&
		       prim != myPseudoRoot) ||
		      (fieldName == SdfFieldKeys->CustomLayerData &&
		       prim == myPseudoRoot)) &&
		     !prim->getCustomData().empty())
	    {
		VtDictionary custom_data;
		for (auto &&it : prim->getCustomData())
		    custom_data[it.first] = it.second;
		return value.Set(custom_data);
	    }

	    auto it = prim->getMetadata().find(fieldName);
	    if (it != prim->getMetadata().end())
		return value.Set(it->second);
	}
    }

    return false;
}

bool
GEO_FileData::Has(
    const SdfPath& id,
    const TfToken& fieldName,
    SdfAbstractDataValue* value) const
{
    return _Has(id, fieldName, GEO_FileFieldValue(value));
}

bool
GEO_FileData::Has(
    const SdfPath& id,
    const TfToken& fieldName,
    VtValue* value) const
{
    return _Has(id, fieldName, GEO_FileFieldValue(value));
}

VtValue
GEO_FileData::Get(
    const SdfPath& id,
    const TfToken& fieldName) const
{
    VtValue result;

    Has(id, fieldName, &result);

    return result;
}

void
GEO_FileData::Set(
    const SdfPath& id,
    const TfToken& fieldName,
    const VtValue& value)
{
    UNSUPPORTED(Set);
}

void
GEO_FileData::Set(
    const SdfPath& id,
    const TfToken& fieldName,
    const SdfAbstractDataConstValue& value)
{
    UNSUPPORTED(Set);
}

void
GEO_FileData::Erase(
    const SdfPath& id,
    const TfToken& fieldName)
{
    UNSUPPORTED(Erase);
}

std::vector<TfToken>
GEO_FileData::List(const SdfPath& id) const
{
    TfTokenVector result;

    if (auto prim = getPrim(id))
    {
	if (id.IsPropertyPath())
	{
	    if (auto prop = prim->getProp(id))
	    {
		if (prop->getIsRelationship())
		{
		    result.push_back(SdfFieldKeys->TargetPaths);
		}
		else
		{
		    if (mySampleFrameSet && !prop->getValueIsDefault())
			result.push_back(SdfFieldKeys->TimeSamples);
		    else
			result.push_back(SdfFieldKeys->Default);
		    result.push_back(SdfFieldKeys->TypeName);
		}
		result.push_back(SdfFieldKeys->Variability);

		if (!prop->getCustomData().empty())
		    result.push_back(SdfFieldKeys->CustomData);

		for (auto &&it : prop->getMetadata())
		    result.push_back(it.first);
	    }
	}
	else
	{
	    if (prim != myPseudoRoot)
	    {
		result.push_back(SdfFieldKeys->Specifier);
		result.push_back(SdfFieldKeys->TypeName);
		if (!prim->getPropNames().empty())
		    result.push_back(SdfChildrenKeys->PropertyChildren);
	    }
	    result.push_back(SdfChildrenKeys->PrimChildren);
	    if (!prim->getCustomData().empty())
	    {
		if (prim == myPseudoRoot)
		    result.push_back(SdfFieldKeys->CustomLayerData);
		else
		    result.push_back(SdfFieldKeys->CustomData);
	    }

	    for (auto &&it : prim->getMetadata())
		result.push_back(it.first);
	}
    }

    return result;
}

std::set<double>
GEO_FileData::ListAllTimeSamples() const
{
    if (mySampleFrameSet)
	return std::set<double>({mySampleFrame});

    static const std::set<double>	 theEmptySet;

    return theEmptySet;
}

std::set<double>
GEO_FileData::ListTimeSamplesForPath(const SdfPath& id) const
{
    if (mySampleFrameSet && id.IsPropertyPath())
    {
	if (auto prim = getPrim(id))
	{
	    auto prop = prim->getProp(id);

	    if (prop && !prop->getValueIsDefault())
		return std::set<double>({mySampleFrame});
	}
    }

    static const std::set<double>	 theEmptySet;

    return theEmptySet;
}

bool
GEO_FileData::GetBracketingTimeSamples(
    double time, double* tLower, double* tUpper) const
{
    if (mySampleFrameSet)
    {
	if (tLower)
	    *tLower = mySampleFrame;
	if (tUpper)
	    *tUpper = mySampleFrame;

	return true;
    }

    return false;
}

size_t
GEO_FileData::GetNumTimeSamplesForPath(
    const SdfPath& id) const
{
    if (mySampleFrameSet && id.IsPropertyPath())
    {
	if (auto prim = getPrim(id))
	{
	    auto prop = prim->getProp(id);

	    if (prop && !prop->getValueIsDefault())
		return 1u;
	}
    }

    return 0u;
}

bool
GEO_FileData::GetBracketingTimeSamplesForPath(
    const SdfPath& id,
    double time, double* tLower, double* tUpper) const
{
    if (mySampleFrameSet && id.IsPropertyPath())
    {
	if (auto prim = getPrim(id))
	{
	    auto prop = prim->getProp(id);

	    if (prop && !prop->getValueIsDefault())
	    {
		if (tLower)
		    *tLower = mySampleFrame;
		if (tUpper)
		    *tUpper = mySampleFrame;

		return true;
	    }
	}
    }

    return false;
}

bool
GEO_FileData::QueryTimeSample(
    const SdfPath& id,
    double time,
    SdfAbstractDataValue* value) const
{
    if (mySampleFrameSet && SYSisEqual(time, mySampleFrame))
    {
	if (id.IsPropertyPath())
	{
	    if (auto prim = getPrim(id))
	    {
		auto prop = prim->getProp(id);

		if (prop && !prop->getValueIsDefault())
		{
		    if (value)
			return prop->copyData(GEO_FileFieldValue(value));

		    return true;
		}
	    }
	}
    }

    return false;
}

bool
GEO_FileData::QueryTimeSample(
    const SdfPath& id,
    double time,
    VtValue* value) const
{
    if (mySampleFrameSet && SYSisEqual(time, mySampleFrame))
    {
	if (id.IsPropertyPath())
	{
	    if (auto prim = getPrim(id))
	    {
		auto prop = prim->getProp(id);

		if (prop && !prop->getValueIsDefault())
		{
		    if (value)
			return prop->copyData(GEO_FileFieldValue(value));

		    return true;
		}
	    }
	}
    }

    return false;
}

void
GEO_FileData::SetTimeSample(
    const SdfPath& id,
    double time,
    const VtValue& value)
{
    UNSUPPORTED(SetTimeSample);
}

void
GEO_FileData::EraseTimeSample(const SdfPath& id, double time)
{
    UNSUPPORTED(EraseTimeSample);
}

const GEO_FilePrim *
GEO_FileData::getPrim(const SdfPath& id) const
{
    GEO_FilePrimMap::const_iterator it;

    if (id == SdfPath::AbsoluteRootPath())
        it = myPrims.find(id);
    else
        it = myPrims.find(id.GetPrimOrPrimVariantSelectionPath());

    if (it != myPrims.end())
	return &it->second;

    return nullptr;
}

PXR_NAMESPACE_CLOSE_SCOPE

