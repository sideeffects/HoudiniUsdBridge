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

#include "SOP_UsdPointInstancer.h"
#include "SOP_UsdPointInstancer.proto.h"

#include <GU/GU_PackedGeometry.h>
// #include <GU/GU_PrimPacked.h>

#include <HUSD/HUSD_Constants.h>
#include <HUSD/HUSD_DataHandle.h>
#include <HUSD/HUSD_ErrorScope.h>
#include <HUSD/HUSD_FindPrims.h>
#include <HUSD/HUSD_GetAttributes.h>
#include <HUSD/HUSD_Info.h>
#include <HUSD/HUSD_PointInstancer.h>
#include <HUSD/HUSD_TimeCode.h>
#include <HUSD/XUSD_Data.h>

#include <LOP/LOP_Node.h>
#include <LOP/LOP_PRMShared.h>

#include <OP/OP_Operator.h>
#include <PRM/PRM_TemplateBuilder.h>

#include <SOP/SOP_Error.h>
#include <SOP/SOP_Node.h>
#include <SOP/SOP_NodeVerb.h>

#include <UT/UT_StringHolder.h>
#include <UT/UT_Regex.h>

#include "UT/UT_VarEncode.h"

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
        label   "Primitive Path"
        type    string
        default { "" }
        menutoggle {
            [ "import loputils" ]
            [ "return loputils.createPrimPathMenu()" ]
            language python
        }
        parmtag { "script_action" "import loputils\nkwargs['ctrl'] = True\nloputils.selectPrimsInParm(kwargs, True,\n    lopparmname='loppath', allowinstanceproxies=True)" }
        parmtag { "script_action_help" "Select primitives using the primitive picker dialog." }
        parmtag { "script_action_icon" "BUTTONS_reselect" }
        parmtag { "sidefx::usdpathtype" "prim" }
    }
    parm {
        name    "pathattr"
        cppname "PathAttribute"
        label   "Path Attribute"
        type    toggle
        default { "1" }
    }
    parm {
        name    "importedidsdict"
        cppname "ImportedIdsDict"
        label   "Imported Ids Dictionary"
        type    toggle
        default { "1" }
    }
    parm {
        name    "importedprimvarsdict"
        cppname "ImportedPrimvarsDict"
        label   "Imported Primvars Dictionary"
        type    toggle
        default { "1" }
    }

    groupcollapsible {
        name	    "importpropertiesgroup"
        label	    "Import Properties"
        parmtag     { "group_default" "1" }

        parm {
            name    "xformintoworldspace"
            cppname "XformIntoWorldSpace"
            label   "Transform into World Space"
            type    toggle
            default { "0" }
        }

        parm {
            name    "importids"
            cppname "ImportIds"
            label   "Ids"
            type    toggle
            default { "1" }
            disablewhen "{ importinvisids == on }"
        }
        parm {
            name    "importinvisids"
            cppname "ImportInvisIds"
            label   "Invisible Ids"
            type    toggle
            default { "1" }
        }
        parm {
            name    "importpositions"
            cppname "ImportPositions"
            label   "Positions"
            type    toggle
            default { "1" }
        }
        parm {
            name    "importorientations"
            cppname "ImportOrientations"
            label   "Orientation"
            type    toggle
            default { "1" }
        }
        parm {
            name    "importscales"
            cppname "ImportScales"
            label   "Scales"
            type    toggle
            default { "1" }
        }
        parm {
            name    "importaccelerations"
            cppname "ImportAccelerations"
            label   "Accelerations"
            type    toggle
            default { "1" }
        }
        parm {
            name    "importvelocities"
            cppname "ImportVelocities"
            label   "Velocities"
            type    toggle
            default { "1" }
        }
        parm {
            name    "importangularvelocities"
            cppname "ImportAngularVelocities"
            label   "Angular Velocities"
            type    toggle
            default { "1" }
        }
        parm {
            name    "primvarsfilter"
            cppname "PrimvarsFilter"
            label   "Primvars Filter"
            type    string
            default { "*" }
            menutoggle {
                [ "from pxr import UsdGeom" ]
                [ "" ]
                [ "menu = []" ]
                [ "node = kwargs['node']" ]
                [ "loppath = node.parm('loppath').eval()" ]
                [ "PrimPattern = node.parm('PrimPattern').eval()" ]
                [ "" ]
                [ "try:" ]
                [ "    stage = hou.node(loppath).stage()" ]
                [ "except:" ]
                [ "    return []" ]
                [ "" ]
                [ "if not stage:" ]
                [ "    return []" ]
                [ "" ]
                [ "api = UsdGeom.PrimvarsAPI(stage.GetPrimAtPath(PrimPattern))" ]
                [ "if not api:" ]
                [ "    return []" ]
                [ "    " ]
                [ "for primvar in api.GetPrimvarsWithAuthoredValues():" ]
                [ "    menu.extend([primvar.GetPrimvarName(), primvar.GetPrimvarName()])" ]
                [ "" ]
                [ "return menu" ]
                language python
            }
        }
        parm {
            name    "commonprimvars"
            cppname "CommonPrimvars"
            label   "Common Primvars"
            type    string
            default { "" }
            hidewhen "{ importedprimvarsdict == off }"
        }
        parm {
            name    "sepparm2"
            label   "Separator"
            type    separator
            default { "" }
        }
        parm {
            name    "protomode"
            cppname "ProtoMode"
            label   "Import Prototypes From"
            type    string
            default { "fromattr" }
            menu {
                "none"             "None"
                "protoindices"     "protoIndices Attribute"
                "protoprimpath"    "Prototype Prim Path"
                "protoprimname"    "Prototype Prim Name"
            }
        }
        parm {
            name    "protointattr"
            cppname "ProtoIntAttr"
            label   "Integer Attrib Name"
            type    string
            default { "protoindex" }
            hidewhen "{ protomode != protoindices }"
        }
        parm {
            name    "protostrattr"
            cppname "ProtoStrAttr"
            label   "String Attrib Name"
            type    string
            default { "protoname" }
            hidewhen "{ protomode == protoindices } { protomode == none }"
        }
        parm {
            name    "sepparm3"
            label   "Separator"
            type    separator
            default { "" }
        }
        parm {
            name    "bboxmode"
            cppname "BBoxMode"
            label   "Import Bounding Box"
            type    string
            default { "none" }
            joinnext
            menu {
                "none"      "None"
                "asattr"    "As Attribute"
                "aspacked"  "As Packed Primitive"
                "asboth"    "As Attribute and Packed Primitive"
            }
        }
        parm {
            name    "bboxpurposes"
            cppname "BBoxPurposes"
            label   "Purposes"
            type    string
            default { "default" }
            menureplace {
                "default"       "default"
                "render,proxy"  "render,proxy"
                "render"        "render"
                "proxy"         "proxy"
                "guide"         "guide"
            }
            hidewhen "{ bboxmode == none }"
        }
        parm {
            name    "bboxattr"
            cppname "BBoxAttr"
            label   "Attribute"
            type    string
            default { "bounds" }
            hidewhen "{ bboxmode != asattr bboxmode != asboth }"
        }
    }
}
)THEDSFILE";

PRM_Template *
SOP_UsdPointInstancer::buildTemplates()
{
   static PRM_TemplateBuilder templ("SOP_UsdPointInstancer.C", theDsFile);
   return templ.templates();
}

OP_Operator *
SOP_UsdPointInstancer::createOperator()
{
   return new OP_Operator(
           "usdpointinstancerimport", "USD PointInstancer Import", myConstructor, buildTemplates(), 0,
           0, nullptr);
}

SOP_UsdPointInstancer::SOP_UsdPointInstancer(
       OP_Network *net,
       const char *name,
       OP_Operator *op)
   : SOP_Node(net, name, op)
{
   mySopFlags.setManagesDataIDs(true);
}

OP_ERROR
SOP_UsdPointInstancer::cookMySop(OP_Context &context)
{
   return cookMyselfAsVerb(context);
}

class SOP_UsdPointInstancerVerb : public SOP_NodeVerb
{
public:
    SOP_UsdPointInstancerVerb() {}
   ~SOP_UsdPointInstancerVerb() override {}

   SOP_NodeParms *allocParms() const override
   {
       return new SOP_UsdPointInstancerParms();
   }

   UT_StringHolder name() const override
   {
       return "loppointinstancer";
   }

   CookMode cookMode(const SOP_NodeParms *parms) const override
   {
       return COOK_GENERATOR;
   }

   void cook(const CookParms &cookparms) const override;
};

static SOP_NodeVerb::Register<SOP_UsdPointInstancerVerb> theSOPLOPPointInstancerVerb;

const SOP_NodeVerb *
SOP_UsdPointInstancer::cookVerb() const
{
   return theSOPLOPPointInstancerVerb.get();
}

void
SOP_UsdPointInstancerVerb::cook(const CookParms &cookparms) const
{
    HUSD_ErrorScope errorscope(cookparms.error());
    OP_Context      context(cookparms.getContext());

    auto &&parms = cookparms.parms<SOP_UsdPointInstancerParms>();

    LOP_Node *lop = cookparms.getCwd()->getLOPNode(parms.getLOPPath());
    if (!lop)
    {
        cookparms.sopAddWarning(SOP_MESSAGE, "No LOP at path.");
        return;
    }

    // add lop's dataMicroNode as an input to trigger a re-cook when the lop
    // data changes.
    cookparms.addExplicitInput(lop->dataMicroNode());

    HUSDPointInstancerParms husdparms;
    husdparms.myPrimPattern = parms.getPrimPattern();
    husdparms.myPrimvarsFilter = parms.getPrimvarsFilter();
    husdparms.myCreatePathAttribute = parms.getPathAttribute();
    husdparms.myTransformIntoWorldSpace = parms.getXformIntoWorldSpace();
    husdparms.myImportPositions = parms.getImportPositions();
    husdparms.myImportOrientations = parms.getImportOrientations();
    husdparms.myImportScales = parms.getImportScales();
    husdparms.myImportAccelerations = parms.getImportAccelerations();
    husdparms.myImportVelocities = parms.getImportVelocities();
    husdparms.myImportAngularVelocities = parms.getImportAngularVelocities();
    husdparms.myImportIds = parms.getImportIds();
    husdparms.myImportVisibility = parms.getImportInvisIds();

    if (parms.getProtoMode() == "none")
        husdparms.myProtoSource = HUSD_PointInstancerSopProtoIndexSource::None;
    else if (parms.getProtoMode() == "protoindices")
        husdparms.myProtoSource = HUSD_PointInstancerSopProtoIndexSource::Attribute;
    else if (parms.getProtoMode() == "protoprimpath")
        husdparms.myProtoSource = HUSD_PointInstancerSopProtoIndexSource::PrimPath;
    else if (parms.getProtoMode() == "protoprimname")
        husdparms.myProtoSource = HUSD_PointInstancerSopProtoIndexSource::PrimName;

    husdparms.myImportBoundingBoxesAsAttr = false;
    husdparms.myImportBoundingBoxesAsPacked = false;
    if (parms.getBBoxMode() == "asattr")
        husdparms.myImportBoundingBoxesAsAttr = true;
    else if (parms.getBBoxMode() == "aspacked")
        husdparms.myImportBoundingBoxesAsPacked = true;
    else if (parms.getBBoxMode() == "asboth")
    {
        husdparms.myImportBoundingBoxesAsAttr = true;
        husdparms.myImportBoundingBoxesAsPacked = true;
    }

    husdparms.myIntAttrName = parms.getProtoIntAttr();
    husdparms.myStrAttrName = parms.getProtoStrAttr();

    husdparms.myImportBoundingBoxesAttr = parms.getBBoxAttr();

    UT_StringRef purposes = parms.getBBoxPurposes();
    UT_StringViewArray   tokens = UT_StringView(purposes).tokenize(", ");
    for (auto& purp : tokens)
    {
        UT_StringHolder hardened(purp);
        husdparms.myImportBoundingBoxesPurposes.append(hardened);
    }

    GU_Detail      *gdp = cookparms.gdh().gdpNC();
    if (gdp == nullptr)
    {
        cookparms.sopAddError(SOP_MESSAGE, "Invalid GU Detail.");
        return;
    }

    HUSD_AutoReadLock   readlock(lop->getCookedDataHandle(context));
    if (!readlock.constData() ||
        !readlock.constData()->isStageValid())
    {
        cookparms.sopAddError(SOP_MESSAGE,
                              "Invalid LOP Network or USD Stage");
        return;
    }

    HUSD_FindPrims                findprims(readlock);
    HUSD_GetAttributes            getattrs(readlock);
    HUSD_Info                     info(readlock);
    UT_StringArray                primpaths;
    UT_StringMap<UT_Array<exint>> pointinstancer_map;

    if (parms.getPrimPattern().contains("\\[")) // contains id syntax
    {
        findprims.setFindPointInstancerIds(true);
        if (!findprims.addPattern(parms.getPrimPattern(), lop->getUniqueId(),
                                 HUSD_TimeCode(context.getTime())))
        {
            cookparms.sopAddError(SOP_MESSAGE,
                                  "Invalid Primitive Pattern Supplied.");
            return;
        }
        pointinstancer_map = findprims.getPointInstancerIds();
    }
    else
    {
        if (LOP_Node::getSimplifiedCollection(lop, parms.getPrimPattern(), findprims))
            findprims.getExpandedPathSet().getPathsAsStrings(primpaths);
        else
        {
            cookparms.sopAddError(SOP_MESSAGE,
                                  "Invalid Primitive Pattern Supplied.");
            return;
        }
        // empty indices list means affect all instances.
        for (const auto &x : primpaths)
        {
            pointinstancer_map[x] = UT_Array<exint>();
        }
    }

    if (pointinstancer_map.size() == 0)
        return;

    // Copy USD Attrs & Primvars to SOP Points
    HUSD_TimeCode timeCode(context.getTime(), HUSD_TimeCode::TIME);
    HUSD_PointInstancer::copyUsdAttrsToGeoAttrs(gdp, readlock, husdparms,
                                                pointinstancer_map, timeCode);

    if (parms.getImportedIdsDict() || parms.getImportedPrimvarsDict())
    {
        // Create "importedids" Dictionary Detail Attribute, to hold the per-
        // PointInstancer ids that were imported by this node.  This is useful
        // for the PointInstancer SOP to check for deleted ids.
        UT_Options         idsmap;
        UT_Options         primvarmap;
        UT_StringArray     globalprimvars;
        {
            UT_StringView      globalprimvarsview = parms.getCommonPrimvars();
            UT_StringViewArray&& tokens = globalprimvarsview.split();
            globalprimvars.setSize(tokens.size());
            for (auto&& token : tokens)
                globalprimvars.append(token);
        }

        for (auto pi : pointinstancer_map)
        {
            // Todo: The details attribute view cannot handle an entry with
            //       millions of entries
            if (parms.getImportedIdsDict())
            {
                if (pi.second.isEmpty())
                {
                    UT_Array<exint> ids;
                    getattrs.getAttribute(pi.first, HUSD_Constants::getAttributePointIds(), ids, timeCode);
                    if (ids.isEmpty())
                    {
                        exint numpoints = info.getPointInstancerInstanceCount(pi.first, timeCode);
                        pi.second.setSize(numpoints);
                        for (exint idx = 0; idx < numpoints; ++idx)
                            pi.second[idx] = idx;
                        idsmap.setOptionIArray(pi.first, pi.second);
                    }
                    else
                        idsmap.setOptionIArray(pi.first, ids);
                }
                else
                    idsmap.setOptionIArray(pi.first, pi.second);
            }

            if (parms.getImportedPrimvarsDict())
            {
                UT_ArrayStringSet primvarsset;
                info.getPrimvarNames(pi.first, primvarsset);
                UT_StringArray primvars;
                primvars.setCapacity(primvarsset.size());
                for (const UT_StringHolder& primvar : primvarsset)
                {
                    // primvar names always start with 'primvar:', which we don't
                    // want
                    UT_StringView pv(primvar);
                    primvars.append(UT_VarEncode::encodeVar(pv.substr(9)));
                }
                primvars.concat(globalprimvars);
                primvarmap.setOptionSArray(pi.first, primvars);
            }
        }

        if (parms.getImportedIdsDict())
        {
            GA_RWHandleDict importidsattr = gdp->addDictTuple(GA_ATTRIB_DETAIL,
                                                              GA_SCOPE_PUBLIC,
                                                              "importedids", 1);
            UT_OptionsHolder holder(&idsmap);
            importidsattr.set(GA_Offset(0), holder);
        }

        if (parms.getImportedPrimvarsDict())
        {
            GA_RWHandleDict primvarsattr = gdp->addDictTuple(GA_ATTRIB_DETAIL,
                                                             GA_SCOPE_PUBLIC,
                                                             "importedprimvars", 1);
            UT_OptionsHolder primvarholder(&primvarmap);
            primvarsattr.set(GA_Offset(0), primvarholder);
        }
    }
}

const char* SOP_UsdPointInstancer::inputLabel(OP_InputIdx idx) const
{
    UT_ASSERT(idx >= 0);
    return "";
}

PXR_NAMESPACE_CLOSE_SCOPE
