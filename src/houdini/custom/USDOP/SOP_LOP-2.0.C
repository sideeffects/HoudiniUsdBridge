/*
 * Copyright 2021 Side Effects Software Inc.
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

#include "SOP_LOP-2.0.h"
#include "SOP_LOP-2.0.proto.h"

#include <gusd/GU_USD.h>
#include <GA/GA_SplittableRange.h>
#include <GU/GU_Detail.h>
#include <GU/GU_PrimPacked.h>
#include <HUSD/HUSD_ErrorScope.h>
#include <HUSD/HUSD_FindPrims.h>
#include <HUSD/HUSD_LockedStageRegistry.h>
#include <HUSD/HUSD_Path.h>
#include <HUSD/HUSD_PathSet.h>
#include <HUSD/XUSD_Utils.h>
#include <LOP/LOP_Error.h>
#include <LOP/LOP_Node.h>
#include <OP/OP_Operator.h>
#include <PRM/PRM_TemplateBuilder.h>
#include <EXPR/EXPR_Lock.h>
#include <SOP/SOP_Error.h>
#include <UT/UT_ScopeExit.h>
#include <UT/UT_StringHolder.h>

#include <pxr/pxr.h>

using namespace UT::Literal;

PXR_NAMESPACE_OPEN_SCOPE

static const char* theDsFile = R"THEDSFILE(
{
    name	parameters
    parm {
        name    "loppath"
        cppname "LOPPath"
        label   "LOP Path"
        type    oppath
        default { "" }
        parmtag { "opfilter" "!!LOP!!" }
        parmtag { "oprelative" "." }
    }
    parm {
        name    "primpattern"
        cppname "PrimPattern"
        label   "Primitives"
        type    string
        default { "" }
        menutoggle {
            [ "import loputils" ]
            [ "node = hou.node(kwargs['node'].parm('loppath').eval())" ]
            [ "return loputils.createPrimPatternMenu(node, input_idx=None, expressions=('Sop/lopimport', 'Lop/selectionrule'))" ]
            language python
        }
        parmtag { "script_action" "import loputils\nkwargs['ctrl'] = True\nloputils.selectPrimsInParm(kwargs, True,\n    lopparmname='loppath', allowinstanceproxies=True)" }
        parmtag { "script_action_help" "Select primitives using the primitive picker dialog." }
        parmtag { "script_action_icon" "BUTTONS_reselect" }
        parmtag { "sidefx::usdpathtype" "primlist" }
    }
    parm {
        name    "purpose"
        cppname "Purpose"
        label   "Purpose"
        type    string
        default { "proxy" }
        menutoggle {
            "proxy"     "proxy"
            "render"    "render"
            "guide"     "guide"
        }
    }
    parm {
        name    "importtraversal"
        cppname "ImportTraversal"
        label   "Traversal"
        type    string
        default { "none" }
        menu {
            "std:components"    "Components"
            "std:boundables"    "Gprims"
            "std:groups"        "Groups"
            "none"              "No Traversal"
        }
    }
    parm {
        name    "striplayers"
        cppname "StripLayers"
        label   "Strip Layers Above Layer Breaks"
        type    toggle
        default { "0" }
    }
    parm {
        name    "timesample"
        cppname "TimeSample"
        label   "Time Sample"
        type    ordinal
        default { "animated" }
        menu {
            "static"    "Static"
            "animated"  "Animated"
        }
    }
    parm {
        name    "importframe"
        cppname "AnimatedImportFrame"
        label   "Import Frame"
        type    float
        default { "$FF" }
        range   { 0 10 }
        disablewhen "{ timesample == static }"
        hidewhen "{ timesample == static }"
    }
    parm {
        name    "staticimportframe"
        cppname "StaticImportFrame"
        label   "Static Import Frame"
        type    float
        default { "1" }
        range   { 0 10 }
        disablewhen "{ timesample == animated }"
        hidewhen "{ timesample == animated }"
    }
    parm {
        name    "sepparm"
        label   ""
        type    separator
        default { "" }
    }
    parm {
        name    "addpathattrib"
        cppname "AddPathAttrib"
        label   "Add Path Attribute"
        type    toggle
        nolabel
        joinnext
        default { "1" }
    }
    parm {
        name    "pathattrib"
        cppname "PathAttrib"
        label   "Path Attribute"
        type    string
        default { "path" }
        disablewhen "{ addpathattrib == 0 }"
    }
    parm {
        name    "addnameattrib"
        cppname "AddNameAttrib"
        label   "Add Name Attribute"
        type    toggle
        nolabel
        joinnext
        default { "1" }
    }
    parm {
        name    "nameattrib"
        cppname "NameAttrib"
        label   "Name Attribute"
        type    string
        default { "name" }
        disablewhen "{ addnameattrib == 0 }"
    }
    parm {
        name    "viewportlod"
        cppname "ViewportLod"
        label   "Display As"
        type    ordinal
        default { "full" }
        menu {
            "full"      "Full Geometry"
            "points"    "Point Cloud"
            "box"       "Bounding Box"
            "centroid"  "Centroid"
            "hidden"    "Hidden"
        }
    }
    parm {
        name    "pivot"
        cppname "PivotLocation"
        label   "Pivot Location"
        type    ordinal
        default { "centroid" }
        menu {
            "origin"    "Origin"
            "centroid"  "Centroid"
        }
    }
}
)THEDSFILE";

PRM_Template *
SOP_LOP2::buildTemplates()
{
    static PRM_TemplateBuilder templ("SOP_LOP-2.0.C"_sh, theDsFile);
    return templ.templates();
}

OP_Operator *
SOP_LOP2::createOperator()
{
    return new OP_Operator(
            "lopimport::2.0", "LOP Import", myConstructor, buildTemplates(), 0,
            0, nullptr);
}

SOP_LOP2::SOP_LOP2(OP_Network *net, const char *name, OP_Operator *op)
    : SOP_Node(net, name, op)
{
    mySopFlags.setManagesDataIDs(true);

    // Initialize to $FSTART
    setFloat("staticimportframe", 0, 0, CHgetManager()->getGlobalStartFrame());
}

void
SOP_LOP2::getDescriptiveParmName(UT_String &name) const
{
    name = "loppath";
}

void
SOP_LOP2::checkTimeDependencies(int do_parms, int do_inputs, int do_extras)
{
    // Don't inherit time dependency from the referenced LOP. The Import Frame
    // parameter controls the frame at which the LOP is cooked / the time
    // sample used, and therefore should determine whether the output is
    // time-dependent.
    SOP_Node::checkTimeDependencies(do_parms, 0, 0);
}

OP_ERROR
SOP_LOP2::cookMySop(OP_Context &context)
{
    return cookMyselfAsVerb(context);
}

class SOP_LOP2Cache : public SOP_NodeCache
{
    using PivotLocation = SOP_LOP_2_0Enums::PivotLocation;

public:
    void reset()
    {
        myLOPMicroNode.clearInputs();
        myLOPMicroNode.setDirty(true);
        myLOPPath.clear();
        myStripLayers = false;
        myDataHandle.reset(OP_INVALID_NODE_ID);
        myLockedStage.reset();

        myPrimPattern.clear();
        myPrimPatternIsTimeVarying = false;
        myTraversal.clear();
        myPurpose.clear();
        myPivotLocation = PivotLocation::ORIGIN;
        myPathAttrib.clear();
        myNameAttrib.clear();
        myTopologyId = GA_INVALID_DATAID;
        myLastUpdateTime = -std::numeric_limits<fpreal>::infinity();
    }

    bool requiresStageUpdate(
            const OP_Context &context,
            const SOP_LOP_2_0Parms &parms) const
    {
        if (myLOPMicroNode.requiresUpdate(context.getTime()))
            return true;

        if (myLOPMicroNode.requiresUpdateOptions(
                    context.getContextOptions(),
                    context.getContextOptionsStack()))
            return true;

        return myLOPPath != parms.getLOPPath()
               || myStripLayers != parms.getStripLayers();
    }

    void update(const OP_Context &context)
    {
        myLOPMicroNode.inheritContextOptionDepsFromExplicitInputs( {} );
        myLOPMicroNode.inheritTimeDependentFromExplicitInputs();
        myLOPMicroNode.update(context.getTime());
        myLOPMicroNode.updateOptions(context.getContextOptions(),
            context.getContextOptionsStack());
        myLastUpdateTime = context.getTime();
    }

    OP_ContextOptionsMicroNode myLOPMicroNode;
    UT_StringHolder myLOPPath;
    bool myStripLayers = false;
    HUSD_DataHandle myDataHandle;
    HUSD_LockedStagePtr myLockedStage;

    UT_StringHolder myPrimPattern;
    bool myPrimPatternIsTimeVarying = false;
    UT_StringHolder myTraversal;
    UT_StringHolder myPurpose;
    PivotLocation myPivotLocation = PivotLocation::ORIGIN;
    UT_StringHolder myPathAttrib;
    UT_StringHolder myNameAttrib;
    GA_DataId myTopologyId = GA_INVALID_DATAID;
    fpreal myLastUpdateTime = -std::numeric_limits<fpreal>::infinity();
};

class SOP_LOP2Verb : public SOP_NodeVerb
{
public:
    SOP_LOP2Verb() {}
    ~SOP_LOP2Verb() override {}

    SOP_NodeParms *allocParms() const override
    {
        return new SOP_LOP_2_0Parms();
    }

    SOP_NodeCache *allocCache() const override
    {
        return new SOP_LOP2Cache();
    }

    UT_StringHolder name() const override
    {
        return "lopimport::2.0"_sh;
    }

    CookMode cookMode(const SOP_NodeParms *parms) const override
    {
        return COOK_GENERIC;
    }

    void cook(const CookParms &cookparms) const override;
};

static SOP_NodeVerb::Register<SOP_LOP2Verb> theSOPLOP2Verb;

const SOP_NodeVerb *
SOP_LOP2::cookVerb() const
{
    return theSOPLOP2Verb.get();
}

GEO_ViewportLOD
sopGetViewportLOD(SOP_LOP_2_0Enums::ViewportLod parm_value)
{
    using SOP_LOP_2_0Enums::ViewportLod;

    GEO_ViewportLOD lod = GEO_VIEWPORT_INVALID_MODE;
    switch (parm_value)
    {
    case ViewportLod::FULL:
        lod = GEO_VIEWPORT_FULL;
        break;
    case ViewportLod::POINTS:
        lod = GEO_VIEWPORT_POINTS;
        break;
    case ViewportLod::BOX:
        lod = GEO_VIEWPORT_BOX;
        break;
    case ViewportLod::CENTROID:
        lod = GEO_VIEWPORT_CENTROID;
        break;
    case ViewportLod::HIDDEN:
        lod = GEO_VIEWPORT_HIDDEN;
        break;
    }

    return lod;
}

static void
sopAddPathAttribs(
        GU_Detail &detail,
        const UT_StringHolder &path_attr_name,
        const UT_StringHolder &name_attr_name)
{
    GA_Attribute *path_attrib = nullptr;
    if (path_attr_name.isstring())
    {
        path_attrib = detail.addStringTuple(
                GA_ATTRIB_PRIMITIVE, path_attr_name, 1);
    }

    GA_Attribute *name_attrib = nullptr;
    if (name_attr_name.isstring())
    {
        name_attrib = detail.addStringTuple(
                GA_ATTRIB_PRIMITIVE, name_attr_name, 1);
    }

    if (!path_attrib && !name_attrib)
        return;

    const GA_PrimitiveTypeId usd_id = GusdGU_PackedUSD::typeId();

    UTparallelFor(
            GA_SplittableRange(detail.getPrimitiveRange()),
            [&](const GA_SplittableRange &range)
    {
        GA_RWBatchHandleS path_handle(path_attrib);
        GA_RWBatchHandleS name_handle(name_attrib);

        for (GA_Offset primoff : range)
        {
            GEO_Primitive *prim = detail.getGEOPrimitive(primoff);

            UT_ASSERT(prim->getTypeId() == usd_id);
            if (prim->getTypeId() != usd_id)
                continue;

            GU_PrimPacked *packed
                    = UTverify_cast<GU_PrimPacked *>(prim);
            auto packed_usd =
#if !defined(LINUX)
                    UTverify_cast<GusdGU_PackedUSD *>(
                            packed->hardenImplementation());
#else
                    static_cast<GusdGU_PackedUSD *>(
                            packed->hardenImplementation());
#endif

            SdfPath sdfpath = packed_usd->primPath();

            if (path_handle.isValid())
                path_handle.set(primoff, sdfpath.GetString());

            if (name_handle.isValid())
                name_handle.set(primoff, sdfpath.GetName());
        }
    });
}

void
SOP_LOP2Verb::cook(const CookParms &cookparms) const
{
    HUSD_ErrorScope errorscope(cookparms.error());

    auto &&parms = cookparms.parms<SOP_LOP_2_0Parms>();
    auto &cache = *UTverify_cast<SOP_LOP2Cache *>(cookparms.cache());

    GU_Detail *gdp = cookparms.gdh().gdpNC();

    fpreal64 import_frame;
    switch (parms.getTimeSample())
    {
        case SOP_LOP_2_0Enums::TimeSample::STATIC:
            import_frame = parms.getStaticImportFrame();
            break;
        case SOP_LOP_2_0Enums::TimeSample::ANIMATED:
            import_frame = parms.getAnimatedImportFrame();
            break;
    }

    const GEO_ViewportLOD lod = sopGetViewportLOD(parms.getViewportLod());
    const HUSD_TimeCode timecode(import_frame, HUSD_TimeCode::FRAME);
    const UsdTimeCode usd_timecode = HUSDgetUsdTimeCode(timecode);

    UT_StringHolder path_attrib;
    if (parms.getAddPathAttrib())
        path_attrib = parms.getPathAttrib();

    UT_StringHolder name_attrib;
    if (parms.getAddNameAttrib())
        name_attrib = parms.getNameAttrib();

    // Rebuild the packed USD primitives if necessary.
    if (cache.requiresStageUpdate(cookparms.getContext(), parms)
        || (cache.myPrimPatternIsTimeVarying &&
            cache.myLastUpdateTime != cookparms.getCookTime())
        || cache.myPrimPattern != parms.getPrimPattern()
        || cache.myTraversal != parms.getImportTraversal()
        || cache.myPurpose != parms.getPurpose()
        || cache.myPivotLocation != parms.getPivotLocation()
        || cache.myPathAttrib != path_attrib
        || cache.myNameAttrib != name_attrib
        || cache.myTopologyId != gdp->getTopology().getDataId())
    {
        gdp->stashAll();
        UT_AT_SCOPE_EXIT(gdp->destroyStashed());

        LOP_Node *lop = cookparms.getCwd()->getLOPNode(parms.getLOPPath());
        if (!lop)
        {
            cache.reset();
            cookparms.sopAddError(SOP_MESSAGE, "Invalid LOP node path.");
            return;
        }

        // Keeping a HUSD_LockedStagePtr reference in the cache improves
        // performance for recooks that only change the primitive pattern,
        // traversal, etc. Otherwise, clearing the detail's packed prims might
        // remove the last reference to the locked stage, requiring it to be
        // rebuilt again.
        if (cache.requiresStageUpdate(cookparms.getContext(), parms))
        {
            cache.reset();

            cache.myLOPPath = parms.getLOPPath();
            cache.myStripLayers = parms.getStripLayers();

            OP_Context context(cookparms.getContext());
            context.setFrame(import_frame);

            // Even though getCookedDataHandle uses
            // ev_GlobalEvalLock().lockedExecute() internally, we must enclose the
            // following code in its own ev_GlobalEvalLock().lockedExecute() call
            // so that the getLockedStage call associates the correct data with
            // the cooked data handle.
            ev_GlobalEvalLock().lockedExecute([&]() {
                cache.myDataHandle = lop->getCookedDataHandle(context);
                cache.myLockedStage = HUSD_LockedStageRegistry::getInstance().
                    getLockedStage(lop, cache.myDataHandle, cache.myStripLayers,
                        context.getTime(), HUSD_IGNORE_STRIPPED_LAYERS);
            });
        }

        cookparms.addExplicitInput(lop->dataMicroNode());
        cache.myLOPMicroNode.addExplicitInput(lop->dataMicroNode());

        cache.myPrimPattern = parms.getPrimPattern();
        cache.myTraversal = parms.getImportTraversal();
        cache.myPurpose = parms.getPurpose();
        cache.myPivotLocation = parms.getPivotLocation();
        cache.myPathAttrib = path_attrib;
        cache.myNameAttrib = name_attrib;
        cache.myTopologyId = GA_INVALID_DATAID;

        GusdStageCacheReader stage_cache;
        UsdStageRefPtr stage = stage_cache.Find(
                cache.myLockedStage->getStageCacheIdentifier().toStdString());

        if (!stage)
        {
            cookparms.sopAddError(SOP_MESSAGE, "Failed to cook LOP node.");
            return;
        }

        const auto purpose = GusdPurposeSet(
                GusdPurposeSetFromMask(cache.myPurpose) | GUSD_PURPOSE_DEFAULT);

        HUSD_AutoReadLock readlock(cache.myDataHandle);
        auto demands = HUSD_PrimTraversalDemands(
                HUSD_TRAVERSAL_DEFAULT_DEMANDS
                | HUSD_TRAVERSAL_ALLOW_INSTANCE_PROXIES);
        HUSD_FindPrims findprims(readlock, demands);

        UT_WorkBuffer pattern;
        pattern.append(cache.myPrimPattern);
        // Filter by purpose to be consistent with the filtering done while
        // traversing / unpacking.
        pattern.append(" & %purpose:");
        GusdPurposeSetToStrings(purpose).join(",", pattern);

        if (!findprims.addPattern(pattern, lop->getUniqueId(), timecode))
        {
            cookparms.addError(
                    LOP_OPTYPE_NAME, LOP_COLLECTION_FAILED_TO_CALCULATE,
                    findprims.getLastError().c_str());
            return;
        }

        // Load the root prims from the locked stage (even though the prim
        // paths came from the LOP's data handle).
        UT_Array<UsdPrim> prims;
        prims.setCapacity(findprims.getExpandedPathSet().size());
        for (auto &&path : findprims.getExpandedPathSet())
        {
            UsdPrim prim = stage->GetPrimAtPath(path.sdfPath());
            if (prim)
                prims.append(prim);
        }
        
        cache.myPrimPatternIsTimeVarying = findprims.getIsTimeVarying();

        GusdDefaultArray<UT_StringHolder> stageids;
        stageids.SetConstant(cache.myLockedStage->getStageCacheIdentifier());

        GusdDefaultArray<UsdTimeCode> times;
        times.SetConstant(usd_timecode);

        GusdDefaultArray<UT_StringHolder> lods;
        lods.SetConstant(GEOviewportLOD(lod));

        GusdDefaultArray<GusdPurposeSet> purposes;
        purposes.SetConstant(purpose);

        GusdGU_PackedUSD::PivotLocation pivot;
        switch (cache.myPivotLocation)
        {
        case SOP_LOP_2_0Enums::PivotLocation::ORIGIN:
            pivot = GusdGU_PackedUSD::PivotLocation::Origin;
            break;
        case SOP_LOP_2_0Enums::PivotLocation::CENTROID:
            pivot = GusdGU_PackedUSD::PivotLocation::Centroid;
            break;
        }

        // Apply the traversal.
        const auto &traversals = GusdUSD_TraverseTable::GetInstance();
        const GusdUSD_Traverse *traversal = nullptr;
        if (cache.myTraversal != "none"_sh)
        {
            traversal = traversals.FindTraversal(cache.myTraversal);
            UT_ASSERT(traversal);
        }

        if (traversal)
        {
            UT_Array<GusdUSD_Traverse::PrimIndexPair> traversed_prims;

            // Note that we don't configure the traversal options, which are
            // only used for custom traversals.
            if (traversal->FindPrims(
                        prims, times, purposes, traversed_prims,
                        /*skip root*/ false, /* opts */ nullptr))
            {
                // Replace the list of prims.
                prims.setSize(traversed_prims.size());
                for (exint i = 0, n = traversed_prims.size(); i < n; ++i)
                    prims[i] = traversed_prims[i].first;
            }
            else
            {
                cookparms.sopAddWarning(SOP_MESSAGE, "Traversal failed.");
            }
        }

        // Create packed prims.
        GusdGU_USD::AppendPackedPrimsFromLopNode(
                *gdp, prims, stageids, times, lods, purposes, pivot);

        if (gdp->getNumPrimitives() > 0)
        {
            // If we have any packed USD prims, the locked stage should have a
            // reference in the packed USD registry. (Bug 117875)
            UT_ASSERT(cache.myLockedStage.use_count() > 1);
        }

        sopAddPathAttribs(*gdp, path_attrib, name_attrib);

        gdp->bumpAllDataIds();
        cache.myTopologyId = gdp->getTopology().getDataId();

        // Do this last so that we only clear the dirty flag on a successful
        // cook.
        cache.update(cookparms.getContext());
    }
    else
    {
        // Otherwise, if the frame / LOD changed just update the intrinsics for
        // the cached USD prims.
        const GA_PrimitiveTypeId usd_id = GusdGU_PackedUSD::typeId();

        for (GA_Offset primoff : gdp->getPrimitiveRange())
        {
            GEO_Primitive *prim = gdp->getGEOPrimitive(primoff);

            UT_ASSERT(prim->getTypeId() == usd_id);
            if (prim->getTypeId() != usd_id)
                continue;

            GU_PrimPacked *packed = UTverify_cast<GU_PrimPacked *>(prim);
            auto packed_usd =
#if !defined(LINUX)
                    UTverify_cast<GusdGU_PackedUSD *>(
                            packed->hardenImplementation());
#else
                    static_cast<GusdGU_PackedUSD *>(
                            packed->hardenImplementation());
#endif

            packed_usd->setFrame(packed, usd_timecode);
            packed->setViewportLOD(lod);
        }

        gdp->getPrimitiveList().bumpDataId();
    }
}

PXR_NAMESPACE_CLOSE_SCOPE
