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
 *	Rob Bairos
 *      Side Effects Software Inc
 *      477 Richmond Street West
 *      Toronto, Ontario
 *      Canada   M5V 3E7
 *      416-504-9876
 *
 * NAME:        OBJ_LOP.C (Custom Library, C++)
 *
 * COMMENTS:    An object to fetch it's transform from another object.
 */

#include "OBJ_LOP.h"
#include <OBJ/OBJ_Shared.h>
#include <OBJ/OBJ_SharedNames.h>
#include <LOP/LOP_Node.h>
#include <LOP/LOP_Error.h>
#include <LOP/LOP_PRMShared.h>
#include <HUSD/HUSD_DataHandle.h>
#include <HUSD/XUSD_Data.h>
#include <HUSD/XUSD_Utils.h>
#include <gusd/UT_Gf.h>
#include <OP/OP_OperatorTable.h>
#include <OP/OP_AutoLockInputs.h>
#include <PRM/PRM_Include.h>
#include <PRM/PRM_ChoiceList.h>
#include <PRM/PRM_SpareData.h>
#include <PRM/PRM_Parm.h>
#include <pxr/usd/usdGeom/imageable.h>
#include <pxr/usd/usdGeom/xformCache.h>
#include <pxr/usd/usd/stage.h>

PXR_NAMESPACE_OPEN_SCOPE

int *OBJ_LOP::fetchIndirect = 0;

void
OBJ_LOP::Register(OP_OperatorTable* table)
{
    OP_Operator* op =
	new OP_Operator("lopimport",
			"LOP Import",
			Create,
			OBJ_LOP::getTemplateList(),
			OBJ_LOP::theChildTableName,
			/* min inputs */ 0,
			/* max inputs */ 1,
			/* variables  */ 0,
			OP_FLAG_GENERATOR);
    op->setIconName("OBJ_lopimport");
    table->addOperator(op);
}

static PRM_Name		 theLopPathName("loppath", "LOP Path");
static const char	*thePrimPathSpareDataBaseScript =
    "import loputils\n"
    "kwargs['ctrl'] = True\n"
    "loputils.selectPrimsInParm(kwargs, False, lopparmname='loppath')";
static PRM_SpareData	 thePrimPathSpareData(PRM_SpareArgs() <<
    PRM_SpareData::usdPathTypePrim <<
    PRM_SpareToken(
	PRM_SpareData::getScriptActionToken(),
	thePrimPathSpareDataBaseScript) <<
    PRM_SpareToken(
	PRM_SpareData::getScriptActionHelpToken(),
	    "Select primitives using the "
	    "primitive picker dialog.") <<
    PRM_SpareToken(
	PRM_SpareData::getScriptActionIconToken(),
	"BUTTONS_reselect"));

enum {
    OBJ_LOP_XFORMTYPE_LOCALTOWORLD,
    OBJ_LOP_XFORMTYPE_LOCAL,
    OBJ_LOP_XFORMTYPE_PARENTTOWORLD,
};
static PRM_Name		 theXformTypeName("xformtype", "Transform Type");
static PRM_Name		 theXformTypeChoices[] = {
    PRM_Name("localtoworld", "Local to World"),
    PRM_Name("local", "Local"),
    PRM_Name("parenttoworld", "Parent to World"),
    PRM_Name()
};
static PRM_Default	 theXformTypeDefault(0,
    theXformTypeChoices[OBJ_LOP_XFORMTYPE_LOCALTOWORLD].getToken());
static PRM_ChoiceList	 theXformTypeMenu(PRM_CHOICELIST_SINGLE,
    theXformTypeChoices);

PRM_Template *
OBJ_LOP::getTemplateList()
{
    // The parm templates here are not created as a static list because
    // if that static list was built before the OBJbaseTemplate static list
    // (which it references) then that list would be corrupt.  Thus we have
    // to force our static list to be created after OBJbaseTemplate.
    static PRM_Template	    *theTemplate = 0;
    static PRM_Template	     OBJlopTemplate[] = {
	PRM_Template(PRM_STRING, PRM_TYPE_DYNAMIC_PATH, 1, &theLopPathName,
		     /*default*/ 0, /*choicelist*/ 0, /*range*/ 0,
		     /*callback*/ 0, &PRM_SpareData::lopPath),
	PRM_Template(PRM_STRING, 1, &lopPrimPathName, 0,
		     0, 0, 0, &thePrimPathSpareData),
	PRM_Template(PRM_STRING, 1, &theXformTypeName,
		     &theXformTypeDefault, &theXformTypeMenu),
    };

    if (!theTemplate)
    {
	int	    i = 0;

	// We need space for I_N_LOP_INDICES and one for the list terminator
	theTemplate = new PRM_Template[I_N_LOP_INDICES+1];

	// Fetch parms (7)
	SET_TPLATE_EX(OBJlopTemplate, I_LOP_LOPPATH - I_N_GEO_INDICES)
	SET_TPLATE_EX(OBJlopTemplate, I_LOP_PRIMPATH - I_N_GEO_INDICES)
	SET_TPLATE_EX(OBJlopTemplate, I_LOP_XFORMTYPE - I_N_GEO_INDICES)
	SET_TPLATE(OBJbaseTemplate, I_USE_DCOLOR)
	SET_TPLATE(OBJbaseTemplate, I_DCOLOR)
	SET_TPLATE(OBJbaseTemplate, I_PICKING)
	SET_TPLATE(OBJbaseTemplate, I_PICKSCRIPT)
	SET_TPLATE(OBJbaseTemplate, I_CACHING)

	// Transform (18) - old transform page of ignored parameters.
	SET_TPLATE(OBJbaseITemplate, I_XORDER)
	SET_TPLATE(OBJbaseITemplate, I_RORDER)
	SET_TPLATE(OBJbaseITemplate, I_T)
	SET_TPLATE(OBJbaseITemplate, I_R)
	SET_TPLATE(OBJbaseITemplate, I_S)
	SET_TPLATE(OBJbaseITemplate, I_P)
	SET_TPLATE(OBJbaseITemplate, I_PIVOTR)
	SET_TPLATE(OBJbaseITemplate, I_SCALE)
	SET_TPLATE(OBJbaseITemplate, I_PRETRANSFORM)
	SET_TPLATE(OBJbaseITemplate, I_KEEPPOS)
	SET_TPLATE(OBJbaseITemplate, I_CHILDCOMP)
	SET_TPLATEI_LEGACY_LOOKAT_PATH()

	// Render (8)
	SET_TPLATE(OBJbaseITemplate, I_TDISPLAY)
	SET_TPLATE(OBJbaseITemplate, I_DISPLAY)
	SET_TPLATE(OBJgeoITemplate, I_SHOP_MATERIAL - I_N_BASE_INDICES)
	SET_TPLATE(OBJgeoITemplate, I_SHOP_MATERIALOPT - I_N_BASE_INDICES)

	// Misc (3)
	SET_TPLATE(OBJgeoITemplate, I_VPORT_SHADEOPEN - I_N_BASE_INDICES)
	SET_TPLATE(OBJgeoITemplate, I_VPORT_DISPLAYASSUBDIV - I_N_BASE_INDICES)
	SET_TPLATE(OBJgeoITemplate, I_VPORT_ONIONSKIN - I_N_BASE_INDICES)

        UT_ASSERT(i == I_N_LOP_INDICES);
	theTemplate[i++] = PRM_Template();
    }
    return theTemplate;
}

OBJ_LOP::OBJ_LOP(OP_Network *net, const char *name, OP_Operator *op)
		  : OBJ_Geometry(net, name, op)
{
    if (!fetchIndirect)
        fetchIndirect = allocIndirect(I_N_LOP_INDICES);
} 

OBJ_LOP::~OBJ_LOP()
{
}

OBJ_OBJECT_TYPE
OBJ_LOP::getObjectType() const
{
    return OBJ_GEOMETRY;
}

OP_Node *
OBJ_LOP::Create(OP_Network *net, const char *name, OP_Operator *op)
{
    return new OBJ_LOP(net, name, op);
}

void
OBJ_LOP::LOPPATH(UT_String &str)
{
    evalString(str, theLopPathName.getToken(),
	&getIndirect()[I_LOP_LOPPATH], 0, 0.0f);
}

void
OBJ_LOP::PRIMPATH(UT_String &str)
{
    evalString(str, lopPrimPathName.getToken(),
	&getIndirect()[I_LOP_PRIMPATH], 0, 0.0f);
}

void
OBJ_LOP::XFORMTYPE(UT_String &str)
{
    evalString(str, theXformTypeName.getToken(),
	&getIndirect()[I_LOP_XFORMTYPE], 0, 0.0f);
}

OP_ERROR
OBJ_LOP::cookMyObj(OP_Context &context)
{
    LOP_Node		*lop = nullptr;
    UT_String		 loppath;
    UT_String		 primpath;
    UT_String		 xformtype;
    UT_Matrix4D		 l(1.0), w(1.0);
    UT_DMatrix4		 this_parent_xform(1.0);
    OP_AutoLockInputs	 auto_lock_inputs(this);
    HUSD_TimeSampling	 time_sampling = HUSD_TimeSampling::NONE;

    if (auto_lock_inputs.lock(context) >= UT_ERROR_ABORT)
	return error();

    LOPPATH(loppath);
    PRIMPATH(primpath);
    XFORMTYPE(xformtype);
    if (loppath.isstring() && primpath.isstring())
    {
	lop = findLOPNode(loppath);
	if( lop )
	{
	    HUSD_DataHandle	 datahandle = lop->getCookedDataHandle(context);
	    HUSD_AutoReadLock	 readlock(datahandle);
	    XUSD_ConstDataPtr	 data(readlock.data());

	    addExtraInput(lop, OP_INTEREST_DATA);
	    if (!data || !data->isStageValid())
	    {
		appendError("LOP", LOP_FAILED_TO_COOK,
		    loppath.c_str(), UT_ERROR_ABORT);
		return UT_ERROR_ABORT;
	    }

	    SdfPath		 sdfpath(HUSDgetSdfPath(primpath));
	    UsdPrim		 prim(data->stage()->GetPrimAtPath(sdfpath));
	    if (!prim)
	    {
		appendError("LOP", LOP_PRIM_NOT_FOUND,
		    primpath.c_str(), UT_ERROR_ABORT);
		return UT_ERROR_ABORT;
	    }

	    UsdGeomImageable	 imageable(prim);
	    if (!imageable)
	    {
		appendError("LOP", LOP_PRIM_NO_XFORM,
		    primpath.c_str(), UT_ERROR_ABORT);
		return UT_ERROR_ABORT;
	    }

	    UsdTimeCode		 timecode(HUSDgetCurrentUsdTimeCode());
	    UsdGeomXformCache	 xformcache(timecode);
	    GfMatrix4d		 gfl(1.0);
	    bool		 resets = false;

	    if (xformtype ==
		theXformTypeChoices[OBJ_LOP_XFORMTYPE_LOCALTOWORLD].getToken())
	    {
		gfl = xformcache.GetLocalToWorldTransform(prim);
		time_sampling = HUSDgetWorldTransformTimeSampling(prim);
	    }
	    else if (xformtype ==
		theXformTypeChoices[OBJ_LOP_XFORMTYPE_PARENTTOWORLD].getToken())
	    {
		UsdPrim		 parent = prim.GetParent();
		gfl = xformcache.GetLocalToWorldTransform(parent);
		time_sampling = HUSDgetWorldTransformTimeSampling(parent);
	    }
	    else if (xformtype ==
		theXformTypeChoices[OBJ_LOP_XFORMTYPE_LOCAL].getToken())
	    {
		gfl = xformcache.GetLocalTransformation(prim, &resets);
		time_sampling = HUSDgetLocalTransformTimeSampling(prim);
	    }

	    l = GusdUT_Gf::Cast(gfl);
	}
	else
	    addWarning(OBJ_ERR_CANT_FIND_OBJ, (const char *)loppath);

	// include the parent of this (fetching) object in the transform chain
	if (!getParentToWorldTransform(context, this_parent_xform))
	{
	    addTransformError(*this, "parent");
	    return UT_ERROR_ABORT;
	}
	w = l;
	w *= this_parent_xform;
    }

    // If the stage data is time varying, even if the LOP node is not, this
    // object is time dependent;
    if (HUSDisTimeVarying(time_sampling))
    {
	appendError("LOP", LOP_TIMEDEP_ANIMATED_STAGE,
	    nullptr, UT_ERROR_MESSAGE);
	flags().setTimeDep(true);
    }

    setLocalXform(l);
    setWorldXform(w);

    return error();
}

PXR_NAMESPACE_CLOSE_SCOPE

