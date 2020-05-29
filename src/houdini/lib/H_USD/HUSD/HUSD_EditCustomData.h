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

#ifndef __HUSD_EditCustomData_h__
#define __HUSD_EditCustomData_h__

#include "HUSD_API.h"
#include "HUSD_DataHandle.h"
#include "HUSD_TimeCode.h"
#include <UT/UT_StringHolder.h>

class HUSD_FindPrims;
class HUSD_FindProps;

class HUSD_API HUSD_EditCustomData
{
public:
			 HUSD_EditCustomData(HUSD_AutoWriteLock &lock);
			~HUSD_EditCustomData();

    // These functions set the actual custom data on the layer, prim, or
    // property. The UtValueType parameters can be any of:
    //    bool
    //    int
    //    int64
    //    UT_Vector2i
    //    UT_Vector3i
    //    UT_Vector4i
    //    fpreal32
    //    fpreal64
    //    UT_Vector2F
    //    UT_Vector3F
    //    UT_Vector4F
    //    UT_QuaternionF
    //    UT_QuaternionH
    //    UT_Matrix3D
    //    UT_Matrix4D
    //    UT_StringHolder
    // Make sure to explicitly cast to one of these data types, even if
    // implicit conversions exist.
    template<typename UtValueType>
    bool		 setLayerCustomData(const UT_StringRef &key,
				const UtValueType &value) const;
    template<typename UtValueType>
    bool		 setCustomData(const HUSD_FindPrims &findprims,
				const UT_StringRef &key,
				const UtValueType &value) const;
    template<typename UtValueType>
    bool		 setCustomData(const HUSD_FindProps &findprops,
				const UT_StringRef &key,
				const UtValueType &value) const;

    bool                 setIconCustomData(const HUSD_FindPrims &findprims,
                                const UT_StringHolder &icon);
    bool                 setIconCustomData(const HUSD_FindProps &findprops,
                                const UT_StringHolder &icon);

    bool		 removeLayerCustomData(const UT_StringRef &key) const;
    bool		 removeCustomData(const HUSD_FindPrims &findprims,
				const UT_StringRef &key) const;
    bool		 removeCustomData(const HUSD_FindProps &findprops,
				const UT_StringRef &key) const;

    bool		 clearLayerCustomData() const;
    bool		 clearCustomData(const HUSD_FindPrims &findprims) const;
    bool		 clearCustomData(const HUSD_FindProps &findprops) const;

private:
    HUSD_AutoWriteLock	&myWriteLock;
};

#endif

