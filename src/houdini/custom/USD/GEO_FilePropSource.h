//
// Copyright 2016 Pixar
//
// Licensed under the Apache License, Version 2.0 (the "Apache License")
// with the following modification; you may not use this file except in
// compliance with the Apache License and the following modification to it:
// Section 6. Trademarks. is deleted and replaced with:
//
// 6. Trademarks. This License does not grant permission to use the trade
//    names, trademarks, service marks, or product names of the Licensor
//    and its affiliates, except as required to comply with Section 4(c) of
//    the License and to reproduce the content of the NOTICE file.
//
// You may obtain a copy of the Apache License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the Apache License with the above modification is
// distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied. See the Apache License for the specific
// language governing permissions and limitations under the Apache License.
//
#ifndef __GEO_FILE_PROP_SOURCE_H__
#define __GEO_FILE_PROP_SOURCE_H__
 
#include "pxr/pxr.h"
#include "GEO_FileFieldValue.h"
#include <GT/GT_DataArray.h>
#include <UT/UT_IntrusivePtr.h>
#include <UT/UT_NonCopyable.h>
#include <UT/UT_TBBSpinLock.h>
#include <pxr/base/vt/array.h>

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
			     : Vt_ArrayForeignDataSource(detachFunction)
			 { }

	    void	 setPropSource(GEO_FilePropAttribSource *prop_source)
			 {
			     if (!prop_source)
			     {
				 // We are trying to clear the shared pointer
				 // because the last array pointing to us has
				 // been destroyed.
				 UT_TBBSpinLock::Scope	 lock_scope(mySpinLock);

				 // Make sure we weren't added to an array
				 // by another thread since the last time we
				 // looked.
				 if (_refCount.load() == 0)
				    myPropSource.reset();
			     }
			     // Do a quick check that myPropSource is null
			     // before locking to set it. No race condition
			     // here because setting the shared pointer a
			     // second time is perfectly safe (just wasteful).
			     else if (!myPropSource.get())
			     {
				 // We have been added to an array, and so
				 // are setting our shared pointer to avoid
				 // deleting the prop source.
				 UT_TBBSpinLock::Scope	 lock_scope(mySpinLock);

				 // Because of the way this method is called,
				 // our refcount can't go to zero between the
				 // above check and acquiring the lock, so we
				 // can just set the pointer (inside a lock
				 // in case another thread is setting the
				 // pointer at the same time).
				 UT_ASSERT(_refCount.load() > 0);
				 myPropSource.reset(prop_source);
			     }
			 }

	private:
	    static void	 detachFunction(Vt_ArrayForeignDataSource *self)
			 {
			     geo_AttribForeignSource *geo_self =
				 static_cast<geo_AttribForeignSource *>(self);

			     // No more arrays are holding onto us, so let go
			     // of our hold on our parent PropSource. Note that
			     // we may be deleted as soon as we release this
			     // shared pointer.
			     geo_self->setPropSource(nullptr);
			 }

	    GEO_FilePropSourceHandle		 myPropSource;
	    UT_TBBSpinLock			 mySpinLock;
    };

public:
			 GEO_FilePropAttribSource(
				 const GT_DataArrayHandle &attrib)
			     : myAttrib(attrib),
			       myData(nullptr)
			 {
			    GT_DataArrayHandle	 storage;

			    if (SYSisSame<ComponentT, uint8>())
			    {
				myData = myAttrib->getU8Array(storage);
			    }
			    else if (SYSisSame<ComponentT, int8>())
			    {
				myData = myAttrib->getI8Array(storage);
			    }
			    else if (SYSisSame<ComponentT, int16>())
			    {
				myData = myAttrib->getI16Array(storage);
			    }
			    else if (SYSisSame<ComponentT, int32>())
			    {
				myData = myAttrib->getI32Array(storage);
			    }
			    else if (SYSisSame<ComponentT, int64>())
			    {
				myData = myAttrib->getI64Array(storage);
			    }
			    else if (SYSisSame<ComponentT, fpreal16>())
			    {
				myData = myAttrib->getF16Array(storage);
			    }
			    else if (SYSisSame<ComponentT, fpreal32>())
			    {
				myData = myAttrib->getF32Array(storage);
			    }
			    else if (SYSisSame<ComponentT, fpreal64>())
			    {
				myData = myAttrib->getF64Array(storage);
			    }

			    if (storage)
				myAttrib = storage;

			    UT_ASSERT(myData);
			 }

    virtual bool	 copyData(const GEO_FileFieldValue &value)
			 {
			    if (myData)
			    {
				VtArray<T>	 result(
				    &myForeignSource,
				    reinterpret_cast<T *>(
					SYSconst_cast(myData)),
				    myAttrib->entries());

				// If our data source is being held in an
				// array, hold a pointer to this object in the
				// data source. When the last array releases
				// the data source, the "detachedFn" in the
				// data source will eliminate the hold on this
				// object, so the whole ball of wax can be
				// deleted.
				//
				// Set this pointer after creating the VtArray
				// to ensure that the ref count on the data
				// source is at least one when we set the
				// shared pointer, so we don't need to worry
				// about another thread coming in and setting
				// the shared pointer to null from within
				// the detachedFn after we set it non-null,
				// but before we have incremented the data
				// source ref counter.
				myForeignSource.setPropSource(this);

				return value.Set(result);
			    }

			    return false;
			 }

    GT_Size		 size() const
			 { return myAttrib->entries(); }
    const T		*data() const
			 { return reinterpret_cast<const T *>(myData); }

private:
    GT_DataArrayHandle		 myAttrib;
    const void			*myData;
    geo_AttribForeignSource	 myForeignSource;
};

template<>
class GEO_FilePropAttribSource<std::string, std::string> :
    public GEO_FilePropSource
{
public:
			 GEO_FilePropAttribSource(
				 const GT_DataArrayHandle &attrib)
			     : myValue(attrib->entries())
			 {
			    exint	 length = attrib->entries();

			    for (exint i = 0; i < length; ++i)
			    {
				const GT_String	str = attrib->getS(i);

				if (str)
				    myValue[i] = str;
			    }
			 }

    virtual bool	 copyData(const GEO_FileFieldValue &value)
			 {
			    return value.Set(myValue);
			 }

    GT_Size		 size() const
			 { return myValue.size(); }
    const std::string	*data() const
			 { return myValue.data(); }

private:
    VtArray<std::string> myValue;
};


template<class T>
class GEO_FilePropConstantSource : public GEO_FilePropSource
{
public:
			 GEO_FilePropConstantSource(const T &value)
			     : myValue(value)
			 { }

    virtual bool	 copyData(const GEO_FileFieldValue &value)
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

    virtual bool	 copyData(const GEO_FileFieldValue &value)
			 {
			     return value.Set(myValue);
			 }

private:
    VtArray<T>		 myValue;
};


PXR_NAMESPACE_CLOSE_SCOPE

#endif // __GEO_FILE_PROP_SOURCE_H__
