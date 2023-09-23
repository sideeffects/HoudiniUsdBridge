/*
 * Copyright 2019 Side Effects Software Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Produced by:
 *	Side Effects Software Inc.
 *	123 Front Street West, Suite 1401
 *	Toronto, Ontario
 *      Canada   M5J 2M2
 *	416-504-9876
 *
 */

#ifndef __HUSD_ErrorScope_h__
#define __HUSD_ErrorScope_h__

#include "HUSD_API.h"
#include <UT/UT_Error.h>
#include <UT/UT_UniquePtr.h>

class UT_ErrorManager;
class OP_Node;

enum HUSD_ErrorCodes
{
    HUSD_ERR_STRING = 0,
    HUSD_ERR_LAYERS_STRIPPED = 1,
    HUSD_ERR_DUPLICATE_SUBLAYER = 2,
    HUSD_ERR_FIXED_INVALID_NAME = 3,
    HUSD_ERR_FIXED_INVALID_PATH = 4,
    HUSD_ERR_FIXED_INVALID_VARIANT_NAME = 5,
    HUSD_ERR_IGNORING_INSTANCE_PROXY = 6,
    HUSD_ERR_SAVED_FILE_WITH_NODE_PATH = 7,
    HUSD_ERR_SAVED_FILE = 8,
    HUSD_ERR_READ_LOCK_FAILED = 9,
    HUSD_ERR_WRITE_LOCK_FAILED = 10,
    HUSD_ERR_OVERRIDE_LOCK_FAILED = 11,
    HUSD_ERR_LAYER_LOCK_FAILED = 12,
    HUSD_ERR_CANT_FIND_LAYER = 13,
    HUSD_ERR_SAVED_FILE_WITH_EMPTY_DEFAULTPRIM = 14,
    HUSD_ERR_INVALID_DEFAULTPRIM = 15,
    HUSD_ERR_FAILED_TO_PARSE_PATTERN = 16,
    HUSD_ERR_CANT_FIND_PRIM = 17,
    HUSD_ERR_NOT_INSTANCER_PRIM = 18,
    HUSD_ERR_NOT_USD_PRIM = 19,
    HUSD_ERR_NOT_XFORMABLE_PRIM = 20,
    HUSD_ERR_NO_XFORM_FOUND = 21,
    HUSD_ERR_RELATIONSHIP_CANT_TARGET_SELF = 22,
    HUSD_ERR_CANT_COPY_PRIM_INTO_ITSELF = 23,
    HUSD_ERR_CANT_MOVE_PRIM_INTO_ITSELF = 24,
    HUSD_ERR_AUTO_REFERENCE_MISSES_SOME_DATA = 25,
    HUSD_ERR_DEFAULT_PRIM_IS_MISSING = 26,
    HUSD_ERR_LAYERS_SHARING_SAVE_PATH = 27,
    HUSD_ERR_GPRIM_MARKED_INSTANCEABLE = 28,
    HUSD_ERR_MIXED_SAVE_PATH_TIME_DEPENDENCY = 29,
    HUSD_ERR_UNABLE_TO_RELOCATE_REF = 30,
    HUSD_ERR_UNKNOWN_AUTO_COLLECTION = 31,
    HUSD_ERR_MISSING_MATERIAL_IN_TARGET = 32,
    HUSD_ERR_FAILED_TO_CREATE_ATTRIB = 33,
    HUSD_ERR_FAILED_TO_SET_ATTRIB = 34,
    HUSD_ERR_PRIM_IN_REFERENCE = 35,
    HUSD_ERR_CANT_FIND_PROPERTY = 36,
    HUSD_ERR_CANT_CREATE_PROPERTY = 37,
    HUSD_ERR_STAGE_LOCK_FAILED = 38,
    HUSD_ERR_PYTHON_ERROR = 39,
    HUSD_ERR_IGNORING_MISSING_EXPLICIT_PRIM = 40,
    HUSD_ERR_SUBSETS_ONLY_ON_MESH_PRIMITIVES = 41,
    HUSD_ERR_IGNORING_PROTOTYPE = 42,
    HUSD_ERR_LAYER_SAVE_FAILED = 43,
    HUSD_ERR_CANT_COPY_DIRECTLY_INTO_ROOT = 44,
    HUSD_ERR_EXISTENCE_TRACKING_PER_FRAME_FILES = 45,
    HUSD_PRIM_NOT_EDITABLE = 46,
    HUSD_ERR_INACTIVE_ANCESTOR_FOUND = 47,
    HUSD_ERR_SKIPPING_XFORM_ADJUST_INSTANCE_PROXY = 48,
    HUSD_ERR_FAILED_TO_APPLY_SCHEMA = 49,
    HUSD_ERR_INVALID_INTERPOLATION = 50,
    HUSD_ERR_TARGETED_MISSING_EXPLICIT_PRIM = 51,
    HUSD_ERR_CANT_FIND_MATERIAL = 52,
    HUSD_ERR_DEFAULT_VALUE_IS_VARYING = 53,
    HUSD_ERR_COMPACTING_INVALID_LAYER = 54
};

class HUSD_API HUSD_ErrorScope
{
public:
    enum CopyExistingScopeTag {
        CopyExistingScope
    };

    // Use the passed error manager if non-null, otherwise use the global one.
		 HUSD_ErrorScope();
		 HUSD_ErrorScope(UT_ErrorManager *mgr);
		 HUSD_ErrorScope(OP_Node *node);
                 HUSD_ErrorScope(CopyExistingScopeTag);
		~HUSD_ErrorScope();

    static void	 addMessage(int code, const char *msg = nullptr);
    static void	 addWarning(int code, const char *msg = nullptr);
    static void	 addError(int code, const char *msg = nullptr);

    static UT_ErrorSeverity usdOutputMinimumSeverity();
    static void  setUsdOutputMinimumSeverity(UT_ErrorSeverity severity);

    void         setErrorSeverityMapping(UT_ErrorSeverity usd_severity,
                        UT_ErrorSeverity hou_severity);

private:
    class husd_ErrorScopePrivate;

    UT_UniquePtr<husd_ErrorScopePrivate> myPrivate;
};

#endif

