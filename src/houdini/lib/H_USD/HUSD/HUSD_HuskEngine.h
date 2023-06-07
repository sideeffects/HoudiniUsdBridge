/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
 *
 * NAME:	HUSD_HuskEngine.h (HUSD Library, C++)
 *
 * COMMENTS:
 */

#ifndef __HUSD_HuskEngine__
#define __HUSD_HuskEngine__

#include "HUSD_API.h"
#include <pxr/pxr.h>
#include <UT/UT_StringHolder.h>
#include <UT/UT_UniquePtr.h>
#include <UT/UT_Rect.h>
#include "HUSD_RenderBuffer.h"

PXR_NAMESPACE_OPEN_SCOPE
class XUSD_HuskEngine;
class VtDictionary;
PXR_NAMESPACE_CLOSE_SCOPE

class PY_PyObject;
class HUSD_RenderSettings;
class UT_Options;
class UT_JSONWriter;
class UT_WorkBuffer;

/// Wrapper class around XUSD_HuskEngine that has no dependencies on pxr
class HUSD_API HUSD_HuskEngine
{
public:
    HUSD_HuskEngine();
    ~HUSD_HuskEngine();

    class HUSD_API RenderStats
    {
    public:
        RenderStats()
            : myStorage(nullptr)
        {
        }
        RenderStats(const HUSD_HuskEngine &engine)
            : myStorage(nullptr)
        {
            engine.fillStats(*this);
        }
        ~RenderStats()
        {
            freeStorage();
        }

        /// Importing counts of different types
        enum CountType
        {
            POLYGON,
            CURVE,
            POINT,
            POINT_MESH,
            VOLUME,
            PROCEDURAL,
            LIGHT,
            CAMERA,
            COORDSYS,

            PRIMARY,
            INDIRECT,
            OCCLUSION,
            LIGHT_GEO,
            PROBE,
        };

        static const char       *countType(CountType type);

        /// @{
        /// Import a value
        bool    import(int32 &val, const UT_StringRef &name) const;
        bool    import(int64 &val, const UT_StringRef &name) const;
        bool    import(fpreal32 &val, const UT_StringRef &name) const;
        bool    import(fpreal64 &val, const UT_StringRef &name) const;
        bool    import(UT_StringHolder &val, const UT_StringRef &name) const;
        bool    import(UT_Vector2i &val, const UT_StringRef &name) const;
        bool    import(UT_Vector2I &val, const UT_StringRef &name) const;
        bool    import(UT_Vector2F &val, const UT_StringRef &name) const;
        bool    import(UT_Vector2D &val, const UT_StringRef &name) const;
        bool    import(UT_Vector3i &val, const UT_StringRef &name) const;
        bool    import(UT_Vector3I &val, const UT_StringRef &name) const;
        bool    import(UT_Vector3F &val, const UT_StringRef &name) const;
        bool    import(UT_Vector3D &val, const UT_StringRef &name) const;
        bool    import(UT_Vector4i &val, const UT_StringRef &name) const;
        bool    import(UT_Vector4I &val, const UT_StringRef &name) const;
        bool    import(UT_Vector4F &val, const UT_StringRef &name) const;
        bool    import(UT_Vector4D &val, const UT_StringRef &name) const;
        /// @}

        /// Common imports
        bool    rendererName(UT_StringHolder &sval) const;
        bool    percentDone(fpreal &pct, bool final=false) const;
        bool    renderTime(fpreal &wall, fpreal &user, fpreal &sys) const;
        int64   getMemory() const;
        int64   getPeakMemory() const;

        bool    importCount(UT_Vector2I &val, CountType type) const;
        bool    importCount(int64 &val, CountType type) const;

        // Number of options in the stats
        exint   size() const;

        // Convert this object to a UT_Options
        void    fillOptions(UT_Options &opts) const;

        void    dump() const;   // For debugging
        void    dump(UT_WorkBuffer &buffer) const;
        void    dump(UT_JSONWriter &w) const;

        void    setStorage(const PXR_NS::VtDictionary &v);
        void    freeStorage();

    private:
        void    *myStorage;
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

    bool        loadStage(const UT_StringHolder &usdfile,
                          const UT_StringHolder &resolver_context_file,
                          const UT_StringHolder &mask = UT_StringHolder::theEmptyString);
    bool        isValid() const;

    const UT_StringHolder       &usdFile() const;
    time_t                       usdTimeStamp() const;

    /// Return a reference to the stage as a Python object
    PY_PyObject *pyStage() const;

    /// Create a PY_PyDict object for the render settings.  For ownership
    /// issues, this is equivalent to calling PY_PyNewDict().
    PY_PyObject *pySettingsDict(const HUSD_RenderSettings &settings) const;

    /// Return the FPS defined on the stage
    fpreal                       stageFPS() const;

    /// Entry point for kicking off a render
    bool Render(fpreal frame);

    /// Returns true if the resulting image is fully converged.
    /// (otherwise, caller may need to call Render() again to refine the result)
    bool IsConverged() const;

    /// Set the data window for rendering
    void setDataWindow(const UT_DimRect &dataWindow);

    /// Return the deleget plugin name
    UT_StringHolder     pluginName() const;

    /// Set the current delegate based on the settings
    bool setRendererPlugin(const HUSD_RenderSettings &settings,
	    const char *complexity);

    /// Once render products have been finalized, set the AOVs.
    /// Though render settings can have multiple product groups, AOVs are added
    /// for the union of all product groups.  It's assumed that the normal use
    /// case is that all product groups will share the same render vars (i.e.
    /// stereo cameras).
    bool setAOVs(const HUSD_RenderSettings &settings);

    /// Update settings for the next frame.
    /// Render Settings may have multiple product groups.  This only updates
    /// the products for the specified product group.
    void updateSettings(const HUSD_RenderSettings &settings);

    /// Send the delegate render products
    /// Render Settings may have multiple product groups.  This only generates
    /// delegate render products for the specified product group.
    void delegateRenderProducts(const HUSD_RenderSettings &settings,
                        int product_group);

    /// Set the random seed for karma
    void setKarmaRandomSeed(int seed) const;

    /// Send the mouse click position
    void mplayMouseClick(int x, int y) const;

    /// Send the "snapshot" render setting to the delegate
    void huskSnapshot() const;

    // ---------------------------------------------------------------------
    /// @name AOVs and Renderer Settings
    /// @{
    // ---------------------------------------------------------------------
    HUSD_RenderBuffer   GetRenderOutput(const UT_StringRef &name) const;

    // Render stats
    void        fillStats(RenderStats &stats) const;
    /// @}

    /// Debug - dump the stage
    void        dumpUSD() const;

    /// Find the settings primitive path for rendering
    UT_StringHolder     settingsPath(const char *path) const;

    /// Fill out the list of settings on the stage
    void        listSettings(UT_StringArray &settings) const;

    /// Fill out the list of all cameras on the stage
    void        listCameras(UT_StringArray &cameras) const;

    /// Fill out the list of all delegates
    static void listDelegates(UT_StringArray &delegates);

    /// Access to the underlying engine
    const PXR_NS::XUSD_HuskEngine       *impl() const { return myEngine.get(); }

private:
    UT_UniquePtr<PXR_NS::XUSD_HuskEngine>       myEngine;
};

#endif

