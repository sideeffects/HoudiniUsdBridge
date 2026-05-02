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

// Simple GU_Detail cache to prevent multiple file opens on a volume containing
// multiple fields
class DetailCache
{
public:
    struct Item
    {
        Item()
        : myCounter(0)
        {
        }
        GU_DetailHandle  myDetail;
        // A dedicated counter for the field prims referencing the file. Don't
        // use the refcount in detail handle because it's referenced by the
        // primitives which are also referenced by BRAY, so we wouldn't know if
        // we need to let go until the next render.
        int              myCounter;
    };

    GU_ConstDetailHandle get(const UT_StringHolder &filename)
    {
        // NOTE that this prevents concurrent loads, though that's fine (for
        // now) since Bprims aren't synced concurrently anyways.
        UT_Lock::Scope lock(myLock);
        Item &item = myMap[filename];
        item.myCounter++;
        if (!item.myDetail)
        {
            item.myDetail.allocateAndSet(new GU_Detail());
            if (!item.myDetail.gdpNC()->load(filename.c_str()))
                item.myDetail.deleteGdp();
        }
        return item.myDetail;
    }

    void release(const UT_StringHolder &filename)
    {
        UT_Lock::Scope lock(myLock);
        UT_Map<UT_StringHolder, Item>::iterator it = myMap.find(filename);
        if (it != myMap.end())
        {
            Item &item = it->second;
            item.myCounter--;
            if (!item.myCounter)
                myMap.erase(filename);
        }
    }

    UT_Map<UT_StringHolder, Item>   myMap;
    UT_Lock myLock;
};

static DetailCache &
detailCache()
{
    static DetailCache theCache;
    return theCache;
}

// Pull specific field from detail
template <bool NATIVE>
GT_PrimitiveHandle
getVolumePrimitiveFromDetail(
    GU_ConstDetailHandle &gdh,
    const UT_StringRef &fieldname,
    int fieldindex)
{
    if (gdh)
    {
        GU_DetailHandleAutoReadLock	 lock(gdh);
        const GU_Detail *gdp = lock.getGdp();

        if (gdp)
        {
            const GEO_Primitive	*geoprim = nullptr;
            GA_Offset field_offset = GA_INVALID_OFFSET;

            // For Houdini volumes, the field index is the primary identifier,
            // and has no need to use the name.
            if (field_offset == GA_INVALID_OFFSET && NATIVE &&
                fieldindex < gdp->getNumPrimitives())
            {
                field_offset = gdp->primitiveOffset(GA_Index(fieldindex));
            }

            if (field_offset == GA_INVALID_OFFSET && fieldname.isstring())
            {
                // Look for VDB volumes by default, Houdini volumes if
                // the fieldtype indicates a Houdini volume.
                GA_PrimCompat::TypeMask primtype;
                if (NATIVE)
                    primtype = GEO_PrimTypeCompat::GEOPRIMVOLUME;
                else
                    primtype = GEO_PrimTypeCompat::GEOPRIMVDB;

                // For Houdini volumes, always use the first name match (the
                // field index, if it exists, is a prim number, not a match
                // number). For other volume types the field index is the
                // match number.
                int matchnumber = 0;
                if (!NATIVE)
                    matchnumber = (fieldindex >= 0 ? fieldindex : 0);

                const GEO_Primitive *prim = gdp->findPrimitiveByName(
                    fieldname, primtype, "name", matchnumber);
                if (prim)
                    field_offset = prim->getMapOffset();
            }

            if (field_offset != GA_INVALID_OFFSET)
                geoprim = gdp->getGEOPrimitive(field_offset);

            if (geoprim && geoprim->getTypeId().get() == GEO_PRIMVDB)
            {
                return UTmakeIntrusive<GT_PrimVDB>(gdh, geoprim);
            }
            else if (geoprim && geoprim->getTypeId().get() == GEO_PRIMVOLUME)
            {
                return UTmakeIntrusive<GT_PrimVolume>(gdh, geoprim,
                    GT_DataArrayHandle());
            }
        }
    }

    return nullptr;
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
        detailCache().release(myFilePath);

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
    dirtyVolumes(sceneDelegate, renderParam);

    // cleanup after yourself.
    *dirtyBits = Clean;
}

void
BRAY_HdField::Finalize(HdRenderParam *renderParam)
{
    detailCache().release(myFilePath);

    // Do NOT clear theFieldToVolumes entry: clearing it defeats the purpose of
    // it being a persistent map, which is to remember which volumes to dirty
    // in the event of the field being finalized and anewed (which can happen,
    // for instance, with Volume LOP with animated parameters where the field
    // gets replaced without recreating the volume).
}

bool
BRAY_HdField::registerVolume(const UT_StringHolder& volume,
    HdRenderParam *renderparam)
{
    return UTverify_cast<BRAY_HdParam*>(renderparam)->registerFieldToVolume(
        BRAY_HdUtil::toStr(GetId()), volume);
}

// private methods
// Update the underlying stored
void
BRAY_HdField::updateGTPrimitive()
{
    // Make sure that we our field type is something that we support
    // if not return immediately
    if (!((myFieldType == BRAYHdTokens->houdiniFieldAsset) ||
	  (myFieldType == BRAYHdTokens->openvdbAsset)))
    {
	return;
    }

    SdfFileFormat::FileFormatArguments	 args;
    std::string				 path;
    SdfLayer::SplitIdentifier(myFilePath.toStdString(), &path, &args);

    // ".volumes" from HUSD_Constants::getVolumeSopSuffix()
    if (myFilePath.startsWith("op:")
        || myFilePath.startsWith("hda:")
        || UT_StringRef(path).endsWith(".volumes"))
    {
        // load from SOP or packed disk
        GT_Primitive        *gt = nullptr;
        if (myFieldType == BRAYHdTokens->houdiniFieldAsset)
            gt = houLoader().hou(myFilePath, myFieldName, myFieldIdx);
        else if (myFieldType == BRAYHdTokens->openvdbAsset)
            gt = houLoader().vdb(myFilePath, myFieldName, myFieldIdx);

        UT_ASSERT(gt);
        myField.reset(gt);
    }
    else
    {
        // load from file
        GU_ConstDetailHandle gdh = detailCache().get(myFilePath);
        if (myFieldType == BRAYHdTokens->houdiniFieldAsset)
        {
            myField = getVolumePrimitiveFromDetail<true>(gdh, myFieldName,
                myFieldIdx);
        }
        else if (myFieldType == BRAYHdTokens->openvdbAsset)
        {
            myField = getVolumePrimitiveFromDetail<false>(gdh, myFieldName,
                myFieldIdx);
        }
    }
}

void
BRAY_HdField::dirtyVolumes(HdSceneDelegate* sceneDelegate,
    HdRenderParam *renderparam)
{
    // go through the list of stored volumes and mark them dirty
    auto&& changeTracker = sceneDelegate->GetRenderIndex().GetChangeTracker();
    UT_StringSet volumes =
        UTverify_cast<BRAY_HdParam*>(renderparam)->getVolumes(
        BRAY_HdUtil::toStr(GetId()));
    for(auto& vol : volumes)
    {
	changeTracker.MarkRprimDirty(BRAY_HdUtil::toSdf(vol),
	    HdChangeTracker::DirtyVolumeField);
    }
}

PXR_NAMESPACE_CLOSE_SCOPE
