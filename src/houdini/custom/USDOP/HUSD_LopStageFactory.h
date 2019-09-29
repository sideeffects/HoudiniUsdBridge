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
 * NAME:	HD_HoudiniRendererPlugin.C
 *
 * COMMENTS:	Render delegate for the native Houdini viewport renderer
 */

#include <HUSD/XUSD_Utils.h>

PXR_NAMESPACE_OPEN_SCOPE

class HUSD_LopStageFactory : public XUSD_StageFactory
{
public:
    virtual int			 getPriority() const override
				 { return 0; }
    virtual UsdStageRefPtr	 createStage(UsdStage::InitialLoadSet loadset,
					int nodeid) const override;
};

PXR_NAMESPACE_CLOSE_SCOPE

