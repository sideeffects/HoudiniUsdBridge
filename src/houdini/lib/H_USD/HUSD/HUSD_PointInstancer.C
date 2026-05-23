/*
* Copyright 2024 Side Effects Software Inc.
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
*       Canada   M5J 2M2
*	416-504-9876
*
*/

#include "HUSD_PointInstancer.h"

#include "HUSD_Constants.h"
#include "HUSD_ErrorScope.h"
#include "HUSD_FindPrims.h"
#include "HUSD_GetAttributes.h"
#include "HUSD_Info.h"
#include "HUSD_SetAttributes.h"
#include "XUSD_AttributeUtils.h"
#include "XUSD_Data.h"
#include "XUSD_Utils.h"

#include <GA/GA_ATIStringArray.h>
#include <GA/GA_AttributeInstanceMatrix.h>
#include <GA/GA_Handle.h>
#include <GA/GA_Names.h>
#include <GA/GA_SplittableRange.h>
#include <GA/GA_Types.h>
#include <GU/GU_PackedGeometry.h>
#include <GU/GU_PrimPacked.h>
#include <UT/UT_Algorithm.h>
#include <UT/UT_Lock.h>
#include <UT/UT_RWLock.h>
#include <UT/UT_Set.h>
#include <UT/UT_StringHolder.h>
#include <UT/UT_UniquePtr.h>
#include <UT/UT_VarEncode.h>
#include <UT/UT_VectorTypes.h>

#include <gusd/UT_Gf.h>

#include <pxr/base/gf/matrix2f.h>
#include <pxr/base/gf/matrix3f.h>
#include <pxr/base/gf/matrix4f.h>
#include <pxr/base/vt/value.h>
#include <pxr/usd/sdf/types.h>
#include <pxr/usd/sdf/valueTypeName.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usdGeom/bboxCache.h>
#include <pxr/usd/usdGeom/pointBased.h>
#include <pxr/usd/usdGeom/pointInstancer.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdGeom/xformable.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace
{

constexpr UT_StringLit theDeleteAttributeName("usddelete");
constexpr UT_StringLit theDeleteAttributeValue("delete");
constexpr UT_StringLit theUsdVisibilityAttributeName("usdvisibility");
constexpr UT_StringLit theInvisibleName("invisible");
constexpr UT_StringLit theInvalidPrimPath("__theInvalidPrimPath");
constexpr UT_StringLit theImportedIdsName("importedids");
constexpr UT_StringLit theImportedPrimvarsName("importedprimvars");

const UT_Vector3F theDefaultScale{1.0, 1.0, 1.0};
const GA_Defaults theScaleDefault(theDefaultScale.data(), 3);

class husd_UsdWriteQueueBase
{
public:
    virtual                 ~husd_UsdWriteQueueBase() = default;
    virtual bool             write(HUSD_SetAttributes &setattrs) const = 0;
};

template <typename UtType>
class husd_SetAttributeQueue final : public husd_UsdWriteQueueBase
{
public:
    UT_StringHolder          myPrimPath;
    UT_StringHolder          myAttrName;
    UT_Array<UtType>         myValues;
    HUSD_TimeCode            myTimeCode;
    UT_StringHolder          myValueType;

                             husd_SetAttributeQueue(
                                 const UT_StringRef &primpath,
                                 const UT_StringRef &attrname,
                                 const HUSD_TimeCode &timecode,
                                 const UT_StringRef &valuetype,
                                 UT_Array<UtType> &&values)
                                 : myPrimPath(primpath),
                                   myAttrName(attrname),
                                   myValues(std::move(values)),
                                   myTimeCode(timecode),
                                   myValueType(valuetype)
                             {}

    bool                     write(HUSD_SetAttributes &setattrs) const override
                             {
                                 return setattrs.setAttribute(myPrimPath,
                                                              myAttrName,
                                                              myValues,
                                                              myTimeCode,
                                                              myValueType);
                             }
};

template <typename UtType>
class husd_SetPrimvarQueue final : public husd_UsdWriteQueueBase
{
public:
    UT_StringHolder          myPrimPath;
    UT_StringHolder          myPrimvarName;
    UT_StringHolder          myInterpolation;
    UT_Array<UtType>         myValues;
    HUSD_TimeCode            myTimeCode;
    UT_StringHolder          myValueType;

                             husd_SetPrimvarQueue(
                                 const UT_StringRef &primpath,
                                 const UT_StringRef &primvarname,
                                 const UT_StringRef &interpolation,
                                 const HUSD_TimeCode &timecode,
                                 const UT_StringRef &valuetype,
                                 UT_Array<UtType> &&values)
                                 : myPrimPath(primpath),
                                   myPrimvarName(primvarname),
                                   myInterpolation(interpolation),
                                   myValues(std::move(values)),
                                   myTimeCode(timecode),
                                   myValueType(valuetype)
                             {}

    bool                     write(HUSD_SetAttributes &setattrs) const override
                             {
                                 return setattrs.setPrimvar(myPrimPath,
                                                            myPrimvarName,
                                                            myInterpolation,
                                                            myValues,
                                                            myTimeCode,
                                                            myValueType);
                             }
};

class husd_UsdWriteQueue
{
public:
                            // append() is thread safe
    void                     append(UT_UniquePtr<husd_UsdWriteQueueBase> entry)
                             {
                                 UT_Lock::Scope scope(myLock);
                                 myWrites.append(std::move(entry));
                             }

    template <typename UtType>
    void                     appendAttribute(
                                 const UT_StringRef &primpath,
                                 const UT_StringRef &attrname,
                                 const HUSD_TimeCode &timecode,
                                 const UT_StringRef &valuetype,
                                 UT_Array<UtType> &&values)
                             {
                                 append(UTmakeUnique<
                                     husd_SetAttributeQueue<UtType>>(
                                     primpath, attrname, timecode,
                                     valuetype, std::move(values)));
                             }

    template <typename UtType>
    void                     appendPrimvar(
                                 const UT_StringRef &primpath,
                                 const UT_StringRef &primvarname,
                                 const UT_StringRef &interpolation,
                                 const HUSD_TimeCode &timecode,
                                 const UT_StringRef &valuetype,
                                 UT_Array<UtType> &&values)
                             {
                                 append(UTmakeUnique<
                                     husd_SetPrimvarQueue<UtType>>(
                                     primpath, primvarname, interpolation,
                                     timecode, valuetype,
                                     std::move(values)));
                             }

                             // writeAll is NOT thread safe
    void                     writeAll(HUSD_SetAttributes &setattrs)
                             {
                                 for (const auto &entry : myWrites)
                                     entry->write(setattrs);
                                 myWrites.clear();
                             }

private:
    UT_Array<UT_UniquePtr<husd_UsdWriteQueueBase>> myWrites;
    UT_Lock                                      myLock;
};

class HUSDpointInstancerOffsetMap
{
public:
    class OffsetMap
    {
    public:
        UT_Array<exint>   myDeletedIds;
        UT_Array<exint>   myForceDeletedIndicesMap;
        UT_Array<exint>   myMissingIndicesMap;
        exint             myMaxIdx;

        UT_StringArray    myImportedPrimvars;

        UT_Array<exint>   myNewIds;
        UT_Array<exint>   myImportedIds;
        UT_StringRef      myPrimPath;
        UT_Array<exint>   myOffsetToIdMap;
        UT_Array<exint>   myOffsetToIdxMap;
        UT_Array<exint>   myUsdIdToIdxMap;
        UT_Array<exint>   myUsdIds;
        UT_Array<exint>   mySopIds;
        GA_PointGroupUPtr myGroup;
        GA_ROHandleI      myIdHandle;
        GA_ROHandleS      myDeleteHandle;
        exint             myNextIdx = 0;
        exint             myOffsetIdx = 0;
        bool              myUseIds = false;

        exint             myOriginalNumInstances = 0;

        HUSD_PointInstancerCopyStyle myCopyStyle;

        mutable exint     myMaxId = std::numeric_limits<exint>::min();

        OffsetMap()
        {}

        OffsetMap(const GU_Detail *gdp,
                  HUSD_AutoWriteLock &writelock,
                  const UT_StringRef &primPath,
                  const HUSD_TimeCode &timecode,
                  const HUSD_PointInstancerCopyStyle &copystyle,
                  const GA_Offset &maxoffset = GA_Offset((std::numeric_limits<exint>::max)()),
                  const exint &maxid = (std::numeric_limits<exint>::min)())
        : myPrimPath(primPath), myCopyStyle(copystyle)
        {
            HUSD_Info          info(writelock);
            HUSD_GetAttributes getattrs(writelock);

            const GA_ROHandleDict idmapattr = gdp->findDictTuple(
                                                  GA_ATTRIB_DETAIL,
                                                  theImportedIdsName.asRef());

            const GA_ROHandleDict primvarmapattr = gdp->findDictTuple(
                                                     GA_ATTRIB_DETAIL,
                                                     theImportedPrimvarsName.asRef());

            myOffsetToIdMap.setSize(maxoffset+1);
            myOffsetToIdxMap.setSize(maxoffset+1);
            mySopIds.setCapacity(gdp->getPointRange().getEntries());
            myGroup = gdp->createDetachedPointGroup();
            myIdHandle = gdp->findIntTuple(GA_ATTRIB_POINT, GA_Names::id);
            myDeleteHandle = gdp->findStringTuple(GA_ATTRIB_POINT, theDeleteAttributeName.asRef());
            myOriginalNumInstances = info.getPointInstancerInstanceCount(primPath, timecode);

            getattrs.getAttributeArray(primPath,
                                       HUSD_Constants::getAttributePointIds(),
                                       myUsdIds, timecode);

            if (myUsdIds.isEmpty() || !myIdHandle.isValid())
            {
                myUseIds = false;
            }
            else
            {
                // both usd ids exist and a sop ids attr, so an id map needs to be
                // built and used.
                myUseIds = true;
            }

            if (myUseIds)
            {
                exint min = (std::numeric_limits<exint>::max)();
                exint max = maxid;
                UTgetArrayMinMax(myUsdIds.begin(), myUsdIds.end(), min, max);
                myUsdIdToIdxMap.appendMultiple(-1, max+1);
                myMaxIdx = -1;
                for (exint id : myUsdIds)
                    myUsdIdToIdxMap[id] = ++myMaxIdx;
            }

            if (idmapattr.isValid())
            {
                const UT_OptionsHolder idmapholder = idmapattr.get(GA_Offset(0));
                const UT_Int64Array &importedids = idmapholder->getOptionIArray(primPath);
                myImportedIds = importedids;
                exint min = (std::numeric_limits<exint>::max)();
                exint max = (std::numeric_limits<exint>::min)();
                UTgetArrayMinMax(importedids.begin(), importedids.end(), min, max);
                if (max >= 0)
                {
                    myMissingIndicesMap.appendMultiple(-1, max+1);
                    // start by recording all importedids as deletedids (1).  As
                    // offsets are added, ids will be removed from the deletedids
                    // list (set to -1)
                    for (exint id : importedids)
                    {
                        if (myUsdIdToIdxMap.isEmpty())
                            myMissingIndicesMap[id] = 1;
                        else
                            myMissingIndicesMap[myUsdIdToIdxMap[id]] = 1;
                    }
                }
            }

            if (primvarmapattr.isValid())
            {
                const UT_OptionsHolder primvarmapholder = primvarmapattr.get(GA_Offset(0));
                // TODO: is all the copying necessary....
                for (UT_StringHolder pv : primvarmapholder->getOptionSArray(primPath))
                    myImportedPrimvars.append(pv);
            }
        }

        void addOffset(GA_Offset ptoff)
        {
            myOffsetToIdxMap[ptoff] = myOffsetIdx++;
            myGroup->addOffset(ptoff);
            exint &id = myOffsetToIdMap[ptoff];
            exint idx = -1;

            if (myUseIds)
            {
                id = myIdHandle.get(ptoff);
                if (id < 0)
                {
                    if (myMaxId == (std::numeric_limits<exint>::min)())
                        myMaxId = -1;

                    id = ++myMaxId;
                    if (id >= myUsdIdToIdxMap.size())
                        myUsdIdToIdxMap.appendMultiple(-1, id+1);
                    myUsdIdToIdxMap[id] = ++myMaxIdx;
                    myNewIds.append(id);
                    idx = myMaxIdx;
                }
                if (id >= myUsdIdToIdxMap.size())
                {
                    // new id
                    myUsdIdToIdxMap.appendMultiple(-1, (id-myUsdIdToIdxMap.size()+1));
                    myUsdIdToIdxMap[id] = ++myMaxIdx;
                    myNewIds.append(id);
                    idx = myMaxIdx;
                }
                else
                {
                    // // potentially existing id
                    // if (id >= myUsdIdToIdxMap.size())
                    // {
                    //     myNewIds.append(id);
                    //     myUsdIdToIdxMap[id] = myMaxIdx++;
                    // }
                    // else
                    {
                        idx = myUsdIdToIdxMap[id];
                        if (idx >= 0 && idx < myMissingIndicesMap.size() &&
                            myMissingIndicesMap[idx] == 1)
                        {
                            // this id exists and so the idx was not deleted.
                            myMissingIndicesMap[idx] = -1;
                        }
                        else
                        {
                            // not an imported id, and since we're using ids,
                            // this must be a new id.
                            myNewIds.append(id);
                            myUsdIdToIdxMap[id] = ++myMaxIdx;
                        }
                    }
                }
            }
            else
            {
                // no usd ids / sop id matching to worry about
                // ie id is idx
                idx = myNextIdx++;
                id = idx; // need to enuse id is set
                if (idx < myMissingIndicesMap.size() &&
                    myMissingIndicesMap[idx] == 1)
                {
                    // this was imported, and exists so wasn't deleted
                    myMissingIndicesMap[idx] = -1;
                }
                else
                {
                    // we've run out of imported indices, so this must be new
                    myNewIds.append(idx);
                }
            }
            mySopIds.append(id);
            if (idx >= 0)
            {
                if (myDeleteHandle.isValid())
                {
                    if (myDeleteHandle.get(ptoff) == theDeleteAttributeValue)
                    {
                        if (idx >= myForceDeletedIndicesMap.size())
                        {
                            myForceDeletedIndicesMap.appendMultiple(-1, idx+1);
                        }
                        myForceDeletedIndicesMap[idx] = 1;
                    }
                }
            }
        }

        exint getIdx(GA_Offset ptoff) const
        {
            if (myCopyStyle == HUSD_PointInstancerCopyStyle::Overwrite)
                return myOffsetToIdxMap[ptoff];

            if (myUsdIdToIdxMap.isEmpty())
                return myOffsetToIdMap[ptoff];

            return myUsdIdToIdxMap[ myOffsetToIdMap[ptoff] ];
        }

        exint getId(GA_Offset ptoff) const
        {
            return myOffsetToIdMap[ptoff];
        }

        bool isMissing(exint idx) const
        {
            if (idx >= myMissingIndicesMap.size())
                return false;
            return myMissingIndicesMap[idx] == 1;
        }

        bool isDeleted(exint idx) const
        {
            if (idx < 0 || idx >= myForceDeletedIndicesMap.size())
                return false;
            return myForceDeletedIndicesMap[idx] == 1;
        }

        exint getMaxId() const
        {
            if (myMaxId == (std::numeric_limits<exint>::min)()) {
                exint min = (std::numeric_limits<exint>::max)();
                UTgetArrayMinMax(myUsdIds.begin(), myUsdIds.end(), min, myMaxId);
                UTgetArrayMinMax(mySopIds.begin(), mySopIds.end(), min, myMaxId);
                UTgetArrayMinMax(myDeletedIds.begin(), myDeletedIds.end(), min, myMaxId);
                UTgetArrayMinMax(myNewIds.begin(), myNewIds.end(), min, myMaxId);
                UTgetArrayMinMax(myImportedIds.begin(), myImportedIds.end(), min, myMaxId);
                if (myMaxId < 0)
                    myMaxId = -1;
            }
            return myMaxId;
        }

        void updateDeletedIds()
        {
            myDeletedIds.setCapacity(myMissingIndicesMap.size());
            for (exint idx = 0, end = myMissingIndicesMap.size();
                 idx < end; ++idx)
            {
                if (myMissingIndicesMap[idx] == 1)
                {
                    // idx was deleted
                    exint id;
                    if (idx < 0 || idx >= myUsdIds.size())
                        id = idx;
                    else
                        id = myUsdIds[idx];
                    myDeletedIds.append(id);
                }
            }
            myDeletedIds.shrinkToFit();
        }

        const UT_Array<exint> &getNewIds() const
        {
            return myNewIds;
        }
    };

    UT_StringMap<OffsetMap> myOffsetMap;

    HUSDpointInstancerOffsetMap(const GU_Detail *gdp,
                                const GA_Range  &range,
                                HUSD_AutoWriteLock &readlock,
                                HUSD_TimeCode timecode,
                                HUSD_PointInstancerCopyStyle copystyle,
                                const UT_StringRef &fallbackprimpath,
                                const UT_StringSet &createdprimpaths)
    {
        GA_ROHandleS pathhandle = gdp->findStringTuple(GA_ATTRIB_POINT,
                                                       GA_Names::path);
        GA_ROHandleI idhandle   = gdp->findIntTuple(GA_ATTRIB_POINT,
                                                    GA_Names::id);
        UT_StringRef primpath = fallbackprimpath;

        if (idhandle.isValid())
        {
            // need to find max id for all points to use as container size
            // for all maps.
            exint     maxid(std::numeric_limits<exint>::min());
            GA_Offset maxoffset(std::numeric_limits<exint>::min());
            exint     id;
            {
                for (GA_Offset ptoff : range)
                {
                    id = idhandle.get(ptoff);
                    maxid = id > maxid ? id : maxid;
                    maxoffset = ptoff > maxoffset ? ptoff : maxoffset;
                }
            }

            UT_StringRef lastprimpath = theInvalidPrimPath.asRef();
            primpath = fallbackprimpath;
            for (const GA_Offset &ptoff : range)
            {
                if (pathhandle.isValid())
                {
                    primpath = pathhandle.get(ptoff);
                    if (primpath.isEmpty())
                        primpath = fallbackprimpath;
                }
                if (primpath.isEmpty())
                    continue;

                if (primpath != lastprimpath)
                {
                    if (!myOffsetMap.contains(primpath))
                    {
                        HUSD_PointInstancerCopyStyle style = copystyle;
                        if (createdprimpaths.contains(primpath))
                            style = HUSD_PointInstancerCopyStyle::Overwrite;

                        myOffsetMap[primpath] = OffsetMap(gdp, readlock, primpath, timecode, style, maxoffset, maxid);
                    }
                }
                myOffsetMap[primpath].addOffset(ptoff);
                lastprimpath = primpath;
            }
        }
        else
        {
            // no id attribute in SOPs, so we need to map from ptoff -> array index directly,
            // which means addOffset can not be threaded.
            GA_Offset maxoffset(std::numeric_limits<exint>::min());
            {
                for (GA_Offset ptoff : range)
                {
                    maxoffset = ptoff > maxoffset ? ptoff : maxoffset;
                }
            }

            UT_StringRef lastprimpath = UT_StringHolder::theEmptyString;
            primpath = fallbackprimpath;
            for (const GA_Offset &ptoff : range)
            {
                if (pathhandle.isValid())
                {
                    primpath = pathhandle.get(ptoff);
                    if (primpath.isEmpty())
                        primpath = fallbackprimpath;
                }

                if (primpath.isEmpty())
                    continue;

                if (primpath != lastprimpath)
                {
                    if (!myOffsetMap.contains(primpath))
                    {
                        HUSD_PointInstancerCopyStyle style = copystyle;
                        if (createdprimpaths.contains(primpath))
                            style = HUSD_PointInstancerCopyStyle::Overwrite;

                        myOffsetMap[primpath] = OffsetMap(gdp, readlock, primpath, timecode, style, maxoffset);
                    }
                }
                myOffsetMap[primpath].addOffset(ptoff);
                lastprimpath = primpath;
            }
        }

        const GA_ROHandleDict idmapattr = gdp->findDictTuple(
                                                  GA_ATTRIB_DETAIL,
                                                  theImportedIdsName.asRef());
        if (idmapattr.isValid())
        {
            const UT_Options &idmapholder = *idmapattr.get(GA_Offset(0)).options();
            for (auto mapentry = idmapholder.begin();
                      mapentry != idmapholder.end();
                    ++mapentry )
            {
                if (!myOffsetMap.contains(mapentry.name()))
                    myOffsetMap[mapentry.name()] = OffsetMap(gdp, readlock, mapentry.name(),
                                                      timecode, copystyle);
            }
        }

        for (auto &map : myOffsetMap)
            map.second.updateDeletedIds();

    }

    UT_StringMap<OffsetMap>::iterator
    begin()
    {
        return myOffsetMap.begin();
    }

    UT_StringMap<OffsetMap>::const_iterator
    begin() const
    {
        return myOffsetMap.begin();
    }

    UT_StringMap<OffsetMap>::iterator
    end()
    {
        return myOffsetMap.end();
    }

    UT_StringMap<OffsetMap>::const_iterator
    end() const
    {
        return myOffsetMap.end();
    }
};

class IdToIdxMap
{
public:
    IdToIdxMap(const UT_StringRef &primpath,
               HUSD_AutoReadLock &readlock,
               const HUSD_TimeCode &timecode,
               const GU_Detail *gdp,
               const GA_Range &range,
               const UT_Array<exint> &importedids)
    {
        HUSD_GetAttributes getattrs(readlock);
        UT_Array<exint>    usd_ids;
        getattrs.getAttributeArray(primpath,
                                   HUSD_Constants::getAttributePointIds(),
                                   usd_ids,
                                   timecode);
        if (!usd_ids.isEmpty())
        {
            UTgetArrayMinMax(usd_ids.begin(), usd_ids.end(), myMinId, myMaxId);
            myIdMap.appendMultiple(-1, myMaxId + 1);
            for (exint i = 0; i < usd_ids.size(); ++i)
                myIdMap[usd_ids[i]] = i;
        }

        GA_Offset maxptoff(std::numeric_limits<exint>::min());
        for (const GA_Offset &ptoff : range)
            maxptoff = ptoff > maxptoff ? ptoff : maxptoff;
        myOffsetIdxMap.setSize(maxptoff+1);

        // if importedids is empty, then we are importing every instance
        if (importedids.isEmpty())
        {
            exint idx = -1;
            for (const GA_Offset &ptoff : range)
                myOffsetIdxMap[ptoff] = ++idx;
        }
        else
        {
            exint idx = -1;
            exint id;
            for (const GA_Offset &ptoff : range)
            {
                id = importedids[++idx];
                myOffsetIdxMap[ptoff] = getIdx(id);
            }
        }
    }

    exint getIdxFromOffset(const GA_Offset &ptoff) const
    {
        return myOffsetIdxMap[ptoff];
    }


    exint getIdx(const exint &id) const
    {
        if (myIdMap.isEmpty())
            return id;
        return myIdMap[id];
    }

    void getIdxs(const UT_Array<exint>& ids, UT_Array<exint> &indices) const
    {
        indices.setSize(ids.size());
        if (!myIdMap.isEmpty())
            for (exint i = 0; i < ids.size(); ++i)
                indices[i] = myIdMap[ids[i]];
        else
            indices = ids;
    }

private:
    UT_Array<exint> myIdMap;
    UT_Array<exint> myOffsetIdxMap;
    exint           myMinId = (std::numeric_limits<exint>::max)();
    exint           myMaxId = 0;
};

template <class UtType>
void husdMakeIndexed(UT_Array<UtType> &values, UT_Array<exint> &indices)
{
    indices.setCapacity(values.size());
    exint valuessize = 0;

    bool newvalue;
    while (valuessize < values.size())
    {
        newvalue = true;
        // Check the first valuessize elements for a match
        for (exint idx = 0; idx < valuessize; ++idx)
        {
            if (values[idx] == values[valuessize])
            {
                // we've found a duplicate entry, record idx and remove value
                // at valuesize
                indices.append(idx);
                values.removeIndex(valuessize);
                newvalue = false;
                break;
            }
        }
        if (newvalue)
        {
            // this is a new value.  keep it (by incrementing valuessize)
            // and record the index.
            indices.append(valuessize++);
        }
    }
    values.shrinkToFit();
}

void bboxConvert(const GfBBox3d &gfbox, UT_BoundingBoxD &utbox)
{
    GfRange3d range = gfbox.ComputeAlignedBox();
    GfVec3d   min = range.GetMin();
    GfVec3d   max = range.GetMax();
    utbox.setBounds(min[0], min[1], min[2],
                    max[0], max[1], max[2] );
}

float husdRandom01(exint id, fpreal protoseed)
{
    uint seed = SYSwang_inthash64(id);
    seed = SYSreal_hashseed(protoseed, seed);
    float rand = SYSfastRandom(seed);
    return rand;
}

int husdRandomProto(exint id, fpreal protoseed, exint numprotos)
{
    float rand = husdRandom01(id, protoseed);
    int inst_index = int(rand * numprotos);
    if (inst_index == numprotos)
        inst_index = numprotos-1;
    return inst_index;
}

UT_StringRef
husdGetSopAttrName(const UT_StringRef &usdAttrName)
{
    if (usdAttrName.equal(HUSD_Constants::getPrimvarsDisplayColor()))
        return GA_Names::Cd;

    if (usdAttrName.equal(HUSD_Constants::getPrimvarsDisplayOpacity()))
        return GA_Names::Alpha;

    if (usdAttrName.equal(HUSD_Constants::getAttributePointOrientations()) ||
        usdAttrName.equal(HUSD_Constants::getAttributePointOrientationsF()))
        return GA_Names::orient;

    if (usdAttrName.equal(HUSD_Constants::getAttributePointScales()))
        return GA_Names::scale;

    if (usdAttrName.equal(HUSD_Constants::getAttributePointAccelerations()))
        return GA_Names::accel;

    if (usdAttrName.equal(HUSD_Constants::getAttributePointVelocities()))
        return GA_Names::v;

    if (usdAttrName.equal(HUSD_Constants::getAttributePointAngularVelocities()))
        return GA_Names::w;

    // If this is a primvar, we want to strip the 'primvars:' prefix.
    UT_StringView primvarname(usdAttrName);
    if (primvarname.startsWith(HUSD_Constants::getPrimvarsPrefix()))
        primvarname = primvarname.substr(9);

    return UT_VarEncode::encodeVar(primvarname);
}

UT_StringRef
husdGetPrimvarName(const UT_StringRef &sopattrname)
{
    if (sopattrname.equal(GA_Names::Cd))
        return HUSD_Constants::getPrimvarsDisplayColor();

    if (sopattrname.equal(GA_Names::Alpha))
        return HUSD_Constants::getPrimvarsDisplayOpacity();

    if (sopattrname.equal(GA_Names::orient))
        return HUSD_Constants::getAttributePointOrientations();

    if (sopattrname.equal(GA_Names::scale))
        return HUSD_Constants::getAttributePointScales();

    if (sopattrname.equal(GA_Names::accel))
        return HUSD_Constants::getAttributePointAccelerations();

    if (sopattrname.equal(GA_Names::v))
        return HUSD_Constants::getAttributePointVelocities();

    if (sopattrname.equal(GA_Names::w))
        return HUSD_Constants::getAttributePointAngularVelocities();

    return UT_VarEncode::encodeVar(sopattrname);
}

template <class UTTYPE, GA_Storage SOPSTORAGE, int TUPLESIZE>
bool _doCopyUsdPrimvarToSopPointAttr(GU_Detail *gdp,
                                     const GA_Range &range,
                                     const VtValue &value,
                                     const UT_StringHolder primvarname,
                                     const GA_TypeInfo &type_info,
                                     const IdToIdxMap &idToIdxMap)
{
    UT_Array<UTTYPE> utvalue;
    HUSDgetValue(value, utvalue);
    if (utvalue.size() == 0)
        return false;

    UT_StringRef sopattrname = husdGetSopAttrName(primvarname);

    GA_RWHandleT<UTTYPE> sopattr = gdp->findPointAttribute(GA_SCOPE_PUBLIC,
                                                           sopattrname);
    if (sopattr.isInvalid())
        return false;
    sopattr->setTypeInfo(type_info);

    UTparallelFor(
        GA_SplittableRange(range),
        [&](const GA_SplittableRange &split_range)
    {
        for (const GA_Offset ptoff : split_range)
            sopattr.set(ptoff, utvalue[idToIdxMap.getIdxFromOffset(ptoff)]);
    });
    return true;
}

template <int TUPLESIZE>
bool _doCopyUsdPrimvarToSopPointAttrString(GU_Detail *gdp,
                                const GA_Range &range,
                                const VtValue &value,
                                const UT_StringHolder primvarname,
                                const GA_TypeInfo &type_info,
                                const IdToIdxMap &idToIdxMap)
{
    UT_StringArray utvalue;
    utvalue.setCapacity(value.GetArraySize());
    if (value.IsHolding<VtArray<std::string>>())
    {
        VtArray<std::string> strvalues;
        strvalues = value.UncheckedGet<VtArray<std::string>>();
        for (const auto &str : strvalues)
            utvalue.append(str);
    }
    else if (value.IsHolding<VtArray<SdfAssetPath>>())
    {
        VtArray<SdfAssetPath> assetpaths;
        assetpaths = value.UncheckedGet<VtArray<SdfAssetPath>>();
        for (const auto &p : assetpaths)
            utvalue.append(p.GetAssetPath());
    }
    else if (value.IsHolding<VtArray<TfToken>>())
    {
        VtArray<TfToken> tokens;
        tokens = value.UncheckedGet<VtArray<TfToken>>();
        for (const auto &p : tokens)
            utvalue.append(p.GetString());
    }
    else if (value.IsHolding<VtArray<SdfPathExpression>>())
    {
        VtArray<SdfPathExpression> pathexpressions;
        pathexpressions = value.UncheckedGet<VtArray<SdfPathExpression>>();
        for (const auto &p : pathexpressions)
            utvalue.append(p.GetText());
    }

    if (utvalue.size() == 0)
        return false;

    // get / create necessary sop attribute.
    UT_StringRef sopattrname = husdGetSopAttrName(primvarname);

    GA_RWHandleS sopattr = gdp->findPointAttribute(GA_SCOPE_PUBLIC, sopattrname);
    if (sopattr.isInvalid())
        return false;

    UTparallelFor(
        GA_SplittableRange(range),
        [&](const GA_SplittableRange &split_range)
    {
        for (const GA_Offset ptoff : split_range)
            sopattr.set(ptoff, utvalue[idToIdxMap.getIdxFromOffset(ptoff)]);
    });
    return true;
}

bool _copyUsdPrimvarToSopPointAttr(GU_Detail *gdp,
                                const GA_Range &range,
                                const UsdGeomPrimvar &primvar,
                                const HUSD_TimeCode &timecode,
                                const IdToIdxMap &idToIdxMap)
{
    SdfValueTypeName value_type = primvar.GetTypeName();

    GA_TypeInfo ga_type_info = GA_TYPE_VOID;
    if (value_type.GetRole() == SdfValueRoleNames->Color)
        ga_type_info = GA_TYPE_COLOR;
    else if (value_type.GetRole() == SdfValueRoleNames->Normal)
        ga_type_info = GA_TYPE_NORMAL;
    else if (value_type.GetRole() == SdfValueRoleNames->Point)
        ga_type_info = GA_TYPE_POINT;
    else if (value_type.GetRole() == SdfValueRoleNames->Vector)
        ga_type_info = GA_TYPE_VECTOR;
    else if (value_type.GetRole() == SdfValueRoleNames->TextureCoordinate)
        ga_type_info = GA_TYPE_TEXTURE_COORD;

    // first need to find primvar type
    VtValue value;
    primvar.ComputeFlattened(&value, HUSDgetUsdTimeCode(timecode));
    // Floats
    if (value.IsHolding<VtArray<GfVec4f>>())
        _doCopyUsdPrimvarToSopPointAttr<UT_Vector4F, GA_STORE_REAL32, 4>(
            gdp, range, value, primvar.GetBaseName().GetString(),
            ga_type_info, idToIdxMap);

    else if (value.IsHolding<VtArray<GfVec3f>>())
        _doCopyUsdPrimvarToSopPointAttr<UT_Vector3F, GA_STORE_REAL32, 3>(
            gdp, range, value, primvar.GetBaseName().GetString(),
            ga_type_info, idToIdxMap);

    else if (value.IsHolding<VtArray<GfVec2f>>())
        _doCopyUsdPrimvarToSopPointAttr<UT_Vector2F, GA_STORE_REAL32, 2>(
            gdp, range, value, primvar.GetBaseName().GetString(),
            ga_type_info, idToIdxMap);

    else if (value.IsHolding<VtArray<fpreal32>>())
        _doCopyUsdPrimvarToSopPointAttr<float, GA_STORE_REAL32, 1>(
            gdp, range, value, primvar.GetBaseName().GetString(),
            ga_type_info, idToIdxMap);

    // else if (value.IsHolding<VtArray<GfMatrix2f>>())
    //     _doCopyUsdPrimvarToSopPointAttr<UT_Matrix2F, GA_STORE_REAL32, 4>(
    //         gdp, start_offset, value, primvar.GetBaseName().GetString(),
    //         ga_type_info, idToIdxMap);

    else if (value.IsHolding<VtArray<GfMatrix3f>>())
        _doCopyUsdPrimvarToSopPointAttr<UT_Matrix3F, GA_STORE_REAL32, 9>(
            gdp, range, value, primvar.GetBaseName().GetString(),
            ga_type_info, idToIdxMap);

    else if (value.IsHolding<VtArray<GfMatrix4f>>())
        _doCopyUsdPrimvarToSopPointAttr<UT_Matrix4F, GA_STORE_REAL32, 16>(
            gdp, range, value, primvar.GetBaseName().GetString(),
            ga_type_info, idToIdxMap);

    else if (value.IsHolding<VtArray<GfQuath>>())
        _doCopyUsdPrimvarToSopPointAttr<UT_QuaternionH, GA_STORE_REAL16, 4>(
            gdp, range, value, primvar.GetBaseName().GetString(),
            ga_type_info, idToIdxMap);

    else if (value.IsHolding<VtArray<GfQuatf>>())
        _doCopyUsdPrimvarToSopPointAttr<UT_QuaternionF, GA_STORE_REAL32, 4>(
            gdp, range, value, primvar.GetBaseName().GetString(),
            ga_type_info, idToIdxMap);

    else if (value.IsHolding<VtArray<GfQuatd>>())
        _doCopyUsdPrimvarToSopPointAttr<UT_QuaternionD, GA_STORE_REAL64, 4>(
            gdp, range, value, primvar.GetBaseName().GetString(),
            ga_type_info, idToIdxMap);

    // Doubles
    else if (value.IsHolding<VtArray<GfVec4d>>())
        _doCopyUsdPrimvarToSopPointAttr<UT_Vector4D, GA_STORE_REAL64, 4>(
            gdp, range, value, primvar.GetBaseName().GetString(),
            ga_type_info, idToIdxMap);

    else if (value.IsHolding<VtArray<GfVec3d>>())
        _doCopyUsdPrimvarToSopPointAttr<UT_Vector3D, GA_STORE_REAL64, 3>(
            gdp, range, value, primvar.GetBaseName().GetString(),
            ga_type_info, idToIdxMap);

    else if (value.IsHolding<VtArray<GfVec2d>>())
        _doCopyUsdPrimvarToSopPointAttr<UT_Vector2D, GA_STORE_REAL64, 2>(
            gdp, range, value, primvar.GetBaseName().GetString(),
            ga_type_info, idToIdxMap);

    else if (value.IsHolding<VtArray<fpreal64>>())
        _doCopyUsdPrimvarToSopPointAttr<float, GA_STORE_REAL64, 1>(
            gdp, range, value, primvar.GetBaseName().GetString(),
            ga_type_info, idToIdxMap);

    else if (value.IsHolding<VtArray<GfMatrix2d>>())
        _doCopyUsdPrimvarToSopPointAttr<UT_Matrix2F, GA_STORE_REAL64, 4>(
            gdp, range, value, primvar.GetBaseName().GetString(),
            ga_type_info, idToIdxMap);

    else if (value.IsHolding<VtArray<GfMatrix3d>>())
        _doCopyUsdPrimvarToSopPointAttr<UT_Matrix3F, GA_STORE_REAL64, 9>(
            gdp, range, value, primvar.GetBaseName().GetString(),
            ga_type_info, idToIdxMap);

    else if (value.IsHolding<VtArray<GfMatrix4d>>())
        _doCopyUsdPrimvarToSopPointAttr<UT_Matrix4F, GA_STORE_REAL64, 16>(
            gdp, range, value, primvar.GetBaseName().GetString(),
            ga_type_info, idToIdxMap);

    // Halfs
    else if (value.IsHolding<VtArray<GfVec4h>>())
        _doCopyUsdPrimvarToSopPointAttr<UT_Vector4H, GA_STORE_REAL16, 4>(
            gdp, range, value, primvar.GetBaseName().GetString(),
            ga_type_info, idToIdxMap);

    else if (value.IsHolding<VtArray<GfVec3h>>())
        _doCopyUsdPrimvarToSopPointAttr<UT_Vector3H, GA_STORE_REAL16, 3>(
            gdp, range, value, primvar.GetBaseName().GetString(),
            ga_type_info, idToIdxMap);

    else if (value.IsHolding<VtArray<GfVec2h>>())
        _doCopyUsdPrimvarToSopPointAttr<UT_Vector2H, GA_STORE_REAL16, 2>(
            gdp, range, value, primvar.GetBaseName().GetString(),
            ga_type_info, idToIdxMap);

    else if (value.IsHolding<VtArray<GfHalf>>())
        _doCopyUsdPrimvarToSopPointAttr<fpreal16, GA_STORE_REAL16, 1>(
            gdp, range, value, primvar.GetBaseName().GetString(),
            ga_type_info, idToIdxMap);

    // integers
    else if (value.IsHolding<VtArray<GfVec4i>>())
        _doCopyUsdPrimvarToSopPointAttr<UT_Vector4i, GA_STORE_INT32, 4>(
            gdp, range, value, primvar.GetBaseName().GetString(),
            ga_type_info, idToIdxMap);

    else if (value.IsHolding<VtArray<GfVec3i>>())
        _doCopyUsdPrimvarToSopPointAttr<UT_Vector3i, GA_STORE_INT32, 3>(
            gdp, range, value, primvar.GetBaseName().GetString(),
            ga_type_info, idToIdxMap);

    else if (value.IsHolding<VtArray<GfVec2i>>())
        _doCopyUsdPrimvarToSopPointAttr<UT_Vector2i, GA_STORE_INT32, 2>(
            gdp, range, value, primvar.GetBaseName().GetString(),
            ga_type_info, idToIdxMap);

    else if (value.IsHolding<VtArray<int>>())
        _doCopyUsdPrimvarToSopPointAttr<int32, GA_STORE_INT32, 1>(
            gdp, range, value, primvar.GetBaseName().GetString(),
            ga_type_info, idToIdxMap);

    else if (value.IsHolding<VtArray<uint>>())
        _doCopyUsdPrimvarToSopPointAttr<uint32, GA_STORE_INT32, 1>(
            gdp, range, value, primvar.GetBaseName().GetString(),
            ga_type_info, idToIdxMap);

    else if (value.IsHolding<VtArray<int64>>())
        _doCopyUsdPrimvarToSopPointAttr<int64, GA_STORE_INT64, 1>(
            gdp, range, value, primvar.GetBaseName().GetString(),
            ga_type_info, idToIdxMap);

    else if (value.IsHolding<VtArray<uint64>>())
        _doCopyUsdPrimvarToSopPointAttr<uint64, GA_STORE_INT64, 1>(
            gdp, range, value, primvar.GetBaseName().GetString(),
            ga_type_info, idToIdxMap);

    // Other Data Types
    else if (value.IsHolding<VtArray<bool>>())
        _doCopyUsdPrimvarToSopPointAttr<bool, GA_STORE_INT8, 1>(
            gdp, range, value, primvar.GetBaseName().GetString(),
            ga_type_info, idToIdxMap);

    else if (value.IsHolding<VtArray<std::string>>())
        _doCopyUsdPrimvarToSopPointAttrString<1>(
            gdp, range, value, primvar.GetBaseName().GetString(),
            ga_type_info, idToIdxMap);

    else if (value.IsHolding<VtArray<SdfAssetPath>>())
        _doCopyUsdPrimvarToSopPointAttrString<1>(
            gdp, range, value, primvar.GetBaseName().GetString(),
            ga_type_info, idToIdxMap);

    else if (value.IsHolding<VtArray<TfToken>>())
        _doCopyUsdPrimvarToSopPointAttrString<1>(
            gdp, range, value, primvar.GetBaseName().GetString(),
            ga_type_info, idToIdxMap);

    else if (value.IsHolding<VtArray<SdfPathExpression>>())
        _doCopyUsdPrimvarToSopPointAttrString<1>(
            gdp, range, value, primvar.GetBaseName().GetString(),
            ga_type_info, idToIdxMap);
    return true;
}


template <class UtType, class HandleType = GA_ROHandleT<UtType>>
bool _copySopAttrToUsdPrimvar(HUSD_AutoWriteLock &writelock,
                              const GU_Detail *gdp,
                              const GA_Range &primrange,
                              const UT_StringRef &primpath,
                              const UT_StringRef &attrname,
                              const HUSD_TimeCode &timecode,
                              bool indexed,
                              const UT_StringRef &valuetype,
                              const HUSDpointInstancerOffsetMap::OffsetMap &map,
                              const HUSD_PointInstancer::SopToUsdConfig &config,
                              husd_UsdWriteQueue &pending,
                              bool forceupdate = false)
{
    HUSD_GetAttributes     getattrs(writelock);
    const UT_StringHolder  usdname = husdGetPrimvarName(attrname);
    const UT_Array<exint> &newids = map.getNewIds();
    UT_Array<UtType>       values;

    HUSD_PointInstancerCopyStyle copystyle = map.myCopyStyle;
    if (forceupdate)
        copystyle = HUSD_PointInstancerCopyStyle::Update;

    if (copystyle == HUSD_PointInstancerCopyStyle::Sparse ||
        copystyle == HUSD_PointInstancerCopyStyle::Update)
    {
        if (config.myMissingPrimvarsPolicy == HUSD_PointInstancerMissingPrimvarsPolicy::IgnorePrimvar &&
            config.myCommonPrimvars.find(usdname) < 0 &&
            map.myImportedPrimvars.find(usdname) < 0)
        {
            return true;
        }

        getattrs.getPrimvarArray(primpath, usdname, values, timecode);
        if (values.isEmpty())
        {
            if (copystyle == HUSD_PointInstancerCopyStyle::Update)
                return true; // Nothing to update for non-existent attr

            // this is a new primvar, so we need to populate some empty values.
            // the array should have one entry for each existing instance
            values.setSize(map.myOriginalNumInstances);
        }
        // make room for new ids / instances
        values.setSize(values.size() + newids.size());
    }
    else
        values.setSize(primrange.getEntries());

    if (copystyle != HUSD_PointInstancerCopyStyle::Update)
    {
        HandleType handle = gdp->findAttribute(GA_ATTRIB_POINT,
                                               attrname);
        if (handle.isValid())
        {
            UTparallelFor(GA_SplittableRange(primrange),
                [&] (const GA_SplittableRange &splitrange)
                {
                    for (GA_Offset ptoff : splitrange)
                    {
                        values[map.getIdx(ptoff)] = handle.get(ptoff);
                    }
                });
        }
    }

    bool checkmissing = copystyle != HUSD_PointInstancerCopyStyle::Overwrite &&
                        config.myMissingPointsPolicy == HUSD_PointInstancerMissingPointsPolicy::Remove;
    UT_Array<UtType>  updatedvalues;
    updatedvalues.setCapacity(values.size());
    for (exint idx = 0, end = values.size(); idx < end; ++idx)
    {
        if (!map.isDeleted(idx) &&
            (!checkmissing || !map.isMissing(idx)))
            updatedvalues.append(values[idx]);
    }
    values = std::move(updatedvalues);

    pending.appendPrimvar(primpath, usdname,
        HUSD_Constants::getInterpolationVarying(),
        timecode, valuetype, std::move(values));
    return true;
}

bool copySopAttrToUsdPrimvar(HUSD_AutoWriteLock &writelock,
                             const GU_Detail *gdp,
                             const GA_Range &primrange,
                             const UT_StringRef &primpath,
                             const UT_StringRef &attrname,
                             const HUSD_TimeCode &timecode,
                             bool indexed,
                             const HUSDpointInstancerOffsetMap::OffsetMap &map,
                             const HUSD_PointInstancer::SopToUsdConfig &config,
                             husd_UsdWriteQueue &pending,
                             bool forceupdate = false)
{
    const GA_Attribute       *attrib = gdp->findAttribute(GA_ATTRIB_POINT, attrname);
    if (!attrib)
        return false;

    UT_StringRef              valuetype = UT_StringHolder::theEmptyString;
    int                       tuplesize = attrib->getTupleSize();
    GA_TypeInfo               typeinfo = attrib->getTypeInfo();
    GA_StorageClass           storageclass = attrib->getStorageClass();
    const GA_AIFTuple         *tuple = attrib->getAIFTuple();
    GA_Storage                storage = GA_STORE_INVALID;

    if (tuple)
        storage = tuple->getStorage(attrib);

    // Float
    if (storageclass == GA_STORECLASS_REAL)
    {
        if (storage == GA_STORE_REAL32)
        {
            if (tuplesize == 16)
                return _copySopAttrToUsdPrimvar<UT_Matrix4F>(
                           writelock, gdp, primrange, primpath, attrname,
                           timecode, indexed, valuetype, map, config, pending, forceupdate);

            if (tuplesize == 9)
                return _copySopAttrToUsdPrimvar<UT_Matrix3F>(
                           writelock, gdp, primrange, primpath, attrname,
                           timecode, indexed, valuetype, map, config, pending, forceupdate);

            if (tuplesize == 4)
            {
                valuetype = SdfValueTypeNames->Float4Array.GetAsToken().GetString();
                if (typeinfo == GA_TYPE_QUATERNION)
                {
                    valuetype = SdfValueTypeNames->QuatfArray.GetAsToken().GetString();
                    return _copySopAttrToUsdPrimvar<UT_QuaternionF>(
                           writelock, gdp, primrange, primpath, attrname,
                           timecode, indexed, valuetype, map, config, pending, forceupdate);
                }

                if (typeinfo == GA_TYPE_COLOR)
                    valuetype = SdfValueTypeNames->Color4fArray.GetAsToken().GetString();

                return _copySopAttrToUsdPrimvar<UT_Vector4F>(
                       writelock, gdp, primrange, primpath, attrname,
                       timecode, indexed, valuetype, map, config, pending, forceupdate);
            }

            if (tuplesize == 3)
            {
                valuetype = SdfValueTypeNames->Float3Array.GetAsToken().GetString();
                if (typeinfo == GA_TYPE_POINT)
                    valuetype = SdfValueTypeNames->Point3fArray.GetAsToken().GetString();
                else if (typeinfo == GA_TYPE_COLOR)
                    valuetype = SdfValueTypeNames->Color3fArray.GetAsToken().GetString();
                else if (typeinfo == GA_TYPE_VECTOR)
                    valuetype = SdfValueTypeNames->Vector3fArray.GetAsToken().GetString();
                else if (typeinfo == GA_TYPE_NORMAL)
                    valuetype = SdfValueTypeNames->Normal3fArray.GetAsToken().GetString();
                else if (typeinfo == GA_TYPE_TEXTURE_COORD)
                    valuetype = SdfValueTypeNames->TexCoord3fArray.GetAsToken().GetString();

                return _copySopAttrToUsdPrimvar<UT_Vector3F>(
                       writelock, gdp, primrange, primpath, attrname,
                       timecode, indexed, valuetype, map, config, pending, forceupdate);
            }

            if (tuplesize == 2)
            {
                valuetype = SdfValueTypeNames->Float2Array.GetAsToken().GetString();
                if (typeinfo == GA_TYPE_TEXTURE_COORD)
                    valuetype = SdfValueTypeNames->TexCoord2fArray.GetAsToken().GetString();

                return _copySopAttrToUsdPrimvar<UT_Vector2F>(
                       writelock, gdp, primrange, primpath, attrname,
                       timecode, indexed, valuetype, map, config, pending, forceupdate);
            }
            if (tuplesize == 1)
                return _copySopAttrToUsdPrimvar<fpreal32>(
                       writelock, gdp, primrange, primpath, attrname,
                       timecode, indexed, valuetype, map, config, pending, forceupdate);
        }

        // Double
        if (storage == GA_STORE_REAL64)
        {
            if (tuplesize == 16)
                return _copySopAttrToUsdPrimvar<UT_Matrix4D>(
                       writelock, gdp, primrange, primpath, attrname,
                       timecode, indexed, valuetype, map, config, pending, forceupdate);

            if (tuplesize == 9)
                return _copySopAttrToUsdPrimvar<UT_Matrix3D>(
                       writelock, gdp, primrange, primpath, attrname,
                       timecode, indexed, valuetype, map, config, pending, forceupdate);

            if (tuplesize == 4)
            {
                valuetype = SdfValueTypeNames->Double4Array.GetAsToken().GetString();
                if (typeinfo == GA_TYPE_QUATERNION)
                {
                    valuetype = SdfValueTypeNames->QuatdArray.GetAsToken().GetString();
                    return _copySopAttrToUsdPrimvar<UT_QuaternionD>(
                       writelock, gdp, primrange, primpath, attrname,
                       timecode, indexed, valuetype, map, config, pending, forceupdate);
                }
                if (typeinfo == GA_TYPE_TRANSFORM)
                {
                    return _copySopAttrToUsdPrimvar<UT_Matrix2D>(
                       writelock, gdp, primrange, primpath, attrname,
                       timecode, indexed, valuetype, map, config, pending, forceupdate);
                }

                if (typeinfo == GA_TYPE_COLOR)
                    valuetype = SdfValueTypeNames->Color4dArray.GetAsToken().GetString();

                return _copySopAttrToUsdPrimvar<UT_Vector4D>(
                   writelock, gdp, primrange, primpath, attrname,
                   timecode, indexed, valuetype, map, config, pending, forceupdate);
            }

            if (tuplesize == 3)
            {
                valuetype = SdfValueTypeNames->Double3Array.GetAsToken().GetString();
                if (typeinfo == GA_TYPE_POINT)
                    valuetype = SdfValueTypeNames->Point3dArray.GetAsToken().GetString();
                else if (typeinfo == GA_TYPE_COLOR)
                    valuetype = SdfValueTypeNames->Color3dArray.GetAsToken().GetString();
                else if (typeinfo == GA_TYPE_VECTOR)
                    valuetype = SdfValueTypeNames->Vector3dArray.GetAsToken().GetString();
                else if (typeinfo == GA_TYPE_NORMAL)
                    valuetype = SdfValueTypeNames->Normal3dArray.GetAsToken().GetString();
                else if (typeinfo == GA_TYPE_TEXTURE_COORD)
                    valuetype = SdfValueTypeNames->TexCoord3dArray.GetAsToken().GetString();

                return _copySopAttrToUsdPrimvar<UT_Vector3D>(
                   writelock, gdp, primrange, primpath, attrname,
                   timecode, indexed, valuetype, map, config, pending, forceupdate);
            }

            if (tuplesize == 2)
            {
                valuetype = SdfValueTypeNames->Double2Array.GetAsToken().GetString();
                if (typeinfo == GA_TYPE_TEXTURE_COORD)
                    valuetype = SdfValueTypeNames->TexCoord2dArray.GetAsToken().GetString();

                return _copySopAttrToUsdPrimvar<UT_Vector2D>(
                       writelock, gdp, primrange, primpath, attrname,
                       timecode, indexed, valuetype, map, config, pending, forceupdate);
            }

            if (tuplesize == 1)
                return _copySopAttrToUsdPrimvar<fpreal64>(
                           writelock, gdp, primrange, primpath, attrname,
                           timecode, indexed, valuetype, map, config, pending, forceupdate);
        }

        // Half
        if (storage == GA_STORE_REAL16)
        {
            if (tuplesize == 4)
            {
                valuetype = SdfValueTypeNames->Half4Array.GetAsToken().GetString();
                if (typeinfo == GA_TYPE_QUATERNION)
                {
                    valuetype = SdfValueTypeNames->QuathArray.GetAsToken().GetString();
                    return _copySopAttrToUsdPrimvar<UT_QuaternionH>(
                       writelock, gdp, primrange, primpath, attrname,
                       timecode, indexed, valuetype, map, config, pending, forceupdate);
                }
                if (typeinfo == GA_TYPE_COLOR)
                    valuetype = SdfValueTypeNames->Color4hArray.GetAsToken().GetString();

                return _copySopAttrToUsdPrimvar<UT_Vector4H>(
                       writelock, gdp, primrange, primpath, attrname,
                       timecode, indexed, valuetype, map, config, pending, forceupdate);
            }

            if (tuplesize == 3)
            {
                valuetype = SdfValueTypeNames->Half3Array.GetAsToken().GetString();
                if (typeinfo == GA_TYPE_POINT)
                    valuetype = SdfValueTypeNames->Point3hArray.GetAsToken().GetString();
                else if (typeinfo == GA_TYPE_COLOR)
                    valuetype = SdfValueTypeNames->Color3hArray.GetAsToken().GetString();
                else if (typeinfo == GA_TYPE_VECTOR)
                    valuetype = SdfValueTypeNames->Vector3hArray.GetAsToken().GetString();
                else if (typeinfo == GA_TYPE_NORMAL)
                    valuetype = SdfValueTypeNames->Normal3hArray.GetAsToken().GetString();
                else if (typeinfo == GA_TYPE_TEXTURE_COORD)
                    valuetype = SdfValueTypeNames->TexCoord3hArray.GetAsToken().GetString();

                return _copySopAttrToUsdPrimvar<UT_Vector3H>(
                       writelock, gdp, primrange, primpath, attrname,
                       timecode, indexed, valuetype, map, config, pending, forceupdate);
            }

            if (tuplesize == 2)
            {
                valuetype = SdfValueTypeNames->Half2Array.GetAsToken().GetString();
                if (typeinfo == GA_TYPE_TEXTURE_COORD)
                    valuetype = SdfValueTypeNames->TexCoord2hArray.GetAsToken().GetString();

                return _copySopAttrToUsdPrimvar<UT_Vector2H>(
                       writelock, gdp, primrange, primpath, attrname,
                       timecode, indexed, valuetype, map, config, pending, forceupdate);
            }

            if (tuplesize == 1)
                return _copySopAttrToUsdPrimvar<fpreal16>(
                       writelock, gdp, primrange, primpath, attrname,
                       timecode, indexed, valuetype, map, config, pending, forceupdate);
        }
    }
    else if (storageclass == GA_STORECLASS_INT)
    {
        if (storage == GA_STORE_INT32)
        {
            if (tuplesize == 4)
                return _copySopAttrToUsdPrimvar<UT_Vector4i>(
                           writelock, gdp, primrange, primpath, attrname,
                           timecode, indexed, valuetype, map, config, pending, forceupdate);


            if (tuplesize == 3)
                return _copySopAttrToUsdPrimvar<UT_Vector3i>(
                               writelock, gdp, primrange, primpath, attrname,
                               timecode, indexed, valuetype, map, config, pending, forceupdate);

            if (tuplesize == 2)
                return _copySopAttrToUsdPrimvar<UT_Vector2i>(
                                   writelock, gdp, primrange, primpath, attrname,
                                   timecode, indexed, valuetype, map, config, pending, forceupdate);

            if (tuplesize == 1)
                return _copySopAttrToUsdPrimvar<int32>(
                                   writelock, gdp, primrange, primpath, attrname,
                                   timecode, indexed, valuetype, map, config, pending, forceupdate);
        }

        if (storage == GA_STORE_INT64)
        {
            if (tuplesize == 1)
                return _copySopAttrToUsdPrimvar<int64>(
                                   writelock, gdp, primrange, primpath, attrname,
                                   timecode, indexed, valuetype, map, config, pending, forceupdate);
        }

        // bool
        if (storage == GA_STORE_BOOL)
        {
            if (tuplesize == 1)
                return _copySopAttrToUsdPrimvar<bool>(
                            writelock, gdp, primrange, primpath, attrname,
                            timecode, indexed, valuetype, map, config, pending, forceupdate);
        }
    }
    else if (storageclass == GA_STORECLASS_STRING)
    {
        return _copySopAttrToUsdPrimvar<UT_StringHolder, GA_ROHandleS>(
                   writelock, gdp, primrange, primpath, attrname,
                   timecode, indexed, valuetype, map, config, pending, forceupdate);
    }
    return false;
}


template <class UtType>
void _copySopAttrToUsdAttr(HUSD_AutoWriteLock &writelock,
                           const GU_Detail *gdp,
                           const GA_Range &primrange,
                           const UT_StringRef &primpath,
                           const UT_StringRef &attrname,
                           const HUSD_TimeCode &timecode,
                           const HUSDpointInstancerOffsetMap::OffsetMap &map,
                           const HUSD_PointInstancer::SopToUsdConfig &config,
                           const HUSD_PointInstancerCopyStyle copystyle,
                           husd_UsdWriteQueue &pending,
                           const UtType* defaultvalue=nullptr)
{
    if (copystyle == HUSD_PointInstancerCopyStyle::Invalid)
        return;

    HUSD_GetAttributes    getattrs(writelock);
    const UT_StringHolder usdname = husdGetPrimvarName(attrname);

    UT_Array<UtType>     values;

    if (copystyle == HUSD_PointInstancerCopyStyle::Sparse ||
        copystyle == HUSD_PointInstancerCopyStyle::Update)
    {
        getattrs.getAttribute(primpath, usdname, values, timecode);
        if (values.isEmpty())
        {
            if (copystyle == HUSD_PointInstancerCopyStyle::Update)
                return;  // Nothing to update for non-existent attr

            // this is a new primvar, so we need to populate some empty values.
            // the array should have one entry for each existing instance
            if (defaultvalue)
                values.appendMultiple(*defaultvalue, map.myOriginalNumInstances);
            else
                values.setSize(map.myOriginalNumInstances);
        }
        // make room for new ids / instances
        if (defaultvalue)
        {
            values.appendMultiple(*defaultvalue, map.myNewIds.size());
        }
        else
        {
            values.setSize(values.size() + map.myNewIds.size());
        }
    }
    else
    {
        if (defaultvalue)
            values.appendMultiple(*defaultvalue, primrange.getEntries());
        else
            values.setSize(primrange.getEntries());
    }

    // in update mode, we do not care about getting values from SOPs
    if (copystyle != HUSD_PointInstancerCopyStyle::Update)
    {
        GA_ROHandleT<UtType> handle = gdp->findAttribute(GA_ATTRIB_POINT,
                                                         attrname);
        if (handle.isValid())
        {
            UTparallelFor(GA_SplittableRange(primrange),
            [&] (const GA_SplittableRange &splitrange)
                {
                    for (GA_Offset ptoff : splitrange)
                    {
                        values[map.getIdx(ptoff)] = handle.get(ptoff);
                    }
                });
        }
    }

    {
        bool checkmissing = copystyle != HUSD_PointInstancerCopyStyle::Overwrite &&
                                         config.myMissingPointsPolicy == HUSD_PointInstancerMissingPointsPolicy::Remove;
        UT_Array<UtType>  updatedvalues;
        updatedvalues.setCapacity(values.size());
        for (exint idx = 0, end = values.size(); idx < end; ++idx)
            if (!map.isDeleted(idx) &&
                (!checkmissing || !map.isMissing(idx)))
                updatedvalues.append(values[idx]);
        values = std::move(updatedvalues);
    }

    pending.appendAttribute(primpath, usdname, timecode,
        UT_StringHolder::theEmptyString, std::move(values));
}



template <class valuetype, GA_Storage sopstorage, int tuplesize>
bool _copyUsdAttrToSopAttr(GU_Detail *gdp,
                                const GA_Range &range,
                                const HUSD_GetAttributes& getattrs,
                                const UT_StringRef& primpath,
                                const HUSD_TimeCode &timecode,
                                const UT_StringRef &usdattrname,
                                const IdToIdxMap &idToIdxMap,
                                const GA_TypeInfo &typeinfo=GA_TYPE_VOID,
                                const UT_StringRef *sopnameoverride=nullptr)
{
    // look up USD Primvar values
    UT_Array<valuetype> usdvalues;
    getattrs.getAttributeArray(primpath, usdattrname, usdvalues, timecode);
    if (usdvalues.size() == 0)
    {
        // no usd primvar values, so no sop values to set
        HUSD_ErrorScope::addWarning(
            HUSD_ERR_CANT_FIND_PROPERTY,
            usdattrname);
        return false;
    }

    // get / create necessary sop attribute.
    UT_StringRef sopattrname;
    if (sopnameoverride)
        sopattrname = *sopnameoverride;
    else
        sopattrname = husdGetSopAttrName(usdattrname);

    GA_RWHandleT<valuetype> sopattr = gdp->findPointAttribute(GA_SCOPE_PUBLIC,
                                                              sopattrname);
    if (sopattr.isInvalid())
        return false;

    UTparallelFor(
        GA_SplittableRange(range),
        [&](const GA_SplittableRange &split_range)
    {
        for (const GA_Offset ptoff : split_range)
            sopattr.set(ptoff, usdvalues[idToIdxMap.getIdxFromOffset(ptoff)]); //TODO: this is returning 300 for ptoff 0
    });
    return true;
}

/// --------------------------Attribute Functions------------------------
void _setFromWorldXform(GU_Detail *gdp,
                        HUSD_AutoReadLock &readlock,
                        const UT_StringRef &primpath,
                        const HUSDPointInstancerParms &parms,
                        const HUSD_TimeCode &timecode,
                        const GA_Range &range,
                        const IdToIdxMap &idToIdxMap)
{
    HUSD_Info             info(readlock);
    const UT_Matrix4D     worldXform = info.getWorldXform(primpath, timecode);
    UT_Array<UT_Matrix4D> instanceXforms;

    info.getPointInstancerXforms(primpath, instanceXforms, timecode);

    GA_RWHandleV3 scaleattr = gdp->findPointAttribute(GA_SCOPE_PUBLIC,
                                                      GA_Names::scale);
    GA_RWHandleQ  orientattr = gdp->findPointAttribute(GA_SCOPE_PUBLIC,
                                                       GA_Names::orient);
    UTparallelFor(
        GA_SplittableRange(range),
        [&](const GA_SplittableRange &split_range)
        {
            UT_Matrix4D instance_xform;
            UT_Vector3D translation;
            UT_Vector3D scale;
            UT_Matrix3D tmp_xform3d;
            UT_Quaternion orient;
            for (GA_Offset ptoff : split_range)
            {
                instance_xform = instanceXforms[idToIdxMap.getIdxFromOffset(ptoff)];
                tmp_xform3d = instance_xform;
                instance_xform = instance_xform * worldXform;
                instance_xform.getTranslates(translation);
                tmp_xform3d.extractScales(scale);
                orient.updateFromArbitraryMatrix(tmp_xform3d);

                if (parms.myImportPositions)
                    gdp->setPos3(ptoff, translation);
                if (scaleattr.isValid() && parms.myImportScales)
                    scaleattr.set(ptoff, scale);
                if (orientattr.isValid() && parms.myImportOrientations)
                    orientattr.set(ptoff, orient);
            }
        });
}

void _setPointPositions(GU_Detail *gdp,
                        HUSD_AutoReadLock &readlock,
                        const UT_StringRef &primpath,
                        const HUSD_TimeCode &timecode,
                        const GA_Range &range,
                        const IdToIdxMap &idToIdxMap)
{
    HUSD_GetAttributes  getattrs(readlock);
    UT_Vector3FArray    usd_positions;
    getattrs.getAttributeArray(primpath,
        HUSD_Constants::getAttributePointPositions(),
        usd_positions, timecode);


    UTparallelFor(
        GA_SplittableRange(range),
        [&](const GA_SplittableRange &split_range)
        {
            for (GA_Offset ptoff : split_range)
                gdp->setPos3(ptoff,
                             usd_positions[idToIdxMap.getIdxFromOffset(ptoff)]);
        });
}

void _setPointIds(GU_Detail *gdp,
    UT_Array<GA_Offset> &idToPtoffMap,
    HUSD_AutoReadLock &readlock,
    const UT_StringRef &primpath,
    const HUSD_TimeCode &timecode,
    const GA_Range &range,
    const IdToIdxMap &idToIdxMap,
    const UT_Array<exint> *ids = nullptr)
{
    GA_RWHandleI sop_idattr = gdp->findPointAttribute(GA_SCOPE_PUBLIC,
                                                      GA_Names::id);
    UT_ASSERT(sop_idattr.isValid());
    exint        idx;
    if (ids && !ids->isEmpty())
    {
        exint i = 0;
        for (GA_Offset ptoff : range)
        {
            sop_idattr.set(ptoff, (*ids)[i++]);
        }

        exint min, max=-1;
        UTgetArrayMinMax(ids->begin(), ids->end(), min, max);
        idToPtoffMap.appendMultiple(GA_INVALID_OFFSET, max+1);
        idx = -1;
        for (GA_Offset ptoff : range)
            idToPtoffMap[(*ids)[++idx]] = ptoff;
        return;
    }
    // We were provided no indices, meaning import the entire point instancer
    // First, we need to check for an ids attribute and set that, otherwise
    // we will fallback to starting at 0 and incrementing.
    HUSD_GetAttributes getattrs(readlock);
    UT_Array<exint> usd_ids;
    getattrs.getAttributeArray(primpath,
                               HUSD_Constants::getAttributePointIds(),
                               usd_ids, timecode);
    if (usd_ids.isEmpty())
    {
        // no ids attribute in usd, and no inidces list supplied, increment
        // starting from 0.
        usd_ids.setSize(range.getEntries());
        idToPtoffMap.setSize(range.getEntries());
        idx = -1;
        for (GA_Iterator ptoff = range.begin(); ptoff != range.end(); ++ptoff)
        {
            ++idx;
            usd_ids[idx] = idx;
            idToPtoffMap[idx] = *ptoff;
        }
    }
    else
    {
        if (usd_ids.size() != range.getEntries())
        {
            // usd_ids is invalid size
            HUSD_ErrorScope::addError(
                                HUSD_ERR_INVALID_POINTINSTANCER_PROPERTY_LENGTH,
                                "ids");
            return;
        }

        exint min, max=-1;
        UTgetArrayMinMax(usd_ids.begin(), usd_ids.end(), min, max);
        idToPtoffMap.appendMultiple(GA_INVALID_OFFSET, max+1);

        idx = -1;
        for (GA_Iterator ptoff = range.begin(); ptoff != range.end(); ++ptoff)
            idToPtoffMap[usd_ids[++idx]] = *ptoff;
    }

    UT_ASSERT(usd_ids.size() == range.getEntries());
    UTparallelFor(
        GA_SplittableRange(range),
        [&](const GA_SplittableRange &split_range)
    {
        for (const GA_Offset ptoff : split_range)
            sop_idattr.set(ptoff, usd_ids[idToIdxMap.getIdxFromOffset(ptoff)]);
    });
}

void _setPointPaths(GU_Detail          *gdp,
                    const UT_StringRef &primpath,
                    const GA_Range     &range)
{
    GA_RWHandleS sop_pathattr = gdp->findPointAttribute(GA_SCOPE_PUBLIC,
                                                        GA_Names::path);
    UT_ASSERT(sop_pathattr.isValid());

    UTparallelFor(
        GA_SplittableRange(range),
        [&](const GA_SplittableRange &split_range)
        {
            GA_RWBatchHandleS path_handle(sop_pathattr.getAttribute());
            for (GA_Offset ptoff : split_range)
                path_handle.set(ptoff, primpath);
        });
}

void _setPointVisibility(GU_Detail *gdp,
                         HUSD_AutoReadLock &readlock,
                         const UT_StringRef &primpath,
                         const HUSD_TimeCode &timecode,
                         const UT_Array<GA_Offset> &idToPtoffMap)
{
    UT_Array<exint>    usd_invisibleids;
    HUSD_GetAttributes getattrs(readlock);

    getattrs.getAttributeArray(primpath,
        UsdGeomTokens->invisibleIds.GetString(), usd_invisibleids,
        timecode);

    GA_PointGroupUPtr group = gdp->createDetachedPointGroup();
    for (const exint &id : usd_invisibleids)
    {
        if (id < idToPtoffMap.size())
        {
            const GA_Offset ptoff = idToPtoffMap[id];
            if (ptoff != GA_INVALID_OFFSET)
                group->addOffset(ptoff);
        } // else invalid id
    }

    GA_RWHandleS visattr = gdp->findPointAttribute(GA_SCOPE_PUBLIC,
                                         theUsdVisibilityAttributeName.asRef());
    UT_ASSERT(visattr.isValid());
    GA_Range range = gdp->getPointRange(group.get());

    UTparallelFor(
        GA_SplittableRange(range),
        [&](const GA_SplittableRange &split_range)
        {
            GA_RWBatchHandleS vis_handle(visattr.getAttribute());
            for (GA_Offset ptoff : split_range)
                vis_handle.set(ptoff, theInvisibleName.asRef());
        });
}

void _setPrototypeIndices(GU_Detail *gdp,
                         HUSD_AutoReadLock &readlock,
                         const UT_StringRef &primpath,
                         const HUSD_TimeCode &timecode,
                         const GA_Range &range,
                         const HUSDPointInstancerParms &parms,
                         const IdToIdxMap &idToIdxMap)
{
    UT_Array<exint>    usd_protoindices;
    HUSD_GetAttributes getattrs(readlock);

    if (parms.myProtoSource != HUSD_PointInstancerSopProtoIndexSource::None)
    {
        UT_Array<exint>    usd_ids;

        getattrs.getAttributeArray(primpath,
            UsdGeomTokens->protoIndices.GetString(), usd_protoindices,
            timecode);

        if (parms.myProtoSource ==
                           HUSD_PointInstancerSopProtoIndexSource::Attribute)
        {
            _copyUsdAttrToSopAttr<exint, GA_STORE_INT64, 1>(gdp,
                            range, getattrs, primpath, timecode,
                            UsdGeomTokens->protoIndices.GetString(), idToIdxMap,
                            GA_TYPE_VOID, &parms.myIntAttrName);
        }
        else
        {
            UT_StringArray  usd_prototypes;
            HUSD_Info       info(readlock);

            GA_RWHandleS    sop_protopathattr = gdp->findPointAttribute(
                                                           GA_SCOPE_PUBLIC,
                                                           parms.myStrAttrName);
            UT_ASSERT(sop_protopathattr.isValid());

            info.getRelationshipTargets(primpath,
                UsdGeomTokens->prototypes.GetString(), usd_prototypes);

            if (parms.myProtoSource == HUSD_PointInstancerSopProtoIndexSource::PrimPath)
            {
                UTparallelFor(
                    GA_SplittableRange(range),
                    [&](const GA_SplittableRange &split_range)
                    {
                        GA_RWBatchHandleS protopathhandle(sop_protopathattr.getAttribute());
                        for (GA_Offset ptoff : split_range)
                            protopathhandle.set(ptoff,
                                                usd_prototypes[
                                                    usd_protoindices[
                                                        idToIdxMap.getIdxFromOffset(ptoff)]]);
                    });
            }
            else if (parms.myProtoSource == HUSD_PointInstancerSopProtoIndexSource::PrimName)
            {
                // first build a list of names in the same order as the
                // the prototypes
                UT_Array<UT_StringHolder> names;
                for (const UT_StringRef &path : usd_prototypes)
                {
                    UT_StringView view(path);
                    names.append(view.substr(view.findLastOf("/")+1)); // +1 to skip '/'
                }
                UTparallelFor(
                    GA_SplittableRange(range),
                    [&](const GA_SplittableRange &split_range)
                    {
                        GA_RWBatchHandleS protopathhandle(sop_protopathattr.getAttribute());
                        for (GA_Offset ptoff : split_range)
                        {
                            protopathhandle.set(ptoff,
                                                names[usd_protoindices[
                                                        idToIdxMap.getIdxFromOffset(ptoff)]]);
                        }
                    });
            }
        }
    }
}


// Create the SOP point attribute corresponding to a USD primvar, mirroring
// the value-type dispatch in _copyUsdPrimvarToSopPointAttr but ONLY adding
// the attribute (no value population). Must be called from the main thread.
void _createSopAttrForPrimvar(GU_Detail *gdp,
                              const UsdGeomPrimvar &primvar,
                              const HUSD_TimeCode &timecode)
{
    SdfValueTypeName value_type = primvar.GetTypeName();

    GA_TypeInfo ga_type_info = GA_TYPE_VOID;
    if (value_type.GetRole() == SdfValueRoleNames->Color)
        ga_type_info = GA_TYPE_COLOR;
    else if (value_type.GetRole() == SdfValueRoleNames->Normal)
        ga_type_info = GA_TYPE_NORMAL;
    else if (value_type.GetRole() == SdfValueRoleNames->Point)
        ga_type_info = GA_TYPE_POINT;
    else if (value_type.GetRole() == SdfValueRoleNames->Vector)
        ga_type_info = GA_TYPE_VECTOR;
    else if (value_type.GetRole() == SdfValueRoleNames->TextureCoordinate)
        ga_type_info = GA_TYPE_TEXTURE_COORD;

    VtValue value;
    primvar.ComputeFlattened(&value, HUSDgetUsdTimeCode(timecode));
    if (value.IsEmpty())
        return;

    UT_StringRef sopattrname =
        husdGetSopAttrName(primvar.GetBaseName().GetString());

    GA_Attribute *attr = nullptr;

    // Floats
    if (value.IsHolding<VtArray<GfVec4f>>())
        attr = gdp->addTuple(GA_STORE_REAL32, GA_ATTRIB_POINT, sopattrname, 4);
    else if (value.IsHolding<VtArray<GfVec3f>>())
        attr = gdp->addTuple(GA_STORE_REAL32, GA_ATTRIB_POINT, sopattrname, 3);
    else if (value.IsHolding<VtArray<GfVec2f>>())
        attr = gdp->addTuple(GA_STORE_REAL32, GA_ATTRIB_POINT, sopattrname, 2);
    else if (value.IsHolding<VtArray<fpreal32>>())
        attr = gdp->addTuple(GA_STORE_REAL32, GA_ATTRIB_POINT, sopattrname, 1);
    else if (value.IsHolding<VtArray<GfMatrix3f>>())
        attr = gdp->addTuple(GA_STORE_REAL32, GA_ATTRIB_POINT, sopattrname, 9);
    else if (value.IsHolding<VtArray<GfMatrix4f>>())
        attr = gdp->addTuple(GA_STORE_REAL32, GA_ATTRIB_POINT, sopattrname, 16);

    // Quaternions
    else if (value.IsHolding<VtArray<GfQuath>>())
        attr = gdp->addTuple(GA_STORE_REAL16, GA_ATTRIB_POINT, sopattrname, 4);
    else if (value.IsHolding<VtArray<GfQuatf>>())
        attr = gdp->addTuple(GA_STORE_REAL32, GA_ATTRIB_POINT, sopattrname, 4);
    else if (value.IsHolding<VtArray<GfQuatd>>())
        attr = gdp->addTuple(GA_STORE_REAL64, GA_ATTRIB_POINT, sopattrname, 4);

    // Doubles
    else if (value.IsHolding<VtArray<GfVec4d>>())
        attr = gdp->addTuple(GA_STORE_REAL64, GA_ATTRIB_POINT, sopattrname, 4);
    else if (value.IsHolding<VtArray<GfVec3d>>())
        attr = gdp->addTuple(GA_STORE_REAL64, GA_ATTRIB_POINT, sopattrname, 3);
    else if (value.IsHolding<VtArray<GfVec2d>>())
        attr = gdp->addTuple(GA_STORE_REAL64, GA_ATTRIB_POINT, sopattrname, 2);
    else if (value.IsHolding<VtArray<fpreal64>>())
        attr = gdp->addTuple(GA_STORE_REAL64, GA_ATTRIB_POINT, sopattrname, 1);
    else if (value.IsHolding<VtArray<GfMatrix2d>>())
        attr = gdp->addTuple(GA_STORE_REAL64, GA_ATTRIB_POINT, sopattrname, 4);
    else if (value.IsHolding<VtArray<GfMatrix3d>>())
        attr = gdp->addTuple(GA_STORE_REAL64, GA_ATTRIB_POINT, sopattrname, 9);
    else if (value.IsHolding<VtArray<GfMatrix4d>>())
        attr = gdp->addTuple(GA_STORE_REAL64, GA_ATTRIB_POINT, sopattrname, 16);

    // Halfs
    else if (value.IsHolding<VtArray<GfVec4h>>())
        attr = gdp->addTuple(GA_STORE_REAL16, GA_ATTRIB_POINT, sopattrname, 4);
    else if (value.IsHolding<VtArray<GfVec3h>>())
        attr = gdp->addTuple(GA_STORE_REAL16, GA_ATTRIB_POINT, sopattrname, 3);
    else if (value.IsHolding<VtArray<GfVec2h>>())
        attr = gdp->addTuple(GA_STORE_REAL16, GA_ATTRIB_POINT, sopattrname, 2);
    else if (value.IsHolding<VtArray<GfHalf>>())
        attr = gdp->addTuple(GA_STORE_REAL16, GA_ATTRIB_POINT, sopattrname, 1);

    // Integers
    else if (value.IsHolding<VtArray<GfVec4i>>())
        attr = gdp->addTuple(GA_STORE_INT32, GA_ATTRIB_POINT, sopattrname, 4);
    else if (value.IsHolding<VtArray<GfVec3i>>())
        attr = gdp->addTuple(GA_STORE_INT32, GA_ATTRIB_POINT, sopattrname, 3);
    else if (value.IsHolding<VtArray<GfVec2i>>())
        attr = gdp->addTuple(GA_STORE_INT32, GA_ATTRIB_POINT, sopattrname, 2);
    else if (value.IsHolding<VtArray<int>>())
        attr = gdp->addTuple(GA_STORE_INT32, GA_ATTRIB_POINT, sopattrname, 1);
    else if (value.IsHolding<VtArray<uint>>())
        attr = gdp->addTuple(GA_STORE_INT32, GA_ATTRIB_POINT, sopattrname, 1);
    else if (value.IsHolding<VtArray<int64>>())
        attr = gdp->addTuple(GA_STORE_INT64, GA_ATTRIB_POINT, sopattrname, 1);
    else if (value.IsHolding<VtArray<uint64>>())
        attr = gdp->addTuple(GA_STORE_INT64, GA_ATTRIB_POINT, sopattrname, 1);

    // Other Data Types
    else if (value.IsHolding<VtArray<bool>>())
        attr = gdp->addTuple(GA_STORE_INT8, GA_ATTRIB_POINT, sopattrname, 1);
    else if (value.IsHolding<VtArray<std::string>>())
        attr = gdp->addStringTuple(GA_ATTRIB_POINT, sopattrname, 1);
    else if (value.IsHolding<VtArray<SdfAssetPath>>())
        attr = gdp->addStringTuple(GA_ATTRIB_POINT, sopattrname, 1);
    else if (value.IsHolding<VtArray<TfToken>>())
        attr = gdp->addStringTuple(GA_ATTRIB_POINT, sopattrname, 1);
    else if (value.IsHolding<VtArray<SdfPathExpression>>())
        attr = gdp->addStringTuple(GA_ATTRIB_POINT, sopattrname, 1);

    if (attr)
        attr->setTypeInfo(ga_type_info);
}

void _setPrimvars(GU_Detail *gdp,
                  HUSD_AutoReadLock &readlock,
                  const UT_StringRef &primpath,
                  const HUSD_TimeCode &timecode,
                  const GA_Range &range,
                  const HUSDPointInstancerParms &parms,
                  const IdToIdxMap &idToIdxMap)
{
    HUSD_Info         info(readlock);
    UT_ArrayStringSet primvarnames;
    UT_StringRef      sop_attrname;

    UsdStageRefPtr stage = readlock.constData()->stage();
    UsdPrim        prim = stage->GetPrimAtPath(HUSDgetSdfPath(primpath));
    if (!prim)
        return;

    info.getPrimvarNames(primpath, primvarnames, true);
    UsdGeomPrimvarsAPI primvarsapi(prim);
    if (!primvarsapi)
        return;

    const std::vector<UsdGeomPrimvar> primvars =
                                    primvarsapi.GetPrimvarsWithAuthoredValues();

    // Pass 1 (main thread): pre-create the SOP attribute for each primvar so
    // the parallel population pass below isn't doing worker-thread
    // addAttribute (which doesn't reliably preserve GA_Defaults). This
    // function is now called serially after the outer UTparallelInvoke in
    // _updateTransformAttrs, so we are guaranteed to be on the main thread
    // here.
    {
        UT_StringRef primvarname;
        for (const UsdGeomPrimvar &primvar : primvars)
        {
            primvarname = primvar.GetPrimvarName().GetString();
            if (primvarname.multiMatch(parms.myPrimvarsFilter))
                _createSopAttrForPrimvar(gdp, primvar, timecode);
        }
    }

    // Pass 2 (parallel): populate values. The addTuple calls inside
    // _copyUsdPrimvarToSopPointAttr now hit existing attributes, so they
    // are no-op finds rather than worker-thread creates.
    UT_BlockedRange<exint> blockedrange(0, primvars.size());
    UTparallelFor(blockedrange, [&](const UT_BlockedRange<exint> &subrange)
    {
        UT_StringRef primvarname;
        for (exint idx = subrange.begin(), end = subrange.end();
             idx < end;
             ++idx)
        {
            const UsdGeomPrimvar &primvar = primvars[idx];
            primvarname = primvar.GetPrimvarName().GetString();
            if (primvarname.multiMatch(parms.myPrimvarsFilter))
                _copyUsdPrimvarToSopPointAttr(gdp, range, primvar,
                                              timecode, idToIdxMap);
        }
    });
}

// -----------------------------------------------------------------------------
// GEO -> USD
// -----------------------------------------------------------------------------
template <typename OrientationType = UT_QuaternionH>
void _updateTransformAttrs(HUSD_AutoWriteLock &writelock,
                           const GU_Detail *gdp,
                           const GA_Range &primrange,
                           const UT_StringRef &primpath,
                           const HUSD_TimeCode &timecode,
                           const HUSDpointInstancerOffsetMap::OffsetMap &offsetmap,
                           const HUSD_PointInstancer::SopToUsdConfig &config,
                           husd_UsdWriteQueue &pending)
{
    // TODO: need to deal with usdxform deatil attribute as well.
    bool simplexforms = false;
    if (simplexforms)
    {
        if (config.mySetPositions)
            _copySopAttrToUsdAttr<UT_Vector3>(writelock, gdp, primrange, primpath,
                GA_Names::P, timecode, offsetmap, config,
                config.myExistingCopyStyle, pending);
        if (config.mySetScales)
        {
            UT_Vector3F default_scale{1,1,1};
            _copySopAttrToUsdAttr<UT_Vector3>(writelock, gdp, primrange, primpath,
                GA_Names::scale, timecode, offsetmap, config,
                config.myExistingCopyStyle, pending, &default_scale);
        }
        if (config.mySetOrientations)
            _copySopAttrToUsdAttr<UT_QuaternionH>(writelock, gdp, primrange, primpath,
                GA_Names::orient, timecode, offsetmap, config,
                config.myExistingCopyStyle, pending);
    }
    else
    {
        HUSD_GetAttributes    getattrs(writelock);
        HUSD_Info             info(writelock);

        UT_Array<UT_Vector3D> positions;
        UT_Array<UT_Vector3D> scales;
        UT_Array<OrientationType> orientations;

        GA_AttributeInstanceMatrix inst_matrix;
        inst_matrix.initialize(gdp->pointAttribs());

        bool author_positions = true;
        bool author_scales = true;
        bool author_orientations = true;

        HUSD_PointInstancerCopyStyle positions_copystyle = offsetmap.myCopyStyle;
        HUSD_PointInstancerCopyStyle scales_copystyle = offsetmap.myCopyStyle;
        HUSD_PointInstancerCopyStyle orientations_copystyle = offsetmap.myCopyStyle;
        if (!config.mySetPositions)
            positions_copystyle = HUSD_PointInstancerCopyStyle::Update;
        if (!config.mySetScales)
            scales_copystyle = HUSD_PointInstancerCopyStyle::Update;
        if (!config.mySetOrientations)
            orientations_copystyle = HUSD_PointInstancerCopyStyle::Update;

        if (positions_copystyle == HUSD_PointInstancerCopyStyle::Sparse ||
            positions_copystyle == HUSD_PointInstancerCopyStyle::Update)
        {
            getattrs.getAttribute(primpath,
                                  HUSD_Constants::getAttributePointPositions(),
                                  positions,
                                  timecode);
            if (positions.isEmpty())
            {
                if (positions_copystyle != HUSD_PointInstancerCopyStyle::Update)
                {
                    positions.setSize(offsetmap.myOriginalNumInstances);
                }
                else
                {
                    // if copystyle is update, but there are no existing positions
                    // we don't want to author anything.
                    author_positions = false;
                }
            }
            positions.setSize(positions.size() + offsetmap.myNewIds.size());
        }
        else
            positions.setSize(primrange.getEntries());

        if (scales_copystyle == HUSD_PointInstancerCopyStyle::Sparse ||
            scales_copystyle == HUSD_PointInstancerCopyStyle::Update)
        {
            getattrs.getAttribute(primpath,
                                  HUSD_Constants::getAttributePointScales(),
                                  scales,
                                  timecode);
            if (scales.isEmpty())
            {
                if (scales_copystyle != HUSD_PointInstancerCopyStyle::Update)
                {
                    scales.appendMultiple(theDefaultScale,
                                          offsetmap.myOriginalNumInstances);
                }
                else
                {
                    // if copystyle is update, but there are no existing scales
                    // we don't want to author anything.
                    author_scales = false;
                }
            }
            scales.appendMultiple(theDefaultScale,
                                  offsetmap.myNewIds.size());
        }
        else
            scales.appendMultiple(theDefaultScale,
                                  primrange.getEntries());

        if (orientations_copystyle == HUSD_PointInstancerCopyStyle::Sparse ||
            orientations_copystyle == HUSD_PointInstancerCopyStyle::Update)
        {
            getattrs.getAttribute(primpath,
                                  HUSD_Constants::getAttributePointOrientations(),
                                  orientations,
                                  timecode);
            if (orientations.isEmpty())
            {
                if (orientations_copystyle != HUSD_PointInstancerCopyStyle::Update)
                {
                    orientations.setSize(offsetmap.myOriginalNumInstances);
                }
                else
                {
                    // if copystyle is update, but there are no existing orientations
                    // we don't want to author anything.
                    author_orientations = false;
                }
            }
            orientations.setSize(orientations.size() + offsetmap.myNewIds.size());
        }
        else
            orientations.setSize(primrange.getEntries());

        UTparallelFor(GA_SplittableRange(primrange),
            [&] (const GA_SplittableRange &splitrange)
            {
                UT_Matrix4D    temp_xform4d;
                UT_Matrix3D    temp_xform3d;
                UT_Vector3     inst_position;
                UT_QuaternionF inst_orient;
                UT_Vector3     inst_scales;
                for (const GA_Offset &ptoff : splitrange)
                {
                    inst_matrix.getMatrix(temp_xform4d, gdp->getPos3(ptoff), ptoff);
                    temp_xform4d.getTranslates(inst_position);

                    temp_xform3d = temp_xform4d;
                    inst_orient.updateFromArbitraryMatrix(temp_xform3d);
                    if (inst_matrix.hasScales())
                        temp_xform3d.extractScales(inst_scales);
                    else
                        inst_scales = theDefaultScale;

                    if (positions_copystyle != HUSD_PointInstancerCopyStyle::Update)
                        positions[offsetmap.getIdx(ptoff)] = inst_position;
                    if (scales_copystyle != HUSD_PointInstancerCopyStyle::Update)
                        scales[offsetmap.getIdx(ptoff)] = inst_scales;
                    if (orientations_copystyle != HUSD_PointInstancerCopyStyle::Update)
                        orientations[offsetmap.getIdx(ptoff)] = inst_orient;
                }
            });

        {
            bool checkmissing = positions_copystyle != HUSD_PointInstancerCopyStyle::Overwrite &&
                                config.myMissingPointsPolicy == HUSD_PointInstancerMissingPointsPolicy::Remove;
            UT_Array<UT_Vector3D>  updatedpositions;
            updatedpositions.setCapacity(positions.size());
            for (exint idx = 0, end = positions.size(); idx < end; ++idx)
                if (!offsetmap.isDeleted(idx) &&
                    (!checkmissing || !offsetmap.isMissing(idx)))
                    updatedpositions.append(positions[idx]);
            positions = std::move(updatedpositions);
        }

        {
            bool checkmissing = scales_copystyle != HUSD_PointInstancerCopyStyle::Overwrite &&
                                config.myMissingPointsPolicy == HUSD_PointInstancerMissingPointsPolicy::Remove;
            UT_Array<UT_Vector3D>  updatedscales;
            updatedscales.setCapacity(scales.size());
            for (exint idx = 0, end = scales.size(); idx < end; ++idx)
                if (!offsetmap.isDeleted(idx) &&
                    (!checkmissing || !offsetmap.isMissing(idx)))
                    updatedscales.append(scales[idx]);
            scales = std::move(updatedscales);
        }

        {
            bool checkmissing = orientations_copystyle != HUSD_PointInstancerCopyStyle::Overwrite &&
                                config.myMissingPointsPolicy == HUSD_PointInstancerMissingPointsPolicy::Remove;
            UT_Array<OrientationType>  updatedorientations;
            updatedorientations.setCapacity(orientations.size());
            for (exint idx = 0, end = orientations.size(); idx < end; ++idx)
                if (!offsetmap.isDeleted(idx) &&
                    (!checkmissing || !offsetmap.isMissing(idx)))
                    updatedorientations.append(orientations[idx]);
            orientations = std::move(updatedorientations);
        }

        if (author_positions)
            pending.appendAttribute(primpath,
                HUSD_Constants::getAttributePointPositions(),
                timecode, UT_StringHolder::theEmptyString,
                std::move(positions));
        if (author_scales)
            pending.appendAttribute(primpath,
                HUSD_Constants::getAttributePointScales(),
                timecode, UT_StringHolder::theEmptyString,
                std::move(scales));
        if (author_orientations)
            pending.appendAttribute(primpath,
                HUSD_Constants::getAttributePointOrientations(),
                timecode, UT_StringHolder::theEmptyString,
                std::move(orientations));
    }
}


void _updateProtoIndices(HUSD_AutoWriteLock &writelock,
                         const GU_Detail *gdp,
                         const GA_Range &primrange,
                         const UT_StringRef &primpath,
                         const HUSD_TimeCode &timecode,
                         bool  existing,
                         const UT_StringArray &protopaths,
                         const HUSDpointInstancerOffsetMap::OffsetMap &offsetmap,
                         const HUSD_PointInstancer::SopToUsdConfig &config,
                         HUSD_PointInstancerCopyStyle copystyle,
                         husd_UsdWriteQueue &pending)
{
    UT_Array<exint> protoindices;
    HUSD_PointInstancerProtoIndexSource protoindexsrc;
    UT_String intattrname;
    UT_String strattrname;
    float randomseed;

    if (existing)
    {
        if (config.myExistingProtoSource == HUSD_PointInstancerProtoIndexSource::None)
            copystyle = HUSD_PointInstancerCopyStyle::Update;

        if (copystyle == HUSD_PointInstancerCopyStyle::Sparse ||
            copystyle == HUSD_PointInstancerCopyStyle::Update)
        {
            HUSD_GetAttributes getattrs(writelock);
            getattrs.getAttribute(primpath,
                                  HUSD_Constants::getAttributePointProtoIndices(),
                                  protoindices,
                                  timecode);
            if (protoindices.isEmpty())
            {
                // this is a new primvar, so we need to populate some empty values.
                // the array should have one entry for each existing instance
                HUSD_Info info(writelock);
                protoindices.setSize(offsetmap.myOriginalNumInstances);
            }
            // make room for new ids / instances
            protoindices.setSize(protoindices.size() + offsetmap.myNewIds.size());
        }
        else
            protoindices.setSize(primrange.getEntries());

        protoindexsrc = config.myExistingProtoSource;
        intattrname = config.myExistingIntAttrName;
        strattrname = config.myExistingStringAttrName;
        randomseed = config.myExistingRandomSeed;
    }
    else
    {
        protoindexsrc = config.myNewProtoSource;
        intattrname = config.myNewIntAttrName;
        strattrname = config.myNewStringAttrName;
        randomseed = config.myNewRandomSeed;

        if (protoindexsrc != HUSD_PointInstancerProtoIndexSource::None)
            protoindices.setSize(primrange.getEntries());
    }

    if (protoindexsrc != HUSD_PointInstancerProtoIndexSource::None)
    {
        if (config.myUseRootAsPrototype &&
            (!existing ||
            config.myExisitingPrototypeRelMode == HUSD_PointInstancerExistingProtoRelationshipMode::Overwrite))
        {
            UTparallelFor(GA_SplittableRange(primrange),
                [&](const GA_SplittableRange &splitrange)
                {
                    for (const GA_Offset& ptoff : splitrange)
                        protoindices[offsetmap.getIdx(ptoff)] = 0;
                });
        }
        else if (protoindexsrc == HUSD_PointInstancerProtoIndexSource::Random)
        {
            UTparallelFor(GA_SplittableRange(primrange),
               [&](const GA_SplittableRange &splitrange)
               {
                   for (const GA_Offset& ptoff : splitrange)
                       protoindices[offsetmap.getIdx(ptoff)] =
                                    husdRandomProto(ptoff,
                                                    randomseed,
                                                    protopaths.size());
               });
        }
        else if (protoindexsrc == HUSD_PointInstancerProtoIndexSource::IntAttribute)
        {
            GA_ROHandleI inthandle = gdp->findPointAttribute(intattrname);
            if (inthandle.isValid())
            {
                UTparallelFor(GA_SplittableRange(primrange),
                   [&](const GA_SplittableRange &splitrange)
                   {
                       for (const GA_Offset& ptoff : splitrange)
                           protoindices[offsetmap.getIdx(ptoff)] = inthandle.get(ptoff);
                   });
            }
            else
                HUSD_ErrorScope::addError(HUSD_ERR_CANT_FIND_SOP_ATTR, intattrname);
        }
        else if (protoindexsrc == HUSD_PointInstancerProtoIndexSource::StrAttribute)
        {
            GA_ROHandleS strhandle = gdp->findPointAttribute(strattrname);
            if (strhandle.isValid())
            {
                // first build a map of unique sop strs -> proto indices
                const GA_AIFSharedStringTuple *tpl = strhandle->getAIFSharedStringTuple();
                UT_StringArray uniquestrs;
                UT_IntArray    handles;
                tpl->extractStrings(strhandle.getAttribute(), uniquestrs, handles);
                UT_StringMap<int> strmap;
                for (const UT_StringRef &str : uniquestrs)
                {
                    for (exint i = 0; i < protopaths.size(); ++i)
                    {
                        if (protopaths[i].endsWith(str))
                        {
                            strmap[str] = i;
                            break;
                        }
                    }
                }

                UTparallelFor(GA_SplittableRange(primrange),
                   [&](const GA_SplittableRange &splitrange)
                   {
                       UT_StringRef str;
                       for (const GA_Offset& ptoff : splitrange)
                       {
                           str = strhandle.get(ptoff);
                           protoindices[offsetmap.getIdx(ptoff)] = strmap[str];
                       }
                   });
            }
            else
                HUSD_ErrorScope::addError(HUSD_ERR_CANT_FIND_SOP_ATTR, strattrname);
        }
    }

    {
        bool checkmissing = copystyle != HUSD_PointInstancerCopyStyle::Overwrite &&
                                         config.myMissingPointsPolicy == HUSD_PointInstancerMissingPointsPolicy::Remove;
        UT_Array<exint>  updatedvalues;
        updatedvalues.setCapacity(protoindices.size());
        for (exint idx = 0, end = protoindices.size(); idx < end; ++idx)
            if (!offsetmap.isDeleted(idx) &&
                (!checkmissing || !offsetmap.isMissing(idx)))
                updatedvalues.append(protoindices[idx]);
        protoindices = std::move(updatedvalues);
    }

    if (!protoindices.isEmpty())
        pending.appendAttribute(primpath,
                                HUSD_Constants::getAttributePointProtoIndices(),
                                timecode, UT_StringHolder::theEmptyString,
                                std::move(protoindices));
}

void _updateIds(HUSD_AutoWriteLock &writelock,
                const GU_Detail *gdp,
                const GA_Range &primrange,
                const UT_StringRef &primpath,
                const HUSD_TimeCode &timecode,
                const HUSDpointInstancerOffsetMap::OffsetMap &offsetmap,
                const HUSD_PointInstancer::SopToUsdConfig &config,
                husd_UsdWriteQueue &pending)
{
    UT_Array<exint> ids;
    HUSD_PointInstancerCopyStyle copystyle = offsetmap.myCopyStyle;

    if (!config.mySetIds)
        copystyle = HUSD_PointInstancerCopyStyle::Update;

    if (copystyle != HUSD_PointInstancerCopyStyle::Overwrite)
    {
        HUSD_GetAttributes    getattrs(writelock);
        getattrs.getAttribute(primpath,
                              HUSD_Constants::getAttributePointIds(),
                              ids,
                              timecode);

        if (ids.isEmpty() && !config.mySetIds)
            return;

        if (ids.isEmpty())
        {
            ids.setSize(offsetmap.myOriginalNumInstances);
            for (exint idx = 0; idx < offsetmap.myOriginalNumInstances; ++idx)
            {
                ids[idx] = idx;
            }
        }
        ids.concat(offsetmap.myNewIds);

        bool checkmissing = config.myMissingPointsPolicy == HUSD_PointInstancerMissingPointsPolicy::Remove;
        UT_Array<exint> updatedids;
        for (exint idx = 0, end = ids.size(); idx < end; ++idx)
        {
            if (!offsetmap.isDeleted(idx) &&
                (!checkmissing || !offsetmap.isMissing(idx)))
                updatedids.append(ids[idx]);
        }
        ids = std::move(updatedids);
    }
    else
    {
        // OVERWRITE (and config.mySetIds)
        GA_ROHandleI idshandle = gdp->findPointAttribute(GA_Names::id);
        if (idshandle.isValid())
        {
            ids.setSize(primrange.getEntries());
            UTparallelFor(GA_SplittableRange(primrange),
                [&] (const GA_SplittableRange &splitrange)
                {
                    for (GA_Offset ptoff : splitrange)
                        ids[offsetmap.getIdx(ptoff)] = idshandle.get(ptoff);
                });
        }
        else
        {
            ids.setSize(primrange.getEntries());
            for (exint id = 0, end = ids.size(); id < end; ++id)
            {
                ids[id] = id;
            }
        }
    }

    pending.appendAttribute(primpath, HUSD_Constants::getAttributePointIds(),
                            timecode, UT_StringHolder::theEmptyString,
                            std::move(ids));
}

void _updateInvisIds(HUSD_AutoWriteLock &writelock,
                     const GU_Detail *gdp,
                     const GA_Range &primrange,
                     const UT_StringRef &primpath,
                     const HUSD_TimeCode &timecode,
                     const HUSDpointInstancerOffsetMap::OffsetMap &offsetmap,
                     const HUSD_PointInstancer::SopToUsdConfig &config,
                     husd_UsdWriteQueue &pending)
{
    HUSD_PointInstancerCopyStyle copystyle = offsetmap.myCopyStyle;
    if (!config.mySetInvisIds)
        copystyle = HUSD_PointInstancerCopyStyle::Update;

    UT_Array<bool> invisidsmap;
    invisidsmap.appendMultiple(false, offsetmap.getMaxId()+1);

    UT_Array<exint> usdinvisids;
    if (copystyle != HUSD_PointInstancerCopyStyle::Overwrite)
    {
        HUSD_GetAttributes getattrs(writelock);
        getattrs.getAttribute(primpath,
                              HUSD_Constants::getAttributePointInvisibleIds(),
                              usdinvisids, timecode);
        for (const exint &id : usdinvisids)
        {
            if (id >= invisidsmap.size())
                invisidsmap.setSize(id+1);
            invisidsmap[id] = true;
        }
    }

    if (copystyle != HUSD_PointInstancerCopyStyle::Update)
    {
        GA_ROHandleS invisidshandle = gdp->findPointAttribute(
                                         theUsdVisibilityAttributeName.asRef());
        if (invisidshandle.isValid())
        {
            for (const GA_Offset &ptoff : primrange)
            {
                if (invisidshandle.get(ptoff) == theInvisibleName)
                    invisidsmap[offsetmap.getId(ptoff)] = true;
                else
                {
                    exint id = offsetmap.getId(ptoff);
                    if (id >= 0 && id < invisidsmap.size())
                        invisidsmap[id] = false;
                }
            }
        }
    }

    if (config.myMissingPointsPolicy == HUSD_PointInstancerMissingPointsPolicy::Invis)
        for (const exint &id : offsetmap.myDeletedIds) {
            if (id >= offsetmap.myDeletedIds.size())
                invisidsmap.setSize(id+1);
            invisidsmap[id] = true;
        }

    // Flatten invisidsmap
    UT_Array<exint> invis_ids;
    invis_ids.setCapacity(invisidsmap.size());
    bool checkmissing = copystyle != HUSD_PointInstancerCopyStyle::Overwrite &&
                        config.myMissingPointsPolicy == HUSD_PointInstancerMissingPointsPolicy::Remove;
    for (exint id = 0, end = invisidsmap.size(); id < end; ++id)
    {
        if (invisidsmap[id])
        {
            if (!checkmissing || !offsetmap.isMissing(id))
            {
                invis_ids.append(id);
            }
        }
    }

    if (copystyle == HUSD_PointInstancerCopyStyle::Overwrite ||
        !(usdinvisids.isEmpty() && invis_ids.isEmpty()))
    {
        // We only want to write invis ids if we're in OVERWRITE mode
        // or if there are invis ids to author
        invis_ids.sort();
        pending.appendAttribute(primpath,
                                HUSD_Constants::getAttributePointInvisibleIds(),
                                timecode, UT_StringHolder::theEmptyString,
                                std::move(invis_ids));
    }
}

} // namespace

bool HUSD_PointInstancer::copyUsdAttrsToGeoAttrs(
                              GU_Detail *gdp,
                              HUSD_AutoReadLock &readlock,
                              const HUSDPointInstancerParms &parms,
                              const UT_StringMap<UT_Array<exint>> &instancermap,
                              const HUSD_TimeCode &timecode)
{
    HUSD_Info          info(readlock);
    HUSD_GetAttributes getattrs(readlock);

    UT_Array<exint>    usd_ids;
    exint              numpoints;
    GA_Offset          start_offset;

    // First create all necessary sop attributes in the main thread. The
    // orient attribute is always created as REAL32 — USD half-precision
    // `orientations` values are promoted to float when read via
    // getAttributeArray, so a single REAL32 attribute serves both
    // `orientations` and `orientationsf` and avoids handle/storage mismatch
    // across mixed instancermaps.
    if (parms.myCreatePathAttribute)
        gdp->addStringTuple(GA_ATTRIB_POINT, GA_Names::path, 1);

    if (parms.myImportIds || parms.myImportVisibility)
        gdp->addIntTuple(GA_ATTRIB_POINT, GA_Names::id, 1, GA_Defaults(-1));

    if (parms.myImportVisibility)
        gdp->addStringTuple(GA_ATTRIB_POINT,
                            theUsdVisibilityAttributeName.asRef(),
                            1);

    if (parms.myImportOrientations)
    {
        GA_Attribute *orient = gdp->addFloatTuple(GA_ATTRIB_POINT,
                                                  GA_Names::orient, 4);
        orient->setTypeInfo(GA_TYPE_QUATERNION);
    }

    if (parms.myImportScales)
        gdp->addFloatTuple(GA_ATTRIB_POINT, GA_Names::scale, 3, theScaleDefault);

    if (parms.myImportAccelerations)
        gdp->addFloatTuple(GA_ATTRIB_POINT, GA_Names::accel, 3);

    if (parms.myImportVelocities)
        gdp->addFloatTuple(GA_ATTRIB_POINT, GA_Names::v, 3);

    if (parms.myImportAngularVelocities)
        gdp->addFloatTuple(GA_ATTRIB_POINT, GA_Names::w, 3, theScaleDefault);

    if (parms.myProtoSource == HUSD_PointInstancerSopProtoIndexSource::Attribute)
        gdp->addIntTuple(GA_ATTRIB_POINT, parms.myIntAttrName, 1,
                         GA_Defaults(0), nullptr, nullptr, GA_STORE_INT64);

    if (parms.myProtoSource == HUSD_PointInstancerSopProtoIndexSource::PrimName ||
        parms.myProtoSource == HUSD_PointInstancerSopProtoIndexSource::PrimPath)
        gdp->addStringTuple(GA_ATTRIB_POINT, parms.myStrAttrName, 1);

    for (const auto &instancer : instancermap)
    {
        const UT_StringHolder &primpath = instancer.first;
        const UT_Array<exint> &instance_ids = instancer.second;

        if (instance_ids.isEmpty())
            numpoints = info.getPointInstancerInstanceCount(primpath, timecode);
        else
            numpoints = instance_ids.size();

        if (numpoints == 0)
            continue;

        start_offset = gdp->appendPointBlock(numpoints);
        const GA_Range instancer_range = gdp->getPointRangeSlice(
                                                       exint(start_offset));

        IdToIdxMap id_to_idx_map(primpath, readlock, timecode, gdp, instancer_range, instance_ids);
        UT_Array<exint> instance_indices;
        id_to_idx_map.getIdxs(instance_ids, instance_indices);

        // Invoke all functions to set point attributes from various USD
        // attributes and primvars in parallel.
        UTparallelInvoke(true,
            [&] {
                if (parms.myTransformIntoWorldSpace)
                {
                    _setFromWorldXform(gdp, readlock, primpath, parms, timecode,
                                       instancer_range, id_to_idx_map);
                }
            },
            [&] {
                if (!parms.myTransformIntoWorldSpace && parms.myImportPositions)
                {
                    _setPointPositions(gdp, readlock, primpath, timecode,
                                       instancer_range, id_to_idx_map);
                }
            },
            [&] {
                if (!parms.myTransformIntoWorldSpace && parms.myImportScales)
                {
                    _copyUsdAttrToSopAttr<UT_Vector3F, GA_STORE_REAL32, 3>(gdp,
                    instancer_range, getattrs, primpath, timecode,
                    UsdGeomTokens->scales.GetString(), id_to_idx_map);
                }
            },
            [&] {
                if (!parms.myTransformIntoWorldSpace && parms.myImportOrientations)
                {
                    UT_StringRef usd_orient_attr;
                    if (info.hasAuthoredValueForProperty(primpath,
                                        UsdGeomTokens->orientations.GetString()))
                        usd_orient_attr = UsdGeomTokens->orientations.GetString();
                    else if (info.hasAuthoredValueForProperty(primpath,
                                        UsdGeomTokens->orientationsf.GetString()))
                        usd_orient_attr = UsdGeomTokens->orientationsf.GetString();

                    if (usd_orient_attr.isstring())
                    {
                        _copyUsdAttrToSopAttr<UT_Quaternion, GA_STORE_REAL32, 4>(
                            gdp, instancer_range, getattrs, primpath, timecode,
                            usd_orient_attr, id_to_idx_map);
                    }
                }
            },
            [&] {
                if (parms.myCreatePathAttribute)
                {
                    _setPointPaths(gdp, primpath, instancer_range);
                }
            },
            [&] {
                if (parms.myImportIds ||
                    parms.myImportVisibility)
                {
                    UT_Array<GA_Offset> id_to_ptoff_map;
                    _setPointIds(gdp, id_to_ptoff_map, readlock, primpath, timecode,
                                 instancer_range, id_to_idx_map, &instance_ids);
                    if (parms.myImportVisibility)
                    {
                        _setPointVisibility(gdp, readlock, primpath, timecode,
                                            id_to_ptoff_map);
                    }
                }
            },
            [&] {
                if (parms.myImportVelocities)
                {
                    _copyUsdAttrToSopAttr<UT_Vector3F, GA_STORE_REAL32, 3>(gdp,
                        instancer_range, getattrs, primpath, timecode,
                        UsdGeomTokens->velocities.GetString(), id_to_idx_map);
                }
            },
            [&] {
                if (parms.myImportAngularVelocities)
                {
                    _copyUsdAttrToSopAttr<UT_Vector3F, GA_STORE_REAL32, 3>(gdp,
                        instancer_range, getattrs, primpath, timecode,
                        UsdGeomTokens->angularVelocities.GetString(), id_to_idx_map);
                }
            },
            [&] {
                if (parms.myImportAccelerations)
                {
                    _copyUsdAttrToSopAttr<UT_Vector3F, GA_STORE_REAL32, 3>(gdp,
                        instancer_range, getattrs, primpath, timecode,
                        UsdGeomTokens->accelerations.GetString(), id_to_idx_map);
                }
            },
            [&] {
                if (parms.myProtoSource != HUSD_PointInstancerSopProtoIndexSource::None)
                {
                    _setPrototypeIndices(gdp, readlock, primpath, timecode,
                                         instancer_range, parms, id_to_idx_map);
                }
            }
        ); // UparallelForInvoke

        { // this seems to perform better on its own.
            createBoundingBoxGeoAttr(gdp, readlock, primpath, timecode, instancer_range,
                parms, instance_indices);
        }

        // Run primvar setup on the main thread. Its internal pre-pass needs
        // to do addAttribute serially.
        _setPrimvars(gdp, readlock, primpath, timecode, instancer_range,
                     parms, id_to_idx_map);
    }
    return true;
}

bool HUSD_PointInstancer::copyGeoAttrsToUsdAttrs(
        const GU_Detail *gdp,
        const GA_Range &range,
        HUSD_AutoWriteLock &writelock,
        const UT_StringRef &fallbackprimpath,
        const HUSD_TimeCode &timecode,
        UT_StringSet &primpaths,
        UT_StringSet &createdprimpaths,
        const SopToUsdConfig &config,
        const UT_StringMap<UT_StringArray> &prototypepathmap)
{
    // first off, build a map of GA_Offset -> primvar idx
    const HUSDpointInstancerOffsetMap map(gdp, range, writelock, timecode,
                                          config.myExistingCopyStyle,
                                          config.myFallbackPrimpath,
                                          createdprimpaths);
    for (const auto &data : map)
    {
        husd_UsdWriteQueue pending;

        UTparallelInvoke(true,
        [&]{
            const GA_Range primrange = gdp->getPointRange(data.second.myGroup.get());
            if (config.mySetAccelerations)
            {
                _copySopAttrToUsdAttr<UT_Vector3>(writelock, gdp, primrange, data.first,
                    GA_Names::accel, timecode, data.second, config,
                    config.myExistingCopyStyle, pending);
            }
            else
            {
                _copySopAttrToUsdAttr<UT_Vector3>(writelock, gdp, primrange, data.first,
                    GA_Names::accel, timecode, data.second, config,
                    HUSD_PointInstancerCopyStyle::Update, pending);
            }
        },
        [&]{
            const GA_Range primrange = gdp->getPointRange(data.second.myGroup.get());
            if (config.mySetVelocities)
            {
                _copySopAttrToUsdAttr<UT_Vector3>(writelock, gdp, primrange, data.first,
                    GA_Names::v, timecode, data.second, config,
                    config.myExistingCopyStyle, pending);
            }
            else
            {
                _copySopAttrToUsdAttr<UT_Vector3>(writelock, gdp, primrange, data.first,
                    GA_Names::v, timecode, data.second, config,
                    HUSD_PointInstancerCopyStyle::Update, pending);
            }
        },
        [&]{
            const GA_Range primrange = gdp->getPointRange(data.second.myGroup.get());
            if (config.mySetAngularVelocities)
            {
                _copySopAttrToUsdAttr<UT_Vector3>(writelock, gdp, primrange, data.first,
                    GA_Names::w, timecode, data.second, config,
                    config.myExistingCopyStyle, pending);
            }
            else
            {
                _copySopAttrToUsdAttr<UT_Vector3>(writelock, gdp, primrange, data.first,
                    GA_Names::w, timecode, data.second, config,
                    HUSD_PointInstancerCopyStyle::Update, pending);
            }
        },
        [&]{
            const GA_Range primrange = gdp->getPointRange(data.second.myGroup.get());
            // Use orientationsf if it already exists, otherwise use
            // orientations
            HUSD_Info info(writelock);
            if (info.hasAuthoredValueForProperty(data.first,
                              HUSD_Constants::getAttributePointOrientationsF()))
                _updateTransformAttrs<UT_QuaternionF>(writelock, gdp, primrange,
                    data.first, timecode, data.second, config, pending);
            else
                _updateTransformAttrs(writelock, gdp, primrange, data.first,
                    timecode, data.second, config, pending);
        },
        [&]{
            const GA_Range primrange = gdp->getPointRange(data.second.myGroup.get());
            HUSD_Info info(writelock);
            UT_ArrayStringSet primvars;
            info.getPrimvarNames(data.first, primvars);
            UT_StringArray updatePrimvars;

            auto &&attrs = gdp->pointAttribs();
            UT_Array<const GA_Attribute*> copyattribs;
            UT_StringArray copyattrnames;
            copyattribs.setCapacity(attrs.entries());
            for (auto &&attrib : attrs)
                if (attrib->getName().multiMatch(config.myAttributePattern))
                {
                    // in addition to the supplied pattern to import primvars,
                    // we also want to skip creating primvars for the custom
                    // protoindices attributes.
                    if (attrib->getName() == config.myExistingIntAttrName ||
                        attrib->getName() == config.myExistingStringAttrName ||
                        attrib->getName() == config.myNewIntAttrName ||
                        attrib->getName() == config.myNewStringAttrName)
                        continue;
                    copyattribs.append(attrib);
                    copyattrnames.append(attrib->getName());
                }

            // Update non-matching primvars.  This will ensure that any
            // points that were added / deleted are handled correctly for all
            // primvars (even those not being edited directly via SOP attrs)
            UT_Array<UT_StringRef> updatePrimvarNames;
            for (const UT_StringRef& primvar : primvars)
            {
                UT_StringRef sopname = husdGetSopAttrName(primvar);
                if (copyattrnames.find(sopname) == -1)
                    updatePrimvarNames.append(sopname);
            }

            UT_BlockedRange<exint> updaterange(0, updatePrimvarNames.size());
            UTparallelFor(updaterange,
                [&](const UT_BlockedRange<exint> &subrange)
                {
                    for (exint idx = subrange.begin(), end = subrange.end();
                         idx < end;
                         ++idx)
                    {
                        copySopAttrToUsdPrimvar(writelock, gdp, primrange,
                                                data.first,
                                                updatePrimvarNames[idx],
                                                timecode, false, data.second,
                                                config, pending, true);
                    }

                });

            // Update matching primvars from SOPs.
            UT_BlockedRange<exint> blockedrange(0, copyattribs.size());
            UTparallelFor(blockedrange,
                [&](const UT_BlockedRange<exint> &subrange)
                {
                    const GA_Attribute *attr;
                    for (exint idx = subrange.begin(), end = subrange.end();
                         idx < end;
                         ++idx)
                    {
                        attr = copyattribs[idx];
                        copySopAttrToUsdPrimvar(writelock, gdp, primrange,
                                                data.first, attr->getName(),
                                                timecode, false, data.second,
                                                config, pending);
                    }
                });
        },
        [&]{
            const GA_Range primrange = gdp->getPointRange(data.second.myGroup.get());
            UT_StringArray default_protoprims;
            UT_StringArray protoprims = prototypepathmap.get(data.first,
                                                             default_protoprims);
            _updateProtoIndices(writelock, gdp, primrange, data.first, timecode,
                                !createdprimpaths.contains(data.first),
                                protoprims, data.second, config,
                                config.myExistingCopyStyle, pending);
        },
        [&]{
            const GA_Range primrange = gdp->getPointRange(data.second.myGroup.get());
            _updateIds(writelock, gdp, primrange, data.first, timecode,
                data.second, config, pending);
        },
        [&]{
            const GA_Range primrange = gdp->getPointRange(data.second.myGroup.get());
            _updateInvisIds(writelock, gdp, primrange, data.first, timecode,
                data.second, config, pending);
        });

        HUSD_SetAttributes setattrs(writelock);
        pending.writeAll(setattrs);
    }

    return true;
}

bool
HUSD_PointInstancer::createBoundingBoxGeoAttr(GU_Detail *gdp,
                               HUSD_AutoReadLock &readlock,
                               const UT_StringRef    &primpath,
                               const HUSD_TimeCode   &timecode,
                               const GA_Range       &range,
                               const HUSDPointInstancerParms &parms,
                               const UT_Array<exint> &indices)
{
    if (!parms.myImportBoundingBoxesAsAttr &&
        !parms.myImportBoundingBoxesAsPacked)
        return true;

    const HUSD_Info info(readlock);
    GA_RWHandleF boundsAttr = gdp->addFloatTuple(GA_ATTRIB_POINT,
                                                parms.myImportBoundingBoxesAttr,
                                                6);

    bool            applyPrimXform = parms.myTransformIntoWorldSpace;
    UT_Array<exint> lookup_instances;
    exint           numpoints = indices.size();

    if (numpoints == 0)
    {
        // todo: if this is by id, then we'll need to check for ids, etc first.
        lookup_instances.setSize(info.getPointInstancerInstanceCount(
                                     primpath, timecode));
        for (exint i = 0, end = lookup_instances.size(); i < end; ++i)
            lookup_instances[i] = i;
        numpoints = lookup_instances.size();
    }
    else
        lookup_instances = indices;

    // Compute all BBoxes for all necessary instances.
    const UsdStageRefPtr  stage = readlock.constData()->stage();
    TfTokenVector         purpose_tokens{parms.myImportBoundingBoxesPurposes.begin(),
                                        parms.myImportBoundingBoxesPurposes.end()};
    std::vector<GfBBox3d> bboxes(lookup_instances.size());
    UsdGeomPointInstancer pi(stage->GetPrimAtPath(HUSDgetSdfPath(primpath)));
    UsdGeomBBoxCache     *bbox_cache = new UsdGeomBBoxCache(HUSDgetUsdTimeCode(timecode),
                                                            purpose_tokens);

    bbox_cache->ComputePointInstanceUntransformedBounds(pi,
                                                        lookup_instances.data(),
                                                        numpoints,
                                                        bboxes.data());

    // Set SOP Values in parallel
    const UT_Matrix4D &xform = info.getWorldXform(primpath, timecode);
    const GA_Index    &start_index = range.begin().getIndex();
    {
        UTparallelFor(
            GA_SplittableRange(range),
            [&](const GA_SplittableRange &split_range)
            {
                UT_BoundingBoxD  bbox;
                exint            idx;
                for (const GA_Offset &ptoff : split_range)
                {
                    idx = gdp->pointIndex(ptoff)-start_index;
                    bboxConvert(bboxes[idx], bbox);
                    if (applyPrimXform)
                        bbox.transform(xform);

                    if (parms.myImportBoundingBoxesAsAttr)
                    {
                        int j = -1;
                        for (fpreal64 d : bbox)
                            boundsAttr.set(ptoff, ++j, d);
                    }

                    // if (parms.myImportBoundingBoxesAsPacked)
                    // {
                    //     if (idx%100000 == 0)
                    //         UTdebugPrint("idx", idx);
                    //     // add packed prims
                    //     GU_Detail       *geo_to_pack = new GU_Detail;
                    //     UT_Vector3 half_size = bbox.size()/2.0;
                    //     geo_to_pack->cube(-half_size.x(), half_size.x(),
                    //                       -half_size.y(), half_size.y(),
                    //                       -half_size.z(), half_size.z());
                    //     GU_DetailHandle gdh;
                    //     gdh.allocateAndSet(geo_to_pack, true);
                    //     GU_PackedGeometry::packGeometry(*gdp, gdh, ptoff);
                    // }
                }
            });
    }
    delete bbox_cache;
    return true;
}

bool
HUSDprototypeIsTimeVarying(const HUSD_AutoAnyLock &lock,
                           const UT_StringRef &prototype_path)
{
    auto data = lock.constData();
    if (!data || !data->isStageValid())
        return false;

    auto stage = data->stage();
    if (!stage)
        return false;

    UsdPrim root = stage->GetPrimAtPath(HUSDgetSdfPath(prototype_path));
    if (!root)
        return false;

    for (const UsdPrim &p : UsdPrimRange(root))
    {
        if (UsdGeomXformable xf{p})
        {
            bool reset = false;
            for (const UsdGeomXformOp &op : xf.GetOrderedXformOps(&reset))
            {
                if (op.GetAttr().ValueMightBeTimeVarying())
                    return true;
            }
        }
        if (UsdGeomBoundable b{p})
        {
            if (b.GetExtentAttr().ValueMightBeTimeVarying())
                return true;
        }
        if (UsdGeomPointBased pb{p})
        {
            if (pb.GetPointsAttr().ValueMightBeTimeVarying())
                return true;
        }
        if (UsdGeomImageable img{p})
        {
            if (img.GetVisibilityAttr().ValueMightBeTimeVarying() ||
                img.GetPurposeAttr().ValueMightBeTimeVarying())
                return true;
        }
        if (UsdGeomPointInstancer pi{p})
        {
            if (pi.GetPositionsAttr().ValueMightBeTimeVarying() ||
                pi.GetOrientationsAttr().ValueMightBeTimeVarying() ||
                pi.GetScalesAttr().ValueMightBeTimeVarying() ||
                pi.GetProtoIndicesAttr().ValueMightBeTimeVarying())
                return true;
        }
    }
    return false;
}
