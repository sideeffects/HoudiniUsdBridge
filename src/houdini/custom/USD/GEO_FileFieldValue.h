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

#include <SYS/SYS_Inline.h>

#include <pxr/base/vt/value.h>
#include <pxr/pxr.h>
#include <pxr/usd/sdf/abstractData.h>

#include <variant>

PXR_NAMESPACE_OPEN_SCOPE

/// Wraps a VtValue or SdfAbstractDataValue so we can access any
/// the same way.  This type allows us to implement some methods without
/// templatizing them.
class GEO_FileFieldValue
{
public:
    /// Construct an empty any.
    GEO_FileFieldValue() = default;

    /// Construct with a pointer to any supported type-erased object \p any.
    /// If \p any is \c NULL then this object is considered to be empty.
    template <class T>
    explicit GEO_FileFieldValue(T* any)
    {
        if (any)
            myValuePtr = any;
    }

    /// Assigns \p rhs to the value passed in the c'tor.
    template <class T>
    bool Set(T &&rhs) const
    {
        return std::visit(
                [&](auto &&dst) { return setValue(dst, std::forward<T>(rhs)); },
                myValuePtr);
    }

    /// Returns \c true iff constructed with a NULL pointer.
    bool IsEmpty() const
    {
        return std::holds_alternative<std::monostate>(myValuePtr);
    }

    /// Returns value convertable to \c true in a boolean expression iff
    /// constructed with a non-NULL pointer.
    explicit operator bool() const { return !IsEmpty(); }

private:
    // Visitors for assignment.
    template <typename T>
    bool setValue(std::monostate, T &&rhs) const
    {
        // Convenience for "Has" methods.  Discard the value and return true.
        return true;
    }

    template <typename T>
    SYS_FORCE_INLINE
    bool setValue(VtValue *dst, T &&rhs) const
    {
        *dst = std::forward<T>(rhs);
        return true;
    }

    template <typename T>
    SYS_FORCE_INLINE
    bool setValue(SdfAbstractDataValue *dst, T &&rhs) const
    {
        return dst->StoreValue(std::forward<T>(rhs));
    }

private:
    std::variant<std::monostate, VtValue*, SdfAbstractDataValue*> myValuePtr;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // GEO_FILE_FIELD_VALUE_H
