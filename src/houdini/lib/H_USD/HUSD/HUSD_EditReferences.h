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
    // Specify the reference type to use (refeernce, specialize, inherit).
    void		 setRefType(const UT_StringHolder &reftype)
			 { myRefType = reftype; }
    const UT_StringHolder &refType() const
			 { return myRefType; }
    // Specify the way to edit the reference list (append, prepend, etc).
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

