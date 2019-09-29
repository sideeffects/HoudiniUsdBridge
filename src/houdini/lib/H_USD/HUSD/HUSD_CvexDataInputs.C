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

#include "HUSD_CvexDataInputs.h"
#include <HUSD/HUSD_DataHandle.h>

HUSD_CvexDataInputs::HUSD_CvexDataInputs()
{
}

HUSD_CvexDataInputs::~HUSD_CvexDataInputs()
{
    removeAllInputData();
}

void
HUSD_CvexDataInputs::setInputData(int idx, HUSD_AutoAnyLock *data_lock)
{
    removeInputData( idx );

    myDataHandleArray.forcedRef( idx ) = nullptr;
    myDataLockArray.forcedRef( idx ) = data_lock;
    myIsOwned.forcedRef( idx ) = false;
}

void
HUSD_CvexDataInputs::setInputData(int idx, const HUSD_DataHandle &data)
{
    removeInputData( idx );

    // Make a copy of the data handle, because the HUSD_AutoReadLock only
    // holds a reference to the HUSD_DataHandle, and the one passed in here
    // may be modified or deleted without any way for us to know.
    myDataHandleArray.forcedRef( idx ) =
        new HUSD_DataHandle(data);
    myDataLockArray.forcedRef( idx ) =
        new HUSD_AutoReadLock(*myDataHandleArray(idx));
    myIsOwned.forcedRef( idx ) = true;
}

void
HUSD_CvexDataInputs::removeInputData(int idx)
{
    UT_ASSERT(myDataHandleArray.size() == myDataLockArray.size());
    UT_ASSERT(myDataLockArray.size() == myIsOwned.size() );
    if( !myDataHandleArray.isValidIndex( idx ))
        return;

    if( myIsOwned[ idx ] )
    {
        delete myDataLockArray[ idx ];
        delete myDataHandleArray[ idx ];
    }

    myDataHandleArray[ idx ] = nullptr;
    myDataLockArray[ idx ] = nullptr;
    myIsOwned[ idx ] = false;
}

void
HUSD_CvexDataInputs::removeAllInputData()
{
    for( int i = 0; i < myDataLockArray.size(); i++ )
        removeInputData(i);
}

HUSD_AutoAnyLock *
HUSD_CvexDataInputs::getInputData(int idx) const
{
    if( myDataLockArray.isValidIndex( idx ))
        return myDataLockArray[ idx ];

    return nullptr;
}

