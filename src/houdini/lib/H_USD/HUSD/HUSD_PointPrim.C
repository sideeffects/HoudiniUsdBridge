/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
 *
 * Produced by:
 *	Side Effects Software Inc.
 *	123 Front Street West, Suite 1401
 *	Toronto, Ontario
 *      Canada   M5J 2M2
 *	416-504-9876
 *
 */

#include "HUSD_PointPrim.h"
#include <HUSD/HUSD_Constants.h>
#include <HUSD/HUSD_GetAttributes.h>
#include <HUSD/HUSD_SetAttributes.h>
#include <HUSD/HUSD_Info.h>
#include <HUSD/XUSD_AttributeUtils.h>
#include <HUSD/XUSD_Data.h>
#include <HUSD/XUSD_Utils.h>
#include <GA/GA_AIFTuple.h>
#include <GA/GA_AIFNumericArray.h>
#include <GA/GA_ATINumericArray.h>
#include <GA/GA_ATIStringArray.h>
#include <UT/UT_ArrayStringSet.h>
#include <UT/UT_Quaternion.h>
#include <UT/UT_Matrix4.h>
#include <pxr/usd/usdGeom/pointBased.h>
#include <pxr/usd/usdGeom/pointInstancer.h>
#include <pxr/usd/usdLux/light.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace
{
    template<typename uttype>
    bool
    husdScatterArrayAttribute(
	    UsdStageRefPtr &stage,
	    HUSD_GetAttributes &getattrs,
	    HUSD_SetAttributes &setattrs,
	    const UT_StringRef &sourceprimpath,
	    const UsdAttribute &attrib,
	    const HUSD_TimeCode &timecode,
	    const UT_StringArray &targetprimpaths)
    {
	UT_StringHolder attribname(attrib.GetName());
	UT_Array<uttype> values;

	if (!getattrs.getAttributeArray(
                sourceprimpath, attribname, values, timecode))
	    return false;

	exint count = SYSmin(targetprimpaths.size(), values.size());
	UT_StringHolder valuetype(attrib.GetTypeName().GetAsToken().GetText());

	// For now just assume that the array primvar & attributes are
	// written to single-value attributes on the target primitives.
	//
	// This already covers many uses cases, like writing to standard light
	// attributes.
	attribname.substitute("primvars:", "");
	valuetype.substitute("[]", "");

	UT_String    tempname;

	for (int i = 0; i < count; ++i)
	{
	    tempname.harden(attribname);

	    auto primpath = targetprimpaths[i];
	    auto sdfpath = HUSDgetSdfPath(primpath);
	    auto prim = stage->GetPrimAtPath(sdfpath);

	    if (!prim)
		continue;

	    if (prim.IsA<UsdLuxLight>())
		tempname.substitute("displayColor", "color");

	    if (!setattrs.setAttribute(
			primpath, tempname,
			values[i], timecode, valuetype))
		return false;
	}

	return true;
    }

    template<typename uttype>
    bool
    husdScatterSopArrayAttribute(
	    UsdStageRefPtr &stage,
	    const GA_Attribute *attrib,
	    const GA_PointGroup *group,
	    HUSD_SetAttributes &setattrs,
	    const HUSD_TimeCode &timecode,
	    const UT_StringArray &targetprimpaths,
	    const UT_StringRef &valuetype = UT_String::getEmptyString())
    {
	GA_ROHandleT<uttype>	 handle(attrib);
	GA_Offset		 start, end;
	UT_Array<uttype>	 valarray(1, 1);
	exint                    i = 0;

	auto range = attrib->getDetail().getPointRange(group);
	for (GA_Iterator it(range); it.blockAdvance(start, end);)
	{
	    for (GA_Offset ptoff = start; ptoff < end; ++ptoff)
	    {
		UT_StringHolder myvaluetype(valuetype);
                auto primpath = targetprimpaths[i++];
		auto sdfpath = HUSDgetSdfPath(primpath);
		auto prim = stage->GetPrimAtPath(sdfpath);

                if (!prim)
                    continue;

                UT_StringHolder name = attrib->getName();
		bool islight = prim.IsA<UsdLuxLight>();
		bool isarray = valuetype.endsWith("[]");
                bool isprimvar = false;

		if (name.equal("Cd"))
		{
		    if (islight)
		    {
			name = "color";
			isarray = false;
			myvaluetype.substitute("[]", "");
		    }
		    else
                    {
			name = "displayColor";
                        isprimvar = true;
                        UT_ASSERT(isarray);
                    }
		}
                else
                {
                    // If the SOP attribute name matches an existing USD
                    // attribute name, then we want to set that attribute.
                    // Otherwise we want to create a primvar. We always
                    // create primvars with array values.
                    isprimvar = !prim.HasAttribute(TfToken(name.toStdString()));
                    if (isprimvar && !isarray)
                    {
                        isarray = true;
                        if (myvaluetype.isstring())
                            myvaluetype += "[]";
                    }
                }

		if (isarray)
		{
		    // if setting the value of an array attribute, make
		    // the value a single-element array.
		    valarray[0] = handle.get(ptoff);
		    if (isprimvar)
		    {
			if (!setattrs.setPrimvarArray(
				    primpath, name,
				    HUSD_Constants::getInterpolationConstant(),
				    valarray,
				    timecode, myvaluetype))
			    return false;
		    }
		    else
		    {
			if (!setattrs.setAttributeArray(
				    primpath, name,
				    valarray,
				    timecode, myvaluetype))
			    return false;
		    }
		}
		else
		{
		    if (isprimvar)
		    {
			if (!setattrs.setPrimvar(
				    primpath, name,
				    HUSD_Constants::getInterpolationConstant(),
				    handle.get(ptoff),
				    timecode, myvaluetype))
			    return false;
		    }
		    else
		    {
			if (!setattrs.setAttribute(
				    primpath, name,
				    handle.get(ptoff),
				    timecode, myvaluetype))
			    return false;
		    }
		}
	    }
	}

	return true;
    }

    template<>
    bool
    husdScatterSopArrayAttribute<UT_StringHolder>(
	    UsdStageRefPtr &stage,
	    const GA_Attribute *attrib,
	    const GA_PointGroup *group,
	    HUSD_SetAttributes &setattrs,
	    const HUSD_TimeCode &timecode,
	    const UT_StringArray &targetprimpaths,
	    const UT_StringRef &valuetype)
    {
	GA_ROHandleS		 handle(attrib);
	GA_Offset		 start, end;
	exint                    i = 0;

	auto range = attrib->getDetail().getPointRange(group);
	for (GA_Iterator it(range); it.blockAdvance(start, end);)
	{
	    for (GA_Offset ptoff = start; ptoff < end; ++ptoff)
	    {
		UT_StringHolder		 myvaluetype(valuetype);

                auto primpath = targetprimpaths[i++];
		auto sdfpath = HUSDgetSdfPath(primpath);

		if (!setattrs.setAttribute(
			    primpath, attrib->getName(),
			    handle.get(ptoff),
			    timecode, myvaluetype))
		    return false;
	    }
	}

	return true;
    }

    template<typename ArrayType>
    bool
    husdScatterSopArrayOfArrayAttribute(
	    UsdStageRefPtr &stage,
	    const GA_Attribute *attrib,
	    const GA_PointGroup *group,
	    HUSD_SetAttributes &setattrs,
	    const HUSD_TimeCode &timecode,
	    const UT_StringArray &targetprimpaths,
	    const UT_StringRef &valuetype = UT_String::getEmptyString())
    {
        // Convert an array attribute into two primvars: a list of lengths with
        // the appropriate interpolation (constant in this case), and a
        // constant array with the concatenated values. This matches the SOP
        // Import LOP's behavior.
        GA_ROHandleT<ArrayType> handle(attrib);
        const int elementsize = attrib->getTupleSize();
        ArrayType val;
        UT_Array<int32> lengths;
        lengths.setSize(1);

        auto range = attrib->getDetail().getPointRange(group);
        GA_Offset start, end;
        exint i = 0;

        UT_WorkBuffer primvar_name;
        for (GA_Iterator it(range); it.blockAdvance(start, end);)
        {
            for (GA_Offset ptoff = start; ptoff < end; ++ptoff)
            {
                val.clear();
                handle.get(ptoff, val);

                exint len = val.entries();
                if (elementsize > 1)
                    len /= elementsize;
                lengths[0] = len;

                auto primpath = targetprimpaths[i++];
                auto sdfpath = HUSDgetSdfPath(primpath);

                primvar_name.format("primvars:{}", attrib->getName());
                if (!setattrs.setPrimvarArray(
                        primpath, primvar_name,
                        HUSD_Constants::getInterpolationConstant(), val,
                        timecode, valuetype, elementsize))
                {
                    return false;
                }

                primvar_name.append(":lengths");
                if (!setattrs.setPrimvarArray(
                        primpath, primvar_name,
                        HUSD_Constants::getInterpolationConstant(), lengths,
                        timecode, valuetype, elementsize))
                {
                    return false;
                }
            }
        }

        return true;
    }

    template<typename uttype>
    void
    husdGetArrayAttribValues(
	    const GA_Attribute *attrib,
	    const GA_PointGroup *group,
	    UT_Array<uttype> &values)
    {
	GA_ROHandleT<uttype>	 handle(attrib);
	GA_Offset		 start, end;
	exint                    i = 0;

	auto range = attrib->getDetail().getPointRange(group);

	values.setSize(range.getEntries());

	for (GA_Iterator it(range); it.blockAdvance(start, end);)
	{
	    for (GA_Offset ptoff = start; ptoff < end; ++ptoff)
	    {
		values[i++] = handle.get(ptoff);
	    }
	}
    }

    void
    husdGetArrayAttribValues(
	    const GA_Attribute *attrib,
	    const GA_PointGroup *group,
	    UT_Array<UT_StringHolder> &values)
    {
	GA_ROHandleS		 handle(attrib);
	GA_Offset		 start, end;
	exint                    i = 0;

	auto range = attrib->getDetail().getPointRange(group);

	values.setSize(range.getEntries());

	for (GA_Iterator it(range); it.blockAdvance(start, end);)
	{
	    for (GA_Offset ptoff = start; ptoff < end; ++ptoff)
	    {
		values[i++] = handle.get(ptoff);
	    }
	}
    }

    template<typename uttype>
    bool
    husdCopySopArrayAttribute(
	    UsdStageRefPtr &stage,
	    const GA_Attribute *attrib,
	    const GA_PointGroup *group,
	    HUSD_SetAttributes &setattrs,
	    const HUSD_TimeCode &timecode,
	    const UT_StringRef &targetprimpath,
	    const UT_StringRef &valuetype = UT_String::getEmptyString())
    {
	UT_Array<uttype> values;
	husdGetArrayAttribValues(attrib, group, values);

	UT_StringHolder primvarname = attrib->getName();
	if (primvarname.equal("Cd"))
	    primvarname = "displayColor";

	setattrs.setPrimvarArray(targetprimpath, primvarname,
				    HUSD_Constants::getInterpolationVarying(),
				    values, timecode, valuetype);

	return true;
    }

    template <typename ArrayType>
    void
    husdGetArrayOfArrayAttribValues(const GA_Attribute *attrib,
                                    const GA_PointGroup *group,
                                    ArrayType &values,
                                    UT_Array<int32> &lengths)
    {
        GA_ROHandleT<ArrayType> handle(attrib);
        const int elementsize = attrib->getTupleSize();

        auto range = attrib->getDetail().getPointRange(group);
        lengths.setCapacity(range.getEntries());

        ArrayType val;
        GA_Offset start, end;
        for (GA_Iterator it(range); it.blockAdvance(start, end);)
        {
            for (GA_Offset ptoff = start; ptoff < end; ++ptoff)
            {
                val.clear();
                handle.get(ptoff, val);
                values.concat(val);

                exint len = val.entries();
                if (elementsize > 1)
                    len /= elementsize;
                lengths.append(len);
            }
        }
    }

    template <typename ArrayType>
    bool
    husdCopySopArrayOfArraysAttribute(
        UsdStageRefPtr &stage, const GA_Attribute *attrib,
        const GA_PointGroup *group, HUSD_SetAttributes &setattrs,
        const HUSD_TimeCode &timecode, const UT_StringRef &targetprimpath,
        const UT_StringRef &valuetype = UT_String::getEmptyString())
    {
        // Convert an array attribute into two primvars: a list of lengths with
        // the appropriate interpolation, and a constant array with the
        // concatenated values. This matches the SOP Import LOP's behavior.
	ArrayType values;
        UT_Array<int32> lengths;
	husdGetArrayOfArrayAttribValues(attrib, group, values, lengths);

	UT_StringHolder primvarname;
        primvarname.format("primvars:{}", attrib->getName());

	UT_StringHolder lengthsname;
        lengthsname.format("{}:lengths", primvarname);

        const int elementsize = attrib->getTupleSize();
        setattrs.setPrimvarArray(targetprimpath, primvarname,
                                 HUSD_Constants::getInterpolationConstant(),
                                 values, timecode, valuetype, elementsize);
        setattrs.setPrimvarArray(targetprimpath, lengthsname,
                                 HUSD_Constants::getInterpolationVarying(),
                                 lengths, timecode, valuetype);

        return true;
    }
}

bool
HUSD_PointPrim::extractTransforms(HUSD_AutoAnyLock &readlock,
				const UT_StringRef &primpath,
				UT_Vector3FArray &positions,
				UT_Array<UT_QuaternionH> &orientations,
				UT_Vector3FArray &scales,
				const HUSD_TimeCode &timecode,
				bool doorient,
				bool doscale,
				const UT_Matrix4D *transform/*=nullptr*/)
{
    HUSD_GetAttributes	     getattrs(readlock);

    if (primpath.isstring())
    {
	if (readlock.constData() &&
	    readlock.constData()->isStageValid())
	{
	    SdfPath			 sdfpath(HUSDgetSdfPath(primpath));
	    bool			 hasorient = false;
	    bool			 hasscale = false;
	    bool			 haspscale = false;
	    UT_Vector3FArray		 tmppositions;
	    UT_Array<UT_QuaternionH>	 tmporientationsH;
	    UT_Array<UT_QuaternionF>	 tmporientationsF;
	    UT_FloatArray		 tmppscales;
	    UT_Vector3FArray		 tmpscales;
	    UT_QuaternionF		 tmprot;
	    UT_Matrix3F			 tmprotmatrix;

	    auto			 stage = readlock.constData()->stage();
	    auto			 prim = stage->GetPrimAtPath(sdfpath);

	    if (UsdGeomPointBased(prim))
	    {
		if (!getattrs.getAttributeArray(
			primpath, { HUSD_Constants::getAttributePoints() },
			tmppositions,
			timecode))
		    return false;

		if (doorient)
		{
		    hasorient = getattrs.getAttributeArray(
			    primpath,
			    { "primvars:orient" },
			    tmporientationsH,
			    timecode);
		    if (!hasorient)
		    {
			hasorient = getattrs.getAttributeArray(
				primpath,
				{ "primvars:orient" },
				tmporientationsF,
				timecode);
		    }

		}

		if (doscale)
		{
		    hasscale = getattrs.getAttributeArray(
			    primpath,
			    { "primvars:scale" },
			    tmpscales,
			    timecode);
		    haspscale = getattrs.getAttributeArray(
			    primpath,
			    { "primvars:pscale" },
			    tmppscales,
			    timecode);
		    if (!haspscale)
		    {
			haspscale = getattrs.getAttributeArray(
			    primpath,
			    { "widths" },
			    tmppscales,
			    timecode);
		    }
		}
	    }
	    else if (UsdGeomPointInstancer(prim))
	    {
		if (!getattrs.getAttributeArray(
			primpath,
			{ HUSD_Constants::getAttributePointPositions() },
			tmppositions,
			timecode))
		    return false;

		if (doorient)
		{
		    hasorient = getattrs.getAttributeArray(
			    primpath,
			    { HUSD_Constants::getAttributePointOrientations() },
			    tmporientationsH,
			    timecode);

		}

		if (doscale)
		{
		    hasscale = getattrs.getAttributeArray(
			    primpath,
			    { HUSD_Constants::getAttributePointScales() },
			    tmpscales,
			    timecode);
		}
	    }
	    else
	    {
		return false;
	    }

	    int outcount = positions.size();

	    positions.setSize(positions.size() + tmppositions.size());

	    if (doorient)
	    {
		orientations.setSize(
			orientations.size() + tmppositions.size());
	    }

	    if (doscale)
	    {
		scales.setSize(
			orientations.size() + tmppositions.size());
	    }

	    for (exint i = 0; i < tmppositions.size(); ++i)
	    {
		positions[outcount] = tmppositions[i];

		if (transform)
		    positions[outcount] *= *transform;

		if (doorient || doscale)
		{
		    if (transform)
		    {

			// Build a transform from orientation & scale. Extract
			// rotation and scale from transform Non-uniform scale
			// or shears from the primitive can not be represented
			// by the point instancer's transform model when points
			// are rotated off-axis.
			UT_Matrix3F pointtransform(1.0);
			if (hasscale) // implies doscale = true
			    pointtransform.scale(tmpscales[i]);
			if (haspscale)
			    pointtransform.scale(UT_Vector3(tmppscales[i]));

			if (hasorient) // implies doorient = true
			{
			    if (!tmporientationsH.isEmpty())
				tmporientationsH[i].getRotationMatrix(tmprotmatrix);
			    else
				tmporientationsF[i].getRotationMatrix(tmprotmatrix);
			    pointtransform *= tmprotmatrix;
			}

			pointtransform *= (UT_Matrix3F)(*transform);

			if (doorient)
			    orientations[outcount].updateFromArbitraryMatrix(
				    pointtransform);

			if (doscale)
			    pointtransform.extractScales(scales[outcount]);
		    }
		    else
		    {
			if (doorient)
			{
			    if (hasorient)
			    {
				if (!tmporientationsH.isEmpty())
				    orientations[outcount] = tmporientationsH[i];
				else
				    orientations[outcount] = tmporientationsF[i];
			    }
			    else
				orientations[outcount].identity();
			}

			if (doscale)
			{
			    scales[outcount] = UT_Vector3F(1.0);
			    if (hasscale)
				scales[outcount] = tmpscales[i];
			    if (haspscale)
				scales[outcount] *= tmppscales[i];
			}
		    }
		}

		outcount++;
	    }
	    return true;
	}
    }

    return false;
}

bool
HUSD_PointPrim::extractTransforms(HUSD_AutoAnyLock &readlock,
				const UT_StringRef &primpath,
				UT_Matrix4DArray &xforms,
				const HUSD_TimeCode &timecode,
				bool doorient,
				bool doscale,
				const UT_Matrix4D *transform)
{
    UT_Matrix3F			 tmprotmatrix;
    UT_Vector3FArray		 positions;
    UT_Array<UT_QuaternionH>	 orientations;
    UT_Vector3FArray		 scales;

    if (!extractTransforms(
	    readlock,
	    primpath,
	    positions,
	    orientations,
	    scales,
	    timecode,
	    doorient,
	    doscale,
	    transform))
	return false;

    xforms.setSize(positions.size());

    for (int i = 0; i < positions.size(); ++i)
    {
	xforms[i].identity();

	if (doscale && scales.size() > 0 )
	    xforms[i].scale(scales[i]);

	if (doorient && orientations.size() > 0)
	{
	    orientations[i].getRotationMatrix(tmprotmatrix);
	    xforms[i] *= tmprotmatrix;
	}

	xforms[i].translate(positions[i]);
    }

    return true;
}

bool
HUSD_PointPrim::transformInstances(HUSD_AutoWriteLock &writelock,
				const UT_StringRef &primpath,
				const UT_IntArray &indices,
				const UT_Array<UT_Matrix4D> &xforms,
				const HUSD_TimeCode &timecode)
{
    HUSD_GetAttributes	     getattrs(writelock);
    HUSD_SetAttributes	     setattrs(writelock);

    if (primpath.isstring())
    {
	if (writelock.data() &&
	    writelock.data()->isStageValid())
	{
	    SdfPath			 sdfpath(HUSDgetSdfPath(primpath));
	    bool			 hasorient = false;
	    bool			 hasscale = false;
	    UT_Vector3FArray		 positions;
	    UT_Array<UT_QuaternionH>	 orientations;
	    UT_Vector3FArray		 scales;
	    UT_QuaternionF		 tmprot;
	    UT_Matrix3F			 tmprotmatrix;

	    auto			 stage = writelock.data()->stage();
	    auto			 prim = stage->GetPrimAtPath(sdfpath);

	    if (!UsdGeomPointInstancer(prim))
		return false;

	    if (!getattrs.getAttributeArray(
		    primpath,
		    { HUSD_Constants::getAttributePointPositions() },
		    positions,
		    timecode))
		return false;

	    hasorient = getattrs.getAttributeArray(
		    primpath,
		    { HUSD_Constants::getAttributePointOrientations() },
		    orientations,
		    timecode);

	    hasscale = getattrs.getAttributeArray(
		    primpath,
		    { HUSD_Constants::getAttributePointScales() },
		    scales,
		    timecode);

	    if (!hasscale)
	    {
		scales.setSize(positions.size());
		for (int i = 0; i < scales.size(); ++i)
		    scales[i] = UT_Vector3F(1.0);
	    }

	    if (!hasorient)
	    {
		orientations.setSize(positions.size());
		for (int i = 0; i < orientations.size(); ++i)
		    orientations[i].identity();
	    }

	    UT_Matrix4D pointxform;
	    for (int i = 0 ; i < indices.size(); ++i)
	    {
		int index = indices[i];

		pointxform.identity();
		if (hasscale)
		    pointxform.scale(scales[index]);

		if (hasorient)
		{
		    orientations[index].getRotationMatrix(tmprotmatrix);
		    pointxform *= tmprotmatrix;
		}

		pointxform.translate(positions[index]);

		pointxform = xforms[i] * pointxform;

		orientations[index].updateFromArbitraryMatrix(
			UT_Matrix3D(pointxform));

		UT_Matrix3D(pointxform).extractScales(scales[index]);

		pointxform.getTranslates(positions[index]);
	    }

	    if (!setattrs.setAttributeArray(
		    primpath,
		    { HUSD_Constants::getAttributePointPositions() },
		    positions,
		    timecode))
		return false;

	    if (!setattrs.setAttributeArray(
		    primpath,
		    { HUSD_Constants::getAttributePointOrientations() },
		    orientations,
		    timecode))
		return false;

	    if (!setattrs.setAttributeArray(
		    primpath,
		    { HUSD_Constants::getAttributePointScales() },
		    scales,
		    timecode))
		return false;
	}
    }

    return false;
}

bool
HUSD_PointPrim::scatterArrayAttributes(HUSD_AutoWriteLock &writelock,
				const UT_StringRef &primpath,
				const UT_ArrayStringSet &attribnames,
			        const HUSD_TimeCode &timecode,
				const UT_StringArray &targetprimpaths)
{
    HUSD_GetAttributes	     getattrs(writelock);
    HUSD_SetAttributes	     setattrs(writelock);

    if (primpath.isstring())
    {
	if (writelock.constData() &&
	    writelock.constData()->isStageValid())
	{
	    auto		 stage = writelock.constData()->stage();
	    auto		 sdfpath = HUSDgetSdfPath(primpath);
	    auto		 prim = stage->GetPrimAtPath(sdfpath);

	    for (auto &&attribname : attribnames)
	    {
		auto &&attrib = prim.GetAttribute(TfToken(attribname.c_str()));

		if (!attrib.IsValid())
		    continue;

		if (husdScatterArrayAttribute<float>(stage, getattrs,
			setattrs, primpath, attrib, timecode, targetprimpaths))
		    continue;

		if (husdScatterArrayAttribute<UT_Vector2F>(stage, getattrs,
			setattrs, primpath, attrib, timecode, targetprimpaths))
		    continue;

		if (husdScatterArrayAttribute<UT_Vector3F>(stage, getattrs,
			setattrs, primpath, attrib, timecode, targetprimpaths))
		    continue;

		if (husdScatterArrayAttribute<UT_Vector4F>(stage, getattrs,
			setattrs, primpath, attrib, timecode, targetprimpaths))
		    continue;

		if (husdScatterArrayAttribute<UT_QuaternionF>(stage, getattrs,
			setattrs, primpath, attrib, timecode, targetprimpaths))
		    continue;

		if (husdScatterArrayAttribute<UT_QuaternionH>(stage, getattrs,
			setattrs, primpath, attrib, timecode, targetprimpaths))
		    continue;

		if (husdScatterArrayAttribute<UT_Matrix3D>(stage, getattrs,
			setattrs, primpath, attrib, timecode, targetprimpaths))
		    continue;

		if (husdScatterArrayAttribute<UT_Matrix4D>(stage, getattrs,
			setattrs, primpath, attrib, timecode, targetprimpaths))
		    continue;

		if (husdScatterArrayAttribute<bool>(stage, getattrs,
			setattrs, primpath, attrib, timecode, targetprimpaths))
		    continue;

		if (husdScatterArrayAttribute<int>(stage, getattrs,
			setattrs, primpath, attrib, timecode, targetprimpaths))
		    continue;

		if (husdScatterArrayAttribute<int64>(stage, getattrs,
			setattrs, primpath, attrib, timecode, targetprimpaths))
		    continue;

		if (husdScatterArrayAttribute<UT_Vector2i>(stage, getattrs,
			setattrs, primpath, attrib, timecode, targetprimpaths))
		    continue;

		if (husdScatterArrayAttribute<UT_Vector3i>(stage, getattrs,
			setattrs, primpath, attrib, timecode, targetprimpaths))
		    continue;

		if (husdScatterArrayAttribute<UT_Vector4i>(stage, getattrs,
			setattrs, primpath, attrib, timecode, targetprimpaths))
		    continue;

		if (husdScatterArrayAttribute<UT_StringHolder>(stage, getattrs,
			setattrs, primpath, attrib, timecode, targetprimpaths))
		    continue;
	    }

	    return true;
	}
    }

    return false;
}

bool
HUSD_PointPrim::scatterSopArrayAttributes(HUSD_AutoWriteLock &writelock,
				const GU_Detail *gdp,
				const GA_PointGroup *group,
				const UT_Array<const GA_Attribute*> &attribs,
			        const HUSD_TimeCode &timecode,
				const UT_StringArray &targetprimpaths)
{
    HUSD_SetAttributes	     setattrs(writelock);

    if (gdp == nullptr)
	return false;

    if (writelock.constData() &&
	writelock.constData()->isStageValid())
    {
	auto		 stage = writelock.constData()->stage();

	for (const GA_Attribute *attrib : attribs)
	{
	    int	                 tuplesize = attrib->getTupleSize();
	    GA_TypeInfo          typeinfo = attrib->getTypeInfo();
	    GA_StorageClass      storageclass = attrib->getStorageClass();
	    const GA_AIFTuple   *tuple = attrib->getAIFTuple();
	    const GA_AIFNumericArray *num_array = attrib->getAIFNumericArray();
	    GA_Storage           storage = GA_STORE_INVALID;
            const bool is_array_attrib = GA_ATINumericArray::isType(attrib) ||
                                         GA_ATIStringArray::isType(attrib);

	    if (tuple)
		storage = tuple->getStorage(attrib);
            else if (num_array)
                storage = num_array->getStorage(attrib);

	    if (tuplesize == 3 && typeinfo == GA_TYPE_COLOR)
	    {
		if (husdScatterSopArrayAttribute<UT_Vector3F>(
			stage, attrib, group, setattrs, timecode,
			targetprimpaths, "color3f[]"))
		    continue;
	    }
	    else if (storageclass == GA_STORECLASS_REAL)
	    {
		if (storage == GA_STORE_REAL32)
		{
                    if (is_array_attrib)
                    {
			if (husdScatterSopArrayOfArrayAttribute<UT_Fpreal32Array>(
				stage, attrib, group, setattrs, timecode,
				targetprimpaths))
			    continue;
                    }
		    else if (tuplesize == 16)
		    {
			if (husdScatterSopArrayAttribute<UT_Matrix4F>(
				stage, attrib, group, setattrs, timecode,
				targetprimpaths))
			    continue;
		    }
		    else if (tuplesize == 9)
		    {
			if (husdScatterSopArrayAttribute<UT_Matrix3F>(
				stage, attrib, group, setattrs, timecode,
				targetprimpaths))
			    continue;
		    }
		    else if (tuplesize == 4 && typeinfo == GA_TYPE_QUATERNION)
		    {
			if (husdScatterSopArrayAttribute<UT_QuaternionF>(
				stage, attrib, group, setattrs, timecode,
				targetprimpaths))
			    continue;
		    }
		    else if (tuplesize == 4)
		    {
			if (husdScatterSopArrayAttribute<UT_Vector4F>(
				stage, attrib, group, setattrs, timecode,
				targetprimpaths))
			    continue;
		    }
		    else if (tuplesize == 3)
		    {
			if (husdScatterSopArrayAttribute<UT_Vector3F>(
				stage, attrib, group, setattrs, timecode,
				targetprimpaths))
			    continue;
		    }
		    else if (tuplesize == 2)
		    {
			if (husdScatterSopArrayAttribute<UT_Vector2F>(
				stage, attrib, group, setattrs, timecode,
				targetprimpaths))
			    continue;
		    }
		    else if (tuplesize == 1)
		    {
			if (husdScatterSopArrayAttribute<fpreal32>(
				stage, attrib, group, setattrs, timecode,
				targetprimpaths))
			    continue;
		    }
		}
		else if (storage == GA_STORE_REAL64)
		{
                    if (is_array_attrib)
                    {
			if (husdScatterSopArrayOfArrayAttribute<UT_Fpreal64Array>(
				stage, attrib, group, setattrs, timecode,
				targetprimpaths))
			    continue;
                    }
                    else if (tuplesize == 16)
		    {
			if (husdScatterSopArrayAttribute<UT_Matrix4D>(
				stage, attrib, group, setattrs, timecode,
				targetprimpaths))
			    continue;
		    }
		    else if (tuplesize == 9)
		    {
			if (husdScatterSopArrayAttribute<UT_Matrix3D>(
				stage, attrib, group, setattrs, timecode,
				targetprimpaths))
			    continue;
		    }
		    else if (tuplesize == 4 && typeinfo == GA_TYPE_QUATERNION)
		    {
			if (husdScatterSopArrayAttribute<UT_QuaternionD>(
				stage, attrib, group, setattrs, timecode,
				targetprimpaths))
			    continue;
		    }
		    else if (tuplesize == 4)
		    {
			if (husdScatterSopArrayAttribute<UT_Vector4D>(
				stage, attrib, group, setattrs, timecode,
				targetprimpaths))
			    continue;
		    }
		    else if (tuplesize == 3)
		    {
			if (husdScatterSopArrayAttribute<UT_Vector3D>(
				stage, attrib, group, setattrs, timecode,
				targetprimpaths))
			    continue;
		    }
		    else if (tuplesize == 2)
		    {
			if (husdScatterSopArrayAttribute<UT_Vector2D>(
				stage, attrib, group, setattrs, timecode,
				targetprimpaths))
			    continue;
		    }
		    else if (tuplesize == 1)
		    {
			if (husdScatterSopArrayAttribute<fpreal64>(
				stage, attrib, group, setattrs, timecode,
				targetprimpaths))
			    continue;
		    }
		}
	    }
	    else if (storageclass == GA_STORECLASS_INT)
	    {
		if (storage == GA_STORE_INT32)
		{
                    if (is_array_attrib)
                    {
			if (husdScatterSopArrayOfArrayAttribute<UT_Int32Array>(
				stage, attrib, group, setattrs, timecode,
				targetprimpaths))
			    continue;
                    }
		    else if (tuplesize == 4)
		    {
			if (husdScatterSopArrayAttribute<UT_Vector4i>(
			    stage, attrib, group, setattrs, timecode,
			    targetprimpaths))
			continue;
		    }
		    if (tuplesize == 3)
		    {
			if (husdScatterSopArrayAttribute<UT_Vector3i>(
			    stage, attrib, group, setattrs, timecode,
			    targetprimpaths))
			continue;
		    }
		    else if (tuplesize == 2)
		    {
			if (husdScatterSopArrayAttribute<UT_Vector2i>(
			    stage, attrib, group, setattrs, timecode,
			    targetprimpaths))
			continue;
		    }
		    else if (tuplesize == 1)
		    {
			if (husdScatterSopArrayAttribute<int>(
			    stage, attrib, group, setattrs, timecode,
			    targetprimpaths))
			continue;
		    }
		}
		else if (storage == GA_STORE_INT64)
		{
                    if (is_array_attrib)
                    {
			if (husdScatterSopArrayOfArrayAttribute<UT_Int64Array>(
				stage, attrib, group, setattrs, timecode,
				targetprimpaths))
			    continue;
                    }
		    else if (tuplesize == 1)
		    {
			if (husdScatterSopArrayAttribute<int64>(
			    stage, attrib, group, setattrs, timecode,
			    targetprimpaths))
			continue;
		    }
		}
	    }
	    else if (storageclass == GA_STORECLASS_STRING)
	    {
                if (is_array_attrib)
                {
                    if (husdScatterSopArrayOfArrayAttribute<UT_StringArray>(
                            stage, attrib, group, setattrs, timecode,
                            targetprimpaths))
                        continue;
                }
                else if (tuplesize == 1)
		{
		    if (husdScatterSopArrayAttribute<UT_StringHolder>(
                            stage, attrib, group, setattrs, timecode,
                            targetprimpaths))
			continue;
		}
	    }
	}
	return true;
    }

    return false;
}

bool
HUSD_PointPrim::copySopArrayAttributes(HUSD_AutoWriteLock &writelock,
				const GU_Detail *gdp,
				const GA_PointGroup *group,
				const UT_Array<const GA_Attribute*> &attribs,
			        const HUSD_TimeCode &timecode,
				const UT_StringRef &targetprimpath)
{
    HUSD_SetAttributes	     setattrs(writelock);

    if (gdp == nullptr)
	return false;

    if (writelock.constData() &&
	writelock.constData()->isStageValid())
    {
	auto		 stage = writelock.constData()->stage();

	for (const GA_Attribute *attrib : attribs)
	{
	    int                  tuplesize = attrib->getTupleSize();
	    GA_TypeInfo          typeinfo = attrib->getTypeInfo();
	    GA_StorageClass      storageclass = attrib->getStorageClass();
	    const GA_AIFTuple   *tuple = attrib->getAIFTuple();
	    const GA_AIFNumericArray *num_array = attrib->getAIFNumericArray();
	    GA_Storage           storage = GA_STORE_INVALID;
            const bool is_array_attrib = GA_ATINumericArray::isType(attrib) ||
                                         GA_ATIStringArray::isType(attrib);

            if (tuple)
                storage = tuple->getStorage(attrib);
            else if (num_array)
                storage = num_array->getStorage(attrib);

	    if (tuplesize == 3 && typeinfo == GA_TYPE_COLOR)
	    {
		if (husdCopySopArrayAttribute<UT_Vector3F>(
			stage, attrib, group, setattrs, timecode,
			targetprimpath, "color3f[]"))
		    continue;
	    }
	    else if (storageclass == GA_STORECLASS_REAL)
	    {
		if (storage == GA_STORE_REAL32)
		{
                    if (is_array_attrib)
                    {
			if (husdCopySopArrayOfArraysAttribute<UT_Fpreal32Array>(
				stage, attrib, group, setattrs, timecode,
				targetprimpath))
			    continue;
                    }
                    else if (tuplesize == 16)
		    {
			if (husdCopySopArrayAttribute<UT_Matrix4F>(
				stage, attrib, group, setattrs, timecode,
				targetprimpath))
			    continue;
		    }
		    else if (tuplesize == 9)
		    {
			if (husdCopySopArrayAttribute<UT_Matrix3F>(
				stage, attrib, group, setattrs, timecode,
				targetprimpath))
			    continue;
		    }
		    else if (tuplesize == 4 && typeinfo == GA_TYPE_QUATERNION)
		    {
			if (husdCopySopArrayAttribute<UT_QuaternionF>(
				stage, attrib, group, setattrs, timecode,
				targetprimpath))
			    continue;
		    }
		    else if (tuplesize == 4)
		    {
			if (husdCopySopArrayAttribute<UT_Vector4F>(
				stage, attrib, group, setattrs, timecode,
				targetprimpath))
			    continue;
		    }
		    else if (tuplesize == 3)
		    {
			if (husdCopySopArrayAttribute<UT_Vector3F>(
				stage, attrib, group, setattrs, timecode,
				targetprimpath))
			    continue;
		    }
		    else if (tuplesize == 2)
		    {
			if (husdCopySopArrayAttribute<UT_Vector2F>(
				stage, attrib, group, setattrs, timecode,
				targetprimpath))
			    continue;
		    }
		    else if (tuplesize == 1)
		    {
			if (husdCopySopArrayAttribute<fpreal32>(
				stage, attrib, group, setattrs, timecode,
				targetprimpath))
			    continue;
		    }
		}
		else if (storage == GA_STORE_REAL64)
		{
                    if (is_array_attrib)
                    {
			if (husdCopySopArrayOfArraysAttribute<UT_Fpreal64Array>(
				stage, attrib, group, setattrs, timecode,
				targetprimpath))
			    continue;
                    }
                    else if (tuplesize == 16)
		    {
			if (husdCopySopArrayAttribute<UT_Matrix4D>(
				stage, attrib, group, setattrs, timecode,
				targetprimpath))
			    continue;
		    }
		    else if (tuplesize == 9)
		    {
			if (husdCopySopArrayAttribute<UT_Matrix3D>(
				stage, attrib, group, setattrs, timecode,
				targetprimpath))
			    continue;
		    }
		    else if (tuplesize == 4 && typeinfo == GA_TYPE_QUATERNION)
		    {
			if (husdCopySopArrayAttribute<UT_QuaternionD>(
				stage, attrib, group, setattrs, timecode,
				targetprimpath))
			    continue;
		    }
		    else if (tuplesize == 4)
		    {
			if (husdCopySopArrayAttribute<UT_Vector4D>(
				stage, attrib, group, setattrs, timecode,
				targetprimpath))
			    continue;
		    }
		    else if (tuplesize == 3)
		    {
			if (husdCopySopArrayAttribute<UT_Vector3D>(
				stage, attrib, group, setattrs, timecode,
				targetprimpath))
			    continue;
		    }
		    else if (tuplesize == 2)
		    {
			if (husdCopySopArrayAttribute<UT_Vector2D>(
				stage, attrib, group, setattrs, timecode,
				targetprimpath))
			    continue;
		    }
		    else if (tuplesize == 1)
		    {
			if (husdCopySopArrayAttribute<fpreal64>(
				stage, attrib, group, setattrs, timecode,
				targetprimpath))
			    continue;
		    }
		}
	    }
	    else if (storageclass == GA_STORECLASS_INT)
	    {
		if (storage == GA_STORE_INT32)
		{
                    if (is_array_attrib)
                    {
			if (husdCopySopArrayOfArraysAttribute<UT_Int32Array>(
				stage, attrib, group, setattrs, timecode,
				targetprimpath))
			    continue;
                    }
		    else if (tuplesize == 4)
		    {
			if (husdCopySopArrayAttribute<UT_Vector4i>(
			    stage, attrib, group, setattrs, timecode,
			    targetprimpath))
			continue;
		    }
                    else if (tuplesize == 3)
		    {
			if (husdCopySopArrayAttribute<UT_Vector3i>(
			    stage, attrib, group, setattrs, timecode,
			    targetprimpath))
			continue;
		    }
		    else if (tuplesize == 2)
		    {
			if (husdCopySopArrayAttribute<UT_Vector2i>(
			    stage, attrib, group, setattrs, timecode,
			    targetprimpath))
			continue;
		    }
		    else if (tuplesize == 1)
		    {
			if (husdCopySopArrayAttribute<int>(
			    stage, attrib, group, setattrs, timecode,
			    targetprimpath))
			continue;
		    }
		}
		else if (storage == GA_STORE_INT64)
		{
                    if (is_array_attrib)
                    {
			if (husdCopySopArrayOfArraysAttribute<UT_Int64Array>(
				stage, attrib, group, setattrs, timecode,
				targetprimpath))
			    continue;
                    }
		    else if (tuplesize == 1)
		    {
			if (husdCopySopArrayAttribute<int64>(
			    stage, attrib, group, setattrs, timecode,
			    targetprimpath))
			continue;
		    }
		}
	    }
	    else if (storageclass == GA_STORECLASS_STRING)
	    {
                if (is_array_attrib)
                {
                    if (husdCopySopArrayOfArraysAttribute<UT_StringArray>(
                            stage, attrib, group, setattrs, timecode,
                            targetprimpath))
                        continue;
                }
                else if (tuplesize == 1)
		{
		    if (husdCopySopArrayAttribute<UT_StringHolder>(
			stage, attrib, group, setattrs, timecode,
			targetprimpath))
		    continue;
		}
	    }
	}
	return true;
    }

    return false;
}
