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
 *
 */

#ifndef DEV_HUSD_SCENEDOCTOR_H
#define DEV_HUSD_SCENEDOCTOR_H

#include "HUSD_API.h"
#include "HUSD_FindPrims.h"
#include "HUSD_Path.h"
#include <PY/PY_CompiledCode.h>
#include <UT/UT_Array.h>
#include <UT/UT_ThreadSpecificValue.h>

class HUSD_API HUSD_SceneDoctor
{
public:
    enum ValidationErrors
    {
        UNDEFINED = -1,
        // kind
        PARENT_PRIM_IS_NONE_KIND = 0,
        COMPONENT_HAS_MODEL_CHILD = 1,
        SUBCOMPONENT_HAS_MODEL_CHILD = 2,
        // gprims
        GPRIM_TYPE_HAS_CHILD = 3,
        // primvar
        PRIMVAR_ARRAY_LENGTH_MISMATCH = 4,
        INTERPOLATION_TYPE_MISMATCH = 5,
        PRIM_ARRAY_LENGTH_MISMATCH = 6,
        INVALID_PRIMVAR_INDICES = 7,
        // value clips
        MISSING_VALUECLIP_MANIFEST = 8,
        // python
        PYTHON_EXCEPTION = 9,
    };
    struct ValidationError
    {
        HUSD_Path myPath;
        int myErrorState;
        ValidationError(HUSD_Path path, int errorState)
            : myPath{path}, myErrorState{errorState} {}
        bool operator<(const ValidationError &message) const
            { return myPath < message.myPath; }
        bool operator==(const ValidationError &message) const
            { return myPath == message.myPath; }
    };
    struct ValidationFlags
    {
        bool myValidateKind = false;
        bool myValidateGprims = false;
        bool myValidatePrimvars = false;
        bool myValidateValueClips = false;
    };
    HUSD_SceneDoctor(HUSD_AutoAnyLock &lock, ValidationFlags &flags);
    ~HUSD_SceneDoctor();
    void validate(UT_Array<ValidationError> &errors, const HUSD_FindPrims *prims);
    bool validatePython(const HUSD_FindPrims *validationPrims,
            const HUSD_FindPrims *collectionPrim,
            const UT_String &collectionName,
            PY_CompiledCode &pyExpr);
private:
    HUSD_AutoAnyLock &myLock;
    ValidationFlags myFlags;
    UT_Array<ValidationError> myValidationErrors;
};

#endif // DEV_HUSD_SCENEDOCTOR_H
