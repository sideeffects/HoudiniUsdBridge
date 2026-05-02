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
 *
 * Produced by:
 *	Side Effects Software Inc.
 *	123 Front Street West, Suite 1401
 *	Toronto, Ontario
 *      Canada   M5J 2M2
 *	416-504-9876
 *
 */

#ifndef __HUSD_PythonConverter_h__
#define __HUSD_PythonConverter_h__

#include "HUSD_API.h"
#include "HUSD_DataHandle.h"
#include "HUSD_Overrides.h"

class GU_DetailHandle;

class HUSD_API HUSD_PythonConverter
{
public:
			 HUSD_PythonConverter(
				HUSD_AutoAnyLock &lock);
			 HUSD_PythonConverter(
				const HUSD_ConstOverridesPtr &overrides);
			~HUSD_PythonConverter();

    // Functions for getting python objects representing a stage or layer
    // from an HUSD_AutoAnyLock. The "Editable" methods only work if the
    // lock is an HUSD_AutoWriteLock.
    void		*getEditableLayer() const;
    void		*getEditableOverridesLayer() const;
    void		*getActiveLayer() const;
    void		*getEditableStage() const;
    void		*getEditableOverridesStage() const;
    void		*getStage() const;
    void		*getPrim(const UT_StringRef &primpath) const;
    void		*getSourceLayer(int layerindex) const;
    int			 getSourceLayerCount() const;

    // Functions for getting python objects representing a layer from an
    // HUSD_ConstOverridesPtr. Only read-only access is supported.
    void		*getOverridesLayer(
				HUSD_OverridesLayerId layer_id) const;

    std::string		 addLockedGeo(
				const UT_StringHolder &identifier,
				const std::map<std::string, std::string> &args,
				const GU_DetailHandle &gdh) const;
    bool                 addHeldLayer(const UT_StringRef &identifier) const;
    bool                 addSubLayer(const UT_StringRef &identifier) const;

    // Returns the lock the converter holds.
    HUSD_AutoAnyLock	*getLock() const
			 { return myAnyLock; }

private:
    HUSD_AutoAnyLock		*myAnyLock;
    HUSD_ConstOverridesPtr	 myOverrides;
};


// ===========================================================================
// Utility class for managing the python converter.
class HUSD_API HUSD_ScopedPythonConverter
{
public:
    /// Creates a python converter and sets the pointer reference to it.
    /// Referenced pointer's lifetime must be greater than object of this class.
    HUSD_ScopedPythonConverter( HUSD_AutoAnyLock &lock,
	    HUSD_PythonConverter **converter_ptr )
	: myPythonConverter( lock )
	, myPythonConverterPtr( converter_ptr )
    {
	// Set the ref to member converter. It sets the ctor's parameter too.
	if(  myPythonConverterPtr)
	    *myPythonConverterPtr = &myPythonConverter;
    }

    ~HUSD_ScopedPythonConverter()
    {
	// The python converter object is being destroyed, so clear the ref.
	if(  myPythonConverterPtr)
	    *myPythonConverterPtr = nullptr;
    }

private:
    HUSD_PythonConverter      myPythonConverter;
    HUSD_PythonConverter    **myPythonConverterPtr;

};

#endif

