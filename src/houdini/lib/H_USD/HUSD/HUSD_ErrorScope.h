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
};

class HUSD_API HUSD_ErrorScope
{
public:
    // Use the passed error manager if non-null, otherwise use the global one.
		 HUSD_ErrorScope(
			 bool for_html = false);
		 HUSD_ErrorScope(UT_ErrorManager *mgr,
			 bool for_html = false);
		 HUSD_ErrorScope(OP_Node *node,
			 bool for_html = false);
		~HUSD_ErrorScope();

    static void	 addMessage(int code, const char *msg = nullptr);
    static void	 addWarning(int code, const char *msg = nullptr);
    static void	 addError(int code, const char *msg = nullptr);

private:
    class husd_ErrorScopePrivate;

    UT_UniquePtr<husd_ErrorScopePrivate> myPrivate;
};

#endif

