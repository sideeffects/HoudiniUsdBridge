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
 * NAME:        OBJ_LOPCamera.C (Custom Library, C++)
 *
 * COMMENTS:    An object to fetch it's transform from another object.
 */

#include "OBJ_LOPCamera.h"
#include <OBJ/OBJ_Shared.h>
#include <OBJ/OBJ_SharedNames.h>
#include <LOP/LOP_Node.h>
#include <LOP/LOP_Error.h>
#include <LOP/LOP_PRMShared.h>
#include <HUSD/HUSD_DataHandle.h>
#include <HUSD/HUSD_FindPrims.h>
#include <HUSD/HUSD_TimeCode.h>
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

int *OBJ_LOPCamera::fetchIndirect = 0;

void
OBJ_LOPCamera::Register(OP_OperatorTable* table)
{
    OP_Operator* op =
	new OP_Operator("lopimportcam",
			"LOP Import Camera",
			Create,
			OBJ_LOPCamera::getTemplateList(),
			OBJ_LOPCamera::theChildTableName,
			/* min inputs */ 0,
			/* max inputs */ 1,
			/* variables  */ 0,
			OP_FLAG_GENERATOR);
    op->setIconName("OBJ_lopimportcam");
    table->addOperator(op);
}

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
OBJ_LOPCamera::getTemplateList()
{
    // The parm templates here are not created as a static list because
    // if that static list was built before the OBJbaseTemplate static list
    // (which it references) then that list would be corrupt.  Thus we have
    // to force our static list to be created after OBJbaseTemplate.
    static PRM_Template	    *theTemplate = 0;
    static PRM_Template	     OBJlopTemplate[] = {
	PRM_Template(PRM_STRING, PRM_TYPE_DYNAMIC_PATH, 1, &lopPathName,
		     /*default*/ 0, /*choicelist*/ 0, /*range*/ 0,
		     /*callback*/ 0, &PRM_SpareData::lopPath),
	PRM_Template(PRM_STRING, 1, &lopPrimPathName, 0,
		     0, 0, 0, &lopPrimPathDialogSpareData),
	PRM_Template(PRM_STRING, 1, &theXformTypeName,
		     &theXformTypeDefault, &theXformTypeMenu),
    };

    if (!theTemplate)
    {
	int	    i = 0;

	// We need space for I_N_LOPCAMERA_INDICES and one for the list
        // terminator
	theTemplate = new PRM_Template[I_N_LOPCAMERA_INDICES+1];

	// Fetch parms (7)
	SET_TPLATE_EX(OBJlopTemplate, I_LOPCAMERA_LOPPATH - I_N_CAM_INDICES)
	SET_TPLATE_EX(OBJlopTemplate, I_LOPCAMERA_PRIMPATH - I_N_CAM_INDICES)
	SET_TPLATE_EX(OBJlopTemplate, I_LOPCAMERA_XFORMTYPE - I_N_CAM_INDICES)
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

        UT_ASSERT(i == I_N_LOPCAMERA_INDICES);
	theTemplate[i++] = PRM_Template();
    }
    return theTemplate;
}

OBJ_LOPCamera::OBJ_LOPCamera(OP_Network *net, const char *name, OP_Operator *op)
        : OBJ_Camera(net, name, op)
{
    if (!fetchIndirect)
        fetchIndirect = allocIndirect(I_N_LOPCAMERA_INDICES);
} 

OBJ_LOPCamera::~OBJ_LOPCamera()
{
}

OBJ_OBJECT_TYPE
OBJ_LOPCamera::getObjectType() const
{
    return OBJ_CAMERA;
}

OP_Node *
OBJ_LOPCamera::Create(OP_Network *net, const char *name, OP_Operator *op)
{
    return new OBJ_LOPCamera(net, name, op);
}

void
OBJ_LOPCamera::LOPPATH(UT_String &str)
{
    evalString(str, lopPathName.getToken(),
	&getIndirect()[I_LOPCAMERA_LOPPATH], 0, 0.0f);
}

void
OBJ_LOPCamera::PRIMPATH(UT_String &str)
{
    evalString(str, lopPrimPathName.getToken(),
	&getIndirect()[I_LOPCAMERA_PRIMPATH], 0, 0.0f);
}

void
OBJ_LOPCamera::XFORMTYPE(UT_String &str)
{
    evalString(str, theXformTypeName.getToken(),
	&getIndirect()[I_LOPCAMERA_XFORMTYPE], 0, 0.0f);
}

OP_ERROR
OBJ_LOPCamera::cookMyObj(OP_Context &context)
{
    LOP_Node		*lop = nullptr;
    UT_String		 loppath;
    UT_String		 primpattern;
    UT_String		 xformtype;
    UT_Matrix4D		 l(1.0), w(1.0);
    UT_DMatrix4		 this_parent_xform(1.0);
    OP_AutoLockInputs	 auto_lock_inputs(this);
    HUSD_TimeSampling	 time_sampling = HUSD_TimeSampling::NONE;

    if (auto_lock_inputs.lock(context) >= UT_ERROR_ABORT)
	return error();

    LOPPATH(loppath);
    PRIMPATH(primpattern);
    XFORMTYPE(xformtype);
    if (loppath.isstring() && primpattern.isstring())
    {
	lop = getLOPNode(loppath);
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

            // Allow using a primitive pattern to specify the camera, but with
            // a warning if multiple primitives match the pattern.
            auto demands = HUSD_PrimTraversalDemands(
                    HUSD_TRAVERSAL_DEFAULT_DEMANDS
                    | HUSD_TRAVERSAL_ALLOW_INSTANCE_PROXIES);
            HUSD_FindPrims findprims(readlock, demands);
            if (!findprims.addPattern(
                        primpattern, lop->getUniqueId(),
                        HUSD_TimeCode(context.getTime(), HUSD_TimeCode::TIME)))
            {
                appendError(
                        LOP_OPTYPE_NAME, LOP_COLLECTION_FAILED_TO_CALCULATE,
                        findprims.getLastError().c_str(), UT_ERROR_ABORT);
                return error();
            }

            const HUSD_PathSet &primpaths = findprims.getExpandedPathSet();
            if (primpaths.empty())
            {
                appendError(
                        "LOP", LOP_MESSAGE,
                        "Primitive pattern did not match any primitives",
                        UT_ERROR_ABORT);
                return error();
            }

            HUSD_Path primpath = *primpaths.begin();
            if (primpaths.size() > 1)
            {
                UT_WorkBuffer msg;
                msg.format(
                        "Primitive pattern matched multiple primitives. Using "
                        "'{}'",
                        primpath.pathStr());
                appendError("LOP", LOP_MESSAGE, msg.buffer(), UT_ERROR_WARNING);
            }

	    UsdPrim prim(data->stage()->GetPrimAtPath(primpath.sdfPath()));
	    if (!prim)
	    {
		appendError("LOP", LOP_PRIM_NOT_FOUND,
		    primpath.pathStr().c_str(), UT_ERROR_ABORT);
		return UT_ERROR_ABORT;
	    }

	    UsdGeomImageable	 imageable(prim);
	    if (!imageable)
	    {
		appendError("LOP", LOP_PRIM_NO_XFORM,
		    primpath.pathStr().c_str(), UT_ERROR_ABORT);
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

