/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
 *
 * Produced by:
 *	Side Effects Software Inc
 *	123 Front Street West, Suite 1401
 *	Toronto, Ontario
 *	Canada   M5J 2M2
 *	416-504-9876
 *
 * NAME:	HUSD Library (C++)
 *
 */

#ifndef __HUSD_XformEditor_h__
#define __HUSD_XformEditor_h__

#include "HUSD_API.h"
#include "HUSD_Xform.h"
#include <GU/GU_DetailHandle.h>
#include <UT/UT_Matrix4.h>
#include <UT/UT_StringHolder.h>
#include <UT/UT_StringMap.h>
#include <functional>

class GU_Detail;
class HUSD_AutoAnyLock;
class HUSD_AutoWriteLock;
class HUSD_Path;
class HUSD_PathSet;
class HUSD_TimeCode;

class HUSD_API HUSD_XformEditor
{
public:
    static GU_DetailHandle  getDeltaWithParmXforms(
                                    const UT_StringArray &changepaths,
                                    const HUSD_TimeCode &timecode,
                                    const GU_ConstDetailHandle &delta,
                                    const UT_Matrix4D *local_parm_xform,
                                    const UT_Matrix4D *local_parm_pivot_xform,
                                    const UT_Matrix4D *global_parm_xform,
                                    bool set_pivot_on_primary_prim,
                                    HUSD_AutoAnyLock &lock,
                                    bool exclude_edits);
    static UT_StringHolder  applyEdits(HUSD_AutoWriteLock &writelock,
                                    const GU_Detail *gdp,
                                    bool apply_inverse_xforms,
                                    const UT_StringRef &xform_description,
                                    const HUSD_XformStyle xform_style,
                                    const HUSD_TimeCode &timecode,
                                    std::function<bool(const UT_StringRef &path)> allow_edit_fn,
                                    HUSD_PathSet &modified_paths,
                                    bool &time_varying);
};

#endif
