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
 *      Side Effects Software Inc.
 *      123 Front Street West, Suite 1401
 *      Toronto, Ontario
 *      Canada   M5J 2M2
 *      416-504-9876
 *
 */

#include "BRAY_HdField.h"
#include "BRAY_HdParam.h"
#include "BRAY_HdUtil.h"
#include "BRAY_HdFormat.h"
#include "BRAY_HdTokens.h"

#include <GT/GT_Primitive.h>
#include <GT/GT_PrimVDB.h>
#include <GT/GT_PrimVolume.h>
#include <pxr/imaging/hd/changeTracker.h>
#include <pxr/imaging/hd/rprim.h>
#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/sdf/fileFormat.h>
#include <pxr/usd/usdVol/tokens.h>
#include <FS/UT_DSO.h>
#include <UT/UT_ErrorLog.h>

PXR_NAMESPACE_OPEN_SCOPE

namespace
{
static UT_Lock	    theLock;
static UT_Lock	    theGdpReadLock;

struct DsoLoader
{
    using vdb_func = GT_Primitive *(*)(const char *, const char *, int);
    using hou_func = GT_Primitive *(*)(const char *, const char *, int);
    DsoLoader()
    {
        UT_DSO  dso;
        myVDBProc = dso.findProcedure("USD_SopVol" FS_DSO_EXTENSION,
                            "SOPgetVDBVolumePrimitiveWithIndex", myVDBPath);
        myHoudiniProc = dso.findProcedure("USD_SopVol" FS_DSO_EXTENSION,
                            "SOPgetHoudiniVolumePrimitive", myHoudiniPath);
        UT_ASSERT(myVDBProc && myHoudiniProc);
    }
    vdb_func    vdb() const { return reinterpret_cast<vdb_func>(myVDBProc); }
    hou_func    hou() const { return reinterpret_cast<hou_func>(myHoudiniProc); }

    GT_Primitive  *vdb(const char *filename, const char *name, int idx) const
    {
        if (!myVDBProc)
            return nullptr;
        return vdb()(filename, name, idx);
    }
    GT_Primitive  *hou(const char *filename, const char *name, int idx) const
    {
        if (!myHoudiniProc)
            return nullptr;
        return hou()(filename, name, idx);
    }

    void                *myVDBProc;
    void                *myHoudiniProc;
    UT_StringHolder      myHoudiniPath;
    UT_StringHolder      myVDBPath;
};

static const DsoLoader &
houLoader()
{
    static DsoLoader    theLoader;
    return theLoader;
}

}//ns

BRAY_HdField::BRAY_HdField(const TfToken& typeId, const SdfPath& primId)
    : HdField(primId)
    , myFieldType(typeId)
    , myFieldIdx(-1)
{
}

// public methods
void
BRAY_HdField::Sync(HdSceneDelegate *sceneDelegate,
        HdRenderParam *renderParam,
        HdDirtyBits *dirtyBits)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    if (!TF_VERIFY(sceneDelegate))
	return;

    const SdfPath& id = GetId();
    auto&& rparm = UTverify_cast<BRAY_HdParam*>(renderParam);
    auto&& scene = rparm->getSceneForEdit();

    // check if we have a transform on our field
    if (*dirtyBits & DirtyTransform)
    {
	// Field's are BPrims and hence don't have an instancer
	// associated with them. Hence pass a empty SdfPath for instancer
	BRAY_HdUtil::xformBlur(sceneDelegate, *rparm, id,
		myXfm, scene.objectProperties());
#if 0
	for(auto& xfm : myXfm)
	    UTdebugFormat("{} : dirty xfm : {}", id, xfm);
#endif
    }

    if (*dirtyBits & DirtyParams)
    {
	SdfAssetPath	filePath;
	TfToken		fieldName;
	int		fieldIdx;

	BRAY_HdUtil::eval(filePath, sceneDelegate, id, UsdVolTokens->filePath);
	myFilePath = filePath.GetResolvedPath();
	if (!myFilePath.isstring())
	    myFilePath = filePath.GetAssetPath();
	BRAY_HdUtil::eval(fieldName, sceneDelegate, id, UsdVolTokens->fieldName);
	myFieldName = BRAY_HdUtil::toStr(fieldName);
        BRAY_HdUtil::eval(fieldIdx, sceneDelegate, id, UsdVolTokens->fieldIndex);
        myFieldIdx = fieldIdx;

#if 0
	UTdebugFormat(
	    R"({} : dirtyParams for field :
		    filepath  : {}
		    fieldname : {})",
	    id, myFilePath, myFieldName);
#endif

	updateGTPrimitive();
    }

    // tag all volume RPrims that have this field as dirty so that
    // they can appropriately update their internal data.
    dirtyVolumes(sceneDelegate);

    // cleanup after yourself.
    *dirtyBits = Clean;
}

void
BRAY_HdField::Finalize(HdRenderParam *renderParam)
{
}

bool
BRAY_HdField::registerVolume(const UT_StringHolder& volume)
{
    // Can be called from multiple threads at the same time
    UT_Lock::Scope  lock(theLock);
    int prevsize = myVolumes.size();
    myVolumes.insert(volume);
    return myVolumes.size() != prevsize;
}

// private methods
// Update the underlying stored
void
BRAY_HdField::updateGTPrimitive()
{
    // Make sure that we our field type is something that we support
    // if not return immediately
    if (!((myFieldType == BRAYHdTokens->bprimHoudiniFieldAsset) ||
	  (myFieldType == BRAYHdTokens->openvdbAsset)))
    {
	return;
    }

    GT_Primitive        *gt = nullptr;
    if (myFieldType == BRAYHdTokens->bprimHoudiniFieldAsset)
        gt = houLoader().hou(myFilePath, myFieldName, myFieldIdx);
    else if (myFieldType == BRAYHdTokens->openvdbAsset)
        gt = houLoader().vdb(myFilePath, myFieldName, myFieldIdx);
    UT_ASSERT(gt);
    myField.reset(gt);
}

void
BRAY_HdField::dirtyVolumes(HdSceneDelegate* sceneDelegate)
{
    // go through the list of stored volumes and mark them dirty
    // NOTE: we mark the RPrim as having 'DirtyTopology' so that it can
    // pull all the details of all its fields.
    auto&& changeTracker = sceneDelegate->GetRenderIndex().GetChangeTracker();
    for(auto& vol : myVolumes)
	changeTracker.MarkRprimDirty(BRAY_HdUtil::toSdf(vol),
	    HdChangeTracker::DirtyVolumeField);
}

PXR_NAMESPACE_CLOSE_SCOPE
