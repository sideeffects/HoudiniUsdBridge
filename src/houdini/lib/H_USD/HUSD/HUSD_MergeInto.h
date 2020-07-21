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

#ifndef __HUSD_MergeInto_h__
#define __HUSD_MergeInto_h__

#include "HUSD_API.h"
#include "HUSD_DataHandle.h"
#include <UT/UT_ValArray.h>
#include <UT/UT_StringArray.h>
#include <UT/UT_UniquePtr.h>

class HUSD_API HUSD_MergeInto
{
public:
			 HUSD_MergeInto();
			~HUSD_MergeInto();

    // Setting the primitive type for any parent primitives that need to be
    // created when creating the merge destination primitive.
    void		 setParentPrimType(const UT_StringHolder &primtype)
			 { myParentPrimType = primtype; }
    const UT_StringHolder &parentPrimType() const
			 { return myParentPrimType; }
    // Specify the primitive kind value to be set on the inserted primitives.
    void		 setPrimKind(const UT_StringHolder &kind)
			 { myPrimKind = kind; }
    const UT_StringHolder &primKind() const
			 { return myPrimKind; }
    // Specify the primitive kind value to be set on any automatically created
    // parents of the inserted primitives.
    void		 setParentPrimKind(const UT_StringHolder &kind)
			 { myParentPrimKind = kind; }
    const UT_StringHolder &parentPrimKind() const
			 { return myParentPrimKind; }
    // Specify that this operation should guarantee that each destination
    // location is unique, and doesn't overwrite any existing primitive on
    // the stage.
    void		 setMakeUniqueDestPaths(bool make_unique)
			 { myMakeUniqueDestPaths = make_unique; }
    bool		 makeUniqueDestPaths() const
			 { return myMakeUniqueDestPaths; }

    bool		 addHandle(const HUSD_DataHandle &src,
				const UT_StringHolder &dest_path,
				const UT_StringHolder &source_node_path,
				const UT_StringHolder &source_path = UT_StringHolder(),
				const fpreal frame_offset = 0);
    bool		 execute(HUSD_AutoLayerLock &lock) const;

private:
    class husd_MergeIntoPrivate;

    UT_UniquePtr<husd_MergeIntoPrivate>	 myPrivate;
    UT_StringArray			 myDestPaths;
    UT_StringArray			 mySourceNodePaths;
    UT_StringArray			 mySourcePaths;
    UT_FprealArray			 myFrameOffsets;
    UT_StringHolder			 myPrimKind;
    UT_StringHolder			 myParentPrimKind;
    UT_StringHolder			 myParentPrimType;
    bool				 myMakeUniqueDestPaths;
};

#endif

