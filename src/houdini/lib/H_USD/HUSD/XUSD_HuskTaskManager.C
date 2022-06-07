/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
 *
 * NAME:	XUSD_HuskTaskManager.C (karma, C++)
 *
 * COMMENTS:
 */

#include "XUSD_HuskTaskManager.h"
#include "XUSD_Format.h"
#include "XUSD_Tokens.h"
#include <UT/UT_Assert.h>
#include <UT/UT_Debug.h>

#include <pxr/imaging/hd/camera.h>
#include <pxr/imaging/hd/renderPass.h>
#include <pxr/imaging/hd/renderPassState.h>
#include <pxr/imaging/hd/renderBuffer.h>
#include <pxr/imaging/hd/renderDelegate.h>
#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/imaging/hd/task.h>

PXR_NAMESPACE_OPEN_SCOPE

namespace
{
    class XUSD_HuskRenderTaskParams
    {
    public:
        XUSD_HuskRenderTaskParams()
            : viewport(0.0)
        {}

        // AOVs to render
        HdRenderPassAovBindingVector aovBindings;

        // Viewer & Camera Framing state
        SdfPath camera;
        GfVec4d viewport;
    };

    bool
    operator==(const XUSD_HuskRenderTaskParams& lhs, const XUSD_HuskRenderTaskParams& rhs)
    {
        return (lhs.aovBindings == rhs.aovBindings &&
                lhs.camera == rhs.camera &&
                lhs.viewport == rhs.viewport);
    }

    bool
    operator!=(const XUSD_HuskRenderTaskParams& lhs, const XUSD_HuskRenderTaskParams& rhs)
    { return !(lhs == rhs); }

    class XUSD_HuskRenderSetupTask : public HdTask
    {
    public:
        XUSD_HuskRenderSetupTask(HdSceneDelegate* delegate, SdfPath const& id);
        ~XUSD_HuskRenderSetupTask() override;


        // APIs used from HdxRenderTask to manage the sync/prepare process.
        void SyncParams(HdSceneDelegate* delegate,
            XUSD_HuskRenderTaskParams const &params);
        void PrepareCamera(HdRenderIndex* renderIndex);

        HdRenderPassStateSharedPtr const &GetRenderPassState() const;

        /// Sync the render pass resources
        void Sync(HdSceneDelegate* delegate,
            HdTaskContext* ctx,
            HdDirtyBits* dirtyBits) override;

        /// Prepare the tasks resources
        void Prepare(HdTaskContext* ctx,
            HdRenderIndex* renderIndex) override;

        /// Execute render pass task
        void Execute(HdTaskContext* ctx) override;

    private:
        HdRenderPassStateSharedPtr _renderPassState;
        SdfPath _cameraId;
        GfVec4d _viewport;
        HdRenderPassAovBindingVector _aovBindings;

        HdRenderPassStateSharedPtr &_GetRenderPassState(HdRenderIndex* renderIndex);

        void _PrepareAovBindings(HdTaskContext* ctx, HdRenderIndex* renderIndex);

        XUSD_HuskRenderSetupTask() = delete;
        XUSD_HuskRenderSetupTask(const XUSD_HuskRenderSetupTask &) = delete;
        XUSD_HuskRenderSetupTask &operator =(const XUSD_HuskRenderSetupTask &) = delete;
    };
    using XUSD_HuskRenderSetupTaskSharedPtr =
        std::shared_ptr<class XUSD_HuskRenderSetupTask>;

    XUSD_HuskRenderSetupTask::XUSD_HuskRenderSetupTask(HdSceneDelegate* delegate, SdfPath const& id)
        : HdTask(id)
        , _viewport(0)
    { }

    XUSD_HuskRenderSetupTask::~XUSD_HuskRenderSetupTask() = default;

    void
    XUSD_HuskRenderSetupTask::SyncParams(HdSceneDelegate* delegate,
        XUSD_HuskRenderTaskParams const &params)
    {
        _viewport = params.viewport;
        _cameraId = params.camera;
        _aovBindings = params.aovBindings;
    }

    void
    XUSD_HuskRenderSetupTask::PrepareCamera(HdRenderIndex* renderIndex)
    {
        // If the render delegate does not support cameras, then
        // there is nothing to do here.
        if (!renderIndex->IsSprimTypeSupported(HdTokens->camera)) {
            return;
        }

        const HdCamera *camera = static_cast<const HdCamera *>(
            renderIndex->GetSprim(HdPrimTypeTokens->camera, _cameraId));
        TF_VERIFY(camera);

        HdRenderPassStateSharedPtr const &renderPassState =
            _GetRenderPassState(renderIndex);

        renderPassState->SetCameraAndViewport(camera, _viewport);
    }

    HdRenderPassStateSharedPtr const &
    XUSD_HuskRenderSetupTask::GetRenderPassState() const {
        return _renderPassState;
    }

    void
    XUSD_HuskRenderSetupTask::Sync(HdSceneDelegate* delegate,
        HdTaskContext* ctx,
        HdDirtyBits* dirtyBits)
    {
        if ((*dirtyBits) & HdChangeTracker::DirtyParams) {
            XUSD_HuskRenderTaskParams params;

            if (!_GetTaskParams(delegate, &params)) {
                return;
            }

            SyncParams(delegate, params);
        }

        *dirtyBits = HdChangeTracker::Clean;
    }

    void
    XUSD_HuskRenderSetupTask::Prepare(HdTaskContext* ctx,
        HdRenderIndex* renderIndex)
    {
        _PrepareAovBindings(ctx, renderIndex);
        PrepareCamera(renderIndex);

        HdRenderPassStateSharedPtr &renderPassState =
            _GetRenderPassState(renderIndex);

        renderPassState->Prepare(renderIndex->GetResourceRegistry());
        (*ctx)[HusdHuskTokens->renderPassState] = VtValue(_renderPassState);
    }

    void
    XUSD_HuskRenderSetupTask::Execute(HdTaskContext* ctx)
    {
        (*ctx)[HusdHuskTokens->renderPassState] = VtValue(_renderPassState);
    }

    HdRenderPassStateSharedPtr &
    XUSD_HuskRenderSetupTask::_GetRenderPassState(HdRenderIndex* renderIndex)
    {
        if (!_renderPassState) {
            HdRenderDelegate *renderDelegate = renderIndex->GetRenderDelegate();
            _renderPassState = renderDelegate->CreateRenderPassState();
        }

        return _renderPassState;
    }

    void
    XUSD_HuskRenderSetupTask::_PrepareAovBindings(HdTaskContext* ctx, HdRenderIndex* renderIndex)
    {
        // Walk the aov bindings, resolving the render index references as they're
        // encountered.
        for (size_t i = 0; i < _aovBindings.size(); ++i) {
            if (_aovBindings[i].renderBuffer == nullptr) {
                _aovBindings[i].renderBuffer = static_cast<HdRenderBuffer*>(
                    renderIndex->GetBprim(HdPrimTypeTokens->renderBuffer,
                        _aovBindings[i].renderBufferId));
            }
        }

        HdRenderPassStateSharedPtr &renderPassState =
            _GetRenderPassState(renderIndex);
        renderPassState->SetAovBindings(_aovBindings);

        if (!_aovBindings.empty()) {
            // XXX Tasks that are not RenderTasks (OIT, ColorCorrection etc) also
            // need access to AOVs, but cannot access SetupTask or RenderPassState.
            // One option is to let them know about the aovs directly (as task
            // parameters), but instead we do so via the task context.
            (*ctx)[HusdHuskTokens->aovBindings] = VtValue(_aovBindings);
        }
    }

    class XUSD_HuskRenderTask : public HdTask
    {
    public:
        XUSD_HuskRenderTask(HdSceneDelegate* delegate, SdfPath const& id);
        ~XUSD_HuskRenderTask() override;

        /// Hooks for progressive rendering (delegated to renderpasses).
        bool IsConverged() const;

        /// Prepare the tasks resources
        void Prepare(HdTaskContext* ctx,
            HdRenderIndex* renderIndex) override;

        /// Execute render pass task
        void Execute(HdTaskContext* ctx) override;

        /// Collect Render Tags used by the task.
        const TfTokenVector &GetRenderTags() const override;

        /// Sync the render pass resources
        void Sync(HdSceneDelegate* delegate,
            HdTaskContext* ctx,
            HdDirtyBits* dirtyBits) override;

    protected:
        HdRenderPassStateSharedPtr _GetRenderPassState(HdTaskContext *ctx) const;

    private:
        HdRenderPassSharedPtr _pass;
        XUSD_HuskRenderSetupTaskSharedPtr _setupTask;
        TfTokenVector _renderTags;

        // Inspect the AOV bindings to determine if any of them need to be cleared.
        bool _NeedToClearAovs(HdRenderPassStateSharedPtr const &renderPassState) const;

        XUSD_HuskRenderTask() = delete;
        XUSD_HuskRenderTask(const XUSD_HuskRenderTask &) = delete;
        XUSD_HuskRenderTask &operator =(const XUSD_HuskRenderTask &) = delete;
    };

    XUSD_HuskRenderTask::XUSD_HuskRenderTask(HdSceneDelegate* delegate, SdfPath const& id)
        : HdTask(id)
        , _pass()
        , _renderTags()
        , _setupTask()
    {
    }

    XUSD_HuskRenderTask::~XUSD_HuskRenderTask() = default;

    bool
    XUSD_HuskRenderTask::IsConverged() const
    {
        if (_pass) {
            return _pass->IsConverged();
        }

        return true;
    }

    void XUSD_HuskRenderTask::Sync(HdSceneDelegate* delegate,
            HdTaskContext* ctx,
            HdDirtyBits* dirtyBits)
    {
        HdDirtyBits bits = *dirtyBits;

        if (bits & HdChangeTracker::DirtyCollection) {

            VtValue val = delegate->Get(GetId(), HdTokens->collection);

            HdRprimCollection collection = val.Get<HdRprimCollection>();

            // Check for cases where the collection is empty (i.e. default
            // constructed).  To do this, the code looks at the root paths,
            // if it is empty, the collection doesn't refer to any prims at
            // all.
            if (collection.GetName().IsEmpty()) {
                _pass.reset();
            } else {
                if (!_pass) {
                    HdRenderIndex &index = delegate->GetRenderIndex();
                    HdRenderDelegate *renderDelegate = index.GetRenderDelegate();
                    _pass = renderDelegate->CreateRenderPass(&index, collection);
                } else {
                    _pass->SetRprimCollection(collection);
                }
            }
        }

        if (bits & HdChangeTracker::DirtyParams) {
            XUSD_HuskRenderTaskParams params;

            // if XUSD_HuskRenderTaskParams is set on this task, create an
            // XUSD_HuskRenderSetupTask to unpack them internally.
            //
            // As params is optional, the base class helpper can't be used.
            VtValue valueVt = delegate->Get(GetId(), HdTokens->params);
            if (valueVt.IsHolding<XUSD_HuskRenderTaskParams>()) {
                params = valueVt.UncheckedGet<XUSD_HuskRenderTaskParams>();

                if (!_setupTask) {
                    // note that _setupTask should have the same id, since it will
                    // use that id to look up params in the scene delegate.
                    // this setup task isn't indexed, so there's no concern
                    // about name conflicts.
                    _setupTask = std::make_shared<XUSD_HuskRenderSetupTask>(
                        delegate, GetId());
                }

                _setupTask->SyncParams(delegate, params);

            } else {
                // If params are not set, expect the renderpass state to be passed
                // in the task context.
            }
        }

        if (bits & HdChangeTracker::DirtyRenderTags) {
            _renderTags = _GetTaskRenderTags(delegate);
        }

        // sync render pass
        if (_pass) {
            _pass->Sync();
        }

        *dirtyBits = HdChangeTracker::Clean;
    }

    void
    XUSD_HuskRenderTask::Prepare(HdTaskContext* ctx,
        HdRenderIndex* renderIndex)
    {
        if (_setupTask) {
            _setupTask->Prepare(ctx, renderIndex);
        }
    }

    void
    XUSD_HuskRenderTask::Execute(HdTaskContext* ctx)
    {
        HdRenderPassStateSharedPtr renderPassState = _GetRenderPassState(ctx);

        if (!TF_VERIFY(renderPassState)) return;

        // Render geometry with the rendertags (if any)
        if (_pass) {
            _pass->Execute(renderPassState, GetRenderTags());
        }
    }

    const TfTokenVector &
    XUSD_HuskRenderTask::GetRenderTags() const
    {
        return _renderTags;
    }

    HdRenderPassStateSharedPtr
    XUSD_HuskRenderTask::_GetRenderPassState(HdTaskContext *ctx) const
    {
        if (_setupTask) {
            // If XUSD_HuskRenderTaskParams is set on this task, we will have created an
            // internal XUSD_HuskRenderSetupTask in _Sync, to sync and unpack the params,
            // and we should use the resulting resources.
            return _setupTask->GetRenderPassState();
        }

        return HdRenderPassStateSharedPtr();
    }

    bool
    XUSD_HuskRenderTask::_NeedToClearAovs(HdRenderPassStateSharedPtr const &renderPassState) const
    {
        HdRenderPassAovBindingVector const &aovBindings =
            renderPassState->GetAovBindings();
        for (auto const & binding : aovBindings) {
            if (!binding.clearValue.IsEmpty()) {
                return true;
            }
        }
        return false;
    }

}

/* virtual */
VtValue
XUSD_HuskTaskManager::ka_Delegate::Get(const SdfPath &id, const TfToken &key)
{
    ka_ValueCache	*vcache = TfMapLookupPtr(myValueCacheMap, id);
    VtValue		 ret;
    if (vcache && TfMapLookup(*vcache, key, &ret))
        return ret;

    TF_CODING_ERROR("%s:%s doesn't exist in the value cache\n",
            id.GetText(), key.GetText());
    return VtValue();
}

VtValue
XUSD_HuskTaskManager::ka_Delegate::GetCameraParamValue(const SdfPath &id,
	const TfToken &key)
{
    if (key == HdCameraTokens->clipPlanes ||
        key == HdCameraTokens->windowPolicy)
    {
        return Get(id, key);
    }
    // XXX: For now, skip handling physical params on the free cam.
    UT_ASSERT(0);
    return VtValue();
}

/* virtual */
HdRenderBufferDescriptor
XUSD_HuskTaskManager::ka_Delegate::GetRenderBufferDescriptor(const SdfPath &id)
{
    return GetParameter<HdRenderBufferDescriptor>(id,
                HusdHuskTokens->renderBufferDescriptor);
}


/* virtual */
TfTokenVector
XUSD_HuskTaskManager::ka_Delegate::GetTaskRenderTags(const SdfPath &taskId)
{
    if (HasParameter(taskId, HdTokens->renderTags))
    {
        return GetParameter<TfTokenVector>(taskId, HdTokens->renderTags);
    }
    return TfTokenVector();
}


// ---------------------------------------------------------------------------
// Task controller implementation.

XUSD_HuskTaskManager::XUSD_HuskTaskManager(HdRenderIndex *index,
	const SdfPath &controllerId,
	const SdfPath &cameraId)
    : myIndex(index)
    , myControllerId(controllerId)
    , myCameraId(cameraId)
    , myTaskDelegate(index, controllerId)
{
    UT_ASSERT(index->IsBprimTypeSupported(HdPrimTypeTokens->renderBuffer));
    createRenderTask();
    UT_ASSERT(!myRenderTaskId.IsEmpty());
}

void
XUSD_HuskTaskManager::setCamera(const SdfPath &cameraId)
{
    if (cameraId != myCameraId)
    {
        myCameraId = cameraId;
        XUSD_HuskRenderTaskParams renderParams =
            myTaskDelegate.GetParameter<XUSD_HuskRenderTaskParams>(myRenderTaskId, HdTokens->params);
        renderParams.camera = cameraId; // Set the new camera id
        myTaskDelegate.SetParameter(myRenderTaskId, HdTokens->params, renderParams);
        GetRenderIndex()->GetChangeTracker().MarkTaskDirty(myRenderTaskId,
                HdChangeTracker::DirtyParams);
    }
}

XUSD_HuskTaskManager::~XUSD_HuskTaskManager()
{
    GetRenderIndex()->RemoveTask(myRenderTaskId);
    for (const auto &id : myAOVPaths)
        GetRenderIndex()->RemoveBprim(HdPrimTypeTokens->renderBuffer, id);
}

SdfPath
XUSD_HuskTaskManager::createRenderTask()
{
    myRenderTaskId = GetControllerId().AppendChild(HusdHuskTokens->karmaTask);

    HdRprimCollection collection(HdTokens->geometry,
                                 HdReprSelector(HdReprTokens->smoothHull),
                                 /*forcedRepr*/ false,
                                 TfToken());
    collection.SetRootPath(SdfPath::AbsoluteRootPath());

    GetRenderIndex()->InsertTask<XUSD_HuskRenderTask>(&myTaskDelegate, myRenderTaskId);

    // Create an initial set of render tags in case the user doesn't set any
    TfTokenVector renderTags = { HdTokens->geometry };

    XUSD_HuskRenderTaskParams renderParams;

    renderParams.camera = myCameraId;
    renderParams.viewport = GfVec4d(0,0,1,1);

    myTaskDelegate.SetParameter(myRenderTaskId, HdTokens->params, renderParams);
    myTaskDelegate.SetParameter(myRenderTaskId, HdTokens->collection, collection);
    myTaskDelegate.SetParameter(myRenderTaskId, HdTokens->renderTags, renderTags);

    return myRenderTaskId;
}

const HdTaskSharedPtrVector
XUSD_HuskTaskManager::GetRenderingTasks() const
{
    HdTaskSharedPtrVector tasks;

    tasks.push_back(GetRenderIndex()->GetTask(myRenderTaskId));

    return tasks;
}

SdfPath
XUSD_HuskTaskManager::aovPath(const TfToken &aov) const
{
    UT_WorkBuffer	tmp;
    tmp.format("aov_{}", aov);
    UT_String           var(tmp.buffer());
    var.forceValidVariableName();
    return GetControllerId().AppendChild(TfToken(var.c_str()));
}

void
XUSD_HuskTaskManager::SetRenderOutputs(const TfTokenVector &names,
	const HdAovDescriptorList &outputDescs)
{
    if (myAOVNames == names)
        return;

    myAOVNames = names;

    // Delete the old renderbuffers.
    for (auto &&path : myAOVPaths)
    {
        GetRenderIndex()->RemoveBprim(HdPrimTypeTokens->renderBuffer, path);
    }
    myAOVPaths.clear();

    // Get the viewport dimensions (for renderbuffer allocation)
    XUSD_HuskRenderTaskParams renderParams =
        myTaskDelegate.GetParameter<XUSD_HuskRenderTaskParams>(myRenderTaskId,
            HdTokens->params);
    GfVec3i dimensions = GfVec3i(renderParams.viewport[2],
        renderParams.viewport[3], 1);

    // Add the new renderbuffers. aovPath returns ids of the form
    // {controller_id}/aov_{name}.
    for (size_t i = 0; i < names.size(); ++i)
    {
        SdfPath aovId = aovPath(names[i]);
        GetRenderIndex()->InsertBprim(HdPrimTypeTokens->renderBuffer,
            &myTaskDelegate, aovId);
        HdRenderBufferDescriptor desc;
        desc.dimensions = dimensions;
        desc.format = outputDescs[i].format;
        desc.multiSampled = outputDescs[i].multiSampled;
        myTaskDelegate.SetParameter(aovId, HusdHuskTokens->renderBufferDescriptor, desc);
        GetRenderIndex()->GetChangeTracker().MarkBprimDirty(aovId,
            HdRenderBuffer::DirtyDescription);
        myAOVPaths.push_back(aovId);
    }

    // Create the aov binding list and set it on the render task.
    HdRenderPassAovBindingVector aovBindings;
    aovBindings.resize(names.size());
    for (size_t i = 0; i < names.size(); ++i)
    {
        aovBindings[i].aovName = names[i];
        aovBindings[i].clearValue = outputDescs[i].clearValue;
        aovBindings[i].renderBufferId = aovPath(names[i]);
        aovBindings[i].aovSettings = outputDescs[i].aovSettings;
    }

    renderParams.aovBindings = aovBindings;
    myTaskDelegate.SetParameter(myRenderTaskId, HdTokens->params, renderParams);
    GetRenderIndex()->GetChangeTracker().MarkTaskDirty(
        myRenderTaskId, HdChangeTracker::DirtyParams);
}


HdRenderBuffer *
XUSD_HuskTaskManager::GetRenderOutput(const TfToken &name)
{
    SdfPath renderBufferId = aovPath(name);
    return static_cast<HdRenderBuffer*>(
        GetRenderIndex()->GetBprim(HdPrimTypeTokens->renderBuffer,
            renderBufferId));
}

void
XUSD_HuskTaskManager::SetCollection(const HdRprimCollection &collection)
{
    // XXX For now we assume the application calling to set a new
    //     collection does not know or setup the material tags and does not
    //     split up the collection according to material tags.
    //     In order to ignore materialTags when comparing collections we need
    //     to copy the old tag into the new collection. Since the provided
    //     collection is const, we need to make a not-ideal copy.
    HdRprimCollection newCollection = collection;
    HdRprimCollection oldCollection =
        myTaskDelegate.GetParameter<HdRprimCollection>(
            myRenderTaskId, HdTokens->collection);

    const TfToken &oldMaterialTag = oldCollection.GetMaterialTag();
    newCollection.SetMaterialTag(oldMaterialTag);

    if (oldCollection != newCollection)
    {
        myTaskDelegate.SetParameter(myRenderTaskId, HdTokens->collection,
                               newCollection);
        GetRenderIndex()->GetChangeTracker().MarkTaskDirty(
            myRenderTaskId, HdChangeTracker::DirtyCollection);
    }
}

void
XUSD_HuskTaskManager::SetRenderTags(const TfTokenVector &renderTags)
{
    HdChangeTracker &tracker = GetRenderIndex()->GetChangeTracker();

    if (myTaskDelegate.GetTaskRenderTags(myRenderTaskId) != renderTags)
    {
        myTaskDelegate.SetParameter(myRenderTaskId,
                               HdTokens->renderTags,
                               renderTags);
        tracker.MarkTaskDirty(myRenderTaskId,
                              HdChangeTracker::DirtyRenderTags);
    }
}

void
XUSD_HuskTaskManager::SetRenderViewport(const GfVec4d &viewport)
{
    bool viewportChanged = false;

    XUSD_HuskRenderTaskParams params =
        myTaskDelegate.GetParameter<XUSD_HuskRenderTaskParams>(
            myRenderTaskId, HdTokens->params);

    if (params.viewport != viewport)
    {
        viewportChanged = true;
        params.viewport = viewport;
        myTaskDelegate.SetParameter(myRenderTaskId, HdTokens->params, params);
        GetRenderIndex()->GetChangeTracker().MarkTaskDirty(
            myRenderTaskId, HdChangeTracker::DirtyParams);
    }

    if (!viewportChanged)
        return;

    // Update all the render buffer sizes as well.
    GfVec3i dimensions = GfVec3i(viewport[2], viewport[3], 1);
    for (const auto &id : myAOVPaths)
    {
        HdRenderBufferDescriptor desc =
            myTaskDelegate.GetParameter<HdRenderBufferDescriptor>(id,
                HusdHuskTokens->renderBufferDescriptor);
        if (desc.dimensions != dimensions)
	{
            desc.dimensions = dimensions;
            myTaskDelegate.SetParameter(id, HusdHuskTokens->renderBufferDescriptor, desc);
            GetRenderIndex()->GetChangeTracker().MarkBprimDirty(id,
                HdRenderBuffer::DirtyDescription);
        }
    }
}

bool
XUSD_HuskTaskManager::IsConverged() const
{
    for (auto const &task : GetRenderingTasks())
    {
        auto ptask = dynamic_cast<const XUSD_HuskRenderTask *>(task.get());
        if (ptask && !ptask->IsConverged())
	    return false;
    }

    return true;
}

PXR_NAMESPACE_CLOSE_SCOPE
