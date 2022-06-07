/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
 *
 * NAME:	XUSD_HuskTaskManager.h (karma, C++)
 *
 * COMMENTS:
 */

#ifndef __XUSD_HuskTaskManager__
#define __XUSD_HuskTaskManager__

#include <pxr/pxr.h>

#include <pxr/imaging/hd/aov.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/imaging/hd/task.h>

#include <pxr/imaging/cameraUtil/conformWindow.h>

#include <pxr/imaging/glf/simpleLightingContext.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/base/tf/staticTokens.h>
#include <pxr/base/gf/matrix4d.h>

PXR_NAMESPACE_OPEN_SCOPE

class HdRenderBuffer;

/// Replacement for HdxTaskController
class XUSD_HuskTaskManager
{
public:
    XUSD_HuskTaskManager(HdRenderIndex *renderIndex,
	    const SdfPath &controllerId,
	    const SdfPath &cameraId);
    ~XUSD_HuskTaskManager();

    // Set camera
    void        setCamera(const SdfPath &cameraId);

    /// -------------------------------------------------------
    /// Execution API

    /// Obtain the set of tasks managed by the task controller,
    /// for image generation. The tasks returned will be different
    /// based on current renderer state.
    const HdTaskSharedPtrVector	GetRenderingTasks() const;

    /// -------------------------------------------------------
    /// Rendering API

    /// Set the collection to be rendered.
    void	SetCollection(const HdRprimCollection &collection);

    /// Set the "view" opinion of the scenes render tags.
    /// The opinion is the base opinion for the entire scene.
    /// Individual tasks (such as the shadow task) may
    /// have a stronger opinion and override this opinion
    void	SetRenderTags(const TfTokenVector &renderTags);

    /// -------------------------------------------------------
    /// AOV API

    /// Set the list of outputs to be rendered. If outputs.size() == 1,
    /// this will send that output to the viewport via a colorizer task.
    /// Note: names should come from HdAovTokens.
    void	SetRenderOutputs(const TfTokenVector &names,
			    const HdAovDescriptorList &descs);

    /// Get the buffer for a rendered output. Note: the caller should call
    /// Resolve(), as XUSD_HuskTaskManager doesn't guarantee the buffer will
    /// be resolved.
    HdRenderBuffer	*GetRenderOutput(const TfToken &name);

    /// Set the viewport param on tasks.
    void	SetRenderViewport(const GfVec4d &viewport);

    /// Return whether the image has converged.
    bool	IsConverged() const;

private:
    /// Return the controller's scene-graph id (prefixed to any
    /// scene graph objects it creates).
    const SdfPath	&GetControllerId() const { return myControllerId; }

    /// Return the render index this controller is bound to.
    HdRenderIndex	*GetRenderIndex() { return myIndex; }
    const HdRenderIndex	*GetRenderIndex() const { return myIndex; }


    ///
    /// This class is not intended to be copied.
    ///
    XUSD_HuskTaskManager(const XUSD_HuskTaskManager &) = delete;
    XUSD_HuskTaskManager &operator=(const XUSD_HuskTaskManager &) = delete;

    // Create taskController objects.
    SdfPath	createRenderTask();

    // Helper function for renderbuffer management.
    SdfPath	aovPath(const TfToken &aov) const;

    // A private scene delegate member variable backs the tasks this controller
    // generates. To keep ka_Delegate simple, the containing class is
    // responsible for marking things dirty.
    class ka_Delegate : public HdSceneDelegate
    {
    public:
        ka_Delegate(HdRenderIndex *parentIndex, const SdfPath &delegateID)
            : HdSceneDelegate(parentIndex, delegateID)
	{
	}
        ~ka_Delegate() override = default;

        // XUSD_HuskTaskManager set/get interface
        template <typename T>
        void SetParameter(const SdfPath &id, const TfToken &key, const T &value)
	{
            myValueCacheMap[id][key] = value;
        }
        template <typename T>
        const T &GetParameter(const SdfPath &id, const TfToken &key) const
	{
            VtValue vParams;
            ka_ValueCache vCache;
            TF_VERIFY(
                TfMapLookup(myValueCacheMap, id, &vCache)
                && TfMapLookup(vCache, key, &vParams)
                && vParams.IsHolding<T>());
            return vParams.Get<T>();
        }
        bool HasParameter(const SdfPath &id, const TfToken &key) const
	{
            ka_ValueCache vCache;
            if (TfMapLookup(myValueCacheMap, id, &vCache)
                && vCache.count(key) > 0)
	    {
                return true;
            }
            return false;
        }

        // HdSceneDelegate interface
        VtValue Get(const SdfPath &id, const TfToken &key) override;
        VtValue GetCameraParamValue(const SdfPath &id,
                                    const TfToken &key) override;
        HdRenderBufferDescriptor
            GetRenderBufferDescriptor(const SdfPath &id) override;
        TfTokenVector GetTaskRenderTags(const SdfPath &taskId) override;


    private:
        using ka_ValueCache = TfHashMap<TfToken, VtValue, TfToken::HashFunctor>;
        using ka_ValueCacheMap = TfHashMap<SdfPath, ka_ValueCache, SdfPath::Hash>;
        ka_ValueCacheMap myValueCacheMap;
    };

    HdRenderIndex       *myIndex;
    ka_Delegate          myTaskDelegate;
    const SdfPath        myControllerId;
    SdfPath              myCameraId;
    SdfPath              myRenderTaskId;
    SdfPathVector        myAOVPaths;
    TfTokenVector        myAOVNames;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDX_TASK_CONTROLLER_H
