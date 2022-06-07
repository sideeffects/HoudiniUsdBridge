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

#ifndef __HUSD_Overrides_h__
#define __HUSD_Overrides_h__

#include "HUSD_API.h"
#include "HUSD_DataHandle.h"
#include "HUSD_Utils.h"
#include <UT/UT_IntrusivePtr.h>
#include <UT/UT_IStream.h>
#include <UT/UT_StringMap.h>
#include <UT/UT_UniquePtr.h>
#include <pxr/pxr.h>
#include <iosfwd>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

class XUSD_Data;
class XUSD_OverridesData;

PXR_NAMESPACE_CLOSE_SCOPE

class HUSD_TimeCode;
class HUSD_PathSet;

class HUSD_API HUSD_Overrides : public UT_IntrusiveRefCounter<HUSD_Overrides>,
				public UT_NonCopyable
{
public:
                 HUSD_Overrides();
                ~HUSD_Overrides();

    void         save(std::ostream &os) const;
    bool         load(UT_IStream &is);
    void         copy(const HUSD_Overrides &src);
    bool         isEmpty() const;
    bool         isEmpty(HUSD_OverridesLayerId layer_id) const;

    void         clear(const UT_StringRef &fromprim =
                         UT_StringHolder::theEmptyString);
    void         clear(HUSD_OverridesLayerId layer_id,
                         const UT_StringRef &fromprim =
                             UT_StringHolder::theEmptyString);

    bool         getDrawModeOverrides(const UT_StringRef &primpath,
                        UT_StringMap<UT_StringHolder> &overrides) const;
    bool         setDrawMode(HUSD_AutoWriteOverridesLock &lock,
                         const HUSD_FindPrims &prims,
                         const UT_StringRef &drawmode);
    bool         getActiveOverrides(const UT_StringRef &primpath,
                        UT_StringMap<bool> &overrides) const;
    bool         setActive(HUSD_AutoWriteOverridesLock &lock,
                        const HUSD_FindPrims &prims,
                        bool active);
    bool         getVisibleOverrides(const UT_StringRef &primpath,
                        UT_StringMap<UT_StringHolder> &overrides) const;
    bool         setVisible(HUSD_AutoWriteOverridesLock &lock,
                        const HUSD_FindPrims &prims,
                        const HUSD_TimeCode &timecode,
                        bool visible);
    bool         getSelectableOverrides(const UT_StringRef &primpath,
                        UT_StringMap<bool> &overrides) const;
    bool         setSelectable(HUSD_AutoWriteOverridesLock &lock,
                        const HUSD_FindPrims &prims,
                        bool active, bool solo);
    bool         clearSelectable(HUSD_AutoWriteOverridesLock &lock);

    bool         setSoloLights(HUSD_AutoWriteOverridesLock &lock,
                        const HUSD_FindPrims &prims);
    bool         addSoloLights(HUSD_AutoWriteOverridesLock &lock,
                        const HUSD_FindPrims &prims);
    bool         removeSoloLights(HUSD_AutoWriteOverridesLock &lock,
                        const HUSD_FindPrims &prims);
    bool         getSoloLights(HUSD_PathSet &paths) const;
    bool         setSoloGeometry(HUSD_AutoWriteOverridesLock &lock,
                        const HUSD_FindPrims &prims);
    bool         addSoloGeometry(HUSD_AutoWriteOverridesLock &lock,
                        const HUSD_FindPrims &prims);
    bool         removeSoloGeometry(HUSD_AutoWriteOverridesLock &lock,
                        const HUSD_FindPrims &prims);
    bool         getSoloGeometry(HUSD_PathSet &paths) const;
    bool         setDisplayOpacity(HUSD_AutoWriteOverridesLock &lock,
                        const HUSD_FindPrims &prims,
                        const HUSD_TimeCode &timecode,
                        fpreal opacity);

    // Indicate that this override's data is being authored on a stage.
    // We should only be locked to one XUSD_Data at a time, and we
    // should always have a single unlock paired with each lock. The
    // XUSD_Data pointer is passed to the unlock function only to
    // verify this condition. When locked to an XUSD_Data, all requests
    // to edit the owner overrides are applied to the stage's session
    // layers directly. When unlocking, the stage's session layers are
    // copied back into the layers stored in the myLayer array.
    void			 lockToData(PXR_NS::XUSD_Data *data);
    void			 unlockFromData(PXR_NS::XUSD_Data *data);

    PXR_NS::XUSD_OverridesData	&data() const
				 { return *myData; }
    exint			 versionId() const
				 { return myVersionId; }

private:
    UT_UniquePtr<PXR_NS::XUSD_OverridesData>	 myData;
    exint					 myVersionId;
};

#endif

