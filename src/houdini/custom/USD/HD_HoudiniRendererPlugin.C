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
#include "HD_HoudiniRendererPlugin.h"
#include <pxr/imaging/hd/rendererPluginRegistry.h>

#include <HUSD/HUSD_Scene.h>

PXR_NAMESPACE_OPEN_SCOPE

HdRenderDelegate *
HD_HoudiniRendererPlugin::CreateRenderDelegate() 
{
    return reinterpret_cast<HdRenderDelegate *>(HUSD_Scene::newDelegate());
}

HdRenderDelegate *
HD_HoudiniRendererPlugin::CreateRenderDelegate(
	HdRenderSettingsMap const& settingsMap)
{
    return reinterpret_cast<HdRenderDelegate *>(HUSD_Scene::newDelegate());
}

void
HD_HoudiniRendererPlugin::DeleteRenderDelegate(HdRenderDelegate *delegate)
{
    HUSD_Scene::
	freeDelegate(reinterpret_cast<XUSD_SceneGraphDelegate*>(delegate));
}

TF_REGISTRY_FUNCTION(TfType)
{
    HdRendererPluginRegistry::Define<HD_HoudiniRendererPlugin>();
}

PXR_NAMESPACE_CLOSE_SCOPE
