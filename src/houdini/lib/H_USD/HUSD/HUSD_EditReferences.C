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
 *	Side Effects Software Inc.
 *	123 Front Street West, Suite 1401
 *	Toronto, Ontario
 *      Canada   M5J 2M2
 *	416-504-9876
 *
 */

#include "HUSD_EditReferences.h"
#include "HUSD_Constants.h"
#include "HUSD_FindPrims.h"
#include "HUSD_Path.h"
#include "HUSD_PathSet.h"
#include "XUSD_Data.h"
#include "XUSD_Utils.h"
#include "XUSD_LockedGeoRegistry.h"
#include <CH/CH_Manager.h>
#include <SYS/SYS_ParseNumber.h>
#include <pxr/usd/sdf/fileFormat.h>
#include <pxr/usd/usd/specializes.h>
#include <pxr/usd/usd/references.h>
#include <pxr/usd/usd/inherits.h>
#include <pxr/usd/usd/payloads.h>
#include <pxr/usd/usd/common.h>
#include <pxr/usd/usd/stage.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace {
    SdfPrimSpecHandle
    getOrCreatePrimSpec(const UsdStageRefPtr &stage,
            const SdfLayerHandle &layer,
            const SdfPath &sdfpath,
            const UT_StringHolder &primkind,
            const UT_StringHolder &parenttype,
            bool define_parent_prims)
    {
	UT_StringHolder	     parent_alias = HUSDgetPrimTypeAlias(parenttype);
	std::string	     parent_primtype = parenttype.isstring()
                                ? parent_alias.toStdString()
                                : std::string();
	SdfPrimSpecHandle    primspec;

        primspec = HUSDcreatePrimInLayer(stage, layer,
            sdfpath, TfToken(primkind.toStdString()),
            SdfSpecifierOver,
            define_parent_prims ? SdfSpecifierDef : SdfSpecifierOver,
            parent_primtype);

        return primspec;
    }
}

HUSD_EditReferences::HUSD_EditReferences(HUSD_AutoWriteLock &lock)
    : myWriteLock(lock),
      myRefType(HUSD_Constants::getReferenceTypeFile()),
      myRefEditOp(HUSD_Constants::getEditOpAppendFront()),
      myParentPrimType(HUSD_Constants::getXformPrimType())
{
}

HUSD_EditReferences::~HUSD_EditReferences()
{
}

bool
HUSD_EditReferences::addReference(const HUSD_FindPrims &findprims,
	const UT_StringRef &reffilepath,
	const UT_StringRef &refprimpath,
	const HUSD_LayerOffset &offset,
	const UT_StringMap<UT_StringHolder> &refargs,
	const GU_DetailHandle &gdh) const
{
    auto		 outdata = myWriteLock.data();
    UsdListPosition	 editop(HUSDgetUsdListPosition(myRefEditOp));
    bool		 success = false;

    // Make sure we have been given non-empty information where it is needed.
    if (myRefType == HUSD_Constants::getReferenceTypeFile() ||
	myRefType == HUSD_Constants::getReferenceTypePayload())
    {
	if (!reffilepath.isstring())
	    return false;
    }
    else
    {
	if (!refprimpath.isstring())
	    return false;
    }

    if (outdata && outdata->isStageValid())
    {
        auto             stage = outdata->stage();

        for (auto &&path : findprims.getExpandedOrMissingExplicitPathSet())
        {
            SdfPath          sdfpath(path.sdfPath());
            auto             primspec = getOrCreatePrimSpec(stage,
                                outdata->activeLayer(), sdfpath, myPrimKind,
                                myParentPrimType, true);
            auto             prim = stage->GetPrimAtPath(sdfpath);

            if (prim)
            {
                SdfFileFormat::FileFormatArguments args;
                SdfPath bestrefprimpath;
                UsdStageRefPtr stage;

                if (!prim.IsDefined())
                    prim.SetSpecifier(SdfSpecifierDef);
                HUSDconvertToFileFormatArguments(refargs, args);

                SdfLayerRefPtr layer;
                if (gdh.isValid())
                {
                    myWriteLock.data()->addLockedGeo(XUSD_LockedGeoRegistry::
                        createLockedGeo(reffilepath, args, gdh));

                    std::string layer_path = SdfLayer::CreateIdentifier(
                            reffilepath.toStdString(), args);
                    // Also keep the locked geos for any unpacked volumes (see
                    // HUSD_EditLayers::addLayerForEdit()).
                    layer = SdfLayer::FindOrOpen(layer_path);
                    if (layer)
                        HUSDaddVolumeLockedGeos(*outdata, layer);
                }

                HUSDaddPrimEditorNodeId(primspec,
                    myWriteLock.dataHandle().nodeId());
                bestrefprimpath = HUSDgetBestRefPrimPath(
                    reffilepath, args, refprimpath, stage);
                if (myRefType ==
                    HUSD_Constants::getReferenceTypeFile())
                {
                    auto refs = prim.GetReferences();
                    SdfReference sdfreference(
                            SdfLayer::CreateIdentifier(
                                reffilepath.toStdString(), args),
                            bestrefprimpath,
                            HUSDgetSdfLayerOffset(offset));

                    success = refs.AddReference(sdfreference, editop);
                }
                else if (myRefType ==
                         HUSD_Constants::getReferenceTypePrim())
                {
                    auto refs = prim.GetReferences();
                    SdfReference sdfreference(
                            std::string(),
                            bestrefprimpath,
                            HUSDgetSdfLayerOffset(offset));

                    success = refs.AddReference(sdfreference, editop);
                }
                else if (myRefType ==
                         HUSD_Constants::getReferenceTypePayload())
                {
                    auto payloads = prim.GetPayloads();
                    SdfPayload sdfpayload(
                            SdfLayer::CreateIdentifier(
                                reffilepath.toStdString(), args),
                            bestrefprimpath,
                            HUSDgetSdfLayerOffset(offset));

                    success = payloads.AddPayload(sdfpayload, editop);
                }
                else if (myRefType ==
                         HUSD_Constants::getReferenceTypeInherit())
                {
                    auto inherits = prim.GetInherits();

                    success = inherits.AddInherit(
                        HUSDgetSdfPath(refprimpath),
                        editop);
                }
                else if (myRefType ==
                         HUSD_Constants::getReferenceTypeSpecialize())
                {
                    auto specializes = prim.GetSpecializes();

                    success = specializes.AddSpecialize(
                        HUSDgetSdfPath(refprimpath),
                        editop);
                }
            }
        }
    }

    return success;
}

bool
HUSD_EditReferences::addReference(const UT_StringRef &primpath,
        const UT_StringRef &reffilepath,
        const UT_StringRef &refprimpath,
        const HUSD_LayerOffset &offset,
        const UT_StringMap<UT_StringHolder> &refargs,
        const GU_DetailHandle &gdh) const
{
    HUSD_FindPrims findprims(myWriteLock);
    HUSD_PathSet pathset( { HUSD_Path(primpath) } );

    findprims.setTrackMissingExplicitPrimitives(true);
    findprims.setWarnMissingExplicitPrimitives(false);
    findprims.addPaths(pathset);
    return addReference(findprims,
        reffilepath,
        refprimpath,
        offset,
        refargs,
        gdh);
}

bool
HUSD_EditReferences::removeReference(const HUSD_FindPrims &findprims,
	const UT_StringRef &reffilepath,
	const UT_StringRef &refprimpath,
	const HUSD_LayerOffset &offset,
	const UT_StringMap<UT_StringHolder> &refargs,
        bool define_parent_prims) const
{
    auto		 outdata = myWriteLock.data();
    bool		 success = false;

    // Make sure we have been given non-empty information where it is needed.
    if (myRefType == HUSD_Constants::getReferenceTypeFile() ||
	myRefType == HUSD_Constants::getReferenceTypePayload())
    {
	if (!reffilepath.isstring())
	    return false;
    }
    else
    {
	if (!refprimpath.isstring())
	    return false;
    }

    if (outdata && outdata->isStageValid())
    {
        auto		 stage = outdata->stage();

        for (auto &&path : findprims.getExpandedOrMissingExplicitPathSet())
        {
            SdfPath          sdfpath(path.sdfPath());
            auto             primspec = getOrCreatePrimSpec(stage,
                                outdata->activeLayer(), sdfpath, myPrimKind,
                                myParentPrimType, define_parent_prims);
            auto             prim = stage->GetPrimAtPath(sdfpath);

            if (prim)
            {
                SdfPath bestrefprimpath;
                SdfFileFormat::FileFormatArguments args;
                HUSDconvertToFileFormatArguments(refargs, args);

                bestrefprimpath = HUSDgetBestRefPrimPath(
                    reffilepath, args, refprimpath, stage);

                if (myRefType ==
                    HUSD_Constants::getReferenceTypeFile())
                {
                    auto refs = prim.GetReferences();
                    SdfReference sdfreference(
                            SdfLayer::CreateIdentifier(
                                reffilepath.toStdString(), args),
                            bestrefprimpath,
                            HUSDgetSdfLayerOffset(offset));

                    success = refs.RemoveReference(sdfreference);
                }
                else if (myRefType ==
                         HUSD_Constants::getReferenceTypePrim())
                {
                    auto refs = prim.GetReferences();
                    SdfReference sdfreference(
                            std::string(),
                            bestrefprimpath,
                            HUSDgetSdfLayerOffset(offset));

                    success = refs.RemoveReference(sdfreference);
                }
                else if (myRefType ==
                         HUSD_Constants::getReferenceTypePayload())
                {
                    auto payloads = prim.GetPayloads();
                    SdfPayload sdfpayload(
                            SdfLayer::CreateIdentifier(
                                reffilepath.toStdString(), args),
                            bestrefprimpath,
                            HUSDgetSdfLayerOffset(offset));

                    success = payloads.RemovePayload(sdfpayload);
                }
                else if (myRefType ==
                         HUSD_Constants::getReferenceTypeInherit())
                {
                    auto inherits = prim.GetInherits();

                    success = inherits.RemoveInherit(
                        HUSDgetSdfPath(refprimpath));
                }
                else if (myRefType ==
                         HUSD_Constants::getReferenceTypeSpecialize())
                {
                    auto specializes = prim.GetSpecializes();

                    success = specializes.RemoveSpecialize(
                        HUSDgetSdfPath(refprimpath));
                }
            }
        }
    }

    return success;
}

bool
HUSD_EditReferences::removeReference(const UT_StringRef &primpath,
        const UT_StringRef &reffilepath,
        const UT_StringRef &refprimpath,
        const HUSD_LayerOffset &offset,
        const UT_StringMap<UT_StringHolder> &refargs,
        bool define_parent_prims) const
{
    HUSD_FindPrims findprims(myWriteLock);
    HUSD_PathSet pathset( { HUSD_Path(primpath) } );

    findprims.setTrackMissingExplicitPrimitives(true);
    findprims.setWarnMissingExplicitPrimitives(false);
    findprims.addPaths(pathset);
    return removeReference(findprims,
        reffilepath,
        refprimpath,
        offset,
        refargs,
        define_parent_prims);
}

bool
HUSD_EditReferences::clearLayerReferenceEdits(const HUSD_FindPrims &findprims,
        bool define_parent_prims)
{
    auto		 outdata = myWriteLock.data();
    bool		 success = false;

    if (outdata && outdata->isStageValid())
    {
        auto		 stage = outdata->stage();

        for (auto &&path : findprims.getExpandedOrMissingExplicitPathSet())
        {
            SdfPath          sdfpath(path.sdfPath());
            auto             primspec = getOrCreatePrimSpec(stage,
                                outdata->activeLayer(), sdfpath, myPrimKind,
                                myParentPrimType, define_parent_prims);
            auto             prim = stage->GetPrimAtPath(sdfpath);

            if (prim)
            {
                if (myRefType == HUSD_Constants::getReferenceTypeFile() ||
                    myRefType == HUSD_Constants::getReferenceTypePrim())
                {
                    auto refs = prim.GetReferences();

                    success = refs.ClearReferences();
                }
                else if (myRefType ==
                         HUSD_Constants::getReferenceTypePayload())
                {
                    auto payloads = prim.GetPayloads();

                    success = payloads.ClearPayloads();
                }
                else if (myRefType ==
                         HUSD_Constants::getReferenceTypeInherit())
                {
                    auto inherits = prim.GetInherits();

                    success = inherits.ClearInherits();
                }
                else if (myRefType ==
                         HUSD_Constants::getReferenceTypeSpecialize())
                {
                    auto specializes = prim.GetSpecializes();

                    success = specializes.ClearSpecializes();
                }
            }
        }
    }

    return success;
}

bool
HUSD_EditReferences::clearLayerReferenceEdits(const UT_StringRef &primpath,
        bool define_parent_prims)
{
    HUSD_FindPrims findprims(myWriteLock);
    HUSD_PathSet pathset( { HUSD_Path(primpath) } );

    findprims.setTrackMissingExplicitPrimitives(true);
    findprims.setWarnMissingExplicitPrimitives(false);
    findprims.addPaths(pathset);
    return clearLayerReferenceEdits(findprims, define_parent_prims);
}

bool
HUSD_EditReferences::clearReferences(const HUSD_FindPrims &findprims,
        bool define_parent_prims)
{
    auto		 outdata = myWriteLock.data();
    bool		 success = false;

    if (outdata && outdata->isStageValid())
    {
        auto		 stage = outdata->stage();

        for (auto &&path : findprims.getExpandedOrMissingExplicitPathSet())
        {
            SdfPath          sdfpath(path.sdfPath());
            auto             primspec = getOrCreatePrimSpec(stage,
                                outdata->activeLayer(), sdfpath, myPrimKind,
                                myParentPrimType, define_parent_prims);
            auto             prim = stage->GetPrimAtPath(sdfpath);

            if (prim)
            {
                // There seems to be a bug in that setting a list editable
                // value to an empty list if there is not already an edit
                // operation in the current layer does nothing. So we have to
                // set a non-empty array of value, then set it to empty. Put
                // this in an SdfChangeBlock so that the stage isn't recomposed
                // while the invalid empty reference is on the prim.
                SdfChangeBlock changeblock;

                if (myRefType == HUSD_Constants::getReferenceTypeFile() ||
                    myRefType == HUSD_Constants::getReferenceTypePrim())
                {
                    auto refs = prim.GetReferences();

                    success = refs.SetReferences(
                        SdfReferenceVector({SdfReference()}));
                    success = refs.SetReferences(
                        SdfReferenceVector());
                }
                else if (myRefType ==
                         HUSD_Constants::getReferenceTypePayload())
                {
                    auto payloads = prim.GetPayloads();

                    success = payloads.SetPayloads(
                        SdfPayloadVector({SdfPayload()}));
                    success = payloads.SetPayloads(
                        SdfPayloadVector());
                }
                else if (myRefType ==
                         HUSD_Constants::getReferenceTypeInherit())
                {
                    auto inherits = prim.GetInherits();

                    success = inherits.SetInherits(
                        SdfPathVector({sdfpath}));
                    success = inherits.SetInherits(
                        SdfPathVector());
                }
                else if (myRefType ==
                         HUSD_Constants::getReferenceTypeSpecialize())
                {
                    auto specializes = prim.GetSpecializes();

                    success = specializes.SetSpecializes(
                        SdfPathVector({sdfpath}));
                    success = specializes.SetSpecializes(
                        SdfPathVector());
                }
            }
        }
    }

    return success;
}

bool
HUSD_EditReferences::clearReferences(const UT_StringRef &primpath,
        bool define_parent_prims)
{
    HUSD_FindPrims findprims(myWriteLock);
    HUSD_PathSet pathset( { HUSD_Path(primpath) } );

    findprims.setTrackMissingExplicitPrimitives(true);
    findprims.setWarnMissingExplicitPrimitives(false);
    findprims.addPaths(pathset);
    return clearReferences(findprims, define_parent_prims);
}
