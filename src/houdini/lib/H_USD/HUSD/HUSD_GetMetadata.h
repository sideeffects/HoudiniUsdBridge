/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
 *
 * Produced by:
 *	Side Effects Software Inc.
 *	123 Front Street West, Suite 1401
 *	Toronto, Ontario
 *	Canada   M5J 2M2
 *	416-504-9876
 *
 */

#ifndef __HUSD_GetMetadata_h__
#define __HUSD_GetMetadata_h__

#include "HUSD_API.h"
#include "HUSD_DataHandle.h"

class HUSD_API HUSD_GetMetadata
{
public:
			 HUSD_GetMetadata(HUSD_AutoAnyLock &lock);
			~HUSD_GetMetadata();


    /// Obtains a value for a metadata on a given object. 
    /// The object path can point to a primitive, attribute, or a relationship.
    /// The metadata name can be a simple name (eg, "active") or a name path
    /// into metadata dictionaries (eg "assetInfo:foo" or "customData:bar:baz").
    template<typename UtValueType>
    bool		 getMetadata(const UT_StringRef &object_path,
				const UT_StringRef &metadata_name,
				UtValueType &value) const;

private:
    HUSD_AutoAnyLock	&myReadLock;
};

#endif
