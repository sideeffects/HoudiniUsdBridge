/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
*/

#include "HUSD_Skeleton.h"

#include "HUSD_ErrorScope.h"
#include "HUSD_TimeCode.h"
#include "XUSD_Data.h"
#include "XUSD_Utils.h"

#include <GA/GA_Names.h>
#include <GEO/GEO_PolyCounts.h>
#include <GEO/GEO_PrimPoly.h>
#include <GEO/GEO_PrimPolySoup.h>
#include <GT/GT_RefineParms.h>
#include <GU/GU_AttributeSwap.h>
#include <GU/GU_Detail.h>
#include <gusd/USD_Utils.h>
#include <gusd/GU_USD.h>
#include <gusd/UT_Gf.h>
#include <gusd/agentUtils.h>
#include <pxr/usd/usdSkel/cache.h>
#include <pxr/usd/usdSkel/root.h>
#include <pxr/usd/usdSkel/skeletonQuery.h>
#include <pxr/usd/usdSkel/utils.h>

PXR_NAMESPACE_USING_DIRECTIVE

static bool
husdFindSkelBindings(const HUSD_AutoReadLock &readlock,
                     const UT_StringRef &skelrootpath,
                     UsdSkelCache &skelcache,
                     std::vector<UsdSkelBinding> &bindings)
{
    XUSD_ConstDataPtr data(readlock.data());

    if (!data || !data->isStageValid())
    {
        HUSD_ErrorScope::addError(HUSD_ERR_STRING, "Invalid stage.");
        return false;
    }

    SdfPath sdfpath = HUSDgetSdfPath(skelrootpath);
    UsdPrim prim(data->stage()->GetPrimAtPath(sdfpath));
    if (!prim)
    {
        HUSD_ErrorScope::addError(HUSD_ERR_CANT_FIND_PRIM,
                                  skelrootpath.c_str());
        return false;
    }

    UsdSkelRoot skelroot(prim);
    if (!skelroot)
    {
        HUSD_ErrorScope::addError(HUSD_ERR_STRING,
                                  "Primitive is not a SkelRoot.");
        return false;
    }

    skelcache.Populate(skelroot);

    bindings.clear();
    skelcache.ComputeSkelBindings(skelroot, &bindings);

    if (bindings.empty())
    {
        HUSD_ErrorScope::addError(HUSD_ERR_STRING,
                                  "Could not find any skeleton bindings.");
        return false;
    }

    return true;
}

bool
HUSDimportSkinnedGeometry(GU_Detail &gdp, const HUSD_AutoReadLock &readlock,
                          const UT_StringRef &skelrootpath,
                          const UT_StringHolder &shapeattrib)
{
    UsdSkelCache skelcache;
    std::vector<UsdSkelBinding> bindings;
    if (!husdFindSkelBindings(readlock, skelrootpath, skelcache, bindings))
        return false;

    GT_RefineParms refine_parms;
    refine_parms.set(GUSD_REFINE_ADDXFORMATTRIB, false);

    // Skip creating the usdpath attribute, which is random for stages from
    // LOPs. This could be revisited if importing directly from a file is
    // allowed.
    refine_parms.set(GUSD_REFINE_ADDPATHATTRIB, false);

    for (const UsdSkelBinding &binding : bindings)
    {
        if (!GusdCoalesceAgentShapes(
                gdp, binding, UsdTimeCode::EarliestTime(), /*lod*/ nullptr,
                GusdPurposeSet(GUSD_PURPOSE_DEFAULT | GUSD_PURPOSE_PROXY),
                UT_ERROR_WARNING, &refine_parms))
        {
            HUSD_ErrorScope::addError(HUSD_ERR_STRING,
                                      "Failed to load shapes.");
            return false;
        }
    }

    // Convert to polysoups for reduced memory usage.
    GEO_PolySoupParms psoup_parms;
    gdp.polySoup(psoup_parms, &gdp);

    // Set up the shape name attribute, computed using the paths relative to
    // the SkelRoot prim.
    if (!GU_AttributeSwap::swapAttribute(&gdp, GU_AttributeSwap::METHOD_COPY,
                                         GU_AttributeSwap::TYPEINFO_USE_SOURCE,
                                         GA_ATTRIB_PRIMITIVE,
                                         GUSD_PRIMPATH_ATTR, shapeattrib))
    {
        HUSD_ErrorScope::addError(HUSD_ERR_STRING,
                                  "Failed to create shape name attribute.");
        return false;
    }

    GA_RWHandleS shapeattrib_h(
        gdp.findStringTuple(GA_ATTRIB_PRIMITIVE, shapeattrib, 1));
    UT_ASSERT(shapeattrib_h.isValid());

    UT_StringArray strings;
    UT_IntArray handles;
    shapeattrib_h->extractStrings(strings, handles);

    const SdfPath root_path = HUSDgetSdfPath(skelrootpath);
    for (exint i = 0, n = strings.size(); i < n; ++i)
    {
        SdfPath prim_path = HUSDgetSdfPath(strings[i]);
        prim_path.MakeRelativePath(root_path);

        shapeattrib_h->replaceString(
            GA_StringIndexType(handles[i]),
            prim_path.MakeRelativePath(root_path).GetString());
    }

    // Bump all data ids since we've created new geometry.
    gdp.bumpAllDataIds();

    return true;
}

bool
HUSDimportSkeleton(GU_Detail &gdp, const HUSD_AutoReadLock &readlock,
                   const UT_StringRef &skelrootpath)
{
    UsdSkelCache skelcache;
    std::vector<UsdSkelBinding> bindings;
    if (!husdFindSkelBindings(readlock, skelrootpath, skelcache, bindings))
        return false;

    GA_RWHandleS name_attrib =
        gdp.addStringTuple(GA_ATTRIB_POINT, GA_Names::name, 1);

    GA_RWHandleM3D xform_attrib =
        gdp.addFloatTuple(GA_ATTRIB_POINT, GA_Names::transform, 9);
    xform_attrib->setTypeInfo(GA_TypeInfo::GA_TYPE_TRANSFORM);

    // TODO - create attributes to identify the source path of the skeleton.

    for (const UsdSkelBinding &binding : bindings)
    {
        const UsdSkelSkeleton &skel = binding.GetSkeleton();
        UsdSkelSkeletonQuery skelquery = skelcache.GetSkelQuery(skel);
        if (!skelquery.IsValid())
        {
            HUSD_ErrorScope::addError(HUSD_ERR_STRING,
                                      "Invalid skeleton query.");
            return false;
        }

        const UsdSkelTopology &topology = skelquery.GetTopology();

        VtTokenArray joints;
        if (!skel.GetJointsAttr().Get(&joints))
        {
            HUSD_ErrorScope::addError(HUSD_ERR_STRING,
                                      "'joints' attribute is invalid.");
            return false;
        }

        // Prefer the jointNames attribute if it was authored, since it
        // provides nicer unique names than the full paths.
        VtTokenArray joint_names;
        if (skel.GetJointNamesAttr().Get(&joint_names))
        {
            if (joint_names.size() != joints.size())
            {
                HUSD_ErrorScope::addError(
                    HUSD_ERR_STRING, "'jointNames' attribute does not match "
                                     "the size of the 'joints' attribute.");
                return false;
            }
        }
        else
            joint_names = joints;

        // Create a point for each joint, and connect each point to its parent
        // with a polygon.
        GA_Offset start_ptoff = gdp.appendPointBlock(topology.GetNumJoints());
        UT_Array<int> poly_ptnums;
        for (exint i = 0, n = topology.GetNumJoints(); i < n; ++i)
        {
            GA_Offset ptoff = start_ptoff + i;
            name_attrib.set(ptoff,
                            GusdUSD_Utils::TokenToStringHolder(joint_names[i]));

            if (!topology.IsRoot(i))
            {
                int parent = topology.GetParent(i);
                poly_ptnums.append(parent);
                poly_ptnums.append(i);
            }
        }

        GEO_PolyCounts poly_sizes;
        poly_sizes.append(2, poly_ptnums.size() / 2);
        GEO_PrimPoly::buildBlock(&gdp, start_ptoff, topology.GetNumJoints(),
                                 poly_sizes, poly_ptnums.data(),
                                 /* closed */ false);
    }

    // Bump all data ids since new geometry was generated.
    gdp.bumpAllDataIds();

    return true;
}

static bool
husdComputeWorldTransforms(const UsdSkelSkeleton &skel,
                           const UsdSkelTopology &topology,
                           const UsdTimeCode &timecode,
                           const VtMatrix4dArray &local_xforms,
                           VtMatrix4dArray &world_xforms)
{
    const GfMatrix4d root_xform = skel.ComputeLocalToWorldTransform(timecode);

    world_xforms.resize(local_xforms.size());
    if (!UsdSkelConcatJointTransforms(topology, local_xforms, world_xforms,
                                      &root_xform))
    {
        HUSD_ErrorScope::addError(HUSD_ERR_STRING,
                                  "Failed to compute world transforms.");
        return false;
    }

    return true;
}

bool
HUSDimportSkeletonPose(GU_Detail &gdp, const HUSD_AutoReadLock &readlock,
                       const UT_StringRef &skelrootpath,
                       HUSD_SkeletonPoseType pose_type, fpreal time)
{
    UsdSkelCache skelcache;
    std::vector<UsdSkelBinding> bindings;
    if (!husdFindSkelBindings(readlock, skelrootpath, skelcache, bindings))
        return false;

    GA_RWHandleM3D xform_attrib =
        gdp.findFloatTuple(GA_ATTRIB_POINT, GA_Names::transform, 9);
    UT_ASSERT(xform_attrib.isValid());

    GA_Index ptidx = 0;
    for (const UsdSkelBinding &binding : bindings)
    {
        const UsdSkelSkeleton &skel = binding.GetSkeleton();
        UsdSkelSkeletonQuery skelquery = skelcache.GetSkelQuery(skel);
        if (!skelquery.IsValid())
        {
            HUSD_ErrorScope::addError(HUSD_ERR_STRING,
                                      "Invalid skeleton query.");
            return false;
        }

        const UsdSkelTopology &topology = skelquery.GetTopology();

        VtMatrix4dArray world_xforms;
        switch (pose_type)
        {
        case HUSD_SkeletonPoseType::Animation:
        {
            const UsdSkelAnimQuery &animquery = skelquery.GetAnimQuery();
            if (!animquery.IsValid())
            {
                HUSD_ErrorScope::addError(HUSD_ERR_STRING,
                                          "Invalid animation query.");
                return false;
            }

            VtMatrix4dArray local_xforms;
            const UsdTimeCode timecode =
                HUSDgetUsdTimeCode(HUSD_TimeCode(time, HUSD_TimeCode::TIME));
            if (!animquery.ComputeJointLocalTransforms(&local_xforms, timecode))
            {
                HUSD_ErrorScope::addError(
                    HUSD_ERR_STRING, "Failed to compute local transforms.");
                return false;
            }

            if (!husdComputeWorldTransforms(skel, topology, timecode,
                                            local_xforms, world_xforms))
            {
                return false;
            }

            // TODO - output time range detail attribute.
        }
        break;

        case HUSD_SkeletonPoseType::BindPose:
        {
            if (!skel.GetBindTransformsAttr().Get(&world_xforms))
            {
                HUSD_ErrorScope::addError(
                    HUSD_ERR_STRING, "'bindTransforms' attribute is invalid");
                return false;
            }
            else if (world_xforms.size() != topology.GetNumJoints())
            {
                HUSD_ErrorScope::addError(
                    HUSD_ERR_STRING,
                    "'bindTransforms' attribute does not match "
                    "the size of the 'joints' attribute.");
                return false;
            }
        }
        break;

        case HUSD_SkeletonPoseType::RestPose:
        {
            VtMatrix4dArray local_xforms;
            if (!skel.GetRestTransformsAttr().Get(&local_xforms))
            {
                HUSD_ErrorScope::addError(
                    HUSD_ERR_STRING, "'restTransforms' attribute is invalid");
                return false;
            }
            else if (local_xforms.size() != topology.GetNumJoints())
            {
                HUSD_ErrorScope::addError(
                    HUSD_ERR_STRING,
                    "'restTransforms' attribute does not match "
                    "the size of the 'joints' attribute.");
                return false;
            }

            const UsdTimeCode timecode =
                HUSDgetUsdTimeCode(HUSD_TimeCode(time, HUSD_TimeCode::TIME));
            if (!husdComputeWorldTransforms(skel, topology, timecode,
                                            local_xforms, world_xforms))
            {
                return false;
            }
        }
        break;
        }

        UT_ASSERT(ptidx + topology.GetNumJoints() <= gdp.getNumPoints());
        UT_ASSERT(world_xforms.size() == topology.GetNumJoints());
        for (exint i = 0, n = topology.GetNumJoints(); i < n; ++i, ++ptidx)
        {
            GA_Offset ptoff = gdp.pointOffset(ptidx);

            const UT_Matrix4D &xform = GusdUT_Gf::Cast(world_xforms[i]);
            xform_attrib.set(ptoff, UT_Matrix3D(xform));

            UT_Vector3D t;
            xform.getTranslates(t);
            gdp.setPos3(ptoff, t);
        }
    }

    gdp.getP()->bumpDataId();
    xform_attrib.bumpDataId();

    return true;
}
