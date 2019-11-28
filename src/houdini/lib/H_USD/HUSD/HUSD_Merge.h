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

#ifndef __HUSD_Merge_h__
#define __HUSD_Merge_h__

#include "HUSD_API.h"
#include "HUSD_DataHandle.h"
#include "HUSD_Utils.h"
#include <UT/UT_StringArray.h>
#include <UT/UT_UniquePtr.h>

enum HUSD_MergeStyle {
    HUSD_MERGE_FLATTEN_INTO_ACTIVE_LAYER,
    HUSD_MERGE_FLATTENED_LAYERS,
    HUSD_MERGE_PERHANDLE_FLATTENED_LAYERS,
    HUSD_MERGE_SEPARATE_LAYERS,
    HUSD_MERGE_SEPARATE_LAYERS_WEAK_FILES,
    HUSD_MERGE_SEPARATE_LAYERS_WEAK_FILES_AND_SOPS,
};

class HUSD_API HUSD_Merge
{
public:
				 HUSD_Merge(HUSD_MergeStyle merge_style,
					 HUSD_StripLayerResponse response);
				~HUSD_Merge();

    bool			 addHandle(const HUSD_DataHandle &src,
					const UT_StringHolder &dest_path =
					    UT_StringHolder::theEmptyString);

    const HUSD_LoadMasksPtr	&mergedLoadMasks() const;
    bool			 execute(HUSD_AutoWriteLock &lock) const;

private:
    class husd_MergePrivate;

    UT_UniquePtr<husd_MergePrivate>	 myPrivate;
    HUSD_MergeStyle			 myMergeStyle;
    HUSD_StripLayerResponse		 myStripLayerResponse;
};

#endif

