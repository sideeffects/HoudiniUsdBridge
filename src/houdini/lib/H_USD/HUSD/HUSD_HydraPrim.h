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
 *	Side Effects Software Inc
 *	123 Front Street West, Suite 1401
 *	Toronto, Ontario
 *	Canada   M5J 2M2
 *	416-504-9876
 *
 * NAME:	HUSD_HydraPrim.h (HUSD Library, C++)
 *
 * COMMENTS:	Base class container for a hydra prim class
 */
#ifndef HUSD_HydraPrim_h
#define HUSD_HydraPrim_h

#include "HUSD_API.h"

#include <pxr/pxr.h>

#include <GA/GA_Types.h>
#include <UT/UT_StringHolder.h>
#include <UT/UT_Matrix4.h>
#include <UT/UT_Lock.h>
#include <UT/UT_BoundingBox.h>
#include <UT/UT_IntArray.h>
#include <UT/UT_IntrusivePtr.h>

PXR_NAMESPACE_OPEN_SCOPE
class TfToken;
PXR_NAMESPACE_CLOSE_SCOPE

class HUSD_Scene;

#define HUSD_PARM(NAME, TYPE)		\
    void	NAME(TYPE v) { my##NAME = v; }	\
    const TYPE &NAME() const { return my##NAME; }

class HUSD_API HUSD_HydraPrimData
{
public:
	     HUSD_HydraPrimData();
    virtual ~HUSD_HydraPrimData();

    GA_Offset myOffset;
    // Container for extra data associated with a hydra prim.
};

/// Base container for any hydra prim
class HUSD_API HUSD_HydraPrim : public UT_IntrusiveRefCounter<HUSD_HydraPrim>
{
public:
	     HUSD_HydraPrim(HUSD_Scene &scene,
                            const char *geo_id);
    virtual ~HUSD_HydraPrim();

    // USD path of this prim
    const UT_StringHolder &path() const { return myPrimPath; }
    void                 setPath(const UT_StringRef &path) { myPrimPath = path;}

    bool                 isInitialized() const { return myInit; }
    void                 setInitialized(bool i=true) { myInit=i; }
    
    // Hydra identifier of this prim (may not be the USD path in the case of
    // instancers and prototypes).
    const UT_StringHolder &geoID() const { return myGeoID; }

    bool		 hasPathID(int id) const;
    
    int64		 version() const { return myVersion; }
    void		 bumpVersion()   { myVersion++; }
    int			 id() const { return myID; }
    const HUSD_Scene	&scene() const { return myScene; }
    HUSD_Scene		&scene() { return myScene; }
    virtual bool	 selectionDirty() const { return mySelectDirty; }
    void		 selectionDirty(bool d) { mySelectDirty = d; }

    // Returns true if the selection changed. 'has_selection' can be passed to
    // determine if anything is selected.
    virtual bool	 updateGTSelection(bool *has_selection=nullptr)
                            { return false; }
    virtual void	 clearGTSelection() {}
    virtual bool	 getBounds(UT_BoundingBox &box) const;

    static int		 newUniqueId();
    
    // Data is owned once set.
    void		 setExtraData(HUSD_HydraPrimData *data);
    HUSD_HydraPrimData	*extraData() { return myExtraData; }

    bool		isInstanced() const {return myInstanceIDs.entries()>0;}
    UT_IntArray		&instanceIDs() { return myInstanceIDs; }
    const UT_IntArray	&instanceIDs() const { return myInstanceIDs; }

    // Type of prim this is for filtering purposes.
    enum RenderTag
    {
	TagDefault,
	TagGuide,
	TagProxy,
	TagRender,
	TagInvisible,
	
	NumRenderTags
    };
    void		setRenderTag(RenderTag tag) { myRenderTag = tag; }
    RenderTag		renderTag() const { return myRenderTag; }

    /// Look up the enum value from the TfToken
    static RenderTag		 renderTag(const PXR_NS::TfToken &render_tag);
    /// Get the label for a given tag enum
    const PXR_NS::TfToken	&renderTag(RenderTag tag);

    UT_Lock             &lock() { return myLock; }

    HUSD_PARM(Transform, UT_Matrix4D);
    
private:
    HUSD_HydraPrim(const HUSD_HydraPrim &) = delete;
    
    UT_Lock                      myLock;
    UT_Matrix4D			 myTransform;
    UT_StringHolder		 myPrimPath;
    UT_StringHolder		 myGeoID;
    UT_IntArray			 myInstanceIDs;
    HUSD_Scene			&myScene;
    HUSD_HydraPrimData	        *myExtraData;
    int				 myID;
    int64			 myVersion;
    bool			 mySelectDirty;
    bool                         myInit;
    RenderTag			 myRenderTag;
};

#endif
