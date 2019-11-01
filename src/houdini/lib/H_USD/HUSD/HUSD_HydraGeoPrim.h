/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
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
#include <UT/UT_StringHolder.h>

class HUSD_Scene;
class HUSD_HydraMaterial;

/// Container for GT prim repr and a hydro geometry (R) prim
class HUSD_API HUSD_HydraGeoPrim : public HUSD_HydraPrim
{
public:
             HUSD_HydraGeoPrim(HUSD_Scene &scene,
                               const char *geo_id);
    virtual ~HUSD_HydraGeoPrim();

    virtual bool       isValid() const = 0;
    bool	       isDirty() const { return myDirtyMask != 0; }
    int		       getDirtyMask() const { return myDirtyMask; }
    void	       clearDirtyMask() { myDirtyMask = 0; }

    const GT_PrimitiveHandle  &prim() const  { return myGTPrim; }
    const GT_PrimitiveHandle  &instance() const { return myInstance; }
    const UT_StringHolder     &material() const { return myMaterial; }

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

    virtual bool getBounds(UT_BoundingBox &box) const;
    
    void		 setIndex(int i) { myIndex = i; }
    int			 index() const   { return myIndex; }

    uint64		 deferredBits() const	   { return myDeferBits; }
    void		 setDeferredBits(uint64 b) { myDeferBits = b; }

    void		 setMaterial(const UT_StringRef &path);
    bool                 hasMaterialOverrides() const
                                { return myHasMatOverrides; }
    void                 hasMaterialOverrides(bool y)
                                { myHasMatOverrides = y; }

    void		 setVisible(bool v);
    bool		 isVisible() const { return myIsVisible; }
	
    void		 setInstanced(bool i) { myIsInstanced = i; }
    bool		 isInstanced() const { return myIsInstanced; }
	
    void		 setPointInstanced(bool p) { myPointInstanced = p; }
    bool		 isPointInstanced() const
                            { return myIsInstanced && myPointInstanced; }
	

protected:
    GT_PrimitiveHandle		myGTPrim;
    GT_PrimitiveHandle		myInstance;
    UT_StringHolder		myMaterial;
    uint64			myDeferBits;
    int				myDirtyMask;
    int				myIndex;
    bool			myNeedGLStateCheck;
    bool			myIsVisible;
    bool			myIsInstanced;
    bool			myPointInstanced;
    bool                        myHasMatOverrides;
};

#endif
