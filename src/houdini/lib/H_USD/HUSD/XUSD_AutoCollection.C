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

#include "XUSD_AutoCollection.h"
#include "HUSD_DataHandle.h"
#include "HUSD_FindPrims.h"
#include "HUSD_Info.h"
#include "HUSD_Path.h"
#include "HUSD_PathSet.h"
#include "XUSD_AttributeUtils.h"
#include "XUSD_Data.h"
#include "XUSD_FindPrimsTask.h"
#include "XUSD_Utils.h"
#include "UT/UT_EnvControl.h"

#include <gusd/UT_Gf.h>
#include <FS/UT_DSO.h>
#include <BV/BV_Overlap.h>
#include <UT/UT_Interrupt.h>
#include <UT/UT_Matrix3.h>
#include <UT/UT_Matrix4.h>
#include <UT/UT_Vector3.h>
#include <UT/UT_ParallelUtil.h>
#include <UT/UT_StringMMPattern.h>
#include <UT/UT_ThreadSpecificValue.h>
#include <UT/UT_WorkArgs.h>
#include <SYS/SYS_Hash.h>
#include <SYS/SYS_Math.h>
#include <SYS/SYS_ParseNumber.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/pcp/layerStack.h>
#include <pxr/usd/pcp/node.h>
#include <pxr/usd/usd/collectionAPI.h>
#include <pxr/usd/usd/modelAPI.h>
#include <pxr/usd/usd/primCompositionQuery.h>
#include <pxr/usd/usd/variantSets.h>
#include <pxr/usd/usdGeom/camera.h>
#include <pxr/usd/usdGeom/imageable.h>
#include <pxr/usd/usdGeom/bboxCache.h>
#include <pxr/usd/usdGeom/pointInstancer.h>
#include <pxr/usd/usdGeom/primvar.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/usd/usdRender/product.h>
#include <pxr/usd/usdRender/settings.h>
#include <pxr/usd/usdShade/material.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>
#include <pxr/usd/kind/registry.h>
#include <pxr/base/gf/bbox3d.h>
#include <pxr/base/gf/camera.h>
#include <pxr/base/gf/frustum.h>
#include <cmath>

PXR_NAMESPACE_OPEN_SCOPE

namespace {
    static bool                      thePluginsInitialized = false;
    static const char                theSimpleNameTerminator = ':';
    static const char                theFunctionNameTerminator = '(';
    static const char               *theSimpleArgSeparators = ":,";
    static const char               *theFunctionArgSeparators = ",";
    static const UT_StringHolder     theOpenBraces = "({[";
    static const UT_StringHolder     theCloseBraces = ")}]";

    UsdRenderSettings
    getRenderSettingsPrim(HUSD_AutoAnyLock &lock,
        const SdfPath &explicit_path = SdfPath())
    {
        HUSD_Info info(lock);
        HUSD_Path settingspath = info.getBestRenderSettings(
            explicit_path.IsEmpty()
                ? UT_StringHolder::theEmptyString
                : explicit_path.GetAsString());

        if (lock.isStageValid() && !settingspath.isEmpty())
            return UsdRenderSettings(lock.constData()->stage()->
                GetPrimAtPath(settingspath.sdfPath()));

        return UsdRenderSettings();
    }

    bool
    cameraMayBeTimeVarying(const UsdGeomCamera &camera)
    {
        for (auto &&attr: {
            camera.GetClippingRangeAttr(),
            camera.GetClippingPlanesAttr(),
            camera.GetHorizontalApertureAttr(),
            camera.GetHorizontalApertureOffsetAttr(),
            camera.GetVerticalApertureAttr(),
            camera.GetVerticalApertureOffsetAttr(),
            camera.GetProjectionAttr(),
            camera.GetFocalLengthAttr()
        })
        {
            if (attr && attr.ValueMightBeTimeVarying())
                return true;
        }
        if (HUSDisTimeSampled(
                HUSDgetBoundsTimeSampling(camera.GetPrim(), true)))
            return true;
        return false;
    }

    void
    extractCameraFrustums(const UsdGeomCamera &camera,
                          const std::vector<UsdTimeCode> &timecodes,
                          const UT_StringMap<UT_StringHolder> &namedargs,
                          std::vector<GfFrustum> &frustums)
    {
        frustums.clear();
        frustums.reserve(timecodes.size());

        auto dollyit = namedargs.find("dolly");
        auto fovscaleit = namedargs.find("fovscale");

        bool dodolly = (dollyit != namedargs.end());
        bool dofovscale = (fovscaleit != namedargs.end());

        GfFrustum frustum;
        GfCamera gfcam;

        for (auto &&timecode : timecodes)
        {
            gfcam = camera.GetCamera(timecode);

            if (dofovscale)
            {
                // Need to modify the GfCamera directly, since the
                // UsdGeomCamera is from a locked stage
                float focallength = gfcam.GetFocalLength();
                float fovscale = SYSatof(fovscaleit->second);
                if (fovscale <= 0.0001)
                    fovscale = 0.0001;
                // focal length is proportional to the inverse of
                // field of view.
                focallength /= fovscale;
                gfcam.SetFocalLength(focallength);
            }

            frustum = gfcam.GetFrustum();

            if (dodolly)
            {
                fpreal dolly = SYSatof(dollyit->second);
                UT_Matrix4D xform(1.0);
                UT_Vector3D translates;
                UT_Vector3D rotaxis(GusdUT_Gf::Cast(
                    frustum.GetRotation().GetAxis()));
                xform.translate(0.0, 0.0, dolly);
                xform.rotate(rotaxis,
                    SYSdegToRad(frustum.GetRotation().GetAngle()));
                xform.getTranslates(translates);
                translates += GusdUT_Gf::Cast(
                    frustum.GetPosition());
                frustum.SetPosition(GusdUT_Gf::Cast(translates));
                frustum.SetNearFar(
                    GfRange1d(frustum.GetNearFar().GetMin(),
                        frustum.GetNearFar().GetMax() + dolly));
            }
            frustums.push_back(frustum);
        }
    }


    // Returns true if the target of a composition arc is in the stage's root
    // layer stack, which means its path is a path on the stage. Note that we
    // don't care where the arc was introduced. An inherit authored inside a
    // referenced file creates an implied inherit to a class on the stage, so
    // that class should still be found. We also can't rely on the root node
    // of the prim index being in the list of arcs (it is inert for instance
    // proxies). We compare root layers instead of layer stacks because an arc
    // that targets the root layer by asset path has a layer stack without the
    // stage's session layer.
    bool
    isArcTargetOnStage(const UsdPrimCompositionQueryArc &arc)
    {
        PcpNodeRef target = arc.GetTargetNode();

        return target.GetLayerStack()->GetIdentifier().rootLayer ==
            target.GetRootNode().GetLayerStack()->GetIdentifier().rootLayer;
    }
};

////////////////////////////////////////////////////////////////////////////
// XUSD_AutoCollection
////////////////////////////////////////////////////////////////////////////

UT_Array<XUSD_AutoCollectionFactory *> XUSD_AutoCollection::theFactories;

XUSD_AutoCollection::XUSD_AutoCollection(
        const UT_StringHolder &collectionname,
        const UT_StringArray &orderedargs,
        const UT_StringMap<UT_StringHolder> &namedargs,
        HUSD_AutoAnyLock &lock,
        HUSD_PrimTraversalDemands demands,
        int nodeid,
        const HUSD_TimeCode &timecode)
    : myOrderedArgs(orderedargs),
      myNamedArgs(namedargs),
      myMayBeTimeVaryingSubPattern(false),
      myLock(lock),
      myDemands(demands),
      myNodeId(nodeid),
      myHusdTimeCode(timecode),
      myUsdTimeCode(HUSDgetNonDefaultUsdTimeCode(timecode))
{
}

XUSD_AutoCollection::~XUSD_AutoCollection()
{
}

bool
XUSD_AutoCollection::canCreateAutoCollection(const char *token)
{
    UT_String collectionname(token);
    const char *endsimpletoken =
        collectionname.findChar(theSimpleNameTerminator);
    const char *endfunctiontoken =
        collectionname.findChar(theFunctionNameTerminator);
    const char *endtoken = endsimpletoken;

    if (!endtoken || (endfunctiontoken && endfunctiontoken < endsimpletoken))
        endtoken = endfunctiontoken;
    if (endtoken)
        collectionname.truncate(endtoken - token);

    for(auto &&factory : theFactories)
        if (factory->canCreateAutoCollection(collectionname))
            return true;

    return false;
}

XUSD_AutoCollection *
XUSD_AutoCollection::create(const char *token,
        HUSD_AutoAnyLock &lock,
        HUSD_PrimTraversalDemands demands,
        int nodeid,
        const HUSD_TimeCode &timecode)
{
    UT_String splittoken(token);
    UT_StringArray splittokens;
    UT_StringArray orderedargs;
    UT_StringMap<UT_StringHolder> namedargs;
    UT_StringHolder collectionname;
    UT_WorkBuffer nexttoken;
    UT_String argseparators;
    UT_Array<char> expectednextbraces;
    char endofstringmarker = '\0';
    const char *tokenend = token;
    bool nexttoken_has_assignment = false;
    char ch = '\0';

    for (; ; tokenend++)
    {
        ch = *tokenend;

        if (collectionname.isstring())
        {
            if (ch == '\0' ||
                (expectednextbraces.isEmpty() && ch == endofstringmarker) ||
                (expectednextbraces.isEmpty() && argseparators.findChar(ch)))
            {
                const char *namedargsplit = nullptr;

                if (nexttoken_has_assignment)
                    namedargsplit = nexttoken.findChar('=');

                if (namedargsplit)
                {
                    UT_String argname(nexttoken.buffer(), false,
                        (intptr_t)(namedargsplit - nexttoken.buffer()));
                    UT_String argvalue(namedargsplit+1);

                    argname.trimBoundingSpace();
                    argvalue.trimBoundingSpace();
                    namedargs[argname] = argvalue;
                }
                else
                {
                    UT_String argvalue(nexttoken.buffer());

                    argvalue.trimBoundingSpace();
                    orderedargs.append(argvalue);
                }
                nexttoken_has_assignment = false;
                nexttoken.clear();
                if (ch == '\0' || ch == endofstringmarker)
                    break;
                continue;
            }
            else if (!expectednextbraces.isEmpty() &&
                     ch == expectednextbraces.last())
            {
                expectednextbraces.removeLast();
            }
            else if (theOpenBraces.findCharIndex(ch) >= 0)
            {
                expectednextbraces.append(
                    theCloseBraces.c_str()[theOpenBraces.findCharIndex(ch)]);
            }
        }
        else
        {
            // Until we reach a ":" or a "(", we are still building the
            // collection name.
            if (ch == '\0' ||
                ch == theSimpleNameTerminator ||
                ch == theFunctionNameTerminator)
            {
                if (ch == theFunctionNameTerminator)
                {
                    argseparators = theFunctionArgSeparators;
                    endofstringmarker = ')';
                }
                else
                    argseparators = theSimpleArgSeparators;
                collectionname = nexttoken;
                nexttoken_has_assignment = false;
                nexttoken.clear();
                if (ch == '\0')
                    break;
                continue;
            }
        }

        // Record any "=" signs outside of braces, which indicates that the
        // parameter is a named argument.
        if (ch == '=' && expectednextbraces.isEmpty())
            nexttoken_has_assignment = true;
        nexttoken.append(ch);
    }

    XUSD_AutoCollection *ac = nullptr;

    if (collectionname.isstring())
    {
        for (auto &&factory : theFactories)
        {
            ac = factory->create(collectionname,
                orderedargs, namedargs, lock, demands, nodeid, timecode);
            if (ac)
                break;
        }
    }

    if (ac)
    {
        if (!expectednextbraces.isEmpty())
        {
            ac->myTokenParsingError =
                "Open parenthesis without matching close parenthesis.";
        }
        else if (ch == '\0' && endofstringmarker != '\0')
        {
            ac->myTokenParsingError =
                "Missing end of function-style token.";
        }
        else if (ch != '\0' && ch == endofstringmarker)
        {
            if (UT_String(tokenend+1).findNonSpace())
            {
                ac->myTokenParsingError =
                    "Extra characters after end of function-style token.";
            }
        }
    }

    return ac;
}

void
XUSD_AutoCollection::registerPlugin(XUSD_AutoCollectionFactory *factory)
{
    theFactories.append(factory);
}

bool
XUSD_AutoCollection::parseBool(const UT_StringRef &str)
{
    if (str.equal("false", true) ||
        str.equal("no", true) ||
        str.equal("0"))
        return false;

    return true;
}

bool
XUSD_AutoCollection::parseInt(const UT_StringRef &str, exint &i)
{
    const char *end = nullptr;

    return (SYSparseInteger(str.c_str(), end, i) == SYS_ParseStatus::Success);
}

bool
XUSD_AutoCollection::parseFloat(const UT_StringRef &str, fpreal64 &flt)
{
    const char *end = nullptr;

    return (SYSparseFloat(str.c_str(), end, flt) == SYS_ParseStatus::Success);
}

bool
XUSD_AutoCollection::parseVector2(const UT_StringRef &str, UT_Vector2D &vec)
{
    UT_String parsestr(str.c_str());
    UT_WorkArgs args;

    parsestr.tokenize(args, " \t\n()[]{},");
    if (args.getArgc() == 2)
    {
        if (parseFloat(args.getArg(0), vec[0]) &&
            parseFloat(args.getArg(1), vec[1]))
            return true;
    }

    return false;
}

bool
XUSD_AutoCollection::parseVector3(const UT_StringRef &str, UT_Vector3D &vec)
{
    UT_String parsestr(str.c_str());
    UT_WorkArgs args;

    parsestr.tokenize(args, " \t\n()[]{},");
    if (args.getArgc() == 3)
    {
        if (parseFloat(args.getArg(0), vec[0]) &&
            parseFloat(args.getArg(1), vec[1]) &&
            parseFloat(args.getArg(2), vec[2]))
            return true;
    }

    return false;
}

bool
XUSD_AutoCollection::parseVector4(const UT_StringRef &str, UT_Vector4D &vec)
{
    UT_String parsestr(str.c_str());
    UT_WorkArgs args;

    parsestr.tokenize(args, " \t\n()[]{},");
    if (args.getArgc() == 4)
    {
        if (parseFloat(args.getArg(0), vec[0]) &&
            parseFloat(args.getArg(1), vec[1]) &&
            parseFloat(args.getArg(2), vec[2]) &&
            parseFloat(args.getArg(3), vec[3]))
            return true;
    }

    return false;
}

bool
XUSD_AutoCollection::parseTimeRange(const UT_StringRef &str,
    fpreal64 &tstart, fpreal64 &tend, fpreal64 &tstep)
{
    // The time range argument can be (tstart), (tstart, tend), or
    // (tstart, tend, tstep).
    if (str.isstring())
    {
        UT_Vector3D timev3;
        UT_Vector2D timev2;

        if (parseVector3(str, timev3))
        {
            tstart = timev3.x();
            tend = timev3.y();
            tstep = timev3.z();
            return true;
        }
        else if (parseVector2(str, timev2))
        {
            tstart = timev2.x();
            tend = timev2.y();
            return true;
        }
        else if (parseFloat(str, tstart))
        {
            tend = tstart;
            return true;
        }
    }

    // This will ensure any iteration through the time range will result
    // in no valid time sample times.
    tend = tstart - 1.0;

    return false;
}

bool
XUSD_AutoCollection::parsePattern(const UT_StringRef &str,
        HUSD_AutoAnyLock &lock,
        HUSD_PrimTraversalDemands demands,
        int nodeid,
        const HUSD_TimeCode &timecode,
        bool respect_instance_proxy_demands,
        XUSD_PathSet &paths,
        bool *timevaryingflag)
{
    HUSD_FindPrims   findprims(lock);

    // respect_instance_proxy_demands will be true for patterns that can never
    // turn instance proxy prims in their sub-patterns into non-instance-proxy
    // prims in the final result (e.g. %descendants of instance proxy prims are
    // always going to be more instance proxy prims).
    if (!respect_instance_proxy_demands &&
        UT_EnvControl::getInt(ENV_HOUDINI_ALLOW_INSTANCES_IN_SUBPATTERNS))
    {
        // When parsing sub-patterns, allow instance proxies if there is any
        // chance that matches to instance proxies will lead to matches of
        // non-instance proxies in the outer pattern. The calling pattern
        // might use these as breadcrumbs to find non-instance-proxy prims.
        demands = HUSD_PrimTraversalDemands(demands |
            HUSD_TRAVERSAL_ALLOW_INSTANCE_PROXIES);
    }
    findprims.setTraversalDemands(demands);
    findprims.addPattern(str, nodeid, timecode);
    const auto &foundpaths = findprims.getExpandedPathSet().sdfPathSet();
    paths.insert(foundpaths.begin(), foundpaths.end());

    if (timevaryingflag)
        *timevaryingflag |= findprims.getIsTimeVarying();
    
    return true;
}

bool
XUSD_AutoCollection::parsePatternSingleResult(const UT_StringRef &str,
        HUSD_AutoAnyLock &lock,
        HUSD_PrimTraversalDemands demands,
        int nodeid,
        const HUSD_TimeCode &timecode,
        bool respect_instance_proxy_demands,
        SdfPath &path,
        bool *timevaryingflag)
{
    HUSD_FindPrims   findprims(lock);

    // respect_instance_proxy_demands will be true for patterns that can never
    // turn instance proxy prims in their sub-patterns into non-instance-proxy
    // prims in the final result (e.g. %descendants of instance proxy prims are
    // always going to be more instance proxy prims).
    if (!respect_instance_proxy_demands &&
        UT_EnvControl::getInt(ENV_HOUDINI_ALLOW_INSTANCES_IN_SUBPATTERNS))
    {
        // When parsing sub-patterns, allow instance proxies if there is any
        // chance that matches to instance proxies will lead to matches of
        // non-instance proxies in the outer pattern. The calling pattern
        // might use these as breadcrumbs to find non-instance-proxy prims.
        demands = HUSD_PrimTraversalDemands(demands |
            HUSD_TRAVERSAL_ALLOW_INSTANCE_PROXIES);
    }
    findprims.setTraversalDemands(demands);
    findprims.addPattern(str, nodeid, timecode);
    if (!findprims.getExpandedPathSet().empty())
        path = *findprims.getExpandedPathSet().sdfPathSet().begin();
    
    if (timevaryingflag)
        *timevaryingflag |= findprims.getIsTimeVarying();

    return true;
}

////////////////////////////////////////////////////////////////////////////
// XUSD_SimpleAutoCollection
////////////////////////////////////////////////////////////////////////////

XUSD_SimpleAutoCollection::XUSD_SimpleAutoCollection(
        const UT_StringHolder &collectionname,
        const UT_StringArray &orderedargs,
        const UT_StringMap<UT_StringHolder> &namedargs,
        HUSD_AutoAnyLock &lock,
        HUSD_PrimTraversalDemands demands,
        int nodeid,
        const HUSD_TimeCode &timecode)
    : XUSD_AutoCollection(collectionname, orderedargs, namedargs,
          lock, demands, nodeid, timecode)
{
}

XUSD_SimpleAutoCollection::~XUSD_SimpleAutoCollection()
{
}

void
XUSD_SimpleAutoCollection::matchPrimitives(XUSD_PathSet &matches) const
{
    UsdStageRefPtr stage = myLock.constData()->stage();
    UsdPrim root = stage->GetPseudoRoot();
    auto predicate = HUSDgetUsdPrimPredicate(myDemands);

    if (root)
    {
        XUSD_FindPrimPathsTaskData data;

        XUSDfindPrims(root, data, predicate, this);

        data.gatherPathsFromThreads(matches);
    }
}

////////////////////////////////////////////////////////////////////////////
// XUSD_RandomAccessAutoCollection
////////////////////////////////////////////////////////////////////////////

XUSD_RandomAccessAutoCollection::XUSD_RandomAccessAutoCollection(
       const UT_StringHolder &collectionname,
       const UT_StringArray &orderedargs,
       const UT_StringMap<UT_StringHolder> &namedargs,
       HUSD_AutoAnyLock &lock,
       HUSD_PrimTraversalDemands demands,
       int nodeid,
       const HUSD_TimeCode &timecode)
   : XUSD_AutoCollection(collectionname, orderedargs, namedargs,
       lock, demands, nodeid, timecode)
{
}

XUSD_RandomAccessAutoCollection::~XUSD_RandomAccessAutoCollection()
{
}

bool
XUSD_RandomAccessAutoCollection::matchRandomAccessPrimitive(
        const SdfPath &path,
        bool *prune_branch) const
{
    UsdStageRefPtr stage = myLock.constData()->stage();
    UsdPrim prim = stage->GetPrimAtPath(path);

    if (prim)
        return matchPrimitive(prim, prune_branch);

    // We should never be passed an invalid/non-existent prim path.
    UT_ASSERT(false);
    *prune_branch = true;
    return false;
}

////////////////////////////////////////////////////////////////////////////
// XUSD_KindAutoCollection
////////////////////////////////////////////////////////////////////////////

class XUSD_KindAutoCollection : public XUSD_SimpleAutoCollection
{
public:
    XUSD_KindAutoCollection(
           const UT_StringHolder &collectionname,
           const UT_StringArray &orderedargs,
           const UT_StringMap<UT_StringHolder> &namedargs,
           HUSD_AutoAnyLock &lock,
           HUSD_PrimTraversalDemands demands,
           int nodeid,
           const HUSD_TimeCode &timecode)
       : XUSD_SimpleAutoCollection(collectionname, orderedargs, namedargs,
           lock, demands, nodeid, timecode)
    {
        auto strictit = namedargs.find("strict");
        myAllRequestedKindsAreModels = true;
        myStrict = false;
        if (strictit != namedargs.end())
            myStrict = parseBool(strictit->second);

        UT_StringArray invalidkinds;
        for (auto &&orderedarg : orderedargs)
        {
            TfToken kind(orderedarg);
            // We allow the user to search for invalid kinds, but we also
            // want to flag them in case this is just a typo.
            myRequestedKinds.push_back(kind);
            if (!KindRegistry::HasKind(kind))
            {
                invalidkinds.append(orderedarg);
                myRequestedKindIsModel.append(0);
            }
            else
                myRequestedKindIsModel.append(
                    KindRegistry::IsA(kind, KindTokens->model) ? 1 : 0);
            if (!myRequestedKindIsModel.last())
                myAllRequestedKindsAreModels = false;
        }
        if (!invalidkinds.isEmpty())
        {
            UT_WorkBuffer msgbuf;
            msgbuf.append("Unknown kinds: ");
            msgbuf.append(invalidkinds, ", ");
            myTokenParsingError = msgbuf.buffer();
        }
    }
    ~XUSD_KindAutoCollection() override
    { }

    bool matchPrimitive(const UsdPrim &prim,
            bool *prune_branch) const override
    {
        if (!myRequestedKinds.empty())
        {
            UsdModelAPI model(prim);

            if (model)
            {
                TfToken kind;

                if (model.GetKind(&kind))
                {
                    for (int i = 0, n = myRequestedKinds.size(); i < n; i++)
                    {
                        const auto &requestedkind = myRequestedKinds[i];
                        bool requestedismodel = myRequestedKindIsModel[i];

                        if (requestedismodel && !model.IsModel())
                            continue;

                        if (myStrict)
                        {
                            if (kind == requestedkind)
                                return true;
                        }
                        else
                        {
                            if (KindRegistry::IsA(kind, requestedkind))
                                return true;
                        }
                    }
                }

                // If we are only looking for model-kind prims, we can
                // prune if this prim isn't a model. Note that
                // model.IsModel check a flag on the prim that is only
                // set if the model hierarchy is valid to this prim.
                *prune_branch = myAllRequestedKindsAreModels &&
                                !model.IsModel();
            }
            else
            {
                // If we are only looking for model-kind prims, and we hit a
                // prim that can't apply the model API, we can stop looking
                // because the prim definitely isn't a model, and so none
                // of its descendants will be either.
                *prune_branch = myAllRequestedKindsAreModels;
            }
        }
        else
        {
            // If we aren't looking for any kinds, we won't match anything
            // ever, so we might as well prune.
            *prune_branch = true;
        }

        // This prim didn't match.
        return false;
    }

private:
    TfTokenVector        myRequestedKinds;
    UT_IntArray          myRequestedKindIsModel;
    bool                 myAllRequestedKindsAreModels;
    bool                 myStrict;
};

////////////////////////////////////////////////////////////////////////////
// XUSD_PrimTypeAutoCollection
////////////////////////////////////////////////////////////////////////////

class XUSD_PrimTypeAutoCollection : public XUSD_RandomAccessAutoCollection
{
public:
    XUSD_PrimTypeAutoCollection(
           const UT_StringHolder &collectionname,
           const UT_StringArray &orderedargs,
           const UT_StringMap<UT_StringHolder> &namedargs,
           HUSD_AutoAnyLock &lock,
           HUSD_PrimTraversalDemands demands,
           int nodeid,
           const HUSD_TimeCode &timecode)
       : XUSD_RandomAccessAutoCollection(collectionname, orderedargs, namedargs,
           lock, demands, nodeid, timecode),
         myAcceptTypeless(false)
    {
        auto strictit = namedargs.find("strict");
        myStrict = false;
        if (strictit != namedargs.end())
            myStrict = parseBool(strictit->second);

        UT_StringArray invalidtypes;
        for (int i = 0; i < orderedargs.size(); i++)
        {
            auto arg = orderedargs[i];
            if (arg.equal("None", true))
            {
                myAcceptTypeless = true;
                continue;
            }
            if (arg == "Light")
                arg = "LightAPI";

            const TfType &tfprimtype = HUSDfindType(arg);

            if (!tfprimtype.IsUnknown() &&
                UsdSchemaRegistry::IsTyped(tfprimtype))
                myPrimTypes.append(&tfprimtype);
            else if (!tfprimtype.IsUnknown() &&
                UsdSchemaRegistry::IsAppliedAPISchema(tfprimtype))
                myAPISchemaTypes.append(&tfprimtype);
            else
                invalidtypes.append(orderedargs[i]);
        }
        if (!invalidtypes.isEmpty())
        {
            UT_WorkBuffer msgbuf;
            msgbuf.append("Unknown types: ");
            msgbuf.append(invalidtypes, ", ");
            myTokenParsingError = msgbuf.buffer();
        }
    }
    ~XUSD_PrimTypeAutoCollection() override
    { }

    bool matchPrimitive(const UsdPrim &prim,
            bool *prune_branch) const override
    {
        for (auto &&apischema : myAPISchemaTypes)
            if (prim.HasAPI(*apischema))
                return true;

        if (prim.GetTypeName() == TfToken())
            return myAcceptTypeless;

        if (myStrict)
        {
            for (auto &&primtype : myPrimTypes)
                if (prim.GetPrimTypeInfo().GetSchemaType() == *primtype)
                    return true;
        }
        else
        {
            for (auto &&primtype : myPrimTypes)
                if (prim.IsA(*primtype))
                    return true;
        }

        return false;
    }

private:
    UT_Array<const TfType *>     myPrimTypes;
    UT_Array<const TfType *>     myAPISchemaTypes;
    bool                         myAcceptTypeless;
    bool                         myStrict;
};

////////////////////////////////////////////////////////////////////////////
// XUSD_ShaderTypeAutoCollection
////////////////////////////////////////////////////////////////////////////

class XUSD_ShaderTypeAutoCollection : public XUSD_RandomAccessAutoCollection
{
public:
    XUSD_ShaderTypeAutoCollection(
           const UT_StringHolder &collectionname,
           const UT_StringArray &orderedargs,
           const UT_StringMap<UT_StringHolder> &namedargs,
           HUSD_AutoAnyLock &lock,
           HUSD_PrimTraversalDemands demands,
           int nodeid,
           const HUSD_TimeCode &timecode)
       : XUSD_RandomAccessAutoCollection(collectionname, orderedargs, namedargs,
           lock, demands, nodeid, timecode)
    {
        for (int i = 0; i < orderedargs.size(); i++)
            myShaderTypes.append(orderedargs[i]);
    }
    ~XUSD_ShaderTypeAutoCollection() override
    { }

    bool matchPrimitive(const UsdPrim &prim,
        bool *prune_branch) const override
    {
        if (!myShaderTypes.isEmpty())
        {
            UsdShadeShader shader(prim);

            if (shader)
            {
                TfToken id;

                if (shader.GetShaderId(&id))
                {
                    UT_String idstr(id.GetText());

                    for (auto &&shadertype : myShaderTypes)
                        if (idstr.multiMatch(shadertype, false))
                            return true;
                }
            }
        }

        return false;
    }

private:
    UT_StringArray           myShaderTypes;
};

////////////////////////////////////////////////////////////////////////////
// XUSD_VisibleAutoCollection
////////////////////////////////////////////////////////////////////////////

class XUSD_VisibleAutoCollection : public XUSD_RandomAccessAutoCollection
{
public:
    XUSD_VisibleAutoCollection(
           const UT_StringHolder &collectionname,
           const UT_StringArray &orderedargs,
           const UT_StringMap<UT_StringHolder> &namedargs,
           HUSD_AutoAnyLock &lock,
           HUSD_PrimTraversalDemands demands,
           int nodeid,
           const HUSD_TimeCode &timecode)
       : XUSD_RandomAccessAutoCollection(collectionname, orderedargs, namedargs,
           lock, demands, nodeid, timecode)
    {
        myVisibility = true;
        if (orderedargs.size() > 0)
            myVisibility = parseBool(orderedargs(0));
    }
    ~XUSD_VisibleAutoCollection() override
    { }

    bool matchPrimitive(const UsdPrim &prim,
        bool *prune_branch) const override
    {
        bool visibility = computeVisibility(myUsdTimeCode, prim,
            myVisibilityCache.get(), myMayBeTimeVarying.get());

        // If we are looking for visible prims, and we hit an invisible prim,
        // we know there will be no more visible prims showing up further down
        // this branch. In all other cases, we may (or know for sure that we
        // will) find more matches.
        if (myVisibility && !visibility && prune_branch)
            *prune_branch = true;

        // In spite of how we track visibility for non-imageable prims, we
        // don't actually want to return any non-imageable prims as matches
        // for this pattern, whether we've asked for visible prims or not.
        return (visibility == myVisibility && prim.IsA<UsdGeomImageable>());
    }

    bool getMayBeTimeVarying() const override
    {
        if (XUSD_AutoCollection::getMayBeTimeVarying())
            return true;
        
        for (auto &&maybetimevarying : myMayBeTimeVarying)
            if (maybetimevarying)
                return true;
        return false;
    }

private:
    typedef std::map<SdfPath, bool> VisibilityMap;

    static bool
    computeVisibility(
        const UsdTimeCode &timecode,
        const UsdPrim &prim,
        VisibilityMap &map,
        bool &maybetimevarying)
    {
        auto it = map.find(prim.GetPath());

        if (it == map.end())
        {
            UsdPrim parent = prim.GetParent();

            if (parent)
            {
                bool parent_visibility = computeVisibility(
                    timecode, parent, map, maybetimevarying);
                UsdGeomImageable imageable(prim);

                // If we aren't imageable, or our parent isn't visible,
                // we just inherit our parent's visibility value.
                if (imageable && parent_visibility)
                {
                    UsdAttribute visibilityattr = imageable.GetVisibilityAttr();
                    bool visibility = true;

                    if (visibilityattr)
                    {
                        TfToken visibilityvalue;
                        if (visibilityattr.Get(&visibilityvalue, timecode))
                        {
                            if (visibilityvalue == UsdGeomTokens->invisible)
                                visibility = false;
                            maybetimevarying |=
                                visibilityattr.ValueMightBeTimeVarying();
                        }
                    }
                    it = map.emplace(prim.GetPath(), visibility).first;
                }
                else
                    it = map.emplace(prim.GetPath(), parent_visibility).first;
            }
            else
                it = map.emplace(prim.GetPath(), true).first;
        }

        return it->second;
    }

    bool                                             myVisibility;
    mutable UT_ThreadSpecificValue<VisibilityMap>    myVisibilityCache;
    mutable UT_ThreadSpecificValue<bool>             myMayBeTimeVarying;
};

////////////////////////////////////////////////////////////////////////////
// XUSD_DefinedAutoCollection
////////////////////////////////////////////////////////////////////////////

class XUSD_DefinedAutoCollection : public XUSD_RandomAccessAutoCollection
{
public:
    XUSD_DefinedAutoCollection(
           const UT_StringHolder &collectionname,
           const UT_StringArray &orderedargs,
           const UT_StringMap<UT_StringHolder> &namedargs,
           HUSD_AutoAnyLock &lock,
           HUSD_PrimTraversalDemands demands,
           int nodeid,
           const HUSD_TimeCode &timecode)
       : XUSD_RandomAccessAutoCollection(collectionname, orderedargs, namedargs,
           lock, demands, nodeid, timecode)
    {
        myDefined = true;
        if (orderedargs.size() > 0)
            myDefined = parseBool(orderedargs(0));
    }
    ~XUSD_DefinedAutoCollection() override
    { }

    bool matchPrimitive(const UsdPrim &prim,
        bool *prune_branch) const override
    {
        bool result = (prim.IsDefined() == myDefined);

        // If we are looking for defined prims, and we hit an undefined prim,
        // we know there will be no more defined prims showing up further down
        // this branch. In all other cases, we may (or know for sure that we
        // will) find more matches.
        if (myDefined && !result && prune_branch)
            *prune_branch = true;

        return result;
    }

private:
    bool             myDefined;
};

////////////////////////////////////////////////////////////////////////////
// XUSD_ActiveAutoCollection
////////////////////////////////////////////////////////////////////////////

class XUSD_ActiveAutoCollection : public XUSD_RandomAccessAutoCollection
{
public:
    XUSD_ActiveAutoCollection(
        const UT_StringHolder &collectionname,
        const UT_StringArray &orderedargs,
        const UT_StringMap<UT_StringHolder> &namedargs,
        HUSD_AutoAnyLock &lock,
        HUSD_PrimTraversalDemands demands,
        int nodeid,
        const HUSD_TimeCode &timecode)
        : XUSD_RandomAccessAutoCollection(collectionname, orderedargs, namedargs,
              lock, demands, nodeid, timecode)
    {
        myActive = true;
        if (orderedargs.size() > 0)
            myActive = parseBool(orderedargs(0));
    }
    ~XUSD_ActiveAutoCollection() override
    { }

    bool matchPrimitive(const UsdPrim &prim,
        bool *prune_branch) const override
    {
        return (prim.IsActive() == myActive);
    }

private:
    bool             myActive;
};

////////////////////////////////////////////////////////////////////////////
// XUSD_AbstractAutoCollection
////////////////////////////////////////////////////////////////////////////

class XUSD_AbstractAutoCollection : public XUSD_RandomAccessAutoCollection
{
public:
    XUSD_AbstractAutoCollection(
           const UT_StringHolder &collectionname,
           const UT_StringArray &orderedargs,
           const UT_StringMap<UT_StringHolder> &namedargs,
           HUSD_AutoAnyLock &lock,
           HUSD_PrimTraversalDemands demands,
           int nodeid,
           const HUSD_TimeCode &timecode)
       : XUSD_RandomAccessAutoCollection(collectionname, orderedargs, namedargs,
           lock, demands, nodeid, timecode)
    {
        myAbstract = true;
        if (orderedargs.size() > 0)
            myAbstract = parseBool(orderedargs(0));
    }
    ~XUSD_AbstractAutoCollection() override
    { }

    bool matchPrimitive(const UsdPrim &prim,
        bool *prune_branch) const override
    {
        bool result = (prim.IsAbstract() == myAbstract);

        // If we are looking for non-abstract prims, and we hit an abstract
        // prim, we know there will be no more non-abstract prims showing up
        // further down this branch. In all other cases, we may (or know for
        // sure that we will) find more matches.
        if (!myAbstract && !result && prune_branch)
            *prune_branch = true;

        return result;
    }

private:
    bool             myAbstract;
};

////////////////////////////////////////////////////////////////////////////
// XUSD_SpecifierAutoCollection
////////////////////////////////////////////////////////////////////////////

class XUSD_SpecifierAutoCollection : public XUSD_RandomAccessAutoCollection
{
public:
    XUSD_SpecifierAutoCollection(
           const UT_StringHolder &collectionname,
           const UT_StringArray &orderedargs,
           const UT_StringMap<UT_StringHolder> &namedargs,
           HUSD_AutoAnyLock &lock,
           HUSD_PrimTraversalDemands demands,
           int nodeid,
           const HUSD_TimeCode &timecode)
       : XUSD_RandomAccessAutoCollection(collectionname, orderedargs, namedargs,
           lock, demands, nodeid, timecode)
    {
        UT_StringArray invalidspecifiers;
        for (int i = 0; i < orderedargs.size(); i++)
        {
            bool valid = true;
            SdfSpecifier specifier= HUSDgetSdfSpecifier(orderedargs[i], &valid);

            if (valid)
                mySpecifiers.append(specifier);
            else
                invalidspecifiers.append(orderedargs[i]);
        }
        if (!invalidspecifiers.isEmpty())
        {
            UT_WorkBuffer msgbuf;
            msgbuf.append("The following specifier(s) do not exist: ");
            msgbuf.append(invalidspecifiers, ", ");
            myTokenParsingError = msgbuf.buffer();
        }
    }
    ~XUSD_SpecifierAutoCollection() override
    { }

    bool matchPrimitive(const UsdPrim &prim,
        bool *prune_branch) const override
    {
        if (mySpecifiers.find(prim.GetSpecifier()) >= 0)
            return true;

        return false;
    }

private:
    UT_Array<SdfSpecifier>   mySpecifiers;
};

////////////////////////////////////////////////////////////////////////////
// XUSD_PurposeAutoCollection
////////////////////////////////////////////////////////////////////////////

class XUSD_PurposeAutoCollection : public XUSD_RandomAccessAutoCollection
{
public:
    XUSD_PurposeAutoCollection(
           const UT_StringHolder &collectionname,
           const UT_StringArray &orderedargs,
           const UT_StringMap<UT_StringHolder> &namedargs,
           HUSD_AutoAnyLock &lock,
           HUSD_PrimTraversalDemands demands,
           int nodeid,
           const HUSD_TimeCode &timecode)
       : XUSD_RandomAccessAutoCollection(collectionname, orderedargs, namedargs,
           lock, demands, nodeid, timecode)
    {
        const auto &allpurposes = UsdGeomImageable::GetOrderedPurposeTokens();
        UT_StringArray invalidpurposes;
        for (int i = 0; i < orderedargs.size(); i++)
        {
            TfToken tfpurpose(orderedargs[i].toStdString());

            if (std::find(allpurposes.begin(), allpurposes.end(), tfpurpose) !=
                allpurposes.end())
                myPurposes.push_back(tfpurpose);
            else
                invalidpurposes.append(orderedargs[i]);
        }
        if (!invalidpurposes.isEmpty())
        {
            UT_WorkBuffer msgbuf;
            msgbuf.append("Unknown purposes: ");
            msgbuf.append(invalidpurposes, ", ");
            myTokenParsingError = msgbuf.buffer();
        }
    }
    ~XUSD_PurposeAutoCollection() override
    { }

    bool matchPrimitive(const UsdPrim &prim,
            bool *prune_branch) const override
    {
        const auto &info = computePurposeInfo(myPurposeInfoCache.get(), prim);
        auto it = std::find(myPurposes.begin(), myPurposes.end(), info.purpose);

        return (it != myPurposes.end());
    }

private:
    typedef std::map<SdfPath, UsdGeomImageable::PurposeInfo> PurposeInfoMap;

    static const UsdGeomImageable::PurposeInfo &
    computePurposeInfo(PurposeInfoMap &map, const UsdPrim &prim)
    {
        auto it = map.find(prim.GetPath());

        if (it == map.end())
        {
            UsdPrim parent = prim.GetParent();

            if (parent)
            {
                const auto &parent_info = computePurposeInfo(map, parent);
                UsdGeomImageable imageable(prim);

                if (imageable)
                    it = map.emplace(prim.GetPath(),
                        imageable.ComputePurposeInfo(parent_info)).first;
                else
                    it = map.emplace(prim.GetPath(), parent_info).first;
            }
            else
                it = map.emplace(prim.GetPath(),
                    UsdGeomImageable::PurposeInfo()).first;
        }

        return it->second;
    }

    TfTokenVector                                    myPurposes;
    mutable UT_ThreadSpecificValue<PurposeInfoMap>   myPurposeInfoCache;
};

////////////////////////////////////////////////////////////////////////////
// XUSD_AuthoredPurposeAutoCollection
////////////////////////////////////////////////////////////////////////////

class XUSD_AuthoredPurposeAutoCollection : public XUSD_RandomAccessAutoCollection
{
public:
    XUSD_AuthoredPurposeAutoCollection(
        const UT_StringHolder &collectionname,
        const UT_StringArray &orderedargs,
        const UT_StringMap<UT_StringHolder> &namedargs,
        HUSD_AutoAnyLock &lock,
        HUSD_PrimTraversalDemands demands,
        int nodeid,
        const HUSD_TimeCode &timecode)
        : XUSD_RandomAccessAutoCollection(collectionname, orderedargs, namedargs,
              lock, demands, nodeid, timecode)
    {
        const auto &allpurposes = UsdGeomImageable::GetOrderedPurposeTokens();
        UT_StringArray invalidpurposes;
        for (int i = 0; i < orderedargs.size(); i++)
        {
            TfToken tfpurpose(orderedargs[i].toStdString());

            if (std::find(allpurposes.begin(), allpurposes.end(), tfpurpose) !=
                allpurposes.end())
                myPurposes.push_back(tfpurpose);
            else
                invalidpurposes.append(orderedargs[i]);
        }
        if (!invalidpurposes.isEmpty())
        {
            UT_WorkBuffer msgbuf;
            msgbuf.append("Unknown purposes: ");
            msgbuf.append(invalidpurposes, ", ");
            myTokenParsingError = msgbuf.buffer();
        }
    }
    ~XUSD_AuthoredPurposeAutoCollection() override
    { }

    bool matchPrimitive(const UsdPrim &prim,
        bool *prune_branch) const override
    {
        UsdGeomImageable imageable(prim);
        if (imageable)
        {
            TfToken purpose;

            if (imageable.GetPurposeAttr().Get(&purpose))
            {
                auto purposeit = std::find(
                    myPurposes.begin(), myPurposes.end(), purpose);

                return (purposeit != myPurposes.end());
            }
        }

        return false;
    }

private:
    TfTokenVector                                    myPurposes;
};

////////////////////////////////////////////////////////////////////////////
// XUSD_PayloadAutoCollection
////////////////////////////////////////////////////////////////////////////

class XUSD_PayloadAutoCollection : public XUSD_RandomAccessAutoCollection
{
public:
    XUSD_PayloadAutoCollection(
        const UT_StringHolder &collectionname,
        const UT_StringArray &orderedargs,
        const UT_StringMap<UT_StringHolder> &namedargs,
        HUSD_AutoAnyLock &lock,
        HUSD_PrimTraversalDemands demands,
        int nodeid,
        const HUSD_TimeCode &timecode)
        : XUSD_RandomAccessAutoCollection(collectionname, orderedargs, namedargs,
              lock, demands, nodeid, timecode)
    {
        // We are only interested in direct payload compositions authored on
        // this prim. We don't care about variants, references, inherits, or
        // specializes.
        myQueryFilter.arcTypeFilter =
            UsdPrimCompositionQuery::ArcTypeFilter::Payload;
        myQueryFilter.dependencyTypeFilter =
            UsdPrimCompositionQuery::DependencyTypeFilter::Direct;
    }
    ~XUSD_PayloadAutoCollection() override
    { }

    bool matchPrimitive(const UsdPrim &prim,
        bool *prune_branch) const override
    {
        // Quick check that this prim has at least some payload metadata
        // authored on it.
        if (prim.HasAuthoredPayloads())
        {
            // Use a UsdPrimCompositionQuery to find all composition arcs.
            UsdPrimCompositionQuery query(prim, myQueryFilter);

            return (query.GetCompositionArcs().size() > 0);
        }

        return false;
    }

private:
    UsdPrimCompositionQuery::Filter  myQueryFilter;
};

////////////////////////////////////////////////////////////////////////////
// XUSD_ReferenceAutoCollection
////////////////////////////////////////////////////////////////////////////

class XUSD_ReferenceAutoCollection : public XUSD_RandomAccessAutoCollection
{
public:
    XUSD_ReferenceAutoCollection(
           const UT_StringHolder &collectionname,
           const UT_StringArray &orderedargs,
           const UT_StringMap<UT_StringHolder> &namedargs,
           HUSD_AutoAnyLock &lock,
           HUSD_PrimTraversalDemands demands,
           int nodeid,
           const HUSD_TimeCode &timecode)
       : XUSD_RandomAccessAutoCollection(collectionname, orderedargs, namedargs,
           lock, demands, nodeid, timecode)
    {
        if (orderedargs.size() > 0)
            parsePattern(orderedargs[0],
                lock, demands, nodeid, timecode, false, myRefPaths,
                &myMayBeTimeVaryingSubPattern);

        // We are only interested in direct composition authored on this prim,
        // that may be references, inherits, or specializes. We don't care
        // about variants or payloads (though payloads come along with
        // references).
        myQueryFilter.arcTypeFilter =
            UsdPrimCompositionQuery::ArcTypeFilter::NotVariant;
        myQueryFilter.dependencyTypeFilter =
            UsdPrimCompositionQuery::DependencyTypeFilter::Direct;
    }
    ~XUSD_ReferenceAutoCollection() override
    { }

    bool matchPrimitive(const UsdPrim &prim,
            bool *prune_branch) const override
    {
        // Quick check that this prim has at least some inherit, specialize,
        // or reference metadata authored on it.
        if (prim.HasAuthoredReferences() ||
            prim.HasAuthoredInherits() ||
            prim.HasAuthoredSpecializes())
        {
            // Use a UsdPrimCompositionQuery to find all composition arcs.
            UsdPrimCompositionQuery query(prim, myQueryFilter);
            auto arcs = query.GetCompositionArcs();

            // Check arcs for reference, inherit, or specialize arcs that
            // point to the requested node in the root layer stack.
            for (int i = 0, n = arcs.size(); i < n; i++)
            {
                PcpNodeRef target = arcs[i].GetTargetNode();
                PcpArcType arctype = target.GetArcType();

                if (arctype == PcpArcTypeInherit ||
                    arctype == PcpArcTypeReference ||
                    arctype == PcpArcTypeSpecialize)
                {
                    if (myRefPaths.contains(target.GetPath()) &&
                        isArcTargetOnStage(arcs[i]))
                    {
                        return true;
                    }
                }
            }
        }

        return false;
    }

private:
    XUSD_PathSet                     myRefPaths;
    UsdPrimCompositionQuery::Filter  myQueryFilter;
};

////////////////////////////////////////////////////////////////////////////
// XUSD_ReferencedByAutoCollection
////////////////////////////////////////////////////////////////////////////

class XUSD_ReferencedByAutoCollection : public XUSD_AutoCollection
{
public:
    XUSD_ReferencedByAutoCollection(
           const UT_StringHolder &collectionname,
           const UT_StringArray &orderedargs,
           const UT_StringMap<UT_StringHolder> &namedargs,
           HUSD_AutoAnyLock &lock,
           HUSD_PrimTraversalDemands demands,
           int nodeid,
           const HUSD_TimeCode &timecode)
       : XUSD_AutoCollection(collectionname, orderedargs, namedargs,
           lock, demands, nodeid, timecode)
    {
        if (orderedargs.size() > 0)
            parsePattern(orderedargs[0],
                lock, demands, nodeid, timecode, false, myRefPaths,
                &myMayBeTimeVaryingSubPattern);

        // We are only interested in direct composition authored on this prim,
        // that may be references, inherits, or specializes. We don't care
        // about variants or payloads (though payloads come along with
        // references).
        myQueryFilter.arcTypeFilter =
            UsdPrimCompositionQuery::ArcTypeFilter::NotVariant;
        myQueryFilter.dependencyTypeFilter =
            UsdPrimCompositionQuery::DependencyTypeFilter::Direct;
    }
    ~XUSD_ReferencedByAutoCollection() override
    { }

    bool randomAccess() const override
    { return false; }

    void matchPrimitives(XUSD_PathSet &matches) const override
    {
        UsdStageRefPtr stage = myLock.constData()->stage();
        UT_AutoInterrupt boss("Primitive pattern evaluation: %referencedby");
        int count = 0;

        for (auto &&path : myRefPaths)
        {
            if ((count++ & 0x3FF) == 0 && boss.wasInterrupted())
                break;

            UsdPrim prim = stage->GetPrimAtPath(path);

            // Quick check that this prim has at least some inherit, specialize,
            // or reference metadata authored on it.
            if (prim &&
                (prim.HasAuthoredReferences() ||
                 prim.HasAuthoredInherits() ||
                 prim.HasAuthoredSpecializes()))
            {
                // Use a UsdPrimCompositionQuery to find all composition arcs.
                UsdPrimCompositionQuery query(prim, myQueryFilter);
                auto arcs = query.GetCompositionArcs();
                size_t narcs = arcs.size();

                // Check arcs for reference, inherit, or specialize arcs
                // that point to the requested node in the root layer stack.
                for (int i = 0; i < narcs; i++)
                {
                    PcpNodeRef target = arcs[i].GetTargetNode();
                    PcpArcType arctype = target.GetArcType();

                    if (arctype == PcpArcTypeInherit ||
                        arctype == PcpArcTypeReference ||
                        arctype == PcpArcTypeSpecialize)
                    {
                        if (isArcTargetOnStage(arcs[i]))
                            matches.insert(target.GetPath());
                    }
                }
            }
        }
    }

private:
    XUSD_PathSet                     myRefPaths;
    UsdPrimCompositionQuery::Filter  myQueryFilter;
};

////////////////////////////////////////////////////////////////////////////
// XUSD_InstanceAutoCollection
////////////////////////////////////////////////////////////////////////////

class XUSD_InstanceAutoCollection : public XUSD_RandomAccessAutoCollection
{
public:
    XUSD_InstanceAutoCollection(
           const UT_StringHolder &collectionname,
           const UT_StringArray &orderedargs,
           const UT_StringMap<UT_StringHolder> &namedargs,
           HUSD_AutoAnyLock &lock,
           HUSD_PrimTraversalDemands demands,
           int nodeid,
           const HUSD_TimeCode &timecode)
       : XUSD_RandomAccessAutoCollection(collectionname, orderedargs, namedargs,
           lock, demands, nodeid, timecode)
    {
        if (orderedargs.size() > 0)
            parsePatternSingleResult(orderedargs[0],
                lock, demands, nodeid, timecode, false, mySrcPath,
                &myMayBeTimeVaryingSubPattern);
        initialize(lock);
    }

    ~XUSD_InstanceAutoCollection() override
    { }

    bool matchPrimitive(const UsdPrim &prim,
            bool *prune_branch) const override
    {
        if (mySrcPath.IsEmpty())
        {
            // No source prim means find any instance primitive.
            return prim.IsInstance();
        }
        else
        {
            // Exit immediately and stop searching this branch if the source
            // prim was specified but it doesn't have a prototype.
            if (myPrototypePath.IsEmpty())
            {
                *prune_branch = true;
                return false;
            }

            if (prim.GetPrototype().GetPath() == myPrototypePath ||
                prim.GetPrimInPrototype().GetPath() == myPrototypePath)
            {
                // A child of an instance prim can't have that same prim as an
                // instance again.
                *prune_branch = true;
                return true;
            }
        }

        return false;
    }

private:
    void initialize(HUSD_AutoAnyLock &lock)
    {
        if (lock.constData() && lock.constData()->isStageValid())
        {
            UsdStageRefPtr  stage = lock.constData()->stage();
            UsdPrim         prim = stage->GetPrimAtPath(mySrcPath);
            UsdPrim         prototype = prim ? prim.GetPrototype() : UsdPrim();

            // If the prim doesn't have a prototype, check if it is an
            // instance proxy with a corresponding prim inside a prototype.
            if (!prototype)
                prototype = prim ? prim.GetPrimInPrototype() : UsdPrim();
            if (prototype)
                myPrototypePath = prototype.GetPath();
        }
    }

    SdfPath                                      mySrcPath;
    SdfPath                                      myPrototypePath;
};

////////////////////////////////////////////////////////////////////////////
// XUSD_InstanceProxyAutoCollection
////////////////////////////////////////////////////////////////////////////

class XUSD_InstanceProxyAutoCollection : public XUSD_RandomAccessAutoCollection
{
public:
    XUSD_InstanceProxyAutoCollection(
           const UT_StringHolder &collectionname,
           const UT_StringArray &orderedargs,
           const UT_StringMap<UT_StringHolder> &namedargs,
           HUSD_AutoAnyLock &lock,
           HUSD_PrimTraversalDemands demands,
           int nodeid,
           const HUSD_TimeCode &timecode)
       : XUSD_RandomAccessAutoCollection(collectionname, orderedargs, namedargs,
           lock, demands, nodeid, timecode)
    {
        myInstanceProxy = true;
        if (orderedargs.size() > 0)
            myInstanceProxy = parseBool(orderedargs(0));
    }
    ~XUSD_InstanceProxyAutoCollection() override
    { }

    bool matchPrimitive(const UsdPrim &prim,
        bool *prune_branch) const override
    {
        bool result = (prim.IsInstanceProxy() == myInstanceProxy);

        // If we are looking for non-instance-proxies, and we hit an instance
        // proxy, we know there will be no more non-instance-proxy prims
        // showing up further down this branch. In all other cases, we may
        // find more matches down the hierarchy.
        if (!myInstanceProxy && !result && prune_branch)
            *prune_branch = true;

        return result;
    }

private:
    bool             myInstanceProxy;
};

////////////////////////////////////////////////////////////////////////////
// XUSD_BoundAutoCollection
////////////////////////////////////////////////////////////////////////////

class XUSD_BoundAutoCollection : public XUSD_RandomAccessAutoCollection
{
public:
    XUSD_BoundAutoCollection(
           const UT_StringHolder &collectionname,
           const UT_StringArray &orderedargs,
           const UT_StringMap<UT_StringHolder> &namedargs,
           HUSD_AutoAnyLock &lock,
           HUSD_PrimTraversalDemands demands,
           int nodeid,
           const HUSD_TimeCode &timecode)
       : XUSD_RandomAccessAutoCollection(collectionname, orderedargs, namedargs,
           lock, demands, nodeid, timecode),
         myBoundsType(INVALID),
         myTimeCodesOverridden(false),
         myBoundsPrimIsTimeVarying(false)
    {
        if (orderedargs.size() > 0)
            parsePatternSingleResult(orderedargs[0],
                lock, demands, nodeid, timecode, false, myPath,
                &myMayBeTimeVaryingSubPattern);
        initialize(lock, namedargs);
    }
    ~XUSD_BoundAutoCollection() override
    { }

    bool matchPrimitive(const UsdPrim &prim,
            bool *prune_branch) const override
    {
        if (myBoundsType == INVALID)
        {
            // No bounds type means no bounds comparisons. Nothing matches,
            // and our pattern isn't time dependent.
            *prune_branch = true;
            return false;
        }

        BBoxCacheVector &bboxcache = myBBoxCache.get();

        if (!myTimeCodesOverridden &&
            !myBoundsPrimIsTimeVarying &&
            !myMayBeTimeVarying.get())
        {
            HUSD_Path husdpath(prim.GetPath());

            // If we have already determined the /foo is time invariant, that
            // means every descendant of /foo must also be time invariant.
            if (!myTimeInvariantPrims.get().containsPathOrAncestor(husdpath))
            {
                auto prim_time_sampling = HUSDgetBoundsTimeSampling(prim, true);
                if (HUSDisTimeSampled(prim_time_sampling))
                    myMayBeTimeVarying.get() = true;
                else
                    myTimeInvariantPrims.get().insert(husdpath);
            }
        }

        if (bboxcache.size() == 0)
        {
            bboxcache.reserve(myTimeCodes.size());
            for (auto &&timecode : myTimeCodes)
                bboxcache.emplace_back(timecode,
                    UsdGeomImageable::GetOrderedPurposeTokens(), true, true);
        }

        for (size_t i = 0; i < myTimeCodes.size(); i++)
        {
            // Note that we can break out of the loop over our time codes
            // here. If we ever meet the condition at any time code, then
            // we are in the "collection".
            GfBBox3d primbox = bboxcache[i].ComputeWorldBound(prim);
            if (myBoundsType == FRUSTUM)
            {
                // We only want to actually match imageable prims. This is the
                // level at which it is possible to compute a meaningful bound.
                if (myFrustum[i].Intersects(primbox))
                    return prim.IsA<UsdGeomImageable>();
            }
            else if (myBoundsType == BOX)
            {
                UT_Vector3D bmin = GusdUT_Gf::Cast(primbox.GetRange().GetMin());
                UT_Vector3D bmax = GusdUT_Gf::Cast(primbox.GetRange().GetMax());
                UT_Matrix4D bxform = GusdUT_Gf::Cast(primbox.GetMatrix());
                UT_Vector3D bdelta = (bmax + bmin) * 0.5;
                bxform.pretranslate(bdelta);

                // Transform the prim bbox into the space of the main bbox.
                // Scale the prim bbox at the origin before extracting the
                // translations and rotations, which are the only transforms
                // that can be passed to doBoxBoxOverlap.
                UT_Matrix4D dxform = bxform * myBoxIXform[i];
                UT_Matrix3D dscale;
                if (dxform.makeRigidMatrix(&dscale))
                {
                    UT_Vector3D dtrans;
                    UT_Matrix3D drot(dxform);
                    drot.makeRotationMatrix();
                    dxform.getTranslates(dtrans);

                    UT_Vector3D rb = SYSabs(bmin - bdelta);
                    rb *= dscale;

                    // We only want to actually match imageable prims. This is
                    // the level at which it is possible to compute a
                    // meaningful bound.
                    if (BV_Overlap::doBoxBoxOverlap(myBox[i], rb, drot, dtrans))
                        return prim.IsA<UsdGeomImageable>();
                }
            }
        }

        // Handle the INVALID state, and any out-of-bounds results. If a prim
        // is out of bounds, all its children will be out of bounds too, if
        // the bounds hierarchy is authored correctly.
        *prune_branch = true;
        return false;
    }

    bool getMayBeTimeVarying() const override
    {
        if (XUSD_AutoCollection::getMayBeTimeVarying())
            return true;

        if (myBoundsPrimIsTimeVarying)
            return true;

        for (auto &&maybetimevarying : myMayBeTimeVarying)
            if (maybetimevarying)
                return true;
        return false;
    }

private:
    enum BoundsType {
        BOX,
        FRUSTUM,
        INVALID
    };
    typedef std::vector<UsdGeomBBoxCache> BBoxCacheVector;

    void initialize(HUSD_AutoAnyLock &lock,
            const UT_StringMap<UT_StringHolder> &namedargs)
    {
        auto timeit = namedargs.find("t");
        fpreal64 tstart = myUsdTimeCode.GetValue();
        fpreal64 tend = myUsdTimeCode.GetValue();
        fpreal64 tstep = 1.0;

        myBoundsPrimIsTimeVarying = false;
        if (timeit != namedargs.end())
        {
            if (!parseTimeRange(timeit->second, tstart, tend, tstep))
                myTokenParsingError = "Invalid `t` argument specified.";
            myTimeCodesOverridden = true;
        }

        // Don't do any bounds checking other than ensuring tstep will
        // eventually get us from tstart to tend. But we can end up with no
        // time codes in our array.
        if (tstep >= 0.001)
            for (fpreal t = tstart; SYSisLessOrEqual(t, tend); t += tstep)
                myTimeCodes.emplace_back(t);

        if (lock.constData() &&
            lock.constData()->isStageValid() &&
            !myTimeCodes.empty())
        {
            UT_Matrix4D ixform(1.0);
            UT_Vector3D box(0.0);

            if (myPath.IsEmpty())
            {
                auto minit = namedargs.find("min");
                auto maxit = namedargs.find("max");
                auto centerit = namedargs.find("center");
                auto sizeit = namedargs.find("size");

                if (minit != namedargs.end() &&
                    maxit != namedargs.end())
                {
                    UT_Vector3D minv, maxv;

                    if (parseVector3(minit->second, minv) &&
                        parseVector3(maxit->second, maxv))
                    {
                        UT_Vector3D centerv = (minv + maxv) * 0.5;

                        ixform.translate(-centerv);
                        myBoxIXform.insert(myBoxIXform.end(),
                            myTimeCodes.size(), ixform);
                        box = SYSabs(minv - centerv);
                        myBox.insert(myBox.end(),
                            myTimeCodes.size(), box);
                        myBoundsType = BOX;
                    }
                    else
                        myTokenParsingError =
                            "Invalid `min` or `max` argument specified.";
                }
                else if (centerit != namedargs.end() &&
                         sizeit != namedargs.end())
                {
                    UT_Vector3D centerv, sizev;

                    if (parseVector3(centerit->second, centerv) &&
                        parseVector3(sizeit->second, sizev))
                    {
                        ixform.translate(-centerv);
                        myBoxIXform.insert(myBoxIXform.end(),
                            myTimeCodes.size(), ixform);
                        box = SYSabs(sizev * 0.5);
                        myBox.insert(myBox.end(),
                            myTimeCodes.size(), box);
                        myBoundsType = BOX;
                    }
                    else
                        myTokenParsingError =
                            "Invalid `center` or `size` argument specified.";
                }
                else
                    myTokenParsingError =
                        "No valid bounding primitive or parameters found.";
            }
            else
            {
                UsdStageRefPtr stage = lock.constData()->stage();
                UsdPrim prim = stage->GetPrimAtPath(myPath);

                UsdGeomCamera cam(prim);
                if (cam)
                {
                    if (!myTimeCodesOverridden)
                        myBoundsPrimIsTimeVarying = cameraMayBeTimeVarying(cam);

                    extractCameraFrustums(cam, myTimeCodes, namedargs, myFrustum);

                    myBoundsType = FRUSTUM;
                    return;
                }

                UsdGeomImageable imageable(prim);
                if (imageable)
                {
                    // Check if the bounding object is time varying.
                    if (!myTimeCodesOverridden &&
                        HUSDisTimeSampled(
                            HUSDgetBoundsTimeSampling(prim, true)))
                        myBoundsPrimIsTimeVarying = true;

                    myBoxIXform.reserve(myTimeCodes.size());
                    myBox.reserve(myTimeCodes.size());
                    for (auto &&timecode : myTimeCodes)
                    {
                        UsdGeomBBoxCache bboxcache(timecode,
                            UsdGeomImageable::GetOrderedPurposeTokens(), true,
                            true);

                        // Pre-calculate values from the box that we'll need for the intersection tests.
                        GfBBox3d gfbox = bboxcache.ComputeWorldBound(prim);
                        UT_Vector3D bmin = GusdUT_Gf::Cast(
                            gfbox.GetRange().GetMin());
                        UT_Vector3D bmax = GusdUT_Gf::Cast(
                            gfbox.GetRange().GetMax());
                        UT_Vector3D bcenter = (bmin + bmax) * 0.5;

                        ixform = GusdUT_Gf::Cast(gfbox.GetInverseMatrix());
                        ixform.translate(-bcenter);
                        box = SYSabs(bmin - bcenter);
                        myBoxIXform.push_back(ixform);
                        myBox.push_back(box);
                    }
                    myBoundsType = BOX;
                    return;
                }
            }
        }
    }

    BoundsType                                       myBoundsType;
    SdfPath                                          myPath;
    std::vector<UT_Matrix4D>                         myBoxIXform;
    std::vector<UT_Vector3D>                         myBox;
    std::vector<GfFrustum>                           myFrustum;
    std::vector<UsdTimeCode>                         myTimeCodes;
    bool                                             myTimeCodesOverridden;
    bool                                             myBoundsPrimIsTimeVarying;
    mutable UT_ThreadSpecificValue<BBoxCacheVector>  myBBoxCache;
    mutable UT_ThreadSpecificValue<HUSD_PathSet>     myTimeInvariantPrims;
    mutable UT_ThreadSpecificValue<bool>             myMayBeTimeVarying;
};

////////////////////////////////////////////////////////////////////////////
// XUSD_SizeAutoCollection
////////////////////////////////////////////////////////////////////////////

class XUSD_SizeAutoCollection : public XUSD_RandomAccessAutoCollection
{
public:
    XUSD_SizeAutoCollection(
           const UT_StringHolder &collectionname,
           const UT_StringArray &orderedargs,
           const UT_StringMap<UT_StringHolder> &namedargs,
           HUSD_AutoAnyLock &lock,
           HUSD_PrimTraversalDemands demands,
           int nodeid,
           const HUSD_TimeCode &timecode)
       : XUSD_RandomAccessAutoCollection(collectionname, orderedargs, namedargs,
           lock, demands, nodeid, timecode),
         myTimeCodesOverridden(false),
         mySizeType(INVALID),
         myCameraPrimIsTimeVarying(false)
    {
        if (orderedargs.size() > 0)
            parsePatternSingleResult(orderedargs[0],
                lock, demands, nodeid, timecode, false, myCameraPath,
                &myMayBeTimeVaryingSubPattern);
        initialize(lock, namedargs);
    }

    ~XUSD_SizeAutoCollection() override
    {}

    bool matchPrimitive(const UsdPrim &prim,
            bool *prune_branch) const override
    {
        if (mySizeType == INVALID)
        {
            // No size type means no size comparisons. Nothing matches,
            // and our pattern isn't time dependent.
            *prune_branch = true;
            return false;
        }

        if (prim.GetPrimPath() == myCameraPath)
            return false;

        BBoxCacheVector &bboxcache = myBBoxCache.get();

        if (!myTimeCodesOverridden &&
            !myCameraPrimIsTimeVarying &&
            !myMayBeTimeVarying.get())
        {
            HUSD_Path husdpath(prim.GetPath());

            // If we haev already determined the /foo is time invariant, that
            // means every descendant of /foo must also be time invariant.
            if (!myTimeInvariantPrims.get().containsPathOrAncestor(husdpath))
            {
                auto timesampling = HUSDgetBoundsTimeSampling(prim, true);
                if (HUSDisTimeSampled(timesampling))
                    myMayBeTimeVarying.get() = true;
                else
                    myTimeInvariantPrims.get().insert(husdpath);
            }
        }

        if (bboxcache.size() == 0)
        {
            bboxcache.reserve(myTimeCodes.size());
            for (auto &&timecode : myTimeCodes)
                bboxcache.emplace_back(timecode,
                    UsdGeomImageable::GetOrderedPurposeTokens(), true, true);
        }

        for (size_t i = 0; i < myTimeCodes.size(); ++i)
        {
            if (myPointInstancerMode != NONE && prim.IsA<UsdGeomPointInstancer>())
            {
                UsdGeomPointInstancer pointinstancer(prim);
                int numinst = pointinstancer.GetInstanceCount();

                if (myPointInstancerMode == ALL)
                {
                    bool allmatch = true;
                    for (int j=0; j<numinst; ++j)
                    {
                        GfBBox3d bbox = bboxcache[i].ComputePointInstanceWorldBound(pointinstancer, j);
                        if (mySizeType == WORLDVOLUME)
                        {
                            allmatch &= testBBoxVolume(bbox);
                            if (!allmatch)
                                break;
                        }
                        else if (mySizeType == SCREENAREA)
                        {
                            allmatch &= testBBoxScreenArea(bbox, myFrustums[i]);
                            if (!allmatch)
                                break;
                        }
                    }
                    if (allmatch)
                        return prim.IsA<UsdGeomImageable>();
                }
                else if (myPointInstancerMode == ANY)
                {
                    for (int j=0; j<numinst; ++j)
                    {
                        GfBBox3d bbox = bboxcache[i].ComputePointInstanceWorldBound(pointinstancer, j);
                        if (mySizeType == WORLDVOLUME)
                        {
                            if (testBBoxVolume(bbox))
                                return prim.IsA<UsdGeomImageable>();
                        }
                        else if (mySizeType == SCREENAREA)
                        {
                            if (testBBoxScreenArea(bbox, myFrustums[i]))
                                return prim.IsA<UsdGeomImageable>();
                        }
                    }
                }
            }

            else  // pointinstancermode == NONE
            {
                GfBBox3d bbox = bboxcache[i].ComputeWorldBound(prim);
                if (mySizeType == WORLDVOLUME)
                {
                    if (testBBoxVolume(bbox))
                        return prim.IsA<UsdGeomImageable>();
                }
                else if (mySizeType == SCREENAREA)
                {
                    if (testBBoxScreenArea(bbox, myFrustums[i]))
                        return prim.IsA<UsdGeomImageable>();
                }
            }
        }

        return false;
    }

    bool getMayBeTimeVarying() const override
    {
        if (XUSD_AutoCollection::getMayBeTimeVarying())
            return true;

        if (myCameraPrimIsTimeVarying)
            return true;

        for (auto &&maybetimevarying : myMayBeTimeVarying)
        {
            if (maybetimevarying)
                return true;
        }

        return false;
    }

private:
    enum SizeType
    {
        WORLDVOLUME,
        SCREENAREA,
        INVALID
    };

    enum PointInstancerMode
    {
        NONE,
        ANY,
        ALL
    };

    typedef std::vector<UsdGeomBBoxCache> BBoxCacheVector;

    bool testBBoxVolume(const GfBBox3d &bbox) const
    {
        fpreal64 boundvolume = bbox.GetVolume();

        if (myMinValue > 0 and myMaxValue > 0)
        {
            if(myMinValue <= boundvolume && boundvolume <= myMaxValue)
                return true;
        }

        else if (myMinValue > 0 && myMinValue <= boundvolume)
            return true;

        else if (myMaxValue >= boundvolume)
            return true;

        return false;
    }

    bool testBBoxScreenArea(const GfBBox3d &bbox, const GfFrustum &frustum) const
    {
        if (!frustum.Intersects(bbox))
            return false;

        GfRange3d range = bbox.ComputeAlignedRange();

        // compute screen co-ords of each corner of bbox
        fpreal64 minx, miny, maxx, maxy;

        GfMatrix4d viewmat = frustum.ComputeViewMatrix();

        for (int j = 0; j < 8; ++j)
        {
            GfVec3d corner = range.GetCorner(j);

            // convert point to frustum space
            GfVec4d fcorner = GfVec4d(corner[0], corner[1], corner[2], 1) * viewmat;
            fpreal dist = fcorner[2];

            std::vector<GfVec3d> fcorners = frustum.ComputeCornersAtDistance(dist);
            fpreal64 fleft   = (GfVec4d(fcorners[0][0], fcorners[0][1], fcorners[0][2], 1) * viewmat)[0];
            fpreal64 fbottom = (GfVec4d(fcorners[0][0], fcorners[0][1], fcorners[0][2], 1) * viewmat)[1];

            fpreal64 ncornerx = fcorner[0] / fleft;
            fpreal64 ncornery = fcorner[1] / fbottom;

            if (myClip)
            {
                ncornerx = std::clamp(ncornerx, -1.0, 1.0);
                ncornery = std::clamp(ncornery, -1.0, 1.0);
            }

            if (j == 0)
            {
                minx = ncornerx;
                maxx = ncornerx;
                miny = ncornery;
                maxy = ncornery;
            }
            else
            {
                if (ncornerx < minx)
                    minx = ncornerx;
                else if (ncornerx > maxx)
                    maxx = ncornerx;

                if (ncornery < miny)
                    miny = ncornery;
                else if (ncornery > maxy)
                    maxy = ncornery;
            }
        }

        fpreal64 screenarea = 100 * (maxx - minx) * (maxy - miny) / 4.0;  // divide by 4, since screen here is -1,-1 -> 1,1
        if (myMinValue > 0 && myMaxValue > 0)
        {
            if (myMinValue <= screenarea && myMaxValue >= screenarea)
                return true;
        }
        else if (myMinValue > 0 && myMinValue <= screenarea)
            return true;
        else if (myMaxValue >= screenarea)
            return true;

        return false;
    }

    void initialize(HUSD_AutoAnyLock &lock,
            const UT_StringMap<UT_StringHolder> &namedargs)
    {
        // time dependence
        auto     timeit = namedargs.find("t");
        fpreal64 tstart = myUsdTimeCode.GetValue();
        fpreal64 tend   = myUsdTimeCode.GetValue();
        fpreal64 tstep  = 1.0;

        auto pointinstancermodeit = namedargs.find("pointinstancermode") ;
        myPointInstancerMode = NONE;
        if (pointinstancermodeit != namedargs.end())
        {
            const UT_StringHolder mode = pointinstancermodeit->second;
            if (mode.equal("any", true))
                myPointInstancerMode = ANY;
            else if (mode.equal("all", true))
                myPointInstancerMode = ALL;
        }

        myCameraPrimIsTimeVarying = false;
        if (timeit != namedargs.end())
        {
            if (!parseTimeRange(timeit->second, tstart, tend, tstep))
                myTokenParsingError = "Invalid `t` argument specified.";
            myTimeCodesOverridden = true;
        }

        if (tstep >= 0.001)
            for (fpreal t = tstart; SYSisLessOrEqual(t, tend); t += tstep)
                myTimeCodes.emplace_back(t);

        if (!lock.constData() ||
                !lock.constData()->isStageValid() ||
                myTimeCodes.empty())
            return;


        if (myCameraPath.IsEmpty())
        {
            // collect min / max arguments
            auto minvolumeit = namedargs.find("minvolume");
            auto maxvolumeit = namedargs.find("maxvolume");

            bool usemin = minvolumeit != namedargs.end();
            bool usemax = maxvolumeit != namedargs.end();

            // check for existance of minvolume and/or maxvolume
            if (usemin || usemax)
            {
                if (usemin)
                {
                    if (parseFloat(minvolumeit->second, myMinValue))
                        mySizeType = WORLDVOLUME;
                    else
                        myTokenParsingError = "Invalid `minvolume` specified.";
                }

                if (usemax)
                {
                    if (parseFloat(maxvolumeit->second, myMaxValue))
                        mySizeType = WORLDVOLUME;
                    else
                        myTokenParsingError = "Invalid `maxvolume` specified.";
                }
            }
            else
                myTokenParsingError = "No valid arguments found.";
        }
        else
        {
            UsdStageRefPtr stage = lock.constData()->stage();
            UsdGeomCamera  cam(stage->GetPrimAtPath(myCameraPath));
            if (!cam)
                return;

            if (!myTimeCodesOverridden)
                myCameraPrimIsTimeVarying = cameraMayBeTimeVarying(cam);

            extractCameraFrustums(cam, myTimeCodes, namedargs, myFrustums);

            auto minareait = namedargs.find("minarea");
            auto maxareait = namedargs.find("maxarea");
            auto clipit    = namedargs.find("clip");

            bool usemin = minareait != namedargs.end();
            bool usemax = maxareait != namedargs.end();

            // check for existance of minvolume and/or maxvolume
            if (usemin || usemax)
            {
                if (usemin)
                {
                    if (!parseFloat(minareait->second, myMinValue))
                        myTokenParsingError = "Invalid `minarea` specified.";
                }

                if (usemax)
                {
                    if (!parseFloat(maxareait->second, myMaxValue))
                        myTokenParsingError = "Invalid `maxarea` specified.";
                }
            }
            else
                myTokenParsingError = "No valid arguments found.";

            myClip = clipit != namedargs.end() ? parseBool(clipit->second)
                                               : false;

            mySizeType = SCREENAREA;
        }
    }
    
    fpreal64                                        myMinValue = 0.0;
    fpreal64                                        myMaxValue = 0.0;
    bool                                            myClip = false;
    SizeType                                        mySizeType;
    PointInstancerMode                              myPointInstancerMode;
    SdfPath                                         myCameraPath;
    std::vector<UsdTimeCode>                        myTimeCodes;
    std::vector<GfFrustum>                          myFrustums;
    bool                                            myTimeCodesOverridden;
    bool                                            myCameraPrimIsTimeVarying;
    mutable UT_ThreadSpecificValue<BBoxCacheVector> myBBoxCache;
    mutable UT_ThreadSpecificValue<bool>            myMayBeTimeVarying;
    mutable UT_ThreadSpecificValue<HUSD_PathSet>    myTimeInvariantPrims;
};


////////////////////////////////////////////////////////////////////////////
// XUSD_GeoFromMatAutoCollection
////////////////////////////////////////////////////////////////////////////

class XUSD_GeoFromMatAutoCollection : public XUSD_RandomAccessAutoCollection
{
public:
    XUSD_GeoFromMatAutoCollection(
           const UT_StringHolder &collectionname,
           const UT_StringArray &orderedargs,
           const UT_StringMap<UT_StringHolder> &namedargs,
           HUSD_AutoAnyLock &lock,
           HUSD_PrimTraversalDemands demands,
           int nodeid,
           const HUSD_TimeCode &timecode)
       : XUSD_RandomAccessAutoCollection(collectionname, orderedargs, namedargs,
           lock, demands, nodeid, timecode)
    {
        if (orderedargs.size() > 0)
        {
            // Special case looking for prims with no bound material.
            if (orderedargs[0] == "none")
            {
                myMaterialUnbound = true;
            }
            else
            {
                parsePattern(orderedargs[0],
                    lock, demands, nodeid, timecode, false, myMaterialPaths,
                    &myMayBeTimeVaryingSubPattern);
                myMaterialUnbound = false;
            }
        }

        if (orderedargs.size() > 1)
        {
            myMaterialPurpose = TfToken(orderedargs[1].toStdString());
            if (myMaterialPurpose != UsdShadeTokens->allPurpose &&
                myMaterialPurpose != UsdShadeTokens->full &&
                myMaterialPurpose != UsdShadeTokens->preview)
            {
                myMaterialPurpose = UsdShadeTokens->allPurpose;
                myTokenParsingError = "Invalid material binding purpose.";
            }
        }
        else
            myMaterialPurpose = UsdShadeTokens->allPurpose;
    }
    ~XUSD_GeoFromMatAutoCollection() override
    { }

    bool matchPrimitive(const UsdPrim &prim,
            bool *prune_branch) const override
    {
        UsdShadeMaterial material =
            UsdShadeMaterialBindingAPI(prim).ComputeBoundMaterial(
                &myBindingsCache, &myCollectionCache,
                myMaterialPurpose);

        if (material)
        {
            if (!myMaterialUnbound &&
                myMaterialPaths.contains(material.GetPath()))
                return true;
        }
        else
        {
            if (myMaterialUnbound)
                return true;
        }

        return false;
    }

private:
    XUSD_PathSet                                             myMaterialPaths;
    TfToken                                                  myMaterialPurpose;
    bool                                                     myMaterialUnbound;
    mutable UsdShadeMaterialBindingAPI::BindingsCache        myBindingsCache;
    mutable UsdShadeMaterialBindingAPI::CollectionQueryCache myCollectionCache;
};

////////////////////////////////////////////////////////////////////////////
// XUSD_MatFromGeoAutoCollection
////////////////////////////////////////////////////////////////////////////

class XUSD_MatFromGeoAutoCollection : public XUSD_AutoCollection
{
public:
    XUSD_MatFromGeoAutoCollection(
           const UT_StringHolder &collectionname,
           const UT_StringArray &orderedargs,
           const UT_StringMap<UT_StringHolder> &namedargs,
           HUSD_AutoAnyLock &lock,
           HUSD_PrimTraversalDemands demands,
           int nodeid,
           const HUSD_TimeCode &timecode)
       : XUSD_AutoCollection(collectionname, orderedargs, namedargs,
           lock, demands, nodeid, timecode)
    {
        if (orderedargs.size() > 0)
        {
            parsePattern(orderedargs[0],
                lock, demands, nodeid, timecode, false, myGeoPaths,
                &myMayBeTimeVaryingSubPattern);
            myGeoPaths.removeDescendants();
        }

        if (orderedargs.size() > 1)
            myMaterialPurpose = TfToken(orderedargs[1].toStdString());
        else
            myMaterialPurpose = UsdShadeTokens->allPurpose;
    }
    ~XUSD_MatFromGeoAutoCollection() override
    { }

    bool randomAccess() const override
    { return false; }

    void matchPrimitives(XUSD_PathSet &matches) const override
    {
        UsdStageRefPtr stage = myLock.constData()->stage();
        auto predicate = HUSDgetUsdPrimPredicate(myDemands);
        std::vector<UsdPrim> prims;

        for (auto &&path : myGeoPaths)
        {
            UsdPrim root = stage->GetPrimAtPath(path);

            if (root)
            {
                XUSD_FindUsdPrimsTaskData data;

                XUSDfindPrims(root, data, predicate);

                data.gatherPrimsFromThreads(prims);
            }
        }

        if (prims.size() > 0)
        {
            std::vector<UsdShadeMaterial> materials =
                UsdShadeMaterialBindingAPI::ComputeBoundMaterials(
                    prims, myMaterialPurpose);

            for (auto &&material : materials)
            {
                if (material)
                    matches.insert(material.GetPath());
            }
        }
    }

private:
    XUSD_PathSet                     myGeoPaths;
    TfToken                          myMaterialPurpose;
};

////////////////////////////////////////////////////////////////////////////
// XUSD_RelationshipAutoCollection
////////////////////////////////////////////////////////////////////////////

class XUSD_RelationshipAutoCollection : public XUSD_AutoCollection
{
public:
    XUSD_RelationshipAutoCollection(
           const UT_StringHolder &collectionname,
           const UT_StringArray &orderedargs,
           const UT_StringMap<UT_StringHolder> &namedargs,
           HUSD_AutoAnyLock &lock,
           HUSD_PrimTraversalDemands demands,
           int nodeid,
           const HUSD_TimeCode &timecode)
       : XUSD_AutoCollection(collectionname, orderedargs, namedargs,
           lock, demands, nodeid, timecode)
    {
        if (orderedargs.size() == 2)
        {
            parsePattern(orderedargs[0],
                lock, demands, nodeid, timecode, false, myPaths,
                &myMayBeTimeVaryingSubPattern);
            myRelationshipName = TfToken(orderedargs(1).toStdString());
        }
        else if (orderedargs.size() == 1)
        {
            SdfPath path = HUSDgetSdfPath(orderedargs(0));

            myPaths.insert(path.GetPrimPath());
            myRelationshipName = path.GetNameToken();
        }
    }
    ~XUSD_RelationshipAutoCollection() override
    { }

    bool randomAccess() const override
    { return false; }

    void matchPrimitives(XUSD_PathSet &matches) const override
    {
        UsdStageRefPtr stage = myLock.constData()->stage();
        UT_AutoInterrupt boss("Primitive pattern evaluation: %relationship");
        int count = 0;

        for (auto &&path : myPaths)
        {
            if ((count++ & 0x3FF) == 0 && boss.wasInterrupted())
                break;

            SdfPath relpath(path.AppendProperty(myRelationshipName));
            UsdRelationship rel = stage->GetRelationshipAtPath(relpath);

            if (rel)
            {
                SdfPathVector targets;

                rel.GetForwardedTargets(&targets);
                for (auto &&target : targets)
                    matches.insert(target);
            }
        }
    }

private:
    XUSD_PathSet                     myPaths;
    TfToken                          myRelationshipName;
};

////////////////////////////////////////////////////////////////////////////
// XUSD_DistanceAutoCollection
////////////////////////////////////////////////////////////////////////////

template<bool CheckFartherThan>
class XUSD_DistanceAutoCollection : public XUSD_RandomAccessAutoCollection
{
public:
    XUSD_DistanceAutoCollection(
           const UT_StringHolder &collectionname,
           const UT_StringArray &orderedargs,
           const UT_StringMap<UT_StringHolder> &namedargs,
           HUSD_AutoAnyLock &lock,
           HUSD_PrimTraversalDemands demands,
           int nodeid,
           const HUSD_TimeCode &timecode)
       : XUSD_RandomAccessAutoCollection(collectionname, orderedargs, namedargs,
           lock, demands, nodeid, timecode),
         myTimeCodesOverridden(false)
    {
        if (orderedargs.size() < 2)
        {
            UT_WorkBuffer msgbuf;
            msgbuf.format(
                "Expected 2 arguments, received {}.",
                orderedargs.size());
            myTokenParsingError = msgbuf.buffer();
        }
        else
        {
            parsePatternSingleResult(orderedargs[0],
                lock, demands, nodeid, timecode, false, myPath,
                &myMayBeTimeVaryingSubPattern);
            myDistanceBound2 = pow(orderedargs[1].toFloat(), 2);
            initialize(lock, namedargs);
        }
    }
    ~XUSD_DistanceAutoCollection() override
    { }

    bool matchPrimitive(const UsdPrim &prim,
            bool *prune_branch) const override
    {
        UT_ASSERT(myTimeCodes.size() == myCenter.size());
        if (myTimeCodes.empty())
        {
            // No time codes means no distance comparisons. Nothing matches,
            // and our pattern isn't time dependent.
            *prune_branch = true;
            return false;
        }

        BBoxCacheVector &bboxcache = myBBoxCache.get();
        if (!myTimeCodesOverridden)
            myMayBeTimeVarying.get() = true;

        if (bboxcache.size() == 0)
        {
            bboxcache.reserve(myTimeCodes.size());
            for (auto &&timecode : myTimeCodes)
                bboxcache.emplace_back(timecode,
                    UsdGeomImageable::GetOrderedPurposeTokens(), true, true);
        }

        for (size_t i = 0; i < myTimeCodes.size(); i++)
        {
            GfRange3d primrange =
                bboxcache[i].ComputeWorldBound(prim).ComputeAlignedRange();
            UT_BoundingBox primbox(GusdUT_Gf::Cast(primrange.GetMin()),
                GusdUT_Gf::Cast(primrange.GetMax()));

            // We only want to actually match imageable prims. This is the
            // level at which it is possible to compute a meaningful bound.
            // Also note that we break out of the loop over our time codes
            // here. If we ever meet the condition at any time code, then
            // we are in the "collection".
            if (CheckFartherThan)
            {
                if (primbox.maxDist2(myCenter[i]) >= myDistanceBound2)
                    return prim.IsA<UsdGeomImageable>();
            }
            else
            {
                if (primbox.minDist2(myCenter[i]) <= myDistanceBound2)
                    return prim.IsA<UsdGeomImageable>();
            }
        }

        // If a prim is out of bounds - that is, its min distance is too far
        // or its max distance is too close - all its children will be out of
        // bounds too, if the bounds hierarchy is authored correctly.
        *prune_branch = true;
        return false;
    }

    bool getMayBeTimeVarying() const override
    {
        if (XUSD_AutoCollection::getMayBeTimeVarying())
            return true;
        
        for (auto &&maybetimevarying : myMayBeTimeVarying)
            if (maybetimevarying)
                return true;
        return false;
    }

private:
    typedef std::vector<UsdGeomBBoxCache> BBoxCacheVector;

    void initialize(HUSD_AutoAnyLock &lock,
            const UT_StringMap<UT_StringHolder> &namedargs)
    {
        auto timeit = namedargs.find("t");
        fpreal64 tstart = myUsdTimeCode.GetValue();
        fpreal64 tend = myUsdTimeCode.GetValue();
        fpreal64 tstep = 1.0;

        if (timeit != namedargs.end())
        {
            if (!parseTimeRange(timeit->second, tstart, tend, tstep))
                myTokenParsingError = "Invalid `t` argument specified.";
            myTimeCodesOverridden = true;
        }

        // Don't do any bounds checking other than ensuring tstep will
        // eventually get us from tstart to tend. But we can end up with no
        // time codes in our array.
        if (tstep >= 0.001)
            for (fpreal t = tstart; SYSisLessOrEqual(t, tend); t += tstep)
                myTimeCodes.emplace_back(t);

        if (lock.constData() &&
            lock.constData()->isStageValid() &&
            !myTimeCodes.empty())
        {
            UsdStageRefPtr stage = lock.constData()->stage();
            UsdPrim centerprim = stage->GetPrimAtPath(myPath);
            UsdGeomXformable xformable(centerprim);

            if (xformable)
            {
                myCenter.reserve(myTimeCodes.size());
                for (auto &&timecode : myTimeCodes)
                {
                    UT_Matrix4D xform = GusdUT_Gf::Cast(
                        xformable.ComputeLocalToWorldTransform(
                            timecode));
                    UT_Vector3D center;
                    xform.getTranslates(center);
                    myCenter.push_back(center);
                }
            }
            else
            {
                // We didn't find a prim to act as our "center", so clear the
                // time codes array as well. The length of the time codes and
                // "centers" arrays need to be the same.
                myTimeCodes.clear();
            }
        }
    }

    SdfPath                                          myPath;
    fpreal                                           myDistanceBound2;
    std::vector<UT_Vector3D>                         myCenter;
    std::vector<UsdTimeCode>                         myTimeCodes;
    bool                                             myTimeCodesOverridden;
    mutable UT_ThreadSpecificValue<BBoxCacheVector>  myBBoxCache;
    mutable UT_ThreadSpecificValue<bool>             myMayBeTimeVarying;
};

////////////////////////////////////////////////////////////////////////////
// XUSD_RelativeAutoCollection
////////////////////////////////////////////////////////////////////////////

class XUSD_RelativeAutoCollection : public XUSD_AutoCollection
{
public:
    XUSD_RelativeAutoCollection(
           const UT_StringHolder &collectionname,
           const UT_StringArray &orderedargs,
           const UT_StringMap<UT_StringHolder> &namedargs,
           HUSD_AutoAnyLock &lock,
           HUSD_PrimTraversalDemands demands,
           int nodeid,
           const HUSD_TimeCode &timecode,
           bool respect_instance_proxy_demands)
       : XUSD_AutoCollection(collectionname, orderedargs, namedargs,
           lock, demands, nodeid, timecode)
    {
        auto strictit = namedargs.find("strict");
        myStrict = true;
        if (strictit != namedargs.end())
            myStrict = parseBool(strictit->second);

        // Support "relativepath" argument which is matched against the
        // relative path to the "nearest" matching prim.

        if (orderedargs.size() > 0)
            parsePattern(orderedargs[0],
                lock, demands, nodeid, timecode, respect_instance_proxy_demands,
                myPaths, &myMayBeTimeVaryingSubPattern);
    }
    ~XUSD_RelativeAutoCollection() override
    { }

    bool randomAccess() const override
    { return false; }

protected:
    XUSD_PathSet                     myPaths;
    bool                             myStrict;
};

////////////////////////////////////////////////////////////////////////////
// XUSD_ChildrenAutoCollection
////////////////////////////////////////////////////////////////////////////

class XUSD_ChildrenAutoCollection : public XUSD_RelativeAutoCollection
{
public:
    XUSD_ChildrenAutoCollection(
           const UT_StringHolder &collectionname,
           const UT_StringArray &orderedargs,
           const UT_StringMap<UT_StringHolder> &namedargs,
           HUSD_AutoAnyLock &lock,
           HUSD_PrimTraversalDemands demands,
           int nodeid,
           const HUSD_TimeCode &timecode)
       : XUSD_RelativeAutoCollection(collectionname, orderedargs, namedargs,
           lock, demands, nodeid, timecode, true)
    { }

    void matchPrimitives(XUSD_PathSet &matches) const override
    {
        UsdStageRefPtr stage = myLock.constData()->stage();
        auto predicate = HUSDgetUsdPrimPredicate(myDemands);
        UT_AutoInterrupt boss("Primitive pattern evaluation: %children");
        int count = 0;

        if (!myStrict)
            matches = myPaths;

        for (auto &&path : myPaths)
        {
            if ((count++ & 0x3FF) == 0 && boss.wasInterrupted())
                break;

            UsdPrim root = stage->GetPrimAtPath(path);

            if (root)
            {
                TfTokenVector childnames =
                    root.GetFilteredChildrenNames(predicate);

                for (auto &&childname : childnames)
                    matches.insert(path.AppendChild(childname));
            }
        }
    }
};

////////////////////////////////////////////////////////////////////////////
// XUSD_DescendantsAutoCollection
////////////////////////////////////////////////////////////////////////////

class XUSD_DescendantsAutoCollection : public XUSD_RelativeAutoCollection
{
public:
    XUSD_DescendantsAutoCollection(
           const UT_StringHolder &collectionname,
           const UT_StringArray &orderedargs,
           const UT_StringMap<UT_StringHolder> &namedargs,
           HUSD_AutoAnyLock &lock,
           HUSD_PrimTraversalDemands demands,
           int nodeid,
           const HUSD_TimeCode &timecode)
       : XUSD_RelativeAutoCollection(collectionname, orderedargs, namedargs,
           lock, demands, nodeid, timecode, true)
    {
        myPaths.removeDescendants();
    }

    void matchPrimitives(XUSD_PathSet &matches) const override
    {
        UsdStageRefPtr stage = myLock.constData()->stage();
        auto predicate = HUSDgetUsdPrimPredicate(myDemands);
        UT_AutoInterrupt boss("Primitive pattern evaluation: %descendants");

        for (auto &&path : myPaths)
        {
            // This operation is relatively heavy, so check for interrupts
            // at each iteration.
            if (boss.wasInterrupted())
                break;

            UsdPrim root = stage->GetPrimAtPath(path);

            if (root)
            {
                XUSD_FindPrimPathsTaskData data;

                XUSDfindPrims(root, data, predicate);

                data.gatherPathsFromThreads(matches);
            }

            // XUSDfindPrims will find the root prim itself, but in strict
            // mode we don't want these prims included.
            if (myStrict)
                matches.erase(path);
        }
    }
};

////////////////////////////////////////////////////////////////////////////
// XUSD_NoDescendantsAutoCollection
////////////////////////////////////////////////////////////////////////////

class XUSD_NoDescendantsAutoCollection : public XUSD_RelativeAutoCollection
{
public:
    XUSD_NoDescendantsAutoCollection(
            const UT_StringHolder &collectionname,
            const UT_StringArray &orderedargs,
            const UT_StringMap<UT_StringHolder> &namedargs,
            HUSD_AutoAnyLock &lock,
            HUSD_PrimTraversalDemands demands,
            int nodeid,
            const HUSD_TimeCode &timecode)
        : XUSD_RelativeAutoCollection(collectionname, orderedargs, namedargs,
            lock, demands, nodeid, timecode, true)
    {
        myPaths.removeDescendants();
    }

    void matchPrimitives(XUSD_PathSet &matches) const override
    {
        matches = myPaths;
    }
};

////////////////////////////////////////////////////////////////////////////
// XUSD_ParentsAutoCollection
////////////////////////////////////////////////////////////////////////////

class XUSD_ParentsAutoCollection : public XUSD_RelativeAutoCollection
{
public:
    XUSD_ParentsAutoCollection(
           const UT_StringHolder &collectionname,
           const UT_StringArray &orderedargs,
           const UT_StringMap<UT_StringHolder> &namedargs,
           HUSD_AutoAnyLock &lock,
           HUSD_PrimTraversalDemands demands,
           int nodeid,
           const HUSD_TimeCode &timecode)
       : XUSD_RelativeAutoCollection(collectionname, orderedargs, namedargs,
           lock, demands, nodeid, timecode, false)
    { }

    void matchPrimitives(XUSD_PathSet &matches) const override
    {
        UsdStageRefPtr stage = myLock.constData()->stage();
        UT_AutoInterrupt boss("Primitive pattern evaluation: %parents");
        int count = 0;

        if (!myStrict)
            matches = myPaths;

        for (auto &&path : myPaths)
        {
            if ((count++ & 0x3FF) == 0 && boss.wasInterrupted())
                break;

            SdfPath parentpath = path.GetParentPath();

            if (!parentpath.IsAbsoluteRootPath() && !parentpath.IsEmpty())
                matches.insert(parentpath);
        }
    }
};

////////////////////////////////////////////////////////////////////////////
// XUSD_AncestorsAutoCollection
////////////////////////////////////////////////////////////////////////////

class XUSD_AncestorsAutoCollection : public XUSD_RelativeAutoCollection
{
public:
    XUSD_AncestorsAutoCollection(
           const UT_StringHolder &collectionname,
           const UT_StringArray &orderedargs,
           const UT_StringMap<UT_StringHolder> &namedargs,
           HUSD_AutoAnyLock &lock,
           HUSD_PrimTraversalDemands demands,
           int nodeid,
           const HUSD_TimeCode &timecode)
       : XUSD_RelativeAutoCollection(collectionname, orderedargs, namedargs,
           lock, demands, nodeid, timecode, false)
    {
        myPaths.removeAncestors();
    }

    void matchPrimitives(XUSD_PathSet &matches) const override
    {
        UsdStageRefPtr stage = myLock.constData()->stage();
        UT_AutoInterrupt boss("Primitive pattern evaluation: %ancestors");
        int count = 0;

        if (!myStrict)
            matches = myPaths;

        for (auto &&path : myPaths)
        {
            if ((count++ & 0x3FF) == 0 && boss.wasInterrupted())
                break;

            SdfPath parentpath = path.GetParentPath();

            while (!parentpath.IsAbsoluteRootPath() && !parentpath.IsEmpty())
            {
                matches.insert(parentpath);
                parentpath = parentpath.GetParentPath();
            }
        }
    }
};

////////////////////////////////////////////////////////////////////////////
// XUSD_NoAncestorsAutoCollection
////////////////////////////////////////////////////////////////////////////

class XUSD_NoAncestorsAutoCollection : public XUSD_RelativeAutoCollection
{
public:
    XUSD_NoAncestorsAutoCollection(
            const UT_StringHolder &collectionname,
            const UT_StringArray &orderedargs,
            const UT_StringMap<UT_StringHolder> &namedargs,
            HUSD_AutoAnyLock &lock,
            HUSD_PrimTraversalDemands demands,
            int nodeid,
            const HUSD_TimeCode &timecode)
        : XUSD_RelativeAutoCollection(collectionname, orderedargs, namedargs,
            lock, demands, nodeid, timecode, true)
    {
        myPaths.removeAncestors();
    }

    void matchPrimitives(XUSD_PathSet &matches) const override
    {
        matches = myPaths;
    }
};

////////////////////////////////////////////////////////////////////////////
// XUSD_CommonRootsAutoCollection
////////////////////////////////////////////////////////////////////////////

class XUSD_CommonRootsAutoCollection : public XUSD_RelativeAutoCollection
{
public:
    XUSD_CommonRootsAutoCollection(
           const UT_StringHolder &collectionname,
           const UT_StringArray &orderedargs,
           const UT_StringMap<UT_StringHolder> &namedargs,
           HUSD_AutoAnyLock &lock,
           HUSD_PrimTraversalDemands demands,
           int nodeid,
           const HUSD_TimeCode &timecode)
       : XUSD_RelativeAutoCollection(collectionname, orderedargs, namedargs,
           lock, demands, nodeid, timecode, false)
    {
        myPaths.removeDescendants();
    }

    void matchPrimitives(XUSD_PathSet &matches) const override
    {
        SdfPath rootpath;
        SdfPath commonprefix;
        UT_AutoInterrupt boss("Primitive pattern evaluation: %commonroots");
        int count = 0;

        for (auto &&path : myPaths)
        {
            if ((count++ & 0x3FF) == 0 && boss.wasInterrupted())
                break;

            if (!path.HasPrefix(rootpath))
            {
                // Either our first path, or a path with a new root prim.
                if (!commonprefix.IsEmpty())
                    matches.insert(commonprefix);
                rootpath = *path.GetPrefixes().begin();
                commonprefix = path;
            }
            else
            {
                // A path with the same root prim.
                commonprefix = commonprefix.GetCommonPrefix(path);
            }
        }
        if (!commonprefix.IsEmpty())
            matches.insert(commonprefix);
    }
};

////////////////////////////////////////////////////////////////////////////
// XUSD_MinimalSetAutoCollection
////////////////////////////////////////////////////////////////////////////

class XUSD_MinimalSetAutoCollection : public XUSD_AutoCollection
{
public:
    XUSD_MinimalSetAutoCollection(
           const UT_StringHolder &collectionname,
           const UT_StringArray &orderedargs,
           const UT_StringMap<UT_StringHolder> &namedargs,
           HUSD_AutoAnyLock &lock,
           HUSD_PrimTraversalDemands demands,
           int nodeid,
           const HUSD_TimeCode &timecode)
       : XUSD_AutoCollection(collectionname, orderedargs, namedargs,
           lock, demands, nodeid, timecode)
    {
        if (orderedargs.size() > 0)
            parsePattern(orderedargs[0],
                lock, demands, nodeid, timecode, false, myPaths,
                &myMayBeTimeVaryingSubPattern);
    }
    ~XUSD_MinimalSetAutoCollection() override
    { }

    bool randomAccess() const override
    { return false; }

    void matchPrimitives(XUSD_PathSet &matches) const override
    {
        UsdStageRefPtr stage = myLock.constData()->stage();

        matches = myPaths;
        HUSDgetMinimalPathsForInheritableProperty(false, stage, matches);
    }

protected:
    XUSD_PathSet                     myPaths;
};

////////////////////////////////////////////////////////////////////////////
// XUSD_HighestAutoCollection
////////////////////////////////////////////////////////////////////////////

class XUSD_HighestAutoCollection : public XUSD_AutoCollection
{
public:
    XUSD_HighestAutoCollection(
           const UT_StringHolder &collectionname,
           const UT_StringArray &orderedargs,
           const UT_StringMap<UT_StringHolder> &namedargs,
           HUSD_AutoAnyLock &lock,
           HUSD_PrimTraversalDemands demands,
           int nodeid,
           const HUSD_TimeCode &timecode)
       : XUSD_AutoCollection(collectionname, orderedargs, namedargs,
           lock, demands, nodeid, timecode)
    {
        if (orderedargs.size() > 0)
            parsePattern(orderedargs[0],
                lock, demands, nodeid, timecode, true, myPaths,
                &myMayBeTimeVaryingSubPattern);

        myPaths.removeDescendants();
    }
    ~XUSD_HighestAutoCollection() override
    { }

    bool randomAccess() const override
    { return false; }

    void matchPrimitives(XUSD_PathSet &matches) const override
    { matches = myPaths; }

protected:
    XUSD_PathSet                     myPaths;
};

////////////////////////////////////////////////////////////////////////////
// XUSD_LowestAutoCollection
////////////////////////////////////////////////////////////////////////////

class XUSD_LowestAutoCollection : public XUSD_AutoCollection
{
public:
    XUSD_LowestAutoCollection(
           const UT_StringHolder &collectionname,
           const UT_StringArray &orderedargs,
           const UT_StringMap<UT_StringHolder> &namedargs,
           HUSD_AutoAnyLock &lock,
           HUSD_PrimTraversalDemands demands,
           int nodeid,
           const HUSD_TimeCode &timecode)
       : XUSD_AutoCollection(collectionname, orderedargs, namedargs,
           lock, demands, nodeid, timecode)
    {
        if (orderedargs.size() > 0)
            parsePattern(orderedargs[0],
                lock, demands, nodeid, timecode, true, myPaths,
                &myMayBeTimeVaryingSubPattern);
    }
    ~XUSD_LowestAutoCollection() override
    { }

    bool randomAccess() const override
    { return false; }

    void matchPrimitives(XUSD_PathSet &matches) const override
    {
        UsdStageRefPtr stage = myLock.constData()->stage();

        matches = myPaths;
        HUSDgetMinimalMostNestedPathsForInheritableProperty(stage, matches);
    }

protected:
    XUSD_PathSet                     myPaths;
};

////////////////////////////////////////////////////////////////////////////
// XUSD_KeepAutoCollection
////////////////////////////////////////////////////////////////////////////

class XUSD_KeepAutoCollection : public XUSD_AutoCollection
{
public:
    XUSD_KeepAutoCollection(
           const UT_StringHolder &collectionname,
           const UT_StringArray &orderedargs,
           const UT_StringMap<UT_StringHolder> &namedargs,
           HUSD_AutoAnyLock &lock,
           HUSD_PrimTraversalDemands demands,
           int nodeid,
           const HUSD_TimeCode &timecode)
       : XUSD_AutoCollection(collectionname, orderedargs, namedargs,
           lock, demands, nodeid, timecode)
    {
        if (orderedargs.size() > 0)
        {
            parsePattern(orderedargs[0],
                lock, demands, nodeid, timecode, true, myPaths,
                &myMayBeTimeVaryingSubPattern);

            exint idx = 0;
            exint start = getValueFromArg("start", 0);
            exint end = getValueFromArg("end", myPaths.size());
            exint icount = getValueFromArg("count", 1);
            exint interval = getValueFromArg("interval", 2);
            bool keepoutsiderange = false;
            UT_AutoInterrupt boss("Primitive pattern evaluation: %keep");
            int count = 0;

            // Make sure the interval value is valid.
            interval = (interval > 0) ? interval : 1;
            auto keepoutsiderangeit = namedargs.find("keepoutsiderange");
            if (keepoutsiderangeit != namedargs.end())
                keepoutsiderange = parseBool(keepoutsiderangeit->second);

            // Check if set inversion is requested.
            bool invertselection = false;
            auto invertselectionit = namedargs.find("invert");
            if (invertselectionit != namedargs.end())
                invertselection = parseBool(invertselectionit->second);

            for (auto it = myPaths.begin(); it != myPaths.end();)
            {
                if ((count++ & 0x3FF) == 0 && boss.wasInterrupted())
                    break;

                if (!keepoutsiderange && (idx < start || idx >= end))
                {
                    if (invertselection)
                        ++it;
                    else
                        it = myPaths.erase(it);
                }
                else if (idx >= start && idx < end &&
                    (idx - start) % interval >= icount)
                {
                    if (invertselection)
                        ++it;
                    else
                        it = myPaths.erase(it);
                }
                else
                {
                    if (invertselection)
                        it = myPaths.erase(it);
                    else
                        ++it;
                }
                idx++;
                if (idx >= end && ((keepoutsiderange && !invertselection) || (!keepoutsiderange && invertselection)))
                    break;
            }
        }
    }
    ~XUSD_KeepAutoCollection() override
    { }

    bool randomAccess() const override
    { return false; }

    void matchPrimitives(XUSD_PathSet &matches) const override
    { matches = myPaths; }

protected:
    exint getValueFromArg(const char *arg, exint dflt)
    {
        auto argit = myNamedArgs.find(arg);
        exint value = 0;

        if (argit != myNamedArgs.end() && parseInt(argit->second, value))
        {
            if (value < 0)
                value = myPaths.size() + value;
            if (value < 0)
                value = 0;
            if (value > myPaths.size())
                value = myPaths.size();
        }
        else
            value = dflt;

        return value;
    }

    XUSD_PathSet                     myPaths;
};

////////////////////////////////////////////////////////////////////////////
// XUSD_KeepRandomAutoCollection
////////////////////////////////////////////////////////////////////////////

class XUSD_KeepRandomAutoCollection : public XUSD_AutoCollection
{
public:
    XUSD_KeepRandomAutoCollection(
           const UT_StringHolder &collectionname,
           const UT_StringArray &orderedargs,
           const UT_StringMap<UT_StringHolder> &namedargs,
           HUSD_AutoAnyLock &lock,
           HUSD_PrimTraversalDemands demands,
           int nodeid,
           const HUSD_TimeCode &timecode)
       : XUSD_AutoCollection(collectionname, orderedargs, namedargs,
           lock, demands, nodeid, timecode)
    {
        if (orderedargs.size() > 0)
        {
            parsePattern(orderedargs[0],
                lock, demands, nodeid, timecode, true, myPaths,
                &myMayBeTimeVaryingSubPattern);

            fpreal64 seed = 0.0;
            fpreal64 fraction = 0.5;

            auto seedit = namedargs.find("seed");
            auto fractionit = namedargs.find("fraction");

            if (seedit != namedargs.end())
                parseFloat(seedit->second, seed);
            if (fractionit != namedargs.end())
                parseFloat(fractionit->second, fraction);

            bool invertselection = false;
            auto invertselectionit = namedargs.find("invert");
            if (invertselectionit != namedargs.end())
                invertselection = parseBool(invertselectionit->second);

            exint removecount = (1.0 - fraction) * myPaths.size();
            if (invertselection)
                removecount = myPaths.size() - removecount;

            UT_AutoInterrupt boss("Primitive pattern evaluation: %keeprandom");
            int count = 0;

            if (removecount > 0)
            {
                std::map<SYS_HashType, SdfPath> randommap;

                for (auto &&path : myPaths)
                {
                    if ((count++ & 0x3FF) == 0 && boss.wasInterrupted())
                        break;

                    SYS_HashType hash = SYShash(HUSD_Path(path).pathStr());
                    SYShashCombine(hash, seed);
                    while (!randommap.emplace(hash, path).second)
                        hash++;
                }

                auto remove_paths = [&](auto begin, auto end)
                {
                    for (auto it = begin; it != end; ++it)
                    {
                        if ((count++ & 0x3FF) == 0 && boss.wasInterrupted())
                            break;

                        myPaths.erase(it->second);
                        if (--removecount == 0)
                            break;
                    }
                };

                // Set the direction for map traversal at runtime
                if (invertselection)
                    remove_paths(randommap.rbegin(), randommap.rend());
                else
                    remove_paths(randommap.begin(), randommap.end());
            }
        }
    }
    ~XUSD_KeepRandomAutoCollection() override
    { }

    bool randomAccess() const override
    { return false; }

    void matchPrimitives(XUSD_PathSet &matches) const override
    { matches = myPaths; }

protected:
    XUSD_PathSet                     myPaths;
};

////////////////////////////////////////////////////////////////////////////
// XUSD_VariantAutoCollection
////////////////////////////////////////////////////////////////////////////

class XUSD_VariantAutoCollection : public XUSD_RandomAccessAutoCollection
{
public:
    XUSD_VariantAutoCollection(
           const UT_StringHolder &collectionname,
           const UT_StringArray &orderedargs,
           const UT_StringMap<UT_StringHolder> &namedargs,
           HUSD_AutoAnyLock &lock,
           HUSD_PrimTraversalDemands demands,
           int nodeid,
           const HUSD_TimeCode &timecode)
       : XUSD_RandomAccessAutoCollection(collectionname, orderedargs, namedargs,
           lock, demands, nodeid, timecode)
    {
        for (auto &&it : namedargs)
            myVariantMap.emplace(
                it.first.toStdString(), it.second.toStdString());
    }
    ~XUSD_VariantAutoCollection() override
    { }

    bool matchPrimitive(const UsdPrim &prim,
        bool *prune_branch) const override
    {
        for (auto &&it : myVariantMap)
        {
            UsdVariantSets variantsets = prim.GetVariantSets();

            if (variantsets.HasVariantSet(it.first))
            {
                UsdVariantSet variantset = prim.GetVariantSet(it.first);

                if (variantset)
                {
                    UT_String selstr(variantset.GetVariantSelection());

                    if (selstr.multiMatch(it.second.c_str()))
                        return true;
                }
            }
        }

        return false;
    }

private:
    std::map<std::string, std::string>   myVariantMap;
};


////////////////////////////////////////////////////////////////////////////
// XUSD_RenderSettingsAutoCollection
////////////////////////////////////////////////////////////////////////////

class XUSD_RenderSettingsAutoCollection : public XUSD_AutoCollection
{
public:
    XUSD_RenderSettingsAutoCollection(
           const UT_StringHolder &collectionname,
           const UT_StringArray &orderedargs,
           const UT_StringMap<UT_StringHolder> &namedargs,
           HUSD_AutoAnyLock &lock,
           HUSD_PrimTraversalDemands demands,
           int nodeid,
           const HUSD_TimeCode &timecode)
       : XUSD_AutoCollection(collectionname, orderedargs, namedargs,
           lock, demands, nodeid, timecode)
    { }
    ~XUSD_RenderSettingsAutoCollection() override
    { }

    bool randomAccess() const override
    { return false; }

    void matchPrimitives(XUSD_PathSet &matches) const override
    {
        auto settingsPrim = getRenderSettingsPrim(myLock);
        if (settingsPrim)
            matches.insert(settingsPrim.GetPath());
    }
};

////////////////////////////////////////////////////////////////////////////
// XUSD_RenderCameraAutoCollection
////////////////////////////////////////////////////////////////////////////

class XUSD_RenderCameraAutoCollection : public XUSD_AutoCollection
{
public:
    XUSD_RenderCameraAutoCollection(
           const UT_StringHolder &collectionname,
           const UT_StringArray &orderedargs,
           const UT_StringMap<UT_StringHolder> &namedargs,
           HUSD_AutoAnyLock &lock,
           HUSD_PrimTraversalDemands demands,
           int nodeid,
           const HUSD_TimeCode &timecode)
       : XUSD_AutoCollection(collectionname, orderedargs, namedargs,
           lock, demands, nodeid, timecode)
    {
        if (orderedargs.size() > 0)
            parsePatternSingleResult(orderedargs[0],
                lock, demands, nodeid, timecode, false, mySettingsPath,
                &myMayBeTimeVaryingSubPattern);
    }
    ~XUSD_RenderCameraAutoCollection() override
    { }

    bool randomAccess() const override
    { return false; }

    void matchPrimitives(XUSD_PathSet &matches) const override
    {
        auto settingsPrim = getRenderSettingsPrim(myLock, mySettingsPath);

        if (settingsPrim)
        {
            UsdRelationship cameraRel = settingsPrim.GetCameraRel();
            if(cameraRel)
            {
                SdfPathVector   targets;
                cameraRel.GetForwardedTargets(&targets);
                if(!targets.empty())
                    matches.insert(targets.front());
            }
        }
    }

protected:
    SdfPath                          mySettingsPath;
};

////////////////////////////////////////////////////////////////////////////
// XUSD_RenderProductsAutoCollection
////////////////////////////////////////////////////////////////////////////

class XUSD_RenderProductsAutoCollection : public XUSD_AutoCollection
{
public:
    XUSD_RenderProductsAutoCollection(
           const UT_StringHolder &collectionname,
           const UT_StringArray &orderedargs,
           const UT_StringMap<UT_StringHolder> &namedargs,
           HUSD_AutoAnyLock &lock,
           HUSD_PrimTraversalDemands demands,
           int nodeid,
           const HUSD_TimeCode &timecode)
       : XUSD_AutoCollection(collectionname, orderedargs, namedargs,
           lock, demands, nodeid, timecode)
    {
        if (orderedargs.size() > 0)
            parsePatternSingleResult(orderedargs[0],
                lock, demands, nodeid, timecode, false, mySettingsPath,
                &myMayBeTimeVaryingSubPattern);
    }
    ~XUSD_RenderProductsAutoCollection() override
    { }

    bool randomAccess() const override
    { return false; }

    void matchPrimitives(XUSD_PathSet &matches) const override
    {
        auto settingsPrim = getRenderSettingsPrim(myLock, mySettingsPath);

        if(settingsPrim)
        {
            UsdRelationship productsRel = settingsPrim.GetProductsRel();
            if(productsRel)
            {
                SdfPathVector   targets;
                productsRel.GetForwardedTargets(&targets);
                for (auto &&target : targets)
                    matches.insert(target);
            }
        }
    }

protected:
    SdfPath                          mySettingsPath;
};

////////////////////////////////////////////////////////////////////////////
// XUSD_RenderVarsAutoCollection
////////////////////////////////////////////////////////////////////////////

class XUSD_RenderVarsAutoCollection : public XUSD_AutoCollection
{
public:
    XUSD_RenderVarsAutoCollection(
            const UT_StringHolder &collectionname,
            const UT_StringArray &orderedargs,
            const UT_StringMap<UT_StringHolder> &namedargs,
            HUSD_AutoAnyLock &lock,
            HUSD_PrimTraversalDemands demands,
            int nodeid,
            const HUSD_TimeCode &timecode)
        : XUSD_AutoCollection(collectionname, orderedargs, namedargs,
            lock, demands, nodeid, timecode)
    {
        if (orderedargs.size() > 0)
            parsePatternSingleResult(orderedargs[0],
                lock, demands, nodeid, timecode, false, mySettingsPath,
                &myMayBeTimeVaryingSubPattern);
    }
    ~XUSD_RenderVarsAutoCollection() override
    { }

    bool randomAccess() const override
    { return false; }

    void matchPrimitives(XUSD_PathSet &matches) const override
    {
        auto settingsPrim = getRenderSettingsPrim(myLock, mySettingsPath);

        if(settingsPrim)
        {
            UsdRelationship productsRel = settingsPrim.GetProductsRel();
            if(productsRel)
            {
                SdfPathVector   products;
                productsRel.GetForwardedTargets(&products);
                for (auto &&product : products)
                {
                    UsdStageRefPtr stage = myLock.constData()->stage();
                    auto productPrim = UsdRenderProduct::Get(stage, product);
                    if(productPrim)
                    {
                        UsdRelationship varsRel =
                            productPrim.GetOrderedVarsRel();
                        if(varsRel)
                        {
                            SdfPathVector   targets;
                            varsRel.GetForwardedTargets(&targets);
                            for (auto &&target : targets)
                                matches.insert(target);
                        }
                    }
                }
            }
        }
    }

protected:
    SdfPath                          mySettingsPath;
};

////////////////////////////////////////////////////////////////////////////
// XUSD_RenderVarsAutoCollection
////////////////////////////////////////////////////////////////////////////

class XUSD_PathExpressionAutoCollection : public XUSD_RandomAccessAutoCollection
{
public:
    XUSD_PathExpressionAutoCollection(
            const UT_StringHolder &collectionname,
            const UT_StringArray &orderedargs,
            const UT_StringMap<UT_StringHolder> &namedargs,
            HUSD_AutoAnyLock &lock,
            HUSD_PrimTraversalDemands demands,
            int nodeid,
            const HUSD_TimeCode &timecode)
       : XUSD_RandomAccessAutoCollection(collectionname, orderedargs, namedargs,
           lock, demands, nodeid, timecode)
    {
        if (orderedargs.size() > 0)
        {
            myPathExpression = SdfPathExpression(
                HUSDmakeValidPathExpression(orderedargs(0)).toStdString());
            if (!myPathExpression.GetParseError().empty())
            {
                UT_WorkBuffer msgbuf;
                msgbuf.append("Invalid path expression: ");
                msgbuf.append(myPathExpression.GetParseError());
                myTokenParsingError = msgbuf.buffer();
            }
            else
            {
                UsdStageRefPtr stage = myLock.constData()->stage();
                myPathExpressionEvaluator =
                    UTmakeUnique<Evaluator>(stage, myPathExpression);
            }
        }
    }
    ~XUSD_PathExpressionAutoCollection() override
    { }

    bool matchPrimitive(const UsdPrim &prim,
            bool *prune_branch) const override
    {
        if (myPathExpressionEvaluator)
            return myPathExpressionEvaluator->Match(prim);

        return false;
    }

protected:
    using Evaluator = UsdObjectCollectionExpressionEvaluator;
    SdfPathExpression                myPathExpression;
    UT_UniquePtr<Evaluator>          myPathExpressionEvaluator;
};

////////////////////////////////////////////////////////////////////////////
// XUSD_ForEachAutoCollection
////////////////////////////////////////////////////////////////////////////

class XUSD_ForEachAutoCollection : public XUSD_AutoCollection
{
public:
    XUSD_ForEachAutoCollection(
            const UT_StringHolder &collectionname,
            const UT_StringArray &orderedargs,
            const UT_StringMap<UT_StringHolder> &namedargs,
            HUSD_AutoAnyLock &lock,
            HUSD_PrimTraversalDemands demands,
            int nodeid,
            const HUSD_TimeCode &timecode)
        : XUSD_AutoCollection(collectionname, orderedargs, namedargs,
            lock, demands, nodeid, timecode)
    {
        XUSD_PathSet pathset;
        if (orderedargs.size() > 0)
            parsePattern(orderedargs[0],
                lock, demands, nodeid, timecode, false, pathset,
                &myMayBeTimeVaryingSubPattern);
        if (orderedargs.size() > 1)
            myPerPathPattern = orderedargs[1];
        myRangePaths.reserve(pathset.size());
        for (auto &&path : pathset)
            myRangePaths.push_back(path);
    }
    ~XUSD_ForEachAutoCollection() override
    { }

    bool randomAccess() const override
    { return false; }

    void matchPrimitives(XUSD_PathSet &matches) const override
    {
        UT_AutoInterrupt boss("Primitive pattern evaluation: %foreach");

        UTparallelForEachNumber(myRangePaths.size(),
            [&](const UT_BlockedRange<size_t> &r)
            {
                UT_WorkBuffer per_path_pattern;

                for (size_t i = r.begin(), n = r.end(); i < n; ++i)
                {
                    float progress = float(i) / float(myRangePaths.size());
                    if (boss.wasInterrupted(int(progress * 100.0)))
                        break;

                    SdfPath path = myRangePaths[i];
                    per_path_pattern = myPerPathPattern;
                    per_path_pattern.substitute(
                        "@path", path.GetAsString().c_str());
                    per_path_pattern.substitute(
                        "@name", path.GetName().c_str());
                    parsePattern(per_path_pattern,
                        myLock, myDemands, myNodeId, myHusdTimeCode, true,
                        myPerPathPatternMatches.get(),
                        &myMayBeTimeVarying.get());
                }
            });
        if (!boss.wasInterrupted())
            for (auto &&paths : myPerPathPatternMatches)
                matches.insert(paths.begin(), paths.end());
    }

    bool getMayBeTimeVarying() const override
    {
        if (XUSD_AutoCollection::getMayBeTimeVarying())
            return true;

        for (auto &&maybetimevarying : myMayBeTimeVarying)
            if (maybetimevarying)
                return true;
        return false;
    }

protected:
    SdfPathVector                                    myRangePaths;
    UT_StringHolder                                  myPerPathPattern;
    mutable UT_ThreadSpecificValue<XUSD_PathSet>     myPerPathPatternMatches;
    mutable UT_ThreadSpecificValue<bool>             myMayBeTimeVarying;
};

////////////////////////////////////////////////////////////////////////////
// XUSD_AutoCollection registration
////////////////////////////////////////////////////////////////////////////

void
XUSD_AutoCollection::registerPlugins()
{
    registerPlugin(new XUSD_SimpleAutoCollectionFactory
        <XUSD_KindAutoCollection>("kind"));
    registerPlugin(new XUSD_SimpleAutoCollectionFactory
        <XUSD_PrimTypeAutoCollection>("type"));
    registerPlugin(new XUSD_SimpleAutoCollectionFactory
        <XUSD_ShaderTypeAutoCollection>("shadertype"));
    registerPlugin(new XUSD_SimpleAutoCollectionFactory
        <XUSD_PurposeAutoCollection>("purpose"));
    registerPlugin(new XUSD_SimpleAutoCollectionFactory
        <XUSD_AuthoredPurposeAutoCollection>("authoredpurpose"));
    registerPlugin(new XUSD_SimpleAutoCollectionFactory
        <XUSD_PayloadAutoCollection>("payload"));
    registerPlugin(new XUSD_SimpleAutoCollectionFactory
        <XUSD_ReferenceAutoCollection>("reference"));
    registerPlugin(new XUSD_SimpleAutoCollectionFactory
        <XUSD_ReferencedByAutoCollection>("referencedby"));
    registerPlugin(new XUSD_SimpleAutoCollectionFactory
        <XUSD_InstanceAutoCollection>("instance"));
    registerPlugin(new XUSD_SimpleAutoCollectionFactory
        <XUSD_InstanceProxyAutoCollection>("instanceproxy"));
    registerPlugin(new XUSD_SimpleAutoCollectionFactory
        <XUSD_BoundAutoCollection>("bound"));
    registerPlugin(new XUSD_SimpleAutoCollectionFactory
        <XUSD_SizeAutoCollection>("size"));
    registerPlugin(new XUSD_SimpleAutoCollectionFactory
        <XUSD_GeoFromMatAutoCollection>("geofrommat"));
    registerPlugin(new XUSD_SimpleAutoCollectionFactory
        <XUSD_MatFromGeoAutoCollection>("matfromgeo"));
    registerPlugin(new XUSD_SimpleAutoCollectionFactory
        <XUSD_RelationshipAutoCollection>("rel"));
    registerPlugin(new XUSD_SimpleAutoCollectionFactory
        <XUSD_DistanceAutoCollection<false>>("closerthan"));
    registerPlugin(new XUSD_SimpleAutoCollectionFactory
        <XUSD_DistanceAutoCollection<true>>("fartherthan"));
    registerPlugin(new XUSD_SimpleAutoCollectionFactory
        <XUSD_VisibleAutoCollection>("visible"));
    registerPlugin(new XUSD_SimpleAutoCollectionFactory
        <XUSD_DefinedAutoCollection>("defined"));
    registerPlugin(new XUSD_SimpleAutoCollectionFactory
        <XUSD_ActiveAutoCollection>("active"));
    registerPlugin(new XUSD_SimpleAutoCollectionFactory
        <XUSD_AbstractAutoCollection>("abstract"));
    registerPlugin(new XUSD_SimpleAutoCollectionFactory
        <XUSD_SpecifierAutoCollection>("specifier"));
    registerPlugin(new XUSD_SimpleAutoCollectionFactory
        <XUSD_ChildrenAutoCollection>("children"));
    registerPlugin(new XUSD_SimpleAutoCollectionFactory
        <XUSD_DescendantsAutoCollection>("descendants"));
    registerPlugin(new XUSD_SimpleAutoCollectionFactory
        <XUSD_NoDescendantsAutoCollection>("nodescendants"));
    registerPlugin(new XUSD_SimpleAutoCollectionFactory
        <XUSD_ParentsAutoCollection>("parents"));
    registerPlugin(new XUSD_SimpleAutoCollectionFactory
        <XUSD_AncestorsAutoCollection>("ancestors"));
    registerPlugin(new XUSD_SimpleAutoCollectionFactory
        <XUSD_NoAncestorsAutoCollection>("noancestors"));
    registerPlugin(new XUSD_SimpleAutoCollectionFactory
        <XUSD_CommonRootsAutoCollection>("commonroots"));
    registerPlugin(new XUSD_SimpleAutoCollectionFactory
        <XUSD_MinimalSetAutoCollection>("minimalset"));
    registerPlugin(new XUSD_SimpleAutoCollectionFactory
        <XUSD_HighestAutoCollection>("highest"));
    registerPlugin(new XUSD_SimpleAutoCollectionFactory
        <XUSD_LowestAutoCollection>("lowest"));
    registerPlugin(new XUSD_SimpleAutoCollectionFactory
        <XUSD_VariantAutoCollection>("variant"));
    registerPlugin(new XUSD_SimpleAutoCollectionFactory
        <XUSD_KeepAutoCollection>("keep"));
    registerPlugin(new XUSD_SimpleAutoCollectionFactory
        <XUSD_KeepRandomAutoCollection>("keeprandom"));
    registerPlugin(new XUSD_SimpleAutoCollectionFactory
        <XUSD_RenderSettingsAutoCollection>("rendersettings"));
    registerPlugin(new XUSD_SimpleAutoCollectionFactory
        <XUSD_RenderCameraAutoCollection>("rendercamera"));
    registerPlugin(new XUSD_SimpleAutoCollectionFactory
        <XUSD_RenderProductsAutoCollection>("renderproducts"));
    registerPlugin(new XUSD_SimpleAutoCollectionFactory
        <XUSD_RenderVarsAutoCollection>("rendervars"));
    registerPlugin(new XUSD_SimpleAutoCollectionFactory
        <XUSD_ForEachAutoCollection>("foreach"));
    registerPlugin(new XUSD_SimpleAutoCollectionFactory
        <XUSD_PathExpressionAutoCollection>("pathexpr"));

    // Register the auto-collections in XUSD_PropertyAutoCollection.C.
    registerPropertyPlugins();
    if (!thePluginsInitialized)
    {
        UT_DSO dso;

        dso.run("newAutoCollection");
        thePluginsInitialized = true;
    }
}

PXR_NAMESPACE_CLOSE_SCOPE
