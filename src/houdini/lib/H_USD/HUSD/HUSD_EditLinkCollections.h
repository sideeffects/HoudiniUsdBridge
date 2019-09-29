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

#ifndef __HUSD_EditLinkCollections_h__
#define __HUSD_EditLinkCollections_h__

#include "HUSD_API.h"
#include "HUSD_DataHandle.h"
#include <UT/UT_UniquePtr.h>

class husd_EditLinkCollectionsPrivate;
class HUSD_FindPrims;
class HUSD_TimeCode;

// This class allows edit of links between prims that are defined as
// collections, typically with a specific name for the collection that defines
// the link.
// These are things like light links and shadow links with are collections on
// a prim (in their case a UsdLuxLight) that specifies the geometry prims they
// are linked to.
class HUSD_API HUSD_EditLinkCollections
{
public:
    enum LinkType
    {
	LightLink,
	ShadowLink,
	MaterialLink
    };

    HUSD_EditLinkCollections(HUSD_AutoWriteLock &lock, LinkType linktype);
    ~HUSD_EditLinkCollections();


    /// Add a link whose source is the prim that will contain the collection
    /// that defines the link.
    /// This does not create the collections.
    bool		 addLinkItems(const HUSD_FindPrims &linkSource,
				      const HUSD_FindPrims &includeprims,
				      const HUSD_FindPrims &excludeprims,
				      int nodeid,
				      const HUSD_TimeCode &tc,
				      UT_StringArray *errors = nullptr);

    /// Add a link whose source is NOT the prim that will contain the
    /// collection that defines the link, but rather the prims in the link's
    /// include and excludes lists will contain the collection that defines
    /// the link.
    /// This does not create the collections.
    bool		 addReverseLinkItems(const HUSD_FindPrims &linkSource,
					     const HUSD_FindPrims &includeprims,
					     const HUSD_FindPrims &excludeprims,
					     int nodeid,
					     const HUSD_TimeCode &tc,
					     UT_StringArray *errors = nullptr);

    /// Create the collections necessary for all links previously added.
    bool		 createCollections(UT_StringArray *errors = nullptr);

    /// Clear all added links.
    void		 clear();

private:
    HUSD_AutoWriteLock	&myWriteLock;
    LinkType		 myLinkType;
    UT_UniquePtr<husd_EditLinkCollectionsPrivate>
			 myPrivate;
};

#endif

