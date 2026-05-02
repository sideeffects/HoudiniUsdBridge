/*
 * Copyright 2023 Side Effects Software Inc.
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
*/

#ifndef __HUSD_CrowdProcedural_h__
#define __HUSD_CrowdProcedural_h__

#include "HUSD_API.h"

#include <SYS/SYS_Types.h>
#include <UT/UT_VectorTypes.h>

class HUSD_AutoWriteLock;
class HUSD_Path;
class HUSD_PathSet;
class HUSD_TimeCode;

/// For instanced SkelRoot prims which are at a low LOD and have a similar pose
/// to another agent, convert these to instances of the exemplar agent's
/// deformed geometry.
HUSD_API bool HUSDapplyCrowdProcedural(
        HUSD_AutoWriteLock &lock,
        const HUSD_PathSet &prim_paths,
        const HUSD_Path &camera_path,
        const UT_Vector2i &resolution,
        const HUSD_TimeCode &time_sample,
        fpreal lod_threshold,
        fpreal offscreen_quality,
        bool optimize_identical_poses,
        bool bake_prototype_agents,
        bool bake_all_agents,
        const HUSD_Path &prototype_material,
        const HUSD_Path &instance_material,
        const HUSD_Path &default_material);

#endif
