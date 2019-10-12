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
 * NAME:	HD_HoudiniRendererPlugin.h
 *
 * COMMENTS:	Render delegate for the native Houdini viewport renderer
 */
#include <pxr/pxr.h>
#include <pxr/imaging/hd/rendererPlugin.h>

#include <HUSD/HUSD_Scene.h>

PXR_NAMESPACE_OPEN_SCOPE

// -------------------------------------------------------------------------
// Render plugin

class HD_HoudiniRendererPlugin final : public HdRendererPlugin
{
public:
	     HD_HoudiniRendererPlugin() = default;
    virtual ~HD_HoudiniRendererPlugin() = default;

    virtual HdRenderDelegate *CreateRenderDelegate() override;
    virtual HdRenderDelegate *CreateRenderDelegate(
	    HdRenderSettingsMap const& settingsMap) override;
    virtual void DeleteRenderDelegate(HdRenderDelegate *delegate) override;

    virtual bool IsSupported() const override
        { return HUSD_Scene::hasScene(); }
   
private:
    // This class does not support copying.
    HD_HoudiniRendererPlugin(const HD_HoudiniRendererPlugin&)=delete;
    HD_HoudiniRendererPlugin &operator=(const HD_HoudiniRendererPlugin&)=delete;
};

PXR_NAMESPACE_CLOSE_SCOPE
