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

#ifndef __HUSD_PrimHandle_h__
#define __HUSD_PrimHandle_h__

#include "HUSD_API.h"
#include "HUSD_ObjectHandle.h"
#include "HUSD_Utils.h"
#include <UT/UT_StringHolder.h>
#include <UT/UT_ArrayStringSet.h>
#include <UT/UT_Array.h>
#include <UT/UT_Options.h>
#include <SYS/SYS_Inline.h>

enum HUSD_PrimAttribState {
    HUSD_FALSE,
    HUSD_TRUE,
    HUSD_ANIMATED_FALSE,
    HUSD_ANIMATED_TRUE,
    HUSD_OVERRIDDEN_FALSE,
    HUSD_OVERRIDDEN_TRUE,
    HUSD_NOTAPPLICABLE
};

enum HUSD_SoloState {
    HUSD_SOLO_NOSOLO,
    HUSD_SOLO_FALSE,
    HUSD_SOLO_TRUE,
    HUSD_SOLO_NOTAPPLICABLE
};

enum HUSD_PrimStatus {
    HUSD_PRIM_HASARCS,
    HUSD_PRIM_HASPAYLOAD,
    HUSD_PRIM_INSTANCE,
    HUSD_PRIM_INMASTER,
    HUSD_PRIM_NORMAL,
    HUSD_PRIM_ROOT,
    HUSD_PRIM_UNKNOWN
};

SYS_FORCE_INLINE bool
HUSDstateAsBool(HUSD_PrimAttribState state)
{
    return (state == HUSD_TRUE ||
	    state == HUSD_OVERRIDDEN_TRUE ||
	    state == HUSD_ANIMATED_TRUE);
}

class HUSD_TimeCode;
class HUSD_PropertyHandle;

// This class is a standalone wrapper around a specific primitice in a USD
// stage wrapped in an HUSD_DataHandle. It's purpose is to serve as the data
// accessor for tree nodes in the Scene Graph Tree. It should not be used for
// any other purpose, as it is extremely inefficient. Each function call locks
// the HUSD_DataHandle, queries its information, then unlocks it again. This
// is a matter of convenience for the calling pattern of the scene graph tree.
// Because it is inefficient the scene graph tree caches any information that
// comes out of this object.
//
// Anyone else tempted to use this object should use HUSD_Info instead.
class HUSD_API HUSD_PrimHandle : public HUSD_ObjectHandle
{
public:
			 HUSD_PrimHandle();
			 HUSD_PrimHandle(
				const HUSD_DataHandle &data_handle,
				const UT_StringHolder &prim_path =
                                    UT_StringHolder::theEmptyString,
				const UT_StringHolder &prim_name =
                                    UT_StringHolder::theEmptyString);
			 HUSD_PrimHandle(
				const HUSD_DataHandle &data_handle,
				const HUSD_ConstOverridesPtr &overrides,
				const UT_StringHolder &prim_path =
                                    UT_StringHolder::theEmptyString,
				const UT_StringHolder &prim_name =
                                    UT_StringHolder::theEmptyString);
    virtual		~HUSD_PrimHandle();

    virtual const HUSD_DataHandle	&dataHandle() const override
					 { return myDataHandle; }
    virtual const HUSD_ConstOverridesPtr&overrides() const override
					 { return myOverrides; }

    HUSD_PrimStatus	 getStatus() const;
    UT_StringHolder	 getPrimType() const;
    UT_StringHolder	 getVariantInfo() const;
    UT_StringHolder	 getKind() const;
    UT_StringHolder	 getDrawMode(bool *has_override = nullptr) const;
    UT_StringHolder	 getPurpose() const;
    UT_StringHolder	 getProxyPath() const;
    UT_StringHolder	 getSpecifier() const;
    HUSD_PrimAttribState getActive() const;
    HUSD_PrimAttribState getVisible(const HUSD_TimeCode &timecode) const;
    HUSD_SoloState       getSoloState() const;
    bool		 hasAnyOverrides() const;
    bool		 isDefined() const;

    bool		 hasChildren(HUSD_PrimTraversalDemands demands) const;
    void		 getChildren(UT_Array<HUSD_PrimHandle> &children,
				HUSD_PrimTraversalDemands demands) const;
    void		 getProperties(
				UT_Array<HUSD_PropertyHandle> &props,
				bool include_attributes,
				bool include_relationships) const;

    void		 updateOverrides(
				const HUSD_ConstOverridesPtr &overrides);

    // Debugging only... Do not use in production code.
    void		 getAttributeNames(
				UT_ArrayStringSet &attrib_names) const;
    void		 extractAttributes(
				const UT_ArrayStringSet &which_attribs,
				const HUSD_TimeCode &tc,
				UT_Options &values);

private:
    HUSD_DataHandle		 myDataHandle;
    HUSD_ConstOverridesPtr	 myOverrides;
};

#endif

