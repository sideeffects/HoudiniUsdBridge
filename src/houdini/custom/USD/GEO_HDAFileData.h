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

#ifndef __GEO_DYNAMIC_FILEDATA_H__
#define __GEO_DYNAMIC_FILEDATA_H__

#include "GEO_FilePrim.h"
#include "GEO_FilePrimUtils.h"
#include "GEO_HAPIReader.h"
#include "pxr/base/tf/declarePtrs.h"
#include "pxr/pxr.h"
#include "pxr/usd/sdf/abstractData.h"
#include "pxr/usd/sdf/data.h"
#include "pxr/usd/sdf/fileFormat.h"
#include <HAPI/HAPI.h>
#include <UT/UT_Array.h>
#include <UT/UT_UniquePtr.h>

class GA_PrimitiveGroup;

PXR_NAMESPACE_OPEN_SCOPE

class GEO_FileFieldValue;

TF_DECLARE_WEAK_AND_REF_PTRS(GEO_HDAFileData);

/// \class GEO_HDAFileData
///
class GEO_HDAFileData : public SdfAbstractData
{
public:
    static GEO_HDAFileDataRefPtr New(
        const SdfFileFormat::FileFormatArguments &args);

    bool Open(const std::string &filePath);

    // TODO: All overrides below can be shared with GEO_FileData using a base class

    // We don't stream data from disk, but we must claim that we do or else
    // reloading layers of this format will try to do fine grained updates and
    // set values onto this layer, which is not supported.
    virtual bool StreamsData() const override { return true; }

    // SdfAbstractData overrides
    virtual void CreateSpec(const SdfPath &, SdfSpecType specType) override;
    virtual bool HasSpec(const SdfPath &) const override;
    virtual void EraseSpec(const SdfPath &) override;
    virtual void MoveSpec(const SdfPath &oldId, const SdfPath &newId) override;
    virtual SdfSpecType GetSpecType(const SdfPath &) const override;
    virtual bool Has(const SdfPath &,
                     const TfToken &fieldName,
                     SdfAbstractDataValue *value) const override;
    virtual bool Has(const SdfPath &,
                     const TfToken &fieldName,
                     VtValue *value = NULL) const override;
    virtual VtValue Get(const SdfPath &,
                        const TfToken &fieldName) const override;
    virtual void Set(const SdfPath &,
                     const TfToken &fieldName,
                     const VtValue &value) override;
    virtual void Set(const SdfPath &,
                     const TfToken &fieldName,
                     const SdfAbstractDataConstValue &value) override;
    virtual void Erase(const SdfPath &, const TfToken &fieldName) override;
    virtual std::vector<TfToken> List(const SdfPath &) const override;
    virtual std::set<double> ListAllTimeSamples() const override;
    virtual std::set<double> ListTimeSamplesForPath(
        const SdfPath &) const override;
    virtual bool GetBracketingTimeSamples(double time,
                                          double *tLower,
                                          double *tUpper) const override;
    virtual size_t GetNumTimeSamplesForPath(const SdfPath &id) const override;
    virtual bool GetBracketingTimeSamplesForPath(const SdfPath &,
                                                 double time,
                                                 double *tLower,
                                                 double *tUpper) const override;
    virtual bool QueryTimeSample(const SdfPath &,
                                 double time,
                                 SdfAbstractDataValue *value) const override;
    virtual bool QueryTimeSample(const SdfPath &,
                                 double time,
                                 VtValue *value) const override;
    virtual void SetTimeSample(const SdfPath &,
                               double,
                               const VtValue &) override;
    virtual void EraseTimeSample(const SdfPath &, double) override;

protected:
    GEO_HDAFileData();
    virtual ~GEO_HDAFileData();

    // SdfAbstractData overrides
    virtual void _VisitSpecs(
        SdfAbstractDataSpecVisitor *visitor) const override;
    bool _Has(const SdfPath &id,
              const TfToken &fieldName,
              const GEO_FileFieldValue &value) const;

private:

    // This is to keep track of the number of
    // prims that have displayed for naming
    struct PrimCounts
    {
        exint boxes = 0;
        exint curves = 0;
        exint instances = 0;
        exint meshes = 0;
        exint spheres = 0;
        exint volumes = 0;

        exint others = 0;
    };

    const GEO_FilePrim *getPrim(const SdfPath &id) const;

    // Converts Houdini Engine Parts into USD geometry
    void partToPrim(GEO_HAPIPart &part,
                    GEO_ImportOptions &options,
                    const SdfPath &defaultPath,
                    PrimCounts &counts);

    SdfPath getPrimPath(HAPI_PartType type,
                        const SdfPath &defaultPath,
                        PrimCounts &counts);

    // Configures options based on file format arguments
    void configureOptions(GEO_ImportOptions &options);

    GEO_FilePrimMap myPrims;
    GEO_FilePrim *myPseudoRoot;
    GEO_FilePrim *myLayerInfoPrim;
    SdfFileFormat::FileFormatArguments myCookArgs;
    fpreal mySampleFrame;
    bool mySampleFrameSet;
    bool mySaveSampleFrame;

    friend class GEO_FilePrim;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // __GEO_DYNAMIC_FILEDATA_H__
