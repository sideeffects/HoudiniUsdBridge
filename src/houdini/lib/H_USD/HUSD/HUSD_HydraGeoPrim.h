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
 * NAME:	HUSD_HydraGeoPrim.h (HUSD Library, C++)
 *
 * COMMENTS:	Container for GT prim repr of a hydro geometry  prim (HdRprim)
 */
#ifndef HUSD_HydraGeoPrim_h
#define HUSD_HydraGeoPrim_h

#include "HUSD_API.h"
#include "HUSD_HydraPrim.h"

#include <GT/GT_Handles.h>
#include <UT/UT_NonCopyable.h>
#include <UT/UT_StringArray.h>

class HUSD_Scene;
class HUSD_HydraMaterial;

/// Container for GT prim repr and a hydro geometry (R) prim
class HUSD_API HUSD_HydraGeoPrim : public HUSD_HydraPrim
{
public:
             HUSD_HydraGeoPrim(HUSD_Scene &scene,
                               const char *geo_id,
                               bool consolidated = false);
            ~HUSD_HydraGeoPrim() override;

    virtual bool       isValid() const = 0;
    bool	       isDirty() const { return myDirtyMask != 0; }
    int		       getDirtyMask() const { return myDirtyMask; }
    void	       clearDirtyMask() { myDirtyMask = 0; }

    const GT_PrimitiveHandle  &prim() const  { return myGTPrim; }
    const GT_PrimitiveHandle  &instance() const { return myInstance; }
    //const UT_StringHolder     &material() const { return myMaterial; }

    enum husd_DirtyBits
    {
	NEEDS_INIT	 = 0x1,
	TOP_CHANGE	 = 0x2,
	GEO_CHANGE	 = 0x4,
	INSTANCE_CHANGE	 = 0x8,
	MAT_CHANGE	 = 0x10,
	LOD_CHANGE	 = 0x20,
	VIS_CHANGE	 = 0x40,
	LIGHT_LINK_CHANGE= 0x80,

	ALL_DIRTY = 0xFFFFFFFF
    };

    void        dirty(husd_DirtyBits bit) { myDirtyMask |= bit; }

    bool	needsGLStateCheck() const { return myNeedGLStateCheck; }
    void	needsGLStateCheck(bool s) { myNeedGLStateCheck=s; }

    bool        getBounds(UT_BoundingBox &box) const override;
    
    void        setIndex(int i) { myIndex = i; }
    int		index() const   { return myIndex; }

    uint64	deferredBits() const	   { return myDeferBits; }
    void	setDeferredBits(uint64 b) { myDeferBits = b; }

    bool        hasMaterialOverrides() const
                                { return myHasMatOverrides; }
    void        hasMaterialOverrides(bool y)
                                { myHasMatOverrides = y; }

    void	setVisible(bool v);
    bool	isVisible() const { return myIsVisible; }
	
    void	setInstanced(bool i) { myIsInstanced = i; }
    bool	isInstanced() const { return myIsInstanced; }

    bool        isConsolidated() const { return myIsConsolidated; }
    void        setConsolidated(bool c) { myIsConsolidated = c; }
    const UT_IntArray &consolidatedPrimIDs() { return myPrimIDs; }
    // For consolidated prims only.
    virtual bool getSelectedBBox(UT_BoundingBox &bbox) const;
    
    virtual const UT_StringArray &materials() const = 0;
    virtual void getPrimIDRange(int &mn, int &mx) const { mn = mx = 0; }

protected:
    GT_PrimitiveHandle		myGTPrim;
    GT_PrimitiveHandle		myInstance;
    UT_IntArray                 myPrimIDs;
    uint64			myDeferBits;
    int				myDirtyMask;
    int				myIndex;
    unsigned                    myNeedGLStateCheck : 1,
                                myIsVisible : 1,
                                myIsInstanced : 1,
                                myIsConsolidated : 1,
                                myHasMatOverrides : 1;
};

#endif
