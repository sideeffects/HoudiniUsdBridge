/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
 *
 * Produced by:
 *	Side Effects Software Inc.
 *	123 Front Street West, Suite 1401
 *	Toronto, Ontario
 *      Canada   M5J 2M2
 *	416-504-9876
 *
 */

#ifndef __HUSD_Save_h__
#define __HUSD_Save_h__

#include "HUSD_API.h"
#include "HUSD_DataHandle.h"
#include "HUSD_OutputProcessor.h"
#include <UT/UT_PathPattern.h>
#include <UT/UT_StringHolder.h>
#include <UT/UT_UniquePtr.h>
#include <SYS/SYS_Types.h>

enum HUSD_SaveStyle {
    HUSD_SAVE_FLATTENED_IMPLICIT_LAYERS,
    HUSD_SAVE_FLATTENED_ALL_LAYERS,
    HUSD_SAVE_FLATTENED_STAGE,
    HUSD_SAVE_SEPARATE_LAYERS,
};

// Some simple structs that just handle lumping bits of save configuration
// data together.
class husd_SaveTimeData
{
public:
                         husd_SaveTimeData()
                             : myStartFrame(-SYS_FP64_MAX),
                               myEndFrame(SYS_FP64_MAX),
                               myTimeCodesPerSecond(SYS_FP64_MAX),
                               myFramesPerSecond(SYS_FP64_MAX)
                         { }

    fpreal64		 myStartFrame;
    fpreal64		 myEndFrame;
    fpreal64		 myTimeCodesPerSecond;
    fpreal64		 myFramesPerSecond;
};

class husd_SaveProcessorData
{
public:
                         husd_SaveProcessorData()
                             : myConfigNode(nullptr),
                               myConfigTime(0.0)
                         { }

    HUSD_OutputProcessorArray myProcessors;
    OP_Node             *myConfigNode;
    fpreal               myConfigTime;
};

class husd_SaveDefaultPrimData
{
public:
                         husd_SaveDefaultPrimData()
                             : myRequireDefaultPrim(false)
                         { }

    UT_StringHolder      myDefaultPrim;
    bool                 myRequireDefaultPrim;
};

class husd_SaveConfigFlags
{
public:
                         husd_SaveConfigFlags()
                             : myClearHoudiniCustomData(false),
                               myFlattenFileLayers(false),
                               myFlattenSopLayers(false),
                               myErrorSavingImplicitPaths(false),
                               mySaveFilesFromDisk(false)
                         { }

    bool		 myClearHoudiniCustomData;
    bool		 myFlattenFileLayers;
    bool		 myFlattenSopLayers;
    bool		 myErrorSavingImplicitPaths;
    bool		 mySaveFilesFromDisk;
};

class HUSD_API HUSD_Save
{
public:
			 HUSD_Save();
			~HUSD_Save();

    bool		 addCombinedTimeSample(const HUSD_AutoReadLock &lock);
    bool		 saveCombined(const UT_StringRef &filepath,
				UT_StringArray &saved_paths) const;
    bool		 save(const HUSD_AutoReadLock &lock,
				const UT_StringRef &filepath,
				UT_StringArray &saved_paths) const;

    HUSD_SaveStyle	 saveStyle() const
			 { return mySaveStyle; }
    void		 setSaveStyle(HUSD_SaveStyle save_style)
			 { mySaveStyle = save_style; }

    bool                 resuireDefaultPrim() const
			 { return myDefaultPrimData.myRequireDefaultPrim; }
    void		 setRequireDefaultPrim(bool require_default_prim)
			 { myDefaultPrimData.
                             myRequireDefaultPrim = require_default_prim; }

    const UT_StringHolder &defaultPrim() const
			 { return myDefaultPrimData.myDefaultPrim; }
    void		 setDefaultPrim(const UT_StringHolder &defaultprim)
			 { myDefaultPrimData.myDefaultPrim = defaultprim; }

    bool		 clearHoudiniCustomData() const
			 { return myFlags.myClearHoudiniCustomData; }
    void		 setClearHoudiniCustomData(bool clear_data)
			 { myFlags.myClearHoudiniCustomData = clear_data; }

    bool		 flattenFileLayers() const
			 { return myFlags.myFlattenFileLayers; }
    void		 setFlattenFileLayers(bool flatten_file_layers)
			 { myFlags.myFlattenFileLayers = flatten_file_layers; }

    bool		 flattenSopLayers() const
			 { return myFlags.myFlattenSopLayers; }
    void		 setFlattenSopLayers(bool flatten_sop_layers)
			 { myFlags.myFlattenSopLayers = flatten_sop_layers; }

    bool		 errorSavingImplicitPaths() const
			 { return myFlags.myErrorSavingImplicitPaths; }
    void		 setErrorSavingImplicitPaths(bool error)
			 { myFlags.myErrorSavingImplicitPaths = error; }

    bool		 saveFilesFromDisk() const
			 { return myFlags.mySaveFilesFromDisk; }
    void		 setSaveFilesFromDisk(bool save)
			 { myFlags.mySaveFilesFromDisk = save; }

    const UT_PathPattern *saveFilesPattern() const
			 { return mySaveFilesPattern.get(); }
    void		 setSaveFilesPattern(const UT_StringHolder &pattern)
			 {
			    if (pattern.isstring())
				mySaveFilesPattern.reset(
				    new UT_PathPattern(pattern));
			    else
				mySaveFilesPattern.reset();
			 }

    fpreal64		 startFrame() const
			 { return myTimeData.myStartFrame; }
    void		 setStartFrame(fpreal64 start_time = -SYS_FP64_MAX)
			 { myTimeData.myStartFrame = start_time; }

    fpreal64		 endFrame() const
			 { return myTimeData.myEndFrame; }
    void		 setEndFrame(fpreal64 end_time = SYS_FP64_MAX)
			 { myTimeData.myEndFrame = end_time; }

    fpreal64		 timeCodesPerSecond() const
			 { return myTimeData.myTimeCodesPerSecond; }
    void		 setTimeCodesPerSecond(fpreal64 tps = SYS_FP64_MAX)
			 { myTimeData.myTimeCodesPerSecond = tps; }

    fpreal64		 framesPerSecond() const
			 { return myTimeData.myFramesPerSecond; }
    void		 setFramesPerSecond(fpreal64 fps = SYS_FP64_MAX)
			 { myTimeData.myFramesPerSecond = fps; }

    const HUSD_OutputProcessorArray &outputProcessors() const
                         { return myProcessorData.myProcessors; }
    void                 setOutputProcessors(
                                const HUSD_OutputProcessorArray &aps)
                         { myProcessorData.myProcessors = aps; }

    OP_Node             *outputProcessorsConfigNode() const
                         { return myProcessorData.myConfigNode; }
    void                 setOutputProcessorsConfigNode(OP_Node *config_node)
                         { myProcessorData.myConfigNode = config_node; }

    fpreal               outputProcessorsTime() const
                         { return myProcessorData.myConfigTime; }
    void                 setOutputProcessorsTime(fpreal t)
                         { myProcessorData.myConfigTime = t; }



private:
    class		 husd_SavePrivate;

    UT_UniquePtr<husd_SavePrivate>	 myPrivate;
    UT_UniquePtr<UT_PathPattern>	 mySaveFilesPattern;
    HUSD_SaveStyle			 mySaveStyle;
    husd_SaveProcessorData		 myProcessorData;
    husd_SaveDefaultPrimData		 myDefaultPrimData;
    husd_SaveTimeData                    myTimeData;
    husd_SaveConfigFlags                 myFlags;
};

#endif

