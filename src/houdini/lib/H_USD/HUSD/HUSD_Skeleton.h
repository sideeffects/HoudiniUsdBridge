/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
*/

#ifndef __HUSD_Skeleton_h__
#define __HUSD_Skeleton_h__

#include "HUSD_API.h"

#include <SYS/SYS_Types.h>

class GU_Detail;
class HUSD_AutoReadLock;
class UT_StringHolder;
class UT_StringRef;

/// Imports all skinnable primitives underneath the provided SkelRoot prim.
HUSD_API bool
HUSDimportSkinnedGeometry(GU_Detail &gdp, const HUSD_AutoReadLock &readlock,
                          const UT_StringRef &skelrootpath,
                          const UT_StringHolder &shapeattrib);

enum class HUSD_SkeletonPoseType
{
    Animation,
    BindPose,
    RestPose
};

/// Imports all Skeleton primitives underneath the provided SkelRoot prim.
/// A point is created for each joint, and joints are connected to their
/// parents by polyline primitives.
/// Use HUSDimportSkeletonPose() to set the skeleton's transforms. The pose
/// type is only used in this method to initialize attributes that aren't
/// time-varying.
HUSD_API bool
HUSDimportSkeleton(GU_Detail &gdp, const HUSD_AutoReadLock &readlock,
                   const UT_StringRef &skelrootpath,
                   HUSD_SkeletonPoseType pose_type);

/// Updates the pose for the skeleton geometry created by HUSDimportSkeleton().
HUSD_API bool
HUSDimportSkeletonPose(GU_Detail &gdp, const HUSD_AutoReadLock &readlock,
                       const UT_StringRef &skelrootpath,
                       HUSD_SkeletonPoseType pose_type, fpreal time);

#endif
