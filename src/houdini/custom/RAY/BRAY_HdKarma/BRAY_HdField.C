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

#include <GT/GT_Primitive.h>
#include <GT/GT_PrimVDB.h>
#include <GT/GT_PrimVolume.h>
#include <GU/GU_Detail.h>
#include <HUSD/XUSD_Format.h>
#include <HUSD/XUSD_HydraUtils.h>
#include <HUSD/XUSD_TicketRegistry.h>
#include <HUSD/XUSD_Tokens.h>
#include <OP/OP_Node.h>
#include <pxr/imaging/hd/changeTracker.h>
#include <pxr/imaging/hd/rprim.h>
#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/sdf/fileFormat.h>
#include <pxr/usd/usdVol/tokens.h>
#include <HUSD/XUSD_Utils.h>
#include <UT/UT_ErrorLog.h>

PXR_NAMESPACE_OPEN_SCOPE

namespace
{
    static UT_Lock	    theLock;
}

BRAY_HdField::BRAY_HdField(const TfToken& typeId, const SdfPath& primId)
    : HdField(primId)
    , myFieldType(typeId)
    , myFieldIdx(-1)
{
}

// public methods
void
BRAY_HdField::Sync(HdSceneDelegate* sceneDelegate, HdRenderParam* renderParam,
    HdDirtyBits* dirtyBits)
{
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

	XUSD_HydraUtils::evalAttrib(
	    filePath, sceneDelegate, id, UsdVolTokens->filePath);
	myFilePath = filePath.GetResolvedPath();
	if (!myFilePath.isstring())
	    myFilePath = filePath.GetAssetPath();
	XUSD_HydraUtils::evalAttrib(
	    fieldName, sceneDelegate, id, UsdVolTokens->fieldName);
	myFieldName = BRAY_HdUtil::toStr(fieldName);

#if 0
	UTdebugFormat(
	    R"({} : dirtyParams for field :
		    filepath  : {}
		    fieldname : {})",
	    id, myFilePath, myFieldName);
#endif

	if (myFieldType == HusdHdPrimTypeTokens()->
	    bprimHoudiniFieldAsset)
	{
	    XUSD_HydraUtils::evalAttrib(
		fieldIdx, sceneDelegate, id, UsdVolTokens->fieldIndex);
	    myFieldIdx = fieldIdx;
	}

	updateGTPrimitive();
    }
    
    // tag all volume RPrims that have this field as dirty so that 
    // they can appropriately update their internal data.
    dirtyVolumes(sceneDelegate);

    // cleanup after yourself.
    *dirtyBits = Clean;
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
    if (!((myFieldType == HusdHdPrimTypeTokens()->bprimHoudiniFieldAsset) ||
	  (myFieldType == HusdHdPrimTypeTokens()->openvdbAsset)))
	return;

    // Attempt at creating the underlying field
    SdfFileFormat::FileFormatArguments	args;
    std::string				path;
    GU_DetailHandle			gdh;

    if (myFilePath.startsWith(OPREF_PREFIX))
    {
	SdfLayer::SplitIdentifier(myFilePath.toStdString(), &path, &args);
	gdh = XUSD_TicketRegistry::getGeometry(path, args);
    }
    else
    {
	GU_Detail *gdp = new GU_Detail();
	if (gdp->load(myFilePath))
	    gdh.allocateAndSet(gdp);
	else
	    UT_ErrorLog::error("Cannot open file: {}", myFilePath);
    }

    if (gdh)
    {
	GU_DetailHandleAutoReadLock lock(gdh);
	const GU_Detail *gdp = lock.getGdp();

	if (gdp)
	{
	    const GA_Primitive *gaprim = nullptr;
	    const GEO_Primitive *geoprim = nullptr;
	    GA_Offset field_offset = GA_INVALID_OFFSET;

	    if (myFieldName.isstring())
	    {
		GA_ROHandleS nameattrib(gdp, GA_ATTRIB_PRIMITIVE, "name");

		if (nameattrib.isValid())
		{
		    GA_StringIndexType nameindex;

		    nameindex = nameattrib->lookupHandle(myFieldName);
		    if (nameindex != GA_INVALID_STRING_INDEX)
		    {
			for (GA_Iterator it(gdp->getPrimitiveRange());
			    !it.atEnd(); ++it)
			{
			    // Check for any prim with our field name
			    if (nameattrib->getStringIndex(*it) == nameindex)
			    {
				auto&& tid = gdp->getPrimitive(*it)->
				    getTypeId().get();
				if (tid == GA_PRIMVOLUME ||
				    tid == GA_PRIMVDB)
				{
				    field_offset = it.getOffset();
				    break;
				}
			    }
			}
		    }
		}
	    }

	    // If we didn't find the primitive we are looking for
	    // the field name, look at the field index (for native volumes)
	    if (field_offset == GA_INVALID_OFFSET &&
		myFieldType == HusdHdPrimTypeTokens()-> bprimHoudiniFieldAsset)
	    {
		field_offset = gdp->primitiveOffset(GA_Index(myFieldIdx));
	    }

	    if (field_offset != GA_INVALID_OFFSET)
	    {
		gaprim = gdp->getPrimitive(field_offset);
		geoprim = static_cast<const GEO_Primitive*>(gaprim);
	    }

	    if (geoprim)
	    {
		auto&& tid = geoprim->getTypeId().get();
		if (tid == GEO_PRIMVDB)
		    myField = new GT_PrimVDB(gdh, geoprim);
		else if (tid == GEO_PRIMVOLUME)
		    myField = new GT_PrimVolume(gdh, geoprim,
			GT_DataArrayHandle());
	    }
	}
    }
}

void
BRAY_HdField::dirtyVolumes(HdSceneDelegate* sceneDelegate)
{
    // go through the list of stored volumes and mark them dirty
    // NOTE: we mark the RPrim as having 'DirtyTopology' so that it can 
    // pull all the details of all its fields.
    auto&& changeTracker = sceneDelegate->GetRenderIndex().GetChangeTracker();
    for(auto& vol : myVolumes)
	changeTracker.MarkRprimDirty(HUSDgetSdfPath(vol),
	    HdChangeTracker::DirtyTopology);
}

PXR_NAMESPACE_CLOSE_SCOPE
