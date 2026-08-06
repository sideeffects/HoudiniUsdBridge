/*
* PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
 *
 * Produced by:
 *      Side Effects Software Inc
 *	123 Front Street West, Suite 1401
 *	Toronto, Ontario
 *	Canada   M5J 2M2
 *	416-504-9876
 *
 * COMMENTS:
 */

#ifndef HPR_ARRESOLVERCONTEXT_H_
#define HPR_ARRESOLVERCONTEXT_H_

#include "pxr/pxr.h"
#include "pxr/usd/ar/defineResolverContext.h"
#include "pxr/usd/ar/resolverContext.h"

#include <string>
#include <map>

class HPRResolverContext
{
public:
    explicit HPRResolverContext(
        const std::string& contextString);

    HPRResolverContext();

    HPRResolverContext(
        const HPRResolverContext& rhs);

    bool operator<(const HPRResolverContext& rhs) const;

    bool operator==(const HPRResolverContext& rhs) const;

    const std::string& GetVersion(const char* asset) const;

    friend size_t hash_value(const HPRResolverContext& ctx);



private:
    std::map<std::string, std::string> myVersionMap;
};

PXR_NAMESPACE_OPEN_SCOPE
AR_DECLARE_RESOLVER_CONTEXT(HPRResolverContext);
PXR_NAMESPACE_CLOSE_SCOPE

#endif //HPR_ARRESOLVERCONTEXT_H_
