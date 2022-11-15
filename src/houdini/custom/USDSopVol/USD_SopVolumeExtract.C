/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
 *
 * Produced by:
 *	Side Effects Software Inc
 *	477 Richmond Street West
 *	Toronto, Ontario
 *	Canada   M5V 3E7
 *	416-504-9876
 *
 * NAME:	USD_VDB.C
 *
 * COMMENTS:	Function to turn a USD Field asset path that points to SOP
 *              data into a GT_Primitive that can point directly to a VDB
 *              or Houdini volume.
 */

#include <UT/UT_DSOVersion.h>
#include <HUSD/XUSD_Tokens.h>
#include <HUSD/HUSD_HydraField.h>
#include <UT/UT_StringHolder.h>
#include <SYS/SYS_Visibility.h>
#include <string>

PXR_NAMESPACE_USING_DIRECTIVE

// This code defines two simple C API functions that can be used to generate
// either a GT_PrimVDB or GT_PrimVolume pointer (returned as a void pointer)
// from a field asset path stored in a USD file. These functions are meant to
// be used by render delegates that want to convert file paths starting with
// "op:" into volume data structure pointers (though they can also load files
// from disk, either .vdb or any of the flavors of .bgeo, .bgeo.sc, etc).
//
// This code is compiled into a library that ships with Houdini as
// $HH/dso/USD_SopVol.[so | dll | dylib].
// This library can be dynamically loaded by a render delegate, and these
// function pointers extracted to allow access to in-memory volume data from
// SOPs without having to build against any Houdini libraries.

extern "C"
{
    SYS_VISIBILITY_EXPORT extern void *
    SOPgetVDBVolumePrimitive(const char *filepath,
            const char *name)
    {
        return HUSD_HydraField::getVolumePrimitive(filepath, name, -1,
            HusdHdPrimTypeTokens()->openvdbAsset.GetString());
    }

    SYS_VISIBILITY_EXPORT extern void *
    SOPgetHoudiniVolumePrimitive(const char *filepath,
            const char *name,
            int index)
    {
        return HUSD_HydraField::getVolumePrimitive(filepath, name, index,
            HusdHdPrimTypeTokens()->bprimHoudiniFieldAsset.GetString());
    }
}
