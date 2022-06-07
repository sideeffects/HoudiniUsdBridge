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

#include "GEO_SceneDescriptionData.h"
#include "GEO_FileFieldValue.h"
#include "GEO_FilePrimUtils.h"
#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/tf/pathUtils.h>
#include <pxr/usd/kind/registry.h>
#include <pxr/usd/sdf/schema.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdVol/tokens.h>

PXR_NAMESPACE_OPEN_SCOPE

#define UNSUPPORTED(M)                                                         \
    TF_RUNTIME_ERROR("Houdini geometry file " #M "() not supported")

GEO_SceneDescriptionData::GEO_SceneDescriptionData()
    : myPseudoRoot(nullptr), mySampleFrame(0.0), mySampleFrameSet(false)
{
}

GEO_SceneDescriptionData::~GEO_SceneDescriptionData() {}

void
GEO_SceneDescriptionData::CreateSpec(const SdfPath &id, SdfSpecType specType)
{
    UNSUPPORTED(CreateSpec);
}

bool
GEO_SceneDescriptionData::HasSpec(const SdfPath &id) const
{
    if (auto prim = getPrim(id))
    {
        if (id.IsPropertyPath())
            return (prim->getProp(id) != nullptr);
        else
            return true;
    }

    return (id == SdfPath::AbsoluteRootPath());
}

void
GEO_SceneDescriptionData::EraseSpec(const SdfPath &id)
{
    UNSUPPORTED(EraseSpec);
}

void
GEO_SceneDescriptionData::MoveSpec(const SdfPath &oldId, const SdfPath &newId)
{
    UNSUPPORTED(MoveSpec);
}

SdfSpecType
GEO_SceneDescriptionData::GetSpecType(const SdfPath &id) const
{
    if (auto prim = getPrim(id))
    {
        if (id.IsPropertyPath())
        {
            if (prim->getProp(id))
            {
                if (prim->getProp(id)->getIsRelationship())
                    return SdfSpecTypeRelationship;
                else
                    return SdfSpecTypeAttribute;
            }
        }
        else if (prim == myPseudoRoot)
        {
            return SdfSpecTypePseudoRoot;
        }
        else
        {
            return SdfSpecTypePrim;
        }
    }

    return SdfSpecTypeUnknown;
}

void
GEO_SceneDescriptionData::_VisitSpecs(SdfAbstractDataSpecVisitor *visitor) const
{
    for (auto primit = myPrims.begin(); primit != myPrims.end(); ++primit)
    {
        if (!visitor->VisitSpec(*this, primit->first))
            return;

        if (&primit->second != myPseudoRoot)
        {
            const auto &props = primit->second.getProps();

            for (auto propit = props.begin(); propit != props.end(); ++propit)
            {
                if (!visitor->VisitSpec(
                        *this, primit->first.AppendProperty(propit->first)))
                    return;
            }
        }
    }
}

bool
GEO_SceneDescriptionData::_Has(const SdfPath &id,
                           const TfToken &fieldName,
                           const GEO_FileFieldValue &value) const
{
    if (auto prim = getPrim(id))
    {
        if (id.IsPropertyPath())
        {
            auto prop = prim->getProp(id);

            if (prop)
            {
                if (prop->getIsRelationship())
                {
                    // Fields specific to relationships.
                    if (fieldName == SdfFieldKeys->TargetPaths)
                    {
                        return prop->copyData(value);
                    }
                }
                else
                {
                    // Fields specific to attributes.
                    if (fieldName == SdfFieldKeys->Default &&
                        (!mySampleFrameSet || prop->getValueIsDefault()))
                    {
                        return prop->copyData(value);
                    }
                    else if (fieldName == SdfFieldKeys->TypeName)
                    {
                        return value.Set(prop->getTypeName().GetAsToken());
                    }
                    else if (fieldName == SdfFieldKeys->TimeSamples &&
                             (mySampleFrameSet && !prop->getValueIsDefault()))
                    {
                        if (value)
                        {
                            VtValue tmp;
                            GEO_FileFieldValue tmpval(&tmp);
                            SdfTimeSampleMap samples;

                            if (prop->copyData(tmpval))
                                samples[mySampleFrame] = tmp;

                            return value.Set(samples);
                        }
                        else
                            return true;
                    }
                }

                // fields common to attributes and relationships.
                if (fieldName == SdfFieldKeys->CustomData &&
                    !prop->getCustomData().empty())
                {
                    VtDictionary custom_data;
                    for (auto &&it : prop->getCustomData())
                        custom_data[it.first] = it.second;
                    return value.Set(custom_data);
                }
                else if (fieldName == SdfFieldKeys->Variability)
                {
                    if (prop->getValueIsUniform())
                        return value.Set(SdfVariabilityUniform);
                    else
                        return value.Set(SdfVariabilityVarying);
                }

                auto it = prop->getMetadata().find(fieldName);
                if (it != prop->getMetadata().end())
                    return value.Set(it->second);
            }
        }
        else
        {
            if (prim != myPseudoRoot)
            {
                if (fieldName == SdfChildrenKeys->PropertyChildren)
                {
                    return value.Set(prim->getPropNames());
                }
                else if (fieldName == SdfFieldKeys->TypeName)
                {
                    // Don't return a prim type unless the prim is defined.
                    // If we are just creating overlay data for existing prims,
                    // we don't want to change any prim types.
                    if (prim->getIsDefined())
                        return value.Set(prim->getTypeName());
                }
                else if (fieldName == SdfFieldKeys->Specifier)
                {
                    if (prim->getIsDefined())
                        return value.Set(SdfSpecifierDef);
                    else
                        return value.Set(SdfSpecifierOver);
                }
            }
            if (fieldName == SdfChildrenKeys->PrimChildren)
            {
                return value.Set(prim->getChildNames());
            }
            else if (((fieldName == SdfFieldKeys->CustomData &&
                       prim != myPseudoRoot) ||
                      (fieldName == SdfFieldKeys->CustomLayerData &&
                       prim == myPseudoRoot)) &&
                     !prim->getCustomData().empty())
            {
                VtDictionary custom_data;
                for (auto &&it : prim->getCustomData())
                    custom_data[it.first] = it.second;
                return value.Set(custom_data);
            }

            auto it = prim->getMetadata().find(fieldName);
            if (it != prim->getMetadata().end())
                return value.Set(it->second);
        }
    }

    return false;
}

bool
GEO_SceneDescriptionData::Has(const SdfPath &id,
                          const TfToken &fieldName,
                          SdfAbstractDataValue *value) const
{
    return _Has(id, fieldName, GEO_FileFieldValue(value));
}

bool
GEO_SceneDescriptionData::Has(const SdfPath &id,
                          const TfToken &fieldName,
                          VtValue *value) const
{
    return _Has(id, fieldName, GEO_FileFieldValue(value));
}

VtValue
GEO_SceneDescriptionData::Get(const SdfPath &id, const TfToken &fieldName) const
{
    VtValue result;

    Has(id, fieldName, &result);

    return result;
}

void
GEO_SceneDescriptionData::Set(const SdfPath &id,
                          const TfToken &fieldName,
                          const VtValue &value)
{
    UNSUPPORTED(Set);
}

void
GEO_SceneDescriptionData::Set(const SdfPath &id,
                          const TfToken &fieldName,
                          const SdfAbstractDataConstValue &value)
{
    UNSUPPORTED(Set);
}

void
GEO_SceneDescriptionData::Erase(const SdfPath &id, const TfToken &fieldName)
{
    UNSUPPORTED(Erase);
}

std::vector<TfToken>
GEO_SceneDescriptionData::List(const SdfPath &id) const
{
    TfTokenVector result;

    if (auto prim = getPrim(id))
    {
        if (id.IsPropertyPath())
        {
            if (auto prop = prim->getProp(id))
            {
                if (prop->getIsRelationship())
                {
                    result.push_back(SdfFieldKeys->TargetPaths);
                }
                else
                {
                    if (mySampleFrameSet && !prop->getValueIsDefault())
                        result.push_back(SdfFieldKeys->TimeSamples);
                    else
                        result.push_back(SdfFieldKeys->Default);
                    result.push_back(SdfFieldKeys->TypeName);
                }
                result.push_back(SdfFieldKeys->Variability);

                if (!prop->getCustomData().empty())
                    result.push_back(SdfFieldKeys->CustomData);

                for (auto &&it : prop->getMetadata())
                    result.push_back(it.first);
            }
        }
        else
        {
            if (prim != myPseudoRoot)
            {
                result.push_back(SdfFieldKeys->Specifier);
                result.push_back(SdfFieldKeys->TypeName);
                if (!prim->getPropNames().empty())
                    result.push_back(SdfChildrenKeys->PropertyChildren);
            }
            result.push_back(SdfChildrenKeys->PrimChildren);
            if (!prim->getCustomData().empty())
            {
                if (prim == myPseudoRoot)
                    result.push_back(SdfFieldKeys->CustomLayerData);
                else
                    result.push_back(SdfFieldKeys->CustomData);
            }

            for (auto &&it : prim->getMetadata())
                result.push_back(it.first);
        }
    }

    return result;
}

std::set<double>
GEO_SceneDescriptionData::ListAllTimeSamples() const
{
    if (mySampleFrameSet)
        return std::set<double>({mySampleFrame});

    static const std::set<double> theEmptySet;

    return theEmptySet;
}

std::set<double>
GEO_SceneDescriptionData::ListTimeSamplesForPath(const SdfPath &id) const
{
    if (mySampleFrameSet && id.IsPropertyPath())
    {
        if (auto prim = getPrim(id))
        {
            auto prop = prim->getProp(id);

            if (prop && !prop->getValueIsDefault())
                return std::set<double>({mySampleFrame});
        }
    }

    static const std::set<double> theEmptySet;

    return theEmptySet;
}

bool
GEO_SceneDescriptionData::GetBracketingTimeSamples(double time,
                                               double *tLower,
                                               double *tUpper) const
{
    if (mySampleFrameSet)
    {
        if (tLower)
            *tLower = mySampleFrame;
        if (tUpper)
            *tUpper = mySampleFrame;

        return true;
    }

    return false;
}

size_t
GEO_SceneDescriptionData::GetNumTimeSamplesForPath(const SdfPath &id) const
{
    if (mySampleFrameSet && id.IsPropertyPath())
    {
        if (auto prim = getPrim(id))
        {
            auto prop = prim->getProp(id);

            if (prop && !prop->getValueIsDefault())
                return 1u;
        }
    }

    return 0u;
}

bool
GEO_SceneDescriptionData::GetBracketingTimeSamplesForPath(const SdfPath &id,
                                                      double time,
                                                      double *tLower,
                                                      double *tUpper) const
{
    if (mySampleFrameSet && id.IsPropertyPath())
    {
        if (auto prim = getPrim(id))
        {
            auto prop = prim->getProp(id);

            if (prop && !prop->getValueIsDefault())
            {
                if (tLower)
                    *tLower = mySampleFrame;
                if (tUpper)
                    *tUpper = mySampleFrame;

                return true;
            }
        }
    }

    return false;
}

bool
GEO_SceneDescriptionData::QueryTimeSample(const SdfPath &id,
                                      double time,
                                      SdfAbstractDataValue *value) const
{
    if (mySampleFrameSet && SYSisEqual(time, mySampleFrame))
    {
        if (id.IsPropertyPath())
        {
            if (auto prim = getPrim(id))
            {
                auto prop = prim->getProp(id);

                if (prop && !prop->getValueIsDefault())
                {
                    if (value)
                        return prop->copyData(GEO_FileFieldValue(value));

                    return true;
                }
            }
        }
    }

    return false;
}

bool
GEO_SceneDescriptionData::QueryTimeSample(const SdfPath &id,
                                      double time,
                                      VtValue *value) const
{
    if (mySampleFrameSet && SYSisEqual(time, mySampleFrame))
    {
        if (id.IsPropertyPath())
        {
            if (auto prim = getPrim(id))
            {
                auto prop = prim->getProp(id);

                if (prop && !prop->getValueIsDefault())
                {
                    if (value)
                        return prop->copyData(GEO_FileFieldValue(value));

                    return true;
                }
            }
        }
    }

    return false;
}

void
GEO_SceneDescriptionData::SetTimeSample(const SdfPath &id,
                                    double time,
                                    const VtValue &value)
{
    UNSUPPORTED(SetTimeSample);
}

void
GEO_SceneDescriptionData::EraseTimeSample(const SdfPath &id, double time)
{
    UNSUPPORTED(EraseTimeSample);
}

const GEO_FilePrim *
GEO_SceneDescriptionData::getPrim(const SdfPath &id) const
{
    GEO_FilePrimMap::const_iterator it;

    if (id == SdfPath::AbsoluteRootPath())
        it = myPrims.find(id);
    else
        it = myPrims.find(id.GetPrimOrPrimVariantSelectionPath());

    if (it != myPrims.end())
        return &it->second;

    return nullptr;
}

static bool
geoHasChildGprim(
        const GEO_FilePrimMap &prims,
        const SdfPath &path,
        const GEO_FilePrim &prim)
{
    for (const TfToken &child_name : prim.getChildNames())
    {
        SdfPath child_path = path.AppendChild(child_name);
        auto child_it = prims.find(child_path);
        UT_ASSERT(child_it != prims.end());

        if (child_it->second.isGprim())
            return true;
    }

    return false;
}

void
GEO_SceneDescriptionData::setupHierarchyAndKind(
        GEO_FilePrimMap &prims,
        const GEO_ImportOptions &options,
        GEO_HandleOtherPrims parents_primhandling,
        const GEO_FilePrim *layer_info_prim)
{
    // Set up parent-child relationships.
    for (auto &&it : prims)
    {
        const SdfPath &path = it.first;
        SdfPath parentpath = path.GetParentPath();

        // We don't want to author a kind or set up a parent relationship
        // for the pseudoroot.
        if (!parentpath.IsEmpty())
        {
            prims[parentpath].addChild(path.GetNameToken());

            if (!it.second.getInitialized())
            {
                GEOinitXformPrim(it.second, parents_primhandling);
            }

            // Special override of the Kind of root primitives. We can't
            // set the Kind of the pseudo root prim, so don't try.
            // We also don't want to author a kind for the layer info prim.
            if (options.myOtherPrimHandling != GEO_OTHER_DEFINE
                || options.myDefineOnlyLeafPrims
                || &it.second == layer_info_prim)
            {
                continue;
            }

            // When setting all the geometry to a single component, the prefix
            // path should become the component if possible. Otherwise, the
            // root prim(s) are components.
            if (options.myKindSchema == GEO_KINDSCHEMA_COMPONENT)
            {
                TfToken kind;
                if (path == options.myPrefixPath)
                    kind = KindTokens->component;
                else if (options.myPrefixPath.HasPrefix(path))
                    kind = KindTokens->group;
                else if (path.IsRootPrimPath())
                    kind = KindTokens->component;

                if (!kind.IsEmpty())
                    it.second.replaceMetadata(SdfFieldKeys->Kind, VtValue(kind));
            }
        }
    }

    // When creating multiple components, the highest Xform that has a gprim
    // child should become a component.
    // This requires a separate pass once the parent/child info has been
    // recorded.
    if ((options.myKindSchema == GEO_KINDSCHEMA_NESTED_GROUP
         || options.myKindSchema == GEO_KINDSCHEMA_NESTED_ASSEMBLY)
        && options.myOtherPrimHandling == GEO_OTHER_DEFINE
        && !options.myDefineOnlyLeafPrims)
    {
        for (auto it = prims.begin(); it != prims.end();)
        {
            const SdfPath &path = it->first;
            GEO_FilePrim &prim = it->second;

            if (&prim == layer_info_prim)
            {
                ++it;
                continue;
            }

            TfToken kind;
            if (geoHasChildGprim(prims, path, prim))
            {
                kind = KindTokens->component;
                it = it.GetNextSubtree(); // Skip over any child prims.
            }
            else
            {
                if (!prim.getChildNames().empty())
                {
                    if (path.IsRootPrimPath()
                        && options.myKindSchema == GEO_KINDSCHEMA_NESTED_ASSEMBLY)
                    {
                        kind = KindTokens->assembly;
                    }
                    else
                        kind = KindTokens->group;
                }

                ++it;
            }

            if (!kind.IsEmpty())
                prim.replaceMetadata(SdfFieldKeys->Kind, VtValue(kind));
        }
    }
}

PXR_NAMESPACE_CLOSE_SCOPE
