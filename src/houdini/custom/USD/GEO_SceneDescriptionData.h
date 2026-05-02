/*
 * Copyright 2020 Side Effects Software Inc.
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

#ifndef __GEO_SCENE_DESCRIPTION_DATA_H__
#define __GEO_SCENE_DESCRIPTION_DATA_H__

#include "GEO_FilePrim.h"
#include "pxr/base/tf/declarePtrs.h"
#include "pxr/pxr.h"
#include "pxr/usd/sdf/abstractData.h"
#include "pxr/usd/sdf/data.h"
#include "pxr/usd/sdf/fileFormat.h"

PXR_NAMESPACE_OPEN_SCOPE

class GEO_FileFieldValue;
class GEO_ImportOptions;

/// \class GEO_SceneDescriptionData
///
/// Base class providing an SdfAbstractData interface for Houdini geometry data
///
class GEO_SceneDescriptionData : public SdfAbstractData
{
public:
    // Open file at filePath. Override this function to support different file
    // types
    virtual bool Open(const std::string &filePath) = 0;

    // We don't stream data from disk, but we must claim that we do or else
    // reloading layers of this format will try to do fine grained updates and
    // set values onto this layer, which is not supported.
    bool StreamsData() const override { return true; }

    // SdfAbstractData overrides
    void CreateSpec(const SdfPath &, SdfSpecType specType) override;
    bool HasSpec(const SdfPath &) const override;
    void EraseSpec(const SdfPath &) override;
    void MoveSpec(const SdfPath &oldId, const SdfPath &newId) override;
    SdfSpecType GetSpecType(const SdfPath &) const override;
    bool Has(const SdfPath &,
             const TfToken &fieldName,
             SdfAbstractDataValue *value) const override;
    bool Has(const SdfPath &,
             const TfToken &fieldName,
             VtValue *value = NULL) const override;
    VtValue Get(const SdfPath &,
                const TfToken &fieldName) const override;
    void Set(const SdfPath &,
             const TfToken &fieldName,
             const VtValue &value) override;
    void Set(const SdfPath &,
             const TfToken &fieldName,
             const SdfAbstractDataConstValue &value) override;
    void Erase(const SdfPath &, const TfToken &fieldName) override;
    std::vector<TfToken> List(const SdfPath &) const override;
    std::set<double> ListAllTimeSamples() const override;
    std::set<double> ListTimeSamplesForPath(
        const SdfPath &) const override;
    bool GetBracketingTimeSamples(double time,
                                  double *tLower,
                                  double *tUpper) const override;
    size_t GetNumTimeSamplesForPath(const SdfPath &id) const override;
    bool GetBracketingTimeSamplesForPath(const SdfPath &,
                                         double time,
                                         double *tLower,
                                         double *tUpper) const override;
    bool QueryTimeSample(const SdfPath &,
                         double time,
                         SdfAbstractDataValue *value) const override;
    bool QueryTimeSample(const SdfPath &,
                         double time,
                         VtValue *value) const override;
    void SetTimeSample(const SdfPath &,
                       double,
                       const VtValue &) override;
    void EraseTimeSample(const SdfPath &, double) override;

protected:
    GEO_SceneDescriptionData();
    ~GEO_SceneDescriptionData() override;

    const GEO_FilePrim *getPrim(const SdfPath &id) const;

    // SdfAbstractData overrides
    void _VisitSpecs(
        SdfAbstractDataSpecVisitor *visitor) const override;
    bool _Has(const SdfPath &id,
              const TfToken &fieldName,
              const GEO_FileFieldValue &value) const;

    static void setupHierarchyAndKind(
            GEO_FilePrimMap &prims,
            const GEO_ImportOptions &options,
            GEO_HandleOtherPrims parents_primhandling,
            const GEO_FilePrim *layer_info_prim);

    GEO_FilePrimMap myPrims;
    GEO_FilePrim *myPseudoRoot;
    std::set<double> myTimeSamples;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // __GEO_SCENE_DESCRIPTION_DATA_H__
