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
 */


#include "HUSD_CvexCode.h"

HUSD_CvexCode::HUSD_CvexCode( const UT_StringRef &cmd_or_vexpr, bool is_cmd )
    : mySource( cmd_or_vexpr )
    , myIsCommand( is_cmd )
    , myReturnType( HUSD_CvexCode::ReturnType::NONE )
{
}

