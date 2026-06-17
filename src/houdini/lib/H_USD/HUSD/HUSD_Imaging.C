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
 * Produced by:
 *	Side Effects Software Inc.
 *	123 Front Street West, Suite 1401
 *	Toronto, Ontario
 *      Canada   M5J 2M2
 *	416-504-9876
 *
 */

#include "HUSD_Imaging.h"
#include "HUSD_Compositor.h"
#include "HUSD_Constants.h"
#include "HUSD_ErrorScope.h"
#include "HUSD_FindPrims.h"
#include "HUSD_HydraGeoPrim.h"
#include "HUSD_HydraMaterial.h"
#include "HUSD_Info.h"
#include "HUSD_LightingMode.h"
#include "HUSD_Overrides.h"
#include "HUSD_Preferences.h"
#include "HUSD_Scene.h"
#include "HUSD_TimeCode.h"

#include "XUSD_Data.h"
#include "XUSD_Format.h"
#include "XUSD_ImagingEngine.h"
#include "XUSD_HydraUtils.h"
#include "XUSD_PathSet.h"
#include "XUSD_RenderSettings.h"
#include "XUSD_Tokens.h"
#include "XUSD_Utils.h"

#include <gusd/UT_Gf.h>
#include <OP/OP_Director.h>
#include <COP/COP_ApexProgram.h>
#include <COP/COP_CableStructure.h>
#include <COP/COP_Signature.h>
#include <COP/COP_SlapCompInputFetcher.h>
#include <COP/COP_SlapCompRegistry.h>
#include <DEP/DEP_MicroNode.h>
#include <DEP/DEP_TimedMicroNode.h>
#include <GVEX/GVEX_GeoCache.h>
#include <IMX/IMX_Layer.h>
#include <PXL/PXL_OCIO.h>
#include <PXL/PXL_Fill.h>
#include <PXL/PXL_Raster.h>
#include <TIL/TIL_CopResolver.h>
#include <TIL/TIL_Raster.h>
#include <TIL/TIL_TextureMap.h>
#include <HOM/HOM_Module.h>
#include <UT/UT_Array.h>
#include <UT/UT_Debug.h>
#include <UT/UT_Defines.h>
#include <UT/UT_ErrorLog.h>
#include <UT/UT_EnvControl.h>
#include <UT/UT_ErrorManager.h>
#include <UT/UT_Exit.h>
#include <UT/UT_Signal.h>
#include <UT/UT_ParallelUtil.h>
#include <UT/UT_JSONValue.h>
#include <UT/UT_PerfMonAutoEvent.h>
#include <UT/UT_StopWatch.h>
#include <UT/UT_SysClone.h>
#include <UT/UT_TaskGroup.h>
#include <UT/UT_Tracing.h>
#include <tools/henv.h>

#include <pxr/base/gf/bbox3d.h>
#include <pxr/base/gf/range3d.h>
#include <pxr/base/gf/rect2i.h>
#include <pxr/base/gf/size2.h>
#include <pxr/base/gf/vec2i.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/vt/array.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usdGeom/bboxCache.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/imaging/cameraUtil/conformWindow.h>
#include <pxr/imaging/hd/engine.h>
#include <pxr/imaging/hd/renderBuffer.h>
#include <pxr/imaging/hd/renderDelegate.h>
#include <pxr/imaging/hd/rendererPluginRegistry.h>
#include <pxr/imaging/hd/rendererPlugin.h>
#include <pxr/usdImaging/usdImaging/delegate.h>
#include <pxr/imaging/hd/rprim.h>

#include <algorithm>
#include <iostream>
#include <initializer_list>
#include <thread>

using namespace UT::Literal;

PXR_NAMESPACE_USING_DIRECTIVE

namespace
{
    // Count of the number of render engines that use the texture cache. The
    // cache can only be cleared if there are no active renders.
    SYS_AtomicInt<int>           theTextureCacheRenders(0);
    // Track active HUSD_Imaging objects so we can clean up any running
    // renderers during Houdini shutdown.
    UT_Set<HUSD_Imaging *>	 theActiveRenders;
    UT_Lock			 theActiveRenderLock;
    HUSD_RendererInfoMap	 theRendererInfoMap;

    static UT_StringSet         theDefaultColorAOVs({
                                    UTmakeUnsafeRef(HdAovTokens->color.GetText())
                                });
    static UT_StringSet         theDefaultNonColorAOVs({
                                    UTmakeUnsafeRef(HdAovTokens->depth.GetText()),
                                    UTmakeUnsafeRef(HdAovTokens->normal.GetText()),
                                    UTmakeUnsafeRef(HdAovTokens->primId.GetText()),
                                    UTmakeUnsafeRef(HdAovTokens->instanceId.GetText()),
                                });

    bool
    renderUsesTextureCache(const UT_StringRef &name)
    {
        return name == HUSD_Constants::getKarmaRendererPluginName();
    }

    bool
    renderUsesCopResolverCache(const UT_StringRef &name)
    {
        return name != HUSD_Constants::getHoudiniRendererPluginName();
    }

    PXL_DataFormat
    HdToPXL(HdFormat df)
    {
        switch(HdGetComponentFormat(df))
        {
        case HdFormatUNorm8:
        case HdFormatSNorm8:    // We don't have a format for this.
            return PXL_INT8;
        case HdFormatUInt16:
        case HdFormatInt16:     // We don't have a format for this.
            return PXL_INT16;
        case HdFormatFloat16:
            return PXL_FLOAT16;
        case HdFormatFloat32:
            return PXL_FLOAT32;
        case HdFormatInt32:
            return PXL_INT32;
        default:
            break;
        }
        // bad format?
        return PXL_INT8;
    }

    void
    backgroundRenderExitCB(void *data)
    {
        UT_Lock::Scope               lock(theActiveRenderLock);
        for (auto &&item : theActiveRenders)
            item->terminateRender(true);
    }

    void
    backgroundRenderState(bool converged, HUSD_Imaging *ptr)
    {
        // We don't want to run our cleanup code if we are here because we
        // are running the exit callbacks. No need to keep static data
        // structures up to date, or de-register exit callbacks. Both of
        // these operations would trigger crashes during shutdown.
        if (!UT_Exit::isExiting())
        {
            UT_Lock::Scope lock(theActiveRenderLock);
            if (converged)
            {
                theActiveRenders.erase(ptr);
                if (theActiveRenders.size() == 0)
                    UT_Exit::removeExitCallback(backgroundRenderExitCB);
            }
            else
            {
                UT_ASSERT(theActiveRenders.count(ptr) == 0);
                if (theActiveRenders.size() == 0)
                    UT_Exit::addExitCallback(backgroundRenderExitCB, nullptr);
                theActiveRenders.insert(ptr);
            }
        }
    }

    static TfToken
    getHuskFormat(const HdAovDescriptor &aov)
    {
        // We only really care about the husk format (whether it's a color or not)
        auto it = aov.aovSettings.find(HusdHuskTokens->driver_parameters_aov_husk_format);
        if (it != aov.aovSettings.end())
        {
            if (it->second.IsHolding<TfToken>())
                return it->second.UncheckedGet<TfToken>();
        }
        return TfToken();
    }


    static bool
    isEqual(const HdAovDescriptorList &a, const HdAovDescriptorList &b)
    {
        if (a.size() != b.size())
            return false;
        for (int i = 0, n = a.size(); i < n; ++i)
        {
            // Good enough for now (there's the aovSettings and other fields,
            // but we really only care about format).
            if (a[i].format != b[i].format)
                return false;
            if (getHuskFormat(a[i]) != getHuskFormat(b[i]))
                return false;
        }
        return true;
    }

    void
    getHoudiniPluginAOVs(TfTokenVector &aov_names, HdAovDescriptorList &descs)
    {
        static TfTokenVector            theAOVs;
        static HdAovDescriptorList      theDescs;
        static std::once_flag           theInitialize;
        static TfToken                  theColor4h("color4h");
        static TfToken                  theFloat("float");
        std::call_once(theInitialize, [&]() {
                theAOVs.push_back(HdAovTokens->color);
                theAOVs.push_back(HdAovTokens->depth);
                theDescs.push_back(HdAovDescriptor(HdFormatFloat16Vec4,
                                                    true,
                                                    VtValue(GfVec4f(0,0,0,0))));
                theDescs.push_back(HdAovDescriptor(HdFormatFloat32,
                                                    false,
                                                    VtValue(0.0f)));
                theDescs[0].aovSettings[HusdHuskTokens->driver_parameters_aov_husk_format]
                        = VtValue(theColor4h);
                theDescs[1].aovSettings[HusdHuskTokens->driver_parameters_aov_husk_format]
                        = VtValue(theFloat);
        });
        aov_names = theAOVs;
        descs = theDescs;
    }
}       // End namespace

class husd_DefaultRenderSettingContext : public XUSD_RenderSettingsContext
{
public:
    fpreal      startFrame() const override
        { return myFrame; }
    fpreal      fps() const override
        { return myFPS; }
    UsdTimeCode evalTime() const override
        { return UsdTimeCode(myFrame); }
    GfVec2i	defaultResolution() const override
        { return GfVec2i(myW,myH); }
    SdfPath	overrideCamera() const override
        { return myCameraPath; }

    HdAovDescriptor
    defaultAovDescriptor(const TfToken &aov) const override
        { return HdAovDescriptor(); }

    bool getAovDescriptor(TfToken &aov, HdAovDescriptor &desc) const
        {
            auto entry = myAOVs.find(aov.GetText());
            if(entry != myAOVs.end())
            {
                desc = entry->second;
                return true;
            }
            if(aov == HdAovTokens->depth)
            {
                VtValue zero((float)0);
                desc = HdAovDescriptor(HdFormatFloat32, false, zero);
                return true;
            }
            if(aov == HdAovTokens->primId || aov == HdAovTokens->instanceId)
            {
                VtValue zero((int)0);
                desc = HdAovDescriptor(HdFormatInt32, false, zero);
                return true;
            }
            return false;
        }

    bool hasAOV(const UT_StringRef &name) const
        { return (myAOVs.find(name) !=  myAOVs.end()); }

    void setFrame(fpreal frame)
        { myFrame = frame; }
    void setFPS(fpreal fps)
        { myFPS = fps; }
    void setRes(int w, int h)
        { myW = w; myH = h; }
    void setResScale(fpreal scale)
        { myResScale = scale; }
    void setRenderRegion(const GfVec4f &render_region)
        { myRenderRegion = render_region; }

    void setRenderSettingsDataWindowActive(bool active)
        { myRenderSettingsDataWindowActive = active; }

    void setAOVs(const TfTokenVector &aov_names,
                 const HdAovDescriptorList &aov_desc)
        {
            if (aov_names != myCacheAOVNames || !isEqual(myCacheAOVDesc, aov_desc))
            {
                myCacheAOVNames = aov_names;
                myCacheAOVDesc = aov_desc;
                myAOVs.clear();
                for(int i=0; i<aov_names.size(); i++)
                    myAOVs[ aov_names[i].GetText() ] = aov_desc[i];

                XUSD_RenderSettings::splitAOVs(myAOVs, myColorAOVs, myNonColorAOVs);
            }
        }

    bool                 hasAOVs() const
                            { return myColorAOVs.size() || myNonColorAOVs.size(); }
    const UT_StringSet  &colorAOVs() const { return myColorAOVs; }
    const UT_StringSet  &nonColorAOVs() const { return myNonColorAOVs; }

    GfVec2i overrideResolution(const GfVec2i &res) const override
        {
            if (myResScale > 0.0)
                return GfVec2i(res[0] * myResScale, res[1] * myResScale);
            return (myW > 0) ? GfVec2i(myW, myH) : res;
        }
    GfVec4f overrideDataWindow(const GfVec4f &w) const override
        {
            // When we've been provided with a valid render region, for example
            // via `setRenderSettings`, use it (and ignore the data window in
            // the RenderSettings prim)
            if (myRenderRegion[0] < myRenderRegion[2] &&
                myRenderRegion[1] < myRenderRegion[3])
                return myRenderRegion;
            // When doing clone rendering and in some other circumstances we
            // want to respect the data window from the render settings, so
            // just return the passed in window.
            if (myRenderSettingsDataWindowActive)
                return w;
            // If the data window has been deactivated for this context, just
            // return a full data window.
            return GfVec4f(0.0, 0.0, 1.0, 1.0);
        }

    bool    allowCameraless() const override
        { return true; }
    void    setCamera(const SdfPath &campath)
        { myCameraPath = campath; }

private:
    UT_StringMap<HdAovDescriptor>       myAOVs;
    UT_StringSet                        myColorAOVs;
    UT_StringSet                        myNonColorAOVs;
    TfTokenVector                       myCacheAOVNames;
    HdAovDescriptorList                 myCacheAOVDesc;

    SdfPath myCameraPath;
    fpreal myFrame = 1.0;
    fpreal myFPS = 24.0;
    fpreal myResScale = 0.0;
    int myW = 0;
    int myH = 0;
    GfVec4f myRenderRegion = { 0.0, 0.0, 0.0, 0.0 };
    bool myRenderSettingsDataWindowActive = false;
};

class husd_CopTextureMicroNode : public DEP_TimedMicroNode
{
public:
    husd_CopTextureMicroNode(HUSD_Imaging &owner)
        : myOwner(owner)
    { }

    void             becameDirty(DEP_MicroNode &src,
                            const DEP_PropagateData &propdata) override
    {
        // One of our COP texture is dirty.
        myOwner.handleCopTextureChange(false);
    }

private:
    HUSD_Imaging    &myOwner;
};

class husd_CopDefaultsMicroNode : public DEP_TimedMicroNode
{
public:
    husd_CopDefaultsMicroNode(HUSD_Imaging &owner)
        : myOwner(owner)
    { }

    void             becameDirty(DEP_MicroNode &src,
                            const DEP_PropagateData &propdata) override
    {
        // One of our COP defaults is dirty.  Need a full clear.
        myOwner.handleCopTextureChange(true);
    }

private:
    HUSD_Imaging    &myOwner;
};

class HUSD_Imaging::husd_ImagingPrivate
{
public:
    husd_ImagingPrivate(HUSD_Imaging &owner)
        : myCopTextureMicroNode(owner)
        , myCopDefaultsMicroNode(owner)
    { }

    void clearCopTextureDependencies(fpreal frame)
    {
        myCopTextureMicroNode.clearInputs();
        myCopTextureMicroNode.update(CHgetTimeFromFrame(frame));
        myCopDefaultsMicroNode.clearInputs();
        myCopDefaultsMicroNode.update(CHgetTimeFromFrame(frame));
        myCopTextureCachedNodeIds.clear();
    }

    UT_UniquePtr<XUSD_ImagingEngine>	 myImagingEngine;
    UT_TaskGroup			 myUpdateTask;
    XUSD_ImagingRenderParams		 myRenderParams;
    XUSD_ImagingRenderParams		 myLastRenderParams;
    std::map<TfToken, VtValue>           myCurrentRenderSettings;
    std::map<TfToken, VtValue>           myCurrentCameraSettings;
    std::string				 myRootLayerIdentifier;
    HdRenderSettingsMap                  myPrimRenderSettingMap;
    HdRenderSettingsMap                  myOldPrimRenderSettingMap;
    husd_CopTextureMicroNode             myCopTextureMicroNode;
    husd_CopDefaultsMicroNode            myCopDefaultsMicroNode;
    UT_Set<int>                          myCopTextureCachedNodeIds;
};

/// This is a basic HdRenderBuffer wrapper around an IMX_Layer. When Map() is
/// called on such an object, the image is copied to main memory (if needed) and
/// pointer to that data is returned.
class HUSD_Imaging::husd_IMXRenderBuffer : public HdRenderBuffer
{
public:
    husd_IMXRenderBuffer()
        : HdRenderBuffer(SdfPath())
    {
    }
    husd_IMXRenderBuffer(IMX_LayerPtr layer)
        : HdRenderBuffer(SdfPath()),
          myLayer(layer)
    {
    }
    ~husd_IMXRenderBuffer() override
    {
    }

    void setLayer(IMX_LayerPtr layer)
    {
        myLayer = layer;
    }

    bool Allocate(const GfVec3i& dimensions, HdFormat format, bool ms) override
    {
        return false;
    }

    HdFormat GetFormat() const override
    {
        if (myLayer)
        {
            CE_Image::StorageType storage = myLayer->getStorageType();
            int channels = myLayer->getChannels();
            switch (storage)
            {
            case CE_Image::StorageType::INT16:
                UT_ASSERT(channels == 1);
                return HdFormatInt16;
            case CE_Image::StorageType::INT32:
                UT_ASSERT(channels == 1);
                return HdFormatInt32;
            case CE_Image::StorageType::FLOAT16:
                if (channels == 1)
                    return HdFormatFloat16;
                else if (channels == 2)
                    return HdFormatFloat16Vec2;
                else if (channels == 3)
                    return HdFormatFloat16Vec3;
                else if (channels == 4)
                    return HdFormatFloat16Vec4;
                else
                {
                    UT_ASSERT(false);
                    break;
                }
            case CE_Image::StorageType::FLOAT32:
                if (channels == 1)
                    return HdFormatFloat32;
                else if (channels == 2)
                    return HdFormatFloat32Vec2;
                else if (channels == 3)
                    return HdFormatFloat32Vec3;
                else if (channels == 4)
                    return HdFormatFloat32Vec4;
                else
                {
                    UT_ASSERT(false);
                    break;
                }
            case CE_Image::StorageType::FIXED8:
                if (channels == 1)
                    return HdFormatUNorm8;
                else if (channels == 2)
                    return HdFormatUNorm8Vec2;
                else if (channels == 3)
                    return HdFormatUNorm8Vec3;
                else if (channels == 4)
                    return HdFormatUNorm8Vec4;
                else
                {
                    UT_ASSERT(false);
                    break;
                }
            default:
                // Sadly, only 8-bit integer support in HdFormat is for
                // normalized floating point data... Likewise, no fixed point
                // support backed by 16-bit integers...
                break;
            }
        }
        
        return HdFormatInvalid;
    }

    uint GetDepth() const override
    {
        if (myLayer)
            return 1;
        return 0;
    }
    uint GetWidth() const override
    {
        if (myLayer)
            return myLayer->isConstant() ? 1 : myLayer->bufferWidth();
        return 0;
    }
    uint GetHeight() const override
    {
        if (myLayer)
            return myLayer->isConstant() ? 1 : myLayer->bufferHeight();
        return 0;
    }

    bool IsMultiSampled() const override
    {
        return false;
    }

    void* Map() override
    {
        if (myLayer)
            return myLayer->getCPUBuffer(true, false);
        return nullptr;
    }
    void Unmap() override
    {
    }
    bool IsMapped() const override
    {
        return false;
    }

    void* MapExtra(int idx)
    {
        return nullptr;
    }
    void UnmapExtra(int idx)
    {
    }

    bool IsConverged() const override
    {
        return true;
    }

    void Resolve() override
    {
    }

    VtValue GetResource(bool ms) const override
    {
        return VtValue();
    }

private:
    void _Deallocate() override
    {
    }

private:
    IMX_LayerPtr                    myLayer;
};

/// This function returns the storage type in IMX that corresponds to the input
/// format.
static CE_Image::StorageType
husd_storageFromFormat(PXL_DataFormat format)
{
    // TODO: check the returned format--should they be fixed point or integer?
    switch (format)
    {
        case PXL_INT8:
            return CE_Image::StorageType::INT8;
        case PXL_INT16:
            return CE_Image::StorageType::INT16;
        case PXL_INT32:
            return CE_Image::StorageType::INT32;
        case PXL_FLOAT16:
            return CE_Image::StorageType::FLOAT16;

        case PXL_FLOAT32:
            return CE_Image::StorageType::FLOAT32;

        case PXL_MAX_DATA_FORMAT:
            break;
    }
    UT_ASSERT(0);
    return CE_Image::StorageType::INT8;
}

/// Returns the number of channels per pixel for the input format.
static int
husd_channelsFromPacking(PXL_Packing packing)
{
    switch (packing)
    {
    case PACK_SINGLE:
        return 1;
    case PACK_DUAL:
    case PACK_UV:
        return 2;
    case PACK_RGB:
        return 3;
    case PACK_RGBA:
        return 4;
    case PACK_DUAL_NI:
    case PACK_RGB_NI:
    case PACK_RGBA_NI:
    case PACK_UNKNOWN:
        break;
    }
    UT_ASSERT(false);
    return -1;
}

/// Converts type of the render buffer to a Copernicus type.
static COP_Type
husd_getBufferCOPType(const HUSD_RenderBuffer& buf)
{
    COP_Type coptype = COP_TYPE_UNDEF;
    if (buf.isValid())
    {
        switch (buf.dataFormat())
        {
        case PXL_INT8:
        case PXL_INT16:
        case PXL_INT32:
            coptype = COP_TYPE_INT;
            break;
        case PXL_FLOAT16:
        case PXL_FLOAT32:
            switch (buf.packing())
            {
                case PACK_SINGLE: coptype = COP_TYPE_FLOAT; break;
                case PACK_UV: coptype = COP_TYPE_VECTOR2; break;
                case PACK_RGB: coptype = COP_TYPE_VECTOR; break;
                case PACK_RGBA: coptype = COP_TYPE_VECTOR4; break;

                case PACK_DUAL:
                case PACK_DUAL_NI:
                case PACK_RGB_NI:
                case PACK_RGBA_NI:
                case PACK_UNKNOWN:
                    UT_ASSERT(0);
                    break;
            }
            break;

        case PXL_MAX_DATA_FORMAT:
            UT_ASSERT(0);
            break;
        }
    }

    return coptype;
}

/// Returns a shared pointer to an IMX layer with identical data to the input
/// render buffer.
IMX_LayerPtr
husd_convertBufferToLayer(HUSD_RenderBuffer &buf,
                          const COP_SlapCompViewInfo *view_info)
{
    if (!buf.isValid())
        return IMX_LayerPtr();

    const void *mapped_buffer = buf.map();
    if (!mapped_buffer)
        return IMX_LayerPtr();

    IMX_LayerPtr layer_ptr = UTmakeShared<IMX_Layer>();

    if (view_info)
        view_info->applyToLayer(*layer_ptr);

    layer_ptr->setBorder(IMX_BorderType::IMX_CLAMP);

    layer_ptr->setDataWindow(buf.xres(), buf.yres());
    layer_ptr->setStorageType(husd_storageFromFormat(buf.dataFormat()));
    layer_ptr->setChannels(husd_channelsFromPacking(buf.packing()));
    layer_ptr->setBufferToPixels();

    std::memcpy(layer_ptr->getCPUBuffer(false, true), mapped_buffer,
                layer_ptr->bufferWidth() * layer_ptr->bufferHeight() *
                layer_ptr->getChannels() * layer_ptr->getStorageBytes());
    buf.unmap();

    return layer_ptr;
}

class husd_ImagingSlapCompInputFetcher : public COP_SlapCompInputFetcher
{
public:
    husd_ImagingSlapCompInputFetcher(
            HUSD_Imaging &imager,
            const COP_SlapCompViewInfo *view_info)
        : myImager(imager),
          myViewInfo(view_info)
    {
    }

    COP_CableStructure getLayerStructure() const override
    {
        COP_CableStructure outs;
        for (auto && plane_name : myImager.rendererPlanes())
        {
            outs.addField(plane_name,
                husd_getBufferCOPType(myImager.getAOVBuffer(plane_name)));
        }
        return outs;
    }

protected:
    IMX_LayerConstPtr getLayerInternal(
            const UT_StringHolder& layer_name) override
    {
        HUSD_RenderBuffer buf = myImager.getAOVBufferIncludingExtra(layer_name);
        return husd_convertBufferToLayer(buf, myViewInfo);
    }

private:
    HUSD_Imaging                           &myImager;
    const COP_SlapCompViewInfo             *myViewInfo;
};

HUSD_Imaging::HUSD_Imaging()
    : myLastCompositedBufferSet(BUFFER_NONE)
    , myIsPaused(false)
    , myAllowUpdates(true)
    , myAllowStormRenderer(true)
    , myHandlingCopTextureChange(false)
    , mySlapCompProgramManager(nullptr)
{
    myPrivate = UTmakeUnique<husd_ImagingPrivate>(*this);
    myPrivate->myRenderParams.myShowProxy = true;
    myPrivate->myRenderParams.myShowGuides = true;
    myPrivate->myRenderParams.myShowRender = true;
    myPrivate->myRenderParams.myHighlight = true;
    myPrivate->myLastRenderParams = myPrivate->myRenderParams;

    myWantsHeadlight = false;
    myHasHeadlight = false;
    myWantsDomelight = false;
    myHasDomelight = false;
    myWantsThreePointlight = false;
    myHasThreePointlight = false;
    myWantsPhysicalSky = false;
    myHasPhysicalSky = false;
    myDoLighting = true;
    myDoMaterials = true;
    myConverged = true;
    myAOVsStashed = false;
    mySettingsChanged = true;
    myWorkLightsChanged = true;
    myValidRenderSettingsPrim = false;
    myCameraSynced = true;
    myShowHdGpProcedurals = true;
    myConformPolicy = CameraUtilFit;
    myFrame = -1e30;
    myScene = nullptr;
    myCompositor = nullptr;
    myOutputPlane = HdAovTokens->color.GetText();
    myRenderSettingsContext = UTmakeUnique<husd_DefaultRenderSettingContext>();
    myHeadlightIntensity = 114450 * 0.5;
    myKeyColor.assign(1.0f,1.0f,1.0f); 
    myKeyDir.assign(0.0,0.0); 
    myFillColor.assign(1.0f,1.0f,1.0f); 
    myFillDir.assign(0.0,0.0); 
    myBackColor.assign(1.0f,1.0f,1.0f); 
    myBackDir.assign(0.0,0.0); 
}

HUSD_Imaging::~HUSD_Imaging()
{
    UT_Lock::Scope	lock(theActiveRenderLock);
    theActiveRenders.erase(this);

    if (isUpdateRunning() && UT_Exit::isExiting())
    {
	// We're currently running an update.  If we delete our private data,
	// this will cause the delegate to be deleted, causing all sorts of
	// problems while we Sync().  So, in this case, since we're exiting, we
	// can just let the unique pointer float (and not be cleaned up here)
        myPrivate.release();
    }
    else
    {
        // Make sure to clear the imaging engine since we're doing reference
        // counting for clearing the texture cache.
        resetImagingEngine();
    }
}

void
HUSD_Imaging::resetImagingEngine()
{
    bool        clear_cache = false;

    myIsPaused = false;

    if (myPrivate->myImagingEngine && renderUsesTextureCache(myRendererName))
    {
        int     now = theTextureCacheRenders.add(-1);
        UT_ASSERT(now >= 0);
        clear_cache = (now == 0);
    }
    myPrivate->myImagingEngine.reset();
    myPrivate->clearCopTextureDependencies(myFrame);
    // After a restart, we need to re-create the fake domelight and headlight
    // if they are needed, because they are owned by the imaging engine.
    myHasHeadlight = false;
    myHasDomelight = false;
    myHasThreePointlight = false;
    myHasPhysicalSky = false;
    // With no imaging engine, always claim to be "converged" so we don't
    // continuously try to re-render when there is nothing to do the rendering.
    myConverged = true;
    if (clear_cache)
    {
        // Clear out of date textures from cache
        TIL_TextureCache::clearCache(1);
        // Equivalent to "geocache -n" but avoids locking on the global eval
        // lock as would be required to use CMD_Manager::execute.
        GVEX_GeoCache::clearCache(1);
    }
}

bool
HUSD_Imaging::isUpdateRunning() const
{
    RunningStatus status = RunningStatus(myRunningInBackground.relaxedLoad());

    return (status != RUNNING_UPDATE_NOT_STARTED);
}

bool
HUSD_Imaging::isUpdateComplete() const
{
    RunningStatus status = RunningStatus(myRunningInBackground.relaxedLoad());

    return (status != RUNNING_UPDATE_IN_BACKGROUND);
}

void
HUSD_Imaging::getRendererCommands(UT_StringArray &command_names,
        UT_StringArray &command_descriptions) const
{
    if (myPrivate && myPrivate->myImagingEngine)
        myPrivate->myImagingEngine->GetRendererCommands(
            command_names, command_descriptions);
}

void
HUSD_Imaging::invokeRendererCommand(const UT_StringHolder &command_name) const
{
    if (myPrivate && myPrivate->myImagingEngine)
    {
        myPrivate->myImagingEngine->InvokeRendererCommand(command_name);
    }
}

void
HUSD_Imaging::terminateRender(bool hard_halt)
{
    waitForUpdateToComplete();
    registerSlapCompAOVs(true, &myLastSlapCompViewInfo);
    mySettingsChanged = true;
    if(hard_halt)
    {
        resetImagingEngine();
    }
    else if(myPrivate && myPrivate->myImagingEngine)
    {
        UsdStageRefPtr stage = UsdStage::CreateInMemory();

        myPrivate->myImagingEngine->DispatchRender(
            stage->GetPseudoRoot(), myPrivate->myRenderParams);
    }
}

void
HUSD_Imaging::setDrawMode(DrawMode mode)
{
    XUSD_ImagingRenderParams::XUSD_ImagingDrawMode usdmode;

    switch(mode)
    {
    case DRAW_WIRE:
	usdmode = XUSD_ImagingRenderParams::DRAW_WIREFRAME;
	break;
    case DRAW_SHADED_NO_LIGHTING:
	usdmode = XUSD_ImagingRenderParams::DRAW_GEOM_ONLY;
	break;
    case DRAW_SHADED_FLAT:
	usdmode = XUSD_ImagingRenderParams::DRAW_SHADED_FLAT;
	break;
    case DRAW_SHADED_SMOOTH:
	usdmode = XUSD_ImagingRenderParams::DRAW_SHADED_SMOOTH;
	break;
    case DRAW_WIRE_SHADED_SMOOTH:
	usdmode = XUSD_ImagingRenderParams::DRAW_WIREFRAME_ON_SURFACE;
	break;
    default:
	UT_ASSERT(!"Unhandled draw mode");
	usdmode = XUSD_ImagingRenderParams::DRAW_SHADED_SMOOTH;
	break;
    }
    myPrivate->myRenderParams.myDrawMode = usdmode;
}

void
HUSD_Imaging::showPurposeRender(bool enable)
{
    myPrivate->myRenderParams.myShowRender = enable;
}

void
HUSD_Imaging::showPurposeProxy(bool enable)
{
    myPrivate->myRenderParams.myShowProxy = enable;
}

void
HUSD_Imaging::showPurposeGuide(bool enable)
{
    myPrivate->myRenderParams.myShowGuides = enable;
}

void
HUSD_Imaging::setDrawComplexity(float complexity)
{
    myPrivate->myRenderParams.myComplexity = complexity;
}

void
HUSD_Imaging::setBackfaceCull(bool bf)
{
    auto style = bf ? XUSD_ImagingRenderParams::CULL_STYLE_BACK
		    : XUSD_ImagingRenderParams::CULL_STYLE_NOTHING;
    myPrivate->myRenderParams.myCullStyle = style;
}

void
HUSD_Imaging::setScene(HUSD_Scene *scene)
{
    myScene = scene;
}

bool
HUSD_Imaging::isSlapCompEnabled() const
{
    return mySlapCompProgramManager &&
           mySlapCompProgramManager->isEnabled();
}

bool
HUSD_Imaging::isViewportSlapCompEnabled() const
{
    return mySlapCompProgramManager
            && mySlapCompProgramManager->hasViewportFilters()
            && mySlapCompProgramManager->isEnabled();
}


void
HUSD_Imaging::deactivate()
{
    // Nothing to do right now - but we know we're about to get switched out to
    // a different delegate.
}

void
HUSD_Imaging::setStage(const HUSD_DataHandle &data_handle,
        const HUSD_ConstOverridesPtr &overrides,
        const HUSD_ConstPostLayersPtr &postlayers)
{
    myDataHandle = data_handle;
    myOverrides = overrides;
    myPostLayers = postlayers;
}

bool
HUSD_Imaging::setFrame(fpreal frame)
{
    if (frame != myFrame)
    {
	myFrame = frame;
        myRenderSettingsContext->setFrame(myFrame);
	myPrivate->myRenderParams.myFrame = frame;
	mySettingsChanged = true;

        // If our COP texture micronode is time dependent, this will trigger
        // the karma texture cache to be flushed.
        fpreal t = CHgetTimeFromFrame(frame);
        if (myPrivate->myCopDefaultsMicroNode.requiresUpdate(t) ||
            myPrivate->myCopTextureMicroNode.requiresUpdate(t))
        {
            handleCopTextureChange(true);
        }

	return true;
    }

    return false;
}

void
HUSD_Imaging::setAspectPolicy(HUSD_AspectConformPolicy p)
{
    if(p == HUSD_AspectConformPolicy::EXPAND_APERTURE)
        myConformPolicy = CameraUtilFit;
    else if(p == HUSD_AspectConformPolicy::CROP_APERTURE)
        myConformPolicy = CameraUtilCrop;
    else if(p == HUSD_AspectConformPolicy::ADJUST_HAPERTURE)
        myConformPolicy = CameraUtilMatchHorizontally;
    else if(p == HUSD_AspectConformPolicy::ADJUST_VAPERTURE)
        myConformPolicy = CameraUtilMatchVertically;
    else if(p == HUSD_AspectConformPolicy::ADJUST_PIXEL_ASPECT)
        myConformPolicy = CameraUtilDontConform;
}

void
HUSD_Imaging::setAllowStormRenderer(bool allow_storm)
{
    myAllowStormRenderer = allow_storm;
}

bool
HUSD_Imaging::setLightingMode(HUSD_LightingMode mode)
{
    bool     changed = false;

    if(mode != myLightingMode)
    {
	mySettingsChanged = true;
	myWantsHeadlight = (mode == HUSD_LIGHTING_MODE_HEADLIGHT_ONLY);
        myWantsDomelight = (mode == HUSD_LIGHTING_MODE_DOMELIGHT_ONLY);
        myWantsThreePointlight = (mode == HUSD_LIGHTING_MODE_THREE_POINT_ONLY);
        myWantsPhysicalSky = (mode == HUSD_LIGHTING_MODE_PHYSICAL_SKY_ONLY);
	changed = true;

        myLightingMode = mode;
        myWorkLightsChanged = true;
    }

    return changed;
}


void
HUSD_Imaging::setHeadlightIntensity(fpreal intensity)
{
    const fpreal conversion = 9.34941e4;
    intensity *= conversion;
    if(myHeadlightIntensity != intensity)
    {
        myHeadlightIntensity = intensity;
        if(myWantsHeadlight)
        {
            myWorkLightsChanged = true;
            mySettingsChanged = true;
        }
    }
}

void
HUSD_Imaging::setThreePointLights(const UT_Vector3F &key_color,
                                  const UT_Vector2D &key_dir,
                                  const UT_Vector3F &rim_color,
                                  const UT_Vector2D &rim_dir,
                                  const UT_Vector3F &back_color,
                                  const UT_Vector2D &back_dir)
{
    if(myKeyColor != key_color ||
       myKeyDir != key_dir ||
       myFillColor != rim_color ||
       myFillDir != rim_dir ||
       myBackColor != back_color ||
       myBackDir != back_dir)
    {
        myKeyColor = key_color;
        myKeyDir = key_dir;
        myFillColor = rim_color;
        myFillDir = rim_dir;
        myBackColor = back_color;
        myBackDir = back_dir;
        if(myWantsThreePointlight)
        {
            myWorkLightsChanged = true;
            mySettingsChanged = true;
        }
    }
}

void
HUSD_Imaging::setDomeLight(const UT_StringHolder &map_name,
                           const UT_Vector3F &tint,
                           const UT_Vector3F &rotate)
{
    if(myDomeMapName != map_name ||
       myDomeTint != tint ||
       myDomeRotate != rotate)
    {
        myDomeMapName = map_name;
        myDomeTint = tint;
        myDomeRotate = rotate;
        if(myWantsDomelight)
        {
            myWorkLightsChanged = true;
            mySettingsChanged = true;
        }
    }
}

void
HUSD_Imaging::setPhysicalSky(const fpreal32 &sun_int,
                             const UT_Vector2D &sun_dir,
                             const fpreal32 &sky_int, 
                             const UT_StringHolder &sky_map)
{
    if(mySkyIntensity != sky_int ||
       mySunIntensity != sun_int ||
       mySkyDir != sun_dir ||
       mySkyMapName != sky_map)
    {
        mySkyIntensity = sky_int;
        mySunIntensity = sun_int;
        mySkyDir = sun_dir;
        mySkyMapName = sky_map;
        if(myWantsPhysicalSky)
        {
            myWorkLightsChanged = true;
            mySettingsChanged = true;
        }
    }
}


void
HUSD_Imaging::setLighting(bool do_lighting)
{
    if (myDoLighting != do_lighting)
	mySettingsChanged = true;
    myDoLighting = do_lighting;
}

void
HUSD_Imaging::setMaterials(bool do_materials)
{
    if (myDoMaterials != do_materials)
	mySettingsChanged = true;
    myDoMaterials = do_materials;
}

const HUSD_DataHandle &
HUSD_Imaging::viewerLopDataHandle() const
{
    return myDataHandle;
}

// Start of anonymous namespace
namespace
{
    static void
    warnAboutBadDelegate(int signal)
    {
        fprintf(stderr, "WARNING: Crashing creating delegate, this might happen\n");
        fprintf(stderr, "\tif the TfType template name doesn't match the string\n");
        fprintf(stderr, "\tin the .json file\n");
    }

    static bool
    isSupported(const TfToken &id)
    {
        UT_Signal               trap(SIGSEGV, warnAboutBadDelegate, true);
	auto			&reg = HdRendererPluginRegistry::GetInstance();
	HdRendererPlugin	*plugin = reg.GetRendererPlugin(id);
	bool			 supported = false;

	if (plugin)
	{
	    supported = plugin->IsSupported();
	    reg.ReleasePlugin(plugin);
	}
        if (!supported && UT_EnvControl::getInt(ENV_HOUDINI_DSO_ERROR))
        {
            static UT_Set<TfToken>      map;
            if (!map.contains(id))
            {
                map.insert(id);
                UTformat(stderr,
                        "Unable to create Usd Render Plugin: {}\n", id);
            }
        }

	return supported;
    }

    static UT_StringHolder
    getDefaultRendererName()
    {
	auto &&reg = HdRendererPluginRegistry::GetInstance();
	return UT_StringHolder(reg.GetDefaultPluginId().GetText());
    }
}
// End of anonymous namespace

bool
HUSD_Imaging::setupRenderer(const UT_StringRef &renderer_name,
                            const UT_Options *render_opts,
                            bool cam_effects)
{
    UT_StringHolder	 new_renderer_name = renderer_name;

    // At this point we are ready to create our new imaging engine if we
    // need one. But first, make sure that we are allowed to render...
    if (!myAllowUpdates)
    {
        if (!myPrivate->myImagingEngine)
        {
            myConverged = true;
            backgroundRenderState(myConverged, this);
        }
        return true;
    }

    if(render_opts)
    {
	if(*render_opts != myCurrentDisplayOptions)
	{
            myCurrentDisplayOptions = *render_opts;
	    mySettingsChanged = true;
	}
    }
    else if(myCurrentDisplayOptions.getNumOptions() > 0)
    {
        myCurrentDisplayOptions.clear();
	mySettingsChanged = true;
    }

    if(myScene)
	HUSD_Scene::pushScene(myScene);

    if (myRendererName != new_renderer_name)
    {
        // The first time we are asked to create a renderer, if storm is
        // supposed to be supported, we must create a dummy imaging engine
        // to initialize OpenGL and the Hgi library. Otherwise, if we are
        // running in a non-graphical process, we wouldn't be allowed to
        // create a Storm renderer (because Hgi isn't initialized).
        static bool theFirstRendererCreation = true;
        if (myAllowStormRenderer && theFirstRendererCreation)
        {
            XUSD_ImagingEngine::Parameters params;
            params.force_null_hgi = false;
            theFirstRendererCreation = false;
            // Calling this function like this will allocate an imaging engine,
            // then immediately delete it as we free the returned unique ptr.
            XUSD_ImagingEngine::createImagingEngine(params);
        }

        // Make sure we preload any required extra libraries.
        theRendererInfoMap[new_renderer_name].preloadLibraries();

	if (!isSupported(TfToken(new_renderer_name.c_str())))
	{
            // We can never use this renderer because it isn't supported.
            // Remove it from our map of choices, and return false to reject
            // the requested change of renderer.
            if (UT_EnvControl::getInt(ENV_HOUDINI_DSO_ERROR))
            {
                static UT_Set<UT_StringHolder>  badGuys;
                if (!badGuys.contains(new_renderer_name))
                {
                    UTformat("{} not supported - removing from renderer list\n",
                        new_renderer_name);
                    badGuys.insert(new_renderer_name);
                }
            }
	    theRendererInfoMap.erase(new_renderer_name);
            resetImagingEngine();
            myRendererName.clear();
            if(myScene)
                HUSD_Scene::popScene(myScene);

            return false;
	}

        // Reset the engine before changing the renderer name so that we
        // do the proper cleanup for the _old_ renderer, not the cleanup that
        // would be appropriate for the _new_ renderer.
        resetImagingEngine();
        myRendererName = new_renderer_name;
    }

    const HUSD_DataHandle &maindata = viewerLopDataHandle();

    if (maindata.rootLayerIdentifier() != myPrivate->myRootLayerIdentifier)
    {
        resetImagingEngine();
	myPrivate->myRootLayerIdentifier = maindata.rootLayerIdentifier();
    }

    // Check for restart settings changes even if the imaging engine is
    // already null, because this method also initializes the camera settings
    // map with the current values.
    if (updateRestartCameraSettings(cam_effects) ||
        (myPrivate->myImagingEngine && anyRestartRenderSettingsChanged()))
    {
        resetImagingEngine();
    }

    HUSD_LightingMode lighting_mode = render_opts
        ? (HUSD_LightingMode)render_opts->getOptionI("lighting_mode")
        : HUSD_LIGHTING_MODE_NORMAL;
    bool do_lighting = (lighting_mode != HUSD_LIGHTING_MODE_NO_LIGHTING);
    auto &&draw_mode = myPrivate->myRenderParams.myDrawMode;
    if (draw_mode == XUSD_ImagingRenderParams::DRAW_SHADED_FLAT ||
        draw_mode == XUSD_ImagingRenderParams::DRAW_SHADED_SMOOTH ||
        draw_mode == XUSD_ImagingRenderParams::DRAW_WIREFRAME_ON_SURFACE)
	do_lighting = myDoLighting;

    myPrivate->myRenderParams.myEnableLighting = do_lighting;
    myPrivate->myRenderParams.myEnableSceneLights = do_lighting &&
        (lighting_mode != HUSD_LIGHTING_MODE_HEADLIGHT_ONLY &&
         lighting_mode != HUSD_LIGHTING_MODE_DOMELIGHT_ONLY && 
         lighting_mode != HUSD_LIGHTING_MODE_THREE_POINT_ONLY && 
         lighting_mode != HUSD_LIGHTING_MODE_PHYSICAL_SKY_ONLY);
    myPrivate->myRenderParams.myEnableSceneMaterials = myDoMaterials;

    // Setting this value to true causes the "automatic" Alpha Threshold
    // setting to be set to 0.1 instead of 0.5 (which is the value used if
    // this flag is left at its default value of false).
    myPrivate->myRenderParams.myEnableSampleAlphaToCoverage = true;

    // Create myImagingEngine inside a render call. Otherwise
    // we can't initialize OpenGL, so USD won't detect it is
    // running in a GL4 context, so it will use the terrible
    // reference renderer.
    if (!myPrivate->myImagingEngine)
    {
        bool drawmode = theRendererInfoMap[myRendererName].drawModeSupport();

        XUSD_ImagingEngine::Parameters params;
        params.rootPath = SdfPath::AbsoluteRootPath();
        params.excludedPaths = SdfPathVector();
        params.invisedPaths = SdfPathVector();
        params.sceneDelegateID = SdfPath::AbsoluteRootPath();
        params.driver = HdDriver();
        params.rendererPluginId = TfToken();
        params.gpuEnabled = true;
        params.displayUnloadedPrimsWithBounds = drawmode;
        params.allowAsynchronousSceneProcessing = false;
        params.enableUsdDrawMode = drawmode;
        params.use_scene_indices = HUSD_Info::getUsingStageSceneIndex();
        params.force_null_hgi = !myAllowStormRenderer;

	myPrivate->myImagingEngine =
            XUSD_ImagingEngine::createImagingEngine(params);
        if (!myPrivate->myImagingEngine)
        {
            if(myScene)
                HUSD_Scene::popScene(myScene);
            return false;
        }

        if (renderUsesTextureCache(myRendererName))
        {
            UT_VERIFY(theTextureCacheRenders.add(1) > 0);
        }

	if (!myPrivate->myImagingEngine->SetRendererPlugin(
               TfToken(myRendererName.toStdString())))
        {
            if(myScene)
                HUSD_Scene::popScene(myScene);
            // We couldn't change to this renderer right now. This can
            // happen in the case where a render delegate only supports a
            // single instance of the renderer and we are asking for a
            // second instance. The renderer is supported, and this
            // request may work next time, but this time it fails.
            resetImagingEngine();
            myRendererName.clear();
            return false;
        }

        // Update the render delegate's render settings before setting up
        // the AOVs. Because we just created a new render delegate, we need
        // to send all render settings again, so make sure all our caches and
        // are cleared and the "changed" flag is set.
        mySettingsChanged = true;
        myPrivate->myCurrentRenderSettings.clear();
        myPrivate->myRenderParams.myEnableUsdDrawModes = drawmode;

        // Since we have a brand new renderer, we are not converged.
        myConverged = false;

        HUSD_AutoReadLock    lock(maindata, myOverrides, myPostLayers);
        updateSettingsIfRequired(lock);
    }

    bool has_aov = false;

    TfTokenVector list;
    bool aovs_specified = false;

    if(myValidRenderSettingsPrim)
    {
        // Got AOVs from a render settings prim.
        bool has_depth = false;
        bool has_primid = false;
        bool has_instid = false;
        HdAovDescriptorList descs;

        // If myValidRenderSettingsPrim is set, myRenderSettings should also
        // be set.
        UT_ASSERT(myRenderSettings);
        if (myRenderSettings)
            myRenderSettings->collectAovs(
                list, CUSTOM_PRODUCT_ADD_AOVS, descs);

        if (list.size())
        {
            for(auto &t : list)
            {
                if(t == HdAovTokens->depth)
                    has_depth = true;
                else if(t == HdAovTokens->primId)
                    has_primid = true;
                else if(t == HdAovTokens->instanceId)
                    has_instid = true;
            }
            // Make sure depth, primId, and instanceId are in the list.
            if(!has_depth)
                list.push_back(HdAovTokens->depth);
            if(!has_primid)
                list.push_back(HdAovTokens->primId);
            if(!has_instid)
                list.push_back(HdAovTokens->instanceId);

            aovs_specified = true;
        }
    }
    if(!aovs_specified)
    {
        // Use a default set of AOVs.
        list.push_back(HdAovTokens->color);
        list.push_back(HdAovTokens->depth);
        list.push_back(HdAovTokens->normal);
        list.push_back(HdAovTokensMakePrimvar(TfToken("st")));
        list.push_back(HdAovTokens->primId);
        list.push_back(HdAovTokens->instanceId);
    }

    // Figure out which AOVs the renderer actually supports.
    auto aov_list = myPrivate->myImagingEngine->GetRendererAovs(list);
    myRendererPlaneList.clear();
    for(auto &t : aov_list)
    {
        myRendererPlaneList.append(t.GetText());
        if (myOutputPlane.isstring() &&
            myOutputPlane == myRendererPlaneList.last())
        {
            has_aov = true;
            myCurrentAOV = myOutputPlane;
        }
    }
    // Merge our final list of output planes.
    myPlaneList = myRendererPlaneList;

    if (has_aov)
    {
        TfToken outputplane_token(myOutputPlane.toStdString());

        if (std::find(list.begin(), list.end(), outputplane_token) == list.end())
            list.push_back(outputplane_token);
    }
    else
        myCurrentAOV = list[0].GetText();

    if(myPrivate->myImagingEngine->SetRendererAovs( list ) &&
        myValidRenderSettingsPrim)
    {
        for(auto &aov_name  : list)
        {
            HdAovDescriptor aov_desc;
            if(myRenderSettingsContext->getAovDescriptor(aov_name, aov_desc))
                myPrivate->myImagingEngine->
                    SetRenderOutputSettings(aov_name, aov_desc);
        }
    }

    myWorkLightsChanged = false;

    if(myScene)
	HUSD_Scene::popScene(myScene);

    return true;
}

void
HUSD_Imaging::updatePlanes(bool ignore_slapcomp)
{
    // If slapcomp is disabled, we merely have to reset the layer list.
    if (!isSlapCompEnabled() || ignore_slapcomp)
    {
        // If the current AOV is a slapcomp output, pick a new one (the first
        // renderer layer).
        if (isSlapCompAOV(myCurrentAOV))
            myCurrentAOV = myPlaneList[0];

        // Clear all slapcomp outputs.
        myPlaneList.truncate(myRendererPlaneList.entries());
        return;
    }

    // The renderer's planes should always come first, so truncation here is
    // equivalent to setting myPlaneList equal to myRendererPlaneList: the
    // real intention behind this line.
    myPlaneList.truncate(myRendererPlaneList.entries());
}

bool
HUSD_Imaging::setOutputPlane(const UT_StringRef &name)
{
    myOutputPlane = name;

    if ((myValidRenderSettingsPrim && myRenderSettingsContext->hasAOV(name)))
    {
        myCurrentAOV = name;
        return true;
    }

    return false;
}

static const UT_StringHolder theStageMetersPerUnit("stageMetersPerUnit");
static const UT_StringHolder theHoudiniViewportToken("houdini:viewport");
static const UT_StringHolder theHoudiniFrameToken("houdini:frame");
static const UT_StringHolder theHoudiniFPSToken("houdini:fps");
static const UT_StringHolder theRenderCameraPathToken("renderCameraPath");
static const UT_StringSet    theAlwaysAvailableSettings({
    theStageMetersPerUnit,
    theHoudiniViewportToken,
    theHoudiniFrameToken,
    theHoudiniFPSToken,
    theRenderCameraPathToken
});
static const UT_StringHolder theUseRenderSettingsPrim("houdini:use_render_settings_prim");

static bool
isRestartSetting(const UT_StringRef &key,
        const UT_StringArray &restartsettings)
{
    for (auto &&setting : restartsettings)
        if (key.multiMatch(setting.c_str()))
            return true;

    return false;
}

static bool
isRestartSettingChanged(const UT_StringRef &key,
        const VtValue &vtvalue,
        const UT_StringArray &restartsettings,
        const std::map<TfToken, VtValue> currentsettings)
{
    TfToken       tfkey(key.toStdString());
    auto        &&it = currentsettings.find(tfkey);

    if (it == currentsettings.end() || it->second != vtvalue)
        return isRestartSetting(key, restartsettings);

    return false;
}

bool
HUSD_Imaging::updateRestartCameraSettings(bool cam_effects) const
{
    if (!theRendererInfoMap.contains(myRendererName))
        return false;

    const UT_StringArray &restart_camera_settings =
        theRendererInfoMap[myRendererName].restartCameraSettings();
    bool restart_required = false;

    if (!restart_camera_settings.isEmpty())
    {
        HUSD_AutoReadLock lock(viewerLopDataHandle(), myOverrides, myPostLayers);
        SdfPath campath;

        if(!myCameraPath.isstring() || !myCameraSynced || !cam_effects)
            campath = HUSDgetHoudiniFreeCameraSdfPath();
        else if(myCameraPath)
            campath = SdfPath(myCameraPath.toStdString());

        if (lock.data() && lock.data()->isStageValid())
        {
            UsdPrim cam = lock.data()->stage()->GetPrimAtPath(campath);

            std::vector<UsdAttribute> attributes = cam
                ? cam.GetAttributes()
                : std::vector<UsdAttribute>();
            std::set<TfToken> missingsettings;

            for (auto it = myPrivate->myCurrentCameraSettings.begin();
                      it != myPrivate->myCurrentCameraSettings.end(); ++it)
                missingsettings.insert(it->first);

            for (auto &&attr : attributes)
            {
                const TfToken &attrname = attr.GetName();
                VtValue value;

                attr.Get(&value, UsdTimeCode::EarliestTime());
                if (!value.IsEmpty())
                {
                    missingsettings.erase(attrname);
                    if (isRestartSettingChanged(attrname.GetText(),
                            value, restart_camera_settings,
                            myPrivate->myCurrentCameraSettings))
                    {
                        myPrivate->myCurrentCameraSettings[attrname] = value;
                        restart_required = true;
                    }
                }
            }

            for (auto &&missingsetting : missingsettings)
            {
                myPrivate->myCurrentCameraSettings.erase(missingsetting);
                restart_required = true;
            }
        }
    }

    return restart_required;
}

bool
HUSD_Imaging::anyRestartRenderSettingsChanged() const
{
    if (!theRendererInfoMap.contains(myRendererName))
        return false;

    if (myPrivate->myRenderParams != myPrivate->myLastRenderParams ||
        mySettingsChanged)
    {
        if(myWorkLightsChanged)
            return true;
        
        const UT_StringArray &restart_render_settings =
            theRendererInfoMap[myRendererName].restartRenderSettings();
        SdfPath campath;

        if(!myCameraPath.isstring() || !myCameraSynced)
            campath = HUSDgetHoudiniFreeCameraSdfPath();
        else if(myCameraPath)
            campath = SdfPath(myCameraPath.toStdString());

        if (isRestartSettingChanged(theHoudiniFrameToken,
                VtValue(myFrame), restart_render_settings,
                myPrivate->myCurrentRenderSettings) ||
            isRestartSettingChanged("renderCameraPath",
                VtValue(campath), restart_render_settings,
                myPrivate->myCurrentRenderSettings))
            return true;

        for(auto opt : myPrivate->myOldPrimRenderSettingMap)
        {
            const UT_StringRef optnamestr(opt.first.GetText());
            auto it = myPrivate->myPrimRenderSettingMap.find(opt.first);

            // If the setting the has been removed is one of the special
            // "always on" settings added above, or if we will immediately
            // be setting the value from myCurrentDisplayOptions in the
            // next loop, don't bother clearing the setting here.
            if (it == myPrivate->myPrimRenderSettingMap.end() &&
                !theAlwaysAvailableSettings.contains(optnamestr) &&
                !myCurrentDisplayOptions.getOptionEntry(optnamestr) &&
                isRestartSetting(optnamestr, restart_render_settings))
                return true;
        }

        for(auto opt = myCurrentDisplayOptions.begin();
            opt != myCurrentDisplayOptions.end(); ++opt)
        {
            if(myValidRenderSettingsPrim)
            {
                // Render setting prims override display options. Skip
                // any display options in case a render setting exists
                // for that option.
                TfToken name(opt.name());
                auto it = myPrivate->myPrimRenderSettingMap.find(name);
                if(it != myPrivate->myPrimRenderSettingMap.end())
                    continue;
            }

            VtValue value(HUSDoptionToVtValue(opt.entry()));
            if (!value.IsEmpty() &&
                isRestartSettingChanged(opt.name(),
                    value, restart_render_settings,
                    myPrivate->myCurrentRenderSettings))
                return true;
        }

        if(myValidRenderSettingsPrim)
        {
            for(auto opt : myPrivate->myPrimRenderSettingMap)
            {
                const auto &key = opt.first;
                auto &&it = myPrivate->myCurrentRenderSettings.find(key);

                if ((it == myPrivate->myCurrentRenderSettings.end() ||
                     it->second != opt.second) &&
                    isRestartSetting(key.GetText(), restart_render_settings))
                    return true;
            }
        }
    }

    return false;
}

void
HUSD_Imaging::updateSettingIfRequired(const UT_StringRef &key,
        const VtValue &vtvalue,
        bool from_usd_prim)
{
    TfToken       tfkey(key.toStdString());
    auto        &&it = myPrivate->myCurrentRenderSettings.find(tfkey);

    if (it == myPrivate->myCurrentRenderSettings.end() || it->second != vtvalue)
    {
        myPrivate->myImagingEngine->SetRendererSetting(tfkey, vtvalue);
        myPrivate->myCurrentRenderSettings[tfkey] = vtvalue;
        UT_ErrorLog::format(4, "Render setting from {}: {} = {}",
            from_usd_prim ? "USD" : "Houdini", tfkey, vtvalue);
    }
}

void
HUSD_Imaging::updateSettingsIfRequired(HUSD_AutoReadLock &lock)
{
    // Pass the stage metrics (meter per units). We do this outside the if
    // block because we don't have any way to detect this change other than
    // fetching the value to see if it changed since our last time here.
    double metersperunit = HUSD_Preferences::defaultMetersPerUnit();
    if (lock.data() && lock.data()->isStageValid())
        metersperunit = UsdGeomGetStageMetersPerUnit(lock.data()->stage());
    double fps = CHgetManager()->getSamplesPerSec();
    if (lock.data() && lock.data()->isStageValid())
        fps = lock.data()->stage()->GetTimeCodesPerSecond();
    updateSettingIfRequired(theStageMetersPerUnit, VtValue(metersperunit));

    // Render setting prims override display options. Pass down the flag
    // to the render delegate too. This enables the delegate to decouple/run
    // different sets of eg. image filters:
    // "karma:global:imagefilter" and "karma:hydra:denoise"
    updateSettingIfRequired(
        theUseRenderSettingsPrim, VtValue(myValidRenderSettingsPrim));

    if (myPrivate->myRenderParams != myPrivate->myLastRenderParams ||
        mySettingsChanged)
    {
        myPrivate->myLastRenderParams = myPrivate->myRenderParams;
        mySettingsChanged = false;

        updateSettingIfRequired(theHoudiniViewportToken, VtValue(true));
        updateSettingIfRequired(theHoudiniFrameToken, VtValue(myFrame));
        updateSettingIfRequired(theHoudiniFPSToken, VtValue(fps));

        SdfPath campath;
        if(!myCameraPath.isstring() || !myCameraSynced)
            campath = HUSDgetHoudiniFreeCameraSdfPath();
        else if(myCameraPath)
            campath = SdfPath(myCameraPath.toStdString());

        updateSettingIfRequired(theRenderCameraPathToken, VtValue(campath));

        for(auto opt : myPrivate->myOldPrimRenderSettingMap)
        {
            const UT_StringRef optnamestr(opt.first.GetText());
            auto it = myPrivate->myPrimRenderSettingMap.find(opt.first);

            // If the setting the has been removed is one of the special
            // "always on" settings added above, or if we will immediately
            // be setting the value from myCurrentDisplayOptions in the
            // next loop, don't bother clearing the setting here.
            if (it == myPrivate->myPrimRenderSettingMap.end() &&
                !theAlwaysAvailableSettings.contains(optnamestr) &&
                !myCurrentDisplayOptions.getOptionEntry(optnamestr))
            {
                myPrivate->myImagingEngine->
                    SetRendererSetting(opt.first, VtValue());
                myPrivate->myCurrentRenderSettings.erase(opt.first);
                UT_ErrorLog::format(4, "Render setting from USD removed: {}",
                    opt.first);
            }
        }

        for(auto opt = myCurrentDisplayOptions.begin();
            opt != myCurrentDisplayOptions.end(); ++opt)
        {
            if(myValidRenderSettingsPrim)
            {
                // Render setting prims override display options. Skip any
                // display options in case a render setting exists for that
                // option.
                TfToken name(opt.name());
                auto it = myPrivate->myPrimRenderSettingMap.find(name);
                if(it != myPrivate->myPrimRenderSettingMap.end())
                    continue;
            }

            VtValue value(HUSDoptionToVtValue(opt.entry()));
            if (!value.IsEmpty())
                updateSettingIfRequired(opt.name(), value);
        }

        if(myValidRenderSettingsPrim)
        {
            for(auto opt : myPrivate->myPrimRenderSettingMap)
                updateSettingIfRequired(opt.first.GetText(), opt.second, true);
        }
    }
}

void
HUSD_Imaging::setRenderFocus(int x, int y) const
{
    if(myPrivate->myImagingEngine)
    {
        auto &&token = HusdHdRenderStatsTokens->viewerMouseClick;

        GfVec2i pos(x,y);
        myPrivate->myImagingEngine->SetRendererSetting(token, VtValue(pos));
    }
}

void
HUSD_Imaging::updateApexAnimateSceneIndex(
        const UT_StringRef &scene_prim_path,
        const UT_StringMap<GU_ConstDetailHandle> &graph_outputs) const
{
    if (myPrivate->myImagingEngine)
    {
        myPrivate->myImagingEngine->updateApexAnimateSceneIndex(
                scene_prim_path, graph_outputs);
    }
}

void
HUSD_Imaging::clearRenderFocus() const
{
    if(myPrivate->myImagingEngine)
    {
        auto &&token = HusdHdRenderStatsTokens->viewerMouseClick;
        GfRect2i null_area(GfVec2i(0,0),0,0);
        myPrivate->myImagingEngine->
            SetRendererSetting(token,VtValue(null_area));
    }
}

void
HUSD_Imaging::setRenderSettingsDataWindowActive(bool active)
{
    myRenderSettingsContext->setRenderSettingsDataWindowActive(active);
}

HUSD_Imaging::RunningStatus
HUSD_Imaging::updateRenderData(const UT_Matrix4D &view_matrix,
                               const UT_Matrix4D &proj_matrix,
                               const UT_DimRect &viewport_rect,
                               bool cam_effects)
{
    // If we have been told not to render, our engine may be null, but we
    // still want to report the requested update as being complete.
    if (!myAllowUpdates)
        return RUNNING_UPDATE_COMPLETE;

    auto &&engine = myPrivate->myImagingEngine;
    bool success = true;

    myRenderKeyToPathMap.clear();
    myReadLock = UTmakeUnique<HUSD_AutoReadLock>(
        myDataHandle, myOverrides, myPostLayers);
    HUSD_AutoReadLock *lock = myReadLock.get();
    if (lock->data() && lock->data()->isStageValid())
    {
        // Update the FPS setting in the "render context" from the stage. This
        // may affect velocity motion blur.
        myRenderSettingsContext->setFPS(
            lock->constData()->stage()->GetTimeCodesPerSecond());
        // UTdebugPrint("\n\n\n\n********************\nSet Window",
        //              viewport_rect);
        // UTdebugPrint("View", view_matrix);
        // UTdebugPrint("Proj", proj_matrix);
        GfVec4d gf_viewport;
        if (myRenderSettings)
            gf_viewport = GfVec4d(0.0,
                0.0,
                myRenderSettings->xres(nullptr),
                myRenderSettings->yres(nullptr));
        else
            gf_viewport = GfVec4d(viewport_rect.x(),
                viewport_rect.y(),
                viewport_rect.w(),
                viewport_rect.h());
        engine->SetRenderViewport(gf_viewport);

        SdfPath campath;
        if(myCameraPath && myCameraSynced && cam_effects)
            campath = SdfPath(myCameraPath.toStdString());
        else
            campath = HUSDgetHoudiniFreeCameraSdfPath();

        if(!campath.IsEmpty())
        {
            engine->SetCameraPath(campath);
            engine->SetWindowPolicy(
                (CameraUtilConformWindowPolicy)myConformPolicy);
        }
        myRenderSettingsContext->setCamera(campath);

        UT_Array<XUSD_GLSimpleLight> lights;
        GfVec4f ambient(0.0, 0.0, 0.0, 0.0);

        if (myPrivate->myRenderParams.myEnableLighting &&
            (myWantsHeadlight || myWantsDomelight ||
             myWantsThreePointlight || myWantsPhysicalSky))
        {
            // With any change to the number/type of simple lights, we
            // first want to clear all the existing "simple" lights,
            // because there seems to be update issues.
            if (myHasHeadlight != myWantsHeadlight ||
                myHasDomelight != myWantsDomelight ||
                myHasThreePointlight != myWantsThreePointlight ||
                myHasPhysicalSky != myWantsPhysicalSky)
                engine->SetLightingState(lights, ambient);

            if(myWantsHeadlight)
            {
                XUSD_GLSimpleLight	 light;

                light.myIsDomeLight = false;
#if MATCH_HYDRA_DEFAULT
                light.myIntensity = 15000.0;
#else
                light.myIntensity = myHeadlightIntensity;
#endif
                light.myAngle = 0.53;
                light.myColor = UT_Vector3F(1.0, 1.0, 1.0);
                light.myXform = view_matrix;
                light.myXform.invert();
                
                lights.append(light);
            }
            if(myWantsDomelight)
            {
                XUSD_GLSimpleLight	 light;

                light.myIsDomeLight = true;
                light.myDomeMapPath = myDomeMapName;
                light.myIntensity = 1.0;
                light.myAngle = 0.53;
                light.myColor = myDomeTint;
                light.myXform.identity();
                light.myXform.rotate(SYSdegToRad(-myDomeRotate.x()),
                                     SYSdegToRad(-myDomeRotate.y()),
                                     SYSdegToRad(-myDomeRotate.z()),
                                     UT_XformOrder());
                lights.append(light);
            }
            if(myWantsThreePointlight)
            {
                UT_XformOrder order(UT_XformOrder::TSR, UT_XformOrder::YXZ);
                const fpreal conversion = 9.34941e4;
                XUSD_GLSimpleLight light;
                light.myIsDomeLight = false;
                light.myAngle = 0.53;
                light.myIntensity = 1.0;


                light.myColor = myKeyColor * conversion;
                light.myXform = view_matrix;
                light.myXform.prerotate(SYSdegToRad(myKeyDir.y()),
                                        SYSdegToRad(-myKeyDir.x()),
                                        0.0, order);
                light.myXform.invert();
                lights.append(light);

                light.myColor = myFillColor * conversion;
                light.myXform = view_matrix;
                light.myXform.prerotate(SYSdegToRad(myFillDir.y()),
                                        SYSdegToRad(-myFillDir.x()),
                                        0.0, order);
                light.myXform.invert();
                lights.append(light);
                
                light.myColor = myBackColor * conversion;
                light.myXform = view_matrix;
                light.myXform.prerotate(SYSdegToRad(myBackDir.y()),
                                        SYSdegToRad(-myBackDir.x()),
                                        0.0, order);
                light.myXform.invert();
                lights.append(light);
            }
            if(myWantsPhysicalSky)
            {
                XUSD_GLSimpleLight sky_light;
                sky_light.myIsDomeLight = true;
                sky_light.myDomeMapPath = mySkyMapName;
                sky_light.myIntensity = 1.0;
                sky_light.myAngle = 0.53;
                sky_light.myColor.assign(mySkyIntensity, mySkyIntensity,
                                         mySkyIntensity);
                sky_light.myXform.identity();
                lights.append(sky_light);

                XUSD_GLSimpleLight sun_light;
                sun_light.myIsDomeLight = false;
                sun_light.myIntensity = 1.0;
                sun_light.myAngle = 0.53;
                sun_light.myColor.assign(mySunIntensity, mySunIntensity,
                                         mySunIntensity);
                sun_light.myXform.identity();
                sun_light.myXform.prerotate(UT_Axis3::YAXIS,
                                            SYSdegToRad(mySkyDir.x()));
                sun_light.myXform.prerotate(UT_Axis3::XAXIS,
                                            -SYSdegToRad(mySkyDir.y()));
               lights.append(sun_light);
            }
            myHasHeadlight = myWantsHeadlight;
            myHasDomelight = myWantsDomelight;
            myHasThreePointlight = myWantsThreePointlight;
            myHasPhysicalSky = myWantsPhysicalSky;
            engine->SetLightingState(lights, ambient);
        }
        else if (myHasHeadlight || myHasDomelight || myHasThreePointlight || myHasPhysicalSky)
        {
            myHasHeadlight = false;
            myHasDomelight = false;
            myHasThreePointlight = false;
            myHasPhysicalSky = false;
            engine->SetLightingState(lights, ambient);
        }

        updateSettingsIfRequired(*lock);

        engine->SetActiveRenderPassPrimPath(myRenderPassPath.sdfPath());
        engine->showHdGpProcedurals(myShowHdGpProcedurals);

        try
        {
            engine->DispatchRender(
                lock->data()->stage()->GetPseudoRoot(),
                myPrivate->myRenderParams);
        }
        catch (std::exception &err)
        {
            UT_ErrorLog::error("Render delegate exception: {}", err.what());
            HUSD_ErrorScope::addError(HUSD_ERR_STRING,
                    "Render delegate threw exception during update");
            success = false;
        }
    }
    else
        success = false;

    // Other renderers need to return to executing on
    // the main thread now. This is where the actual
    // GL calls happen.
    if (success)
        return RUNNING_UPDATE_COMPLETE;
    else
        return RUNNING_UPDATE_FATAL;
}

HUSD_Imaging::BufferSet
HUSD_Imaging::hasAOVBuffers() const
{
    if(myPrivate && myPrivate->myImagingEngine && myCompositor)
    {
        husd_IMXRenderBuffer b;
        void* color_buf = getRenderOrSlapCompOutput(myCurrentAOV, &b);
        void* depth_buf =
            getRenderOrSlapCompOutput(HdAovTokens->depth.GetText(), &b);

        if(color_buf && depth_buf)
            return BUFFER_COLOR_DEPTH;
        else if(color_buf)
            return BUFFER_COLOR;
        else
            return BUFFER_NONE;
    }

    return myLastCompositedBufferSet;
}

void
HUSD_Imaging::setPostRenderCallback(ImagingCallback cb)
{
    myPostRenderCallback = cb;
}

bool
HUSD_Imaging::getUsingCoreProfile()
{
    if (myPrivate->myImagingEngine)
        return myPrivate->myImagingEngine->isUsingGLCoreProfile();

    return false;
}

void
HUSD_Imaging::finishRender(bool do_render)
{
    // myImagingEngine may be null here if we are running updates on the
    // foreground thread, and we have updated to an empty data handle or
    // an empty stage.
    if (!myPrivate->myImagingEngine)
        return;

    // Add dependencies on any COP nodes in the resolver cache. Do this before
    // calling CompleteRender, which spins up the renderer if it isn't already
    // running, which may add COP dependencies, risking a recursive call to
    // handleCopTextureChange.
    gatherCopResolverDependencies();

    if (do_render)
    {
        bool viewportrenderer =
            theRendererInfoMap[myRendererName].viewportRenderer();

        myPrivate->myImagingEngine->CompleteRender(
            myPrivate->myRenderParams, viewportrenderer);
        if (myPostRenderCallback)
            myPostRenderCallback(this);
    }

    auto converged = myPrivate->myImagingEngine->IsConverged();
    if (converged != myConverged)
    {
        myConverged = converged;
        if (!myConverged)
            myAOVsStashed = false;
        backgroundRenderState(converged, this);
        // One last check of COP dependencies when the render completes.
        if (myConverged)
            gatherCopResolverDependencies();
    }

}

namespace
{
    static UT_StringHolder
    valueToString(const VtValue &val)
    {
        if (val.IsHolding<TfToken>())
            return UT_StringHolder(val.UncheckedGet<TfToken>().GetText());
        if (val.IsHolding<std::string>())
            return UT_StringHolder(val.UncheckedGet<std::string>());
        return UT_StringHolder();
    }

    static void
    ocioTransform(const PXL_OCIO::PHandle &proc,
            float *dst,
            const void *src,
            PXL_DataFormat df,
            exint npixels,
            int nchan)
    {
        if (df == PXL_FLOAT32)
        {
            memcpy(dst, src, sizeof(float)*npixels*nchan);
        }
        else
        {
            // Convert source data to float
            PXL_FillParms           fill;
            fill.setSourceType(df);
            fill.setDestType(PXL_FLOAT32);
            fill.mySource = src;
            fill.myDest = dst;
            fill.mySInc = 1;
            fill.myDInc = 1;
            fill.setSourceArea(0, 0, npixels*nchan - 1, 0);
            fill.setDestArea(0, 0, npixels*nchan - 1, 0);
            PXL_Fill::fill(fill);
        }
        PXL_OCIO::transform(proc, dst, npixels, nchan);
    }

    // Stash a specific buffer and add to the aov list,
    // along with any extra buffers.
    void
    stashSlapAOV(bool dostash,
            const COP_SlapCompViewInfo *view_info,
            UT_Array<COP_SlapCompRegistry::AovInfo> &aovs,
            UT_StringHolder aov,
            HUSD_RenderBuffer &buf)
    {
        COP_Type coptype = husd_getBufferCOPType(buf);
        if (coptype == COP_TYPE_UNDEF)
            return;

        aovs.append( { aov, coptype } );

        if (dostash)
        {
            IMX_LayerPtr buf_lay = husd_convertBufferToLayer(buf, view_info);
            COP_SlapCompRegistry::stashLayer(aov, buf_lay);
        }

        // Process any secondary buffers.
        for (int i = 0, n = buf.numExtraBuffers(); i < n; i++)
        {
            UT_StringHolder     extraname = buf.extraName(i);
            HUSD_RenderBuffer   extrabuf = buf.extraBuffer(i);
            if (!extrabuf.isValid())
                continue;

            stashSlapAOV(dostash, view_info, aovs, extraname, extrabuf);
        }
    }
}       // end namespace

bool
HUSD_Imaging::isSlapCompAOV(const UT_StringHolder& name) const
{
    return isSlapCompEnabled() && mySlapCompProgramManager->isSlapCompAOV(name);
}

void
HUSD_Imaging::registerSlapCompAOVs(bool dostash, 
                                   const COP_SlapCompViewInfo *view_info)
{
    if (!myCompositor || !myPrivate || !myPrivate->myImagingEngine)
    {
        return;
    }

    if (myAOVsStashed)
        return;
    myAOVsStashed = dostash;

    UT_Array<COP_SlapCompRegistry::AovInfo>        aovs;
    for (auto && aov : rendererPlanes())
    {
        HUSD_RenderBuffer buf = getAOVBuffer(aov);

        stashSlapAOV(dostash, view_info, aovs, aov, buf);
    }
    COP_SlapCompRegistry::registerViewerAOVs(aovs);

    if (dostash)
        COP_SlapCompRegistry::notifyNodes();
}

void
HUSD_Imaging::runSlapCompIfNeeded(const COP_SlapCompViewInfo* view_info)
{
    if (!myPrivate || !myPrivate->myImagingEngine)
    {
        return;
    }

    UT_StringSet             desiredaovs;

    COP_SlapCompRegistry::activeAOVs(desiredaovs);

    // Write to the registry any aovs we've written that
    // are of interest to anyone.
    for (auto && aov : desiredaovs)
    {
        HUSD_RenderBuffer buf = getAOVBufferIncludingExtra(aov);

        // We don't do C conversion here as we are doing
        // that in the import...
        if (!buf)
        {
            // We don't error here, it is the slapcompimport
            // that doesn't get the values.
            continue;
        }
        // We always use any provided viewport for our global
        // stash as we don't know what the eventual consuming
        // layer will desire.
        IMX_LayerPtr buf_lay = husd_convertBufferToLayer(buf, view_info);
        COP_SlapCompRegistry::stashLayer(aov, buf_lay);
    }

    registerSlapCompAOVs(myConverged, view_info);
    if (!myConverged)
    {
        // Unstashed avos..
        myAOVsStashed = false;
    }

    if (isSlapCompEnabled())
    {
        UT_PerfMonAutoViewportDrawEvent perf("LOP Viewer", "Apply Slap Comp",
                                            UT_PERFMON_3D_VIEWPORT);

        if (!mySlapCompProgramManager->expectsLayerCameras())
            view_info = nullptr;
        husd_ImagingSlapCompInputFetcher input_fetcher(*this, view_info);

        const OP_Context context(OP_Context::CurrentEvalTime);
        const bool ignore_slapcomp =
            !mySlapCompProgramManager->build(input_fetcher, context);

        // If we failed to build, we can't run slapcomp...
        updatePlanes(ignore_slapcomp);
        if (ignore_slapcomp)
            return;

        // Don't run slapcomp if there is no need...
        if (!isSlapCompAOV(myCurrentAOV) &&
            !isSlapCompAOV(HdAovTokens->depth.GetText()) &&
            !isSlapCompAOV(HdAovTokens->primId.GetText()) &&
            !isSlapCompAOV(HdAovTokens->instanceId.GetText()))
            return;

        mySlapCompProgramManager->runSlapComp(input_fetcher, context,
                                              false // should_build
                                              );
    }
}

void*
HUSD_Imaging::getRenderOrSlapCompOutput(const UT_StringHolder& aov,
                                        husd_IMXRenderBuffer* b) const
{
    UT_ASSERT(b);

    if (myPrivate && myPrivate->myImagingEngine)
    {
        bool is_slapcomp_aov = isSlapCompAOV(aov);
        if (is_slapcomp_aov && !mySlapCompProgramManager->hasErrors())
        {
            IMX_LayerPtr layer = mySlapCompProgramManager->getOutputLayer(aov);
            if (!layer && aov == HdAovTokens->color)
                layer = mySlapCompProgramManager->getOutputLayer("C"_sh);
            b->setLayer(layer);
            return b;
        }
        else if (is_slapcomp_aov)
        {
            // If it is a slapcomp AOV, but we had errors executing, return the
            // first renderer AOV instead.
            return myPrivate->myImagingEngine->GetRenderOutput(
                TfToken(myRendererPlaneList[0]));
        }
        else
        {
            return myPrivate->myImagingEngine->GetRenderOutput(TfToken(aov));
        }
    }

    return nullptr;
}

void
HUSD_Imaging::updateComposite(bool free_if_missing,
                              const COP_SlapCompViewInfo* v)
{
    bool     missing = true;
    PXL_OCIO::PHandle   cxform;

    if (v)
        myLastSlapCompViewInfo = *v;

    if(myCompositor && myPrivate && myPrivate->myImagingEngine)
    {
        if (myRenderSettings)
        {
            const HdRenderSettingsMap &map = myRenderSettings->renderSettings();
            auto it = map.find(HdRenderSettingsPrimTokens->renderingColorSpace);
            if (it != map.end())
            {
                UT_StringHolder              name = valueToString(it->second);
                const PXL_OCIO::ColorSpace  *src = PXL_OCIO::lookupSpace(name);
                if (src)
                {
                    const PXL_OCIO::ColorSpace  *dst = PXL_OCIO::lookupSpace(
                                                PXL_OCIO::getSceneLinearRole());
                    cxform = PXL_OCIO::lookupProcessor(src, dst,
                                            UT_StringHolder());
                    if (cxform.isValid() && cxform.isNoOp())
                        cxform.clear();
                }
            }
        }

        runSlapCompIfNeeded(v);
        husd_IMXRenderBuffer color_buf_imxrb, depth_buf_imxrb, prim_id_imxrb,
                             inst_id_imxrb;
        HdRenderBuffer* color_buf = (HdRenderBuffer*)
            getRenderOrSlapCompOutput(myCurrentAOV, &color_buf_imxrb);
        HdRenderBuffer* depth_buf = (HdRenderBuffer*)
            getRenderOrSlapCompOutput(HdAovTokens->depth.GetText(),
                                      &depth_buf_imxrb);
        HdRenderBuffer* prim_id = (HdRenderBuffer*)
            getRenderOrSlapCompOutput(HdAovTokens->primId.GetText(),
                                      &prim_id_imxrb);
        HdRenderBuffer* inst_id = (HdRenderBuffer*)
            getRenderOrSlapCompOutput(HdAovTokens->instanceId.GetText(),
                                      &inst_id_imxrb);

        if(color_buf && depth_buf)
            myLastCompositedBufferSet = BUFFER_COLOR_DEPTH;
        else if(color_buf)
            myLastCompositedBufferSet = BUFFER_COLOR;
        else
            myLastCompositedBufferSet = BUFFER_NONE;

	if (color_buf && depth_buf)
	{
            HdFormat df = color_buf->GetFormat();
            exint nchan = HdGetComponentCount(df);
            exint id = 0;
            exint w = 0;
            exint h = 0;

            color_buf->Resolve();

            if (myPrivate->myImagingEngine->
                GetRawResource(color_buf, id, w, h))
            {
                myCompositor->setResolution(w, h);
                myCompositor->updateColorTexture(id);
            }
            else
            {
                void *color_map = color_buf->Map();
                w = color_buf->GetWidth();
                h = color_buf->GetHeight();

                if (w && h)
                {
                    myCompositor->setResolution(w, h);

                    if (nchan >= 3 && !cxform.isNoOp())
                    {
                        // We need to transform the color to scene linear before
                        // updating the compositor.
                        UT_StackBuffer<float> tmp(w * h * nchan);
                        ocioTransform(cxform, tmp.array(), color_map,
                            HdToPXL(df), w * h, nchan);
                        myCompositor->updateColorBuffer(
                            tmp.array(), PXL_FLOAT32, nchan);
                    }
                    else
                    {
                        myCompositor->updateColorBuffer(
                            color_map, HdToPXL(df), nchan);
                    }
                }
                color_buf->Unmap();
                color_map = nullptr;
            }
            if (w && h)
	    {
                depth_buf->Resolve();

                if (myPrivate->myImagingEngine->
                    GetRawResource(depth_buf, id, w, h))
                {
                    myCompositor->updateDepthTexture(id);
                }
                else
                {
                    auto depth_map = depth_buf->Map();
                    if(depth_buf->GetWidth() == w && depth_buf->GetHeight() == h)
                    {
                        df = depth_buf->GetFormat();
                        myCompositor->updateDepthBuffer(depth_map,
                            HdToPXL(df), HdGetComponentCount(df));
                    }
                    else
                        myCompositor->updateDepthBuffer(nullptr, PXL_FLOAT32, 0);
                    depth_buf->Unmap();
                }
	    }

            bool prim_id_valid = false;
            if(w && h && prim_id)
            {
                prim_id->Resolve();
                auto df = prim_id->GetFormat();

                if (HdToPXL(df) == PXL_INT32 &&
                    HdGetComponentCount(df) == 1 &&
                    prim_id->GetWidth() == w &&
                    prim_id->GetHeight() == h)
                {
                    if (myPrivate->myImagingEngine->
                        GetRawResource(prim_id, id, w, h))
                    {
                        myCompositor->updatePrimIDTexture(id);
                    }
                    else
                    {
                        auto id_map = prim_id->Map();
                        myCompositor->updatePrimIDBuffer(
                            id_map, HdToPXL(df));
                        prim_id->Unmap();
                    }
                    prim_id_valid = true;
                }
            }
            if (!prim_id_valid)
                myCompositor->updatePrimIDBuffer(nullptr, PXL_INT32);

            bool inst_id_valid = false;
            if(w && h && inst_id)
            {
                inst_id->Resolve();
                auto df = inst_id->GetFormat();

                if (HdToPXL(df) == PXL_INT32 &&
                    HdGetComponentCount(df) == 1 &&
                    inst_id->GetWidth() == w &&
                    inst_id->GetHeight() == h)
                {
                    if (myPrivate->myImagingEngine->
                        GetRawResource(inst_id, id, w, h))
                    {
                        myCompositor->updateInstIDTexture(id);
                    }
                    else
                    {
                        auto id_map = inst_id->Map();
                        myCompositor->updateInstanceIDBuffer(
                            id_map, HdToPXL(df));
                        inst_id->Unmap();
                    }
                    inst_id_valid = true;
                }
            }
            if (!inst_id_valid)
                myCompositor->updateInstanceIDBuffer(nullptr, PXL_INT32);

            missing = false;
#if UT_ASSERT_LEVEL > 0
	    // Uncomment to save AOV buffers to disk for debugging.
	    //myCompositor->saveBuffers("colorbuf.pic", "depthbuf.pic");
#endif
	}
        COP_SlapCompRegistry::notifyNodes();
    }
    else if (myCompositor)
    {
        missing = (myLastCompositedBufferSet == BUFFER_NONE);
    }

    if(myCompositor && free_if_missing && missing)
    {
        myCompositor->updateColorBuffer(nullptr, PXL_FLOAT32, 0);
        myCompositor->updateDepthBuffer(nullptr, PXL_FLOAT32, 0);
    }
}

HUSD_RenderBuffer
HUSD_Imaging::getAOVBuffer(const UT_StringRef &name) const
{
    if (!myPrivate || !myPrivate->myImagingEngine)
        return HUSD_RenderBuffer();

    return HUSD_RenderBuffer(
        myPrivate->myImagingEngine->GetRenderOutput(TfToken(name)));
}

HUSD_RenderBuffer
HUSD_Imaging::getAOVBufferIncludingExtra(const UT_StringRef &name) const
{
    HUSD_RenderBuffer   buf = getAOVBuffer(name);
    if (buf.isValid()) return buf;

    // If not valid, we have to test all extra buffers...
    for (auto && aov : rendererPlanes())
    {
        buf = getAOVBuffer(aov);
        if (!buf.isValid())
            continue;

        for (int i = 0, n = buf.numExtraBuffers(); i < n; i++)
        {
            UT_StringHolder     extraname = buf.extraName(i);

            if (extraname == name)
                return buf.extraBuffer(i);
        }
    }
    return HUSD_RenderBuffer();
}


const UT_StringSet &
HUSD_Imaging::getColorAOVs() const
{
    if (myRenderSettingsContext && myRenderSettingsContext->hasAOVs())
        return myRenderSettingsContext->colorAOVs();
    return theDefaultColorAOVs;
}

const UT_StringSet &
HUSD_Imaging::getNonColorAOVs() const
{
    if (myRenderSettingsContext && myRenderSettingsContext->hasAOVs())
        return myRenderSettingsContext->nonColorAOVs();
    return theDefaultNonColorAOVs;
}

void
HUSD_Imaging::getAOVRasters(const UT_Vector2i &res,
        const UT_StringHolder &aovpattern,
        UT_StringArray &aovnames,
        UT_StringMap<UT_UniquePtr<PXL_Raster>> &rasters,
        const PXL_Raster *background_raster,
        int bleft, int bbottom, int bright, int btop,
        fpreal sx, fpreal sy, fpreal sw, fpreal sh) const
{
    for (auto &&plane_name : rendererPlanes())
    {
        // If the AOV doesn't match the requested pattern, don't return it.
        // An empty pattern means return all AOVs.
        if (aovpattern.isstring() && !plane_name.multiMatch(aovpattern.c_str()))
        {
            aovnames.append(plane_name);
            continue;
        }

        // Grab the non-HoudiniGL render (may be just a render region)
        HUSD_RenderBuffer buf = getAOVBuffer(plane_name);
        if (!buf.isValid())
            continue;

        UT_UniquePtr<PXL_Raster> raster_other(new PXL_Raster(
            buf.packing(), buf.dataFormat(), buf.xres(), buf.yres()));
        memcpy(raster_other->getPixels(), buf.map(), raster_other->getSize());

        // Prepare a full-size raster to composite everything into
        UT_UniquePtr<TIL_Raster> image = UTmakeUnique<TIL_Raster>(
            raster_other->getPacking(),
            raster_other->getFormat(),
            res.x(), res.y(), /*clear*/ 1);

        UT_Vector2i fullres;
        UT_DimRect datawindow;
        if (!getAOVBufferInfo(fullres, datawindow))
        {
            fullres.assign(image->getXres(), image->getYres());
            datawindow.set(0, 0, image->getXres(), image->getYres());
        }

        static constexpr UT_StringLit theCName("C");
        static constexpr UT_StringLit theColorName("color");

        // If we're doing a render region and we're processing "C" or "color",
        // we want to place the background image first.
        if (background_raster &&
            (plane_name == theCName.asRef() ||
             plane_name == theColorName.asRef()))
        {
            int bl = bleft == -1 ? 0 : bleft;
            int bb = bbottom == -1 ? 0 : bbottom;
            int br = bright == -1 ? background_raster->getXres() - 1 : bright;
            int bt = btop == -1 ? background_raster->getYres() - 1 : btop;
            
            image->scaledInsertFromRaster(
                background_raster, UT_FILTER_BOX,
                bl, bb, br - bl + 1, bt - bb + 1,
                0, 0, res.x(), res.y());
        }

        // And now composite the real render on top
        if (sx != 0.0 || sy != 0.0 || sw != 1.0 || sh != 1.0)
        {
            // If we have a subregion, then our data window is always
            // the full subregion, so we don't need to handle both a
            // subregion and a data window simultaneously.
            int isx = (int)SYSrint(sx * res.x());
            int isy = (int)SYSrint(sy * res.y());
            int isw = (int)SYSrint(sw * res.x());
            int ish = (int)SYSrint(sh * res.y());
            int cropx1 = SYSclamp(isx, 0, res.x()-1) - isx;
            int cropy1 = SYSclamp(isy, 0, res.y()-1) - isy;
            int cropx2 = SYSclamp(isx+isw, 0, res.x()-1) - isx - 1;
            int cropy2 = SYSclamp(isy+ish, 0, res.y()-1) - isy - 1;
            // Make sure our crop window actually has valid pixels
            if (cropx2 >= cropx1 && cropy2 >= cropy1)
                image->scaledInsertFromRaster(
                    raster_other.get(), UT_FILTER_BOX,
                    cropx1, cropy1, cropx2-cropx1+1, cropy2-cropy1+1,
                    cropx1+isx, cropy1+isy, cropx2-cropx1+1, cropy2-cropy1+1);
        }
        else if (datawindow.x() != 0 || datawindow.y() != 0 ||
                 datawindow.w() != res.x() || datawindow.h() != res.y())
        {
            // Handle the case of a data window. Copy the full raster
            // (which should be the size of the data window) into the
            // data window within the full image.
            int cropx = SYSclamp(datawindow.x1(), 0, res.x());
            int cropy = SYSclamp(datawindow.y1(), 0, res.y());
            int sizex = SYSclamp(datawindow.w(), 0, res.x() - cropx);
            int sizey = SYSclamp(datawindow.h(), 0, res.y() - cropy);
            image->scaledInsertFromRaster(
                raster_other.get(), UT_FILTER_BOX,
                0, 0, raster_other->getXres(), raster_other->getYres(),
                cropx, cropy, sizex, sizey);
        }
        else if (raster_other->getXres() != res.x() ||
                 raster_other->getYres() != res.y())
        {
            image->scaledInsertFromRaster(
                raster_other.get(), UT_FILTER_BOX,
                0, 0, raster_other->getXres(), raster_other->getYres(),
                0, 0, res.x(), res.y());
        }
        else
            image->copy(*raster_other);

        // Add the raster and aov name to the set of images to write out,
        // ensuring "C" is written out first.
        if (plane_name == theCName.asRef() ||
            plane_name == theColorName.asRef())
            aovnames.insert(plane_name, 0);
        else
            aovnames.append(plane_name);
        rasters.emplace(plane_name, std::move(image));
    }
}

bool
HUSD_Imaging::getAOVBufferInfo(UT_Vector2i &resolution,
        UT_DimRect &data_window) const
{
    bool got_data = false;

    if (myRenderSettings)
    {
        GfVec2i gfres = myRenderSettings->res(nullptr);
        resolution = UT_Vector2i(gfres[0], gfres[1]);
        GfVec4f gfw = myRenderSettings->dataWindowF(nullptr);
        data_window = XUSD_RenderSettings::computeDataWindow(gfres, gfw);
        got_data = true;
    }

    return got_data;
}

bool
HUSD_Imaging::canBackgroundRender(const UT_StringRef &renderer) const
{
    bool pref = HUSD_Preferences::updateRendererInBackground();
    UT_StringHolder rname = renderer.isstring() ? renderer : myRendererName;

    // myRendererName should either be something in our map, or the empty
    // string.
    initializeAvailableRenderers();
    if (!theRendererInfoMap.contains(rname))
    {
        UT_ASSERT(!rname.isstring());
        return false;
    }

    return (pref && theRendererInfoMap[rname].allowBackgroundUpdate());
}

bool
HUSD_Imaging::launchBackgroundRender(const UT_Matrix4D &view_matrix,
                                     const UT_Matrix4D &proj_matrix,
                                     const UT_DimRect &viewport_rect,
                                     const UT_StringRef &renderer,
                                     const UT_Options *render_opts,
                                     bool cam_effects)
{
    // An empty renderer name means clear out our imaging data and exit.
    if (!renderer.isstring())
    {
        waitForUpdateToComplete();
        resetImagingEngine();
	return false;
    }

    RunningStatus status = RunningStatus(myRunningInBackground.relaxedLoad());
    if(status != RUNNING_UPDATE_NOT_STARTED)
        return false;

    // If we aren't running in the background, we are free to start a new
    // update/redraw sequence.
    if(!setupRenderer(renderer, render_opts, cam_effects))
        return false;

    // Run the update in the background. Set our running in
    // background status, and spin up the background thread.
    // TODO: Make this a reusable thread instead of creating
    //       a new one every time.
    myRunningInBackground.store(RUNNING_UPDATE_IN_BACKGROUND);

    // If we don't run in the background, handles take a long time to update in
    // the kitchen scene while transforming a large selection of geometry.
    // When we run in the background, the handles are much more interactive.
    if (UT_Thread::getNumProcessors() > 1)
    {
	myPrivate->myUpdateTask.run([this, renderer, view_matrix, proj_matrix,
                                     viewport_rect, cam_effects]()
            {
                UT_PerfMonAutoViewportDrawEvent perfevent("LOP Viewer",
                    "Background Update USD Stage", UT_PERFMON_3D_VIEWPORT);
                utTraceViewportDrawEvent("LOP Viewer",
                    "Background Update USD Stage");
        
                // Before taking the "reload layer lock", pause the renderer.
                // Otherwise the renderer is left to stop itself during the
                // sync phase, once we already have the layer reload lock.
                // This opens us up to a deadlock in the case where:
                // 1. This thread owns the layer reload lock, and is syncing
                //    the renderer (which may require waiting for the renderer
                //    to finish whatever buckets it is working on).
                // 2. The main thread owns the HOM lock, and is waiting for
                //    the layer reload lock in
                //    XUSD_LockedGeoRegistry::createLockedGeo (in the
                //    middle of a cook).
                // 3. A rendering thread is trying to stop, but first it needs
                //    to finish its current bucket, which involves cooking a
                //    COP node to grab a texture map. This requires the HOM
                //    lock.
                // So we have (2) waiting for (1) which is waiting for (3)
                // which is waiting for (2). By pausing the renderer first,
                // we take (3) out of the equation before we have grabbed the
                // reload layer lock which might block (2). (1) can proceed to
                // completion, then (2) can proceed, then (3) can start up
                // again, and grab COP textures once (1) is done cooking.
                // See Bug 137353.
                bool do_resume_render = false;
                if (!myIsPaused &&
                    theRendererInfoMap[renderer].pauseOnUpdate() &&
                    canPause())
                {
                    do_resume_render =
                        myPrivate->myImagingEngine->PauseRenderer();
                }

                // This will hold the result of the updateRenderData call.
                RunningStatus newstatus = RUNNING_UPDATE_IN_BACKGROUND;

                // Make sure nobody calls Reload on any layers while we are
                // performing our update/sync from the viewport stage. This
                // is the only way in which code on the main thread might
                // try to write to/modify any layers referenced by the
                // viewport stage during this update.
                {
                    UT_AutoLock lockscope(HUSDgetLayerReloadLock());

                    newstatus = updateRenderData(
                        view_matrix, proj_matrix, viewport_rect, cam_effects);
                }

                // Resume the render if we paused it.
                if (do_resume_render)
                    myPrivate->myImagingEngine->ResumeRenderer();

                // Change the update status to indicate that we are no
                // longer updating. Do this after resuming the renderer so
                // we can't initiate another background update and interleave
                // pause/resume calls.
                if (newstatus == RUNNING_UPDATE_NOT_STARTED ||
                    newstatus == RUNNING_UPDATE_FATAL)
                    myReadLock.reset();
                myRunningInBackground.store(newstatus);
            });
    }
    else
    {
        status = updateRenderData(
            view_matrix, proj_matrix, viewport_rect, cam_effects);

	if (status == RUNNING_UPDATE_NOT_STARTED ||
	    status == RUNNING_UPDATE_FATAL)
	    myReadLock.reset();
        myRunningInBackground.store(status);
    }

    //UTdebugPrint("Finish launch");
    return true;
}

void
HUSD_Imaging::waitForUpdateToComplete()
{
    RunningStatus status = RunningStatus(myRunningInBackground.relaxedLoad());

    // Loop as long as the background thread is still updating.
    while (status == RUNNING_UPDATE_IN_BACKGROUND)
    {
        // Release the HOM_Lock while we sleep waiting for the render
        // delegate to finish updating, so we don't get stuck in a deadlock
        // as a render thread tries to cook a COP texture.
        {
            HOM_AutoUnlock hom_unlock;
            UTnap(1);
        }
        status = RunningStatus(myRunningInBackground.relaxedLoad());
    }

    // Advance from any error state or the RUNNING_UPDATE_COMPLETE state to
    // the RUNNING_UPDATE_NOT_STARTED state, and free our lock on the stage.
    // But don't do any actual rendering.
    checkRender(false);
}

bool
HUSD_Imaging::checkRender(bool do_render)
{
    RunningStatus status = RunningStatus(myRunningInBackground.relaxedLoad());

    if(status == RUNNING_UPDATE_FATAL)
    {
        // Serious error, or updating to a completely empty stage.
        // Delete our render delegate and free our stage.
        resetImagingEngine();
	myReadLock.reset();
        myRunningInBackground.store(RUNNING_UPDATE_NOT_STARTED);
        return true;
    }

    if (status == RUNNING_UPDATE_COMPLETE)
    {
	myReadLock.reset();
	myRunningInBackground.store(RUNNING_UPDATE_NOT_STARTED);
        status = RUNNING_UPDATE_NOT_STARTED;
        // If we end up here after running an update, but before finishRender
        // has ever been called, we need to force the do_Render flag to true
        // here so that we call CompleteRender at least once before doing a
        // "convergence" test. The CompleteRender call runs HdPass::_Execute
        // which is where the render pass picks up its new set of AOVs, which
        // may have been altered (and so may point to deleted memory) by our
        // most recent update.
        do_render = true;
    }

    // Call finishRender to make sure our converged state is accurate. This
    // also starts up the render if do_render is `true`.
    if (status == RUNNING_UPDATE_NOT_STARTED)
        finishRender(do_render);

    return (status == RUNNING_UPDATE_NOT_STARTED);
}

bool
HUSD_Imaging::render(const UT_Matrix4D &view_matrix,
                     const UT_Matrix4D &proj_matrix,
                     const UT_DimRect &viewport_rect,
                     const UT_StringRef &renderer_name,
                     const UT_Options *render_opts,
                     bool cam_effects,
                     const COP_SlapCompViewInfo *view_info)
{
    // An empty renderer name means clear out our imaging data and exit.
    if (!renderer_name.isstring())
    {
        waitForUpdateToComplete();
        resetImagingEngine();
	return false;
    }

    if (view_info)
        myLastSlapCompViewInfo = *view_info;

    // UTdebugPrint("RENDER & WAIT");
    if(!setupRenderer(renderer_name, render_opts, cam_effects))
        return false;
    
    // Run the update in the foreground. We never enter any running
    // in background status other than "not started".
    RunningStatus status = updateRenderData(
        view_matrix, proj_matrix, viewport_rect, cam_effects);

    if(status == RUNNING_UPDATE_FATAL)
    {
        // Serious error, or updating to a completely empty stage.
        // Delete our render delegate.
        resetImagingEngine();
    }
    else
    {
        finishRender(true);
        updateComposite(false, view_info);
    }
    myReadLock.reset();

    return true;
}

void
HUSD_Imaging::gatherCopResolverDependencies()
{
    if (renderUsesCopResolverCache(myRendererName))
    {
        // Gather dependencies from the TIL_CopResolver cache that is used by
        // Karma to cache COP textures. This is where we establish dependencies
        // let us restart the renderer if any of these COPs change.
        UT_IntArray cached_node_ids;
        bool handle_texture_change = false;
        TIL_CopResolver::getResolverCacheNodeIds(cached_node_ids);
        for (auto &&node_id : cached_node_ids)
        {
            if (myPrivate->myCopTextureCachedNodeIds.emplace(node_id).second)
            {
                OP_Node *node = OP_Node::lookupNode(node_id);
                if (node)
                {
                    myPrivate->myCopTextureMicroNode.addExplicitInput(
                        node->dataMicroNode());
                    if (node->dataMicroNode().isTimeDependent())
                        myPrivate->myCopTextureMicroNode.setTimeDependent(true);

                    // Build the default context for this node, this
                    // will then add dependencies in the same way we
                    // had when we did the cook.
                    OP_Context context(OP_Context::CurrentEvalTime);
                    node->buildDefaultCopContext(context,
                            &myPrivate->myCopDefaultsMicroNode);

                    // We only count a node as dirty if it isn't in an error
                    // state (error'ed nodes are perpetually dirty, but we
                    // should still get micronode dirtying events when
                    // something changes that might un-error them).
                    if (node->dataMicroNode().isDirty() &&
                        node->getErrorSeverity() < UT_ERROR_ABORT)
                        handle_texture_change = true;
                }
            }
        }
        // If we find a COP that is already dirty, we aren't going to get a
        // "becameDirty" event with later changes, so we need to handle this
        // dirty COP immediately by restarting the render.
        if (handle_texture_change)
            handleCopTextureChange(false);
    }
}

void
HUSD_Imaging::handleCopTextureChange(bool time_changed)
{
    // Avoid recursive calls to this function (which really shouldn't be
    // possible, but better safe than sorry).
    if (myHandlingCopTextureChange)
    {
        UT_ASSERT(!"Recursive call to handleCopTextureChange.");
        return;
    }
    myHandlingCopTextureChange = true;

    // Send a cop_texture_changed notification to the renderer, which means
    // it should stop. We don't need to do this if the update phase is
    // currently running in the background because the renderer will not
    // have grabbed any COP textures yet.
    if (!isUpdateRunning() && myPrivate->myImagingEngine)
    {
        myPrivate->myImagingEngine->SetRendererSetting(
            HusdHuskTokens->houdini_cop_texture_changed, VtValue(1));
    }
    // Clear our existing dependency information. We are restarting the render,
    // so we want to restart our texture dependency gathering.
    myPrivate->clearCopTextureDependencies(myFrame);
    // Clear the resolver cache used by karma. If we are here because of a time
    // change with a time dependent node, we need to clear the whole cache.
    // Otherwise we can just clear dirty cache entries.
    TIL_CopResolver::clearResolverCache(time_changed);
    // Render is no longer converged, call finishRender to start it up again.
    myConverged = false;
    myAOVsStashed = false;
    // Restart the render (unless we are in an update phase, in which case
    // it should be safe to assume that whatever code triggered the update
    // will also trigger the render to start again, or unless there's a time
    // change, in which there are likely other changes coming down the pipe).
    if (!time_changed)
        checkRender(true);
    // Callback to force a redraw in the viewport, which is needed to keep the
    // viewport refreshing with updating data from the renderer.
    if (myCopTextureChangeCallback)
        myCopTextureChangeCallback(this);

    // Safe to call this method again.
    myHandlingCopTextureChange = false;
}

void
HUSD_Imaging::setCopTextureChangeCallback(ImagingCallback cb)
{
    myCopTextureChangeCallback = cb;
}

struct prim_data
{
    HdRprim *prim;
    HdSceneDelegate *del;
    uint64 bits;
};

class husd_UpdatePrims
{
public:
    husd_UpdatePrims(const UT_Array<prim_data> &prims,
		     HdChangeTracker &change_tracker,
		     HdRenderParam *render_parm,
		     const HdReprSelector &repr)
	: myPrims(prims),
	  myChangeTracker(change_tracker),
	  myRenderParm(render_parm),
	  myRepr(repr)
	{}

    void operator()(const UT_BlockedRange<exint> &range) const
	{
	    for(int i=range.begin(); i<range.end(); i++)
	    {
		HdDirtyBits bits = HdDirtyBits(myPrims(i).bits);

		// Call Rprim::Sync(..) on each valid repr of the
		// resolved  repr selector.
		for (size_t ridx = 0;
		     ridx < HdReprSelector::MAX_TOPOLOGY_REPRS; ++ridx) {

		    if (myRepr.IsActiveRepr(ridx)) {
			TfToken const& reprToken = myRepr[ridx];

			myPrims(i).prim->Sync(myPrims(i).del,
				   myRenderParm,
				   &bits,
				   reprToken);
		    }
		}

		// Once we finish our updates, mark the prim as clean in the
		// change tracker, or future edits will not mark this prim as
		// dirty. The HdChangeTracker function to mark a prim as
		// dirty will ignore dirtying of bits that are already dirty.
		myChangeTracker.MarkRprimClean(myPrims(i).prim->GetId(), bits);
	    }
	}

private:
    const UT_Array<prim_data>	&myPrims;
    HdChangeTracker		&myChangeTracker;
    HdRenderParam		*myRenderParm;
    const HdReprSelector	&myRepr;
};

void
HUSD_Imaging::updateDeferredPrims()
{
    auto ridx  = myScene->renderIndex();
    auto rparm = myScene->renderParam();

    UT_Array<prim_data> deferred_prims;

    bool shown[HUSD_HydraPrim::NumRenderTags];
    shown[HUSD_HydraPrim::TagDefault] = true; // always shown.
    shown[HUSD_HydraPrim::TagRender]  = myPrivate->myRenderParams.myShowRender;
    shown[HUSD_HydraPrim::TagProxy]   = myPrivate->myRenderParams.myShowProxy;
    shown[HUSD_HydraPrim::TagGuide]   = myPrivate->myRenderParams.myShowGuides;
    shown[HUSD_HydraPrim::TagInvisible] = false;

    for( auto it : myScene->displayGeometry())
    {
	if(it.second->deferredBits()!= 0)
	{
            if(!shown[it.second->renderTag()])
                continue;
            if(it.second->isPendingDelete())
                continue;
            
	    SdfPath path(it.first.sdfPath());
	    HdRprim *prim = const_cast<HdRprim *>(ridx->GetRprim(path));
	    HdSceneDelegate *del = ridx->GetSceneDelegateForRprim(path);
	    if(prim && del)
            {
		deferred_prims.append({ prim, del, it.second->deferredBits()} );
            }
	}
    }

    if(deferred_prims.entries() > 0)
    {
	// This is ignored, but here for completeness.
	static HdReprSelector	 theRepr(HdReprTokens->smoothHull);
	husd_UpdatePrims	 prim_update(deferred_prims,
				    ridx->GetChangeTracker(),
				    rparm, theRepr);

	UTparallelFor(UT_BlockedRange<exint>(0, deferred_prims.entries()),
		      prim_update);
    }

    deferred_prims.clear();
    for(auto it : myScene->materials())
	if(it.second->deferredBits()!= 0)
        {
	    PXR_NS::SdfPath path(it.first.sdfPath());
	    HdSprim *prim = 
                ridx->GetSprim(HdPrimTypeTokens->material,path);
            
            auto sdel = ridx->GetSceneDelegateForRprim(path);
	    if(prim && sdel)
            {
                HdDirtyBits bits = it.second->deferredBits();
                prim->Sync(sdel, rparm, &bits);
                ridx->GetChangeTracker().MarkSprimClean(path);
            }
        }
}

bool
HUSD_Imaging::getBoundingBox(UT_BoundingBox &bbox, const UT_Matrix3R *rot) const
{
    HUSD_AutoReadLock    lock(viewerLopDataHandle(), myOverrides, myPostLayers);

    if (lock.data() && lock.data()->isStageValid())
    {
	auto		 prim = lock.data()->stage()->GetPseudoRoot();
	UsdTimeCode	 t = myPrivate->myRenderParams.myFrame;
	TfTokenVector	 purposes;

	purposes.push_back(UsdGeomTokens->default_);
	purposes.push_back(UsdGeomTokens->proxy);
	purposes.push_back(UsdGeomTokens->render);
	if (prim)
	{
	    UsdGeomBBoxCache	 bboxcache(t, purposes, true);
	    GfBBox3d		 gfbbox;

	    gfbbox = bboxcache.ComputeWorldBound(prim);
	    if (!gfbbox.GetRange().IsEmpty())
	    {
		const GfRange3d	 range = gfbbox.ComputeAlignedRange();

		bbox = UT_BoundingBox(
		    range.GetMin()[0],
		    range.GetMin()[1],
		    range.GetMin()[2],
		    range.GetMax()[0],
		    range.GetMax()[1],
		    range.GetMax()[2]);

		return true;
	    }
	}
    }

    return false;
}

void
HUSD_Imaging::initializeAvailableRenderers()
{
    static bool theRendererInfoMapGenerated = false;

    // The list of available renderers shouldn't change, so just generate the
    // list once, and remember it.
    if (!theRendererInfoMapGenerated)
    {
        HfPluginDescVector	 plugins;

        theRendererInfoMapGenerated = true;
        HdRendererPluginRegistry::GetInstance().GetPluginDescs(&plugins);
        for (int i = 0, n = plugins.size(); i < n; i++)
        {
            HUSD_RendererInfo	 info =
                HUSD_RendererInfo::getRendererInfo(plugins[i].id.GetText(),
                    plugins[i].displayName);

            if (info.isValid())
                theRendererInfoMap.emplace(plugins[i].id.GetText(), info);
        }
    }
}

bool
HUSD_Imaging::getAvailableRenderers(HUSD_RendererInfoMap &info_map)
{
    initializeAvailableRenderers();

    info_map = theRendererInfoMap;

    return (info_map.size() > 0);
}

bool
HUSD_Imaging::canPause() const
{
    if (myPrivate->myImagingEngine)
        return myPrivate->myImagingEngine->IsPauseRendererSupported();

    return false;
}

void
HUSD_Imaging::pauseRender()
{
    if (!myIsPaused && canPause())
    {
        // Not safe to pause while background updates are happening, because
        // background updates pause/resume the renderer.
        waitForUpdateToComplete();
        // Sporadically, we end up with nullptrs here in $RT/homui/PythonPanel
        if (myPrivate && myPrivate->myImagingEngine)
            myPrivate->myImagingEngine->PauseRenderer();
        myIsPaused = true;
    }
}

void
HUSD_Imaging::resumeRender()
{
    // If updates aren't allowed, then resuming rendering also isn't allowed.
    // This is the difference between a user-imposed "pause" from a menu and
    // the automatic pause/resume that happens when tumbling or performing an
    // update to the scene.
    if (myIsPaused && myAllowUpdates && canPause())
    {
        // Not safe to pause while background updates are happening, because
        // background updates pause/resume the renderer.
        waitForUpdateToComplete();
        myPrivate->myImagingEngine->ResumeRenderer();
        myIsPaused = false;
    }
}

bool
HUSD_Imaging::isPausedByUser() const
{
    // This tests if we have been paused by the user, which involves setting
    // both the paused flag and preventing updates.
    return (myIsPaused && !myAllowUpdates);
}

bool
HUSD_Imaging::isStoppedByUser() const
{
    // This tests if we have been stopped by the user, which involves
    // deleting the render delegate and also preventing updates.
    return (myPrivate->myImagingEngine.get() == nullptr && !myAllowUpdates);
}

bool
HUSD_Imaging::rendererCreated() const
{
    return (myPrivate->myImagingEngine.get() != nullptr);
}

void
HUSD_Imaging::getRenderStats(UT_Options &opts)
{
    if (!myPrivate->myImagingEngine)
        return;

    opts.clear();

    UT_JSONValue            jdict;
    {
        // Convert in a scope so that the JSON writer is flushed to the value
        UT_AutoJSONWriter       jw(jdict);
        HUSDconvertDictionary(*jw, myPrivate->myImagingEngine->GetRenderStats());
    }

    UT_JSONValueMap *jsonStatsMap = jdict.getMap();
    if (jsonStatsMap)
        opts.load(*jsonStatsMap, false, true, true);

    UT_OptionsHolder vp_opts;
    vp_opts.update([&](UT_Options &opt) {
        theRendererInfoMap[myRendererName].extractStatsData(opt, jdict);
    });

    opts.setOptionDict("__viewport", vp_opts);
    opts.setOptionS("__json", jdict.toString());
}

void
HUSD_Imaging::setRenderSettings(const UT_StringRef &settings_path,
                                int w, int h, fpreal resscale,
                                const UT_Vector4F &render_region)
{
    HUSD_AutoReadLock lock(viewerLopDataHandle(), myOverrides, myPostLayers);

    UT_StringHolder spath;
    if (settings_path != HUSD_Scene::viewportRenderPrimToken())
    {
        HUSD_Info info(lock);
        spath = info.getBestRenderSettings(
            settings_path,
            myRenderPassPath.pathStr(),
            true).nameStr();
    }

    bool        valid = spath.isstring() && lock.data();
    if (valid)
    {
        SdfPath path(spath.toStdString());

        myRenderSettingsContext->setRes(w,h);
        myRenderSettingsContext->setResScale(resscale);
        myRenderSettingsContext->setRenderRegion(GusdUT_Gf::Cast(render_region));
        if (!myRenderSettings)
            myRenderSettings = UTmakeUnique<XUSD_RenderSettings>();
        // Our render settings are "valid" only if we have managed to set a
        // valid render settings USD prim into myRenderSettings.
        if (myRenderSettings->init(lock.data()->stage(), path,
                *myRenderSettingsContext) &&
            myRenderSettings->prim())
        {
            // If there are only delegate render products, we want to create a
            // dummy raster product so we can get AOVs.
            myRenderSettings->resolveProducts(lock.data()->stage(),
                *myRenderSettingsContext, CUSTOM_PRODUCT_ADD_AOVS);

            HdAovDescriptorList descs;
            TfTokenVector aov_names;

            if (myRendererName == HUSD_Constants::getHoudiniRendererPluginName())
            {
                getHoudiniPluginAOVs(aov_names, descs);
                myRenderSettingsContext->setAOVs(aov_names, descs);
            }
            else if (myRenderSettings->collectAovs(
                    aov_names, CUSTOM_PRODUCT_ADD_AOVS, descs))
            {
                myRenderSettingsContext->setAOVs(aov_names, descs);
            }

            myPrivate->myOldPrimRenderSettingMap =
                myPrivate->myPrimRenderSettingMap;
            myPrivate->myPrimRenderSettingMap =
                myRenderSettings->renderSettings();

            mySettingsChanged = true;
            myValidRenderSettingsPrim = true;
            valid = true;
        }
        else
        {
            valid = false;
        }
    }

    if(!valid)
    {
        if(myValidRenderSettingsPrim)
        {
            myRenderSettings.reset();
            mySettingsChanged = true;
        }
        myPrivate->myOldPrimRenderSettingMap =
            myPrivate->myPrimRenderSettingMap;
        myPrivate->myPrimRenderSettingMap.clear();
        myValidRenderSettingsPrim = false;
    }
}

void
HUSD_Imaging::getPrimPathsFromRenderKeys(
        const UT_Set<HUSD_RenderKey> &keys,
        HUSD_RenderKeyPathMap &outkeypathmap)
{
    if(myPrivate->myImagingEngine)
    {
        UT_Array<HUSD_RenderKey> decode_keys;

        for (auto &&key : keys)
        {
            auto it = myRenderKeyToPathMap.find(key);

            if (it == myRenderKeyToPathMap.end())
                decode_keys.append(key);
            else
                outkeypathmap.emplace(key, it->second);
        }

        SdfPathVector primpaths;
        std::vector<HdInstancerContext> instancer_contexts;
        UT_WorkBuffer index_string;
        char numstr[UT_NUMBUF];
        bool protopaths_in_instance_selections = UT_EnvControl::getInt(
            ENV_HOUDINI_PROTOPATHS_IN_INSTANCE_SELECTIONS) != 0;

        if (myPrivate->myImagingEngine->DecodeIntersections(
                decode_keys, primpaths, instancer_contexts))
        {
            // Map of point instancer paths to an array of id values.
            UT_StringMap<VtArray<int64>> pi_ids_map;

            for (int i = 0, ni = decode_keys.size(); i < ni; i++)
            {
                UT_StringHolder path;

                // The instancer context will only be populated if the
                // instancer is a point instancer rather than a native
                // instancer. For point instancers, the path should be of
                // the form "/inst[0]", whereas native instancers should
                // return the instance proxy path, and so we bypass the
                // indexed path construction.
                if (!instancer_contexts[i].empty())
                {
                    index_string.strcpy(
                        instancer_contexts[i][0].first.GetAsString());
                    for (int j = 0, nj = instancer_contexts[i].size();
                         j < nj; j++)
                    {
                        // Turn the selection index into an instance id.
                        exint instid = instancer_contexts[i][j].second;
                        SdfPath pi_path = instancer_contexts[i][j].first;
                        UT_StringHolder pi_path_str = pi_path.GetAsString();
                        auto it = pi_ids_map.find(pi_path_str);
                        if (it == pi_ids_map.end())
                        {
                            VtArray<int64> ids;
                            HUSD_AutoReadLock lock(myDataHandle,
                                HUSD_AutoReadLock::OVERRIDES_UNCHANGED);
                            UsdGeomPointInstancer pi(lock.constData()->
                                stage()->GetPrimAtPath(pi_path));
                            if (pi)
                                pi.GetIdsAttr().Get(&ids,
                                    UsdTimeCode(CHgetEvalTime()));
                            it = pi_ids_map.emplace(pi_path_str, ids).first;
                        }
                        if (instid >= 0 && instid < it->second.size())
                            instid = it->second[instid];

                        UT_String::itoa(numstr, instid);
                        index_string.append('[');
                        index_string.append(numstr);
                        if (protopaths_in_instance_selections)
                        {
                            index_string.append(':');
                            if (j+1 < nj)
                                index_string.append(instancer_contexts[i][j+1].
                                    first.GetAsString());
                            else
                                index_string.append(
                                    primpaths[i].GetAsString());
                        }
                        index_string.append(']');
                    }
                    path = index_string;
                }
                else
                    path = primpaths[i].GetAsString();

                myRenderKeyToPathMap.emplace(decode_keys[i], path);
                outkeypathmap.emplace(decode_keys[i], path);
            }
        }
    }
}
