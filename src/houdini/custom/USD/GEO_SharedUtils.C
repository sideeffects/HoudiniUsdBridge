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

#include "GEO_FilePrimUtils.h"
#include "GEO_SharedUtils.h"
#include <GA/GA_AttributeInstanceMatrix.h>
#include <UT/UT_WorkBuffer.h>

PXR_NAMESPACE_OPEN_SCOPE

template <class GEOMETRY, typename T>
static const T *
GEOgetAttribValue(
        const GEOMETRY &geo,
        const UT_StringHolder &attrname,
        const GEO_ImportOptions &options,
        UT_ArrayStringSet &processed_attribs,
        T &value)
{
    // Don't check options for GEO_HAPIParts
    if (!options.multiMatch(attrname) && !std::is_same<GEOMETRY, GEO_HAPIPart>::value)
        return nullptr;

    GT_Owner owner;
    GT_DataArrayHandle attrib = geo.findAttribute(attrname, owner, 0);

    if (!attrib || attrib->getTupleSize() != UT_FixedVectorTraits<T>::TupleSize)
        return nullptr;

    attrib->import(0, value.data(), UT_FixedVectorTraits<T>::TupleSize);
    processed_attribs.insert(attrname);
    return &value;
}

template <class GEOMETRY>
static UT_Matrix4D
GEOcomputeStandardPointXformT(
        const GEOMETRY &geo,
        const GEO_ImportOptions &options,
        UT_ArrayStringSet &processed_attribs)
{
    // If the number of attributes changes. this method probably needs
    // updating.
    SYS_STATIC_ASSERT(GA_AttributeInstanceMatrix::theNumAttribs == 10);

    UT_Vector3D P(0, 0, 0);
    GEOgetAttribValue(geo, GA_Names::P, options, processed_attribs, P);

    UT_Matrix4D xform(1.0);
    UT_Matrix3D xform3;
    bool has_xform_attrib = false;

    if (GEOgetAttribValue(
                geo, GA_Names::transform, options, processed_attribs, xform))
    {
        has_xform_attrib = true;
    }
    else if (GEOgetAttribValue(
                     geo, GA_Names::transform, options, processed_attribs,
                     xform3))
    {
        xform = xform3;
        has_xform_attrib = true;
    }

    // If the transform attrib is present, only P / trans / pivot are used.
    if (has_xform_attrib)
    {
        UT_Vector3D trans(0, 0, 0);
        GEOgetAttribValue(
                geo, GA_Names::trans, options, processed_attribs, trans);

        UT_Vector3D p;
        xform.getTranslates(p);
        p += P + trans;
        xform.setTranslates(p);

        UT_Vector3D pivot;
        if (GEOgetAttribValue(
                    geo, GA_Names::pivot, options, processed_attribs, pivot))
        {
            xform.pretranslate(-pivot);
        }

        return xform;
    }

    UT_Vector3D N(0, 0, 0);
    if (!GEOgetAttribValue(geo, GA_Names::N, options, processed_attribs, N))
        GEOgetAttribValue(geo, GA_Names::v, options, processed_attribs, N);

    UT_FixedVector<double, 1> pscale(1.0);
    GEOgetAttribValue(
            geo, GA_Names::pscale, options, processed_attribs, pscale);

    UT_Vector3D s3, up, trans, pivot;
    UT_QuaternionD rot, orient;

    xform.instance(
            P, N, pscale[0],
            GEOgetAttribValue(
                    geo, GA_Names::scale, options, processed_attribs, s3),
            GEOgetAttribValue(
                    geo, GA_Names::up, options, processed_attribs, up),
            GEOgetAttribValue(
                    geo, GA_Names::rot, options, processed_attribs, rot),
            GEOgetAttribValue(
                    geo, GA_Names::trans, options, processed_attribs, trans),
            GEOgetAttribValue(
                    geo, GA_Names::orient, options, processed_attribs, orient),
            GEOgetAttribValue(
                    geo, GA_Names::pivot, options, processed_attribs, pivot));
    return xform;
}

UT_Matrix4D
GEOcomputeStandardPointXform(
        const GEO_HAPIPart &geo,
        UT_ArrayStringSet &processed_attribs)
{
    GEO_ImportOptions options;
    return GEOcomputeStandardPointXformT(geo, options, processed_attribs);
}

UT_Matrix4D
GEOcomputeStandardPointXform(
        const GT_Primitive &geo,
        const GEO_ImportOptions &options,
        UT_ArrayStringSet &processed_attribs)
{
    return GEOcomputeStandardPointXformT(geo, options, processed_attribs);
}

PXR_NAMESPACE_CLOSE_SCOPE
