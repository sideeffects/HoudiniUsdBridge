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

#ifndef __HUSD_ExpansionState_h__
#define __HUSD_ExpansionState_h__

#include "HUSD_API.h"
#include <UT/UT_IntrusivePtr.h>
#include <UT/UT_NonCopyable.h>
#include <UT/UT_StringHolder.h>
#include <UT/UT_StringMap.h>
#include <iosfwd>

class UT_IStream;
class UT_JSONValue;
class UT_JSONWriter;
class HUSD_ExpansionState;

typedef UT_IntrusivePtr<HUSD_ExpansionState> HUSD_ExpansionStateHandle;
typedef UT_StringMap<HUSD_ExpansionStateHandle> HUSD_ExpansionStateMap;

class HUSD_API HUSD_ExpansionState :
    public UT_IntrusiveRefCounter<HUSD_ExpansionState>,
    public UT_NonCopyable
{
public:
				 HUSD_ExpansionState();
				~HUSD_ExpansionState();

    bool			 isExpanded(const char *path) const;
    void			 setExpanded(const char *path, bool expanded);

    const HUSD_ExpansionState	*getChild(const UT_StringRef &name) const;
    bool			 getExpanded() const;
    exint			 getMemoryUsage() const;

    void			 clear();
    void			 copy(const HUSD_ExpansionState &src);
    bool			 save(UT_JSONWriter &writer) const;
    bool			 save(std::ostream &os, bool binary) const;
    bool			 load(const UT_JSONValue &value);
    bool			 load(UT_IStream &is);

private:
    HUSD_ExpansionStateMap	 myChildren;
    bool			 myExpanded;
};

#endif
