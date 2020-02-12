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
#include <UT/UT_UniquePtr.h>
#include <UT/UT_Rect.h>
#include <UT/UT_NonCopyable.h>
#include <UT/UT_StringArray.h>
#include <pxr/pxr.h>
#include <pxr/imaging/hd/aov.h>
#include <pxr/imaging/hd/renderDelegate.h>
#include <pxr/usd/usdRender/var.h>
#include <pxr/usd/usdRender/product.h>
#include <pxr/usd/usdRender/settings.h>

class UT_JSONWriter;

PXR_NAMESPACE_OPEN_SCOPE

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

    /// Start frame for a render sequence
    virtual UsdTimeCode	evalTime() const = 0;

    /// Get a default rendering descriptor for a given AOV
    virtual HdAovDescriptor defaultAovDescriptor(const TfToken &aov) const
    {
	return HdAovDescriptor();
    }

    /// Default product name
    virtual const char	*defaultProductName() const { return nullptr; }

    /// Return a product name override
    virtual const char	*overrideProductName() const { return nullptr; }

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

    const TfToken	        &productType() const;
    const TfToken	        &productName() const;

    // Current output filename (with all variables expanded)
    const UT_StringHolder	&outputName() const { return myFilename; }

    const RenderVarList	        &vars() const { return myVars; }

    const_iterator	         begin() const { return myVars.begin(); }
    const_iterator	         end() const { return myVars.end(); }

    /// Expand product name variables.  Returns false if there are multiple
    /// frames, but no frame expansion.
    bool	                 expandProduct(
                                        const XUSD_RenderSettingsContext &opts,
                                        int frame);
    bool	                 collectAovs(TfTokenVector &aovs,
                                        HdAovDescriptorList &descs) const;

    /// User settings for this product
    const HdAovSettingsMap	&settings() const { return mySettings; }

    /// Print out the settings
    void	                 dump(UT_JSONWriter &w) const;

protected:
    /// If you have a sub-class of XUSD_RenderVar, you can create it here
    virtual UT_UniquePtr<XUSD_RenderVar>	newRenderVar() const
    {
	return UTmakeUnique<XUSD_RenderVar>();
    }

    // Member data
    HdAovSettingsMap	        mySettings;
    UT_StringHolder		myFilename;
    UT_StringHolder		myPartname;
    RenderVarList		myVars;
};

/// XUSD_RenderSettings contains the HdRenderSettings for the render
class HUSD_API XUSD_RenderSettings
    : public UT_NonCopyable
{
public:
    using ProductList = UT_Array<UT_UniquePtr<XUSD_RenderProduct>>;
    using const_iterator = ProductList::const_iterator;

    XUSD_RenderSettings();
    virtual ~XUSD_RenderSettings();

    static void	findCameras(UT_Array<SdfPath> &list, UsdPrim prim);

    /// Since the settings primitive may specify values used by the render
    /// settings context (like frame count, etc.) we pass in a
    /// non-const @c context so the initialization process so we can call
    /// initFromUSD() once we've found the render settings.
    bool	init(const UsdStageRefPtr &usd,
			const SdfPath &settings_path,
			XUSD_RenderSettingsContext &ctx);

    /// Resolve products/vars
    bool	resolveProducts(const UsdStageRefPtr &usd,
			const XUSD_RenderSettingsContext &ctx);

    /// Get the render settings
    UsdPrim	prim() const { return myUsdSettings.GetPrim(); }

    /// Rendering head
    const TfToken	        &renderer() const { return myRenderer; }

    /// Properties from the render settings
    const SdfPath	        &cameraPath() const { return myCameraPath; }
    double			 shutterOpen() const { return myShutter[0]; }
    double			 shutterClose() const { return myShutter[1]; }
    int				 xres() const { return myRes[0]; }
    int				 yres() const { return myRes[1]; }
    const GfVec2i	        &res() const { return myRes; }
    float			 pixelAspect() const { return myPixelAspect; }
    const GfVec4f	        &dataWindowF() const { return myDataWindowF; }
    const VtArray<TfToken>      &purpose() const { return myPurpose; }

    const UT_DimRect	        &dataWindow() const { return myDataWindow; }

    UT_StringHolder	         outputName() const;

    const HdRenderSettingsMap	&renderSettings() const { return mySettings; }

    /// @{
    /// Render Products
    const ProductList	        &products() const { return myProducts; }
    const_iterator	         begin() const { return myProducts.begin(); }
    const_iterator	         end() const { return myProducts.end(); }
    /// @}

    /// Expand product name variables
    bool	expandProducts(const XUSD_RenderSettingsContext &ctx,
			int frame);

    /// Print out the settings to UT_ErrorLog
    void	printSettings() const;
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

protected:
    virtual UT_UniquePtr<XUSD_RenderProduct>	newRenderProduct() const
    {
	return UTmakeUnique<XUSD_RenderProduct>();
    }
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

    UsdRenderSettings		myUsdSettings;
    SdfPath			myCameraPath;
    HdRenderSettingsMap		mySettings;
    TfToken			myRenderer;
    ProductList			myProducts;
    double			myShutter[2];
    GfVec2i			myRes;
    float			myPixelAspect;
    GfVec4f			myDataWindowF;
    UT_DimRect			myDataWindow;
    VtArray<TfToken>    	myPurpose;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
