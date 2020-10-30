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



#include "XUSD_ShaderRegistry.h"

#include <UT/UT_StringArray.h>

#include <pxr/usd/usdShade/shader.h>
#include <pxr/usd/sdr/registry.h>
#include <pxr/usd/sdr/shaderNode.h>
#include <pxr/usd/sdr/shaderProperty.h>

PXR_NAMESPACE_OPEN_SCOPE

static inline SdrShaderNodeConstPtr 
husdGetSdrNode( const UsdShadeShader &shader )
{
    // Note, this function can be replaced with
    //   shader.GetShaderNodeForSourceType(UsdShadeTokens->universalSourceType);
    // if it starts returning non-null on a PxrSurface shader primitive.
    // Until then, most of this code comes from that method.
    SdrRegistry &sdr_reg = SdrRegistry::GetInstance();
    TfToken  impl_source = shader.GetImplementationSource();
    if (impl_source == UsdShadeTokens->id) 
    {
        TfToken shader_id;
        if (shader.GetShaderId(&shader_id)) 
            return sdr_reg.GetShaderNodeByIdentifier(shader_id);
    } 
    else if (impl_source == UsdShadeTokens->sourceAsset) 
    {
        SdfAssetPath source_asset;
        if (shader.GetSourceAsset(&source_asset))
            return sdr_reg.GetShaderNodeFromAsset(source_asset, 
		shader.GetSdrMetadata());
    } 
    else if (impl_source == UsdShadeTokens->sourceCode) 
    {
	// TODO: XXX: for non-vex shaders we need to pass correct sourcetype 
	// to get the appropriate parser; but how do we find the source type?
	static const TfToken	theVEXToken("VEX", TfToken::Immortal);

        std::string source_code;
        if (shader.GetSourceCode(&source_code)) 
            return sdr_reg.GetShaderNodeFromSourceCode( source_code, 
		    theVEXToken, shader.GetSdrMetadata());
    }

    return nullptr;

}

bool
XUSD_ShaderRegistry::getShaderInputNames( const UsdPrim &prim,
	UT_StringArray &input_names) 
{
    UsdShadeShader shader(prim);
    if (!shader)
	return false;

    SdrShaderNodeConstPtr sdr_node = husdGetSdrNode(shader);
    if (!sdr_node)
	return false;

    auto inputs = sdr_node->GetInputNames();
    for(auto &&input : inputs)
    	input_names.append(input.GetString());

    return true;
}

bool
XUSD_ShaderRegistry::getShaderInputInfo( const UsdPrim &prim,
	const UT_StringRef &input_name,
	SdfValueTypeName *type,
	VtValue *default_value,
	UT_StringHolder *label)
{
    UsdShadeShader shader(prim);
    if (!shader)
	return false;

    auto sdr_node = husdGetSdrNode(shader);
    if (!sdr_node)
	return false;

    auto sdr_input = sdr_node->GetShaderInput( TfToken( input_name ));
    if (!sdr_input )
	return false;

    if (type)
	*type = sdr_input->GetTypeAsSdfType().first;

    if (default_value)
	*default_value = sdr_input->GetDefaultValue();

    if (label)
	*label = sdr_input->GetLabel().GetString();

    return true;
}

PXR_NAMESPACE_CLOSE_SCOPE

