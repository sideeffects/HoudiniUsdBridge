/*
 * Copyright 2026 Side Effects Software Inc.
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

#pragma once

#include <APEX/APEX_COW.h>
#include <GU/GU_DetailHandle.h>
#include <SYS/SYS_Types.h>
#include <UT/UT_Array.h>
#include <UT/UT_ConcurrentHashMap.h>
#include <UT/UT_IntrusivePtr.h>
#include <UT/UT_Lock.h>
#include <UT/UT_NonCopyable.h>
#include <UT/UT_StringHolder.h>
#include <UT/UT_StringMap.h>
#include <UT/UT_TaskExclusive.h>
#include <UT/UT_UniquePtr.h>

class APEXA_SceneInvoke;

/// Utility for evaluating outputs from an APEX scene at varying times.
/// This also can accept overrides to the output geometry when the Animate state
/// is running.
class HUSD_HydraApexSceneEvaluator
    : public UT_IntrusiveRefCounter<HUSD_HydraApexSceneEvaluator>
{
public:
    HUSD_HydraApexSceneEvaluator();
    ~HUSD_HydraApexSceneEvaluator();

    UT_NON_COPYABLE(HUSD_HydraApexSceneEvaluator)

    /// Update the APEX scene geometry.
    void setSceneGeometry(const GU_ConstDetailHandle &scene_gdh);

    /// Returns the geometry that the APEX scene will be loaded from.
    const GU_ConstDetailHandle &getSceneGeometry() const { return mySceneGeo; }

    /// Register an APEX output for evaluation.
    exint registerOutput(
            const UT_StringHolder &path,
            const UT_StringHolder &key);

    /// Update the current scene frame which will be used for evaluation.
    void setCurrentFrame(fpreal frame);

    /// Returns the current scene frame.
    fpreal getCurrentFrame() const { return myCurrentFrame; }

    /// Override the output geometries (used while in the Animate state).
    /// While active, evaluating at non-zero shutter offsets will not work.
    void setOutputOverrides(
            const UT_StringMap<GU_ConstDetailHandle> &overrides);

    /// Evaluate the geometry for the given output at the current scene frame
    /// (plus the shutter offset).
    GU_ConstDetailHandle getGeometry(exint output_idx,
                                     fpreal shutter_offset) const;

private:
    void ensureSceneLoaded() const;
    void evaluateOutputsForSample(
            fpreal shutter_offset,
            UT_Array<apex::ApexGeometry> &evaluated_outputs) const;

    /// Records information about the outputs we are interested in evaluating
    /// from the APEX scene.
    struct OutputInfo
    {
        UT_StringHolder myPath;
        UT_StringHolder myKey;
    };

    GU_ConstDetailHandle mySceneGeo;
    fpreal myCurrentFrame = 1.0;

    UT_Array<OutputInfo> myOutputInfos;
    UT_StringMap<exint> myPathToOutputMap;
    UT_Array<GU_ConstDetailHandle> myViewerStateOverrides;

    mutable UT_Lock mySceneLock;
    mutable UT_UniquePtr<APEXA_SceneInvoke> myScene;
    mutable bool mySceneValid = false;

    /// The scene can be evaluated at different shutter offsets for motion blur.
    /// This contains the evaluated outputs for a single shutter offset,
    /// protected by a task exclusive since multiple threads may be requesting
    /// evaluation of this time sample to read different outputs (e.g. when
    /// there are multiple shapes / characters).
    struct EvaluatedSample
    {
    public:
        EvaluatedSample(
                const HUSD_HydraApexSceneEvaluator &owner,
                fpreal sample_frame)
            : myEvaluator(owner), mySampleFrame(sample_frame)
        {
        }

        /// Returns the evaluated geometry for the specified output. This method
        /// is safe to call from multiple threads.
        GU_ConstDetailHandle getGeometry(exint output_idx);

        /// Called by UT_TaskExclusive to evaluate the outputs.
        void operator()();

    private:
        const HUSD_HydraApexSceneEvaluator &myEvaluator;
        fpreal mySampleFrame = 0;

        UT_TaskExclusive<EvaluatedSample> myExclusive;
        UT_Array<apex::ApexGeometry> myOutputs;
    };

    /// Map from shutter offset to the evaluated outputs for the time sample.
    using EvaluatedSampleMap
            = UT_ConcurrentHashMap<fpreal, UT_UniquePtr<EvaluatedSample>>;
    mutable EvaluatedSampleMap myEvaluatedSamples;
};

using HUSD_HydraApexSceneEvaluatorConstPtr
        = UT_IntrusivePtr<const HUSD_HydraApexSceneEvaluator>;
