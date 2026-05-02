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

#ifndef __GEO_FILE_PROP_SOURCE_H__
#define __GEO_FILE_PROP_SOURCE_H__
 
#include "pxr/pxr.h"
#include "GEO_FileFieldValue.h"
#include <gusd/UT_Gf.h>
#include <GT/GT_DataArray.h>
#include <UT/UT_IntrusivePtr.h>
#include <UT/UT_NonCopyable.h>
#include <UT/UT_TBBSpinLock.h>
#include <pxr/base/vt/array.h>
#include <pxr/base/gf/numericCast.h>

PXR_NAMESPACE_OPEN_SCOPE

class GEO_FilePropSource : public UT_IntrusiveRefCounter<GEO_FilePropSource>,
			   public UT_NonCopyable
{
public:
			 GEO_FilePropSource()
			 { }
    virtual		~GEO_FilePropSource()
			 { }

    virtual bool	 copyData(const GEO_FileFieldValue &value) = 0;
};

typedef UT_IntrusivePtr<GEO_FilePropSource> GEO_FilePropSourceHandle;

template<class T, class ComponentT = T>
class GEO_FilePropAttribSource : public GEO_FilePropSource
{
private:
    class geo_AttribForeignSource : public Vt_ArrayForeignDataSource
    {
	public:
			 geo_AttribForeignSource()
			     : Vt_ArrayForeignDataSource(detachFunction),
                               myPendingDetachCount(0)
			 { }

	    void	 setPropSource(GEO_FilePropAttribSource *prop_source)
			 {
                             // Grab the lock before manipulating either the
                             // _refCount or the myPendingDetachCount of this
                             // data source. The lock around the _refCount
                             // access is so that multiple simultaneous
                             // callers building VtArrays from this data source
                             // will bump the pending detach count once and
                             // only once. It also protect the pending detach
                             // count from simultaneous detach calls.
                             //
                             // The lock during the detach is to protect the
                             // pending detach count, and ensure that the
                             // myPropSource value is cleared only when there
                             // are no more detach calls coming in.
                             //
                             // It is guaranteed that clearing myPropSource
                             // won't delete the object it points to (and thus
                             // potentailly us as well) because both the attach
                             // and detach callers of this method guarantee
                             // that there is at least one more shared pointer
                             // to the object pointed to by myPropSource.
                             UT_TBBSpinLock::Scope lock(mySpinLock);

			     if (!prop_source)
			     {
                                 // It is fine that we may get a detach call
                                 // while another detach call is pending. This
                                 // means that between the time our owner
                                 // VtArray was deleted and we reached this
                                 // point in the code, another thread created
                                 // a VtArray from this data source.
                                 myPendingDetachCount--;
                                 if (myPendingDetachCount == 0)
                                 {
                                     UT_ASSERT(myPropSource.get());
                                     myPropSource.reset();
                                 }
			     }
                             else if (_refCount.fetch_add(1) == 0)
                             {
                                 // If there is another thread waiting to
                                 // execute a detach call, myPendingDetach
                                 // count and myPropSource may already have
                                 // non-zero values. This is fine. It is why
                                 // myPendingDetachCount exists.
                                 myPendingDetachCount++;
                                 myPropSource.reset(prop_source);
                             }
			 }

	private:
	    static void	 detachFunction(Vt_ArrayForeignDataSource *self)
			 {
			     geo_AttribForeignSource *geo_self =
				 static_cast<geo_AttribForeignSource *>(self);

                             // Hold onto myPropSource to make sure it doesn't
                             // get deleted by a subsequent call to copyData
                             // and detachFunction while this thread is stuck
                             // waiting.
                             GEO_FilePropSourceHandle
                                 hold_source(geo_self->myPropSource);

                             // No more arrays are holding onto us, so let
                             // go of our hold on our parent PropSource.
                             UT_ASSERT(hold_source.get());
                             geo_self->setPropSource(nullptr);

                             // This object may deleted as soon as we leave
                             // this function and hold_source is released (if
                             // geo_self wasn't already deleted by another
                             // thread calling detachFunction).
			 }

	    GEO_FilePropSourceHandle     myPropSource;
	    UT_TBBSpinLock               mySpinLock;
            int                          myPendingDetachCount;
    };

public:
			 GEO_FilePropAttribSource(
				 const GT_DataArrayHandle &attrib)
			     : myAttrib(attrib),
			       myData(nullptr)
			 {
			    // Verify that the GT data type matches the value type of the USD tuple,
			    // aside from the special case of GfHalf / fpreal16 which are
			    // unique but equivalent types.
			    static_assert(
			    	SYSisSame<ComponentT, typename GusdPodTupleTraits<T>::ValueType>() ||
			    	(SYSisSame<typename GusdPodTupleTraits<T>::ValueType, GfHalf>() &&
			    	 SYSisSame<ComponentT, fpreal16>()));

			    GT_DataArrayHandle	 storage;
                            myData = myAttrib->getArray<ComponentT>(storage);

			    if (storage)
				myAttrib = storage;
                         }

    bool	         copyData(const GEO_FileFieldValue &value) override
			 {
                            // If our data source is being held in an array,
                            // hold a pointer to this object in the data
                            // source. When the last array releases the data
                            // source, the "detachedFn" in the data source will
                            // eliminate the hold on this object, so the whole
                            // ball of wax can be deleted.
                            //
                            // Set this pointer before creating the VtArray to
                            // ensure that the ref count on the data source is
                            // going to be reliably zero for the first call
                            // into this method. This is the signal that we
                            // will be calling detach at some point in the
                            // future.
                            myForeignSource.setPropSource(this);

                            // Pass addRef == false because we add one to the
                            // _refCount as part of the setPropSource call.
                            VtArray<T>	 result(
                                &myForeignSource,
                                reinterpret_cast<T *>(
                                    SYSconst_cast(myData)),
                                size(),
                                false /* addRef */);

                            return value.Set(result);
			 }

    GT_Size		 size() const
			 {
                            // Since we cast myData to T, if there are multiple
                            // T's per element we need to report the appropriate
                            // array size.
                            const int entries_per_elem
                                    = myAttrib->getTupleSize()
                                      / GusdGetTupleSize<T>();
                            return myAttrib->entries() * entries_per_elem;
                         }
    const T		*data() const
			 { return reinterpret_cast<const T *>(myData); }

private:
    GT_DataArrayHandle		 myAttrib;
    const void			*myData;
    geo_AttribForeignSource	 myForeignSource;
};

/// Specialization for USD types such as std::string or SdfAssetPath.
template <typename StringT>
class GEO_FilePropAttribSource<StringT, std::string> : public GEO_FilePropSource
{
public:
			 GEO_FilePropAttribSource(
				 const GT_DataArrayHandle &attrib)
                         {
                            const int tuple_size = attrib->getTupleSize();
                            exint length = attrib->entries();
                            myValue.reserve(length * tuple_size);

                            for (exint i = 0; i < length; ++i)
                            {
                                for (exint j = 0; j < tuple_size; ++j)
                                {
                                    const GT_String str = attrib->getS(i, j);
                                    myValue.emplace_back(str.toStdString());
                                }
                            }
                         }

    bool	         copyData(const GEO_FileFieldValue &value) override
			 {
			    return value.Set(myValue);
			 }

private:
    VtArray<StringT> myValue;
};

/// Convert from a GT_DataArray to a VtArray when the types are not identical
/// (e.g. uint8 -> bool or int64 -> uint32).
template <typename UsdT, typename GtT>
VtArray<UsdT>
GEOconvertAttribData(const GT_DataArrayHandle &attrib)
{
    GT_DataArrayHandle storage;
    const GtT *gt_data = attrib->getArray<GtT>(storage);

    const exint entries = attrib->entries() * attrib->getTupleSize();

    VtArray<UsdT> values;
    values.resize(entries);

    bool reported_warning = false;

    for (exint i = 0; i < entries; ++i)
    {
    	if constexpr (SYSisSame<UsdT, bool>())
            values[i] = (gt_data[i] != 0);
        else
        {
            std::optional<UsdT> result = GfNumericCast<UsdT, GtT>(gt_data[i]);
            values[i] = result.value_or(UsdT(0));

            // Report a warning for failed numeric conversions.
            if (!result && !reported_warning)
            {
            	UT_WorkBuffer msg;
                msg.format(
                        "Cannot convert element {0} (value {1}) to the USD "
                        "data type",
                        i / attrib->getTupleSize(), gt_data[i]);
                TF_WARN("%s", msg.buffer());

            	reported_warning = true;
            }
        }
    }

    return values;
}

#define GEO_CONVERT_ATTRIB_TYPES(UsdT, GtT)                                    \
    template <>                                                                \
    class GEO_FilePropAttribSource<UsdT, GtT> : public GEO_FilePropSource      \
    {                                                                          \
    public:                                                                    \
        GEO_FilePropAttribSource(const GT_DataArrayHandle &attrib)             \
        {                                                                      \
            myValue = GEOconvertAttribData<UsdT, GtT>(attrib);                 \
        }                                                                      \
                                                                               \
        bool copyData(const GEO_FileFieldValue &value) override                \
        {                                                                      \
            return value.Set(myValue);                                         \
        }                                                                      \
                                                                               \
    private:                                                                   \
        VtArray<UsdT> myValue;                                                 \
    };

GEO_CONVERT_ATTRIB_TYPES(uint32, int64)
GEO_CONVERT_ATTRIB_TYPES(uint64, int64)
GEO_CONVERT_ATTRIB_TYPES(bool, uint8)

#undef GEO_CONVERT_ATTRIB_TYPES

template<class T>
class GEO_FilePropConstantSource : public GEO_FilePropSource
{
public:
			 GEO_FilePropConstantSource(const T &value)
			     : myValue(value)
			 { }

    bool	         copyData(const GEO_FileFieldValue &value) override
			 {
			     return value.Set(myValue);
			 }

private:
    T			 myValue;
};

template<class T>
class GEO_FilePropConstantArraySource : public GEO_FilePropSource
{
public:
			 GEO_FilePropConstantArraySource(
				 const UT_Array<T> &value)
			 { myValue.assign(value.begin(), value.end()); }

    bool	         copyData(const GEO_FileFieldValue &value) override
			 {
			     return value.Set(myValue);
			 }

private:
    VtArray<T>		 myValue;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // __GEO_FILE_PROP_SOURCE_H__
