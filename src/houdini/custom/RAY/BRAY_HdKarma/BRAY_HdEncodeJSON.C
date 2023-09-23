/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
 *
 * NAME:	BRAY_HdEncodeJSON.h (BRAY Library, C++)
 *
 * COMMENTS:
 */

#include "BRAY_HdEncodeJSON.h"
#include "BRAY_HdFormat.h"
#include <UT/UT_Debug.h>
#include <UT/UT_JSONValue.h>
#include <UT/UT_JSONValueArray.h>
#include <UT/UT_JSONValueMap.h>
#include <pxr/base/vt/array.h>
#include <string>

PXR_NAMESPACE_OPEN_SCOPE
namespace BRAY_HdEncodeJSON
{

namespace
{
static const TfToken &
getTfToken(const UT_StringHolder &key)
{
    static UT_StringMap<TfToken>        theTokenMap;
    auto it = theTokenMap.find(key);
    if (it == theTokenMap.end())
    {
        it = theTokenMap.emplace(key, TfToken(key.c_str())).first;
    }
    return it->second;
}

static bool
validArrayType(UT_JSONValue::Type &type, const UT_JSONValue::Type &ntype)
{
    if (type == ntype)
        return true;

    // If we have an array of [ bool, int, int, bool ], we can import an array
    // of integers.  If the array is [bool, int, real, bool, int], we can
    // import floats.
    switch (type)
    {
        case UT_JSONValue::JSON_BOOL:
            if (ntype == UT_JSONValue::JSON_INT || ntype == UT_JSONValue::JSON_REAL)
            {
                type = ntype;
                return true;
            }
            break;
        case UT_JSONValue::JSON_INT:
            if (ntype == UT_JSONValue::JSON_REAL)
            {
                type = ntype;
                return true;
            }
            if (ntype == UT_JSONValue::JSON_BOOL)
                return true;
            break;
        case UT_JSONValue::JSON_REAL:
            return ntype == UT_JSONValue::JSON_BOOL
                || ntype == UT_JSONValue::JSON_INT;
        default:
            break;
    }
    return false;
}

template <typename T>
static void
extractArray(VtArray<T> &arr, const UT_JSONValueArray &j)
{
    arr.resize(j.size());
    for (exint i = 0, n = j.size(); i < n; ++i)
        UT_VERIFY(j.get(i)->import(arr[i]));
}

static void
extractStringArray(VtArray<std::string> &arr, const UT_JSONValueArray &j)
{
    arr.resize(j.size());
    for (exint i = 0, n = j.size(); i < n; ++i)
    {
        const UT_StringHolder   *s = j.get(i)->getStringHolder();
        UT_ASSERT(s);
        arr[i] = s->toStdString();
    }
}

static bool
extractMapArray(VtArray<VtDictionary> &arr, const UT_JSONValueArray &j)
{
    arr.resize(j.size());
    for (exint i = 0, n = j.size(); i < n; ++i)
    {
        if (!encodeJSONMap(arr[i], *j.get(i)))
            return false;
    }
    return true;
}

} // end anonymous namespace

bool
encodeJSONArray(VtValue &dest, const UT_JSONValue &value)
{
    const UT_JSONValueArray     *arr = value.getArray();
    if (!arr)
        return false;
    if (!arr->size())
        return true;    // Empty array is a null

    UT_JSONValue::Type  type = arr->get(0)->getType();
    for (int i = 1, n = arr->size(); i < n; ++i)
    {
        if (!validArrayType(type, arr->get(i)->getType()))
        {
            UT_ASSERT(0 && "Heterogeneous arrays not supported");
            return false;
        }
    }
    switch (type)
    {
        case UT_JSONValue::JSON_BOOL:
        {
            VtArray<bool>       val;
            extractArray(val, *arr);
            dest = val;
            break;
        }
        case UT_JSONValue::JSON_INT:
        {
            VtArray<int64>      val;
            extractArray(val, *arr);
            dest = val;
            break;
        }
        case UT_JSONValue::JSON_REAL:
        {
            VtArray<fpreal64>   val;
            extractArray(val, *arr);
            dest = val;
            break;
        }
        case UT_JSONValue::JSON_STRING:
        {
            VtArray<std::string>        val;
            extractStringArray(val, *arr);
            dest = val;
            break;
        }
        case UT_JSONValue::JSON_MAP:
        {
            VtArray<VtDictionary>       val;
            if (!extractMapArray(val, *arr))
                return false;
            dest = val; // Take(val);
            break;
        }
        default:
            UT_ASSERT("Unsupported array type");
            return false;
    }
    return true;
}

bool
encodeJSONMap(VtDictionary &result, const UT_JSONValue &value)
{
    const UT_JSONValueMap       *map = value.getMap();
    if (!map)
        return false;

    UT_StringArray      keys;
    map->getKeys(keys);
    for (exint i = 0, n = map->size(); i < n; ++i)
    {
        if (!insert(result, keys[i], *map->get(i)))
            return false;
    }

    return true;
}

bool
insert(VtDictionary &result,
        const UT_StringHolder &key, const UT_JSONValue &value)
{
    const TfToken       &token = getTfToken(key);
    switch (value.getType())
    {
        case UT_JSONValue::JSON_NULL:
            result[token] = VtValue();
            break;
        case UT_JSONValue::JSON_BOOL:
            result[token] = VtValue(value.getB());
            break;
        case UT_JSONValue::JSON_INT:
            result[token] = VtValue(value.getI());
            break;
        case UT_JSONValue::JSON_REAL:
            result[token] = VtValue(value.getF());
            break;
        case UT_JSONValue::JSON_STRING:
        {
            const UT_StringHolder   *s = value.getStringHolder();
            UT_ASSERT(s);
            result[token] = VtValue(s->toStdString());
            break;
        }
        case UT_JSONValue::JSON_ARRAY:
        {
            VtValue arr;
            if (!encodeJSONArray(arr, value))
                return false;
            result[token] = arr;
            break;
        }
        case UT_JSONValue::JSON_MAP:
        {
            VtDictionary    dict;
            if (!encodeJSONMap(dict, value))
                return false;
            result[token] = VtValue(dict);
            break;
        }
        case UT_JSONValue::JSON_KEY:
        {
            UT_ASSERT(0 && "Should not be getting a key without a map");
            const UT_StringHolder   *s = value.getKeyHolder();
            UT_ASSERT(s);
            result[token] = VtValue(s->toStdString());
            break;
        }
    }
    return true;
}

} // End namespace BRAY_HdEncodeJSON

PXR_NAMESPACE_CLOSE_SCOPE
