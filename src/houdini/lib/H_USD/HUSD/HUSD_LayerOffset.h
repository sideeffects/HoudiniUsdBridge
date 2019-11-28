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

#ifndef __HUSD_LayerOffset_h__
#define __HUSD_LayerOffset_h__

#include "HUSD_API.h"
#include <SYS/SYS_Types.h>

class HUSD_API HUSD_LayerOffset
{
public:
    explicit		 HUSD_LayerOffset(
				 fpreal64 offset = 0.0,
				 fpreal64 scale = 1.0)
			    : myOffset(offset),
			      myScale(scale)
			 { }

    fpreal64		 offset() const
			 { return myOffset; }
    void		 setOffset(fpreal64 offset)
			 { myOffset = offset; }
    fpreal64		 scale() const
			 { return myScale; }
    void		 setScale(fpreal64 scale)
			 { myScale = scale; }

    bool		 operator==(const HUSD_LayerOffset &other) const;
    bool		 isIdentity() const;
    bool		 isValid() const;
    HUSD_LayerOffset	 inverse() const;

private:
    fpreal64		 myOffset;
    fpreal64		 myScale;
};

#endif

