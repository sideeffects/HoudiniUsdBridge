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
 *      Side Effects Software Inc.
 *      123 Front Street West, Suite 1401
 *      Toronto, Ontario
 *      Canada   M5J 2M2
 *      416-504-9876
 *
 */

#ifndef HDKARMA_INSTANCER_H
#define HDKARMA_INSTANCER_H

#include <pxr/pxr.h>

#include <mutex>
#include <GT/GT_Primitive.h>
#include <UT/UT_Lock.h>
#include <UT/UT_Map.h>
#include <BRAY/BRAY_Interface.h>
#include <HUSD/XUSD_HydraInstancer.h>

PXR_NAMESPACE_OPEN_SCOPE

class BRAY_HdParam;

/// @class BRAY_HdInstancer
///
/// HdKarma implements instancing by adding prototype geometry to the BVH
/// multiple times within HdKarmaMesh::Sync(). The only instance-varying
/// attribute that HdKarma supports is transform, so the natural
/// accessor to instancer data is ComputeInstanceTransforms(),
/// which returns a list of transforms to apply to the given prototype
/// (one instance per transform).
///
/// Nested instancing can be handled by recursion, and by taking the
/// cartesian product of the transform arrays at each nesting level, to
/// create a flattened transform array.
///
class BRAY_HdInstancer : public XUSD_HydraInstancer
{
public:
    /// Constructor.

    ///   \param delegate The scene delegate backing this instancer's data.
    ///   \param id The unique id of this instancer.
    ///   \param parentInstancerId The unique id of the parent instancer,
    ///                            or an empty id if not applicable.
    BRAY_HdInstancer(HdSceneDelegate* delegate, SdfPath const& id);

    /// Destructor.
    ~BRAY_HdInstancer() override;

    /// Karma-specific extensions to primvar gathering code.
    void        syncPrimvars(HdSceneDelegate* delegate,
                        HdRenderParam* renderParam,
                        HdDirtyBits* dirtyBits,
                        int nsegs) override;

    /// Computes all instance transforms for the provided prototype id,
    /// taking into account the scene delegate's instancerTransform and the
    /// instance primvars "instanceTransform", "translate", "rotate", "scale".
    /// Computes and flattens nested transforms, if necessary.
    ///   \param prototypeId The prototype to compute transforms for.
    ///   \return One transform per instance, to apply when drawing.
    void	NestedInstances(BRAY_HdParam &rparm,
			BRAY::ScenePtr &scene,
			SdfPath const &prototypeId,
			const BRAY::ObjectPtr &protoObj,
			const UT_Array<GfMatrix4d> &protoXform,
                        const BRAY::OptionSet &props);

    void	applyNesting(BRAY_HdParam &rparm, BRAY::ScenePtr &scene);

    /// Called when render delegate destroys instancer. Removes instancer(s)
    /// from BRAY scenegraph.
    void	eraseFromScenegraph(BRAY::ScenePtr &scene);

    /// Returns nested level. For example, if this instancer does not have
    /// parent (ie root level) it will return 0. Also, if BRAY::Scene does not
    /// support nested instancing it will return 0.
    int		getNestLevel() const { return myNestLevel; }

private:
    enum class MotionBlurStyle : uint8
    {
        NONE,
        VELOCITY,
        ACCEL,
        DEFORM,
    };
    // Set my blur member data
    void        loadBlur(const BRAY_HdParam &rparm,
                        HdSceneDelegate *sd,
                        const SdfPath &id);

    // Return the attributes for the given prototype
    GT_AttributeListHandle attributesForPrototype(const SdfPath &protoId) const
    {
        return extractListForPrototype(protoId, myAttributes);
    }
    GT_AttributeListHandle extractListForPrototype(const SdfPath &protoId,
                                    const GT_AttributeListHandle &atrs) const;

    // Returns array of instance ids for given prototype. Returns empty array
    // if the ids are contiguous.
    UT_Array<exint>	instanceIdsForPrototype(const SdfPath &protoId);

    BRAY::ObjectPtr	&findOrCreate(const SdfPath &path);

    void	applyNestedInstance(BRAY::ScenePtr &scene,
			SdfPath const &prototypeId,
			const BRAY::ObjectPtr &protoObj,
			const UT_Array<GfMatrix4d> &protoXform);


    UT_Map<SdfPath, BRAY::ObjectPtr>	myInstanceMap;
    BRAY::ObjectPtr			mySceneGraph;
    GT_AttributeListHandle		myAttributes;
    VtValue                             myVelocities;
    VtValue                             myAccelerations;
    int					myNestLevel;
    int                                 mySegments;
    MotionBlurStyle                     myMotionBlur;
    bool				myNewObject;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif  // HDKARMA_INSTANCER_H
