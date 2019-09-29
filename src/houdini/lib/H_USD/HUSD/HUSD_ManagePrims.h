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

#ifndef __HUSD_ManagePrims_h__
#define __HUSD_ManagePrims_h__

#include "HUSD_API.h"
#include "HUSD_DataHandle.h"
#include <UT/UT_StringHolder.h>

class HUSD_API HUSD_ManagePrims
{
public:
			 HUSD_ManagePrims(HUSD_AutoLayerLock &lock);
			~HUSD_ManagePrims();

    // Copy a primspec from one location to another.
    bool		 copyPrim(const UT_StringRef &source_primpath,
				const UT_StringRef &dest_primpath,
				const UT_StringRef &parentprimtype) const;

    // Move a primspec from one location to another.
    bool		 movePrim(const UT_StringRef &source_primpath,
				const UT_StringRef &dest_primpath,
				const UT_StringRef &parentprimtype) const;

    // Delete a primspec.
    bool		 deletePrim(const UT_StringRef &primpath) const;

    // Set the reference value for a primitive.
    bool		 setPrimReference(const UT_StringRef &primpath,
				const UT_StringRef &reffilepath,
				const UT_StringRef &refprimpath,
				bool as_payload) const;

    // Set the transform for a primitive.
    bool		 setPrimXform(const UT_StringRef &primpath,
				const UT_Matrix4D &xform) const;

    // Set a variant set/name for a primitive.
    bool		 setPrimVariant(const UT_StringRef &primpath,
				const UT_StringRef &variantset,
				const UT_StringRef &variantname);

    int			 primEditorNodeId() const
			 { return myPrimEditorNodeId; }
    void		 setPrimEditorNodeId(int nodeid)
			 { myPrimEditorNodeId = nodeid; }

private:
    HUSD_AutoLayerLock	&myLayerLock;
    int                  myPrimEditorNodeId;
};

#endif

