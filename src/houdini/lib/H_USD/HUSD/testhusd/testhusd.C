/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
 *
 * Produced by:
 *  Edward Lam
 *  Side Effects
 *  477 Richmond Street West
 *  Toronto, Ontario
 *  Canada   M5V 3E7
 *  416-504-9876
 *
 * NAME:    testhusd.C (C++)
 *
 * COMMENTS:
 *
 * Test Harness for the HUSD library
 *
 */

#include "HUSD/HUSD_Utils.h"
#include "HUSD/HUSD_DataHandle.h"
#include <CH/CH_Manager.h>
#include <FS/FS_Utils.h>
#include <UT/UT_Exit.h>
#include <UT/UT_TestManager.h>
#include <UT/UT_Thread.h>

PXR_NAMESPACE_USING_DIRECTIVE

// Copied from TEST_UNIT_MAIN.
int theMain(int argc, char *argv[])
{
    /*Ensure we reset the task scheduler to avoid hangning on exit in
     * Windows.*/
    UT_Thread::resetNumProcessors();
    // Initialize USD, and create a stage through an HUSD_DataHandle. This
    // makes sure all the worker threads are set up, and running the tests
    // won't create any additional threads.
    {
        // Create a CH_Manager
        new CH_Manager(1);
        HUSDinitialize();
        HUSD_DataHandle datahandle;
        datahandle.createNewData();
    }
    return UT_TestManager::get().run(argc, argv);
}
UT_MAIN(theMain);
