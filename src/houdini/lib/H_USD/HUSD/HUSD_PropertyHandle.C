/*
 * Copyright 2019 Side Effects Software Inc.
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
 *
 * Produced by:
 *	Side Effects Software Inc.
 *	123 Front Street West, Suite 1401
 *	Toronto, Ontario
 *      Canada   M5J 2M2
 *	416-504-9876
 *
 */

#include "HUSD_PropertyHandle.h"
#include "XUSD_ObjectLock.h"
#include "XUSD_Utils.h"
#include <PI/PI_EditScriptedParms.h>
#include <PRM/PRM_ChoiceList.h>
#include <PRM/PRM_Default.h>
#include <PRM/PRM_Range.h>
#include <PRM/PRM_Shared.h>
#include <PRM/PRM_SpareData.h>
#include <UT/UT_Format.h>
#include <UT/UT_VarEncode.h>
#include <pxr/usd/usdGeom/xformOp.h>
#include <pxr/usd/usd/property.h>
#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/usd/relationship.h>
#include <pxr/usd/sdf/attributeSpec.h>
#include <pxr/usd/sdf/path.h>

using namespace UT::Literal;

PXR_NAMESPACE_USING_DIRECTIVE

namespace { // start anonymous namespace

typedef void (*ValueConverter)(const VtValue &in, UT_StringHolder *out);

void
theDefaultConverter(const VtValue &in, UT_StringHolder *out)
{
    out->clear();
}

void
theAssetConverter(const VtValue &in, UT_StringHolder *out)
{
    VtValue castin = VtValue::Cast<SdfAssetPath>(in);
    if (castin.IsEmpty())
	return;
    out[0] = castin.UncheckedGet<SdfAssetPath>().GetAssetPath();
}

void
theArrayAssetConverter(const VtValue &in, UT_StringHolder *out)
{
    if (in.GetArraySize() > 0)
    {
	VtValue castin = VtValue::Cast<VtArray<SdfAssetPath> >(in);
	if (castin.IsEmpty())
	    return;
	VtValue elementin(castin.UncheckedGet<VtArray<SdfAssetPath> >()[0]);
	theAssetConverter(elementin, out);
    }
}

template <typename ScalarType> void
theStringConverter(const VtValue &in, UT_StringHolder *out)
{
    VtValue castin = VtValue::Cast<ScalarType>(in);
    if (castin.IsEmpty())
	return;
    out[0] = (std::string)castin.UncheckedGet<ScalarType>();
}

template <typename ScalarType> void
theArrayStringConverter(const VtValue &in, UT_StringHolder *out)
{
    if (in.GetArraySize() > 0)
    {
	VtValue castin = VtValue::Cast<VtArray<ScalarType> >(in);
	if (castin.IsEmpty())
	    return;
	VtValue elementin(castin.UncheckedGet<VtArray<ScalarType> >()[0]);
	theStringConverter<ScalarType>(elementin, out);
    }
}

template <typename ScalarType> void
theScalarConverter(const VtValue &in, UT_StringHolder *out)
{
    static constexpr size_t	theMaxLen = 256;
    char			 numstr[theMaxLen];
    size_t			 len;

    VtValue castin = VtValue::Cast<ScalarType>(in);
    if (castin.IsEmpty())
	return;
    len = UTformat(numstr, theMaxLen, "{}", castin.UncheckedGet<ScalarType>());
    numstr[len] = '\0';
    out[0] = numstr;
}

template <typename ScalarType> void
theArrayScalarConverter(const VtValue &in, UT_StringHolder *out)
{
    if (in.GetArraySize() > 0)
    {
	VtValue castin = VtValue::Cast<VtArray<ScalarType> >(in);
	if (castin.IsEmpty())
	    return;
	VtValue elementin(castin.UncheckedGet<VtArray<ScalarType> >()[0]);
	theScalarConverter<ScalarType>(elementin, out);
    }
}

template <typename VecType> void
theVecConverter(const VtValue &in, UT_StringHolder *out)
{
    VtValue castin = VtValue::Cast<VecType>(in);
    if (castin.IsEmpty())
	return;
    for (int i = 0; i < VecType::dimension; i++)
	out[i].sprintf(SYS_FPREAL_DIG_FMT, castin.UncheckedGet<VecType>()[i]);
}

template <typename VecType> void
theArrayVecConverter(const VtValue &in, UT_StringHolder *out)
{
    if (in.GetArraySize() > 0)
    {
	VtValue castin = VtValue::Cast<VtArray<VecType> >(in);
	if (castin.IsEmpty())
	    return;
	VtValue elementin(castin.UncheckedGet<VtArray<VecType> >()[0]);
	theVecConverter<VecType>(elementin, out);
    }
}

template <typename MatType> void
theMatConverter(const VtValue &in, UT_StringHolder *out)
{
    VtValue castin = VtValue::Cast<MatType>(in);
    if (castin.IsEmpty())
	return;
    for (int r = 0; r < MatType::numRows; r++)
	for (int c = 0; c < MatType::numColumns; c++)
	    out[r*MatType::numColumns+c].sprintf(SYS_FPREAL_DIG_FMT,
		castin.UncheckedGet<MatType>()[r][c]);
}

template <typename MatType> void
theArrayMatConverter(const VtValue &in, UT_StringHolder *out)
{
    if (in.GetArraySize() > 0)
    {
	VtValue castin = VtValue::Cast<VtArray<MatType> >(in);
	if (castin.IsEmpty())
	    return;
	VtValue elementin(castin.UncheckedGet<VtArray<MatType> >()[0]);
	theMatConverter<MatType>(elementin, out);
    }
}

PRM_Name	 theDefaultName("name", "name");
PRM_Template	 theDefaultTemplate(PRM_STRING, 1, &theDefaultName);
PRM_Default	 thePivotSwitcherInfo(2, "Pivot Transform");
PRM_Template	 theXformTemplates[] = {
    PRM_Template(PRM_ORD, PRM_TYPE_JOIN_PAIR, 1, &PRMtrsName,
		 0, &PRMtrsMenu),
    PRM_Template(PRM_ORD, PRM_TYPE_NO_LABEL,  1, &PRMxyzName,
		 0, &PRMxyzMenu),
    PRM_Template(PRM_XYZ, 3, &PRMxlateName),
    PRM_Template(PRM_XYZ, 3, &PRMrotName,
		 0, 0, &PRMangleRange),
    PRM_Template(PRM_XYZ, 3, &PRMscaleName,
		 PRMoneDefaults),
    PRM_Template(PRM_FLT, 3, &PRMshearName,
		 PRMzeroDefaults),
    PRM_Template(PRM_FLT, 1, &PRMuscaleName,
		 PRMoneDefaults, 0,&PRMuscaleRange),
    PRM_Template(PRM_SWITCHER, 1, &PRMpivotXformParmGroupName,
		 &thePivotSwitcherInfo, 0, 0, 0,
		 &PRM_SpareData::groupTypeCollapsible),
    PRM_Template(PRM_XYZ, 3, &PRMpivotXlateLabelName,
		 PRMzeroDefaults),
    PRM_Template(PRM_XYZ, 3, &PRMpivotRotName,
		 PRMzeroDefaults),
    PRM_Template(),
};

class AttribInfo
{
public:
    PRM_Template	 myTemplate = theDefaultTemplate;
    ValueConverter	 myValueConverter = theDefaultConverter;
    ValueConverter	 myArrayValueConverter = theDefaultConverter;
    bool                 mySupportsExtendedControl = false;
};

const PRM_Template &
getTemplateForRelationship()
{
    return theDefaultTemplate;
}

const PRM_Template &
getTemplateForTransform()
{
    static PRM_Name		 theTransformChoices[] = {
	PRM_Name("append", "Append"),
	PRM_Name("prepend", "Prepend"),
	PRM_Name("overwriteorappend", "Overwrite or Append"),
	PRM_Name("overwriteorprepend", "Overwrite or Prepend"),
	PRM_Name("world", "Apply Transform in World Space"),
	PRM_Name("replace", "Replace All Local Transforms"),
	PRM_Name()
    };
    static PRM_Default		 theTransformDefault(0,
					theTransformChoices[0].getToken());
    static PRM_ChoiceList	 theTransformMenu(PRM_CHOICELIST_SINGLE,
					theTransformChoices);
    static PRM_Template		 theTransformTemplate(PRM_STRING, 1,
					&theDefaultName,
					&theTransformDefault,
					&theTransformMenu);

    return theTransformTemplate;
}

const AttribInfo &
getAttribInfoForValueType(const UT_StringRef &scalartypename)
{
    static PRM_Range	 theUnsignedRange(PRM_RANGE_RESTRICTED, 0,
				PRM_RANGE_UI, 10);

    static PRM_Template	 theStringTemplate(PRM_STRING, 1, &theDefaultName);
    static PRM_Template	 theFileTemplate(PRM_FILE, 1, &theDefaultName);
    static PRM_Template	 theBoolTemplate(PRM_TOGGLE, 1, &theDefaultName);
    static PRM_Template	 theColor3Template(PRM_RGB, 3, &theDefaultName);
    static PRM_Template	 theColor4Template(PRM_RGBA, 4, &theDefaultName);
    static PRM_Template	 theFloatTemplate(PRM_FLT, 1, &theDefaultName);
    static PRM_Template	 theFloat2Template(PRM_FLT, 2, &theDefaultName);
    static PRM_Template	 theFloat3Template(PRM_FLT, 3, &theDefaultName);
    static PRM_Template	 theFloat4Template(PRM_FLT, 4, &theDefaultName);
    static PRM_Template	 theFloat9Template(PRM_FLT, 9, &theDefaultName);
    static PRM_Template	 theFloat16Template(PRM_FLT, 16, &theDefaultName);
    static PRM_Template	 theIntTemplate(PRM_INT, 1, &theDefaultName);
    static PRM_Template	 theInt2Template(PRM_INT, 2, &theDefaultName);
    static PRM_Template	 theInt3Template(PRM_INT, 3, &theDefaultName);
    static PRM_Template	 theInt4Template(PRM_INT, 4, &theDefaultName);
    static PRM_Template	 theUIntTemplate(PRM_INT, 1, &theDefaultName, nullptr,
				nullptr, &theUnsignedRange);

    static UT_Map<UT_StringHolder, AttribInfo> theTemplateMap({
	{ "token"_sh, { theStringTemplate, theStringConverter<TfToken>,
	    theArrayStringConverter<TfToken> } },
	{ "string"_sh, { theStringTemplate, theStringConverter<std::string>,
	    theArrayStringConverter<std::string> } },
	{ "uchar"_sh, { theStringTemplate, theStringConverter<std::string>,
	    theArrayStringConverter<std::string> } },

	{ "asset"_sh, { theFileTemplate, theAssetConverter,
	    theArrayAssetConverter } },

	{ "bool"_sh, { theBoolTemplate, theScalarConverter<int>,
		theArrayScalarConverter<int> } },

	{ "color3d"_sh, { theColor3Template, theVecConverter<GfVec3d>,
		theArrayVecConverter<GfVec3d>, true } },
	{ "color3f"_sh, { theColor3Template, theVecConverter<GfVec3f>,
		theArrayVecConverter<GfVec3f>, true } },
	{ "color3h"_sh, { theColor3Template, theVecConverter<GfVec3f>,
		theArrayVecConverter<GfVec3f>, true } },

	{ "color4d"_sh, { theColor4Template, theVecConverter<GfVec4d>,
		theArrayVecConverter<GfVec4d>, true } },
	{ "color4f"_sh, { theColor4Template, theVecConverter<GfVec4f>,
		theArrayVecConverter<GfVec4f>, true } },
	{ "color4h"_sh, { theColor4Template, theVecConverter<GfVec4f>,
		theArrayVecConverter<GfVec4f>, true } },

	{ "double"_sh, { theFloatTemplate, theScalarConverter<fpreal64>,
		theArrayScalarConverter<fpreal64>, true } },
	{ "float"_sh, { theFloatTemplate, theScalarConverter<fpreal32>,
		theArrayScalarConverter<fpreal32>, true } },
	{ "half"_sh, { theFloatTemplate, theScalarConverter<fpreal32>,
		theArrayScalarConverter<fpreal32>, true } },

	{ "double2"_sh, { theFloat2Template, theVecConverter<GfVec2d>,
		theArrayVecConverter<GfVec2d>, true } },
	{ "float2"_sh, { theFloat2Template, theVecConverter<GfVec2f>,
		theArrayVecConverter<GfVec2f>, true } },
	{ "half2"_sh, { theFloat2Template, theVecConverter<GfVec2f>,
		theArrayVecConverter<GfVec2f>, true } },
	{ "texcoord2d"_sh, { theFloat2Template, theVecConverter<GfVec2d>,
		theArrayVecConverter<GfVec2d>, true } },
	{ "texcoord2f"_sh, { theFloat2Template, theVecConverter<GfVec2f>,
		theArrayVecConverter<GfVec2f>, true } },
	{ "texcoord2h"_sh, { theFloat2Template, theVecConverter<GfVec2f>,
		theArrayVecConverter<GfVec2f>, true } },

	{ "double3"_sh, { theFloat3Template, theVecConverter<GfVec3d>,
		theArrayVecConverter<GfVec3d>, true } },
	{ "float3"_sh, { theFloat3Template, theVecConverter<GfVec3f>,
		theArrayVecConverter<GfVec3f>, true } },
	{ "half3"_sh, { theFloat3Template, theVecConverter<GfVec3f>,
		theArrayVecConverter<GfVec3f>, true } },
	{ "normal3d"_sh, { theFloat3Template, theVecConverter<GfVec3d>,
		theArrayVecConverter<GfVec3d>, true } },
	{ "normal3f"_sh, { theFloat3Template, theVecConverter<GfVec3f>,
		theArrayVecConverter<GfVec3f>, true } },
	{ "normal3h"_sh, { theFloat3Template, theVecConverter<GfVec3f>,
		theArrayVecConverter<GfVec3f>, true } },
	{ "point3d"_sh, { theFloat3Template, theVecConverter<GfVec3d>,
		theArrayVecConverter<GfVec3d>, true } },
	{ "point3f"_sh, { theFloat3Template, theVecConverter<GfVec3f>,
		theArrayVecConverter<GfVec3f>, true } },
	{ "point3h"_sh, { theFloat3Template, theVecConverter<GfVec3f>,
		theArrayVecConverter<GfVec3f>, true } },
	{ "vector3d"_sh, { theFloat3Template, theVecConverter<GfVec3d>,
		theArrayVecConverter<GfVec3d>, true } },
	{ "vector3f"_sh, { theFloat3Template, theVecConverter<GfVec3f>,
		theArrayVecConverter<GfVec3f>, true } },
	{ "vector3h"_sh, { theFloat3Template, theVecConverter<GfVec3f>,
		theArrayVecConverter<GfVec3f>, true } },
	{ "texcoord3d"_sh, { theFloat3Template, theVecConverter<GfVec3d>,
		theArrayVecConverter<GfVec3d>, true } },
	{ "texcoord3f"_sh, { theFloat3Template, theVecConverter<GfVec3f>,
		theArrayVecConverter<GfVec3f>, true } },
	{ "texcoord3h"_sh, { theFloat3Template, theVecConverter<GfVec3f>,
		theArrayVecConverter<GfVec3f>, true } },

	{ "double4"_sh, { theFloat4Template, theVecConverter<GfVec4d>,
		theArrayVecConverter<GfVec4d>, true } },
	{ "float4"_sh, { theFloat4Template, theVecConverter<GfVec4f>,
		theArrayVecConverter<GfVec4f>, true } },
	{ "half4"_sh, { theFloat4Template, theVecConverter<GfVec4f>,
		theArrayVecConverter<GfVec4f>, true } },
	{ "quatd"_sh, { theFloat4Template, theVecConverter<GfVec4d>,
		theArrayVecConverter<GfVec4d>, true } },
	{ "quatf"_sh, { theFloat4Template, theVecConverter<GfVec4f>,
		theArrayVecConverter<GfVec4f>, true } },
	{ "quath"_sh, { theFloat4Template, theVecConverter<GfVec4f>,
		theArrayVecConverter<GfVec4f>, true } },

	{ "matrix2d"_sh, { theFloat4Template, theMatConverter<GfMatrix2d>,
		theArrayMatConverter<GfMatrix2d>, true } },
	{ "matrix3d"_sh, { theFloat9Template, theMatConverter<GfMatrix3d>,
		theArrayMatConverter<GfMatrix3d>, true } },
	{ "matrix4d"_sh, { theFloat16Template, theMatConverter<GfMatrix4d>,
		theArrayMatConverter<GfMatrix4d>, true } },
	{ "frame4d"_sh, { theFloat16Template, theMatConverter<GfMatrix4d>,
		theArrayMatConverter<GfMatrix4d>, true } },

	{ "int"_sh, { theIntTemplate, theScalarConverter<int>,
		theArrayScalarConverter<int>, true } },
	{ "int64"_sh, { theIntTemplate, theScalarConverter<int64>,
		theArrayScalarConverter<int64>, true } },
	{ "int2"_sh, { theInt2Template, theVecConverter<GfVec2i>,
		theArrayVecConverter<GfVec2i>, true } },
	{ "int3"_sh, { theInt3Template, theVecConverter<GfVec3i>,
		theArrayVecConverter<GfVec3i>, true } },
	{ "int4"_sh, { theInt4Template, theVecConverter<GfVec4i>,
		theArrayVecConverter<GfVec4i>, true } },

	{ "uint"_sh, { theUIntTemplate, theScalarConverter<uint>,
		theArrayScalarConverter<uint>, true } },
	{ "uint64"_sh, { theUIntTemplate, theScalarConverter<uint64>,
		theArrayScalarConverter<uint64>, true } },
    });

    return theTemplateMap[scalartypename];
}

} // end anonymous namespace

HUSD_PropertyHandle::HUSD_PropertyHandle()
{
}

HUSD_PropertyHandle::HUSD_PropertyHandle(const HUSD_PrimHandle &prim_handle,
	const UT_StringHolder &property_name)
    : myPrimHandle(prim_handle)
{
    SdfPath	 path = HUSDgetSdfPath(prim_handle.path());
    path = path.AppendProperty(TfToken(property_name.toStdString()));
    myPath = path.GetString();
    myName = property_name;
}

HUSD_PropertyHandle::~HUSD_PropertyHandle()
{
}

bool
HUSD_PropertyHandle::isCustom() const
{
    XUSD_AutoObjectLock<UsdProperty> lock(*this);

    if (!lock.obj())
	return false;

    return lock.obj().IsCustom();
}

bool
HUSD_PropertyHandle::isXformOp() const
{
    XUSD_AutoObjectLock<UsdProperty> lock(*this);

    if (!lock.obj())
	return false;

    return UsdGeomXformOp::IsXformOp(lock.obj().GetName());
}

UT_StringHolder
HUSD_PropertyHandle::getSourceSchema() const
{
    XUSD_AutoObjectLock<UsdPrim> lock(myPrimHandle);

    if (lock.obj())
    {
	UsdSchemaRegistry	&registry = UsdSchemaRegistry::GetInstance();
	TfTokenVector		 schemas = lock.obj().GetAppliedSchemas();

	if (schemas.size() > 0)
	{
	    TfToken		 tfname(myName.toStdString());
	    SdfPath		 proppath(SdfPath::ReflexiveRelativePath().
					AppendProperty(tfname));

	    for (auto &&schema : schemas)
	    {
		SdfPrimSpecHandle primspec = registry.GetPrimDefinition(schema);

		if (primspec && primspec->GetPropertyAtPath(proppath))
		    return schema.GetText();
	    }
	}
    }

    return UT_StringHolder::theEmptyString;
}

void
HUSD_PropertyHandle::createScriptedControlParm(
	UT_Array<PI_EditScriptedParm *> &parms,
	const UT_String &propbasename,
        bool extendedcontrolmenu) const
{
    static PRM_Name		 theControlName("control", "control");

    // A control menu for attributes that do not support adding or
    // multiplying the existing value (such as strings).
    static PRM_Name		 theControlChoices[] = {
	PRM_Name("set", "Set or Create"),
	PRM_Name("setexisting", "Set if Exists"),
	PRM_Name("block", "Block"),
	PRM_Name("none", "Do Nothing"),
	PRM_Name()
    };
    static PRM_Default		 theControlDefault(0,
                                    theControlChoices[0].getToken());
    static PRM_ChoiceList	 theControlMenu(PRM_CHOICELIST_SINGLE,
                                    theControlChoices);
    static PRM_Template		 theControlParm(
	PRM_STRING, 1,
	&theControlName,
	&theControlDefault,
	&theControlMenu
    );
    static PRM_Name		 theExtendedControlChoices[] = {
	PRM_Name("set", "Set or Create"),
	PRM_Name("setexisting", "Set if Exists"),
	PRM_Name("add", "Add if Exists"),
	PRM_Name("multiply", "Multiply if Exists"),
	PRM_Name("block", "Block"),
	PRM_Name("none", "Do Nothing"),
	PRM_Name()
    };

    // A control menu for attributes that support adding and multiplying
    // the existing value.
    static PRM_Default		 theExtendedControlDefault(0,
                                    theExtendedControlChoices[0].getToken());
    static PRM_ChoiceList	 theExtendedControlMenu(PRM_CHOICELIST_SINGLE,
                                    theExtendedControlChoices);
    static PRM_Template		 theExtendedControlParm(
	PRM_STRING, 1,
	&theControlName,
	&theExtendedControlDefault,
	&theExtendedControlMenu
    );

    PI_EditScriptedParm	*parm;
    UT_String		 propname(propbasename);
    UT_String		 proplabel(propname);

    propname.append("_control");
    if (extendedcontrolmenu)
        parm = new PI_EditScriptedParm(theExtendedControlParm, nullptr, false);
    else
        parm = new PI_EditScriptedParm(theControlParm, nullptr, false);
    parm->myName = UT_VarEncode::encodeParm(propname);
    parm->myLabel = proplabel;
    parm->myMenuType = PI_MENU_JOIN;

    parms.append(parm);
}

void
HUSD_PropertyHandle::createScriptedParms(
	UT_Array<PI_EditScriptedParm *> &parms,
	const UT_StringRef &custom_name,
	bool prepend_control_parm,
	bool prefix_xform_parms) const
{
    XUSD_AutoObjectLock<UsdProperty> lock(*this);

    if (!lock.obj())
	return;

    PI_EditScriptedParm	*parm = nullptr;
    UsdAttribute	 attr = lock.obj().As<UsdAttribute>();
    UsdRelationship	 rel = lock.obj().As<UsdRelationship>();
    bool		 istransformop = false;
    bool                 extendedcontrolmenu = false;

    if (UsdGeomXformOp::IsXformOp(attr))
    {
	UsdGeomXformOp	 xformop(attr);

	if (xformop && xformop.GetOpType() == UsdGeomXformOp::TypeTransform)
	    istransformop = true;
    }

    // Figure out the base name for parameters representing this property.
    UT_String		 propbasename;

    propbasename = (custom_name.isstring() ? custom_name : name()).c_str();
    if (istransformop && custom_name.isstring())
    {
	UT_StringHolder	 xform_type;

	// If a custom name was provided, it may not be a valid xformOp name.
	// In this case we must treat it as if the custom_name is just the
	// transform op suffix.
	if (!HUSDisXformAttribute(propbasename, &xform_type) ||
	    UsdGeomXformOp::GetOpTypeEnum(TfToken(xform_type.toStdString())) !=
		UsdGeomXformOp::TypeTransform)
	{
	    propbasename = UsdGeomXformOp::GetOpName(
		UsdGeomXformOp::TypeTransform,
		TfToken(propbasename.toStdString())).GetString();
	}
    }

    if (istransformop)
    {
	PRM_Template	 tplate = getTemplateForTransform();

	parm = new PI_EditScriptedParm(tplate, nullptr, false);
	parm->setSpareValue(HUSD_PROPERTY_VALUETYPE,
	    HUSD_PROPERTY_VALUETYPE_XFORM);
	if (prefix_xform_parms)
	{
	    UT_String	 prefix(propbasename);

	    prefix.append("_");
	    parm->setSpareValue(HUSD_PROPERTY_XFORM_PARM_PREFIX, prefix);
	}
    }
    else if (attr)
    {
	SdfValueTypeName valuetype = attr.GetTypeName();
	SdfValueTypeName scalartype = valuetype.GetScalarType();
	UT_StringRef	 scalartypename = scalartype.GetAsToken().GetText();
	AttribInfo	 info = getAttribInfoForValueType(scalartypename);
	UT_StringHolder	 source_schema = getSourceSchema();
	VtValue		 value;

	parm = new PI_EditScriptedParm(info.myTemplate, nullptr, false);
	parm->setSpareValue(HUSD_PROPERTY_VALUETYPE,
	    valuetype.GetAsToken().GetText());
	if (source_schema.isstring())
	    parm->setSpareValue(HUSD_PROPERTY_APISCHEMA, source_schema);

	attr.Get(&value, HUSDgetCurrentUsdTimeCode());
	if (!value.IsEmpty())
	{
	    if (value.IsArrayValued())
		info.myArrayValueConverter(value, parm->myDefaults);
	    else
		info.myValueConverter(value, parm->myDefaults);
	}

	// Check if a token attribute has a specific set of allowed values.
	if (scalartypename == "token")
	{
            VtTokenArray         allowedtokens;

	    if (attr.GetMetadata(SdfFieldKeys->AllowedTokens, &allowedtokens))
	    {
		for (auto &&token : allowedtokens)
		    parm->myMenu.append({token.GetString(), token.GetString()});
		parm->myMenuType = PI_MENU_NORMAL;
		parm->myMenuEnable = PI_MENU_ITEMS;
	    }
	}

        extendedcontrolmenu = info.mySupportsExtendedControl;
    }
    else if (rel)
    {
	PRM_Template	 tplate = getTemplateForRelationship();
	SdfPathVector	 targets;
	UT_WorkBuffer	 targets_buf;

	parm = new PI_EditScriptedParm(tplate, nullptr, false);
	parm->setSpareValue(HUSD_PROPERTY_VALUETYPE,
	    HUSD_PROPERTY_VALUETYPE_RELATIONSHIP);
	rel.GetTargets(&targets);
	for (auto &&target : targets)
	{
	    if (!targets_buf.isEmpty())
		targets_buf.append(' ');
	    targets_buf.append(target.GetString());
	}
	targets_buf.stealIntoStringHolder(parm->myDefaults[0]);
    }

    if (!parm)
	return;

    UT_String		 propname(propbasename);
    UT_String		 proplabel(lock.obj().GetDisplayName());
    UT_String		 disablecond;

    // If the property doesn't have a display name, use the internal name.
    if (!proplabel.isstring())
	proplabel = propname;

    // Encode the property name in case it is namespaced.
    parm->myName = UT_VarEncode::encodeParm(propname);
    parm->myLabel = proplabel;

    if (prepend_control_parm)
    {
	createScriptedControlParm(parms, propbasename, extendedcontrolmenu);
	disablecond.sprintf("{ %s == block } { %s == none }",
	    parms.last()->myName.c_str(), parms.last()->myName.c_str());
	parm->myConditional[PRM_CONDTYPE_DISABLE] = disablecond;
    }

    parms.append(parm);

    // For transform ops, we now need to append all the individual xform
    // components that are used to build the transform matrix.
    if (istransformop)
    {
	PI_EditScriptedParms	 xformparms(nullptr, theXformTemplates,
				    false, false, false);
	PI_EditScriptedParm	*xformparm;

	for (int i = 0, n = xformparms.getNParms(); i < n; i++)
	{
	    xformparm = new PI_EditScriptedParm(*xformparms.getParm(i));
	    if (prefix_xform_parms)
	    {
		propname = propbasename;
		propname.append('_');
		propname.append(xformparm->myName);
		xformparm->myName = UT_VarEncode::encodeParm(propname);
	    }
	    xformparm->myConditional[PRM_CONDTYPE_DISABLE] = disablecond;
	    parms.append(xformparm);
	}
    }
}

