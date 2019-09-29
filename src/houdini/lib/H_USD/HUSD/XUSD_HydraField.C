/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
 *
 * Produced by:
 *	Side Effects Software Inc
 *	123 Front Street West, Suite 1401
 *	Toronto, Ontario
 *	Canada   M5J 2M2
 *	416-504-9876
 *
 * NAME:	XUSD_HydraField.h (HUSD Library, C++)
 *
 * COMMENTS:	Container for a hydra light prim (HdRprim)
 */
#include "XUSD_HydraField.h"
#include "XUSD_SceneGraphDelegate.h"
#include "XUSD_HydraUtils.h"
#include "XUSD_Tokens.h"
#include "XUSD_Utils.h"

#include "HUSD_HydraField.h"
#include "HUSD_Scene.h"

#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdVol/tokens.h>
#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/imaging/hd/dirtyList.h>
#include <pxr/imaging/hd/enums.h>
#include <pxr/imaging/hd/light.h>
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/base/vt/value.h>
#include <gusd/UT_Gf.h>

#include <UT/UT_Debug.h>

using namespace UT::Literal;

PXR_NAMESPACE_OPEN_SCOPE

XUSD_HydraField::XUSD_HydraField(TfToken const& typeId,
				 SdfPath const& primId,
				 HUSD_HydraField &field)
    : HdField(primId),
      myFieldType(typeId.GetText()),
      myField(field),
      myDirtyFlag(true)
{
}

XUSD_HydraField::~XUSD_HydraField()
{
}
   
GT_PrimitiveHandle
XUSD_HydraField::getGTPrimitive() const
{
    return myField.getGTPrimitive();
}

const UT_StringHolder &
XUSD_HydraField::getFieldType() const
{
    return myFieldType;
}

void
XUSD_HydraField::dirtyVolumes(HdSceneDelegate *sceneDelegate)
{
    HdChangeTracker &change_tracker =
	sceneDelegate->GetRenderIndex().GetChangeTracker();
    const UT_StringSet &volumes =
	myField.scene().volumesUsingField(GetId().GetString());
    for (auto &&volumepath : volumes)
	change_tracker.MarkRprimDirty(HUSDgetSdfPath(volumepath),
	    HdChangeTracker::DirtyTopology);
}

void
XUSD_HydraField::Sync(HdSceneDelegate *sceneDelegate,
                      HdRenderParam *renderParam,
                      HdDirtyBits *dirtyBits)
{
    if (!TF_VERIFY(sceneDelegate))
        return;

    SdfPath const &id = GetId();

    // Change tracking
    HdDirtyBits bits = *dirtyBits;

    if (bits & DirtyTransform)
	myField.Transform(XUSD_HydraUtils::fullTransform(sceneDelegate, id));

    if (bits & DirtyParams)
    {
	SdfAssetPath	 filePath;
	TfToken		 fieldName;
	int		 fieldIndex = 0;

	// Get other attributes from the USD prim through the scene delegate.
	// Then store the resulting values on this object.
	XUSD_HydraUtils::evalAttrib(
	    filePath, sceneDelegate, id, UsdVolTokens->filePath);
	myField.FilePath(filePath.GetResolvedPath());
	if (!myField.FilePath().isstring())
	    myField.FilePath(filePath.GetAssetPath());
	XUSD_HydraUtils::evalAttrib(
	    fieldName, sceneDelegate, id, UsdVolTokens->fieldName);
	myField.FieldName(fieldName.GetText());

	// Only Houdini Field Assets have a field index. VDB fields do not.
	if (myFieldType == HusdHdPrimTypeTokens()->
		bprimHoudiniFieldAsset.GetString())
	{
	    XUSD_HydraUtils::evalAttrib(
		fieldIndex, sceneDelegate, id, UsdVolTokens->fieldIndex);
	    myField.FieldIndex(fieldIndex);
	}

	dirtyVolumes(sceneDelegate);
    }

    if(bits)
    {
	myDirtyFlag = true;
	myField.bumpVersion();
    }

    *dirtyBits = Clean;
}
    
HdDirtyBits
XUSD_HydraField::GetInitialDirtyBitsMask() const
{
    return AllDirty;
}


PXR_NAMESPACE_CLOSE_SCOPE
