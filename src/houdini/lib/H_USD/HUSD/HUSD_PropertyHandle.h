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

#ifndef __HUSD_PropertyHandle_h__
#define __HUSD_PropertyHandle_h__

#include "HUSD_API.h"
#include "HUSD_ObjectHandle.h"
#include "HUSD_PrimHandle.h"

class PI_EditScriptedParm;

#define HUSD_PROPERTY_VALUETYPE			"usdvaluetype"
#define HUSD_PROPERTY_VALUETYPE_RELATIONSHIP	"relationship"
#define HUSD_PROPERTY_VALUETYPE_XFORM		"xform"
#define HUSD_PROPERTY_APISCHEMA			"usdapischema"
#define HUSD_PROPERTY_XFORM_PARM_PREFIX		"xformparmprefix"

// This class is a standalone wrapper around a specific property in a USD
// stage wrapped in an HUSD_DataHandle. It's purpose is to serve as the data
// accessor for tree nodes in the Scene Graph Tree. It should not be used for
// any other purpose, as it is extremely inefficient. Each function call locks
// the HUSD_DataHandle, queries its information, then unlocks it again. This
// is a matter of convenience for the calling pattern of the scene graph tree.
// Because it is inefficient the scene graph tree caches any information that
// comes out of this object.
//
// Anyone else tempted to use this object should use HUSD_Info instead.
class HUSD_API HUSD_PropertyHandle : public HUSD_ObjectHandle
{
public:
				 HUSD_PropertyHandle();
				 HUSD_PropertyHandle(
					const HUSD_PrimHandle &prim_handle,
					const UT_StringHolder &property_name);
    virtual			~HUSD_PropertyHandle();

    virtual const HUSD_DataHandle	&dataHandle() const override
					 { return myPrimHandle.dataHandle(); }
    virtual const HUSD_ConstOverridesPtr&overrides() const override
					 { return myPrimHandle.overrides(); }
    const HUSD_PrimHandle		&primHandle() const
					 { return myPrimHandle; }

    UT_StringHolder		 getSourceSchema() const;
    bool			 isCustom() const;
    bool			 isXformOp() const;

    void			 createScriptedParms(
					UT_Array<PI_EditScriptedParm *> &parms,
					const UT_StringRef &custom_name,
					bool prepend_control_parm,
					bool prefix_xform_parms) const;

private:
    void			 createScriptedControlParm(
					UT_Array<PI_EditScriptedParm *> &parms,
					const UT_String &propbasename) const;

    HUSD_PrimHandle		 myPrimHandle;
};

#endif

