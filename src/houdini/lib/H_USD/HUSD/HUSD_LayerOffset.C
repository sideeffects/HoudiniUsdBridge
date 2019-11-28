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

#include "HUSD_LayerOffset.h"
#include <SYS/SYS_Math.h>

bool
HUSD_LayerOffset::operator==(const HUSD_LayerOffset &other) const
{
    if (!isValid())
	return !other.isValid();

    return SYSisEqual(myOffset, other.myOffset, SYS_FP64_EPSILON) &&
	   SYSisEqual(myScale, other.myScale, SYS_FP64_EPSILON);
}

bool
HUSD_LayerOffset::isIdentity() const
{
    static HUSD_LayerOffset	 theIdentityOffset;

    return (*this == theIdentityOffset);
}

bool
HUSD_LayerOffset::isValid() const
{
    return SYSisFinite(myOffset) && SYSisFinite(myScale);
}

HUSD_LayerOffset
HUSD_LayerOffset::inverse() const
{
    if (isIdentity())
	return *this;

    fpreal64	 newscale;

    if (myScale != 0.0)
	newscale = 1.0 / myScale;
    else
	newscale = SYS_Types<fpreal64>::infinity();

    return HUSD_LayerOffset(-myOffset, newscale);
}

