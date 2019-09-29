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

