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

#include "HUSD_ObjectHandle.h"
#include <UT/UT_String.h>

PXR_NAMESPACE_USING_DIRECTIVE

using namespace UT::Literal;

const UT_StringHolder	 HUSD_ObjectHandle::theRootPrimPath = "/"_sh;
const UT_StringHolder	 HUSD_ObjectHandle::theRootPrimName = "/"_sh;

HUSD_ObjectHandle::HUSD_ObjectHandle()
{
}

HUSD_ObjectHandle::HUSD_ObjectHandle(const UT_StringHolder &path,
	const UT_StringHolder &name)
    : myPath(path),
      myName(name)
{
    if (!myPath.isstring())
        myPath = theRootPrimPath;

    if (!myName.isstring())
    {
        // Figure out our name by looking at the last component of our path.
        if (myPath == theRootPrimPath)
        {
            myName = theRootPrimName;
        }
        else
        {
            UT_String    pathstr(path);
            UT_String    dirstr;
            UT_String    filestr;

            pathstr.splitPath(dirstr, filestr);
            myName = filestr;
        }
    }
}

HUSD_ObjectHandle::~HUSD_ObjectHandle()
{
}

