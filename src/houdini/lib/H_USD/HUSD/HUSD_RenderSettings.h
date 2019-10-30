/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
 *
 * NAME:	HUSD_RenderSettings.h (karma Library, C++)
 *
 * COMMENTS:
 */

#ifndef __HUSD_RenderSettings__
#define __HUSD_RenderSettings__

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

class HUSD_RenderSettings;
class UT_JSONWriter;

class HUSD_API HUSD_RenderSettingsContext
    : public UT_NonCopyable
{
public:
    HUSD_RenderSettingsContext() {}
    virtual ~HUSD_RenderSettingsContext();

    /// Update any settings from the render settings primitive.  This allows
    /// the context to look at custom attributes on the RenderSettings.
    ///
    /// This function will always be called - even if there are no settings.
    virtual void	initFromUSD(PXR_NS::UsdRenderSettings &settings) { }

    /// Return the name of the render delegate
    virtual PXR_NS::TfToken	renderer() const = 0;

    /// Override the path to the camera
    virtual PXR_NS::SdfPath	overrideCamera() const
    {
	return PXR_NS::SdfPath();
    }

    /// Return the default resolution for rendering products
    virtual PXR_NS::GfVec2i	defaultResolution() const = 0;

    /// Optionally override the resolution of the product
    virtual PXR_NS::GfVec2i	overrideResolution(
				     const PXR_NS::GfVec2i &res) const
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

    /// Get a default rendering descriptor for a given AOV
    virtual PXR_NS::HdAovDescriptor	defaultAovDescriptor(
						const PXR_NS::TfToken &aov
					) const
    {
	return PXR_NS::HdAovDescriptor();
    }

    /// Default product name
    virtual const char	*defaultProductName() const { return nullptr; }

    /// Return a product name override
    virtual const char	*overrideProductName() const { return nullptr; }

    /// Build initial render settings map
    virtual void	setDefaultSettings(const HUSD_RenderSettings &rset,
				PXR_NS::HdRenderSettingsMap &settings) const
    {
    }

    /// After the products have been loaded, apply any overrides
    virtual void	overrideSettings(const HUSD_RenderSettings &rset,
				PXR_NS::HdRenderSettingsMap &settings) const
    {
    }
};

class HUSD_API HUSD_RenderVar
    : public UT_NonCopyable
{
public:
    HUSD_RenderVar();
    virtual ~HUSD_RenderVar();

    bool	loadFrom(const PXR_NS::UsdRenderVar &prim);
    bool	resolveFrom(const HUSD_RenderSettingsContext &ctx,
			const PXR_NS::UsdRenderVar &prim);
    bool	buildDefault(const HUSD_RenderSettingsContext &ctx);

    const std::string		&aovName() const { return myAovName; }
    const PXR_NS::TfToken	&aovToken() const { return myAovToken; }
    const PXR_NS::TfToken	&dataType() const;
    const std::string		&sourceName() const;
    const PXR_NS::TfToken	&sourceType() const;

    const PXR_NS::HdAovDescriptor	&desc() const { return myHdDesc; }
    PXL_DataFormat	 pxlFormat() const { return myDataFormat; }
    PXL_Packing		 pxlPacking() const { return myPacking; }

    /// Print out the settings
    void	dump(UT_JSONWriter &w) const;

protected:
    PXR_NS::HdAovDescriptor	myHdDesc;
    std::string			myAovName;
    PXR_NS::TfToken		myAovToken;
    PXL_DataFormat		myDataFormat;
    PXL_Packing			myPacking;
};

class HUSD_API HUSD_RenderProduct
    : public UT_NonCopyable
{
public:
    using RenderVarList = UT_Array<UT_UniquePtr<HUSD_RenderVar>>;
    using const_iterator = RenderVarList::const_iterator;

    HUSD_RenderProduct();
    virtual ~HUSD_RenderProduct();

    bool	 loadFrom(const PXR_NS::UsdStageRefPtr &usd,
			const PXR_NS::UsdRenderProduct &prim);
    bool	 resolveFrom(const PXR_NS::UsdStageRefPtr &usd,
			const PXR_NS::UsdRenderProduct &prim,
			const HUSD_RenderSettingsContext &ctx);
    bool	 buildDefault(const HUSD_RenderSettingsContext &ctx);

    const PXR_NS::TfToken	&productType() const;
    const PXR_NS::TfToken	&productName() const;

    // Current output filename (with all variables expanded)
    const UT_StringHolder	&outputName() const { return myFilename; }

    const RenderVarList	&vars() const { return myVars; }

    const_iterator	begin() const { return myVars.begin(); }
    const_iterator	end() const { return myVars.end(); }

    /// Expand product name variables.  Returns false if there are multiple
    /// frames, but no frame expansion.
    bool	expandProduct(const HUSD_RenderSettingsContext &opts, int frame);

    bool	collectAovs(PXR_NS::TfTokenVector &aovs,
			PXR_NS::HdAovDescriptorList &descs) const;

    /// User settings for this product
    const PXR_NS::HdAovSettingsMap	&settings() const { return mySettings; }

    /// Print out the settings
    void	dump(UT_JSONWriter &w) const;

protected:
    /// If you have a sub-class of HUSD_RenderVar, you can create it here
    virtual UT_UniquePtr<HUSD_RenderVar>	newRenderVar() const
    {
	return UTmakeUnique<HUSD_RenderVar>();
    }

    // Member data
    PXR_NS::HdAovSettingsMap	mySettings;
    UT_StringHolder		myFilename;
    UT_StringHolder		myPartname;
    RenderVarList		myVars;
};

/// HUSD_RenderSettings contains the HdRenderSettings for the render
class HUSD_API HUSD_RenderSettings
    : public UT_NonCopyable
{
public:
    using ProductList = UT_Array<UT_UniquePtr<HUSD_RenderProduct>>;
    using const_iterator = ProductList::const_iterator;

    HUSD_RenderSettings();
    virtual ~HUSD_RenderSettings();

    static void	findCameras(UT_Array<PXR_NS::SdfPath> &list,
			PXR_NS::UsdPrim prim);

    /// Since the settings primitive may specify values used by the render
    /// settings context (like frame count, etc.) we pass in a
    /// non-const @c context so the initialization process so we can call
    /// initFromUSD() once we've found the render settings.
    bool	init(const PXR_NS::UsdStageRefPtr &usd,
			const PXR_NS::SdfPath &settings_path,
			HUSD_RenderSettingsContext &ctx);

    /// Resolve products/vars
    bool	resolveProducts(const PXR_NS::UsdStageRefPtr &usd,
			const HUSD_RenderSettingsContext &ctx);

    /// Get the render settings
    PXR_NS::UsdPrim	prim() const { return myUsdSettings.GetPrim(); }

    /// Rendering head
    const PXR_NS::TfToken	&renderer() const { return myRenderer; }

    /// Properties from the render settings
    const PXR_NS::SdfPath	&cameraPath() const { return myCameraPath; }
    double			 shutterOpen() const { return myShutter[0]; }
    double			 shutterClose() const { return myShutter[1]; }
    int				 xres() const { return myRes[0]; }
    int				 yres() const { return myRes[1]; }
    const PXR_NS::GfVec2i	&res() const { return myRes; }
    float			 pixelAspect() const { return myPixelAspect; }
    const PXR_NS::GfVec4f	&dataWindowF() const { return myDataWindowF; }
    const PXR_NS::VtArray<PXR_NS::TfToken> &purpose() const { return myPurpose; }

    const UT_DimRect	&dataWindow() const { return myDataWindow; }

    UT_StringHolder	outputName() const;

    const PXR_NS::HdRenderSettingsMap	&renderSettings() const
    {
	return mySettings;
    }

    /// @{
    /// Render Products
    const ProductList	&products() const { return myProducts; }
    const_iterator	 begin() const { return myProducts.begin(); }
    const_iterator	 end() const { return myProducts.end(); }
    /// @}

    /// Expand product name variables
    bool	expandProducts(const HUSD_RenderSettingsContext &ctx,
			int frame);

    /// Print out the settings to UT_ErrorLog
    void	printSettings() const;
    void	dump(UT_JSONWriter &w) const;

    bool	collectAovs(PXR_NS::TfTokenVector &aovs,
			PXR_NS::HdAovDescriptorList &descs) const;

protected:
    virtual UT_UniquePtr<HUSD_RenderProduct>	newRenderProduct() const
    {
	return UTmakeUnique<HUSD_RenderProduct>();
    }
    void	computeImageWindows(const PXR_NS::UsdStageRefPtr &usd);
    void	setDefaults(const PXR_NS::UsdStageRefPtr &usd,
			const HUSD_RenderSettingsContext &ctx);
    bool	loadFromPrim(const PXR_NS::UsdStageRefPtr &usd,
			const HUSD_RenderSettingsContext &ctx);
    bool	loadFromOptions(const PXR_NS::UsdStageRefPtr &usd,
			const HUSD_RenderSettingsContext &ctx);
    void	buildRenderSettings(const PXR_NS::UsdStageRefPtr &usd,
			const HUSD_RenderSettingsContext &ctx);

    PXR_NS::UsdRenderSettings		myUsdSettings;
    PXR_NS::SdfPath			myCameraPath;
    PXR_NS::HdRenderSettingsMap		mySettings;
    PXR_NS::TfToken			myRenderer;
    ProductList				myProducts;
    double				myShutter[2];
    PXR_NS::GfVec2i			myRes;
    float				myPixelAspect;
    PXR_NS::GfVec4f			myDataWindowF;
    UT_DimRect				myDataWindow;
    PXR_NS::VtArray<PXR_NS::TfToken>	myPurpose;
};

#endif
