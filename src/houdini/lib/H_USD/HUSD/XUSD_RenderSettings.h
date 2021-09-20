/*
 * Copyright 2019 Side Effects Software Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * NAME:	XUSD_RenderSettings.h (karma Library, C++)
 *
 * COMMENTS:
 */

#ifndef __XUSD_RenderSettings__
#define __XUSD_RenderSettings__

#include "HUSD_API.h"
#include <PXL/PXL_Common.h>
#include <UT/UT_Array.h>
#include <UT/UT_NonCopyable.h>
#include <UT/UT_Rect.h>
#include <UT/UT_StringHolder.h>
#include <UT/UT_UniquePtr.h>
#include <SYS/SYS_Types.h>
#include <pxr/pxr.h>
#include <pxr/imaging/hd/aov.h>
#include <pxr/imaging/hd/renderDelegate.h>
#include <pxr/usd/usdRender/product.h>
#include <pxr/usd/usdRender/settings.h>
#include <pxr/usd/usdRender/var.h>

#include <string>

class UT_JSONWriter;

PXR_NAMESPACE_OPEN_SCOPE

class XUSD_RenderProduct;
class XUSD_RenderSettings;

class HUSD_API XUSD_RenderSettingsContext
    : public UT_NonCopyable
{
public:
    XUSD_RenderSettingsContext() {}
    virtual ~XUSD_RenderSettingsContext();

    /// Update any settings from the render settings primitive.  This allows
    /// the context to look at custom attributes on the RenderSettings.
    ///
    /// This function will always be called - even if there are no settings.
    virtual void	initFromUSD(UsdRenderSettings &settings) { }

    /// Return the name of the render delegate
    virtual TfToken	renderer() const = 0;

    /// Override the path to the camera
    virtual SdfPath	overrideCamera() const
    {
	return SdfPath();
    }

    /// Return the default resolution for rendering products
    virtual GfVec2i	defaultResolution() const = 0;

    /// Optionally override the resolution of the product
    virtual GfVec2i	overrideResolution(const GfVec2i &res) const
    {
	return res;
    }

    /// Optionally, override the pixel aspect ratio.
    virtual fpreal	overridePixelAspect(fpreal pa) const { return pa; }

    /// Optionally, override the data window
    virtual GfVec4f	overrideDataWindow(const GfVec4f &w) const { return w; }

    /// Optionally, override the instantaneousShutter
    virtual bool	overrideInstantaneousShutter(bool v) const { return v; }

    /// Return if there's an overridden purpose for the render
    virtual const char	*overridePurpose() const { return nullptr; }

    /// Return the default purpose (this is a comma separated list)
    virtual const char	*defaultPurpose() const
    {
	const char *p = overridePurpose();
	if (!p)
	    p = "geometry,render";
	return p;
    }

    /// Start frame for a render sequence
    virtual fpreal	startFrame() const = 0;

    /// Frame increment, when computing sequences
    virtual fpreal	frameInc() const { return 1; }

    /// Return the number of frames being rendered
    virtual int		frameCount() const { return 1; }

    /// Return the fps
    virtual fpreal	fps() const { return 24; }

    /// Current frame in the render sequence
    virtual UsdTimeCode	evalTime() const = 0;

    /// Get a default rendering descriptor for a given AOV
    virtual HdAovDescriptor defaultAovDescriptor(const TfToken &aov) const
    {
	return HdAovDescriptor();
    }

    /// Default product name
    virtual const char	*defaultProductName() const { return nullptr; }

    /// Return a product name override
    virtual const char	*overrideProductName(const XUSD_RenderProduct &p) const
    {
        return nullptr;
    }

    /// Optionally, override the path to the snapshots
    virtual const char	*overrideSnapshotPath(const XUSD_RenderProduct &p) const
    {
        return nullptr;
    }
    /// Optionally, override the suffix on snapshots
    virtual const char	*overrideSnapshotSuffix(const XUSD_RenderProduct &p) const
    {
        return "_part";
    }


    /// Get the tile suffix, if there is one
    virtual const char	*tileSuffix() const { return nullptr; }

    /// Get the tile index, defaults to 0
    virtual int		tileIndex() const { return 0; }

    /// Build initial render settings map
    virtual void	setDefaultSettings(const XUSD_RenderSettings &rset,
				HdRenderSettingsMap &settings) const
    { }

    /// After the products have been loaded, apply any overrides
    virtual void	overrideSettings(const XUSD_RenderSettings &rset,
				HdRenderSettingsMap &settings) const
    { }

    /// Allow render options to be applied without a camera present.
    virtual bool        allowCameraless() const { return false; }
};

class HUSD_API XUSD_RenderVar
    : public UT_NonCopyable
{
public:
    XUSD_RenderVar();
    virtual ~XUSD_RenderVar();

    bool	loadFrom(const UsdRenderVar &prim,
                        const XUSD_RenderSettingsContext &ctx);
    bool	resolveFrom(const UsdRenderVar &prim,
                        const XUSD_RenderSettingsContext &ctx);
    bool	buildDefault(const XUSD_RenderSettingsContext &ctx);

    virtual UT_UniquePtr<XUSD_RenderVar>        clone() const;

    const std::string	        &aovName() const { return myAovName; }
    const TfToken	        &aovToken() const { return myAovToken; }
    const TfToken	        &dataType() const;
    const std::string	        &sourceName() const;
    const TfToken	        &sourceType() const;

    const HdAovDescriptor	&desc() const { return myHdDesc; }
    PXL_DataFormat	         pxlFormat() const { return myDataFormat; }
    PXL_Packing		         pxlPacking() const { return myPacking; }

    /// Print out the settings
    void	                 dump(UT_JSONWriter &w) const;

protected:
    HdAovDescriptor	myHdDesc;
    std::string		myAovName;
    TfToken		myAovToken;
    PXL_DataFormat	myDataFormat;
    PXL_Packing		myPacking;
};

class HUSD_API XUSD_RenderProduct
    : public UT_NonCopyable
{
public:
    using RenderVarList = UT_Array<UT_UniquePtr<XUSD_RenderVar>>;
    using const_iterator = RenderVarList::const_iterator;

    XUSD_RenderProduct();
    virtual ~XUSD_RenderProduct();

    bool	 loadFrom(const UsdStageRefPtr &usd,
			const UsdRenderProduct &prim,
			const XUSD_RenderSettingsContext &ctx);
    bool	 resolveFrom(const UsdStageRefPtr &usd,
			const UsdRenderProduct &prim,
			const XUSD_RenderSettingsContext &ctx);
    bool	 buildDefault(const XUSD_RenderSettingsContext &ctx);

    void         updateSettings(const UsdStageRefPtr &use,
                        const UsdRenderProduct &prim,
                        const XUSD_RenderSettingsContext &ctx);

    const TfToken       &productType() const;
    TfToken              productName(int frame = 0) const;

    // Current output filename (with all variables expanded)
    const UT_StringHolder	&outputName() const { return myFilename; }

    const RenderVarList	        &vars() const { return myVars; }

    /// @{
    /// Properties that can override settings defined on a render settings
    /// primitive The functions return true if they are authored on the
    /// product.
    template <typename T>
    struct SettingOverride
    {
        void    clear() { myAuthored = false; }
        bool    import(T &val) const
        {
            if (myAuthored)
            {
                val = myValue;
                return true;
            }
            return false;
        }
        T       myValue;
        bool    myAuthored = false;
    };
    bool   cameraPath(SdfPath &val) const
    {
        if (myCameraPath.IsEmpty())
            return false;
        val = myCameraPath;
        return true;
    }
    bool   shutter(GfVec2d &val) const { return myShutter.import(val); }
    bool   res(GfVec2i &val) const { return myRes.import(val); }
    bool   pixelAspect(float &val) const { return myPixelAspect.import(val); }
    bool   dataWindow(GfVec4f &val) const { return myDataWindowF.import(val); }
    bool   instantaneousShutter(bool &val) const { return myInstantShutter.import(val); }
    /// @}

    /// @{
    /// Test whether the product list has a specific value for the given
    /// attribute.  If so, overwrite the value with the value of the product
    /// list.  Note, all products must author the attribute and have the same
    /// value.
    using ProductList = UT_Array<UT_UniquePtr<XUSD_RenderProduct>>;
    static void specificRes(GfVec2i &val, const ProductList &products);
    static void specificPixelAspect(float &val, const ProductList &products);
    static void specificDataWindow(GfVec4f &val, const ProductList &products);
    static void specificInstantaneousShutter(bool &val, const ProductList &products);
    /// @}

    const SdfPath       &cameraPath() const { return myCameraPath; }

    const_iterator      begin() const { return myVars.begin(); }
    const_iterator      end() const { return myVars.end(); }

    /// Expand product name variables.  Returns false if there are multiple
    /// frames, but no frame expansion.
    bool   expandProduct(const XUSD_RenderSettingsContext &opts, int frame);
    bool   collectAovs(TfTokenVector &aovs, HdAovDescriptorList &descs) const;

    /// User settings for this product
    const HdAovSettingsMap	&settings() const { return mySettings; }

    /// Print out the settings
    void	                 dump(UT_JSONWriter &w) const;

    bool        isDefault() const { return myIsDefault; }
    void        setIsDefault() { myIsDefault = true; }

protected:
    /// If you have a sub-class of XUSD_RenderVar, you can create it here
    virtual UT_UniquePtr<XUSD_RenderVar>	newRenderVar() const
    {
	return UTmakeUnique<XUSD_RenderVar>();
    }

    // Member data
    HdAovSettingsMap	        mySettings;
    SdfPath                     myCameraPath;
    UT_StringHolder		myFilename;
    UT_StringHolder		myPartname;
    RenderVarList		myVars;

    // Override values
    SettingOverride<GfVec2d>    myShutter;
    SettingOverride<GfVec2i>    myRes;
    SettingOverride<float>      myPixelAspect;
    SettingOverride<GfVec4f>    myDataWindowF;
    SettingOverride<bool>       myInstantShutter;
    bool                        myIsDefault;

};

/// XUSD_RenderSettings contains the HdRenderSettings for the render
class HUSD_API XUSD_RenderSettings
    : public UT_NonCopyable
{
public:
    using ProductList = UT_Array<UT_UniquePtr<XUSD_RenderProduct>>;
    using ProductGroup = UT_Array<int>;
    using ProductGroupList = UT_Array<ProductGroup>;
    using const_iterator = ProductList::const_iterator;

    XUSD_RenderSettings(const UT_StringHolder &prim_path,
            const UT_StringHolder &filename,
            time_t file_timestamp);
    virtual ~XUSD_RenderSettings();

    static void	findCameras(UT_Array<SdfPath> &list, UsdPrim prim);

    /// Since the settings primitive may specify values used by the render
    /// settings context (like frame count, etc.) we pass in a
    /// non-const @c context so the initialization process so we can call
    /// initFromUSD() once we've found the render settings.
    bool	init(const UsdStageRefPtr &usd,
			const SdfPath &settings_path,
			XUSD_RenderSettingsContext &ctx);
    /// Alternative initialization with a string path
    bool	init(const UsdStageRefPtr &usd,
			const UT_StringHolder &settings_path,
			XUSD_RenderSettingsContext &ctx);

    /// Update the frame
    bool	updateFrame(const UsdStageRefPtr &usd,
			XUSD_RenderSettingsContext &ctx);

    /// Resolve products/vars
    bool	resolveProducts(const UsdStageRefPtr &usd,
			const XUSD_RenderSettingsContext &ctx);

    /// Get the render settings
    UsdPrim	prim() const { return myUsdSettings.GetPrim(); }

    /// Rendering head
    const TfToken	        &renderer() const { return myRenderer; }

    /// Properties from the render settings which cannot be overridden per
    /// product.
    const VtArray<TfToken>      &purpose() const { return myPurpose; }

    /// Properties which a render product might override
    SdfPath             cameraPath(const XUSD_RenderProduct *p) const;
    double              shutterOpen(const XUSD_RenderProduct *p) const;
    double              shutterClose(const XUSD_RenderProduct *p) const;
    int                 xres(const XUSD_RenderProduct *p) const;
    int                 yres(const XUSD_RenderProduct *p) const;
    GfVec2i             res(const XUSD_RenderProduct *p) const;
    float               pixelAspect(const XUSD_RenderProduct *p) const;
    GfVec4f             dataWindowF(const XUSD_RenderProduct *p) const;
    UT_DimRect          dataWindow(const XUSD_RenderProduct *p) const;
    bool                instantaneousShutter(const XUSD_RenderProduct *p) const;
    UT_StringHolder     outputName(int product_group) const;

    const HdRenderSettingsMap	&renderSettings() const { return mySettings; }

    /// @{
    /// Render Products
    const ProductGroupList      &productGroups() const { return myProductGroups; }
    const ProductList	        &products() const { return myProducts; }
    const_iterator	         begin() const { return myProducts.begin(); }
    const_iterator	         end() const { return myProducts.end(); }
    /// @}

    /// Expand product name variables
    bool	expandProducts(const XUSD_RenderSettingsContext &ctx,
			int frame,
                        int product_group);

    /// Print out the settings to UT_ErrorLog
    void	printSettings() const;
    void        dump() const;           // Dump for a debug build
    void	dump(UT_JSONWriter &w) const;

    bool	collectAovs(TfTokenVector &aovs,
			HdAovDescriptorList &descs) const;

    enum class HUSD_AspectConformPolicy
    {
	INVALID = -1,
	EXPAND_APERTURE,
	CROP_APERTURE,
	ADJUST_HAPERTURE,
	ADJUST_VAPERTURE,
	ADJUST_PIXEL_ASPECT,
	DEFAULT = EXPAND_APERTURE
    };
    static HUSD_AspectConformPolicy	conformPolicy(const TfToken &t);
    static TfToken	conformPolicy(HUSD_AspectConformPolicy policy);

    /// When the camera aspect ratio doesn't match the image aspect ratio, USD
    /// specifies five different approatches to resolving this difference.
    /// HoudiniGL and Karma only use the vertical aperture and thus have a
    /// fixed way to resolve aspect ratio differences.  This method will adjust
    /// the vertical aspect or pixel aspect ratio to fit with the five
    /// different methods described in USD.  The method returns true if values
    /// were changed.  The method is templated on single/double precision
    template <typename T>
    static bool	aspectConform(HUSD_AspectConformPolicy conform,
		    T &vaperture, T &pixel_aspect,
		    T cam_aspect, T img_aspect);

    /// This method assumes you have render settings defined
    template <typename T>
    bool	aspectConform(const XUSD_RenderSettingsContext &ctx,
		    T &vaperture, T &pixel_aspect,
		    T cam_aspect, T img_aspect) const;

    HUSD_AspectConformPolicy	conformPolicy(
				    const XUSD_RenderSettingsContext &c) const;

    // Return a VtValue for all non-raster render products for the delegate
    // render product interface.
    VtValue             delegateRenderProducts(int product_group) const;

    virtual bool        supportedDelegate(const TfToken &name) const;
protected:
    virtual UT_UniquePtr<XUSD_RenderProduct>	newRenderProduct() const
    {
	return UTmakeUnique<XUSD_RenderProduct>();
    }
    void        partitionProducts();
    void	computeImageWindows(const UsdStageRefPtr &usd,
			const XUSD_RenderSettingsContext &ctx);
    void	setDefaults(const UsdStageRefPtr &usd,
			const XUSD_RenderSettingsContext &ctx);
    bool	loadFromPrim(const UsdStageRefPtr &usd,
			const XUSD_RenderSettingsContext &ctx);
    bool	loadFromOptions(const UsdStageRefPtr &usd,
			const XUSD_RenderSettingsContext &ctx);
    void	buildRenderSettings(const UsdStageRefPtr &usd,
			const XUSD_RenderSettingsContext &ctx);
    bool        isDefaultProduct() const
    {
        return myProducts.size() == 1 && myProducts[0]->isDefault();
    }

    UsdRenderSettings		myUsdSettings;
    SdfPath			myCameraPath;
    HdRenderSettingsMap		mySettings;
    TfToken			myRenderer;
    ProductList			myProducts;
    ProductGroupList		myProductGroups;
    GfVec2d			myShutter;
    GfVec2i			myRes;
    float			myPixelAspect;
    GfVec4f			myDataWindowF;
    UT_DimRect			myDataWindow;
    VtArray<TfToken>    	myPurpose;
    exint                       myProductGroup;
    bool			myInstantShutter;
    bool                        myFirstFrame;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
