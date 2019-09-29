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

#include "HUSD_AssetPath.h"

HUSD_AssetPath::HUSD_AssetPath()
    : UT_StringHolder()
{
}

HUSD_AssetPath::HUSD_AssetPath(const char *src)
    : UT_StringHolder(src)
{
}

HUSD_AssetPath::HUSD_AssetPath(const std::string &src)
    : UT_StringHolder(src)
{
}

HUSD_AssetPath::HUSD_AssetPath(const UT_StringHolder &src)
    : UT_StringHolder(src)
{
}

