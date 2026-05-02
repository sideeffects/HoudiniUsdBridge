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
#include "HUSD_Constants.h"
#include "XUSD_Format.h"
#include "XUSD_ObjectLock.h"
#include "XUSD_ShaderRegistry.h"
#include "XUSD_Utils.h"
#include <PI/PI_EditScriptedParms.h>
#include <PI/PI_OldParms.h>
#include <PRM/PRM_ChoiceList.h>
#include <PRM/PRM_Conditional.h>
#include <PRM/PRM_Default.h>
#include <PRM/PRM_Range.h>
#include <PRM/PRM_Shared.h>
#include <PRM/PRM_SpareData.h>
#include <CH/CH_ExprLanguage.h>
#include <UT/UT_Digits.h>
#include <UT/UT_Format.h>
#include <UT/UT_VarEncode.h>
#include <pxr/usd/usdGeom/xformOp.h>
#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/usd/collectionAPI.h>
#include <pxr/usd/usd/property.h>
#include <pxr/usd/usd/relationship.h>
#include <pxr/usd/sdf/attributeSpec.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/usdShade/tokens.h>

#define HUSD_XFORM_STYLE_PARAMETER_TOKEN "xform_style"

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
	out[i] = UT_Digits(castin.UncheckedGet<VecType>()[i]).c_str();
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
	    out[r*MatType::numColumns+c] = UT_Digits(
		castin.UncheckedGet<MatType>()[r][c]).c_str();
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
PRM_Template	 theDefaultStringTemplate(PRM_STRING, 1, &theDefaultName);
PRM_Template	 theDefaultFloatRampTemplate(PRM_MULTITYPE_RAMP_FLT,
                        nullptr, 1, &theDefaultName);
PRM_Template	 theDefaultColorRampTemplate(PRM_MULTITYPE_RAMP_RGB,
                        nullptr, 1, &theDefaultName);
PRM_Default	 thePivotSwitcherInfo(2, "Pivot Transform");
PRM_Conditional  theXformOrderCondition(
                        "{ " HUSD_XFORM_STYLE_PARAMETER_TOKEN " == xformcommonapi } "
                        "{ " HUSD_XFORM_STYLE_PARAMETER_TOKEN " == extendedxformcommonapi }",
                        PRM_CONDTYPE_DISABLE);
PRM_Conditional  theXformCommonCondition(
                        "{ " HUSD_XFORM_STYLE_PARAMETER_TOKEN " == xformcommonapi }",
                        PRM_CONDTYPE_DISABLE);

PRM_Template	 theXformTemplates[] = {
    PRM_Template(PRM_ORD, PRM_TYPE_JOIN_PAIR, 1, &PRMtrsName,
                 0, &PRMtrsMenu, 0, 0, 0, 1, 0,
                 &theXformOrderCondition),
    PRM_Template(PRM_ORD, PRM_TYPE_NO_LABEL,  1, &PRMxyzName,
		 0, &PRMxyzMenu),
    PRM_Template(PRM_XYZ, 3, &PRMxlateName),
    PRM_Template(PRM_XYZ, 3, &PRMrotName,
		 0, 0, &PRMangleRange),
    PRM_Template(PRM_XYZ, 3, &PRMscaleName,
		 PRMoneDefaults),
    PRM_Template(PRM_FLT, 3, &PRMshearName,
		 PRMzeroDefaults, 0, 0, 0, 0, 1, 0,
		 &theXformCommonCondition),
    PRM_Template(PRM_FLT, 1, &PRMuscaleName,
		 PRMoneDefaults, 0,&PRMuscaleRange),
    PRM_Template(PRM_SWITCHER, 1, &PRMpivotXformParmGroupName,
		 &thePivotSwitcherInfo, 0, 0, 0,
		 &PRM_SpareData::groupTypeCollapsible),
    PRM_Template(PRM_XYZ, 3, &PRMpivotXlateLabelName,
		 PRMzeroDefaults),
    PRM_Template(PRM_XYZ, 3, &PRMpivotRotName,
                 PRMzeroDefaults, 0, 0, 0, 0, 1, 0,
                 &theXformCommonCondition),
    PRM_Template(),
};

PRM_Name	 theConstraintsGroupName("parmgroup_constraints",
                        "Constraints");
PRM_Default	 theConstraintsSwitcherInfo(6, "Constraints");
PRM_Name         theLookAtUpVecAxisChoices[] = {
                    PRM_Name(HUSD_PROPERTY_LOOKAT_UPVECMETHOD_XAXIS, "X Axis"),
                    PRM_Name(HUSD_PROPERTY_LOOKAT_UPVECMETHOD_YAXIS, "Y Axis"),
                    PRM_Name(HUSD_PROPERTY_LOOKAT_UPVECMETHOD_CUSTOM, "Custom"),
                    PRM_Name()
                 };
PRM_ChoiceList   theLookAtUpVecAxisMenu(PRM_CHOICELIST_SINGLE,
                        theLookAtUpVecAxisChoices);
PRM_Default	 theLookAtUpVecAxisDefault(0,
                        HUSD_PROPERTY_LOOKAT_UPVECMETHOD_YAXIS);
PRM_Name	 theLookAtEnableName(HUSD_PROPERTY_LOOKAT_ENABLE,
                        "Enable Look At");
PRM_Name	 theLookAtPositionName(HUSD_PROPERTY_LOOKAT_POSITION,
                        "Look At Position");
PRM_Name	 theLookAtPrimName(HUSD_PROPERTY_LOOKAT_PRIM,
                        "Look At Primitive");
PRM_Name	 theLookAtUpVecMethodName(HUSD_PROPERTY_LOOKAT_UPVECMETHOD,
                        "Up Vector Method");
PRM_Name	 theLookAtUpVecName(HUSD_PROPERTY_LOOKAT_UPVEC,
                        "Up Vector");
PRM_Name	 theLookAtTwistName(HUSD_PROPERTY_LOOKAT_TWIST,
                        "Twist");
PRM_Conditional  theLookAtEnabledCondition("{ lookatenable == 0 }",
                        PRM_CONDTYPE_DISABLE);
PRM_ConditionalGroup theLookAtUpVectorCondition(PRM_ConditionalGroupArgs()
                        << PRM_ConditionalGroupItem(
                            "{ lookatenable == 0 }", PRM_CONDTYPE_DISABLE)
                        << PRM_ConditionalGroupItem(
                            "{ upvecmethod != custom }", PRM_CONDTYPE_HIDE));
// This is copied from, and should be kept in sync with, the
// lopPrimPathSpareData defined in LOP_PRMShared.C.
const char	*theLookatPrimPathSpareDataBaseScript =
                        "import loputils\n"
                        "loputils.selectPrimsInParm(kwargs, False)";
const UT_StringHolder theLookatSinglePrimSelectTooltip(
                        "Select a primitive in the Scene Viewer or "
                        "Scene Graph Tree pane.\n"
                        "Ctrl-click to select using the "
                        "primitive picker dialog.\n"
                        "Alt-click to toggle movement of "
                        "the display flag.");

PRM_SpareData	 theLookatPrimPathSpareData(PRM_SpareArgs() <<
                        PRM_SpareData::usdPathTypePrim <<
                        PRM_SpareToken(
                            PRM_SpareData::getScriptActionToken(),
                                 theLookatPrimPathSpareDataBaseScript) <<
                        PRM_SpareToken(
                            PRM_SpareData::getScriptActionHelpToken(),
                                 theLookatSinglePrimSelectTooltip) <<
                        PRM_SpareToken(
                            PRM_SpareData::getScriptActionIconToken(),
                            "BUTTONS_reselect"));

PRM_Template	 theXformWithLookAtTemplates[] = {
    PRM_Template(PRM_ORD, PRM_TYPE_JOIN_PAIR, 1, &PRMtrsName,
                 0, &PRMtrsMenu, 0, 0, 0, 1, 0,
                 &theXformOrderCondition),
    PRM_Template(PRM_ORD, PRM_TYPE_NO_LABEL,  1, &PRMxyzName,
                 0, &PRMxyzMenu),
    PRM_Template(PRM_XYZ, 3, &PRMxlateName),
    PRM_Template(PRM_XYZ, 3, &PRMrotName,
                 0, 0, &PRMangleRange),
    PRM_Template(PRM_XYZ, 3, &PRMscaleName,
                 PRMoneDefaults),
    PRM_Template(PRM_FLT, 3, &PRMshearName,
                 PRMzeroDefaults, 0, 0, 0, 0, 1, 0,
                 &theXformCommonCondition),
    PRM_Template(PRM_FLT, 1, &PRMuscaleName,
                 PRMoneDefaults, 0,&PRMuscaleRange),
    PRM_Template(PRM_SWITCHER, 1, &PRMpivotXformParmGroupName,
                 &thePivotSwitcherInfo, 0, 0, 0,
                 &PRM_SpareData::groupTypeCollapsible),
    PRM_Template(PRM_XYZ, 3, &PRMpivotXlateLabelName,
                 PRMzeroDefaults),
    PRM_Template(PRM_XYZ, 3, &PRMpivotRotName,
                 PRMzeroDefaults, 0, 0, 0, 0, 1, 0,
                 &theXformCommonCondition),

    // Look at constraint
    PRM_Template(PRM_SWITCHER, 1, &theConstraintsGroupName,
                 &theConstraintsSwitcherInfo, 0, 0, 0,
                 &PRM_SpareData::groupTypeCollapsible),
    PRM_Template(PRM_TOGGLE, 1, &theLookAtEnableName,
                 PRMzeroDefaults),
    PRM_Template(PRM_XYZ, 3, &theLookAtPositionName,
                 PRMzeroDefaults, 0, 0, 0, 0, 1, 0,
                 &theLookAtEnabledCondition),
    PRM_Template(PRM_STRING, 1, &theLookAtPrimName,
                 PRMzeroDefaults, 0, 0, 0,
                 &theLookatPrimPathSpareData, 1, 0,
                 &theLookAtEnabledCondition),
    PRM_Template(PRM_STRING, 1, &theLookAtUpVecMethodName,
                 &theLookAtUpVecAxisDefault,
                 &theLookAtUpVecAxisMenu, 0, 0, 0, 1, 0,
                 &theLookAtEnabledCondition),
    PRM_Template(PRM_XYZ, 3, &theLookAtUpVecName,
                 PRMyaxisDefaults, 0, 0, 0, 0, 1, 0,
                 &theLookAtUpVectorCondition),
    PRM_Template(PRM_FLT, 1, &theLookAtTwistName,
                 PRMzeroDefaults, 0, 0, 0, 0, 1, 0,
                 &theLookAtEnabledCondition),
    PRM_Template(),
};

const char *thePrimPatternWithProxiesSpareDataBaseScript =
    "import loputils\n"
    "loputils.selectPrimsInParm(kwargs, True, allowinstanceproxies=True)";
const UT_StringHolder theMultiPrimSelectTooltip(
    "Select primitives in the Scene Viewer or "
    "Scene Graph Tree pane.\n"
    "Ctrl-click to select using the "
    "primitive picker dialog.\n"
    "Shift-click to select using the "
    "primitive pattern editor.\n"
    "Alt-click to toggle movement of "
    "the display flag.");
PRM_SpareData thePrimPatternWithProxiesSpareData(PRM_SpareArgs() <<
    PRM_SpareData::usdPathTypePrimList <<
    PRM_SpareToken(
        PRM_SpareData::getScriptActionToken(),
        thePrimPatternWithProxiesSpareDataBaseScript) <<
    PRM_SpareToken(
        PRM_SpareData::getScriptActionHelpToken(),
        theMultiPrimSelectTooltip) <<
    PRM_SpareToken(
        PRM_SpareData::getScriptActionIconToken(),
        "BUTTONS_reselect"));
const char *thePatternMenuScript =
    "import loputils\n"
    "return loputils.createPrimPatternMenu(\n"
    "    kwargs['node'], 0, ['Lop/collection',\n"
    "    'Lop/primpattern', 'Lop/selectionrule'])";
PRM_ChoiceList thePatternMenu(PRM_CHOICELIST_TOGGLE,
    thePatternMenuScript, CH_PYTHON_SCRIPT);
PRM_Name theIsPathExpressionName("ispathexpression",
    "Pattern Is USD Path Expression");

PRM_Name theOptionsFolderName("options", "Collection Options");
PRM_Default theOptionsSwitcherInfo(5, "Collection Options");

PRM_Name theDoExclusionsName("doexclusions", "Add Exclusions");
PRM_Name theExcludePatternName("excludepattern", "Exclude Primitives");
PRM_Conditional theExcludePatternCondition("{ doexclusions == 0 }",
    PRM_CONDTYPE_DISABLE);

PRM_Name theIconName("icon", "Icon");
PRM_Name theExpansionRuleName("expansionrule", "Expansion Rule");
PRM_Default theExpansionRuleDefault(0, "explicitOnly");
PRM_Name theExpansionRuleChoices[] = {
    PRM_Name(HUSD_Constants::getExpansionExplicit(),
        "No Expansion"),
    PRM_Name(HUSD_Constants::getExpansionExpandPrims(),
        "Expand Primitives"),
    PRM_Name(HUSD_Constants::getExpansionExpandPrimsAndProperties(),
        "Expand Primitives and Properties"),
    PRM_Name()
};
PRM_ChoiceList theExpansionRuleMenu(PRM_CHOICELIST_SINGLE,
    theExpansionRuleChoices);
PRM_Name theAllowInstanceProxiesName("allowinstanceproxies",
    "Allow Instance Proxies in Collection");

const PRM_Template theDefaultCollectionParmTemplate(
    PRM_STRING, 1, &theDefaultName, 0,
    &thePatternMenu, 0, 0,
    &thePrimPatternWithProxiesSpareData);

PRM_Template	 theCollectionExtraTemplates[] = {
    PRM_Template(PRM_TOGGLE, 1, &theIsPathExpressionName),
    PRM_Template(PRM_SWITCHER, 1, &theOptionsFolderName,
        &theOptionsSwitcherInfo, 0, 0, 0,
        &PRM_SpareData::groupTypeCollapsible),
    PRM_Template(PRM_TOGGLE, 1, &theDoExclusionsName),
    PRM_Template(PRM_STRING, 1, &theExcludePatternName, 0,
        &thePatternMenu, 0, 0,
        &thePrimPatternWithProxiesSpareData, 1, 0,
        &theExcludePatternCondition),
    PRM_Template(PRM_ICONFILE, 1, &theIconName),
    PRM_Template(PRM_STRING, 1, &theExpansionRuleName,
        &theExpansionRuleDefault, &theExpansionRuleMenu),
    PRM_Template(PRM_TOGGLE, 1, &theAllowInstanceProxiesName),
    PRM_Template(),
};

class AttribInfo
{
public:
    PRM_Template	 myTemplate = theDefaultStringTemplate;
    ValueConverter	 myValueConverter = theDefaultConverter;
    ValueConverter	 myArrayValueConverter = theDefaultConverter;
};

const PRM_Template &
husdGetTemplateForRelationship()
{
    return theDefaultStringTemplate;
}

const PRM_Template &
husdGetTemplateForRamp(bool color_ramp)
{
    if (color_ramp)
        return theDefaultColorRampTemplate;

    return theDefaultFloatRampTemplate;
}

const PRM_Template &
husdGetTemplateForCollection()
{
    return theDefaultCollectionParmTemplate;
}

const PRM_Template &
husdGetTemplateForTransform()
{
    static PRM_Name		 theTransformChoices[] = {
	PRM_Name("append", "Append"),
	PRM_Name("prepend", "Prepend"),
	PRM_Name("overwriteorappend", "Overwrite or Append"),
	PRM_Name("overwriteorprepend", "Overwrite or Prepend"),
	PRM_Name("world", "Apply Transform in World Space"),
	PRM_Name("replace", "Replace All Local Transforms"),
        PRM_Name("xformcommonapi", "Apply XformCommonAPI"),
	PRM_Name("extendedxformcommonapi", "Apply Extended XformCommonAPI (with Shear + Pivot Rotate)"),
        PRM_Name()
    };
    static PRM_Default		 theTransformDefault(0,
					theTransformChoices[6].getToken());
    static PRM_ChoiceList	 theTransformMenu(PRM_CHOICELIST_SINGLE,
					theTransformChoices);
    static PRM_Template		 theTransformTemplate(PRM_STRING, 1,
					&theDefaultName,
					&theTransformDefault,
					&theTransformMenu);

    return theTransformTemplate;
}

const AttribInfo &
husdGetAttribInfoForValueType(const UT_StringRef &scalartypename)
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

	{ "asset"_sh, { theFileTemplate, theAssetConverter,
	    theArrayAssetConverter } },

	{ "bool"_sh, { theBoolTemplate, theScalarConverter<int>,
		theArrayScalarConverter<int> } },

	{ "color3d"_sh, { theColor3Template, theVecConverter<GfVec3d>,
		theArrayVecConverter<GfVec3d> } },
	{ "color3f"_sh, { theColor3Template, theVecConverter<GfVec3f>,
		theArrayVecConverter<GfVec3f> } },
	{ "color3h"_sh, { theColor3Template, theVecConverter<GfVec3f>,
		theArrayVecConverter<GfVec3f> } },

	{ "color4d"_sh, { theColor4Template, theVecConverter<GfVec4d>,
		theArrayVecConverter<GfVec4d> } },
	{ "color4f"_sh, { theColor4Template, theVecConverter<GfVec4f>,
		theArrayVecConverter<GfVec4f> } },
	{ "color4h"_sh, { theColor4Template, theVecConverter<GfVec4f>,
		theArrayVecConverter<GfVec4f> } },

	{ "timecode"_sh, { theFloatTemplate, theScalarConverter<SdfTimeCode>,
		theArrayScalarConverter<SdfTimeCode> } },
	{ "double"_sh, { theFloatTemplate, theScalarConverter<fpreal64>,
		theArrayScalarConverter<fpreal64> } },
	{ "float"_sh, { theFloatTemplate, theScalarConverter<fpreal32>,
		theArrayScalarConverter<fpreal32> } },
	{ "half"_sh, { theFloatTemplate, theScalarConverter<fpreal32>,
		theArrayScalarConverter<fpreal32> } },

	{ "double2"_sh, { theFloat2Template, theVecConverter<GfVec2d>,
		theArrayVecConverter<GfVec2d> } },
	{ "float2"_sh, { theFloat2Template, theVecConverter<GfVec2f>,
		theArrayVecConverter<GfVec2f> } },
	{ "half2"_sh, { theFloat2Template, theVecConverter<GfVec2f>,
		theArrayVecConverter<GfVec2f> } },
	{ "texCoord2d"_sh, { theFloat2Template, theVecConverter<GfVec2d>,
		theArrayVecConverter<GfVec2d> } },
	{ "texCoord2f"_sh, { theFloat2Template, theVecConverter<GfVec2f>,
		theArrayVecConverter<GfVec2f> } },
	{ "texCoord2h"_sh, { theFloat2Template, theVecConverter<GfVec2f>,
		theArrayVecConverter<GfVec2f> } },

	{ "double3"_sh, { theFloat3Template, theVecConverter<GfVec3d>,
		theArrayVecConverter<GfVec3d> } },
	{ "float3"_sh, { theFloat3Template, theVecConverter<GfVec3f>,
		theArrayVecConverter<GfVec3f> } },
	{ "half3"_sh, { theFloat3Template, theVecConverter<GfVec3f>,
		theArrayVecConverter<GfVec3f> } },
	{ "normal3d"_sh, { theFloat3Template, theVecConverter<GfVec3d>,
		theArrayVecConverter<GfVec3d> } },
	{ "normal3f"_sh, { theFloat3Template, theVecConverter<GfVec3f>,
		theArrayVecConverter<GfVec3f> } },
	{ "normal3h"_sh, { theFloat3Template, theVecConverter<GfVec3f>,
		theArrayVecConverter<GfVec3f> } },
	{ "point3d"_sh, { theFloat3Template, theVecConverter<GfVec3d>,
		theArrayVecConverter<GfVec3d> } },
	{ "point3f"_sh, { theFloat3Template, theVecConverter<GfVec3f>,
		theArrayVecConverter<GfVec3f> } },
	{ "point3h"_sh, { theFloat3Template, theVecConverter<GfVec3f>,
		theArrayVecConverter<GfVec3f> } },
	{ "vector3d"_sh, { theFloat3Template, theVecConverter<GfVec3d>,
		theArrayVecConverter<GfVec3d> } },
	{ "vector3f"_sh, { theFloat3Template, theVecConverter<GfVec3f>,
		theArrayVecConverter<GfVec3f> } },
	{ "vector3h"_sh, { theFloat3Template, theVecConverter<GfVec3f>,
		theArrayVecConverter<GfVec3f> } },
	{ "texCoord3d"_sh, { theFloat3Template, theVecConverter<GfVec3d>,
		theArrayVecConverter<GfVec3d> } },
	{ "texCoord3f"_sh, { theFloat3Template, theVecConverter<GfVec3f>,
		theArrayVecConverter<GfVec3f> } },
	{ "texCoord3h"_sh, { theFloat3Template, theVecConverter<GfVec3f>,
		theArrayVecConverter<GfVec3f> } },

	{ "double4"_sh, { theFloat4Template, theVecConverter<GfVec4d>,
		theArrayVecConverter<GfVec4d> } },
	{ "float4"_sh, { theFloat4Template, theVecConverter<GfVec4f>,
		theArrayVecConverter<GfVec4f> } },
	{ "half4"_sh, { theFloat4Template, theVecConverter<GfVec4f>,
		theArrayVecConverter<GfVec4f> } },
	{ "quatd"_sh, { theFloat4Template, theVecConverter<GfVec4d>,
		theArrayVecConverter<GfVec4d> } },
	{ "quatf"_sh, { theFloat4Template, theVecConverter<GfVec4f>,
		theArrayVecConverter<GfVec4f> } },
	{ "quath"_sh, { theFloat4Template, theVecConverter<GfVec4f>,
		theArrayVecConverter<GfVec4f> } },

	{ "matrix2d"_sh, { theFloat4Template, theMatConverter<GfMatrix2d>,
		theArrayMatConverter<GfMatrix2d> } },
	{ "matrix3d"_sh, { theFloat9Template, theMatConverter<GfMatrix3d>,
		theArrayMatConverter<GfMatrix3d> } },
	{ "matrix4d"_sh, { theFloat16Template, theMatConverter<GfMatrix4d>,
		theArrayMatConverter<GfMatrix4d> } },
	{ "frame4d"_sh, { theFloat16Template, theMatConverter<GfMatrix4d>,
		theArrayMatConverter<GfMatrix4d> } },

	{ "int"_sh, { theIntTemplate, theScalarConverter<int>,
		theArrayScalarConverter<int> } },
	{ "int64"_sh, { theIntTemplate, theScalarConverter<int64>,
		theArrayScalarConverter<int64> } },
	{ "int2"_sh, { theInt2Template, theVecConverter<GfVec2i>,
		theArrayVecConverter<GfVec2i> } },
	{ "int3"_sh, { theInt3Template, theVecConverter<GfVec3i>,
		theArrayVecConverter<GfVec3i> } },
	{ "int4"_sh, { theInt4Template, theVecConverter<GfVec4i>,
		theArrayVecConverter<GfVec4i> } },

	{ "uchar"_sh, { theUIntTemplate, theScalarConverter<uchar>,
		theArrayScalarConverter<uchar> } },
	{ "uint"_sh, { theUIntTemplate, theScalarConverter<uint>,
		theArrayScalarConverter<uint> } },
	{ "uint64"_sh, { theUIntTemplate, theScalarConverter<uint64>,
		theArrayScalarConverter<uint64> } },
    });

    return theTemplateMap[scalartypename];
}

} // end anonymous namespace

HUSD_PropertyHandle::HUSD_PropertyHandle()
{
}

HUSD_PropertyHandle::HUSD_PropertyHandle(const HUSD_PrimHandle &prim_handle,
	const UT_StringRef &property_name)
    : HUSD_ObjectHandle(prim_handle.path().appendProperty(property_name)),
      myPrimHandle(prim_handle)
{
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

    // Cannot be affected by our overrides layers, so no need to check them,
    // ragardless of what our overridesHandling value is.
    return lock.obj().IsCustom();
}

bool
HUSD_PropertyHandle::isXformOp() const
{
    XUSD_AutoObjectLock<UsdProperty> lock(*this);

    if (!lock.obj())
	return false;

    // Cannot be affected by our overrides layers, so no need to check them,
    // ragardless of what our overridesHandling value is.
    return UsdGeomXformOp::IsXformOp(lock.obj().GetName());
}

UT_StringHolder
HUSD_PropertyHandle::getSourceSchema() const
{
    XUSD_AutoObjectLock<UsdPrim> lock(myPrimHandle);

    // Cannot be affected by our overrides layers, so no need to check them,
    // ragardless of what our overridesHandling value is.
    if (lock.obj())
    {
	UsdSchemaRegistry	&registry = UsdSchemaRegistry::GetInstance();
	TfTokenVector		 schemas = lock.obj().GetAppliedSchemas();

	if (schemas.size() > 0)
	{
	    TfToken		 tfname(path().nameStr().toStdString());

	    for (auto &&schema : schemas)
	    {
                const UsdPrimDefinition *primdef =
                    registry.FindAppliedAPIPrimDefinition(schema);
                if (primdef && primdef->GetSchemaPropertySpec(tfname))
                    return schema.GetText();
	    }
	}
    }

    return UT_StringHolder::theEmptyString;
}
    
UT_StringHolder	
HUSD_PropertyHandle::getTypeDescription() const
{
    XUSD_AutoObjectLock<UsdProperty> prop_lock(*this);

    UsdAttribute attr = prop_lock.obj().As<UsdAttribute>();
    if( attr )
	return UT_StringHolder( attr.GetTypeName().GetAsToken().GetText() );

    UsdRelationship rel = prop_lock.obj().As<UsdRelationship>();
    if( rel )
	return "relationship"_UTsh;

    SdfValueTypeName input_type;
    UT_StringHolder  input_name( SdfPath::StripPrefixNamespace( 
		path().nameStr().toStdString(), UsdShadeTokens->inputs ).first);
    XUSD_AutoObjectLock<UsdPrim> prim_lock(myPrimHandle);
    if( XUSD_ShaderRegistry::getShaderInputInfo( prim_lock.obj(), 
		input_name, &input_type))
	return UT_StringHolder( input_type.GetAsToken().GetText() );
    
    return UT_StringHolder();
}


UT_UniquePtr<PI_EditScriptedParm>
HUSD_PropertyHandle::createScriptedControlParm(
	const UT_StringHolder &propbasename,
        const UT_StringRef &usdvaluetype,
        const UT_StringRef &usdvaluename,
        bool add_value_control_items,
        bool add_connection_control_items)
{
    static PRM_Name	 theControlName("control", "control");
    static PRM_Template	 theControlParm(PRM_STRING, 1, &theControlName);

    UT_String	    propname(propbasename);
    UT_String	    proplabel(propname);
    const char *    for_value = add_value_control_items
                                ? "True" : "False";
    const char *    for_connection = add_connection_control_items
                                ? "True" : "False";
    UT_WorkBuffer   menuscript;

    menuscript.sprintf("import loputils\n"
        "return loputils.createEditPropertiesControlMenu(\n"
        "           kwargs, '%s', '%s', %s, %s)",
        usdvaluetype.c_str(), usdvaluename.c_str(), for_value, for_connection);

    auto parm = UTmakeUnique<PI_EditScriptedParm>(theControlParm, 
	    nullptr, false);
    propname.append("_control");
    parm->myName = UT_VarEncode::encodeParm(propname);
    parm->myLabel = proplabel;
    parm->myDefaults[0] = "set";
    parm->myDefaultsStringMeaning[0] = CH_STRING_LITERAL;
    parm->myMenuEnable = PI_MENU_SCRIPT;
    parm->myMenuType = PI_MENU_JOIN;
    parm->myMenuScript = menuscript.buffer();
    parm->myMenuScriptLanguage = CH_PYTHON_SCRIPT;
    parm->addSpareData(&PRM_SpareData::lookIcon);

    return parm;
}

UT_StringHolder
HUSD_PropertyHandle::getScriptedControlDisableCondition(
	const UT_StringRef &ctrl_parm_name,
        const UT_StringRef &usdvaluetype,
        const UT_StringRef &usdvaluename)
{
    UT_StringArray disable_values({"block"_UTsh, "none"_UTsh});

    // We detect a control of an input attribute by its namespace.
    // Input attributes can be disconnected, so add a menu entry for it.
    UT_StringHolder prop_name(usdvaluename);
    if (!prop_name.isstring())
        prop_name = UT_VarEncode::decodeParm(ctrl_parm_name);
    if (prop_name.startsWith("inputs:") || prop_name.startsWith("outputs:"))
	disable_values.append("disconnectinput"_UTsh); 

    UT_WorkBuffer buffer;
    for (auto &&v : disable_values )
    {
	if (!buffer.isEmpty() )
	    buffer.append(' ');

	buffer.append("{ ");
	buffer.append(ctrl_parm_name);
	buffer.append(" == ");
	buffer.append(v);
	buffer.append(" }");
    }

    return UT_StringHolder(buffer);
}

UT_StringHolder
husdGetBaseName(const UT_StringRef &custom_name,
        const UT_StringRef &prop_name,
        bool is_xform_prop,
        bool is_collection_prop)
{
    UT_StringHolder      prop_base_name;

    if (custom_name.isstring())
        prop_base_name = custom_name;
    else
        prop_base_name = prop_name;

    if (is_xform_prop && custom_name.isstring())
    {
	UT_StringHolder	 xform_type;

	// If a custom name was provided, it may not be a valid xformOp name.
	// In this case we must treat it as if the custom_name is just the
	// transform op suffix.
	if (!HUSDisXformAttribute(prop_base_name, &xform_type) ||
	    UsdGeomXformOp::GetOpTypeEnum(TfToken(xform_type.toStdString())) !=
		UsdGeomXformOp::TypeTransform)
	{
	    prop_base_name = UsdGeomXformOp::GetOpName(
		UsdGeomXformOp::TypeTransform,
		TfToken(prop_base_name.toStdString())).GetString();
	}
    }
    if (is_collection_prop && custom_name.isstring())
    {
        // If a custom name was provided, it may not be a valid collection
        // name. Make sure it has the "collection:" prefix.
        if (!custom_name.startsWith(HUSD_Constants::getCollectionPrefix()))
        {
            prop_base_name = HUSD_Constants::getCollectionPrefix();
            prop_base_name += custom_name;
        }
    }

    return prop_base_name;
}

void
husdAppendParms(UT_Array<PI_EditScriptedParm *> &parms,
    PI_EditScriptedParms &add_parms,
    const UT_StringRef &prop_base_name,
    const UT_StringRef &disable_cond,
    bool prefix_parms)
{
    UT_StringMap<UT_StringHolder> renamemap;

    // If requested, build a map of old parm names to their new values.
    if (prefix_parms)
    {
        // Special case of the HUSD_XFORM_STYLE_PARAMETER_TOKEN token, which
        // we want to replace with the name of the xform style menu that
        // defines the start of this block of xform parameters.
        renamemap.emplace(HUSD_XFORM_STYLE_PARAMETER_TOKEN,
            UT_VarEncode::encodeParm(prop_base_name));
        for (int i = 0, n = add_parms.getNParms(); i < n; i++)
        {
            auto *add_parm = add_parms.getParm(i);
            UT_StringHolder oldname = add_parm->myName;
            UT_WorkBuffer propname;

            propname = prop_base_name;
            propname.append('_');
            propname.append(oldname);
            add_parm->myName = UT_VarEncode::encodeParm(propname);
            renamemap.emplace(oldname, add_parm->myName);
        }
    }

    for (int i = 0, n = add_parms.getNParms(); i < n; i++)
    {
        auto *add_parm = new PI_EditScriptedParm(*add_parms.getParm(i));

        // Fix conditions by replacing any altered parm names, and adding
        // the shared disable_cond value to any existing condition.
        for (int j = 0; j < NB_PRM_CONDTYPES; j++)
        {
            if (j == PRM_CONDTYPE_DISABLE ||
                add_parm->myConditional[j].isstring())
            {
                UT_String new_cond(add_parm->myConditional[j]);
                for (auto &&it : renamemap)
                    new_cond.changeWord(it.first, it.second);
                if (j == PRM_CONDTYPE_DISABLE)
                    new_cond.append(disable_cond);
                add_parm->myConditional[j] = new_cond.c_str();
            }
        }
        parms.append(add_parm);
    }
}

PI_EditScriptedParm *
husdNewParmFromXform(const UT_StringHolder &prop_base_name,
	bool prefix_xform_parms)
{
    PRM_Template	 tplate = husdGetTemplateForTransform();

    auto *parm = new PI_EditScriptedParm(tplate, nullptr, false);
    parm->setSpareValue(HUSD_PROPERTY_VALUETYPE, HUSD_PROPERTY_VALUETYPE_XFORM);
    if (prefix_xform_parms)
    {
	UT_String	 prefix(prop_base_name);

	prefix.append("_");
	parm->setSpareValue(HUSD_PROPERTY_XFORM_PARM_PREFIX, prefix);
    }

    return parm;
}

void
husdAppendParmsFromXform( UT_Array<PI_EditScriptedParm *> &parms,
	const UT_StringRef &prop_base_name, bool prefix_xform_parms,
	const UT_StringRef &disable_cond, bool include_lookat)
{
    PRM_Template *tplates = include_lookat
        ? theXformWithLookAtTemplates
        : theXformTemplates;
    PI_EditScriptedParms xformparms(nullptr, tplates, false, false, false);
    husdAppendParms(parms, xformparms, prop_base_name,
        disable_cond, prefix_xform_parms);
}

PI_EditScriptedParm *
husdNewParmFromRamp(const UsdAttribute &attr,
	const UT_StringRef &prop_base_name,
        bool is_color_ramp)
{
    static TfToken   theRampCountAttrKey(
                        std::string(HUSD_PROPERTY_RAMPCOUNTATTR_KEY));
    static TfToken   theRampBasisAttrKey(
                        std::string(HUSD_PROPERTY_RAMPBASISATTR_KEY));
    static TfToken   theRampBasisIsArrayKey(
                        std::string(HUSD_PROPERTY_RAMPBASISISARRAY_KEY));
    static TfToken   theRampPosAttrKey(
                        std::string(HUSD_PROPERTY_RAMPPOSATTR_KEY));

    PRM_Template     tplate = husdGetTemplateForRamp(is_color_ramp);
    std::string      rampvaluename = prop_base_name.c_str();

    auto *parm = new PI_EditScriptedParm(tplate, nullptr, false);
    parm->setSpareValue(HUSD_PROPERTY_VALUETYPE,
        HUSD_PROPERTY_VALUETYPE_RAMP);

    VtValue countattr = attr.GetCustomDataByKey(theRampCountAttrKey);
    if (countattr.IsHolding<std::string>())
        parm->setSpareValue(HUSD_PROPERTY_RAMPCOUNTNAME,
            countattr.UncheckedGet<std::string>().c_str());

    VtValue basisattr = attr.GetCustomDataByKey(theRampBasisAttrKey);
    if (basisattr.IsHolding<std::string>())
        parm->setSpareValue(HUSD_PROPERTY_RAMPBASISNAME,
            basisattr.UncheckedGet<std::string>().c_str());
    else
        parm->setSpareValue(HUSD_PROPERTY_RAMPBASISNAME,
            (rampvaluename + HUSD_PROPERTY_RAMPBASISSUFFIX).c_str());

    VtValue basisisarray = attr.GetCustomDataByKey(theRampBasisIsArrayKey);
    if (basisisarray.IsHolding<bool>())
        parm->setSpareValue(HUSD_PROPERTY_RAMPBASISISARRAY,
            basisisarray.UncheckedGet<bool>() ? "1" : "0");
    else
        parm->setSpareValue(HUSD_PROPERTY_RAMPBASISISARRAY, "1");

    VtValue posattr = attr.GetCustomDataByKey(theRampPosAttrKey);
    if (posattr.IsHolding<std::string>())
        parm->setSpareValue(HUSD_PROPERTY_RAMPPOSNAME,
            posattr.UncheckedGet<std::string>().c_str());
    else
        parm->setSpareValue(HUSD_PROPERTY_RAMPPOSNAME,
            (rampvaluename + HUSD_PROPERTY_RAMPPOSSUFFIX).c_str());

    return parm;
}

void
husdAppendParmsFromCollection( UT_Array<PI_EditScriptedParm *> &parms,
    const UT_StringRef &prop_base_name, const UT_StringRef &disable_cond)
{
    PRM_Template *tplates = theCollectionExtraTemplates;
    PI_EditScriptedParms collparms(nullptr, tplates, false, false, false);
    husdAppendParms(parms, collparms, prop_base_name, disable_cond, true);
}

PI_EditScriptedParm *
husdNewParmFromCollection()
{
    PRM_Template        tplate = husdGetTemplateForCollection();

    auto *parm = new PI_EditScriptedParm(tplate, nullptr, false);
    parm->setSpareValue(HUSD_PROPERTY_VALUETYPE,
        HUSD_PROPERTY_VALUETYPE_COLLECTION);

    return parm;
}

PI_EditScriptedParm *
husdNewParmFromAttrib( const UsdAttribute &attr, 
	const UT_StringHolder &source_schema )
{
    SdfValueTypeName	valuetype = attr.GetTypeName();
    SdfValueTypeName	scalartype = valuetype.GetScalarType();
    // Special case: opaque attributes are not meant to be read from or
    // written to, so we shouldn't create parms for them.
    if (scalartype == SdfValueTypeNames->Opaque)
        return nullptr;

    UT_StringRef	scalartypename = scalartype.GetAsToken().GetText();
    AttribInfo		info = husdGetAttribInfoForValueType(scalartypename);
    VtValue		value;

    auto *parm = new PI_EditScriptedParm(info.myTemplate, nullptr, false);
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
    
    return parm;
}

bool
husdIsCollectionMatBinding(const UsdRelationship &rel)
{
    return TfStringStartsWith(rel.GetName(), UsdShadeTokens->materialBinding);
}

PI_EditScriptedParm *
husdNewParmFromRel( const UsdRelationship &rel,
        const UT_StringHolder &source_schema )
{
    PRM_Template	 tplate = husdGetTemplateForRelationship();
    SdfPathVector	 targets;
    UT_WorkBuffer	 targets_buf;

    auto *parm = new PI_EditScriptedParm(tplate, nullptr, false);
    parm->setSpareValue(HUSD_PROPERTY_VALUETYPE,
	HUSD_PROPERTY_VALUETYPE_RELATIONSHIP);
    if (source_schema.isstring())
        parm->setSpareValue(HUSD_PROPERTY_APISCHEMA, source_schema);
    // Don't expand collection to a set of prim paths, or binding will break.
    if (husdIsCollectionMatBinding(rel))
	parm->setSpareValue(HUSD_PROPERTY_KEEPCOLLECTIONS, "1");

    rel.GetTargets(&targets);
    for (auto &&target : targets)
    {
	if (!targets_buf.isEmpty())
	    targets_buf.append(' ');
	targets_buf.append(target.GetString());
    }
    parm->myDefaults[0] = std::move(targets_buf);

    return parm;
}

PI_EditScriptedParm *
husdNewParmFromShaderInput( const HUSD_PrimHandle &prim_handle,
	const UT_StringRef &attrib_name )
{
    XUSD_AutoObjectLock<UsdPrim> lock(prim_handle);
    UT_StringHolder input_name( SdfPath::StripPrefixNamespace( 
		attrib_name.toStdString(), UsdShadeTokens->inputs ).first );

    SdfValueTypeName	sdf_input_type;
    VtValue		default_value;
    if( !XUSD_ShaderRegistry::getShaderInputInfo(lock.obj(), input_name, 
		&sdf_input_type, &default_value))
    {
	return nullptr;
    }

    SdfValueTypeName	scalartype = sdf_input_type.GetScalarType();
    UT_StringRef	scalartypename = scalartype.GetAsToken().GetText();
    AttribInfo attr_info = husdGetAttribInfoForValueType(scalartypename);
    auto *parm = new PI_EditScriptedParm(attr_info.myTemplate, nullptr, false);

    parm->myName  = attrib_name;
    parm->setSpareValue(HUSD_PROPERTY_VALUETYPE, 
	    sdf_input_type.GetAsToken().GetText());
    parm->setSpareValue(HUSD_PROPERTY_ISCUSTOM, "0");

    if (!default_value.IsEmpty())
    {
	if (default_value.IsArrayValued())
	    attr_info.myArrayValueConverter(default_value, parm->myDefaults);
	else
	    attr_info.myValueConverter(default_value, parm->myDefaults);
    }

    return parm;
}

UT_StringHolder
husdGetShaderInputLabel( const HUSD_PrimHandle &prim_handle,
	const UT_StringRef &attrib_name )
{
    XUSD_AutoObjectLock<UsdPrim> lock(prim_handle);
    UT_StringHolder input_name( SdfPath::StripPrefixNamespace( 
		attrib_name.toStdString(), UsdShadeTokens->inputs ).first );

    UT_StringHolder label;
    XUSD_ShaderRegistry::getShaderInputInfo(lock.obj(), input_name, 
	    nullptr, nullptr, &label);
    return label;
}

UT_StringHolder
husdGetShaderOutputLabel( const UT_StringRef &attrib_name )
{
    auto names = SdfPath::TokenizeIdentifier( SdfPath::StripPrefixNamespace( 
                attrib_name.toStdString(), UsdShadeTokens->outputs ).first );

    UT_WorkBuffer buffer;
    for( auto &name : names)
    {
        name[0] = ::toupper( name[0] );
        buffer.append( name );
        buffer.append(' ');
    }
    buffer.append("Output");

    return UT_StringHolder(std::move(buffer));
}

UT_StringHolder
husdGetParmLabel( const UT_StringRef &prop_name, const UT_StringRef &prop_label,
        const HUSD_PrimHandle &prim_handle, 
        const UT_StringRef &suffix = UT_StringRef())
{
    UT_StringHolder label(prop_label);

    if (!label.isstring() && 
	    prop_name.startsWith(UsdShadeTokens->inputs.GetText()))
	label = husdGetShaderInputLabel(prim_handle, prop_name);

    if (!label.isstring() && 
	    prop_name.startsWith(UsdShadeTokens->outputs.GetText()))
	label = husdGetShaderOutputLabel(prop_name);

    if (!label.isstring())
	label = prop_name;

    if (suffix.isstring())
    {
        UT_WorkBuffer buffer(label);
        buffer.append(' ');
        buffer.append(suffix);
        label = std::move(buffer);
    }

    return label;
}


void
HUSD_PropertyHandle::createScriptedParms(
	UT_Array<PI_EditScriptedParm *> &parms,
	const UT_StringRef &custom_name,
	bool prepend_control_parm,
	bool prefix_xform_parms) const
{
    static TfToken       theRampValueAttrKey(
                            std::string(HUSD_PROPERTY_RAMPVALUEATTR_KEY));
    static TfToken       theCollectionAttrKey(
                            std::string(HUSD_PROPERTY_COLLECTIONATTR_KEY));

    XUSD_AutoObjectLock<UsdProperty> lock(*this);
    UsdProperty          prop;
    UsdAttribute	 attr;
    UsdRelationship	 rel;
    TfToken              collection_name;
    UT_StringHolder	 prop_base_label;
    UT_StringHolder      help_text;
    bool                 is_collection = false;
    bool		 is_xform_op = false;
    bool		 is_float_ramp = false;
    bool		 is_color_ramp = false;
    bool                 include_lookat = false;

    if (lock.obj())
    {
        prop = lock.obj().As<UsdProperty>();
	attr = lock.obj().As<UsdAttribute>();
	rel = lock.obj().As<UsdRelationship>();
	prop_base_label = lock.obj().GetDisplayName();
        help_text = lock.obj().GetDocumentation();
    }

    if (UsdGeomXformOp::IsXformOp(attr))
    {
	UsdGeomXformOp	 xformop(attr);

	if (xformop && xformop.GetOpType() == UsdGeomXformOp::TypeTransform)
        {
            is_xform_op = true;
            auto custom_data = attr.GetCustomData();
            auto it = custom_data.find(HUSD_PROPERTY_XFORMOP_INCLUDE_LOOKAT);
            if (it != custom_data.end() && it->second.IsHolding<bool>())
                include_lookat = it->second.UncheckedGet<bool>();
        }
    }
    else if (UsdCollectionAPI::IsCollectionAPIPath(
                prop.GetPath(),
                &collection_name) ||
             UsdCollectionAPI::IsCollectionAPIPath(
                prop.GetPrimPath().AppendProperty(prop.GetNamespace()),
                &collection_name))
    {
        UsdCollectionAPI collection_api;

        collection_api = UsdCollectionAPI(prop.GetPrim(), collection_name);
        if (!collection_api)
        {
            // Special case for a collection-like attribute explicitly added
            // to a prim by schemautils.py.
            VtValue iscollattr = attr.GetCustomDataByKey(theCollectionAttrKey);
            if (!iscollattr.IsHolding<bool>() ||
                !iscollattr.UncheckedGet<bool>())
                return;
            // Update the "base label" (the label for the primary parm) if it
            // isn't set as the attrib display name and the user provided a
            // custom name for the collection.
            if (!prop_base_label.isstring() && custom_name.isstring())
                prop_base_label = husdGetBaseName(custom_name,
                    collection_name.GetText(),
                    /*is_xform_prop=*/ false,
                    /*is_collection_prop=*/ true);
        }
        else
        {
            // For any collection-related attribute, we want to create the parms
            // from the basic "collection attribute".
            attr = collection_api.GetCollectionAttr();
        }
        if (!attr)
            return;
        rel = UsdRelationship();
        prop = attr;
        is_collection = true;
    }
    else if (attr)
    {
        VtValue rampvalueattr = attr.GetCustomDataByKey(theRampValueAttrKey);

        if (rampvalueattr.IsHolding<std::string>())
        {
            std::string valueattr = rampvalueattr.UncheckedGet<std::string>();

            if (UTisstring(valueattr.c_str()))
            {
                // We want to create the node parameter using the value
                // attribute as the primary source. This is because it's the
                // value attribute that has the information required about the
                // data type for the ramp.
                attr = attr.GetPrim().GetAttribute(TfToken(valueattr));
                if (!attr)
                    return;
                prop = attr;

                if (attr.GetTypeName().GetScalarType().GetDimensions().size==0)
                    is_float_ramp = true;
                else
                    is_color_ramp = true;
            }
        }
    }

    // The choice of source attribute may have changed if we are creating a
    // ramp parameter from one of the ramp attributes other than the value
    // attribute. The ramp parameter must always be created with the value
    // attribute as its name. Similarly for collection attributes/parameters.
    UT_StringHolder	 name(prop
                            ? prop.GetName().GetString().c_str()
                            : path().nameStr().c_str());
    UT_StringHolder      prop_base_name = husdGetBaseName(custom_name,
                            name, is_xform_op, is_collection);
    UT_String		 prop_name(prop_base_name);
    UT_String		 prop_label(prop_base_label);

    PI_EditScriptedParm	*parm = nullptr;

    if (is_xform_op)
	parm = husdNewParmFromXform(prop_base_name, prefix_xform_parms);
    else if (is_float_ramp || is_color_ramp)
        parm = husdNewParmFromRamp(attr, prop_base_name, is_color_ramp);
    else if (is_collection)
        parm = husdNewParmFromCollection();
    else if (attr)
	parm = husdNewParmFromAttrib(attr, getSourceSchema());
    else if (rel)
	parm = husdNewParmFromRel(rel, getSourceSchema());
    else if (name.startsWith(UsdShadeTokens->inputs.GetText()))
	parm = husdNewParmFromShaderInput(myPrimHandle, name);

    if (!parm)
	return;

    // Encode the property name in case it is namespaced.
    parm->myName = UT_VarEncode::encodeParm(prop_name);
    parm->myLabel = husdGetParmLabel(name, prop_label, myPrimHandle);
    parm->myHelpText = help_text;

    UT_String		 disablecond;
    if (prepend_control_parm)
    {
        auto value_type = parm->getSpareValue(HUSD_PROPERTY_VALUETYPE);
        auto value_name = parm->getSpareValue(HUSD_PROPERTY_VALUENAME);
	auto ctrlparm = createScriptedControlParm(
                prop_base_name, value_type, value_name);

	disablecond = getScriptedControlDisableCondition(
                ctrlparm->myName, value_type, value_name);
	parm->myConditional[PRM_CONDTYPE_DISABLE] = disablecond;
	parms.append(ctrlparm.release());   // parms list takes ownership
    }

    parms.append(parm);

    // For transform ops, we now need to append all the individual xform
    // components that are used to build the transform matrix.
    if (is_xform_op)
        husdAppendParmsFromXform(parms, prop_base_name,
            prefix_xform_parms, disablecond, include_lookat);
    // Similarly for collections, we need to append additional collection
    // authoring parameters.
    if (is_collection)
        husdAppendParmsFromCollection(parms, prop_base_name, disablecond);
}

void
HUSD_PropertyHandle::createScriptedConnectionParms(
        UT_Array<PI_EditScriptedParm *> &parms,
        const UT_StringRef &custom_name,
        bool prepend_control_parm) const
{
    static PRM_Name	 theSourceName("source", "Source");
    static PRM_Template	 theSourceParm(PRM_STRING, 1, &theSourceName);

    // Get attribute info.
    XUSD_AutoObjectLock<UsdProperty> lock(*this);
    UsdAttribute	 attr;
    UT_StringHolder	 label;
    UT_StringHolder      help_text;
    if (lock.obj())
    {
	attr = lock.obj().As<UsdAttribute>();
	label = lock.obj().GetDisplayName();
        help_text  = lock.obj().GetDocumentation();
    }

    // Figure out name and label for the parm.
    UT_StringHolder	 attr_name(attr
                            ? attr.GetName().GetString().c_str()
                            : path().nameStr().c_str());
    UT_StringHolder      attr_type(attr
                            ? attr.GetTypeName().GetAsToken().GetText()
                            : nullptr);
    UT_StringHolder      parm_name = husdGetBaseName(custom_name, attr_name,
                            /*is_xform_prop=*/ false,
                            /*is_collection_prop=*/ false );

    // If parm name comes from attrib name (rather than explicit custom name)
    // append a src suffix, to avoid collision with value parameters.
    if (!custom_name.isstring())
        parm_name += "_source";

    auto *parm = new PI_EditScriptedParm(theSourceParm, nullptr, false);

    parm->myName  = UT_VarEncode::encodeParm(parm_name);
    parm->myLabel = husdGetParmLabel(attr_name, label, myPrimHandle, "Source");
    parm->myHelpText = help_text;

    parm->setSpareValue(HUSD_PROPERTY_ISCONNECTION, "1");
    parm->setSpareValue(HUSD_PROPERTY_VALUETYPE, attr_type);
    parm->setSpareValue(HUSD_PROPERTY_VALUENAME, attr_name);


    if (prepend_control_parm)
    {
	auto ctrl_parm = createScriptedControlParm(
                parm_name, attr_type, attr_name,
                /*add_value_control_items=*/ false,
                /*add_connection_control_items=*/ true);
        
	parm->myConditional[PRM_CONDTYPE_DISABLE] = 
            getScriptedControlDisableCondition(
                ctrl_parm->myName, attr_type, attr_name);

	parms.append(ctrl_parm.release());   // parms list takes ownership
    }

    parms.append(parm);
}
