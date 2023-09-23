/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
 *
 * NAME:	HUSD_RenderSettings.h (HUSD Library, C++)
 *
 * COMMENTS:
 */

#ifndef __HUSD_RenderSettings__
#define __HUSD_RenderSettings__

#include "HUSD_API.h"
#include <pxr/pxr.h>
#include <UT/UT_StringHolder.h>
#include <UT/UT_NonCopyable.h>
#include <UT/UT_UniquePtr.h>
#include <UT/UT_Rect.h>
#include <UT/UT_Vector2.h>
#include <UT/UT_Vector4.h>
#include <PXL/PXL_Common.h>

PXR_NAMESPACE_OPEN_SCOPE
class XUSD_RenderSettingsContext;
class XUSD_RenderVar;
class XUSD_RenderProduct;
class XUSD_RenderSettings;
PXR_NAMESPACE_CLOSE_SCOPE

class HUSD_HuskEngine;
class HUSD_RenderSettingsContext;
class HUSD_RenderVar;
class HUSD_RenderProduct;
class HUSD_RenderSettings;
class IMG_FileParms;

namespace HUSD_RenderTokens
{
    // From UsdRenderTokens
    HUSD_API const char *productName();
    HUSD_API const char *productType();
    HUSD_API const char *dataType();
    HUSD_API const char *aspectRatioConformPolicy();
    HUSD_API const char *dataWindowNDC();
    HUSD_API const char *disableMotionBlur();
    HUSD_API const char *pixelAspectRatio();
    HUSD_API const char *resolution();
    HUSD_API const char *raster();

    // From HdAovTokens
    HUSD_API const char *color();
    HUSD_API const char *cameraDepth();
};

/// Wrapper around XUSD_RenderSettings objects that has no dependencies on pxr
/// libraries.
class HUSD_API HUSD_RenderSettingsContext
    : public UT_NonCopyable
{
public:
    HUSD_RenderSettingsContext();
    virtual ~HUSD_RenderSettingsContext();

    /// During initialization of the HUSD_RenderSettings, this class gets a
    /// call back to initialize any defaults that make sense.  The @c
    /// lookupSetting class provides an interface to lookup very simple POD
    /// types from the render settings primitive.
    class HUSD_API lookupSetting
    {
    public:
        lookupSetting(const void *data)
            : myData(data)
        {
        }
        /// Will lookup up bool, int32 or int64 values
        bool    lookup(const char *token, int64 &val) const;

        /// Will lookup up bool, int32 or int64, fpreal32, or fpreal64
        bool    lookup(const char *token, fpreal64 &val) const;

        /// Will lookup up GfVec2i
        bool    lookup(const char *token, UT_Vector2i &val) const;
    private:
        const void      *myData;
    };

    class HUSD_API storeProperty
    {
    public:
        storeProperty(void *data)
            : myData(data)
        {
        }
        void    store(const char *token, bool val);
        void    store(const char *token, int32 val);
        void    store(const char *token, int64 val);
        void    store(const char *token, fpreal32 val);
        void    store(const char *token, fpreal64 val);
        void    store(const char *token, const char *val);
        void    store(const char *token, const std::string &val);
        void    store(const char *token, const UT_Array<const char *> &val);

        // Store as a TfToken
        void    storeTfToken(const char *token, const char *val);

    private:
        void    *myData;
    };

    // If the settings context has access to a rendering engine, this allows
    // the context to provide default AOV descriptors etc.
    virtual const HUSD_HuskEngine       *huskEngine() const { return nullptr; }

    // Initialize any state data from the RenderSettings primitive
    virtual void        initFromSettings(const lookupSetting &lookup) {}

    // Store state data in the RenderSettingsMap for the RenderSettings
    virtual void        setDefaultSettings(const HUSD_RenderSettings &settings,
                                storeProperty &writer) const {};

    // Override any state data in the RenderSettingsMap for the RenderSettings
    virtual void        overrideSettings(const HUSD_RenderSettings &settings,
                                storeProperty &writer) const {};

    virtual UT_StringHolder     renderer() const = 0;
    virtual UT_StringHolder     overrideCamera() const
                                { return UT_StringHolder(); }

    /// @{
    /// Default & override product name.  The @c raster_index is the offset
    /// into the list of ordered raster products.  If the product is @b not a
    /// raster product, the product index is -1.
    virtual const char  *defaultProductName() const { return nullptr; }
    virtual const char  *overrideProductName(const HUSD_RenderProduct &p,
                                        int raster_index) const
                            { return nullptr; }
    /// @}
    /// Default path for snapshots (in husk)
    virtual const char  *overrideSnapshotPath(const HUSD_RenderProduct &p,
                                        int raster_index) const
                            { return nullptr; }

    /// Override the snapshot suffix (in husk)
    virtual const char  *overrideSnapshotSuffix(const HUSD_RenderProduct &p,
                                        int raster_index) const
                            { return "_part"; }

    /// @{
    /// Default & override render purpose
    virtual const char  *defaultPurpose() const
    {
        const char *p = overridePurpose();
        return p ? p : "geometry,render";
    }
    virtual const char  *overridePurpose() const { return nullptr; }
    /// @}

    /// @{
    /// Resolution, aspect ratio, data window and motion blur overrides
    virtual UT_Vector2i  defaultResolution() const = 0;
    virtual UT_Vector2i  overrideResolution(const UT_Vector2i &res) const
                            { return res; }
    virtual UT_Vector4   overrideDataWindow(const UT_Vector4 &v) const
                            { return v; }
    virtual fpreal       overridePixelAspect(fpreal pa) const
                            { return pa; }
    virtual bool         overrideDisableMotionBlur(bool is) const
                            { return is; }
    /// @}

    /// @{
    /// When composing an image with tiles, these options provide the image
    /// suffix and tile index
    virtual const char  *tileSuffix() const { return nullptr; }
    virtual int          tileIndex() const { return 0; }
    /// @}

    // First frame to be rendered
    virtual fpreal	startFrame() const = 0;
    /// Frame increment, when computing sequences
    virtual fpreal	frameInc() const { return 1; }
    /// Return the number of frames being rendered
    virtual int 	frameCount() const { return 1; }
    /// Return the FPS
    virtual fpreal      fps() const { return 24; }

    /// Current frame (when rendering a sequence)
    virtual fpreal	evalTime() const = 0;

    /// Allow render options to be applied without a camera present.
    virtual bool        allowCameraless() const { return false; }

    const PXR_NS::XUSD_RenderSettingsContext    &impl() const { return *myImpl; }
    PXR_NS::XUSD_RenderSettingsContext          &impl() { return *myImpl; }
private:
    PXR_NS::XUSD_RenderSettingsContext  *myImpl;
};

class HUSD_API HUSD_RenderVar
    : public UT_NonCopyable
{
public:
    HUSD_RenderVar();
    virtual ~HUSD_RenderVar();

    /// Method to clone this render var to another render product
    UT_UniquePtr<HUSD_RenderVar>        clone() const
    {
        UT_UniquePtr<HUSD_RenderVar>    v = doClone();  // Create new object
        v->copyDataFrom(*this);
        return v;
    }

    /// @{
    /// Query settings
    UT_StringHolder     aovName() const;
    UT_StringHolder     aovToken() const;
    UT_StringHolder     dataType() const;
    UT_StringHolder     sourceName() const;
    UT_StringHolder     sourceType() const;

    PXL_DataFormat      pxlFormat() const;
    PXL_Packing         pxlPacking() const;
    /// @}

    /// @{
    /// Query the AOV settings on the underlying render settings map
    bool        lookup(const char *token, int64 &val) const;
    bool        lookup(const char *token, fpreal64 &val) const;
    bool        lookup(const char *token, UT_Vector2i &val) const;
    bool        lookup(const char *token, UT_StringHolder &val) const;
    /// @}

    /// @{
    /// For debugging
    void        dump() const;
    void        dump(UT_JSONWriter &w) const;
    /// @}

    /// @private - Data for implementation
    PXR_NS::XUSD_RenderVar      *myOwner;
protected:
    /// Create a new sub-class of HUSD_RenderVar
    virtual UT_UniquePtr<HUSD_RenderVar>        doClone() const
    {
        return UTmakeUnique<HUSD_RenderVar>();
    }
private:
    void        copyDataFrom(const HUSD_RenderVar &src);
};

class HUSD_API HUSD_RenderProduct
    : public UT_NonCopyable
{
public:
    HUSD_RenderProduct();
    virtual ~HUSD_RenderProduct();

    /// Allocate a new render variable for this product
    virtual UT_UniquePtr<HUSD_RenderVar>        newRenderVar() const;

    /// Provide a default filename
    virtual UT_StringHolder     defaultFilename() const
                                    { return UT_StringHolder();  }

    /// @{
    /// Query settings on the underlying render settings map
    bool        lookup(const char *token, int64 &val) const;
    bool        lookup(const char *token, fpreal64 &val) const;
    bool        lookup(const char *token, UT_Vector2i &val) const;
    bool        lookup(const char *token, UT_StringHolder &val) const;
    /// @}

    /// Create a "writer" to store settings in this objects settings
    HUSD_RenderSettingsContext::storeProperty   writer();

    /// Copy a property from the render settings to this render product
    /// This is typically used during initialization (before the USD product
    /// settings are applied).
    void        copySetting(const HUSD_RenderSettings &settings,
                        const char *token);

    /// Clone from a list of render vars
    void        addRenderVars(const UT_Array<const HUSD_RenderVar *> &vars);

    /// @{
    /// Access render vars
    exint                        size() const;
    const HUSD_RenderVar        *renderVar(exint i) const;
    /// @}

    /// @{
    /// Query methods
    UT_StringHolder     productType() const;
    UT_StringHolder     productName(int frame=0) const;
    UT_StringHolder     outputName() const;     // Current product name
    /// @}

    /// Test if the render product is a raster product
    bool                isRaster() const;

    /// @{
    /// For debugging
    void        dump() const;
    void        dump(UT_JSONWriter &w) const;
    /// @}

    /// Add meta data to the IMG_FileParms
    void        addMetaData(IMG_FileParms &fparms) const;

    /// @private - Data for implementation
    PXR_NS::XUSD_RenderProduct  *myOwner;
protected:
    // Access to the current filename and partname
    const UT_StringHolder       &filename() const;
    const UT_StringHolder       &partname() const;

};

class HUSD_API HUSD_RenderSettings
    : public UT_NonCopyable
{
public:
    using ProductGroup = UT_Array<HUSD_RenderProduct *>;

    HUSD_RenderSettings(const UT_StringHolder &prim_path,
            const UT_StringHolder &filename,
            time_t file_timestamp);
    virtual ~HUSD_RenderSettings();

    // Create directories required to create the file given by the path
    static bool makeFilePathDirs(const char *path);

    virtual bool        supportedDelegate(const UT_StringRef &name) const;

    /// Allocate a new HUSD_RenderProduct for this subclass
    virtual UT_UniquePtr<HUSD_RenderProduct>    newRenderProduct() const;

    /// Initialize the settings, loading the settings, products and render vars
    /// from the path given.  The context is able to override settings during
    /// initialization.
    bool        init(const HUSD_HuskEngine &engine,
                    const UT_StringHolder &settings_path,
                    HUSD_RenderSettingsContext &ctx);

    /// Return the name for the dummy render product name when there are no
    /// raster products being rendered.
    static const char         *huskNullRasterName();

    /// Resolve products defined in the engine.  This is called separately from
    /// initialization to allow the client to initialize the engine and check
    /// for valid products.  Resolving will bind the render var AOVs and set up
    /// all the settings associated with the product.  After products are
    /// resolved, the products are partitioned into product groups.
    ///
    /// If there are no "raster" products, but there are delegate render
    /// products, create a dummy raster product so that delegates will function
    /// properly.  Creation of the dummy product will set the product name to
    /// `huskNullRaster()`.  The dummy product will pick a render var
    /// referenced by the delegate render products, and will fail if there are
    /// no render vars on any delegate render products.
    bool        resolveProducts(const HUSD_HuskEngine &engine,
                    HUSD_RenderSettingsContext &ctx,
                    bool create_dummy_raster_product);


    /// Set up to render the given @c frame and @c product_group.  This will
    /// update the husk engine contained in the settings context, expand all
    /// the product filenames and optionally create output directories for the
    /// products.
    ///
    /// See `resolveProducts()` for help on `create_dummy_render_product`
    bool        updateFrame(HUSD_RenderSettingsContext &ctx,
                    int frame,
                    int product_group,
                    bool make_product_directories,
                    bool process_delegate_products,
                    bool create_dummy_render_product);

    /// @{
    /// Query settings on the underlying render setting
    bool        lookup(const char *token, int64 &val) const;
    bool        lookup(const char *token, fpreal64 &val) const;
    bool        lookup(const char *token, UT_Vector2i &val) const;
    bool        lookup(const char *token, UT_StringHolder &val) const;
    /// @}

    /// Create a "writer" to store settings in this objects settings
    HUSD_RenderSettingsContext::storeProperty   writer();

    /// @{
    /// Query settings that are shared with all products
    UT_StringHolder     renderer() const;
    void                purpose(UT_StringArray &purposes) const;
    /// @}

    /// Return the output name for all the products in the product group
    UT_StringHolder     outputName(int product_group) const;

    /// @{
    /// Query settings which can be overridden by products
    UT_StringHolder     cameraPath(const HUSD_RenderProduct *p) const;
    double              shutterOpen(const HUSD_RenderProduct *p) const;
    double              shutterClose(const HUSD_RenderProduct *p) const;
    int                 xres(const HUSD_RenderProduct *p) const;
    int                 yres(const HUSD_RenderProduct *p) const;
    UT_Vector2i         res(const HUSD_RenderProduct *p) const;
    fpreal              pixelAspect(const HUSD_RenderProduct *p) const;
    UT_Vector4          dataWindowF(const HUSD_RenderProduct *p) const;
    UT_DimRect          dataWindow(const HUSD_RenderProduct *p) const;
    bool                disableMotionBlur(const HUSD_RenderProduct *p) const;

    UT_StringHolder     cameraPath(int product_group) const
                            { return cameraPath(productInGroup(product_group)); }
    double              shutterOpen(int product_group) const
                            { return shutterOpen(productInGroup(product_group)); }
    double              shutterClose(int product_group) const
                            { return shutterClose(productInGroup(product_group)); }
    int                 xres(int product_group) const
                            { return xres(productInGroup(product_group)); }
    int                 yres(int product_group) const
                            { return yres(productInGroup(product_group)); }
    UT_Vector2i         res(int product_group) const
                            { return res(productInGroup(product_group)); }
    fpreal              pixelAspect(int product_group) const
                            { return pixelAspect(productInGroup(product_group)); }
    UT_Vector4          dataWindowF(int product_group) const
                            { return dataWindowF(productInGroup(product_group)); }
    UT_DimRect          dataWindow(int product_group) const
                            { return dataWindow(productInGroup(product_group)); }
    bool                disableMotionBlur(int product_group) const
                            { return disableMotionBlur(productInGroup(product_group)); }
    /// @}

    /// @{
    /// For debugging
    void        dump() const;
    void        dump(UT_JSONWriter &w) const;
    /// @}

    void        printSettings() const;

    /// HUSD_RenderSettings will partition the HUSD_RenderProducts into
    /// "groups".  Each group has similar properties, for example, the same
    /// rendering camera.  This method returns the number of distinct product
    /// groups.
    exint       productGroupSize() const;

    /// Get a list of all the render products in a given product group
    void        productGroup(int product_group, ProductGroup &products) const;

    /// Return the number of render products in a given render product group
    exint       productsInGroup(exint group) const;

    /// Get access to a product inside a product group
    const HUSD_RenderProduct    *product(exint prod_group, exint product) const;

    /// @private - Data for implementation
    PXR_NS::XUSD_RenderSettings *myOwner;

protected:
    bool        expandProducts(const HUSD_RenderSettingsContext &ctx,
                    int fnum,
                    int product_group);

    // Find any product in the given product group
    const HUSD_RenderProduct    *productInGroup(int product_group) const;

    exint       totalProductCount() const;
    void        allProducts(ProductGroup &group) const;

    // Adding a product - this method will fail if the product has already been
    // added to a different render setting.
    bool        addProduct(UT_UniquePtr<HUSD_RenderProduct> p, int prod_group);
    void        removeProduct(exint i);
};

#endif
