/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
 *
 * NAME:	XUSD_HuskEngine.h (karma Library, C++)
 *
 * COMMENTS:
 */

#ifndef __XUSD_HuskEngine__
#define __XUSD_HuskEngine__

#include "HUSD_API.h"
#include <UT/UT_NonCopyable.h>
#include <UT/UT_UniquePtr.h>
#include <UT/UT_String.h>
#include <UT/UT_StringHolder.h>
#include <UT/UT_Array.h>
#include <UT/UT_Rect.h>
#include <SYS/SYS_Types.h>

#include <pxr/pxr.h>
#include <pxr/usdImaging/usdImaging/version.h>
#include <pxr/imaging/hd/engine.h>
#include <pxr/imaging/hd/renderDelegate.h>
#include <pxr/imaging/hd/pluginRenderDelegateUniqueHandle.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/usd/stage.h>

#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/vec4d.h>

class PY_PyObject;

PXR_NAMESPACE_OPEN_SCOPE

class UsdPrim;
class HdRenderIndex;
class HdRendererPlugin;
class UsdImagingDelegate;
class XUSD_RenderSettings;
class XUSD_HuskTaskManager;

class HUSD_API XUSD_HuskEngine
    : UT_NonCopyable
{
public:
    XUSD_HuskEngine();
    ~XUSD_HuskEngine();

    enum RenderComplexity
    {
	COMPLEXITY_LOW,
	COMPLEXITY_MEDIUM,
	COMPLEXITY_HIGH,
	COMPLEXITY_VERYHIGH,
    };

    bool        loadStage(const UT_StringHolder &usdfile,
                        const UT_StringHolder &resolver_context_file);
    bool        isValid() const;

    const UsdStageRefPtr        &stage() const { return myStage; }
    const UT_StringHolder       &usdFile() const { return myUSDFile; }
    time_t                       usdTimeStamp() const { return myUSDTimeStamp; }
    fpreal                       stageFPS() const;

    PY_PyObject *pyStage() const;
    PY_PyObject *pySettingsDict(const XUSD_RenderSettings &settings) const;

    /// Entry point for kicking off a render
    bool Render(fpreal frame);

    /// Returns true if the resulting image is fully converged.
    /// (otherwise, caller may need to call Render() again to refine the result)
    bool IsConverged() const;

    /// Set the data window for rendering
    void setDataWindow(const UT_DimRect &dataWindow);

    /// Return the deleget plugin name
    const TfToken	pluginName() const { return myRendererId; }

    /// Release the renderer plugin
    void        releaseRendererPlugin();

    /// Set the current delegate based on the settings
    bool setRendererPlugin(const XUSD_RenderSettings &settings,
	    const char *complexity);

    /// Once render products have been finalized, set the AOVs.
    bool setAOVs(const XUSD_RenderSettings &settings);

    /// Update settings for the next frame
    void updateSettings(const XUSD_RenderSettings &settings);

    /// Send the delegate render products
    void delegateRenderProducts(const XUSD_RenderSettings &settings,
                        int productGroup);

    /// Set an arbitrary render setting
    void setRenderSetting(const TfToken &token, const VtValue &val);

    // ---------------------------------------------------------------------
    /// @name AOVs and Renderer Settings
    /// @{
    // ---------------------------------------------------------------------
    HdAovDescriptor	 defaultAovDescriptor(const TfToken &name) const;
    HdRenderBuffer	*GetRenderOutput(const TfToken &name) const;

    // Render stats
    VtDictionary	renderStats() const;
    /// @}

    /// Debug - dump the stage
    void        dumpUSD() const;

    /// Find the settings primitive path for rendering
    UT_StringHolder     settingsPath(const char *path) const;

    /// Fill out the list of settings on the stage
    void        listSettings(UT_StringArray &settings) const;

    /// Fill out the list of all cameras on the stage
    void        listCameras(UT_StringArray &cameras) const;

    /// Fill out the list of render delegates
    static void listDelegates(UT_StringArray &delegates);

private:
    void PrepareBatch(const UsdPrim &root, fpreal frame);

    bool doRender();

    // These functions factor batch preparation into separate steps so they
    // can be reused by both the vectorized and non-vectorized API.
    bool canPrepareBatch(const UsdPrim &root);
    void preSetTime(const UsdPrim &root);
    void postSetTime(const UsdPrim &root);

    // Create a hydra collection given root paths and render params.
    // Returns true if the collection was updated.
    static bool updateHydraCollection(HdRprimCollection &collection,
                          const SdfPathVector &roots);

    // This function disposes of: the render index, the render plugin,
    // the task controller, and the usd imaging delegate.
    void deleteHydraResources();

    HdEngine                            myEngine;
    UT_UniquePtr<UsdImagingDelegate>    myDelegate;
    UT_UniquePtr<XUSD_HuskTaskManager>  myTaskManager;
    UT_UniquePtr<HdRenderIndex>         myRenderIndex;
    UsdStageRefPtr                      myStage;
    UT_StringHolder                     myUSDFile;
    time_t                              myUSDTimeStamp;

    TfTokenVector        myAOVs;
    HdAovDescriptorList  myAOVDescs;

    HdRprimCollection	 myRenderCollection;
    const SdfPath	 myDelegateId;
    HdPluginRenderDelegateUniqueHandle	myPlugin;
    TfToken		 myRendererId;
    SdfPath		 myRootPath;
    SdfPathVector	 myExcludedPrimPaths;
    SdfPathVector	 myInvisedPrimPaths;
    TfTokenVector	 myRenderTags;
    HdRenderSettingsMap  myRenderSettings;
    RenderComplexity	 myComplexity;
    int			 myPercentDone;
    bool		 myIsPopulated;
};


PXR_NAMESPACE_CLOSE_SCOPE

#endif // USDIMAGINGGL_ENGINE_H
