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

#include "SOP_UnpackUSD-2.0.h"
#include "SOP_UnpackUSD-2.0.proto.h"

#include <GA/GA_AttributeFilter.h>
#include <gusd/GU_USD.h>
#include <gusd/GU_PackedUSD.h>
#include <GU/GU_PrimPacked.h>
#include <HUSD/HUSD_ErrorScope.h>
#include <PRM/PRM_TemplateBuilder.h>
#include <SOP/SOP_Error.h>
#include <SOP/SOP_Node.h>
#include <UT/UT_StringHolder.h>

#include <pxr/pxr.h>

using namespace UT::Literal;

PXR_NAMESPACE_OPEN_SCOPE

static const char* theDsFile = R"THEDSFILE(
{
    name	parameters
    parm {
        name    "group"
        label   "Group"
        type    string
        default { "" }
        parmtag { "script_action" "import soputils\nkwargs['geometrytype'] = (hou.geometryType.Primitives,)\nkwargs['inputindex'] = 0\nsoputils.selectGroupParm(kwargs)" }
        parmtag { "script_action_help" "Select geometry from an available viewport.\nShift-click to turn on Select Groups." }
        parmtag { "script_action_icon" "BUTTONS_reselect" }
    }
    parm {
        name    "deleteorig"
        cppname "DeleteOriginalPrims"
        label   "Delete Original Prims"
        type    toggle
        default { "1" }
    }
    parm {
        name    "unpacktraversal"
        cppname "UnpackTraversal"
        label   "Traversal"
        type    string
        default { "std:boundables" }
        menu {
            "std:components"    "Components"
            "std:boundables"    "Gprims"
            "std:groups"        "Groups"
            "none"              "No Traversal"
        }
    }

    groupsimple {
        name    "group_unpack"
        label   "Unpack"
        grouptag { "group_type" "simple" }

        parm {
            name    "output"
            label   "Output"
            type    ordinal
            default { "packedprims" }
            menu {
                "packedprims"   "Packed Prims"
                "polygons"      "Polygons"
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
            disablewhen "{ addnameattrib == 0 } { addpathattrib == 0 }"
        }
        parm {
            name    "addfilepathattrib"
            cppname "AddFilePathAttrib"
            label   "Add File Path Attribute"
            type    toggle
            nolabel
            joinnext
            default { "1" }
        }
        parm {
            name    "filepathattrib"
            cppname "FilePathAttrib"
            label   "File Path Attribute"
            type    string
            default { "usdpath" }
            disablewhen "{ addfilepathattrib == 0 }"
        }
        parm {
            name    "transferattributes"
            cppname "TransferAttributes"
            label	"Transfer Attributes"
            type	string
            default	{ "" }
            menutoggle {
                [ "kwargs['node'].generateInputAttribMenu(0)" ]
                language python
            }
        }
        parm {
            name    "transfergroups"
            cppname "TransferGroups"
            label   "Transfer Groups"
            type    string
            default { "" }
            menutoggle {
                [ "kwargs['node'].generateInputGroupMenu(0, (hou.geometryType.Points, hou.geometryType.Primitives), include_name_attrib=False, include_selection=False, parm=kwargs['parm'])" ]
                language python
            }
        }
    }
    groupsimple {
        name    "group_import"
        label   "Import Data"
        grouptag { "group_type" "simple" }
        disablewhen "{ output != polygons }"

        parm {
            name    "importprimvars"
            cppname "ImportPrimvars"
            label   "Import Primvars"
            type    string
            default { "*" }
        }
        parm {
            name    "importinheritedprimvars"
            cppname "ImportInheritedPrimvars"
            label   "Import Inherited Primvars"
            type    toggle
            default { "0" }
        }
        parm {
            name    "importattributes"
            cppname "ImportAttributes"
            label   "Import Attributes"
            type    string
            default { "" }
        }
        parm {
            name    "nontransformingprimvars"
            cppname "NonTransformingPrimvars"
            label   "Non-Transforming Primvars"
            type    string
            default { "rest" }
        }
        parm {
            name    "translatesttouv"
            cppname "TranslateST"
            label   "Translate ST Primvar to UV"
            type    toggle
            default { "1" }
        }
    }
}
)THEDSFILE";

PRM_Template *
SOP_UnpackUSD2::buildTemplates()
{
    static PRM_TemplateBuilder templ("SOP_UnpackUSD-2.0.C"_sh, theDsFile);

    if (templ.justBuilt())
	templ.setChoiceListPtr("group", &SOP_Node::primGroupMenu);

    return templ.templates();
}

OP_Operator *
SOP_UnpackUSD2::createOperator()
{
    return new OP_Operator(
            "unpackusd::2.0", "Unpack USD", myConstructor, buildTemplates(), 1,
            1, nullptr);
}

SOP_UnpackUSD2::SOP_UnpackUSD2(
        OP_Network *net,
        const char *name,
        OP_Operator *op)
    : SOP_Node(net, name, op)
{
    mySopFlags.setManagesDataIDs(true);
}

OP_ERROR
SOP_UnpackUSD2::cookMySop(OP_Context &context)
{
    return cookMyselfAsVerb(context);
}

class SOP_UnpackUSD2Verb : public SOP_NodeVerb
{
public:
    SOP_UnpackUSD2Verb() {}
    ~SOP_UnpackUSD2Verb() override {}

    SOP_NodeParms *allocParms() const override
    {
        return new SOP_UnpackUSD_2_0Parms();
    }

    UT_StringHolder name() const override
    {
        return "unpackusd::2.0"_sh;
    }

    CookMode cookMode(const SOP_NodeParms *parms) const override
    {
        return COOK_GENERATOR;
    }

    void cook(const CookParms &cookparms) const override;
};

static SOP_NodeVerb::Register<SOP_UnpackUSD2Verb> theSOPUnpackUSD2Verb;

const SOP_NodeVerb *
SOP_UnpackUSD2::cookVerb() const
{
    return theSOPUnpackUSD2Verb.get();
}

template <typename T>
static void
sopRemapArray(
        const UT_Array<GusdUSD_Traverse::PrimIndexPair> &pairs,
        const UT_Array<T> &src_array,
        const T &default_value,
        UT_Array<T> &dst_array)
{
    const exint size = pairs.size();
    dst_array.setSize(size);
    for (exint i = 0; i < size; ++i)
    {
        const exint index = pairs(i).second;
        dst_array[i] = (index >= 0 && index < size) ? src_array[index] :
                                                      default_value;
    }
}

static void
sopSetNameAttrib(
        const GA_ROHandleS &src_path_attr,
        GA_RWHandleS &name_attr)
{
    if (!src_path_attr.isValid())
        return;

    if (name_attr.isValid())
    {
        // Clone the path attribute and then edit the string table to keep only
        // the last component of the paths.
        name_attr->replace(*src_path_attr.getAttribute());

        UT_StringArray strings;
        UT_IntArray handles;
        name_attr->extractStrings(strings, handles);
        for (auto &string : strings)
            string = UT_StringWrap(string.data()).fileName();
        name_attr->replaceStrings(handles, strings);
    }
}

static void
sopUnpackUSDPrims(
        GU_Detail &detail,
        const GU_Detail &src_detail,
        const GA_Range &src_range,
        const SOP_NodeVerb::CookParms &cookparms,
        const SOP_UnpackUSD_2_0Parms &parms)
{
    const bool unpack_to_polys
            = (parms.getOutput() == SOP_UnpackUSD_2_0Enums::Output::POLYGONS);

    // Find the USD prims from our packed prims.
    GusdStageCacheReader stage_cache;
    UT_Array<UsdPrim> root_prims;
    GusdDefaultArray<GusdPurposeSet> purposes;
    GusdDefaultArray<UsdTimeCode> times;
    if (!GusdGU_USD::BindPrims(
                stage_cache, root_prims, src_detail, src_range,
                /* variants */ nullptr, &purposes, &times))
    {
        cookparms.sopAddError(SOP_MESSAGE, "Failed to bind USD prims.");
        return;
    }

    // Apply the traversal.
    const auto &traversals = GusdUSD_TraverseTable::GetInstance();
    static const UT_StringLit theGprimTraversalName("std:boundables");
    const GusdUSD_Traverse *traversal = nullptr;
    if (parms.getUnpackTraversal() != "none"_sh)
    {
        traversal = traversals.FindTraversal(parms.getUnpackTraversal());
        UT_ASSERT(traversal);
    }

    UT_Array<GusdUSD_Traverse::PrimIndexPair> traversed_prims;
    if (traversal)
    {
        // For all traversals except gprim level, skip_root must be true to
        // get the correct results. For gprim level traversals, skip_root
        // should be false so the results won't be empty.
        const bool skip_root
                = (parms.getUnpackTraversal() != theGprimTraversalName.asRef());

        // Note that we don't configure the traversal options, which are
        // only used for custom traversals.
        if (!traversal->FindPrims(
                    root_prims, times, purposes, traversed_prims, skip_root,
                    /* opts */ nullptr))
        {
            cookparms.sopAddError(SOP_MESSAGE, "Traversal failed.");
            return;
        }
    }
    else if (unpack_to_polys)
    {
        // If there is no traversal specified, but unpack to polygons is
        // enabled, a second traversal will be done upon traversedPrims to make
        // sure it contains gprim level prims. Just copy the original packed
        // prims into traversedPrims.
        const exint n = root_prims.size();
        traversed_prims.setSize(n);
        for (exint i = 0; i < n; ++i)
            traversed_prims[i] = std::make_pair(root_prims[i], i);
    }

    // If unpacking to polygons AND the traversal was anything other than
    // gprim level, we need to traverse again to get down to the gprim
    // level prims.
    if (unpack_to_polys
        && parms.getUnpackTraversal() != theGprimTraversalName.asRef())
    {
        const exint norig = traversed_prims.size();

        // Split up the traversed prims pairs into 2 arrays.
        UT_Array<UsdPrim> prims(norig, norig);
        UT_Array<exint> indices(norig, norig);
        for (exint i = 0; i < norig; ++i)
        {
            prims[i] = traversed_prims[i].first;
            indices[i] = traversed_prims[i].second;
        }

        GusdDefaultArray<GusdPurposeSet> traversed_purposes(
                purposes.GetDefault());
        if (purposes.IsVarying())
        {
            // Purposes must be remapped to align with traversedPrims.
            sopRemapArray(
                    traversed_prims, purposes.GetArray(), GUSD_PURPOSE_DEFAULT,
                    traversed_purposes.GetArray());
        }

        GusdDefaultArray<UsdTimeCode> traversed_times(times.GetDefault());
        if (times.IsVarying())
        {
            // Times must be remapped to align with traversedPrims.
            sopRemapArray(
                    traversed_prims, times.GetArray(), times.GetDefault(),
                    traversed_times.GetArray());
        }

        // Clear out traversedPrims so it can be re-populated
        // during the new traversal.
        traversed_prims.clear();

        traversal = traversals.FindTraversal(theGprimTraversalName.asRef());
        UT_ASSERT(traversal);

        if (!traversal->FindPrims(
                    prims, traversed_times, traversed_purposes, traversed_prims,
                    /* skip_root */ false,
                    /* opts */ nullptr))
        {
            cookparms.sopAddError(SOP_MESSAGE, "Traversal failed.");
            return;
        }

        // Each index in the traversed_prims pairs needs to be remapped back to
        // a prim in the original range.
        const exint nnew = traversed_prims.size();
        for (exint i = 0; i < nnew; ++i)
        {
            const exint prim_index = traversed_prims[i].second;
            traversed_prims[i].second = indices[prim_index];
        }
    }

    // Build an attribute filter from the parameters.
    GA_AttributeFilter filter(GA_AttributeFilter::selectOr(
            GA_AttributeFilter::selectAnd(
                    GA_AttributeFilter::selectByPattern(
                            parms.getTransferAttributes()),
                    GA_AttributeFilter::selectStandard(src_detail.getP())),
            GA_AttributeFilter::selectAnd(
                    GA_AttributeFilter::selectByPattern(
                            parms.getTransferGroups()),
                    GA_AttributeFilter::selectGroup())));

    GusdDefaultArray<UsdTimeCode> traversed_times(times.GetDefault());
    if (times.IsVarying())
    {
        // Times must be remapped to align with traversedPrims.
        sopRemapArray(
                traversed_prims, times.GetArray(), times.GetDefault(),
                traversed_times.GetArray());
    }

    GusdGU_PackedUSD::PivotLocation pivot;
    switch (parms.getPivotLocation())
    {
    case SOP_UnpackUSD_2_0Enums::PivotLocation::ORIGIN:
        pivot = GusdGU_PackedUSD::PivotLocation::Origin;
        break;
    case SOP_UnpackUSD_2_0Enums::PivotLocation::CENTROID:
        pivot = GusdGU_PackedUSD::PivotLocation::Centroid;
        break;
    }

    UT_StringHolder file_path_attrib_name;
    if (parms.getAddFilePathAttrib())
        file_path_attrib_name = parms.getFilePathAttrib();

    UT_StringHolder path_attrib_name;
    if (parms.getAddPathAttrib())
        path_attrib_name = parms.getPathAttrib();

    GusdGU_USD::AppendExpandedPackedPrimsFromLopNode(
            detail, src_detail, src_range, traversed_prims, traversed_times,
            filter, unpack_to_polys, parms.getImportPrimvars().c_str(),
            parms.getImportInheritedPrimvars(),
            parms.getImportAttributes().c_str(), parms.getTranslateST(),
            parms.getNonTransformingPrimvars(), pivot, file_path_attrib_name,
            path_attrib_name);

    // Set up the name / path attributes.
    GA_RWHandleS path_attrib;
    if (parms.getAddPathAttrib())
    {
        path_attrib = detail.addStringTuple(
                GA_ATTRIB_PRIMITIVE, parms.getPathAttrib(), 1);
    }

    GA_RWHandleS name_attrib;
    if (parms.getAddNameAttrib() && path_attrib.isValid())
    {
        name_attrib = detail.addStringTuple(
                GA_ATTRIB_PRIMITIVE, parms.getNameAttrib(), 1);
    }

    // Just like in the LOP Import SOP, do an optional post-pass to add
    // name and path primitive attributes to any USD primitives or polygons
    // unpacked from USD packed primitives.
    if (path_attrib.isValid() || name_attrib.isValid())
    {
        // The path attrib is created while unpacking USD packed prims to
        // polygons. Trim off the last component for the name attribute.
        if (name_attrib.isValid())
            sopSetNameAttrib(path_attrib, name_attrib);

        if (detail.containsPrimitiveType(GusdGU_PackedUSD::typeId()))
        {
            for (GA_Offset primoff : detail.getPrimitiveRange())
            {
                const GA_Primitive *prim = detail.getPrimitive(primoff);

                if (prim->getTypeId() != GusdGU_PackedUSD::typeId())
                    continue;

                const GU_PrimPacked *packed
                        = UTverify_cast<const GU_PrimPacked *>(prim);
                const GU_PackedImpl *packed_impl
                        = packed->sharedImplementation();

                // NOTE: GCC 6.3 doesn't allow dynamic_cast on non-exported
                // classes,
                //       and GusdGU_PackedUSD isn't exported for some reason,
                //       so to avoid Linux debug builds failing, we static_cast
                //       instead of UTverify_cast.
                const GusdGU_PackedUSD *packedUsd =
#if !defined(LINUX)
                        UTverify_cast<const GusdGU_PackedUSD *>(packed_impl);
#else
                        static_cast<const GusdGU_PackedUSD *>(packed_impl);
#endif
                SdfPath sdfpath = packedUsd->primPath();
                if (path_attrib.isValid())
                    path_attrib.set(primoff, sdfpath.GetString());
                if (name_attrib.isValid())
                    name_attrib.set(primoff, sdfpath.GetName());
            }
        }
    }

    // We might also need to set up a point name & path attrib when importing
    // points prims.
    GA_ROHandleS point_path_attrib = detail.findStringTuple(
            GA_ATTRIB_POINT, path_attrib_name, 1);
    if (point_path_attrib.isValid() && parms.getAddNameAttrib())
    {
        GA_RWHandleS point_name_attrib = detail.addStringTuple(
                GA_ATTRIB_POINT, parms.getNameAttrib(), 1);

        sopSetNameAttrib(point_path_attrib, point_name_attrib);
    }
}

void
SOP_UnpackUSD2Verb::cook(const CookParms &cookparms) const
{
    HUSD_ErrorScope errorscope(cookparms.error());

    auto &&parms = cookparms.parms<SOP_UnpackUSD_2_0Parms>();

    GU_Detail &detail = *cookparms.gdh().gdpNC();
    const GU_Detail &src_detail = *cookparms.inputGeo(0);

    const GA_PrimitiveGroup *group = nullptr;
    GOP_Manager gop;
    if (parms.getGroup().isstring())
    {
        bool ok = true;
        group = gop.parsePrimitiveDetached(
                parms.getGroup(), &src_detail, false, ok);
        if (!group || !ok)
        {
            cookparms.sopAddError(SOP_ERR_BADGROUP, parms.getGroup());
            return;
        }
    }

    const int usd_id = GusdGU_PackedUSD::typeId().get();
    GA_OffsetList usd_offsets;
    GA_OffsetList other_offsets;
    for (GA_Offset primoff : src_detail.getPrimitiveRange())
    {
        if ((!group || group->contains(primoff)) &&
            src_detail.getPrimitiveTypeId(primoff) == usd_id)
        {
            usd_offsets.append(primoff);
        }
        else
            other_offsets.append(primoff);
    }

    if (!parms.getDeleteOriginalPrims())
    {
        // If we aren't deleting the original packed prims, copy everything.
        detail.replaceWith(src_detail);
    }
    else if (other_offsets.size() > 0)
    {
        detail.mergePrimitives(
                src_detail,
                GA_Range(src_detail.getPrimitiveMap(), other_offsets));

        if (src_detail.findUnusedPoints(&other_offsets))
        {
            detail.mergePoints(
                    src_detail,
                    GA_Range(src_detail.getPointMap(), other_offsets));
        }
    }

    GA_Range usd_range(
            src_detail.getIndexMap(GA_ATTRIB_PRIMITIVE), usd_offsets);
    sopUnpackUSDPrims(detail, src_detail, usd_range, cookparms, parms);

    detail.bumpAllDataIds();
}

const char *
SOP_UnpackUSD2::inputLabel(unsigned idx) const
{
    switch (idx)
    {
    case 0:
        return "Packed USD Primitives";
    default:
        UT_ASSERT_MSG(false, "Invalid index");
        return "";
    }
}

PXR_NAMESPACE_CLOSE_SCOPE
