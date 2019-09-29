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
 * NAME:	HUSD_HydraField.h (HUSD Library, C++)
 *
 * COMMENTS:	Container for GT prim repr of a hydro geometry  prim (HdRprim)
 */
#ifndef HUSD_HydraField_h
#define HUSD_HydraField_h

#include <pxr/pxr.h>

#include "HUSD_API.h"
#include "HUSD_HydraPrim.h"

#include <GT/GT_Handles.h>
#include <UT/UT_StringHolder.h>
#include <UT/UT_Vector3.h>
#include <SYS/SYS_Types.h>

class HUSD_Scene;
typedef void * (*HUSD_SopVolumeExtractFunc)(const char *, const char *, int);

PXR_NAMESPACE_OPEN_SCOPE
class XUSD_HydraField;
class TfToken;
class SdfPath;
PXR_NAMESPACE_CLOSE_SCOPE

/// Container for hydra camera (hdSprim)
class HUSD_API HUSD_HydraField : public HUSD_HydraPrim
{
public:
     HUSD_HydraField(PXR_NS::TfToken const& typeId,
		     PXR_NS::SdfPath const& primId,
		     HUSD_Scene &scene);
    ~HUSD_HydraField();

    PXR_NS::XUSD_HydraField     *hydraField() const { return myHydraField; }
    
    HUSD_PARM(FilePath,		UT_StringHolder);
    HUSD_PARM(FieldName,	UT_StringHolder);
    HUSD_PARM(FieldIndex,	int);
   
    GT_PrimitiveHandle		 getGTPrimitive() const;

    // This static function converts a USD Field prim's attributes into a
    // GT_Primitive holding a native volume data structure. In addition to
    // being used by this class, it is also used by the USD_SopVol custom
    // library, which allows third party renderers to easily gain the same
    // capability without having to build against any Houdini libraries.
    static GT_Primitive         *getVolumePrimitive(
                                        const UT_StringRef &filepath,
                                        const UT_StringRef &fieldname,
                                        int fieldindex,
                                        const UT_StringRef &fieldtype);

private:
    UT_StringHolder                      myFilePath;
    UT_StringHolder                      myFieldName;
    int                                  myFieldIndex;
    
    PXR_NS::XUSD_HydraField             *myHydraField;
};

#endif
