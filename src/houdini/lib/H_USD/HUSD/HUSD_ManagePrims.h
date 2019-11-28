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

