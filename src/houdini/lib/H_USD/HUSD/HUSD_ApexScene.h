/*
 * Copyright 2025 Side Effects Software Inc.
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

#ifndef __HUSD_ApexScene_h__
#define __HUSD_ApexScene_h__

#include "HUSD_API.h"

#include "HUSD_Path.h"

#include <SYS/SYS_Types.h>
#include <UT/UT_Array.h>
#include <UT/UT_NonCopyable.h>
#include <UT/UT_Set.h>
#include <UT/UT_StringHolder.h>
#include <UT/UT_StringMap.h>
#include <UT/UT_UniquePtr.h>

class APEXA_SceneInvoke;
class GU_ConstDetailHandle;
class GU_DetailHandle;
class HUSD_AutoAnyLock;
class HUSD_AutoWriteLock;
class HUSD_FindPrims;

enum class HUSD_ApexShapeType : uint8;

/// Stores an APEX scene loaded from USD (a HoudiniApexScene prim), along with
/// information about the USD primitives bound to APEX scene outputs.
/// This can be safely cached between LOP cooks to avoid rebuilding APEX scenes
/// unless the input stage has changed.
class HUSD_API HUSD_ApexScene
{
public:
    HUSD_ApexScene();
    ~HUSD_ApexScene();

    UT_NON_COPYABLE(HUSD_ApexScene)

    /// Options for how to convert USD prims to geometry for APEX rig inputs. 
    struct HUSD_API GeoImportOptions
    {
        /// If non-empty, adds a prim attribute with the source USD prim's path.
        UT_StringHolder myPathAttrib;
    };

    /// Assembles APEX scenes from the specified list of HoudiniApexScene
    /// prims.
    /// This performs validation checks to ensure that e.g. a prim isn't driven
    /// by the output of more than one scene.
    static bool loadScenes(
            const HUSD_AutoAnyLock &read_lock,
            const HUSD_FindPrims &find_prims,
            const GeoImportOptions &import_options,
            UT_Array<HUSD_ApexScene> &scenes);

    /// Assemble the input geometry for an APEX scene from the specified
    /// HoudiniApexScene prim.
    static GU_DetailHandle loadSceneGeometry(
            const HUSD_AutoAnyLock &read_lock,
            const HUSD_Path &scene_path,
            const GeoImportOptions &import_options);

    /// Evaluates the scene at the specified list of samples, and updates any
    /// prims bound to a scene output.
    bool evaluateOutputs(
            HUSD_AutoWriteLock &write_lock,
            const UT_SortedSet<fpreal> &samples,
            const UT_StringRef &xform_description,
            bool use_xform_common_api);

    struct ShapeOutputInfo
    {
        exint mySceneOutputIdx = -1;
        HUSD_Path myPrimPath;
        HUSD_ApexShapeType myShapeType;
    };

    struct XformOutputInfo
    {
        exint mySceneOutputIdx = -1;
        HUSD_Path myPrimPath;
        UT_StringHolder myJointName;
    };

private:
    /// Loads the APEX scene from packed folders.
    bool loadFromGeometry(const GU_ConstDetailHandle &scene_gdh);

    UT_UniquePtr<APEXA_SceneInvoke> myScene;
    UT_Array<ShapeOutputInfo> myShapeOutputs;
    UT_Array<XformOutputInfo> myXformOutputs;

    /// Map from xforming prim paths to their unique index from 0-N.
    UT_Map<HUSD_Path, exint> myXformingPrimIndex;
    /// Index of the closest parent xforming prim, or -1.
    UT_Array<exint> myXformingPrimParents;
};

#endif
