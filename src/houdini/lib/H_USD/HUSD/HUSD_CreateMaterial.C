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

#include "HUSD_CreateMaterial.h"

#include "HUSD/HUSD_Constants.h"
#include "HUSD_ErrorScope.h"
#include "HUSD_ShaderTranslator.h"
#include "XUSD_AttributeUtils.h"
#include "XUSD_Data.h"
#include "XUSD_Utils.h"

#include <VOP/VOP_Node.h>
#include <OP/OP_Input.h>
#include <pxr/usd/usdShade/material.h>
#include <pxr/usd/usd/inherits.h>
#include <pxr/usd/usd/specializes.h>

using namespace UT::Literal;

static const auto HUSD_SHADER_REFTYPE	= "shader_referencetype"_sh;
static const auto HUSD_REFTYPE_NONE	= "none"_sh;
static const auto HUSD_REFTYPE_REF	= "reference"_sh;
static const auto HUSD_REFTYPE_INHERIT	= "inherit"_sh;
static const auto HUSD_REFTYPE_SPEC	= "specialize"_sh;
static const auto HUSD_REFTYPE_REP	= "represent"_sh;
static const auto HUSD_SHADER_BASEPRIM	= "shader_baseprimpath"_sh;
static const auto HUSD_SHADER_BASEASSET	= "shader_baseassetpath"_sh;
static const auto HUSD_SHADER_PRIMTYPE	= "shader_primtype"_sh;
static const auto HUSD_IS_INSTANCEABLE	= "shader_isinstanceable"_sh;
static const auto HUSD_MAT_PRIMTYPE	= "shader_materialprimtype"_sh;
static const auto HUSD_FORCE_TERMINAL	= "shader_forceterminaloutput"_sh;
static const auto HUSD_FORCE_CHILDREN	= "shader_forcechildren"_sh;


PXR_NAMESPACE_USING_DIRECTIVE


HUSD_CreateMaterial::HUSD_CreateMaterial(HUSD_AutoWriteLock &lock,
	const HUSD_OverridesPtr &overrides )
    : myWriteLock(lock)
    , myOverrides(overrides)
{
}

static inline int
vopIntParmVal( const OP_Node &node, const UT_StringRef &parm_name, 
	int def_val = 0 )
{
    const PRM_Parm *parm = node.getParmPtr(parm_name);
    if( !parm )
	return def_val;

    int value;
    parm->getValue(0, value, 0, SYSgetSTID());
    return value;
}

static inline UT_StringHolder
vopStrParmVal( const OP_Node &node, const UT_StringRef &parm_name )
{
    const PRM_Parm *parm = node.getParmPtr(parm_name);
    if( !parm )
	return UT_StringHolder();

    UT_StringHolder value;
    parm->getValue(0, value, 0, /*expand=*/ 1, SYSgetSTID());
    return value;
}

static inline void 
husdCreateAncestors( const UsdStageRefPtr &stage, 
	const SdfPath &parent_path, const TfToken &type_name )
{
    SdfPathVector to_create;
    for( auto && it : parent_path.GetAncestorsRange() )
    {
	UsdPrim prim = stage->GetPrimAtPath( it );
	if( prim && prim.IsDefined() )
	    break;

	to_create.push_back( it );
    }

    for( auto &&it = to_create.rbegin(); it != to_create.rend(); ++it )
	stage->DefinePrim( *it, type_name );
}

static inline UsdShadeNodeGraph
husdCreateMainPrim( const UsdStageRefPtr &stage, const UT_StringRef &usd_path,
	const UT_StringRef &parent_usd_prim_type, bool is_material )
{
    SdfPath material_path( usd_path.toStdString() );

    // If needed, create the parent hierarchy first.
    if( parent_usd_prim_type.isstring() )
    {
	TfToken	parent_type_name(
	    HUSDgetPrimTypeAlias( parent_usd_prim_type ).toStdString() );
	SdfPath parent_path(
	    material_path.GetParentPath() );

	husdCreateAncestors( stage, parent_path, parent_type_name );
    }

    UsdShadeNodeGraph main_prim;
    if( is_material )
	main_prim = UsdShadeMaterial::Define( stage, material_path );
    else
	main_prim = UsdShadeNodeGraph::Define( stage, material_path );

    return main_prim;
}

static inline bool
husdTranslatesToMaterialPrim( VOP_Node &vop )
{
    // If vop has an explicit prim type set to material, then the node
    // translates directly to Material prim and not a Shader.
    UT_StringHolder prim_type( vopStrParmVal( vop, HUSD_SHADER_PRIMTYPE ));
    return prim_type == HUSD_Constants::getMaterialPrimTypeName();
}

static inline UsdShadeNodeGraph
husdCreateMainPrimForNode( VOP_Node &mat_vop,
	const UsdStageRefPtr &stage, const UT_StringRef &usd_path,
	const UT_StringRef &parent_usd_prim_type )
{
    // Check if the node has an explicit USD prim type
    UT_StringHolder prim_type( vopStrParmVal( mat_vop, HUSD_MAT_PRIMTYPE ));
    if( !prim_type.isstring() )
	prim_type = vopStrParmVal( mat_vop, HUSD_SHADER_PRIMTYPE );

    // Choose between a graph and material.
    bool is_material = true;
    if( prim_type == "NodeGraph" )
	is_material = false;
    else if( prim_type == "Material" )
	is_material = true;
    else
	is_material = !mat_vop.isUSDNodeGraph();

    // Create the material or graph.
    return husdCreateMainPrim( stage, usd_path, parent_usd_prim_type, 
	    is_material );
}

static inline bool
husdCreateMaterialShader(HUSD_AutoWriteLock &lock,
	const UT_StringRef &usd_material_path, const HUSD_TimeCode &tc,
	VOP_Node &shader_node, VOP_Type shader_type, 
	const UT_StringRef &output_name,
	const UT_IntArray &dependent_node_ids)
{
    // All VOPs can carry rendering properties, but that's not a real shader
    if( shader_type == VOP_PROPERTIES_SHADER )
	return true;

    // Find a translator for the given render target.
    HUSD_ShaderTranslator *translator = 
	HUSD_ShaderTranslatorRegistry::get().findShaderTranslator(shader_node);
    UT_ASSERT( translator );
    if( !translator )
	return false;

    translator->setDependentNodeIDs( dependent_node_ids );
    translator->createMaterialShader(lock, usd_material_path, tc,
	    shader_node, shader_type, output_name);
    return true;
}

static inline UT_StringHolder
husdCreateShader(HUSD_AutoWriteLock &lock,
	const UT_StringRef &usd_material_path, const HUSD_TimeCode &tc,
	VOP_Node &shader_node, const UT_StringRef &output_name,
	const UT_IntArray &dependent_node_ids)
{
    // Find a translator for the given render target.
    HUSD_ShaderTranslator *translator = 
	HUSD_ShaderTranslatorRegistry::get().findShaderTranslator(shader_node);
    UT_ASSERT( translator );
    if( !translator )
	return UT_StringHolder();

    translator->setDependentNodeIDs( dependent_node_ids );
    return translator->createShader(lock, usd_material_path, usd_material_path,
	    tc, shader_node, output_name);
}

static inline bool
husdUpdateShaderParameters( HUSD_AutoWriteLock &lock,
	const UT_StringRef &usd_shader_path, const HUSD_TimeCode &tc,
	VOP_Node &shader_vop, const UT_StringArray &parameter_names,
	const UT_IntArray &dependent_node_ids)
{
    // Find a translator for the given render target.
    HUSD_ShaderTranslator *translator = 
	HUSD_ShaderTranslatorRegistry::get().findShaderTranslator(shader_vop);
    UT_ASSERT( translator );
    if( !translator )
	return false;

    translator->setDependentNodeIDs( dependent_node_ids );
    translator->updateShaderParameters( lock, usd_shader_path, tc, 
            shader_vop, parameter_names );

    return true;
}

static inline UsdPrim
husdGetConnectedShaderPrim( const UsdShadeConnectionSourceInfo &info )
{
    UsdShadeConnectionSourceInfo tmp = info;
    while( tmp.source.GetPrim().IsA<UsdShadeNodeGraph>() )
    {
	UsdShadeOutput::SourceInfoVector sources;
	if( tmp.sourceType == UsdShadeAttributeType::Input )
	    sources = tmp.source.GetInput( tmp.sourceName ).
		GetConnectedSources();
	else if( tmp.sourceType == UsdShadeAttributeType::Output )
	    sources = tmp.source.GetOutput( tmp.sourceName ).
		GetConnectedSources();
	
	if( sources.size() <= 0 )
	{
	    UT_ASSERT( !"Unconnected node graph output." ); 
	    break;
	}

	// Follow the first connected source.
	tmp = sources[0];
    }

    return tmp.source.GetPrim();
}

static inline bool
husdFindSurfaceShader( UsdPrim *usd_surface_shader_prim, 
	UT_StringHolder *usd_render_context_name,
	UsdShadeNodeGraph &usd_mat_or_graph, 
	UsdPrim target_prim = UsdPrim())
{
    if( !usd_mat_or_graph )
	return false;

    // Look for any surface shader prim. See UsdMaterial::GetSurfaceOutputs().
    for( const UsdShadeOutput& output : usd_mat_or_graph.GetOutputs() )
    {
	auto components = SdfPath::TokenizeIdentifier( output.GetBaseName() );
        if(    components.size() < 2u
	    || components.back() != UsdShadeTokens->surface )
            continue;

	auto sources = output.GetConnectedSources();
	if( sources.size() <= 0 )
	    continue;
    
	auto shader_prim = husdGetConnectedShaderPrim( sources[0] );
	if( !shader_prim )
	    continue;

	if( target_prim && target_prim != shader_prim )
	    continue;

	if( usd_surface_shader_prim )
	    *usd_surface_shader_prim = shader_prim;
	if( usd_render_context_name )
	    *usd_render_context_name = components.front();
	return true;
    }

    return false;
}

static inline UsdShadeMaterial 
husdFindParentMaterial( const UsdPrim &main_shader_prim )
{
    // See python PreviewShaderTranslator._findParentMaterial()
    UsdPrim usd_prim( main_shader_prim );
    UsdShadeMaterial usd_mat;
    while( !usd_mat && usd_prim )
    {
	usd_mat  = UsdShadeMaterial( usd_prim );
	usd_prim = usd_prim.GetParent();
    }

    return usd_mat;
}

static inline bool
husdFindParentMaterialAndRenderContext( UsdShadeMaterial *usd_material_parent, 
	UT_StringHolder *usd_render_context_name,
	const UsdPrim &main_shader_prim )
{
    UsdShadeMaterial usd_mat = husdFindParentMaterial( main_shader_prim );
    if( !usd_mat )
	return false;

    // Find the render context name for the shader prim.
    if( !husdFindSurfaceShader( nullptr, usd_render_context_name, 
		usd_mat, main_shader_prim ))
	return false;

    if( usd_material_parent )
	*usd_material_parent = usd_mat;

    return true;
}

static inline void
husdUpdatePreviewShaderParameters( HUSD_AutoWriteLock &lock,
	const UT_StringRef &usd_main_shader_path,
	const HUSD_TimeCode &time_code )
{
    auto outdata = lock.data();
    if( !outdata || !outdata->isStageValid() )
	return;

    SdfPath sdf_path( usd_main_shader_path.toStdString() );
    UsdPrim main_shader_prim = outdata->stage()->GetPrimAtPath( sdf_path );
    if( !main_shader_prim )
	return;

    UT_StringHolder usd_render_context_name;
    if( !husdFindParentMaterialAndRenderContext( nullptr,
		&usd_render_context_name, main_shader_prim ))
	return;

    // Find the translator for the render context name.
    HUSD_PreviewShaderTranslator *translator = 
	HUSD_ShaderTranslatorRegistry::get().findPreviewShaderTranslator(
		usd_render_context_name );
    UT_ASSERT( translator );
    if( !translator )
	return;

    translator->updateMaterialPreviewShaderParameters( lock, 
	    usd_main_shader_path, time_code  );
}

static inline void
husdCreateAndSetMaterialAttribs( UsdShadeNodeGraph &usd_graph, VOP_Node &vop )
{
    HUSD_TimeCode time_code;

    auto parms = vop.getUSDShaderParms();
    for( const PRM_Parm *parm: parms )
    {
	auto sdf_type = HUSDgetShaderAttribSdfTypeName( *parm );
	if( !sdf_type )
	    continue;

	UsdAttribute attrib( usd_graph.CreateInput( 
		    TfToken(parm->getToken()), sdf_type ));
	HUSDsetAttribute( attrib, *parm, time_code );
    }
}

static inline bool
husdCreateMaterialInputsIfNeeded( HUSD_AutoWriteLock &lock,
	UsdShadeNodeGraph &usd_graph,
	const HUSD_TimeCode &time_code, 
	VOP_Node &mat_vop,
	const UT_IntArray &dependent_node_ids)
{
    if( !usd_graph )
	return false;

    // Create the material or graph.
    bool ok = true; 
    for (int i = 0, n = mat_vop.getNumVisibleInputs(); i < n; ++i)
    {
	OP_Input *input = mat_vop.getInputReferenceConst(i);
	if( !input )
	    continue;

	VOP_Node *input_vop = CAST_VOPNODE( input->getNode() );
	if( !input_vop )
	    continue;

	UT_String output_name;
	input_vop->getOutputName( output_name, input->getNodeOutputIndex() );

	UT_StringHolder usd_mat_path( usd_graph.GetPath().GetString() );
	UT_StringHolder usd_output_path = husdCreateShader( lock,
		usd_mat_path, time_code, *input_vop, output_name,
		dependent_node_ids);
	if( usd_output_path.isEmpty() )
	{
	    ok = false;
	    continue;
	}

	UT_StringHolder input_name;
	mat_vop.getInputName( input_name, i );
	SdfValueTypeName input_type = HUSDgetShaderInputSdfTypeName(mat_vop, i);
	UsdShadeInput usd_mat_input = usd_graph.CreateInput( 
		TfToken(input_name.toStdString()), input_type );
	if( !usd_mat_input )
	{
	    ok = false;
	    continue;
	}
	
	UsdShadeConnectableAPI::ConnectToSource( usd_mat_input, 
	    SdfPath( usd_output_path.toStdString() ));
    }

    return ok;
}


namespace { 
    enum class HUSD_PrimRefType
    {
	REFERENCE, INHERIT, SPECIALIZE 
    };

    inline bool
    husdAddBasePrim( UsdPrim &prim, HUSD_PrimRefType ref_type, 
	    const UT_StringRef &base_prim_path,
	    const UT_StringRef *base_asset_path = nullptr )
    {
	if( !base_prim_path.isstring() )
	    return false;

	bool	    ok = true;
	SdfPath	    sdf_prim_path = HUSDgetSdfPath( base_prim_path );

	std::string str_asset_path;
	if( base_asset_path )
	    str_asset_path = base_asset_path->toStdString();

	if( ref_type == HUSD_PrimRefType::REFERENCE )
	    prim.GetReferences().AddReference( 
		    SdfReference( str_asset_path, sdf_prim_path ));
	else if( ref_type == HUSD_PrimRefType::INHERIT )
	    prim.GetInherits().AddInherit( sdf_prim_path );
	else if( ref_type == HUSD_PrimRefType::SPECIALIZE )
	    prim.GetSpecializes().AddSpecialize( sdf_prim_path );
	else
	    ok = false;

	return ok;
    }

    inline bool
    husdAddBasePrim( UsdPrim &prim, HUSD_PrimRefType ref_type, VOP_Node &vop )
    {
	UT_StringHolder prim_path(  vopStrParmVal(vop, HUSD_SHADER_BASEPRIM ));
	UT_StringHolder asset_path( vopStrParmVal(vop, HUSD_SHADER_BASEASSET ));

	return husdAddBasePrim( prim, ref_type, prim_path, &asset_path );
    }

    inline bool 
    husdAddBasePrim( UsdPrim &prim, VOP_Node &vop )
    {
	UT_StringHolder ref_type( vopStrParmVal( vop, HUSD_SHADER_REFTYPE ));

	if( !ref_type.isstring() )
	    return false;
	else if( ref_type == HUSD_REFTYPE_REF )
	    return husdAddBasePrim( prim, HUSD_PrimRefType::REFERENCE, vop );
	else if( ref_type == HUSD_REFTYPE_INHERIT )
	    return husdAddBasePrim( prim, HUSD_PrimRefType::INHERIT, vop );
	else if( ref_type == HUSD_REFTYPE_SPEC )
	    return husdAddBasePrim( prim, HUSD_PrimRefType::SPECIALIZE, vop );
	else if( ref_type == HUSD_REFTYPE_REP )
	    return true; // do nothing; prim *is* the base prim

	return false;
    }

    inline void
    husdSetInstanceableIfNeeded( UsdPrim &prim, VOP_Node &vop )
    {
	if( vopIntParmVal( vop, HUSD_IS_INSTANCEABLE, /*def_val=*/ false ))
	    prim.SetInstanceable( true );
    }

    inline bool
    husdRepresentsExistingPrim( VOP_Node &vop )
    {
	UT_StringHolder ref_type( vopStrParmVal( vop, HUSD_SHADER_REFTYPE ));

	return ref_type == HUSD_REFTYPE_REP;
    }
} // anonymous namespace

static inline bool
husdIsShaderDisabled( const VOP_Node &vop, VOP_Type shader_type )
{
    const char *type_name = VOPgetShaderTypeName( shader_type );
    if( !UTisstring( type_name ))
	return false;

    // Construct the spare parameter name for the given shader type,
    // eg, "shop_disable_displace_shader".
    UT_WorkBuffer parm_name;
    parm_name = "shop_disable_";
    parm_name.append( type_name );
    parm_name.append( "_shader" );

    return vopIntParmVal( vop, parm_name, /*def_val=*/ false );
}

static inline bool
husdHasNodeGraphOutputSource( const UsdShadeOutput::SourceInfoVector &sources )
{
    if( sources.size() <= 0 )
	return false;
    if( sources[0].sourceType != UsdShadeAttributeType::Output )
	return false;

    return sources[0].source.GetPrim().IsA<UsdShadeNodeGraph>();
}

static std::vector <UsdAttribute>
husdGetAttribsDrivenByNodeGraphOutputs( UsdShadeNodeGraph &parent_graph )
{
    std::vector <UsdAttribute> result;

    // Check for connections directly to the outputs of the given graph.
    for( auto &&output : parent_graph.GetOutputs() )
    {
	if( husdHasNodeGraphOutputSource( output.GetConnectedSources() ))
	    result.emplace_back( output.GetAttr() );
    }

    // Look among shader children.
    for( auto &&child : parent_graph.GetPrim().GetChildren() )
    {
	UsdShadeShader child_shader( child );
	if( !child_shader )
	    continue;

	for( auto &&input : child_shader.GetInputs() )
	{
	    auto sources = input.GetConnectedSources();
	    if( husdHasNodeGraphOutputSource( input.GetConnectedSources() ))
		result.emplace_back( input );
	}
    }

    // Recurse into sub-graphs.
    for( auto &&child : parent_graph.GetPrim().GetChildren() )
    {
	UsdShadeNodeGraph child_graph( child );
	if( !child_graph )
	    continue;
	    
	auto sub_result = husdGetAttribsDrivenByNodeGraphOutputs(child_graph);
	result.insert( result.end(), sub_result.begin(), sub_result.end() );
    }

    return result;
}

static inline void
husdSetIdOnNodeGraphConnectionsIfNeeded( UsdShadeNodeGraph &parent_graph )
{
    // NOTE: This function is a workaround for Hydra bug. Remove it when fixed.
    auto attribs = husdGetAttribsDrivenByNodeGraphOutputs( parent_graph );
    if( attribs.size() <= 0 )
	return;

    // To work around the USD Hydra bug, author a piece of metadata on the
    // Shader input attribute or Material output attribute. 
    // This forces Hydra to use the new value for the input attribute 
    // of a NodeGraph wired into to the Shader or Material.
    static SYS_AtomicCounter     theMaterialIdCounter;
    VtValue			 id( theMaterialIdCounter.add(1) );
    for( auto &&attrib : attribs )
	attrib.SetCustomDataByKey( HUSDgetMaterialIdToken(), id );
}


static inline bool
husdNeedsTerminalShader( const UsdShadeNodeGraph &usd_graph )
{
    // No need to re-create an terminal output if there is one already.
    auto outputs = usd_graph.GetPrim().GetAuthoredPropertyNames( 
	    [](const TfToken &t){ 
		auto tn = SdfPath::TokenizeIdentifier(t);
		return tn.front() == "outputs" 
		    && (  tn.back() == UsdShadeTokens->surface
		       || tn.back() == UsdShadeTokens->displacement
		       || tn.back() == UsdShadeTokens->volume );
	    });

    return outputs.size() == 0;
}

static inline bool
husdNeedsUniversalShader( const UsdShadeMaterial &usd_material )
{
    if( !usd_material )
	return false;

    UsdShadeOutput surf_out = usd_material.GetSurfaceOutput( 
	    UsdShadeTokens->universalRenderContext );
    return !surf_out || !surf_out.HasConnectedSource();
}

static inline bool
husdIsCustomDataSet( UsdPrim &prim, const TfToken &key )
{
    if( !prim || !prim.HasCustomDataKey( key ))
	return false;

    VtValue val = prim.GetCustomDataByKey( key );
    if( !val.IsHolding<bool>() )
	return false;

    return val.UncheckedGet<bool>();
}

static inline void
husdSetHasPreviewShader( UsdPrim prim )
{
    prim.SetCustomDataByKey( HUSDgetHasAutoPreviewShaderToken(), VtValue(true));
}

static inline void
husdClearHasPreviewShader( UsdPrim prim )
{
    prim.ClearCustomDataByKey( HUSDgetHasAutoPreviewShaderToken() );
}

static inline bool
husdHasPreviewShader( HUSD_AutoWriteLock &lock, const UT_StringRef &prim_path ) 
{
    auto outdata = lock.data();
    if( !outdata || !outdata->isStageValid() )
	return false;

    SdfPath sdf_path( prim_path.toStdString() );
    UsdPrim prim = outdata->stage()->GetPrimAtPath( sdf_path );
    return husdIsCustomDataSet( prim, HUSDgetHasAutoPreviewShaderToken() );
}

	
static inline void
husdCreatePreviewShader( HUSD_AutoWriteLock &lock,
	UsdShadeMaterial &usd_material,
	UsdPrim usd_main_shader_prim,
	const HUSD_TimeCode &time_code,
	const UT_StringRef &usd_render_context_name )
{
    HUSD_PreviewShaderTranslator *translator = 
	HUSD_ShaderTranslatorRegistry::get().findPreviewShaderTranslator(
		usd_render_context_name );
    UT_ASSERT( translator );
    if( !translator )
	return;

    translator->createMaterialPreviewShader( lock, 
	    usd_material.GetPath().GetString(),
	    usd_main_shader_prim.GetPath().GetString(),
	    time_code);
}

static inline void
husdDestroyPreviewShader( HUSD_AutoWriteLock &lock,
	const UT_StringRef &material_path, 
	const UT_StringRef &usd_render_context_name )
{
    HUSD_PreviewShaderTranslator *translator = 
	HUSD_ShaderTranslatorRegistry::get().findPreviewShaderTranslator(
		usd_render_context_name );
    UT_ASSERT( translator );
    if( !translator )
	return;

    translator->deleteMaterialPreviewShader( lock, material_path );
}

static inline void
husdCreatePreviewShaderForMaterial( HUSD_AutoWriteLock &lock,
	UsdShadeNodeGraph &usd_mat_or_graph, const HUSD_TimeCode &time_code)
{
    UsdShadeMaterial usd_material( usd_mat_or_graph );
    if( !husdNeedsUniversalShader( usd_material ))
	return;

    UsdPrim	    usd_surface_shader_prim;
    UT_StringHolder usd_render_context_name;
    if( !husdFindSurfaceShader( &usd_surface_shader_prim, 
		&usd_render_context_name, usd_material ))
	return;

    husdCreatePreviewShader( lock, usd_material, usd_surface_shader_prim,
	    time_code, usd_render_context_name );

    husdSetHasPreviewShader( usd_surface_shader_prim );
}

static inline void
husdCreatePreviewShaderForShader( HUSD_AutoWriteLock &lock,
	UsdShadeShader &usd_shader, const HUSD_TimeCode &time_code)
{
    UsdShadeMaterial	usd_material_parent;
    UT_StringHolder	usd_render_context_name;
    if( !husdFindParentMaterialAndRenderContext( 
		&usd_material_parent, &usd_render_context_name, 
		usd_shader.GetPrim() ))
	return;

    husdCreatePreviewShader( lock, usd_material_parent, usd_shader.GetPrim(),
	    time_code, usd_render_context_name );

    husdSetHasPreviewShader( usd_shader.GetPrim() );
}

static inline void
husdDeletePreviewShaderForShader( HUSD_AutoWriteLock &lock,
	UsdShadeShader &usd_shader ) 
{
    UsdShadeMaterial	usd_material_parent;
    UT_StringHolder	usd_render_context_name;
    if( !husdFindParentMaterialAndRenderContext( 
		&usd_material_parent, &usd_render_context_name, 
		usd_shader.GetPrim() ))
	return;

    husdDestroyPreviewShader( lock, 
	    usd_material_parent.GetPath().GetString(), usd_render_context_name);

    husdClearHasPreviewShader( usd_shader.GetPrim() );
}

bool
HUSD_CreateMaterial::createMaterial( VOP_Node &mat_vop,
	const UT_StringRef &usd_mat_path, 
	bool auto_create_preview_shader ) const
{
    auto outdata = myWriteLock.data();
    if( !outdata || !outdata->isStageValid() )
	return false;

    // If node represents an existing USD primitive, no need to create it.
    if( husdRepresentsExistingPrim( mat_vop ))
	return true; 

    UT_StringHolder material_path = SdfPath( usd_mat_path.toStdString() ).
	MakeAbsolutePath( SdfPath::AbsoluteRootPath() ).GetString();

    // Create the material or graph.
    auto usd_mat_or_graph = husdCreateMainPrimForNode( mat_vop,
	    outdata->stage(), material_path, myParentType );
    auto usd_mat_or_graph_prim = usd_mat_or_graph.GetPrim();
    if( !usd_mat_or_graph_prim.IsValid() )
	return false;

    // In previous call, the shader translator may have authored a shader 
    // visualizer in a viewport override layer. We clear the layer here,
    // in case visualizer node no longer exist. We can't rely on the translator
    // clearing it, because the original terminal shader may no longer exist
    // and the translator won't be called at all.
    if( myOverrides )
	myOverrides->clear( material_path );

    bool mat_vop_is_hda = mat_vop.getOperator()->getOTLLibrary();
    bool force_children = vopIntParmVal( mat_vop, HUSD_FORCE_CHILDREN, false );
    bool has_base_prim  = husdAddBasePrim( usd_mat_or_graph_prim, mat_vop );
    bool is_mat_prim    = husdTranslatesToMaterialPrim( mat_vop );
    husdSetInstanceableIfNeeded( usd_mat_or_graph_prim, mat_vop );
    HUSDaddPrimEditorNodeId( usd_mat_or_graph_prim, mat_vop.getUniqueId());

    // Create the shaders inside the material.
    VOP_NodeList	shader_nodes;
    VOP_ShaderTypeList	shader_types;
    UT_StringArray	output_names;
    bool		ok = true; 
    bool		is_mat_vop_translated = false;
    mat_vop.findAllShaders( shader_nodes, shader_types, output_names );
    UT_ASSERT( shader_nodes.size() == shader_types.size() );
    for( int i = 0; i < shader_nodes.size(); i++ )
    {
	bool is_mat_vop = (&mat_vop == shader_nodes[i]);

	// If node translates directly to Material prim, don't create a shader.
	// If node specifies a base material prim, then it represents a derived
	// material and not a shader, so don't translate it into a shader.
	if( is_mat_vop && (is_mat_prim || has_base_prim) )
	    continue;

	// Skip children if material node is an HDA that specifies a reference 
	// primitive, because such a subnet HDA is most likely used both for 
	// authoring the referenced material prim and the derived one (here).
	// But there is an option to force the children.
	if( has_base_prim && !force_children && mat_vop_is_hda &&
		shader_nodes[i]->getParent() == &mat_vop )
	    continue;

	// If the material node has a spare parameter that turns of
	// this particular shader type, then skip it.
	if( husdIsShaderDisabled( mat_vop, shader_types[i] ))
	    continue;

	if( !husdCreateMaterialShader( myWriteLock, material_path, myTimeCode,
		    *shader_nodes[i], shader_types[i], output_names[i],
		    myDependentIDs))
	{
	    ok = false;
	}

	if( is_mat_vop )
	    is_mat_vop_translated = true;
    }

    // If the material node represents a derived material, we need to
    // translate its parameters, because that node was not translated yet.
    if( is_mat_prim || has_base_prim )
	husdCreateAndSetMaterialAttribs( usd_mat_or_graph, mat_vop );

    // Material and NodeGraph prims strictly do not need authored outputs (eg, 
    // if they are in an overriding layer), unless a spare parm forces them to.
    if( vopIntParmVal( mat_vop, HUSD_FORCE_TERMINAL, false ) &&
	husdNeedsTerminalShader( usd_mat_or_graph ))
    {
	husdCreateMaterialShader( myWriteLock, material_path, myTimeCode,
		    mat_vop, VOP_SURFACE_SHADER, "",
		    myDependentIDs);
    }

    // If the material node has not been translated as a shader (because it
    // represents the material primitive we just created), we may need
    // to do some further work, like connect input wires to a sibling graph.
    if( ok && !is_mat_vop_translated && mat_vop.translatesDirectlyToUSD() )
	ok = husdCreateMaterialInputsIfNeeded( myWriteLock, usd_mat_or_graph, 
		myTimeCode, mat_vop, myDependentIDs );

    // Generate a standard USD Preview Surface shader.
    if( auto_create_preview_shader )
	husdCreatePreviewShaderForMaterial( myWriteLock, usd_mat_or_graph,
		myTimeCode);

    // NOTE: thre is a USD Hydra bug that does not sync material when
    //	    NodeGraph input attribute value changes (it works fine for Shader
    //	    input attributes). So, to force Hydra update, we author
    //	    a piece metadata on a Shader input that connects to NodeGraph
    //	    output. This seems to work around the Hydra bug.
    //	    Remove this call when the bug is fixed.
    husdSetIdOnNodeGraphConnectionsIfNeeded( usd_mat_or_graph );

    return ok;
}

bool
HUSD_CreateMaterial::updateShaderParameters( VOP_Node &shader_vop,
        const UT_StringArray &parameter_names,
	const UT_StringRef &usd_shader_path ) const
{ 
    if( !husdUpdateShaderParameters( myWriteLock, 
		usd_shader_path, myTimeCode, shader_vop, parameter_names,
		myDependentIDs))
    {
	return false;
    }

    if( husdHasPreviewShader( myWriteLock, usd_shader_path ))
    {
	husdUpdatePreviewShaderParameters( myWriteLock, 
		usd_shader_path, myTimeCode );
    }

    return true;
}

static inline UsdShadeShader
husdGetMainShader( HUSD_AutoWriteLock &lock,
	const UT_StringRef &main_shader_path ) 
{
    auto outdata = lock.data();
    if( !outdata || !outdata->isStageValid() )
	return UsdShadeShader();

    return UsdShadeShader::Get( outdata->stage(), 
	    HUSDgetSdfPath( main_shader_path ));
}

bool
HUSD_CreateMaterial::createPreviewShader( 
	const UT_StringRef &main_shader_path ) const
{
    // TODO: accept material prim as argument too
    auto usd_shader = husdGetMainShader( myWriteLock, main_shader_path );
    if( !usd_shader )
	return false;

    husdCreatePreviewShaderForShader( myWriteLock, usd_shader, myTimeCode );
    return true;
}

bool
HUSD_CreateMaterial::deletePreviewShader( 
	const UT_StringRef &main_shader_path ) const
{
    // TODO: accept material prim as argument too
    auto usd_shader = husdGetMainShader( myWriteLock, main_shader_path );
    if( !usd_shader )
	return false;
    
    husdDeletePreviewShaderForShader( myWriteLock, usd_shader );
    return true;
}

bool
HUSD_CreateMaterial::hasPreviewShader( const UT_StringRef &main_shader_path )
{
    // TODO: accept material prim as argument too
    return husdHasPreviewShader( myWriteLock, main_shader_path );
}

static void
husdClearAutoCreateFlag( UsdShadeShader usd_shader )
{
    const TfToken &auto_created_key = HUSDgetIsAutoCreatedShaderToken();
    UsdPrim usd_prim = usd_shader.GetPrim();
    if( !husdIsCustomDataSet( usd_prim, auto_created_key ))
	return;

    usd_prim.ClearCustomDataByKey( auto_created_key );

    for( auto &&input : usd_shader.GetInputs() )
    {
	auto sources = input.GetConnectedSources();
	if( sources.size() <= 0 )
	    continue;
    
	husdClearAutoCreateFlag( 
		UsdShadeShader( husdGetConnectedShaderPrim( sources[0] )));
    }
}

bool
HUSD_CreateMaterial::clearAutoCreateFlag( 
	const UT_StringRef &preview_shader_path )
{
    auto outdata = myWriteLock.data();
    if( !outdata || !outdata->isStageValid() )
	return false;

    SdfPath sdf_path( preview_shader_path.toStdString() );
    UsdPrim preview_prim = outdata->stage()->GetPrimAtPath( sdf_path );
    if( !husdIsCustomDataSet( preview_prim, HUSDgetIsAutoCreatedShaderToken() ))
	return false;

    // Recursively traverse auto-created shaders, and clear their flag.
    // Should probably be performed by shader translator that added the
    // auto-create flags to the metadata.
    husdClearAutoCreateFlag( UsdShadeShader( preview_prim ));

    // Find a corresponding main shader and clear the flag.
    UsdPrim main_shader_prim;
    UsdShadeMaterial usd_mat = husdFindParentMaterial( preview_prim );
    if( husdFindSurfaceShader( &main_shader_prim, nullptr, usd_mat ))
	husdClearHasPreviewShader( main_shader_prim );

    return true;
}

static inline bool
husdWarningCreatingAttrib(const UT_StringHolder &name)
{
    HUSD_ErrorScope::addWarning(HUSD_ERR_FAILED_TO_CREATE_ATTRIB, name.c_str());
    return false;
}

static inline bool
husdWarningSettingAttrib(const UT_StringHolder &name)
{
    UT_WorkBuffer buffer;
    buffer.sprintf( "%s (incompatible types)", name.c_str() );

    // Like a failed binding in HUSD_Cvex reports just a warning, here  report
    // a warning too (which is essentially like cvex binding).
    HUSD_ErrorScope::addWarning(HUSD_ERR_FAILED_TO_SET_ATTRIB, buffer.buffer());
    return false;
}

template< typename UT_TYPE >
bool
husdCreateAndSetParmAttrib( const UsdTimeCode &tc, UsdPrim &prim, 
	const UT_StringHolder &name, const UT_OptionEntry *opt_value,
	const SdfValueTypeName &sdf_type )
{
    TfToken attrib_name( SdfPath::StripPrefixNamespace( name.toStdString(), 
		UsdShadeTokens->inputs ).first );

    UsdAttribute attrib( 
	    UsdShadeShader( prim ).CreateInput( attrib_name, sdf_type ));
    if( !attrib )
	return husdWarningCreatingAttrib(name);

    UT_TYPE	    ut_value;
    opt_value->importOption( ut_value );
    if( !HUSDsetAttribute( attrib, ut_value, tc ))
	return husdWarningSettingAttrib( name );

    return true;
}

template<>
bool
husdCreateAndSetParmAttrib< UT_StringArray >( 
	const UsdTimeCode &tc, UsdPrim &prim, 
	const UT_StringHolder &name, const UT_OptionEntry *opt_value,
	const SdfValueTypeName &sdf_type )
{
    TfToken attrib_name( SdfPath::StripPrefixNamespace( name.toStdString(), 
		UsdShadeTokens->inputs ).first );

    UsdAttribute attrib( 
	    UsdShadeShader( prim ).CreateInput( attrib_name, sdf_type ));
    if( !attrib )
	return husdWarningCreatingAttrib(name);

    UT_StringArray  ut_value;
    opt_value->importOption( ut_value );

    UT_Array<UT_StringHolder>	ut_cast( ut_value );
    if( HUSDsetAttribute( attrib, ut_cast, tc ))
	return husdWarningSettingAttrib( name );
    
    return true;
}

static inline bool
husdOverrideMatParm( UsdPrim &prim, 
	const UT_StringHolder &name, const UT_OptionEntry *value )
{
    UsdTimeCode		tc( UsdTimeCode::Default() );
    
    switch( value->getType() )
    {
	case UT_OPTION_INT:
	    return husdCreateAndSetParmAttrib<int64>( tc, prim, 
		    name, value, SdfValueTypeNames->Int );

	case UT_OPTION_FPREAL:
	    return husdCreateAndSetParmAttrib<double>( tc, prim, 
		    name, value,
		    SdfValueTypeNames->Double );

	case UT_OPTION_STRING:
	    return husdCreateAndSetParmAttrib<UT_StringHolder>( tc, prim, 
		    name, value, SdfValueTypeNames->String );

	case UT_OPTION_VECTOR2:
	    return husdCreateAndSetParmAttrib<UT_Vector2D>( tc, prim, 
		    name, value, SdfValueTypeNames->Double2 );

	case UT_OPTION_VECTOR3:
	    return husdCreateAndSetParmAttrib<UT_Vector3D>( tc, prim, 
		    name, value, SdfValueTypeNames->Vector3d );

	case UT_OPTION_VECTOR4:
	    return husdCreateAndSetParmAttrib<UT_Vector4D>( tc, prim, 
		    name, value, SdfValueTypeNames->Double4 );

	case UT_OPTION_MATRIX2:
	    return husdCreateAndSetParmAttrib<UT_Matrix2D>( tc, prim, 
		    name, value, SdfValueTypeNames->Matrix2d );

	case UT_OPTION_MATRIX3:
	    return husdCreateAndSetParmAttrib<UT_Matrix3D>( tc, prim, 
		    name, value, SdfValueTypeNames->Matrix3d );

	case UT_OPTION_MATRIX4:
	    return husdCreateAndSetParmAttrib<UT_Matrix4D>( tc, prim, 
		    name, value, SdfValueTypeNames->Matrix4d );

	case UT_OPTION_INTARRAY:
	    return husdCreateAndSetParmAttrib< UT_Array<int32> >( tc, prim, 
		    name, value, SdfValueTypeNames->IntArray );

	case UT_OPTION_FPREALARRAY:
	    return husdCreateAndSetParmAttrib< UT_Array<fpreal64> >( tc, prim, 
		    name, value, SdfValueTypeNames->DoubleArray );

	case UT_OPTION_STRINGARRAY:
	    return husdCreateAndSetParmAttrib< UT_StringArray >( tc, prim, 
		    name, value, SdfValueTypeNames->StringArray );

	default:
	    break;
    }

    UT_ASSERT( !"Unhandled option type" );
    HUSD_ErrorScope::addError(HUSD_ERR_STRING, "Invalid override value type.");
    return false;
}

static inline bool
husdOverrideMatParms( const UsdShadeNodeGraph &usd_mat_or_graph, 
	const UT_Options &parms )
{
    bool	ok = true;
    UsdPrim	material = usd_mat_or_graph.GetPrim();

    for( auto it = parms.begin(); it != parms.end(); ++it )
    {
	UT_String   shader_name, parm_name;
	UT_String   shader_and_parm( it.name() );
	const UT_OptionEntry *value = it.entry();

	shader_and_parm.splitPath( shader_name, parm_name );
	if( shader_name.isstring() )
	{
	    UT_String	mat_path( material.GetPath().GetString() );
	    auto	stage( material.GetStage() );
	    UT_String	shader_path;

	    shader_path.sprintf("%s/%s", mat_path.c_str(), shader_name.c_str());
	    auto shader = stage->OverridePrim( HUSDgetSdfPath( shader_path ));
	    if( !husdOverrideMatParm( shader, parm_name, value ))
		ok = false;
	}
	else
	{
	    if( !husdOverrideMatParm( material, parm_name, value ))
		ok = false;
	}
    }

    return ok;
}

bool
HUSD_CreateMaterial::createDerivedMaterial( 
	const UT_StringRef &base_material_path,
	const UT_Options &material_parameters, 
	const UT_StringRef &usd_mat_path) const
{
    auto outdata = myWriteLock.data();
    if( !outdata || !outdata->isStageValid() )
    {
	HUSD_ErrorScope::addError( HUSD_ERR_STRING, "Invalid stage." );
	return false;
    }

    bool is_material = true;

    auto stage = outdata->stage();
    auto usd_mat_or_graph = husdCreateMainPrim( stage, usd_mat_path, 
	    myParentType, is_material);
    auto usd_mat_or_graph_prim = usd_mat_or_graph.GetPrim();
    if( !usd_mat_or_graph_prim.IsValid() )
	return false;

    // TODO: make it a choice between inheriting and specializing.
    husdAddBasePrim( usd_mat_or_graph_prim, 
	    HUSD_PrimRefType::SPECIALIZE, base_material_path );
    return husdOverrideMatParms( usd_mat_or_graph, material_parameters );
}


void
HUSD_CreateMaterial::addDependent( OP_Node *node )
{
    myDependentIDs.append( node->getUniqueId() );
}
