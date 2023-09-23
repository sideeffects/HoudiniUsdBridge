/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
 *
 * NAME:	BRAY_HdEncodeJSON.h (BRAY Library, C++)
 *
 * COMMENTS:
 */

#ifndef __BRAY_HdEncodeJSON__
#define __BRAY_HdEncodeJSON__

#include <pxr/pxr.h>
#include <pxr/base/vt/dictionary.h>

class UT_JSONValue;
class UT_StringHolder;

PXR_NAMESPACE_OPEN_SCOPE

namespace BRAY_HdEncodeJSON
{
    bool        encodeJSONArray(VtValue &dest, const UT_JSONValue &value);
    bool        encodeJSONMap(VtDictionary &result, const UT_JSONValue &value);
    bool        insert(VtDictionary &result,
                        const UT_StringHolder &key, const UT_JSONValue &value);
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
