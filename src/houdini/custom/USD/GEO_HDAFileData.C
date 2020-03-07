/*
 * Copyright 2020 Side Effects Software Inc.
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

#include "GEO_FileFieldValue.h"
#include "GEO_FilePropSource.h"
#include "GEO_FileRefiner.h"
#include "GEO_HAPIReader.h"
#include "GEO_HAPIUtils.h"
#include "GEO_HDAFileData.h"
#include <GT/GT_DAIndirect.h>
#include <GT/GT_RefineParms.h>
#include <HUSD/HUSD_Constants.h>
#include <HUSD/XUSD_TicketRegistry.h>
#include <HUSD/XUSD_Utils.h>
#include <OP/OP_Director.h>
#include <SYS/SYS_Math.h>
#include <SYS/SYS_ParseNumber.h>
#include <UT/UT_Format.h>
#include <UT/UT_Matrix4.h>
#include <UT/UT_WorkArgs.h>
#include <UT/UT_WorkBuffer.h>
#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/tf/pathUtils.h>
#include <pxr/usd/sdf/schema.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdVol/tokens.h>

PXR_NAMESPACE_OPEN_SCOPE

#define UNSUPPORTED(M)                                                         \
    TF_RUNTIME_ERROR("Houdini geometry file " #M "() not supported")

#define MAX_CACHED_READERS 3

// TODO: This is a temporary solution for saving Houdini Engine Data
static std::deque<GEO_HAPIReader> theReaders;

GEO_HDAFileData::GEO_HDAFileData() {}

GEO_HDAFileData::~GEO_HDAFileData() {}

GEO_HDAFileDataRefPtr
GEO_HDAFileData::New(const SdfFileFormat::FileFormatArguments &args)
{
    auto data = TfCreateRefPtr(new GEO_HDAFileData);
    auto timeit = args.find("t");

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

bool
getCookOption(const SdfFileFormat::FileFormatArguments *args,
              const UT_StringRef &argname,
              std::string &value)
{
    if (args && argname.isstring())
    {
        auto argit = args->find(argname.toStdString());

        if (argit != args->end())
        {
            value = argit->second;

            return true;
        }
    }

    return false;
}

void
GEO_HDAFileData::configureOptions(GEO_ImportOptions &options)
{
    std::string cook_option;
    UT_String path_attr_str;
    UT_WorkArgs path_attr_args;

    // Only grab the sample frame from the gdp if we weren't passed
    // a value in the args used to open the file.
    if (!mySampleFrameSet)
    {
        if (getCookOption(&myCookArgs, "sampleframe", cook_option))
        {
            mySampleFrame = SYSatof(cook_option.c_str());
            mySampleFrameSet = true;
            mySaveSampleFrame = true;
        }
    }

    if (getCookOption(&myCookArgs, "pathattr", cook_option))
        path_attr_str = cook_option;
    else
        path_attr_str = HUSD_Constants::getDefaultBgeoPathAttr();
    path_attr_str.tokenize(path_attr_args, ", \n\t");
    for (int i = 0; i < path_attr_args.getArgc(); i++)
        options.myPathAttrNames.append(path_attr_args.getArg(i));

    if (getCookOption(&myCookArgs, "pathprefix", cook_option))
    {
        options.myPrefixPath = HUSDgetSdfPath(cook_option);
        if (options.myPrefixPath.IsEmpty())
            options.myPrefixPath = SdfPath::AbsoluteRootPath();
        else
            options.myPrefixPath = options.myPrefixPath.MakeAbsolutePath(
                SdfPath::AbsoluteRootPath());
    }
    else
        options.myPrefixPath =
            HUSDgetSdfPath(HUSD_Constants::getDefaultBgeoPathPrefix());

    bool globalauthortimesamples = true;
    if (getCookOption(&myCookArgs, "globalauthortimesamples", cook_option))
    {
        globalauthortimesamples = (cook_option != "0");
    }

    if (getCookOption(&myCookArgs, "polygonsassubd", cook_option))
        options.myPolygonsAsSubd = (cook_option != "0");

    if (getCookOption(&myCookArgs, "subdgroup", cook_option))
        options.mySubdGroup = cook_option;

    if (getCookOption(&myCookArgs, "reversepolygons", cook_option))
        options.myReversePolygons = (cook_option != "0");

    if (getCookOption(&myCookArgs, "topology", cook_option))
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

    if (getCookOption(&myCookArgs, "usdprims", cook_option))
    {
        if (cook_option == "ignore")
            options.myUsdHandling = GEO_USD_PACKED_IGNORE;
        else if (cook_option == "xform")
            options.myUsdHandling = GEO_USD_PACKED_XFORM;
    }

    if (getCookOption(&myCookArgs, "packedprims", cook_option))
    {
        if (cook_option == "xforms")
            options.myPackedPrimHandling = GEO_PACKED_XFORMS;
        else if (cook_option == "pointinstancer")
            options.myPackedPrimHandling = GEO_PACKED_POINTINSTANCER;
        else if (cook_option == "nativeinstances")
            options.myPackedPrimHandling = GEO_PACKED_NATIVEINSTANCES;
    }

    if (getCookOption(&myCookArgs, "kindschema", cook_option))
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

    if (getCookOption(&myCookArgs, "otherprims", cook_option))
    {
        GEOconvertTokenToEnum(
            TfToken(cook_option), options.myOtherPrimHandling);
        if (options.myOtherPrimHandling == GEO_OTHER_XFORM)
        {
            // We don't want to author kind information when we are only
            // asked for xform override prims.
            options.myKindSchema = GEO_KINDSCHEMA_NONE;
        }
    }

    if (getCookOption(&myCookArgs, "defineonlyleafprims", cook_option))
    {
        options.myDefineOnlyLeafPrims = (cook_option != "0");
    }

    if (getCookOption(&myCookArgs, "group", cook_option))
        options.myImportGroup = cook_option;

    if (getCookOption(&myCookArgs, "attribs", cook_option))
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
    else if (getCookOption(&myCookArgs, "staticattribs", cook_option) &&
             !cook_option.empty())
    {
        options.myStaticAttribs.compile(cook_option.c_str());
    }

    if (getCookOption(&myCookArgs, "constantattribs", cook_option) &&
        !cook_option.empty())
        options.myConstantAttribs.compile(cook_option.c_str());

    if (getCookOption(&myCookArgs, "indexattribs", cook_option) &&
        !cook_option.empty())
        options.myIndexAttribs.compile(cook_option.c_str());

    if (getCookOption(&myCookArgs, "partitionattribs", cook_option) &&
        !cook_option.empty())
        options.myPartitionAttribs.compile(cook_option.c_str());

    if (getCookOption(&myCookArgs, "subsetgroups", cook_option) &&
        !cook_option.empty())
        options.mySubsetGroups.compile(cook_option.c_str());

    if (getCookOption(&myCookArgs, "translateuvtost", cook_option))
        options.myTranslateUVToST = (cook_option != "0");
}

// Assuming argsOut is initially empty, it will be filled with a map containing
// only arguments needed by a GEO_HAPIReader
static void
getNodeParms(const SdfFileFormat::FileFormatArguments &allArgs,
             GEO_HAPIParameterMap &argsOut)
{
    typedef SdfFileFormat::FileFormatArguments::const_iterator iterator;

    iterator end = allArgs.end();
    for (iterator it = allArgs.begin(); it != end; it++)
    {
        const std::string &argName = it->first;
        const std::string &argVal = it->second;
        // If the argument name has a parameter prefix, add it to argsOut
        if (TfStringStartsWith(argName, GEO_HDA_PARM_ARG_PREFIX))
        {
            argsOut[argName] = argVal;
        }
    }
}

bool
GEO_HDAFileData::Open(const std::string &filePath)
{
    GEO_HAPIReader *currentReader = nullptr;

    // Extract the file format arguments that define parameter values for the
    // hda. These will be applied before cooking the asset nodes
    GEO_HAPIParameterMap nodeParmArgs;
    getNodeParms(myCookArgs, nodeParmArgs);

    // Check if relavent HAPI data has already been saved
    for (int i = 0; i < theReaders.size(); i++)
    {
        if (theReaders[i].checkReusable(filePath, nodeParmArgs))
        {
            currentReader = &theReaders[i];
            break;
        }
    }

    if (!currentReader)
    {
        theReaders.push_front(GEO_HAPIReader());
        currentReader = &theReaders.front();

        if (theReaders.size() > MAX_CACHED_READERS)
        {
            theReaders.pop_back();
        }

        // This is where the geometry from the hda is extracted
        // This will take a long time
        if (!currentReader->readHAPI(filePath, nodeParmArgs))
        {
            // This reader was unable to load the data, so don't save it
            theReaders.pop_front();
            return false;
        }
    }

    std::string origPathWithArgs = SdfLayer::CreateIdentifier(
        filePath, myCookArgs);

    GEO_ImportOptions options;

    // Make a prim for our pseudo root.
    myPseudoRoot = &myPrims[SdfPath::AbsoluteRootPath()];
    myPseudoRoot->setPath(SdfPath::AbsoluteRootPath());

    // Make a prim for holding our layer info.
    myLayerInfoPrim = &myPrims[SdfPath(
        HUSD_Constants::getHoudiniLayerInfoPrimPath().toStdString())];
    myLayerInfoPrim->setPath(
        SdfPath(HUSD_Constants::getHoudiniLayerInfoPrimPath().toStdString()));
    myLayerInfoPrim->setTypeName(
        TfToken(HUSD_Constants::getHoudiniLayerInfoPrimType().toStdString()));
    myLayerInfoPrim->setInitialized();

    // setup options based on file format args
    configureOptions(options);

    SdfPath defaultPath;

    // No point in outputting our path attributes.
    for (auto &&path_attr_name : options.myPathAttrNames)
        options.myProcessedAttribs.insert(path_attr_name);
    // Attributes that we never want to output as primvars.
    options.myProcessedAttribs.insert("varmap");
    options.myProcessedAttribs.insert("usdsavepath");

    if (options.myPrefixPath != SdfPath::AbsoluteRootPath())
        defaultPath = options.myPrefixPath;
    else
        defaultPath = SdfPath::AbsoluteRootPath();

    while (defaultPath != SdfPath::AbsoluteRootPath() &&
           !defaultPath.IsRootPrimPath())
        defaultPath = defaultPath.GetParentPath();

    GEOinitRootPrim(*myPseudoRoot, defaultPath.GetNameToken(),
                    mySaveSampleFrame, mySampleFrame);

    GEO_HandleOtherPrims parents_primhandling = options.myOtherPrimHandling;
    GEO_KindSchema parents_kind = options.myKindSchema;
    if (options.myDefineOnlyLeafPrims)
    {
        parents_primhandling = GEO_OTHER_OVERLAY;
        parents_kind = GEO_KINDSCHEMA_NONE;
    }

    // Create a Xform prim to act as a parent prim for all parts
    // This will also avoid warnings when loading empty geometry
    GEO_FilePrim &filePrim(myPrims[defaultPath]);
    filePrim.setPath(defaultPath);
    GEOinitXformPrim(filePrim, parents_primhandling, parents_kind);

    if (currentReader->hasPrim())
    {
        // Get all displaying geometries from the asset
        GEO_HAPIGeoArray &geoArray = currentReader->getGeos();

        GEO_HAPIPrimCounts counts;

        for (exint g = 0; g < geoArray.entries(); g++)
        {
            // Find and display all parts (prims) in each geometry
            GEO_HAPIPartArray &partArray = geoArray(g).getParts();
            GEO_HAPIPointInstancerData piData(partArray, myPrims);

            for (exint p = 0; p < partArray.entries(); p++)
            {
                GEO_HAPIPart::partToPrim(partArray(p), options, defaultPath,
                                         myPrims, origPathWithArgs, counts,
                                         piData);
            }

            piData.initRelationships(myPrims);
        }

        // Set up parent-child relationships.
        for (auto &&it : myPrims)
        {
            SdfPath parentpath = it.first.GetParentPath();

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
                        GEOinitXformPrim(
                            it.second, parents_primhandling, parents_kind);
                    }

                    // Special override of the Kind of root primitives. We can't
                    // set the Kind of the pseudo root prim, so don't try.
                    if (options.myOtherPrimHandling == GEO_OTHER_DEFINE &&
                        !options.myDefineOnlyLeafPrims &&
                        it.first.IsRootPrimPath())
                        GEOsetKind(
                            it.second, options.myKindSchema, GEO_KINDGUIDE_TOP);
                }
            }
        }
    }

    return true;
}

PXR_NAMESPACE_CLOSE_SCOPE