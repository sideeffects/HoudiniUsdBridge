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

#ifndef __HUSD_PostLayers_h__
#define __HUSD_PostLayers_h__

#include "HUSD_API.h"
#include "HUSD_DataHandle.h"
#include <UT/UT_IntrusivePtr.h>
#include <UT/UT_IStream.h>
#include <UT/UT_StringArray.h>
#include <iosfwd>

class HUSD_API HUSD_PostLayers :
    public UT_IntrusiveRefCounter<HUSD_PostLayers>,
    public UT_NonCopyable
{
public:
                             HUSD_PostLayers();
                            ~HUSD_PostLayers();

    int                      layerCount() const;
    const UT_StringHolder   &layerName(int i) const;
    bool                     hasLayer(const UT_StringRef &name) const;
    PXR_NS::XUSD_LayerPtr    layer(int i) const;
    PXR_NS::XUSD_LayerPtr    layer(const UT_StringRef &name) const;
    void                    *pythonLayer(int i) const;
    void                    *pythonLayer(const UT_StringRef &name) const;

    void                     save(std::ostream &os) const;
    bool                     load(UT_IStream &is);
    void                     copy(const HUSD_PostLayers &src);
    void                     clear();
    bool                     removeLayer(int i);
    bool                     removeLayer(const UT_StringRef &name);

    // Prepare to author data into the named layer in the context of the
    // supplied data handle and load masks.
    void	             writeLock(const HUSD_DataHandle &datahandle,
                                    const HUSD_LoadMasksPtr &loadmasks,
                                    const UT_StringHolder &layername);
    // Once a write lock has been established, it is possible to author data
    // to our own copy of this data handle using stadard AutoLock methods.
    const HUSD_DataHandle   &lockedDataHandle();
    // Releasing the write lock copies the active layer of our data handle
    // into the named layer in our map.
    void	             release(const HUSD_AutoWriteLock *writelock);

    exint	             versionId() const
		             { return myVersionId; }

private:
    UT_Array<PXR_NS::XUSD_LayerPtr>      myLayers;
    UT_StringArray                       myLayerNames;
    HUSD_DataHandle                      myDataHandle;
    const HUSD_DataHandle               *myLockedToDataHandle;
    UT_StringHolder                      myLockedToLayerName;
    exint                                myLockedToLayerIndex;
    exint                                myVersionId;
};

#endif

