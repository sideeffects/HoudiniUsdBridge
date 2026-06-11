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

#ifndef __HUSD_PointInstancer_h__
#define __HUSD_PointInstancer_h__

#include "HUSD_API.h"
#include "HUSD_DataHandle.h"
#include "HUSD_Info.h"
#include "HUSD_TimeCode.h"

#include <GA/GA_Types.h>
#include <GU/GU_Detail.h>
#include <UT/UT_StringHolder.h>

struct HUSDPointInstancerParms;

enum class HUSD_PointInstancerMissingPointsPolicy
{
    None,
    Remove,
    Invis
};

enum class HUSD_PointInstancerMissingPrimvarsPolicy
{
    IgnorePrimvar,
    CreatePrimvar
};

enum class HUSD_PointInstancerExistingProtoRelationshipMode
{
    Preserve,
    Overwrite,
    Append
};

enum class HUSD_PointInstancerSopProtoIndexSource
{
    None,
    Attribute,
    PrimName,
    PrimPath
};

enum class HUSD_PointInstancerProtoIndexSource
{
    None,
    Random,
    IntAttribute,
    StrAttribute,
};

enum class HUSD_PointInstancerCopyStyle
{
    Invalid,
    Overwrite,
    Sparse,
    Update
  };

enum class HUSD_PointInstancerXformImportType
{
    None,
    World
};

struct HUSD_PointInstancerSopToUsdConfig
{
    UT_String    myFallbackPrimpath;
    UT_String    myNewPrimKind;
    UT_String    myNewPrimSpec;
    UT_String    myNewPrimParentType;

    UT_String                        myExistingPrimitives;
    HUSD_PointInstancerCopyStyle      myExistingCopyStyle;
    HUSD_PointInstancerExistingProtoRelationshipMode   myExisitingPrototypeRelMode;

    UT_String               mySopPath;
    UT_String               myPointGroup;
    UT_String               myShowLopStage;
    HUSD_PointInstancerMissingPointsPolicy myMissingPointsPolicy;
    bool                    mySetIds;
    bool                    mySetInvisIds;
    bool                    mySetPositions;
    bool                    mySetOrientations;
    bool                    mySetScales;
    bool                    mySetAccelerations;
    bool                    mySetVelocities;
    bool                    mySetAngularVelocities;
    UT_String               myAttributePattern;
    UT_String               myIndexedPrimvarsPattern;
    HUSD_PointInstancerMissingPrimvarsPolicy myMissingPrimvarsPolicy;
    UT_StringArray          myCommonPrimvars;


    bool         myUseRootAsPrototype;

    HUSD_PointInstancerProtoIndexSource  myNewProtoSource;
    float             myNewRandomSeed;
    UT_String         myNewIntAttrName;
    UT_String         myNewStringAttrName;

    HUSD_PointInstancerProtoIndexSource  myExistingProtoSource;
    float             myExistingRandomSeed;
    UT_String         myExistingIntAttrName;
    UT_String         myExistingStringAttrName;

    bool              myWarnOnSkippedInstances;
};

class husd_UsdWriteQueue;

class HUSD_API HUSD_PointInstancerSampleData
{
public:
    HUSD_PointInstancerSampleData(const HUSD_TimeCode &timecode,
                                  bool is_first_sample,
                                  OP_Node *op);
    ~HUSD_PointInstancerSampleData();

    bool accumulate(const GU_Detail *gdp,
        const GA_Range &range,
        HUSD_AutoReadLock &input_readlock,
        UT_StringSet &created_primpaths,
        const HUSD_PointInstancerSopToUsdConfig &config,
        const UT_StringMap<UT_StringArray> &prototype_path_map);

    void apply(HUSD_AutoWriteLock &writelock);

    const HUSD_TimeCode& timecode()
    {return myTimeCode;}

private:
    UT_UniquePtr<husd_UsdWriteQueue> myWriteQueue;
    HUSD_TimeCode                    myTimeCode;
    bool                             myIsFirstSample;
    OP_Node                         *myNode;
};

class HUSD_API HUSD_PointInstancer
{
public:
    static bool copyUsdAttrsToGeoAttrs(GU_Detail                  *gdp,
                              HUSD_AutoReadLock                   &readlock,
                              const HUSDPointInstancerParms       &parms,
                              const UT_StringMap<UT_Array<exint>> &instancermap,
                              const HUSD_TimeCode &timecode,
                              UT_ErrorManager *error_manager);

    static bool createBoundingBoxGeoAttr(
            GU_Detail *gdp,
            HUSD_AutoReadLock &readlock,
            const UT_StringRef    &primPath,
            const HUSD_TimeCode   &timeCode,
            const GA_Range       &range,
            const HUSDPointInstancerParms &parms,
            const UT_Array<exint> &indices);
};

struct HUSDPointInstancerParms
{
    UT_StringHolder                       myPrimPattern;
    UT_StringHolder                       myPrimvarsFilter;
    bool                                  myCreatePathAttribute;
    bool                                  myTransformIntoWorldSpace;
    bool                                  myImportPositions;
    bool                                  myImportOrientations;
    bool                                  myImportScales;
    bool                                  myImportAccelerations;
    bool                                  myImportVelocities;
    bool                                  myImportAngularVelocities;
    bool                                  myImportIds;
    HUSD_PointInstancerSopProtoIndexSource myProtoSource;
    bool                                  myImportVisibility;
    bool                                  myImportBoundingBoxesAsAttr;
    bool                                  myImportBoundingBoxesAsPacked;
    UT_StringArray                        myImportBoundingBoxesPurposes;
    UT_StringHolder                       myImportBoundingBoxesAttr;
    UT_StringHolder                       myIntAttrName;
    UT_StringHolder                       myStrAttrName;
};

// Returns true if any attribute on the prototype prim or any descendant that
// can affect its world-space bounds is time-varying.
HUSD_API bool
HUSDprototypeIsTimeVarying(const HUSD_AutoAnyLock &lock,
                           const UT_StringRef &prototype_path);

#endif // __HUSD_PointInstancer_h__
