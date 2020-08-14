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

#ifndef __XUSD_PathSet_h__
#define __XUSD_PathSet_h__

#include "HUSD_API.h"
#include <pxr/usd/sdf/path.h>

PXR_NAMESPACE_OPEN_SCOPE

class HUSD_API XUSD_PathSet : public SdfPathSet
{
public:
			 XUSD_PathSet();
                         XUSD_PathSet(const SdfPathSet &src);
			~XUSD_PathSet();

    const XUSD_PathSet  &operator=(const SdfPathSet &src);

    bool                 contains(const SdfPath &path) const;
    bool                 containsPathOrAncestor(const SdfPath &path,
                                bool *contains = nullptr) const;
    bool                 containsPathOrDescendant(const SdfPath &path,
                                bool *contains = nullptr) const;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif

