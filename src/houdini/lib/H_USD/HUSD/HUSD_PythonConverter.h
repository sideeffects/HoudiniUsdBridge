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

#ifndef __HUSD_PythonConverter_h__
#define __HUSD_PythonConverter_h__

#include "HUSD_API.h"
#include "HUSD_DataHandle.h"
#include "HUSD_Overrides.h"

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
    void		*getSourceLayer(int layerindex) const;
    int			 getSourceLayerCount() const;

    // Functions for getting python objects representing a layer from an
    // HUSD_ConstOverridesPtr. Only read-only access is supported.
    void		*getOverridesLayer(
				HUSD_OverridesLayerId layer_id) const;

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

