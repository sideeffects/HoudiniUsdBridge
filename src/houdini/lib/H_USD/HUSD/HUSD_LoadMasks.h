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

#ifndef __HUSD_LoadMasks_h__
#define __HUSD_LoadMasks_h__

#include "HUSD_API.h"
#include <UT/UT_StringArray.h>
#include <UT/UT_StringMap.h>
#include <UT/UT_StringSet.h>

enum HUSD_LoadMasksMatchStyle
{
    HUSD_MATCH_EXACT,
    HUSD_MATCH_SELF_OR_PARENT,
    HUSD_MATCH_SELF_OR_CHILD,
    HUSD_MATCH_COUNT
};

class HUSD_API HUSD_LoadMasks
{
public:
			 HUSD_LoadMasks();
			~HUSD_LoadMasks();

    bool		 operator==(const HUSD_LoadMasks &other) const;
    bool		 operator!=(const HUSD_LoadMasks &other) const
			 { return !(*this == other); }

    void		 save(std::ostream &os) const;
    bool		 load(UT_IStream &is);
    bool                 isEmpty() const;

    // Control over the stage population mask.
    void		 setPopulateAll();
    bool		 populateAll() const;

    void		 addPopulatePath(const UT_StringHolder &path);
    void		 removePopulatePath(const UT_StringHolder &path,
                                bool remove_children = false);
    void		 removeAllPopulatePaths();
    void                 setPopulatePaths(const UT_SortedStringSet &paths);

    bool		 isPathPopulated(const UT_StringHolder &path,
				HUSD_LoadMasksMatchStyle match =
                                    HUSD_MATCH_EXACT) const;
    const UT_SortedStringSet &populatePaths() const
			 { return myPopulatePaths; }

    // Control over the layer muting.
    void		 addMuteLayer(const UT_StringHolder &identifier);
    void		 removeMuteLayer(const UT_StringHolder &identifier);
    void		 removeAllMuteLayers();

    bool		 isLayerMuted(const UT_StringHolder &identifier) const
			 { return myMuteLayers.contains(identifier); }
    const UT_SortedStringSet &muteLayers() const
			 { return myMuteLayers; }

    // Control over the payload configuration.
    void		 setLoadAll();
    bool		 loadAll() const;

    void		 addLoadPath(const UT_StringHolder &path);
    void		 removeLoadPath(const UT_StringHolder &path,
                                bool remove_children = false);
    void		 removeAllLoadPaths();
    void                 setLoadPaths(const UT_SortedStringSet &paths);

    // Control over variant selection fallbacks.
    void                 setVariantSelectionFallbacks(
                                const UT_StringMap<UT_StringArray> &fallbacks);
    const UT_StringMap<UT_StringArray> &variantSelectionFallbacks() const;

    // Combine two load masks, as we'd want when merging two stages.
    void		 merge(const HUSD_LoadMasks &other);

    bool		 isPathLoaded(const UT_StringHolder &path,
				HUSD_LoadMasksMatchStyle match =
                                    HUSD_MATCH_EXACT) const;
    const UT_SortedStringSet &loadPaths() const
			 { return myLoadPaths; }

    static const HUSD_LoadMasks theEmptyLoadMasks;

private:
    UT_SortedStringSet               myPopulatePaths;
    UT_SortedStringSet               myMuteLayers;
    UT_SortedStringSet               myLoadPaths;
    UT_StringMap<UT_StringArray>     myVariantSelectionFallbacks;
    bool                             myPopulateAll;
    bool                             myLoadAll;
};

#endif

