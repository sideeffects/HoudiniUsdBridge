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
 * NAME:	XUSD_HydraField.h (HUSD Library, C++)
 *
 * COMMENTS:	Container for a hydra light prim (HdRprim)
 */
#ifndef XUSD_HydraField_h
#define XUSD_HydraField_h

#include <pxr/pxr.h>
#include <pxr/imaging/hd/field.h>
#include <GT/GT_Primitive.h>
#include <UT/UT_StringHolder.h>

class HUSD_HydraField;

PXR_NAMESPACE_OPEN_SCOPE

class XUSD_HydraField : public HdField
{
public:
	     XUSD_HydraField(TfToken const& typeId,
			     SdfPath const& primId,
			     HUSD_HydraField &field);
    virtual ~XUSD_HydraField();
    
    virtual void Sync(HdSceneDelegate *sceneDelegate,
                      HdRenderParam *renderParam,
                      HdDirtyBits *dirtyBits) override;

    GT_PrimitiveHandle getGTPrimitive() const;
    const UT_StringHolder &getFieldType() const;

protected:
    virtual HdDirtyBits GetInitialDirtyBitsMask() const override;

private:
    void		 dirtyVolumes(HdSceneDelegate *sceneDelegate);

    UT_StringHolder	 myFieldType;
    HUSD_HydraField	&myField;
    bool		 myDirtyFlag;
};

PXR_NAMESPACE_CLOSE_SCOPE
#endif
