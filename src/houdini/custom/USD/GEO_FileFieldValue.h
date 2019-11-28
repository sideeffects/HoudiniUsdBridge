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
 */

#ifndef GEO_FILE_FIELD_VALUE_H
#define GEO_FILE_FIELD_VALUE_H

#include "GEO_Boost.h"
#include "pxr/pxr.h"
#include <pxr/usd/sdf/abstractData.h>
#include BOOST_HEADER(call_traits.hpp)
#include BOOST_HEADER(type_traits/remove_const.hpp)
#include BOOST_HEADER(type_traits/remove_reference.hpp)
#include BOOST_HEADER(variant.hpp)

PXR_NAMESPACE_OPEN_SCOPE

/// Wraps a VtValue or SdfAbstractDataValue so we can access any
/// the same way.  This type allows us to implement some methods without
/// templatizing them.
class GEO_FileFieldValue
{
public:
    typedef bool (GEO_FileFieldValue::*_UnspecifiedBoolType)() const;

    /// Construct an empty any.
    GEO_FileFieldValue() { }

    /// Construct with a pointer to any supported type-erased object \p any.
    /// If \p any is \c NULL then this object is considered to be empty.
    template <class T>
    explicit GEO_FileFieldValue(T* any)
    {
        if (any) {
            myValuePtr = any;
        }
    }

    /// Assigns \p rhs to the value passed in the c'tor.
    bool Set(const VtValue& rhs) const
    {
        return BOOST_NS::apply_visitor(_Set(rhs), myValuePtr);
    }

    /// Assigns \p rhs to the value passed in the c'tor.
    template <class T>
    bool Set(T rhs) const
    {
        typedef typename BOOST_NS::remove_reference<
                    typename BOOST_NS::remove_const<T>::type>::type Type;
        return BOOST_NS::apply_visitor(_SetTyped<Type>(rhs), myValuePtr);
    }

    /// Returns \c true iff constructed with a NULL pointer.
    bool IsEmpty() const
    {
        return myValuePtr.which() == 0;
    }

    /// Returns \c true iff constructed with a NULL pointer.
    bool operator!() const
    {
        return myValuePtr.which() == 0;
    }

    /// Returns value convertable to \c true in a boolean expression iff
    /// constructed with a non-NULL pointer.
    operator _UnspecifiedBoolType() const
    {
        return IsEmpty() ? 0 : &GEO_FileFieldValue::IsEmpty;
    }

private:
    // Object representing the NULL pointer.
    class _Empty {};

    // Visitor for assignment.
    struct _Set : public BOOST_NS::static_visitor<bool> {
        _Set(const VtValue& rhs) : value(rhs) { }

        bool operator()(_Empty) const
        {
            // Convenience for "Has" methods.  Discard the value and
            // return true.
            return true;
        }

        bool operator()(VtValue* dst) const
        {
            *dst = value;
            return true;
        }

        bool operator()(SdfAbstractDataValue* dst) const
        {
            return dst->StoreValue(value);
        }

        const VtValue& value;
    };

    // Visitor for assignment.
    template <class T>
    struct _SetTyped : public BOOST_NS::static_visitor<bool> {
        _SetTyped(typename BOOST_NS::call_traits<T>::param_type rhs)
            : value(rhs)
        { }

        bool operator()(_Empty) const
        {
            // Convenience for "Has" methods.  Discard the value and
            // return true.
            return true;
        }

        bool operator()(VtValue* dst) const
        {
            *dst = value;
            return true;
        }

        bool operator()(SdfAbstractDataValue* dst) const
        {
            return dst->StoreValue(value);
        }

        typename BOOST_NS::call_traits<T>::param_type value;
    };

private:
    BOOST_NS::variant<_Empty, VtValue*, SdfAbstractDataValue*> myValuePtr;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // GEO_FILE_FIELD_VALUE_H
