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

#ifndef __HUSD_EditReferences_h__
#define __HUSD_EditReferences_h__

#include "HUSD_API.h"
#include "HUSD_DataHandle.h"
#include "HUSD_LayerOffset.h"
#include <GU/GU_DetailHandle.h>
#include <UT/UT_StringHolder.h>
#include <UT/UT_StringMap.h>

class HUSD_API HUSD_EditReferences
{
public:
			 HUSD_EditReferences(HUSD_AutoWriteLock &lock);
			~HUSD_EditReferences();

    // Setting the primitive type for any parent primitives that need to be
    // created when creating the reference primitive.
    void		 setParentPrimType(const UT_StringHolder &primtype)
			 { myParentPrimType = primtype; }
    const UT_StringHolder &parentPrimType() const
			 { return myParentPrimType; }
    // Specify the primitive kind value to be set on the primitive if we
    // have to create it.
    void		 setPrimKind(const UT_StringHolder &kind)
			 { myPrimKind = kind; }
    const UT_StringHolder &primKind() const
			 { return myPrimKind; }
    // Specify the primitive kind value to be set on the primitive if we
    // have to create it.
    void		 setRefType(const UT_StringHolder &reftype)
			 { myRefType = reftype; }
    const UT_StringHolder &refType() const
			 { return myRefType; }
    // Specify the primitive kind value to be set on the primitive if we
    // have to create it.
    void		 setRefEditOp(const UT_StringHolder &refeditop)
			 { myRefEditOp = refeditop; }
    const UT_StringHolder &refEditOp() const
			 { return myRefEditOp; }

    bool		 addReference(const UT_StringRef &primpath,
				const UT_StringRef &reffilepath,
				const UT_StringRef &refprimpath,
				const HUSD_LayerOffset &offset =
				    HUSD_LayerOffset(),
				const UT_StringMap<UT_StringHolder> &refargs =
				    UT_StringMap<UT_StringHolder>(),
				const GU_DetailHandle &gdh =
				    GU_DetailHandle()) const;
    bool		 removeReference(const UT_StringRef &primpath,
				const UT_StringRef &reffilepath,
				const UT_StringRef &refprimpath,
				const HUSD_LayerOffset &offset =
				    HUSD_LayerOffset(),
				const UT_StringMap<UT_StringHolder> &refargs =
				    UT_StringMap<UT_StringHolder>(),
                                bool define_parent_prims = false) const;
    bool		 clearLayerReferenceEdits(const UT_StringRef &primpath,
                                bool define_parent_prims = false);
    bool		 clearReferences(const UT_StringRef &primpath,
                                bool define_parent_prims = false);

private:
    HUSD_AutoWriteLock	&myWriteLock;
    UT_StringHolder	 myPrimKind;
    UT_StringHolder	 myRefType;
    UT_StringHolder	 myRefEditOp;
    UT_StringHolder	 myParentPrimType;
};

#endif

