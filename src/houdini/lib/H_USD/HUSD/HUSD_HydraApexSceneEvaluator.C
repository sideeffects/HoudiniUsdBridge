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

#include "HUSD_HydraApexSceneEvaluator.h"

#include <APEXA/APEXA_SceneInvoke.h>
#include <GU/GU_DetailHandle.h>
#include <UT/UT_StringHolder.h>
#include <UT/UT_Tracing.h>

#include <pxr/pxr.h>
#include <pxr/base/tf/diagnostic.h>

PXR_NAMESPACE_USING_DIRECTIVE

HUSD_HydraApexSceneEvaluator::HUSD_HydraApexSceneEvaluator() = default;

HUSD_HydraApexSceneEvaluator::~HUSD_HydraApexSceneEvaluator() = default;

void
HUSD_HydraApexSceneEvaluator::setSceneGeometry(
        const GU_ConstDetailHandle &scene_gdh)
{
    UT_ASSERT(scene_gdh.isValid());

    mySceneGeo = scene_gdh;
    myEvaluatedSamples.clear();
    mySceneValid = false;

    myScene = UTmakeUnique<APEXA_SceneInvoke>();
    // Enable animation caching by default for improved performance when viewing
    // animation outside the Animate state.
    myScene->setAnimationCachingMode(APEXA_AnimationCachingMode::Minimal);
}

exint
HUSD_HydraApexSceneEvaluator::registerOutput(
        const UT_StringHolder &path,
        const UT_StringHolder &key)
{
    // This shouldn't be called after we've started evaluating other outputs.
    UT_ASSERT(myEvaluatedSamples.empty());
    UT_ASSERT(!mySceneValid);

    const exint output_idx = myOutputInfos.append({path, key});

    UT_StringHolder full_path;
    full_path.format("{0}/{1}", path, key);
    myPathToOutputMap.emplace(full_path, output_idx);

    return output_idx;
}

void
HUSD_HydraApexSceneEvaluator::setCurrentFrame(fpreal frame)
{
    myCurrentFrame = frame;
    myEvaluatedSamples.clear();
}

void
HUSD_HydraApexSceneEvaluator::setOutputOverrides(
        const UT_StringMap<GU_ConstDetailHandle> &overrides)
{
    if (overrides.empty())
    {
        myViewerStateOverrides.clear();
        return;
    }

    myViewerStateOverrides.clear();
    myViewerStateOverrides.setSize(myPathToOutputMap.size());

    for (auto &&[output_path, gdh] : overrides)
    {
        auto it = myPathToOutputMap.find(output_path);
        if (it == myPathToOutputMap.end())
        {
            UT_IF_ASSERT(const char *path = output_path.c_str();)
            UT_ASSERT_MSG(
                    false, "Unexpected output path '%s'", path);
            continue;
        }

        UT_ASSERT(gdh.isValid());
        myViewerStateOverrides[it->second] = gdh;
    }
}

GU_ConstDetailHandle
HUSD_HydraApexSceneEvaluator::getGeometry(
        exint output_idx,
        fpreal shutter_offset) const
{
    utZoneScopedN("HUSD_HydraApexSceneEvaluator::getGeometry");

    // If we're in the Animate state, use its overrides for the evaluated
    // geometry.
    if (!myViewerStateOverrides.isEmpty())
    {
        UT_ASSERT(myViewerStateOverrides.isValidIndex(output_idx));
        UT_ASSERT(myViewerStateOverrides[output_idx].isValid());
        return myViewerStateOverrides[output_idx];
    }

    EvaluatedSampleMap::accessor accessor;
    if (myEvaluatedSamples.insert(accessor, shutter_offset))
    {
        accessor->second = UTmakeUnique<EvaluatedSample>(
                *this, myCurrentFrame + shutter_offset);
    }

    EvaluatedSample *sample = accessor->second.get();
    accessor.release(); // Release lock before evaluating the scene.

    return sample->getGeometry(output_idx);
}

void
HUSD_HydraApexSceneEvaluator::ensureSceneLoaded() const
{
    if (mySceneValid)
        return;

    UT_ASSERT(mySceneGeo.isValid());

    UT_StringHolder errors;
    mySceneValid = myScene->updateSourceGeometry(*mySceneGeo.gdp(), errors);

    UT_ASSERT_MSG(mySceneValid, "Failed to load scene: '%s'", errors.c_str());
    if (!mySceneValid)
        return;

    for (const OutputInfo &info : myOutputInfos)
    {
        myScene->addOutput(
                info.myPath, info.myKey, /*copies_geo=*/false,
                /*track_output=*/true);
    }
}

GU_ConstDetailHandle
HUSD_HydraApexSceneEvaluator::EvaluatedSample::getGeometry(exint output_idx)
{
    myExclusive.execute(*this);

    UT_ASSERT(myOutputs.isValidIndex(output_idx));
    return myOutputs[output_idx].asConstHandle();;
}

void
HUSD_HydraApexSceneEvaluator::EvaluatedSample::operator()()
{
    utZoneScopedN("HUSD_HydraApexSceneEvaluator evaluate sample");

    myOutputs.clear();
    myOutputs.setSize(myEvaluator.myOutputInfos.size());

    UT_AutoLock lock(myEvaluator.mySceneLock);

    myEvaluator.ensureSceneLoaded();
    if (!myEvaluator.mySceneValid)
        return;

    APEXA_SceneInvoke &scene = *myEvaluator.myScene;
    scene.updateEvaluationTime(
            mySampleFrame,
            /*evaluate_tracked_outputs=*/true);

    for (exint i = 0, n = scene.getOutputs().size(); i < n; ++i)
    {
        UT_StringHolder error;
        if (!scene.evaluateOutput(i, error))
        {
            const APEXA_SceneInvoke::Output &output = scene.getOutputs()[i];
            TF_WARN("Failed to evaluate APEX scene output %s %s: %s",
                    output.myPath.c_str(),
                    output.myKey ? output.myKey->c_str() : "", error.c_str());
            continue;
        }

        const APEXA_SceneInvoke::Output &output = scene.getOutputs()[i];
        if (!output.myGeometry)
            continue;

        myOutputs[i] = *output.myGeometry;
    }
}
