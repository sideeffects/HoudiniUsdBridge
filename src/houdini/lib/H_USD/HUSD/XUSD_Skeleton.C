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

#include "XUSD_Skeleton.h"

#include "HUSD_ErrorScope.h"

#include <GA/GA_Names.h>
#include <GU/GU_AgentBlendShapeUtils.h>
#include <GU/GU_Detail.h>
#include <GU/GU_DetailHandle.h>
#include <GU/GU_LinearSkinDeformer.h>
#include <GU/GU_MotionClipUtil.h>
#include <GU/GU_PackedGeometry.h>
#include <GU/GU_PrimPacked.h>
#include <gusd/GU_USD.h>
#include <gusd/USD_Utils.h>
#include <gusd/UT_Gf.h>
#include <gusd/agentUtils.h>

#include <pxr/usd/usdSkel/bindingAPI.h>
#include <pxr/usd/usdSkel/blendShape.h>
#include <pxr/usd/usdSkel/blendShapeQuery.h>
#include <pxr/usd/usdSkel/inbetweenShape.h>
#include <pxr/usd/usdSkel/skinningQuery.h>

PXR_NAMESPACE_OPEN_SCOPE

static bool
xusdGetOffsets(const UsdSkelBlendShape &blendshape, VtVec3fArray &offsets)
{
    return blendshape.GetOffsetsAttr().Get(&offsets);
}

static bool
xusdGetOffsets(const UsdSkelInbetweenShape &inbetween, VtVec3fArray &offsets)
{
    return inbetween.GetOffsets(&offsets);
}

static bool
xusdGetNormalOffsets(const UsdSkelBlendShape &blendshape, VtVec3fArray &offsets)
{
    return blendshape.GetNormalOffsetsAttr().Get(&offsets);
}

static bool
xusdGetNormalOffsets(
        const UsdSkelInbetweenShape &inbetween,
        VtVec3fArray &offsets)
{
    return inbetween.GetNormalOffsets(&offsets);
}

/// Import the geometry for a blendshape input or in-between shape, which
/// consists of point positions and an id attribute (for sparse blendshapes).
/// In-between shapes use the point indices from the primary shape, if
/// authored.
/// Note since we pre-apply skel:geomBindTransform to the rest
/// geometry if there is skinning, the blendshape offsets also need to be
/// transformed accordingly.
template <typename BlendshapeT>
static bool
xusdImportBlendShape(
        GU_Detail &detail,
        const BlendshapeT &blendshape_or_inbetween,
        const UsdSkelBlendShape &blendshape,
        const GU_Detail &base_shape,
        const UT_Matrix4D &geom_bind_xform)
{
    VtVec3fArray offsets;
    const bool has_P = xusdGetOffsets(blendshape_or_inbetween, offsets);

    VtVec3fArray normal_offsets;
    const bool has_N = xusdGetNormalOffsets(
            blendshape_or_inbetween, normal_offsets);

    if (!has_P && !has_N)
    {
        HUSD_ErrorScope::addWarning(
                HUSD_ERR_STRING, "Blendshape does not have 'offsets' or "
                                 "'normalOffsets' authored.");
        return false;
    }
    if (has_P && has_N && offsets.size() != normal_offsets.size())
    {
        HUSD_ErrorScope::addWarning(
                HUSD_ERR_STRING,
                "Mismatched number of 'offsets' and 'normalOffsets'.");
        return false;
    }

    const size_t num_target_pts = has_P ? offsets.size() :
                                          normal_offsets.size();

    bool has_indices = false;
    VtIntArray indices;
    if (blendshape.GetPointIndicesAttr().Get(&indices))
    {
        has_indices = true;
        if (indices.size() != num_target_pts)
        {
            HUSD_ErrorScope::addError(
                HUSD_ERR_STRING, "Mismatched number of indices and offsets.");
            return false;
        }
    }
    else if (base_shape.getNumPoints() != num_target_pts)
    {
        // If this isn't sparse, we should have the same number of points as
        // the base shape!
        HUSD_ErrorScope::addError(
            HUSD_ERR_STRING,
            "Blendshape has a different number of points than the base shape");
        return false;
    }

    // Translate the pointIndices attr back to an 'id' attribute for GU_Blend
    // to match up points by id.
    GA_ROHandleI base_id_attrib;
    GA_RWHandleI id_attrib;
    if (has_indices)
    {
        id_attrib = detail.addIntTuple(GA_ATTRIB_POINT, GA_Names::id, 1);
        base_id_attrib = base_shape.findIntTuple(
            GA_ATTRIB_POINT, GA_Names::id, 1);
    }

    GA_ROHandleV3 src_normal_attrib
            = base_shape.findNormalAttribute(GA_ATTRIB_POINT);

    GA_RWHandleV3 normal_attrib;
    if (has_N)
        normal_attrib = detail.addNormalAttribute(GA_ATTRIB_POINT);

    UT_Matrix3D inv_geom_bind_xform;
    if (has_N)
    {
        inv_geom_bind_xform = geom_bind_xform;
        inv_geom_bind_xform.invert();
    }

    GA_Offset ptoff = detail.appendPointBlock(num_target_pts);
    for (exint i = 0, n = num_target_pts; i < n; ++i, ++ptoff)
    {
        GA_Index base_ptidx;
        if (has_indices)
        {
            base_ptidx = indices[i];
            if (base_ptidx < 0 || base_ptidx >= base_shape.getNumPoints())
            {
                HUSD_ErrorScope::addError(
                    HUSD_ERR_STRING, "Invalid point index.");
                return false;
            }
        }
        else
            base_ptidx = i;

        const GA_Offset base_ptoff = base_shape.pointOffset(base_ptidx);

        // USD blendshapes store offsets from the base shape's positions, but
        // for agents we need the actual point positions.
        UT_Vector3 pos = base_shape.getPos3(base_ptoff);
        if (has_P)
            pos += rowVecMult3(GusdUT_Gf::Cast(offsets[i]), geom_bind_xform);

        detail.setPos3(ptoff, pos);

        if (has_N)
        {
            UT_Vector3 normal(0, 0, 0);
            if (src_normal_attrib.isValid())
                normal = src_normal_attrib.get(base_ptoff);

            normal += colVecMult(
                    inv_geom_bind_xform, GusdUT_Gf::Cast(normal_offsets[i]));
            normal.normalize();

            normal_attrib.set(ptoff, normal);
        }

        // Record the id point attribute for sparse blendshapes.
        if (has_indices)
        {
            id_attrib.set(ptoff, base_id_attrib.isValid() ?
                                     base_id_attrib.get(base_ptoff) :
                                     static_cast<int>(base_ptidx));
        }
    }

    return true;
}

static bool
xusdFindBlendShapes(
        const UsdSkelSkinningQuery &skinning_query,
        VtTokenArray &channel_names,
        UT_Array<UsdSkelBlendShape> &blendshapes)
{
    UsdSkelBlendShapeQuery blendshape_query(
            UsdSkelBindingAPI(skinning_query.GetPrim()));
    UT_ASSERT(blendshape_query.IsValid());

    if (!skinning_query.GetBlendShapeOrder(&channel_names))
    {
        UT_WorkBuffer msg;
        msg.format(
                "Failed to compute blendshape order for '{}'",
                skinning_query.GetPrim().GetPath().GetString());
        HUSD_ErrorScope::addError(HUSD_ERR_STRING, msg.buffer());
        return false;
    }

    UT_ASSERT(channel_names.size() == blendshape_query.GetNumBlendShapes());

    blendshapes.setCapacity(blendshape_query.GetNumBlendShapes());
    for (exint i = 0, n = blendshape_query.GetNumBlendShapes(); i < n; ++i)
        blendshapes.append(blendshape_query.GetBlendShape(i));

    return true;
}

/// Builds a unique path for the inbetween shape.
static UT_StringHolder
xusdGetInBetweenPath(
        const SdfPath &prim_path,
        const UsdSkelInbetweenShape &inbetween)
{
    static constexpr UT_StringLit theInbetweensPrefix("inbetweens:");

    UT_WorkBuffer name;
    name = inbetween.GetAttr().GetName().GetString();
    // Strip the "inbetweens:" prefix.
    if (!name.strncmp(
                theInbetweensPrefix.c_str(), theInbetweensPrefix.length()))
    {
        name.eraseHead(theInbetweensPrefix.length());
    }

    // Prefix with the blendshape prim's path.
    return prim_path.AppendChild(TfToken(name.toStdString())).GetAsString();
}

bool
XUSDimportBlendShapes(
        GU_Detail &detail,
        const UsdSkelSkinningQuery &skinning_query,
        const SdfPath &root_path,
        const UT_Matrix4D &geom_bind_xform)
{
    VtTokenArray channel_names;
    UT_Array<UsdSkelBlendShape> blendshapes;
    if (!xusdFindBlendShapes(skinning_query, channel_names, blendshapes))
        return false;

    // Import the blendshape points.
    UT_Array<GU_DetailHandle> shape_details;
    UT_Array<GU_DetailHandle> inbetween_details;
    for (exint i = 0, n = blendshapes.entries(); i < n; ++i)
    {
        const UsdSkelBlendShape &blendshape = blendshapes[i];

        GU_DetailHandle shape_gdh;
        shape_gdh.allocateAndSet(new GU_Detail());

        GU_DetailHandleAutoWriteLock shape_detail(shape_gdh);
        if (!xusdImportBlendShape(
                    *shape_detail, blendshape, blendshape, detail,
                    geom_bind_xform))
        {
            UT_WorkBuffer msg;
            msg.format(
                    "Failed to import blendshape '{}'",
                    blendshape.GetPath().GetString());
            HUSD_ErrorScope::addError(HUSD_ERR_STRING, msg.buffer());
            return false;
        }

        shape_details.append(shape_gdh);

        // Import in-between shapes
        for (const UsdSkelInbetweenShape &inbetween :
             blendshape.GetInbetweens())
        {
            float weight = 0;
            if (!inbetween.GetWeight(&weight))
            {
                HUSD_ErrorScope::addError(
                        HUSD_ERR_STRING,
                        "Weight is not authored for in-between shape");
                return false;
            }

            GU_DetailHandle inbetween_gdh;
            inbetween_gdh.allocateAndSet(new GU_Detail());

            GU_DetailHandleAutoWriteLock inbetween_detail(inbetween_gdh);
            if (!xusdImportBlendShape(
                        *inbetween_detail, inbetween, blendshape, detail,
                        geom_bind_xform))
            {
                UT_WorkBuffer msg;
                msg.format(
                        "Failed to import in-between '{}' for '{}'",
                        inbetween.GetAttr().GetName().GetString(),
                        blendshape.GetPath().GetString());
                HUSD_ErrorScope::addError(HUSD_ERR_STRING, msg.buffer());
                return false;
            }

            inbetween_details.append(inbetween_gdh);
        }
    }

    GA_RWHandleS channel_attrib = detail.addStringTuple(
            GA_ATTRIB_PRIMITIVE, GU_MotionClipNames::blendshape_channel, 1);
    GA_RWHandleS shape_name_attrib = detail.addStringTuple(
            GA_ATTRIB_PRIMITIVE, GU_MotionClipNames::blendshape_name, 1);
    GA_PrimitiveGroup *hidden_group
            = detail.newPrimitiveGroup(GA_Names::_3d_hidden_primitives);
    GA_RWHandleS inbetween_name_attrib;
    GA_RWHandleD inbetween_weight_attrib;
    bool has_inbetweens = false;
    exint j = 0;

    // Add packed primitives for each shape.
    for (exint i = 0, n = blendshapes.entries(); i < n; ++i)
    {
        const UsdSkelBlendShape &blendshape = blendshapes[i];

        UT_StringHolder channel_name
                = GusdUSD_Utils::TokenToStringHolder(channel_names[i]);
        SdfPath path
                = blendshape.GetPrim().GetPath().MakeRelativePath(root_path);
        const UT_StringHolder shape_name = path.GetAsString();

        const GU_DetailHandle &shape_gdh = shape_details[i];

        auto packed = GU_PackedGeometry::packGeometry(detail, shape_gdh);
        const GA_Offset primoff = packed->getMapOffset();

        channel_attrib.set(primoff, channel_name);
        shape_name_attrib.set(primoff, shape_name);
        hidden_group->addOffset(primoff);
        if (blendshape.GetInbetweens().size() > 0)
        {
            if (!has_inbetweens)
            {
                has_inbetweens = true;
                inbetween_name_attrib = detail.addStringTuple(
                        GA_ATTRIB_PRIMITIVE,
                        GU_MotionClipNames::blendshape_inbetween_name, 1);
                inbetween_weight_attrib = detail.addFloatTuple(
                        GA_ATTRIB_PRIMITIVE,
                        GU_MotionClipNames::blendshape_inbetween_weight, 1);
            }
            
            inbetween_weight_attrib.set(primoff, 1.0);
        }

        // Add in-between shapes
        for (const UsdSkelInbetweenShape &inbetween :
             blendshape.GetInbetweens())
        {
            float weight = 0;
            inbetween.GetWeight(&weight);

            const GU_DetailHandle &inbetween_gdh = inbetween_details[j];

            auto inbetween_packed = GU_PackedGeometry::packGeometry(detail, inbetween_gdh);
            const GA_Offset inbetween_primoff = inbetween_packed->getMapOffset();

            channel_attrib.set(inbetween_primoff, channel_name);
            shape_name_attrib.set(inbetween_primoff, shape_name);
            hidden_group->addOffset(inbetween_primoff);
            inbetween_name_attrib.set(
                    inbetween_primoff, xusdGetInBetweenPath(path, inbetween));
            inbetween_weight_attrib.set(inbetween_primoff, weight);

            ++j;
        }
    }

    return true;
}

GU_DetailHandle
XUSDimportSkinnedPrim(
        const GusdSkinImportParms &parms,
        const UsdSkelSkinningQuery &skinning_query,
        const VtTokenArray &joint_names,
        const VtMatrix4dArray &inv_bind_transforms,
        const SdfPath &root_path,
        const UT_StringHolder &shape_attrib)
{
    GU_DetailHandle gdh;
    gdh.allocateAndSet(new GU_Detail);
    GU_Detail *gdp = gdh.gdpNC();
    GU_Detail *skin_gdp = gdp;

    // Rigidly deformed shapes will be imported as a packed
    // primitive, unless they have blendshapes.
    GU_DetailHandle packed_gdh;
    bool rigidly_deformed = skinning_query.HasJointInfluences()
                            && skinning_query.IsRigidlyDeformed()
                            && !skinning_query.HasBlendShapes();
    if (rigidly_deformed)
    {
        packed_gdh.allocateAndSet(new GU_Detail);
        skin_gdp = packed_gdh.gdpNC();
    }

    // Import the geometry.
    UT_WorkBuffer primvar_pattern;
    primvar_pattern.append("* ^skel:geomBindTransform ^skel:skinningMethod");
    if (!skinning_query.HasJointInfluences()
        || skinning_query.IsRigidlyDeformed())
    {
        primvar_pattern.append(" ^skel:jointIndices ^skel:jointWeights");
    }

    // skel:geomBindTransform only applies if there is skinning.
    UT_Matrix4D geom_bind_xform(1.0);
    if (skinning_query.HasJointInfluences())
    {
        geom_bind_xform
                = GusdUT_Gf::Cast(skinning_query.GetGeomBindTransform());
    }

    if (!GusdGU_USD::ImportPrimUnpacked(
                *skin_gdp, skinning_query.GetPrim(), parms.myTime, parms.myLOD,
                parms.myPurpose, primvar_pattern.buffer(),
                UT_StringHolder::theEmptyString, true,
                UT_StringHolder::theEmptyString, &geom_bind_xform,
                parms.myRefineParms))
    {
        UT_WorkBuffer msg;
        msg.format(
                "Failed to unpack prim '{0}'.",
                skinning_query.GetPrim().GetPath().GetString());
        HUSD_ErrorScope::addWarning(HUSD_ERR_STRING, msg.buffer());
        return GU_DetailHandle();
    }

    // Import blendshape inputs.
    if (skinning_query.HasBlendShapes()
        && !XUSDimportBlendShapes(
                *skin_gdp, skinning_query, root_path, geom_bind_xform))
    {
        UT_WorkBuffer msg;
        msg.format(
                "Failed to import blendshapes for '{0}'.",
                skinning_query.GetPrim().GetPath().GetString());
        HUSD_ErrorScope::addWarning(HUSD_ERR_STRING, msg.buffer());
    }

    // Create the shapename attribute.
    SdfPath path = skinning_query.GetPrim().GetPath();
    UT_StringHolder shape_name = path.MakeRelativePath(root_path).GetString();
    if (shape_attrib)
    {
        GA_RWBatchHandleS shape_attrib_h = skin_gdp->addStringTuple(
                GA_ATTRIB_PRIMITIVE, shape_attrib, 1);
        shape_attrib_h.set(skin_gdp->getPrimitiveRange(), shape_name);
    }

    // Create a packed primitive for rigidly deformed shapes.
    if (skinning_query.IsRigidlyDeformed())
    {
        GU_PrimPacked *packed_prim = GU_PackedGeometry::packGeometry(
                *gdp, packed_gdh);

        // Also add the name and usdprimpath attribs on the outer
        // packed prim.
        GA_RWHandleS packed_shapeattrib = gdp->addStringTuple(
                GA_ATTRIB_PRIMITIVE, shape_attrib, 1);
        packed_shapeattrib.set(packed_prim->getMapOffset(), shape_name);

        GA_RWHandleS prim_path_attr = gdp->addStringTuple(
                GA_ATTRIB_PRIMITIVE, GUSD_PRIMPATH_ATTR, 1);
        prim_path_attr.set(packed_prim->getMapOffset(), path.GetString());
    }

    // Set up the boneCapture attribute on the shape geometry or
    // packed primitive.
    if (skinning_query.HasJointInfluences())
    {
        if (!GusdCreateCaptureAttribute(
                    *gdp, skinning_query, joint_names, inv_bind_transforms))
        {
            UT_WorkBuffer msg;
            msg.format(
                    "Failed to import boneCapture attribute for "
                    "'{0}'.",
                    skinning_query.GetPrim().GetPath().GetString());
            HUSD_ErrorScope::addWarning(HUSD_ERR_STRING, msg.buffer());
        }

        // Record the skinning method as an attribute for the Joint
        // Deform SOP, to use with the "From Input Geometry" method.
        GA_RWHandleS skinning_method_h = gdp->addStringTuple(
                GA_ATTRIB_DETAIL, GEO_STD_ATTRIB_DEFORM_SKIN_METHOD, 1);

        UT_StringHolder method;
        if (skinning_query.GetSkinningMethod() == UsdSkelTokens->dualQuaternion)
        {
            method = GU_LinearSkinDeformer::SKIN_DUAL_QUATERNION;
        }
        else
            method = GU_LinearSkinDeformer::SKIN_LINEAR;

        skinning_method_h.set(GA_DETAIL_OFFSET, method);
    }

    return gdh;
}

bool
XUSDimportAgentBlendShapes(
        GU_Detail &base_shape,
        UT_Array<GU_DetailHandle> &all_shape_details,
        UT_StringArray &all_shape_names,
        const UsdSkelSkinningQuery &skinning_query,
        const SdfPath &root_path,
        const UT_Matrix4D &geom_bind_xform)
{
    VtTokenArray channel_names_attr;
    UT_Array<UsdSkelBlendShape> blendshapes;
    if (!xusdFindBlendShapes(skinning_query, channel_names_attr, blendshapes))
        return false;

    UT_StringArray shape_names;
    shape_names.setCapacity(blendshapes.size());
    UT_StringArray channel_names;
    channel_names.setCapacity(blendshapes.size());

    UT_StringArray inbetween_names;
    UT_Array<fpreal> inbetween_weights;

    for (exint i = 0, n = blendshapes.entries(); i < n; ++i)
    {
        const UsdSkelBlendShape &blendshape = blendshapes[i];

        channel_names.append(channel_names_attr[i].GetString());

        SdfPath path =
            blendshape.GetPrim().GetPath().MakeRelativePath(root_path);
        UT_StringHolder name = path.GetAsString();
        shape_names.append(name);

        GU_DetailHandle gdh;
        gdh.allocateAndSet(new GU_Detail());

        GU_DetailHandleAutoWriteLock detail(gdh);
        if (!xusdImportBlendShape(
                    *detail, blendshape, blendshape, base_shape,
                    geom_bind_xform))
        {
            UT_WorkBuffer msg;
            msg.format("Failed to import blendshape '{}'",
                       blendshape.GetPath().GetString());
            HUSD_ErrorScope::addError(HUSD_ERR_STRING, msg.buffer());
            return false;
        }

        all_shape_details.append(gdh);
        all_shape_names.append(name);

        // Import in-between shapes.
        inbetween_names.clear();
        inbetween_weights.clear();
        for (const UsdSkelInbetweenShape &inbetween :
             blendshape.GetInbetweens())
        {
            float weight = 0;
            if (!inbetween.GetWeight(&weight))
            {
                HUSD_ErrorScope::addError(
                    HUSD_ERR_STRING,
                    "Weight is not authored for in-between shape");
                return false;
            }

            GU_DetailHandle inbetween_gdh;
            inbetween_gdh.allocateAndSet(new GU_Detail());
            GU_DetailHandleAutoWriteLock inbetween_detail(inbetween_gdh);

            if (!xusdImportBlendShape(
                        *inbetween_detail, inbetween, blendshape, base_shape,
                        geom_bind_xform))
            {
                UT_WorkBuffer msg;
                msg.format("Failed to import in-between '{}' for '{}'",
                           inbetween.GetAttr().GetName().GetString(),
                           blendshape.GetPath().GetString());
                HUSD_ErrorScope::addError(HUSD_ERR_STRING, msg.buffer());
                return false;
            }

            all_shape_names.append(xusdGetInBetweenPath(path, inbetween));
            all_shape_details.append(inbetween_gdh);

            inbetween_names.append(all_shape_names.last());
            inbetween_weights.append(weight);
        }

        GU_AgentBlendShapeUtils::addInBetweenShapes(
            *detail, inbetween_names, inbetween_weights);
    }

    // Record the blendshape inputs as detail attributes on the base shape.
    GU_AgentBlendShapeUtils::addInputsToBaseShape(
        base_shape, shape_names, channel_names);

    return true;
}

PXR_NAMESPACE_CLOSE_SCOPE
