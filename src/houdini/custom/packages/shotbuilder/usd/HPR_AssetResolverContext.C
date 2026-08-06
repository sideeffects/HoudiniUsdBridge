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

#include "HPR_AssetResolverContext.h"

#include "UT/UT_IStream.h"
#include "UT/UT_JSONParser.h"
#include "UT/UT_JSONValue.h"
#include "UT/UT_JSONValueMap.h"
#include "pxr/base/tf/pathUtils.h"

#include <UT/UT_Array.h>
#include <UT/UT_StringHolder.h>

PXR_NAMESPACE_USING_DIRECTIVE

const static std::string theInvalidVersion = "0";

HPRResolverContext::HPRResolverContext()
    = default;

HPRResolverContext::HPRResolverContext(
    const HPRResolverContext&) = default;

HPRResolverContext::HPRResolverContext(
    const std::string& contextStr)
{
    UT_AutoJSONParser parser(contextStr.c_str(), contextStr.length());
    UT_JSONValue contextValue;
    if (contextValue.parseValue(parser))
    {
        UT_JSONValueMap *contextMap = contextValue.getMap();
        if (contextMap)
        {
            UT_JSONValue *versionsValue = contextMap->get("versions");
            if (versionsValue)
            {
                UT_JSONValueMap *versionsMap = versionsValue->getMap();
                if (versionsMap)
                {
                    for (const auto &x : *versionsMap)
                    {
                        myVersionMap[x.first.c_str()] = std::to_string(x.second->getI());
                    }
                }
            }
        }
    }
}

bool
HPRResolverContext::operator<(
    const HPRResolverContext& rhs) const
{
    return myVersionMap.size() < rhs.myVersionMap.size();
}

bool
HPRResolverContext::operator==(
    const HPRResolverContext& rhs) const
{
    return myVersionMap == rhs.myVersionMap;
}
    
size_t hash_value(const HPRResolverContext& ctx)
{
    return TfHash()(ctx.myVersionMap);
}

const std::string&
HPRResolverContext::GetVersion(const char* asset) const
{
    if (auto versionpair = myVersionMap.find(asset); versionpair != myVersionMap.end())
    {
        return versionpair->second;
    }
    return theInvalidVersion;
}