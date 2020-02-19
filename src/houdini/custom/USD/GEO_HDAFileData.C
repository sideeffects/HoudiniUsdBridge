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

#include "GEO_HDAFileData.h"
#include "GEO_FileFieldValue.h"
#include "GEO_FilePropSource.h"
#include "GEO_FileRefiner.h"
#include "GEO_HAPIReader.h"
#include "GEO_HAPIUtils.h"
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

GEO_HDAFileData::GEO_HDAFileData()
    : myPseudoRoot(nullptr), mySampleFrame(0.0), mySampleFrameSet(false)
{
}

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

bool
GEO_HDAFileData::Open(const std::string &filePath)
{
    GEO_HAPIReader *currentReader = nullptr;

    // Check if relavent HAPI data has already been saved
    for (int i = 0; i < theReaders.size(); i++)
    {
        if (theReaders[i].checkReusable(filePath))
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
        if (!currentReader->readHAPI(filePath))
        {
            // This reader was unable to load the data, so don't save it
            theReaders.pop_front();
            return false;
        }
    }

    std::string origPathWithArgs = 
	SdfLayer::CreateIdentifier(filePath, myCookArgs);

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
        // TODO: allow user to specify which assets in the hda to load
        const GEO_HAPIGeoArray *geoArray = currentReader->getGeos();
        GEO_HAPIGeo *geos = geoArray->getArray();

        GEO_HAPIPrimCounts counts;

        for (exint g = 0; g < geoArray->entries(); g++)
        {
            // Find and display all parts (prims) in each geometry
            const GEO_HAPIPartArray *partArray = geos[g].getParts();
            GEO_HAPIPart *parts = partArray->getArray();

            for (exint p = 0; p < partArray->entries(); p++)
            {
                GEO_HAPIPart::partToPrim(parts[p], options, defaultPath,
                                         myPrims, origPathWithArgs, counts);
            }
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

    return true;
}

// TODO: Move all functions below to a base class 

void
GEO_HDAFileData::CreateSpec(const SdfPath &id, SdfSpecType specType)
{
    UNSUPPORTED(CreateSpec);
}

bool
GEO_HDAFileData::HasSpec(const SdfPath &id) const
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
GEO_HDAFileData::EraseSpec(const SdfPath &id)
{
    UNSUPPORTED(EraseSpec);
}

void
GEO_HDAFileData::MoveSpec(const SdfPath &oldId, const SdfPath &newId)
{
    UNSUPPORTED(MoveSpec);
}

SdfSpecType
GEO_HDAFileData::GetSpecType(const SdfPath &id) const
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
GEO_HDAFileData::_VisitSpecs(SdfAbstractDataSpecVisitor *visitor) const
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
                if (!visitor->VisitSpec(
                        *this, primit->first.AppendProperty(propit->first)))
                    return;
            }
        }
    }
}

bool
GEO_HDAFileData::_Has(const SdfPath &id,
                          const TfToken &fieldName,
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
                            VtValue tmp;
                            GEO_FileFieldValue tmpval(&tmp);
                            SdfTimeSampleMap samples;

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
GEO_HDAFileData::Has(const SdfPath &id,
                         const TfToken &fieldName,
                         SdfAbstractDataValue *value) const
{
    return _Has(id, fieldName, GEO_FileFieldValue(value));
}

bool
GEO_HDAFileData::Has(const SdfPath &id,
                         const TfToken &fieldName,
                         VtValue *value) const
{
    return _Has(id, fieldName, GEO_FileFieldValue(value));
}

VtValue
GEO_HDAFileData::Get(const SdfPath &id, const TfToken &fieldName) const
{
    VtValue result;

    Has(id, fieldName, &result);

    return result;
}

void
GEO_HDAFileData::Set(const SdfPath &id,
                         const TfToken &fieldName,
                         const VtValue &value)
{
    UNSUPPORTED(Set);
}

void
GEO_HDAFileData::Set(const SdfPath &id,
                         const TfToken &fieldName,
                         const SdfAbstractDataConstValue &value)
{
    UNSUPPORTED(Set);
}

void
GEO_HDAFileData::Erase(const SdfPath &id, const TfToken &fieldName)
{
    UNSUPPORTED(Erase);
}

std::vector<TfToken>
GEO_HDAFileData::List(const SdfPath &id) const
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
GEO_HDAFileData::ListAllTimeSamples() const
{
    if (mySampleFrameSet)
        return std::set<double>({mySampleFrame});

    static const std::set<double> theEmptySet;

    return theEmptySet;
}

std::set<double>
GEO_HDAFileData::ListTimeSamplesForPath(const SdfPath &id) const
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

    static const std::set<double> theEmptySet;

    return theEmptySet;
}

bool
GEO_HDAFileData::GetBracketingTimeSamples(double time,
                                              double *tLower,
                                              double *tUpper) const
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
GEO_HDAFileData::GetNumTimeSamplesForPath(const SdfPath &id) const
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
GEO_HDAFileData::GetBracketingTimeSamplesForPath(const SdfPath &id,
                                                     double time,
                                                     double *tLower,
                                                     double *tUpper) const
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
GEO_HDAFileData::QueryTimeSample(const SdfPath &id,
                                     double time,
                                     SdfAbstractDataValue *value) const
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
GEO_HDAFileData::QueryTimeSample(const SdfPath &id,
                                     double time,
                                     VtValue *value) const
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
GEO_HDAFileData::SetTimeSample(const SdfPath &id,
                                   double time,
                                   const VtValue &value)
{
    UNSUPPORTED(SetTimeSample);
}

void
GEO_HDAFileData::EraseTimeSample(const SdfPath &id, double time)
{
    UNSUPPORTED(EraseTimeSample);
}

const GEO_FilePrim *
GEO_HDAFileData::getPrim(const SdfPath &id) const
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