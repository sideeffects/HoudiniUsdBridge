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

#include "SOP_UnpackUSD.h"
#include "SOP_CustomTraversal.h"

#include "gusd/GU_USD.h"
#include "gusd/GU_PackedUSD.h"
#include "gusd/USD_ThreadedTraverse.h"
#include "gusd/USD_Utils.h"
#include "gusd/UT_Assert.h"
#include "gusd/UT_StaticInit.h"

#include <pxr/base/tf/pathUtils.h>
#include <pxr/base/tf/fileUtils.h>
#include "pxr/base/plug/registry.h"
#include "pxr/usd/kind/registry.h"
#include "pxr/usd/usd/modelAPI.h"

#include <GU/GU_PrimPacked.h>
#include <GA/GA_AttributeFilter.h>
#include <GA/GA_AIFSharedStringTuple.h>
#include <GA/GA_ATIString.h>
#include <GA/GA_Handle.h>
#include <HUSD/HUSD_ErrorScope.h>
#include <OP/OP_AutoLockInputs.h>
#include <OP/OP_Director.h>
#include <OP/OP_OperatorTable.h>
#include <PI/PI_EditScriptedParms.h>
#include <PRM/PRM_Conditional.h>
#include <PRM/PRM_ChoiceList.h>
#include <UT/UT_WorkArgs.h>
#include <UT/UT_UniquePtr.h>
#include <PY/PY_Python.h>

PXR_NAMESPACE_OPEN_SCOPE

namespace {

int _TraversalChangedCB(void* data, int idx, fpreal64 t,
                        const PRM_Template* tmpl)
{
    auto& sop = *reinterpret_cast<SOP_UnpackUSD*>(data);
    sop.UpdateTraversalParms();
    return 0;
}

PRM_Template*   _CreateTemplates()
{
    static PRM_Name groupName("unpack_group", "Group");

    static PRM_Name traversalName("unpack_traversal", "Traversal");
    static PRM_Default traversalDef(0, _GPRIMTRAVERSE_NAME);

    static PRM_Name geomTypeName("unpack_geomtype", "Geometry Type");
    static PRM_Name geomTypeChoices[] =
    {
        PRM_Name("packedprims", "Packed Prims"),
        PRM_Name("polygons", "Polygons"),
        PRM_Name(0)
    };
    static PRM_ChoiceList geomTypeMenu(PRM_CHOICELIST_SINGLE, geomTypeChoices);


    static PRM_Name deloldName("unpack_delold", "Delete Old Prims");

    static PRM_Name timeName("unpack_time", "Time");
    static PRM_Default timeDef(0, "$RFSTART");
    static PRM_Conditional
            disableWhenNotPoints("{ unpack_class != \"point\" }");

    static PRM_Name attrsHeadingName("attrs_heading", "Attributes");

    static PRM_Name pathAttribName("unpack_pathattrib",
	    "Create Path Attribute");
    static PRM_Default pathAttribDef(0, "path");
    static PRM_Name nameAttribName("unpack_nameattrib",
	    "Create Name Attribute");
    static PRM_Default nameAttribDef(0, "name");

    static PRM_Name attrsName("transfer_attrs", "Transfer Attributes");
    static PRM_Name groupsName("transfer_groups", "Transfer Groups");

    static const char* transferAttrsScript
            = "kwargs['node'].generateInputAttribMenu(0)";
    static const char* transferGroupsScript
            = "kwargs['node'].generateInputGroupMenu(0, "
              "(hou.geometryType.Points, hou.geometryType.Primitives), "
              "include_name_attrib=False, include_selection=False, parm=kwargs['parm'])";
    static PRM_ChoiceList transferAttrsMenu(
            PRM_CHOICELIST_TOGGLE, transferAttrsScript, CH_PYTHON_SCRIPT);
    static PRM_ChoiceList transferGroupsMenu(
            PRM_CHOICELIST_TOGGLE, transferGroupsScript, CH_PYTHON_SCRIPT);

    static PRM_Name primvarsName("import_primvars", "Import Primvars");
    static PRM_Default primvarsDef(0, "*");

    static PRM_Name importInheritedName("importinheritedprimvars",
            "Import Inherited Primvars");

    static PRM_Name importAttrsName("importattributes", "Import Attributes");

    static PRM_Name nonTransformingPrimvarsName("nontransformingprimvars",
            "Non-Transforming Primvars");
    static PRM_Default nonTransformingPrimvarsDef(0, "rest");

    static PRM_Name translateSTtoUVName("translatesttouv",
            "Translate ST Primvar to UV");

    static PRM_Conditional
            disableWhenNotPolygons("{ unpack_geomtype != \"polygons\" }");

    static PRM_Template templates[] = {
        PRM_Template(PRM_STRING, 1, &groupName, 0, &SOP_Node::primGroupMenu,
		     0, 0, SOP_Node::getGroupSelectButton(GA_GROUP_PRIMITIVE)),
        PRM_Template(PRM_TOGGLE, 1, &deloldName, PRMoneDefaults),

        PRM_Template(PRM_FLT, 1, &timeName, &timeDef,
                     0, 0, 0, 0, 0, 0, &disableWhenNotPoints),
        PRM_Template(PRM_ORD, 1, &traversalName,
                     &traversalDef, &SOP_CustomTraversal::CreateTraversalMenu(),
                     0, // range
                     _TraversalChangedCB),
        PRM_Template(PRM_ORD, 1, &geomTypeName, 0, &geomTypeMenu),
        PRM_Template(PRM_ORD, 1, &PRMpackedPivotName, PRMoneDefaults,
                     &PRMpackedPivotMenu),

        PRM_Template(PRM_HEADING, 1, &attrsHeadingName, 0),
	PRM_Template(PRM_STRING, 1, &pathAttribName, &pathAttribDef),
	PRM_Template(PRM_STRING, 1, &nameAttribName, &nameAttribDef),
        PRM_Template(PRM_STRING, 1, &attrsName, 0, &transferAttrsMenu),
        PRM_Template(PRM_STRING, 1, &groupsName, 0, &transferGroupsMenu),

        PRM_Template(PRM_STRING, 1, &primvarsName, &primvarsDef,
                     0, 0, 0, 0, 0, 0, &disableWhenNotPolygons),
        PRM_Template(PRM_TOGGLE, 1, &importInheritedName, PRMzeroDefaults,
                     0, 0, 0, 0, 0, 0, &disableWhenNotPolygons),

        PRM_Template(PRM_STRING, 1, &importAttrsName,
                     0, 0, 0, 0, 0, 0, 0, &disableWhenNotPolygons),

        PRM_Template(PRM_STRING, 1, &nonTransformingPrimvarsName,
                     &nonTransformingPrimvarsDef, 0, 0, 0, 0, 0, 0,
                     &disableWhenNotPolygons),

        PRM_Template(PRM_TOGGLE, 1, &translateSTtoUVName, PRMoneDefaults,
                     0, 0, 0, 0, 0, 0, &disableWhenNotPolygons),
        PRM_Template()
    };

    return templates;
}


auto _mainTemplates(GusdUT_StaticVal(_CreateTemplates));


} /*namespace*/

void
SOP_UnpackUSD::Register(OP_OperatorTable* table)
{
    OP_Operator* op =
        new OP_Operator("unpackusd",
                        "Unpack USD",
                        Create,
                        *_mainTemplates,
                        /* min inputs */ (unsigned int)0,
                        /* max input  */ (unsigned int)1);
    op->setIconName("SOP_unpackusd");
    table->addOperator(op);
}


OP_Node*
SOP_UnpackUSD::Create(OP_Network* net, const char* name, OP_Operator* op)
{
    return new SOP_UnpackUSD(net, name, op);
}


SOP_UnpackUSD::SOP_UnpackUSD(
    OP_Network* net, const char* name, OP_Operator* op)
  : SOP_Node(net, name, op), _group(NULL)
{}


void
SOP_UnpackUSD::UpdateTraversalParms()
{
    if(getIsChangingSpareParms())
        return;

    UT_String traversal;
    evalString(traversal, "unpack_traversal", 0, 0);

    const auto& table = GusdUSD_TraverseTable::GetInstance();

    const PRM_Template* customTemplates = NULL;
    if(traversal != _NOTRAVERSE_NAME) {
        if(const auto* type = table.Find(traversal)) {
            customTemplates = type->GetTemplates();
        }
    }

    _templates.clear();
    const int nCustom = customTemplates ?
        PRM_Template::countTemplates(customTemplates) : 0;
    if(nCustom > 0) {
        /* Build a template list that puts the main
           templates in one tab, and the custom templates in another.*/
        static const int nMainTemplates =
            PRM_Template::countTemplates(*_mainTemplates);

        _tabs[0] = PRM_Default(nMainTemplates, "Main");
        _tabs[1] = PRM_Default(nCustom, "Advanced");

        static PRM_Name tabsName("unpack_tabs", "");
        
        _templates.append(PRM_Template(PRM_SWITCHER, 2, &tabsName, _tabs));
        
        SOP_CustomTraversal::ConcatTemplates(_templates, *_mainTemplates);
        SOP_CustomTraversal::ConcatTemplates(_templates, customTemplates);
    }
    _templates.append(PRM_Template());
                       

    /* Add the custom templates as spare parms.*/
    PI_EditScriptedParms parms(this, &_templates(0), /*spare*/ true,
                               /*skip-reserved*/ false, /*init links*/ false);
    UT_String errs;
    GusdUTverify_ptr(OPgetDirector())->changeNodeSpareParms(this, parms, errs);
    
    _AddTraversalParmDependencies();
}


void
SOP_UnpackUSD::_AddTraversalParmDependencies()
{
    PRM_ParmList* parms = GusdUTverify_ptr(getParmList());
    for(int i = 0; i < parms->getEntries(); ++i) {
        PRM_Parm* parm = GusdUTverify_ptr(parms->getParmPtr(i));
        if(parm->isSpareParm()) {
            for(int j = 0; j < parm->getVectorSize(); ++j)
                addExtraInput(parm->microNode(j));
        }
    }
}

template<typename T>
void RemapArray(const UT_Array<GusdUSD_Traverse::PrimIndexPair>& pairs,
                const UT_Array<T>& srcArray,
                const T& defaultValue,
                UT_Array<T>& dstArray)
{
    const exint size = pairs.size();
    dstArray.setSize(size);
    for (exint i = 0; i < size; ++i) {
        const exint index = pairs(i).second;
        dstArray(i) = (index >= 0 && index < size) ?
                      srcArray(index) : defaultValue;
    }
}

static void
sopSetPathAndName(
        const GA_ROHandleS& src_path_attr,
        GA_RWHandleS& path_attr,
        GA_RWHandleS& name_attr)
{
    if (!src_path_attr.isValid())
        return;

    if (path_attr.isValid())
        path_attr->replace(*src_path_attr.getAttribute());

    if (name_attr.isValid())
    {
        // Clone the path attribute and then edit the string table to keep only
        // the last component of the paths.
        name_attr->replace(*src_path_attr.getAttribute());

        UT_StringArray strings;
        UT_IntArray handles;
        name_attr->extractStrings(strings, handles);
        for (exint i = 0, n = strings.entries(); i < n; ++i)
        {
            name_attr->replaceString(
                    handles[i], UT_StringWrap(strings[i]).fileName());
        }
    }
}

OP_ERROR
SOP_UnpackUSD::_Cook(OP_Context& ctx)
{
    HUSD_ErrorScope errorscope(this);
    fpreal t = ctx.getTime();

    UT_String traversal;
    evalString(traversal, "unpack_traversal", 0, t);

    UT_String geomType;
    evalString(geomType, "unpack_geomtype", 0, t);
    bool unpackToPolygons = (geomType == "polygons");

    // If there is no traversal AND geometry type is not
    // polygons, then the output prims would be the same as the inputs,
    // so nothing left to do.
    if (traversal == _NOTRAVERSE_NAME && !unpackToPolygons) {
        return UT_ERROR_NONE;
    }

    // Construct a range and bind prims.
    GA_Range rng(gdp->getIndexMap(GA_ATTRIB_PRIMITIVE),
                 UTverify_cast<const GA_ElementGroup*>(_group));

    UT_Array<SdfPath> variants;
    GusdDefaultArray<GusdPurposeSet> purposes;
    GusdDefaultArray<UsdTimeCode> times;
    UT_Array<UsdPrim> rootPrims;
    {
        GusdStageCacheReader cache;
        if(!GusdGU_USD::BindPrims(cache, rootPrims, *gdp, rng,
                                  &variants, &purposes, &times)) {
            return error();
        }
    }

    if(!times.IsVarying())
        times.SetConstant(evalFloat("unpack_time", 0, t));

    // Run the traversal and store the resulting prims in traversedPrims.
    // If unpacking to polygons, the traversedPrims will need to contain
    // gprim level prims, which means a second traversal may be required.

    UT_Array<GusdUSD_Traverse::PrimIndexPair> traversedPrims;
    if (traversal != _NOTRAVERSE_NAME) {
        // For all traversals except gprim level, skipRoot must be true to
        // get the correct results. For gprim level traversals, skipRoot
        // should be false so the results won't be empty.
        bool skipRoot = (traversal != _GPRIMTRAVERSE_NAME);
        if (!_Traverse(traversal, t, rootPrims, times, purposes,
                       skipRoot, traversedPrims)) {
            return error();
        }
    } else if (unpackToPolygons) {
        // There is no traversal specified, but unpackToPolygons is true.
        // A second traversal will be done upon traversedPrims to make
        // sure it contains gprim level prims, but for now, just copy the
        // original packed prims from primHnd into traversedPrims.
        const exint size = rootPrims.size();
        traversedPrims.setSize(size);
        for (exint i = 0; i < size; ++i) {
            traversedPrims(i) = std::make_pair(rootPrims(i), i);
        }
    }

    // If unpacking to polygons AND the traversal was anything other than
    // gprim level, we need to traverse again to get down to the gprim
    // level prims.
    if (unpackToPolygons && traversal != _GPRIMTRAVERSE_NAME) {
        const exint size = traversedPrims.size();

        // Split up the traversedPrims pairs into 2 arrays.
        UT_Array<UsdPrim> prims(size, size);
        UT_Array<exint> indices(size, size);
        for (exint i = 0; i < size; ++i) {
            prims(i) = traversedPrims(i).first;
            indices(i) = traversedPrims(i).second;
        }

        GusdDefaultArray<GusdPurposeSet>    
            traversedPurposes(purposes.GetDefault());
        if(purposes.IsVarying()) {
            // Purposes must be remapped to align with traversedPrims.
            RemapArray(traversedPrims, purposes.GetArray(),
                       GUSD_PURPOSE_DEFAULT, traversedPurposes.GetArray());
        }

        GusdDefaultArray<UsdTimeCode> traversedTimes(times.GetDefault());
        if(times.IsVarying()) {
            // Times must be remapped to align with traversedPrims.
            RemapArray(traversedPrims, times.GetArray(),
                       times.GetDefault(), traversedTimes.GetArray());
        }

        // Clear out traversedPrims so it can be re-populated
        // during the new traversal.
        traversedPrims.clear();

        // skipRoot should be false so the result won't be empty.
        bool skipRoot = false;
        if (!_Traverse(_GPRIMTRAVERSE_NAME, t, prims,
                       traversedTimes, traversedPurposes,
                       skipRoot, traversedPrims)) {
            return error();
        }

        // Each index in the traversedPrims pairs needs
        // to be remapped back to a prim in primHnd.
        for (exint i = 0; i < traversedPrims.size(); ++i) {
            const exint primsIndex = traversedPrims(i).second;
            traversedPrims(i).second = indices(primsIndex);
        }
    }

    // Build an attribute filter using the transfer_attrs parameter.
    UT_String transferAttrs;
    evalString(transferAttrs, "transfer_attrs", 0, t);
    UT_String transferGroups;
    evalString(transferGroups, "transfer_groups", 0, t);

    GA_AttributeFilter filter(GA_AttributeFilter::selectOr(
            GA_AttributeFilter::selectAnd(
                    GA_AttributeFilter::selectByPattern(transferAttrs.c_str()),
                    GA_AttributeFilter::selectStandard(gdp->getP())),
            GA_AttributeFilter::selectAnd(
                    GA_AttributeFilter::selectByPattern(transferGroups.c_str()),
                    GA_AttributeFilter::selectGroup())));

    GusdDefaultArray<UsdTimeCode> traversedTimes(times.GetDefault());
    if(times.IsVarying()) {
        // Times must be remapped to align with traversedPrims.
        RemapArray(traversedPrims, times.GetArray(),
                   times.GetDefault(), traversedTimes.GetArray());
    }

    UT_String importPrimvars;
    evalString(importPrimvars, "import_primvars", 0, t);

    const bool translateSTtoUV = evalInt("translatesttouv", 0, t) != 0;
    const bool importInheritedPrimvars
            = evalInt("importinheritedprimvars", 0, t) != 0;

    UT_String nonTransformingPrimvarPattern;
    evalString(nonTransformingPrimvarPattern, "nontransformingprimvars", 0,
               t);

    UT_String importAttributes;
    evalString(importAttributes, "importattributes", 0, t);

    GusdGU_PackedUSD::PivotLocation pivotloc =
        GusdGU_PackedUSD::PivotLocation::Origin;
    if (evalInt(PRMpackedPivotName.getTokenRef(), 0, t) == 1)
        pivotloc = GusdGU_PackedUSD::PivotLocation::Centroid;

    GusdGU_USD::AppendExpandedPackedPrimsFromLopNode(
            *gdp, *gdp, rng, traversedPrims, traversedTimes, filter,
            unpackToPolygons, importPrimvars, importInheritedPrimvars,
            importAttributes, translateSTtoUV, nonTransformingPrimvarPattern,
            pivotloc);

    if(evalInt("unpack_delold", 0, t)) {

        // Only delete prims or points that were successfully
        // binded to prims in primHnd.
        GA_OffsetList delOffsets;
        delOffsets.reserve(rootPrims.size());
        exint i = 0;
        for (GA_Iterator it(rng); !it.atEnd(); ++it, ++i) {
            if (rootPrims(i).IsValid()) {
                delOffsets.append(*it);
            }
        }
        GA_Range delRng(gdp->getIndexMap(GA_ATTRIB_PRIMITIVE), delOffsets);

        gdp->destroyPrimitives(delRng, /*and points*/ true);
    }

    // Gather information about the name and path attributes we have been
    // asked to create on the unpacked geometry, indicating the source USD
    // primitive name and/or path.
    UT_String pathAttribName;
    evalString(pathAttribName, "unpack_pathattrib", 0, t);
    UT_String nameAttribName;
    evalString(nameAttribName, "unpack_nameattrib", 0, t);

    GA_RWHandleS pathAttrib;
    if (pathAttribName.isstring())
	pathAttrib = gdp->addStringTuple(
	    GA_ATTRIB_PRIMITIVE, pathAttribName, 1);

    GA_RWHandleS nameAttrib;
    if (nameAttribName.isstring())
	nameAttrib = gdp->addStringTuple(
	    GA_ATTRIB_PRIMITIVE, nameAttribName, 1);

    // Just like in the LOP Import SOP, do an optional post-pass to add
    // name and path primitive attributes to any USD primitives or polygons
    // unpacked from USD packed primitives.
    if (pathAttrib.isValid() || nameAttrib.isValid())
    {
        // The GUSD_PRIMPATH_ATTR is created while unpacking USD packed
        // prims to polygons. If this attribute exists, copy it to the
        // requested path attribute and/or trim off the last component for
        // the name attribute.
        GA_ROHandleS primUsdPathAttrib = gdp->findStringTuple(
                GA_ATTRIB_PRIMITIVE, GUSD_PRIMPATH_ATTR, 1);
        sopSetPathAndName(primUsdPathAttrib, pathAttrib, nameAttrib);

        if (gdp->containsPrimitiveType(GusdGU_PackedUSD::typeId()))
        {
            for (GA_Offset primoff : gdp->getPrimitiveRange())
            {
                const GA_Primitive* prim = gdp->getPrimitive(primoff);

                if (prim->getTypeId() != GusdGU_PackedUSD::typeId())
                    continue;

                const GU_PrimPacked* packed
                        = UTverify_cast<const GU_PrimPacked*>(prim);
                const GU_PackedImpl* packedImpl = packed->sharedImplementation();

                // NOTE: GCC 6.3 doesn't allow dynamic_cast on non-exported classes,
                //       and GusdGU_PackedUSD isn't exported for some reason,
                //       so to avoid Linux debug builds failing, we static_cast
                //       instead of UTverify_cast.
                const GusdGU_PackedUSD* packedUsd =
#if !defined(LINUX)
                        UTverify_cast<const GusdGU_PackedUSD*>(packedImpl);
#else
                        static_cast<const GusdGU_PackedUSD*>(packedImpl);
#endif
                SdfPath sdfpath = packedUsd->primPath();
                if (pathAttrib.isValid())
                    pathAttrib.set(primoff, sdfpath.GetAsString().c_str());
                if (nameAttrib.isValid())
                    nameAttrib.set(primoff, sdfpath.GetName());
            }
        }
    }

    // We might also need to set up a point name & path attrib when importing
    // points prims.
    GA_ROHandleS pointUsdPathAttrib = gdp->findStringTuple(
            GA_ATTRIB_POINT, GUSD_PRIMPATH_ATTR, 1);
    if (pointUsdPathAttrib.isValid())
    {
        GA_RWHandleS pointPathAttrib;
        if (pathAttribName.isstring())
        {
            pointPathAttrib = gdp->addStringTuple(
                    GA_ATTRIB_POINT, pathAttribName, 1);
        }

        GA_RWHandleS pointNameAttrib;
        if (nameAttribName.isstring())
        {
            pointNameAttrib = gdp->addStringTuple(
                    GA_ATTRIB_POINT, nameAttribName, 1);
        }

        sopSetPathAndName(pointUsdPathAttrib, pointPathAttrib, pointNameAttrib);
    }

    return error();
}

bool
SOP_UnpackUSD::_Traverse(const UT_String& traversal,
                             const fpreal time,
                             const UT_Array<UsdPrim>& prims,
                             const GusdDefaultArray<UsdTimeCode>& times,
                             const GusdDefaultArray<GusdPurposeSet>& purposes,
                             bool skipRoot,
                             UT_Array<GusdUSD_Traverse::PrimIndexPair>& traversed)
{
    const auto& table = GusdUSD_TraverseTable::GetInstance();
    
    const GusdUSD_Traverse* traverse = table.FindTraversal(traversal);
    if (!traverse) {
        GUSD_ERR().Msg("Failed locating traversal '%s'", traversal.c_str());
        return false;
    }

    UT_UniquePtr<GusdUSD_Traverse::Opts> opts(traverse->CreateOpts());
    if (opts) {
        if (!opts->Configure(*this, time)) {
            return false;
        }
    }

    if (!traverse->FindPrims(prims, times, purposes, traversed,
                             skipRoot, opts.get())) {
        return false;
    }

    return true;
}
                               

OP_ERROR
SOP_UnpackUSD::cookInputGroups(OP_Context& ctx, int alone)
{
    if(!getInput(0))
        return UT_ERROR_NONE;

    int groupIdx = getParmList()->getParmIndex("unpack_group");
    
    return cookInputAllGroups(ctx, _group, alone,
                              /* do selection*/ true,
                              groupIdx, -1, GA_GROUP_PRIMITIVE);
}


OP_ERROR
SOP_UnpackUSD::cookMySop(OP_Context& ctx)
{
    OP_AutoLockInputs lock(this);
    if(lock.lock(ctx) >= UT_ERROR_ABORT)
        return error();

    // Local var support.
    setCurGdh(0, myGdpHandle);
    setupLocalVars();

    if(getInput(0))
        duplicateSource(0, ctx);
    else
        gdp->clearAndDestroy();

    /* Extra inputs have to be re-added on each cook.*/
    _AddTraversalParmDependencies();

    if(cookInputGroups(ctx, 0) < UT_ERROR_ABORT)
        _Cook(ctx);
        
    resetLocalVarRefs();

    return error();
}


void
SOP_UnpackUSD::finishedLoadingNetwork(bool isChildCall)
{
    SOP_Node::finishedLoadingNetwork(isChildCall);
    
    if(isChildCall) {
        /* Update our traversal parms.
           Needs to happen post-loading since loading could
           have changed the traversal mode.*/
        UpdateTraversalParms();
    }
}

void
SOP_UnpackUSD::syncNodeVersion(const char *old_version,
                               const char *cur_version,
                               bool *node_deleted)
{
    // Before 18.0.402 / 18.5.141 the pivot was placed at the origin.
    if (UT_String::compareVersionString(old_version, "18.0.402") < 0 ||
        (UT_String::compareVersionString(old_version, "18.5.0") >= 0 &&
         UT_String::compareVersionString(old_version, "18.5.141") < 0))
    {
        setInt(PRMpackedPivotName.getTokenRef(), 0, 0.0, 0);
    }
    SOP_Node::syncNodeVersion(old_version, cur_version, node_deleted);
}

PXR_NAMESPACE_CLOSE_SCOPE

