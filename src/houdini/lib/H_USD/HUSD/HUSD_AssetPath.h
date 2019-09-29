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

#ifndef __HUSD_AssetPath_h__
#define __HUSD_AssetPath_h__

#include "HUSD_API.h"
#include <UT/UT_StringHolder.h>

// Simple subclass of UT_StringHolder that can be used to indicate that a
// string actually represents an asset path, rather than a raw string. Allows
// templated functions to match a string to an SdfAssetPath.
class HUSD_API HUSD_AssetPath : public UT_StringHolder
{
public:
			 HUSD_AssetPath();
			 HUSD_AssetPath(const char *src);
			 HUSD_AssetPath(const std::string &src);
			 HUSD_AssetPath(const UT_StringHolder &src);
};

#endif

