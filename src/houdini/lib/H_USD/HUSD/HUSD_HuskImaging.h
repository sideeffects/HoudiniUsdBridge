/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
 *
 * NAME:	HUSD_HuskImaging.h (HUSD Library, C++)
 *
 * COMMENTS:
 */

#ifndef __HUSD_HuskImaging__
#define __HUSD_HuskImaging__

#include "HUSD_API.h"
#include "HUSD_Utils.h"
#include "HUSD_RendererInfo.h"
#include <pxr/pxr.h>
#include <UT/UT_StringHolder.h>
#include <UT/UT_StringMap.h>
#include <UT/UT_UniquePtr.h>
#include <UT/UT_Rect.h>
#include <UT/UT_Options.h>
#include "HUSD_RenderBuffer.h"

PXR_NAMESPACE_OPEN_SCOPE
class VtDictionary;
class VtValue;
class HdAovDescriptor;
class XUSD_RenderSettings;
PXR_NAMESPACE_CLOSE_SCOPE

class PY_PyObject;
class HUSD_RenderSettings;
class HUSD_RenderSettingsContext;
class HUSD_TimeCode;
class UT_JSONValue;
class UT_JSONWriter;
class UT_WorkBuffer;
class IMG_FileParms;
class IMG_Metadata;

class HUSD_API HUSD_HuskImaging
{
public:
    struct USDContents;

    HUSD_HuskImaging(bool enable_gpu_context=false);
    ~HUSD_HuskImaging();

    // Not all render delegates support this, but provide general option to
    // configure the scene delegate.
    struct HUSD_API DelegateParms
    {
        const char      *myComplexity = "";
        bool             mySceneMaterialsEnabled = true;
        bool             mySceneLightsEnabled = true;
    };

    class HUSD_API RenderStats
    {
    public:
        RenderStats()
            : myStorage(nullptr)
            , myJSONStats(nullptr)
        {
        }
        RenderStats(const HUSD_HuskImaging &engine)
            : myStorage(nullptr)
            , myJSONStats(nullptr)
        {
            engine.fillStats(*this);
        }
        ~RenderStats()
        {
            freeStorage();
        }

        // Number of options in the stats
        exint   size() const;

        void    dump() const;   // For debugging
        void    dump(UT_WorkBuffer &buffer) const;
        void    dump(UT_JSONWriter &w) const { save(w); }

        /// Save the dictionary values to a JSON map of key/values.  Since the
        /// render stat values can have more type information, some information
        /// is lost in the direct conversion to JSON.
        bool    save(UT_JSONWriter &w) const;

        void    setStorage(const PXR_NS::VtDictionary &v);
        void    freeStorage();

        const UT_JSONValue      &jsonStats();

    private:
        const PXR_NS::VtValue   *findForImport(const UT_StringRef &name) const;
        void            *myStorage;
        UT_JSONValue    *myJSONStats;
    };

    /// Create an error delegate which delegates errors to UT_ErrorLog
    class HUSD_API UT_ErrorDelegate
    {
    public:
        class errorImpl;
        UT_ErrorDelegate(bool all_usd_errors);
        ~UT_ErrorDelegate();

    private:
        UT_UniquePtr<errorImpl> myImpl;
    };

    static UT_UniquePtr<UT_ErrorDelegate>       errorDelegate(bool all_errors)
    {
        return UTmakeUnique<UT_ErrorDelegate>(all_errors);
    }

    void        setVariantSelectionFallbacks(
                          const UT_StringMap<UT_StringArray> &fallbacks);
    bool        loadStage(const UT_StringHolder &usdfile,
                          const UT_StringHolder &resolver_context_file,
                          const UT_StringMap<UT_StringHolder> &resolver_context_strings,
                          const char *mask);
    bool        isValid() const;

    const UT_StringHolder       &usdFile() const;
    time_t                       usdTimeStamp() const;

    /// Set the render pass prim to use
    void        setRenderPassPrimPath(const UT_StringRef &path);

    /// Enable/Disable any Hydra Generative Procedural prims
    void        showHdGpProcedurals(bool show);

    /// Get the husk verbose callback and callback interval for the given
    /// delegate.  This may return an empty string.
    bool        getVerboseCallback(UT_StringHolder &callback,
                                    fpreal &interval) const;

    /// Return a reference to the stage as a Python object
    PY_PyObject *pyStage() const;

    /// Create a PY_PyDict object for the render settings.  For ownership
    /// issues, this is equivalent to calling PY_PyNewDict().
    PY_PyObject *pySettingsDict(const HUSD_RenderSettings &settings) const;

    /// Return the FPS defined on the stage
    fpreal	stageFPS() const;

    /// @{
    /// Return metadata defined on the stage .
    bool        stageMetadata(const char *token, fpreal64 &data) const;
    bool        stageMetadata(const char *token, int64 &data) const;
    bool        stageMetadata(const char *token, UT_StringHolder &data) const;
    /// @}

    /// Set up a headlight if needed
    bool	updateHeadlight(const UT_StringHolder &style, fpreal frame);

    /// Entry point for kicking off a render
    bool Render(fpreal frame);

    /// Returns true if the resulting image is fully converged.
    /// (otherwise, caller may need to call Render() again to refine the result)
    bool IsConverged() const;

    /// Set the data window for rendering
    void setDataWindow(const UT_DimRect &dataWindow);

    /// Return the delegate plugin name
    UT_StringHolder     pluginName() const;

    /// Set the current delegate based on the settings
    bool setRendererPlugin(const HUSD_RenderSettings &settings,
            const UT_StringHolder &delegate,
	    const DelegateParms &rparms);

    /// Restart the render delegate based on the settings
    bool restartRendererPlugin(const HUSD_RenderSettings &settings,
            const UT_StringHolder &delegate,
	    const DelegateParms &rparms);

    /// Once render products have been finalized, set the AOVs.
    /// Though render settings can have multiple product groups, AOVs are added
    /// for the union of all product groups.  It's assumed that the normal use
    /// case is that all product groups will share the same render vars (i.e.
    /// stereo cameras).
    bool setAOVs(const HUSD_RenderSettings &settings,
                        HUSD_CustomProductAction custom_product_action);

    /// Update settings for the next frame.
    /// Render Settings may have multiple product groups.  This only updates
    /// the products for the specified product group.
    void updateSettings(const HUSD_RenderSettings &settings);

    /// Send the delegate render products
    /// Render Settings may have multiple product groups.  This only generates
    /// delegate render products for the specified product group.
    void delegateRenderProducts(const HUSD_RenderSettings &settings,
                        int product_group);

    /// Send the delegate the raster render products
    /// Render Settings may have multiple product groups.  This only generates
    /// raster render products for the specified product group.
    void rasterRenderProducts(const HUSD_RenderSettings &settings,
                        int product_group);

    /// @{
    /// Common render stats
    struct ActiveBucket
    {
        // The dimensions for the bucket must come through in the dictionary
        // with keys: "x", "y", "width", "height".
        // - x is the pixel offset from the left side of the image
        // - y is the pixel offset from the bottom of the image
        // - width is the width of the bucket
        // - height is the height of the bucket
        UT_DimRect      myBounds;
        UT_Options      myOptions;
    };
    bool        rendererName(RenderStats &stats, UT_StringHolder &sval) const;
    int         huskErrorStatus(RenderStats &stats) const;
    bool        percentDone(RenderStats &stats,
                            fpreal &pct, bool final=false) const;
    bool        renderTime(RenderStats &stats,
                            fpreal &wall, fpreal &user, fpreal &sys) const;
    bool        renderStage(RenderStats &stats,
                            UT_StringHolder &stage) const;
    int64       renderMemory(RenderStats &stats) const;
    int64       renderPeakMemory(RenderStats &stats) const;
    bool        activeBuckets(RenderStats &stats,
                            UT_Array<ActiveBucket> &buckets) const;
    /// @}

    /// Return true if there's a light primitive on the stage
    bool                lightOnStage(const HUSD_TimeCode &tc) const;

    /// Check to see if a product type is a "disk" product (i.e. writes to the
    /// filesystem).
    bool                isDiskRenderProduct(const UT_StringRef &productType) const;

    /// This is a wrapper on: @code
    ///    addMetadata(fparms.metadata(), base_dict, render_stats);
    /// @endcode
    void addMetadata(IMG_FileParms &fparms,
                    const UT_JSONValue &base_dict,
                    const char *render_stats = "render_stats") const;

    /// Set metadata on the IMG_Metadata
    ///
    /// This uses the "husk.metadata" keys on the delegate renderer info
    /// (UsdRenderers.json) The @c base_dict should be a map of top-level
    /// metadata values.  The @render_stats is the key in the top level
    /// dictionary where the JSON version of the render stats will be
    /// accessible.  For example, the base dictionary might have: @code
    /// {
    ///    "command_line" : "husk foo.usd",
    ///    "frame"        : 42,
    ///    "fps"          : 24,
    ///    "render_stats" : {
    ///         "percent_complete" : 100,
    ///    }
    /// }
    /// @endcode
    ///
    /// The render stats dictionary will be inserted with the given key prior
    /// to expanding metadata strings.
    ///
    /// If the delegate doesn't have a @c "husk.metadata" entry in the
    /// UsdRenderers.json file, then the entire @c base_dict will be added as
    /// metadata (using the optional @c "husk.stats_metadata" to filter the
    /// metadata).  By default @c husk.stats_metadata is @c * (matching all
    /// keys).
    ///
    void addMetadata(IMG_Metadata &storage,
                    const UT_JSONValue &base_dict,
                    const char *render_stats = "render_stats") const;

    /// Perform the metadata expansion using JSON paths.  For this method, the
    /// @c @metadata parameter should be a map of a key value and a JSON path.
    /// The key can have type qualifiers (as described in IMG_Metadata.h).  For
    /// example, the string map might contain: @code
    ///    {
    ///      "float OpenEXR:frame"         : "$frame",
    ///      "string OpenEXR:HostComputer" : "${render_stats.delegate} - ${render_stats.cpu}",
    ///      "string OpenEXR:Constant"     : "This is a constant string"
    ///    }
    /// @endcode
    /// The JSON value could then look like: @code
    /// {
    ///    "frame"  : 23,
    ///    "render_stats" : {
    ///         "delegate"      : "Storm",
    ///         "cpu"           : "Z-80",
    ///    }
    ///    "unused" : "extra data that doesn't get written to the image file"
    /// }
    void addMetadata(IMG_Metadata &storage,
                        const UT_StringMap<UT_StringHolder> &metadata,
                        const UT_JSONValue &value) const;

    /// Set the random seed for karma
    void setKarmaRandomSeed(int64 seed) const;

    /// Send the mouse click position
    void mplayMouseClick(int x, int y) const;

    /// Send the "snapshot" render setting to the delegate
    void huskSnapshot() const;

    /// Send the "interactive" render setting to the delegate.  This is sent
    /// when husk renders to mplay.
    void huskInteractive() const;

    /// Called at the end of each render to pass final metadata written to
    /// output files by husk to the delegate.
    void huskConvergedMetadata(const UT_JSONValue &base_dict) const;

    // ---------------------------------------------------------------------
    /// @name AOVs and Renderer Settings
    /// @{
    // ---------------------------------------------------------------------
    HUSD_RenderBuffer   GetRenderOutput(const UT_StringRef &name) const;

    /// Get the default AOV descriptor
    void        defaultAOVDescriptor(const char *name,
                            PXR_NS::HdAovDescriptor &desc) const;

    // Render stats
    void        fillStats(RenderStats &stats) const;
    /// @}

    /// Debug - dump the stage
    void        dumpUSD() const;

    /// Find the settings primitive path for rendering
    UT_StringHolder     settingsPath(const char *path, const char *pass_path) const;

    /// Find the render pass primitive path for rendering
    UT_StringHolder     passPath(const char *path) const;

    /// Fill out the list of settings on the stage
    void        listSettings(UT_StringArray &settings) const;

    /// Fill out the list of render passes on the stage
    void        listPasses(UT_StringArray &passes) const;

    /// Fill out the list of all cameras on the stage
    void        listCameras(UT_StringArray &cameras) const;

    /// Fill out the list of all delegates.  The function returns true if some
    /// delegates are unsupported.
    static bool listDelegates(UT_StringArray &delegates);

    /// @private method
    bool        initSettings(PXR_NS::XUSD_RenderSettings &settings,
                            const char *settings_path,
                            HUSD_RenderSettingsContext &ctx) const;
    /// @private method
    bool        updateSettings(PXR_NS::XUSD_RenderSettings &settings,
                            HUSD_RenderSettingsContext &ctx,
                            HUSD_CustomProductAction custom_product_action) const;
    /// @private method
    bool        resolveProducts(PXR_NS::XUSD_RenderSettings &settings,
                            HUSD_RenderSettingsContext &ctx,
                            HUSD_CustomProductAction custom_product_action) const;

private:
    void	deleteHydraResources();

    UT_UniquePtr<USDContents>	myContents;
    HUSD_RendererInfo           myRendererInfo;
    UT_StringHolder		myUSDFile;
    time_t			myUSDTimeStamp;
    int				myComplexity;
    bool			mySceneMaterials;
    bool			mySceneLights;
    bool                        myEnableGPU;
};

#endif

