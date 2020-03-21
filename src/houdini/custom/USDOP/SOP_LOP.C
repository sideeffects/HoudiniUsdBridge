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

#include "SOP_LOP.h"

#include <LOP/LOP_Node.h>
#include <LOP/LOP_PRMShared.h>
#include <gusd/GU_USD.h>
#include <gusd/GU_PackedUSD.h>
#include <gusd/PRM_Shared.h>
#include <gusd/USD_Traverse.h>
#include <gusd/USD_Utils.h>
#include <gusd/UT_Assert.h>
#include <gusd/UT_StaticInit.h>
#include <HUSD/HUSD_Constants.h>
#include <HUSD/HUSD_DataHandle.h>
#include <HUSD/HUSD_ErrorScope.h>
#include <HUSD/HUSD_FindPrims.h>
#include <HUSD/HUSD_LoadMasks.h>
#include <HUSD/HUSD_LockedStageRegistry.h>
#include <HUSD/XUSD_PathSet.h>
#include <GU/GU_PrimPacked.h>
#include <GA/GA_AttributeFilter.h>
#include <OP/OP_AutoLockInputs.h>
#include <OP/OP_Director.h>
#include <OP/OP_OperatorTable.h>
#include <PI/PI_EditScriptedParms.h>
#include <PRM/PRM_AutoDeleter.h>
#include <PRM/PRM_ChoiceList.h>
#include <UT/UT_WorkArgs.h>
#include <UT/UT_UniquePtr.h>
#include <PY/PY_Python.h>

#include <pxr/usd/usd/stage.h>
#include <pxr/base/tf/pathUtils.h>
#include <pxr/base/tf/fileUtils.h>

PXR_NAMESPACE_OPEN_SCOPE

namespace {

enum ErrorChoice { MISSINGFRAME_ERR, MISSINGFRAME_WARN };

static PRM_Template	 theCollectionParmTemplate;

#define _NOTRAVERSE_NAME "none"

int
_TraversalChangedCB(void* data, int idx, fpreal64 t,
	const PRM_Template* tmpl)
{
    auto& sop = *reinterpret_cast<SOP_LOP*>(data);
    sop.UpdateTraversalParms();
    return 0;
}

void
_ConcatTemplates(UT_Array<PRM_Template>& array,
	const PRM_Template* templates)
{
    int count = PRM_Template::countTemplates(templates);
    if(count > 0) {
	exint idx = array.size();
	array.bumpSize(array.size() + count);
	UTconvertArray(&array(idx), templates, count);
    }
}

PRM_ChoiceList &
_CreateTraversalMenu()
{
    static PRM_Name noTraverseName(_NOTRAVERSE_NAME, "No Traversal");

    static UT_Array<PRM_Name> names;
    names.append(noTraverseName);

    const auto& table = GusdUSD_TraverseTable::GetInstance();
    for(const auto& pair : table)
	names.append(pair.second->GetName());
    
    names.stdsort(
	[](const PRM_Name& a, const PRM_Name& b)    
	{ return UT_String(a.getLabel()) < UT_String(b.getLabel()); });
    names.append(PRM_Name());

    static PRM_ChoiceList menu(PRM_CHOICELIST_SINGLE, &names(0));
    return menu;
}

PRM_Template *
_CreateTemplates()
{
    static PRM_Name loppathName("loppath", "LOP Path");

    static PRM_Name pathAttribName("pathattrib", "Create Path Attribute");
    static PRM_Default pathAttribDef(0, "path");
    static PRM_Name nameAttribName("nameattrib", "Create Name Attribute");
    static PRM_Default nameAttribDef(0, "name");
    static PRM_Name timeName("importtime", "Import Frame");
    static PRM_Default timeDef(0, "$FF");

    static PRM_Name traversalName("importtraversal", "Traversal");
    static PRM_Default traversalDef(0, "none" );

    static PRM_Name stripLayersName("striplayers",
	"Strip Layers Preceding Layer Breaks");

    static PRM_Name    viewportlodName("viewportlod", "Display As");
    static PRM_Default viewportlodDefault(0, "full");

    static PRM_Name purposeName("purpose", "Purpose");
    static PRM_Default purposeDefault(0, "proxy");
    static PRM_Name purposeChoices[] = {
	PRM_Name("proxy", "proxy"),
	PRM_Name("render", "render"),
	PRM_Name("guide", "guide"),
	PRM_Name(0)
    };
    static PRM_ChoiceList purposeMenu(PRM_CHOICELIST_TOGGLE, purposeChoices);

    static const char	*referencedLopCollectionsMenuScript =
	"import loputils\n"
	"node = hou.node(kwargs['node'].parm('loppath').eval())\n"
	"return loputils.createCollectionsMenu(node)";
    static PRM_ChoiceList referencedLopCollectionsMenu(PRM_CHOICELIST_TOGGLE,
	referencedLopCollectionsMenuScript, CH_PYTHON_SCRIPT);
    static const char	*primPatternSpareDataBaseScript =
	"import loputils\n"
	"kwargs['ctrl'] = True\n"
	"loputils.selectPrimsInParm(kwargs, True, lopparmname='loppath')";
    static PRM_SpareData primPatternSpareData(PRM_SpareArgs() <<
	PRM_SpareData::usdPathTypePrimList <<
	PRM_SpareToken(
	    PRM_SpareData::getScriptActionToken(),
	    primPatternSpareDataBaseScript) <<
	PRM_SpareToken(
	    PRM_SpareData::getScriptActionHelpToken(),
            "Select primitives using the primitive picker dialog.") <<
	PRM_SpareToken(
	    PRM_SpareData::getScriptActionIconToken(),
	    "BUTTONS_reselect"));

    GusdPRM_Shared shared;

    static PRM_Template templates[] = {
	PRM_Template(PRM_STRING, PRM_TYPE_DYNAMIC_PATH, 1, &loppathName,
		     /*default*/ 0, /*choicelist*/ 0, /*range*/ 0,
		     /*callback*/ 0, &PRM_SpareData::lopPath),
	PRM_Template(PRM_STRING, 1, &lopPrimPatternName, 0,
		     &referencedLopCollectionsMenu, 0, 0,
		     &primPatternSpareData),

	PRM_Template(PRM_STRING, 1, &pathAttribName, &pathAttribDef),
	PRM_Template(PRM_STRING, 1, &nameAttribName, &nameAttribDef),
	PRM_Template(PRM_FLT, 1, &timeName, &timeDef),
	PRM_Template(PRM_ORD, 1, &traversalName,
		     &traversalDef, &_CreateTraversalMenu(),
		     /*range*/ 0, _TraversalChangedCB),
	PRM_Template(PRM_TOGGLE, 1, &stripLayersName),
	PRM_Template(PRM_SEPARATOR),
	PRM_Template(PRM_ORD, 1, &viewportlodName, 
		     &viewportlodDefault, &PRMviewportLODMenu),
        PRM_Template(PRM_ORD, 1, &PRMpackedPivotName, PRMoneDefaults,
                     &PRMpackedPivotMenu),
	PRM_Template(PRM_STRING, 1, &purposeName,
		     &purposeDefault, &purposeMenu ),
	PRM_Template()
    };

    return templates;
}

auto _mainTemplates(GusdUT_StaticVal(_CreateTemplates));

} /*namespace*/

void
SOP_LOP::Register(OP_OperatorTable* table)
{
    OP_Operator* op =
	new OP_Operator("lopimport",
			"LOP Import",
			Create,
			*_mainTemplates,
			/* min inputs */ 0,
			/* max inputs */ 0,
			/* variables  */ 0,
			OP_FLAG_GENERATOR);
    op->setIconName("SOP_lopimport");
    table->addOperator(op);
}

OP_Node*
SOP_LOP::Create(OP_Network* net, const char* name, OP_Operator* op)
{
    return new SOP_LOP(net, name, op);
}

SOP_LOP::SOP_LOP(
    OP_Network* net, const char* name, OP_Operator* op)
  : SOP_Node(net, name, op)
{
}

SOP_LOP::~SOP_LOP()
{
}

void
SOP_LOP::UpdateTraversalParms()
{
    if(getIsChangingSpareParms())
	return;

    UT_String traversal;
    evalString(traversal, "importtraversal", 0, 0);

    const auto& table = GusdUSD_TraverseTable::GetInstance();

    const PRM_Template* customTemplates = NULL;
    if(traversal != _NOTRAVERSE_NAME) {
	if(const auto* type = table.Find(traversal))
	    customTemplates = type->GetTemplates();
    }

    myTemplates.clear();
    const int nCustom = customTemplates ?
	PRM_Template::countTemplates(customTemplates) : 0;
    if(nCustom > 0) {
	/* Build a template list that puts the main
	   templates in one tab, and the custom templates in another.*/
	static const int nMainTemplates =
	    PRM_Template::countTemplates(*_mainTemplates);

	myTabs[0] = PRM_Default(nMainTemplates, "Main");
	myTabs[1] = PRM_Default(nCustom, "Advanced");

	static PRM_Name tabsName("importmyTabs", "");
	
	myTemplates.append(PRM_Template(PRM_SWITCHER, 2, &tabsName, myTabs));
	
	_ConcatTemplates(myTemplates, *_mainTemplates);
	_ConcatTemplates(myTemplates, customTemplates);
    }
    myTemplates.append(PRM_Template());
		       

    /* Add the custom templates as spare parms.*/
    PI_EditScriptedParms parms(this, &myTemplates(0), /*spare*/ true,
			       /*skip-reserved*/ false, /*init links*/ false);
    UT_String errs;
    GusdUTverify_ptr(OPgetDirector())->changeNodeSpareParms(this, parms, errs);
    
    _AddTraversalParmDependencies();
}

void
SOP_LOP::_AddTraversalParmDependencies()
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

OP_ERROR
SOP_LOP::_Cook(OP_Context& ctx)
{
    fpreal t = ctx.getTime();

    UT_String traversal;
    evalString( traversal, "importtraversal", 0, t );

    const GusdUSD_Traverse* trav = NULL;
    if(traversal != _NOTRAVERSE_NAME) {
	const auto& table = GusdUSD_TraverseTable::GetInstance();
	trav = table.FindTraversal(traversal);
	
	if(!trav) {
	    UT_WorkBuffer buf;
	    buf.sprintf("Failed locating traversal '%s'", traversal.c_str());
	    return error();
	}
    }
    return _CreateNewPrims(ctx, trav);
}                           

OP_ERROR
SOP_LOP::_CreateNewPrims(OP_Context& ctx, const GusdUSD_Traverse* traverse)
{
    fpreal		 t = ctx.getTime();
    UT_String		 loppath;
    UT_String		 prim_pattern;
    HUSD_LockedStagePtr	 locked_stage;

    evalString(loppath, "loppath", 0, t);
    evalString(prim_pattern, lopPrimPatternName.getToken(), 0, t);
    if(!loppath.isstring()) {
	return error();
    }

    LOP_Node		*lop = getLOPNode(loppath, 1);

    if (!lop)
    {
	addError(SOP_MESSAGE, "Invalid LOP Node path.");
	return error();
    }

    OP_Context		 lopctx(ctx);
    lopctx.setFrame(evalFloat("importtime", 0, t));

    HUSD_DataHandle	 datahandle = lop->getCookedDataHandle(lopctx);
    HUSD_ErrorScope	 errorscope(this, true);
    bool		 strip_layers = evalInt("striplayers", 0, t);

    // Create our new locked stage, and free up the old one we were holding
    // on to. This will take care of cleaning up the stage cache as well.
    locked_stage = HUSD_LockedStageRegistry::getInstance().
	getLockedStage(lop->getUniqueId(), datahandle,
	    strip_layers, lopctx.getTime(), HUSD_IGNORE_STRIPPED_LAYERS);

    HUSD_AutoReadLock	 readlock(datahandle);
    HUSD_FindPrims	 findprims(readlock, HUSD_PrimTraversalDemands(
                                HUSD_TRAVERSAL_DEFAULT_DEMANDS |
                                HUSD_TRAVERSAL_ALLOW_INSTANCE_PROXIES));
    GusdStageCacheReader cache;
    UsdStageRefPtr	 stage =
        cache.Find(locked_stage->getStageCacheIdentifier().toStdString());

    if (!stage)
    {
	addError(SOP_MESSAGE, "Failed to cook LOP node.");
	return error();
    }

    if (!LOP_Node::getSimplifiedCollection(this, prim_pattern, findprims))
    {
	addError(SOP_MESSAGE, "Failed to find primitive targets.");
	return error();
    }

    // Load the root prims from the locked stage (even though the prim paths
    // came from the LOP's data handle).
    UT_Array<UsdPrim>	 rootPrims;

    for (auto &&it : findprims.getExpandedPathSet())
    {
	UsdPrim	 prim = stage->GetPrimAtPath(it);

	if (prim)
	    rootPrims.append(prim);
    }

    UT_String		 purposestr;
    evalString(purposestr, "purpose", 0, t);
    UT_String            lod;
    evalString(lod, "viewportlod", 0, t);
    UsdTimeCode          time(lopctx.getFloatFrame());
    GusdPurposeSet       purpose(GusdPurposeSet(
			     GusdPurposeSetFromMask(purposestr)|
			     GUSD_PURPOSE_DEFAULT));
    UT_Array<UsdPrim>	 prims;
    GusdDefaultArray<UsdTimeCode> times;
    GusdDefaultArray<GusdPurposeSet> purposes;

    times.SetConstant(time);
    purposes.SetConstant(purpose);
    if(traverse) {
	UT_Array<GusdUSD_Traverse::PrimIndexPair> primIndexPairs;

	UT_UniquePtr<GusdUSD_Traverse::Opts> opts(traverse->CreateOpts());
	if(opts) {
	    if(!opts->Configure(*this, t))
		return error();
	}

	if(!traverse->FindPrims(rootPrims, times, purposes, primIndexPairs,
				/*skip root*/ false, opts.get())) {
	    return error();
	}

	// Resize the prims list to match the size of primIndexPairs.
	prims.setSize(primIndexPairs.size());
	// Then iterate through primIndexPairs to populate the prim list.
	for (exint i = 0, n = prims.size(); i < n; i++) {
	    prims(i) = primIndexPairs(i).first;
	}
    } else {
	std::swap(prims, rootPrims);
    }

    GusdGU_PackedUSD::PivotLocation pivotloc =
        GusdGU_PackedUSD::PivotLocation::Origin;
    if (evalInt(PRMpackedPivotName.getTokenRef(), 0, t) == 1)
        pivotloc = GusdGU_PackedUSD::PivotLocation::Centroid;

    // We have the resolved set of USD prims. Now create packed prims in the
    // geometry.
    GusdGU_USD::AppendPackedPrimsFromLopNode(
        *gdp, locked_stage->getStageCacheIdentifier(), prims, time, lod,
        purpose, pivotloc);

    UT_String		 pathAttribName;
    GA_Attribute	*pathAttrib = nullptr;
    UT_String		 nameAttribName;
    GA_Attribute	*nameAttrib = nullptr;
    evalString(pathAttribName, "pathattrib", 0, t);
    evalString(nameAttribName, "nameattrib", 0, t);
    if (pathAttribName.isstring())
	pathAttrib = gdp->addStringTuple(
	    GA_ATTRIB_PRIMITIVE, pathAttribName, 1);
    if (nameAttribName.isstring())
	nameAttrib = gdp->addStringTuple(
	    GA_ATTRIB_PRIMITIVE, nameAttribName, 1);
    if (pathAttrib || nameAttrib)
    {
	GA_RWHandleS	 hpath(pathAttrib);
	GA_RWHandleS	 hname(nameAttrib);

	if (hpath.isValid() || hname.isValid())
	{
	    for (GA_Iterator it(gdp->getPrimitiveRange());
		 !it.atEnd(); ++it)
	    {
		const GA_Primitive *prim = gdp->getPrimitive(*it);

		if (prim->getTypeId() != GusdGU_PackedUSD::typeId())
		    continue;

		const GU_PrimPacked *packed = UTverify_cast<const GU_PrimPacked *>(prim);
		const GU_PackedImpl *packedImpl = packed->implementation();

                // NOTE: GCC 6.3 doesn't allow dynamic_cast on non-exported classes,
                //       and GusdGU_PackedUSD isn't exported for some reason,
                //       so to avoid Linux debug builds failing, we static_cast
                //       instead of UTverify_cast.
                const GusdGU_PackedUSD *packedUsd =
#if !defined(LINUX)
                    UTverify_cast<const GusdGU_PackedUSD *>(packedImpl);
#else
                    static_cast<const GusdGU_PackedUSD *>(packedImpl);
#endif
                SdfPath sdfpath = packedUsd->primPath();
		if (hpath.isValid())
		    hpath.set(*it, sdfpath.GetText());
		if (hname.isValid())
		    hname.set(*it, sdfpath.GetName());
	    }
	}
    }

    return error();
}

OP_ERROR
SOP_LOP::cookMySop(OP_Context& ctx)
{
    OP_AutoLockInputs lock(this);
    if(lock.lock(ctx) >= UT_ERROR_ABORT)
	return error();

    // Local var support.
    setCurGdh(0, myGdpHandle);
    setupLocalVars();

    gdp->clearAndDestroy();

    /* Extra inputs have to be re-added on each cook.*/
    _AddTraversalParmDependencies();
    _Cook(ctx);
	
    resetLocalVarRefs();

    return error();
}

void
SOP_LOP::finishedLoadingNetwork(bool isChildCall)
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
SOP_LOP::syncNodeVersion(const char *old_version,
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
}

PXR_NAMESPACE_CLOSE_SCOPE

