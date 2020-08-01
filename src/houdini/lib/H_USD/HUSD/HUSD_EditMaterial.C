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

#include "HUSD_EditMaterial.h"

#include "HUSD_PrimHandle.h"
#include "HUSD_PropertyHandle.h"
#include "XUSD_AttributeUtils.h"
#include "XUSD_Data.h"

#include <VOP/VOP_GenericShader.h>
#include <PI/PI_EditScriptedParms.h>
#include <PI/PI_SpareProperty.h>
#include <OP/OP_Layout.h>
#include <PRM/PRM_SpareData.h>
#include <VEX/VEX_VexResolver.h>
#include <UT/UT_OpUtils.h>
#include <pxr/usd/usdShade/material.h>

using namespace UT::Literal;

static const auto HUSD_USD_PRIMVAR_READER_OPNAME    = "usdprimvarreader"_sh;
static const auto HUSD_USD_PRIMVAR_READER_SHADER_ID = "UsdPrimvarReader"_sh;
static const auto HUSD_USD_PRIMVAR_READER_PREFIX   = "UsdPrimvarReader_"_sh;

static const auto HUSD_SHADER_PRIMNAME = "shader_shaderprimname"_sh;
static const auto HUSD_IS_SHADER_PARM  = "sidefx::shader_isparm"_sh;


PXR_NAMESPACE_USING_DIRECTIVE


HUSD_EditMaterial::HUSD_EditMaterial( HUSD_AutoAnyLock &lock )
    : myAnyLock(lock)
{
}

static inline UT_StringHolder
husdGetUSDShaderID( const UsdShadeShader &usd_shader )
{
    UsdPrim prim = usd_shader.GetPrim();

    TfToken default_asset( "info:sourceAsset" );
    if( prim.HasAttribute( default_asset ))
    {
	SdfAssetPath  val;
	
	prim.GetAttribute( default_asset ).Get( &val );
	return UT_StringHolder( val.GetAssetPath() );
    }

    TfToken default_id( "info:id" );
    if( prim.HasAttribute( default_id ))
    {
	TfToken	    val;
	
	prim.GetAttribute( default_id ).Get( &val );
	return UT_StringHolder( val.GetString() );
    }

    return UT_StringHolder();
}

static inline UT_StringHolder
husdGetOpTypeName( const UT_StringRef &shader_id )
{
    // SOHO encodes shader references as "opdef:/Vop/foo::2.0" so that Karma
    // can find VEX code. Karma tries to look for such HDA. However, the 
    // same code path is used for "import foo__2_0". So there is an alias
    // resolution mechanism to map "foo__2_0" to "foo::2.0". We take advantage
    // of it here.
    // Basically the opname in the "opdef:/Vop/opname" can be an
    // arbitrary shader function name (eg, "foo"), and if some HDA (eg, "bar")
    // declares that its shader name is "foo", then "opdef:/Vop/foo" will
    // resolve to "opdef:/Vop/bar". Or, in our case "opdef:/Vop/PxrDisney" ->
    // "opdef:/Vop/pxrdisney".
    UT_String alias;
    if( shader_id.startsWith( UT_HDA_DEFINITION_PREFIX ))
    {
	alias = shader_id.c_str();
    }
    else
    {
	UT_String name;

	UT_OpUtils::combineTableAndOpName( name, VOP_TABLE_NAME, shader_id );
	UT_OpUtils::combineOpIndexFileSectionPath( alias,
		UT_HDA_DEFINITION_PREFIX, name, nullptr );
    }

    UT_String op_type;
    if( !VEX_VexResolver::convertAlias( alias, op_type ))
	op_type = alias;

    // Strip the prefix and the section from the shader specification,
    // which should just give us the operator type.
    if( op_type.startsWith( UT_HDA_DEFINITION_PREFIX ))
	UT_OpUtils::splitOpIndexFileSectionPath(
		op_type, nullptr, &op_type, nullptr );

    UT_WorkBuffer table_name;
    UT_WorkBuffer op_name;
    UT_OpUtils::splitTableAndOpName( op_type, table_name, op_name );
    return UT_StringHolder( op_name );
}

static inline bool
husdParmIsActive( VOP_Node &vop, PRM_Parm &parm )
{
    auto name = OP_Parameters::getParmActivationToggleName( parm.getToken() );

    PRM_Parm *activation_parm = vop.getParmPtr( name );
    if( !activation_parm )
	return true; // without activation checkbox, the parm is active

    int val;
    activation_parm->getValue( 0.0f, val, 0, SYSgetSTID() );
    return val;
}

static inline void
husdSetShaderNodeParms( VOP_Node &vop, const UsdShadeShader &usd_shader,
	bool update_only )
{
    auto attribs = usd_shader.GetPrim().GetAuthoredAttributes();
    for( auto &&attrib : attribs )
    {
	if( !attrib.HasValue() )
	    continue; // Can't set parm if the attrib has no value.

	// Name may contain "inputs:" namespace, so use base name instead.
	UT_StringHolder name( attrib.GetBaseName().GetString() );
	PRM_Parm *parm = vop.getParmPtr( name );
	if( !parm )
	    continue; // Can't set parm if we can't find it.

	// In update mode, we set new values only on parameters that
	// have not been activated for edit. Otherwise users loose edits.
	if( update_only && husdParmIsActive( vop, *parm ))
	    continue;

	HUSDsetNodeParm( *parm, attrib, UsdTimeCode::Default() );
    }
}

static inline UT_StringHolder 
husdGetEffectiveShaderPrimName( const UsdShadeShader &usd_shader )
{
    UT_String name( usd_shader.GetPrim().GetName().GetString() );

    // Karma materials add suffix to the prim name, so need to strip it off.
    // Otherwise names won't match and we'll add a new prim instead of override.
    // TODO: figure a way to decide whether there is any suffix
    //		  and what exactly the suffix is. 
    name.replaceSuffix("_surface", "");
    name.replaceSuffix("_displace", "");

    return UT_StringHolder( name );
}

static inline UT_StringHolder 
husdGetShaderRootPath( const UsdShadeShader &usd_shader )
{
    UT_String name( usd_shader.GetPath().GetString() );

    // See comment in husdGetEffectiveShaderPrimName() above.
    name.replaceSuffix("_surface", "");
    name.replaceSuffix("_displace", "");

    return UT_StringHolder( name );
}

static inline UsdShadeMaterial
husdFindMaterialParentPrim( const UsdShadeShader &usd_shader )
{
    UsdPrim prim = usd_shader.GetPrim();
    while( prim )
    {
	UsdShadeMaterial usd_material(prim);
	if( usd_material )
	    return usd_material;

	prim = prim.GetParent();
    }
    
    return UsdShadeMaterial();
}

static inline void
husdSetShaderTypeFromString( PI_EditScriptedParm *parm, 
	const UT_StringRef& type_name )
{
    if( type_name == "surface"_sh )
	parm->setSpareValue( PRM_SPARE_CONNECTOR_TYPE,
		VOPgetShaderTypeName( VOP_SURFACE_SHADER ));
    else if( type_name == "displacement"_sh )
	parm->setSpareValue( PRM_SPARE_CONNECTOR_TYPE,
		VOPgetShaderTypeName( VOP_DISPLACEMENT_SHADER ));
    else if( type_name == "volume"_sh )
	parm->setSpareValue( PRM_SPARE_CONNECTOR_TYPE,
		VOPgetShaderTypeName( VOP_ATMOSPHERE_SHADER ));
}

static inline void
husdSetShaderTypeIfNeeded( PI_EditScriptedParm *parm, 
	const UsdShadeShader &usd_shader )
{
    // We need to figure out the shader type (eg, surface), which will be used 
    // as node output connector type. This info comes from the material itself,
    // whose output links to the shader output. That material output has 
    // the info about the shader type. So, we get material and find output.
    // Note: we could pass the material as parameter, but finding it is ok too.
    auto mat_parent = husdFindMaterialParentPrim( usd_shader ); 
    if( !mat_parent )
	return;
    
    auto mat_outputs = mat_parent.GetOutputs();
    for( auto &&mat_output : mat_outputs )
    {
	TfToken			src_name;
	UsdShadeAttributeType  	src_type; 

	auto mat_out_name = mat_output.GetBaseName();
	auto shader = mat_parent.ComputeOutputSource( mat_out_name, 
		&src_name, &src_type );

	if( shader.GetPrim() == usd_shader.GetPrim() &&
	    parm->myName == src_name.GetText() )
	{
	    husdSetShaderTypeFromString( parm, mat_out_name.GetText());
	    break;
	}
    }
}

static inline OP_Node *
husdCreateDefaultShaderNode( const HUSD_DataHandle &data_handle,
	OP_Network &net, const UsdShadeShader &usd_shader )
{
    // Attrib names without any colons.
    constexpr auto VOP_GENERIC_SHADER_OPNAME = "genericshader";
    OP_Node *node = net.createNode( VOP_GENERIC_SHADER_OPNAME );
    if( !node || !node->runCreateScript() )
	return nullptr;

    HUSD_PrimHandle prim_handle(data_handle, usd_shader.GetPath());

    PI_EditScriptedParms eparms( node, /*add_reserved_parms=*/ true,
				/*links=*/ false );

    // Make the shader ID parameter invisible, since it's not reall editable.
    PI_EditScriptedParm *shader_id = eparms.getParmWithName("sidefx_name"_sh);
    if( shader_id )
	shader_id->myInvisible = true;

    // Create node parameters for the input and output attributes.
    auto attribs = usd_shader.GetPrim().GetAttributes();
    for( auto &&attrib : attribs )
    {
	UT_StringHolder attr_name( attrib.GetName().GetString() );
	UT_StringHolder base_name( attrib.GetBaseName().GetString() );
	UT_StringHolder attr_namespace( attrib.GetNamespace().GetString() );
	
	bool is_input  = (attr_namespace == "inputs"_sh);
	bool is_output = (attr_namespace == "outputs"_sh);
	if( !is_input && !is_output )
	    continue;

	UT_Array<PI_EditScriptedParm *> parms;
	HUSD_PropertyHandle attr_handle( prim_handle, attr_name );
	attr_handle.createScriptedParms( parms, base_name, false, false );

	for( auto &&parm : parms )
	{
	    if( is_input && is_output )
		parm->setSpareValue( PRM_SPARE_CONNECTOR_KIND, 
			PRM_SpareData::connectorKindInOut.getValue(
			    PRM_SPARE_CONNECTOR_KIND) );
	    else if( is_input )
		parm->setSpareValue( PRM_SPARE_CONNECTOR_KIND, 
			PRM_SpareData::connectorKindIn.getValue(
			    PRM_SPARE_CONNECTOR_KIND) );
	    else if( is_output )
	    {
		parm->setSpareValue( PRM_SPARE_CONNECTOR_KIND, 
			PRM_SpareData::connectorKindOut.getValue(
			    PRM_SPARE_CONNECTOR_KIND) );
		parm->myInvisible = true;
		husdSetShaderTypeIfNeeded( parm, usd_shader );
	    }

	    eparms.addParm( parm );
	}
    }

    UT_String	errors;
    OPgetDirector()->changeNodeSpareParms( node, eparms, errors );
    UT_ASSERT( !errors.isstring() );

    // Special case the default vop, on which we set the shader name too.
    VOP_GenericShader *shader_vop = dynamic_cast<VOP_GenericShader*>(node); 
    if( shader_vop )
    {
	UT_StringHolder name = husdGetUSDShaderID( usd_shader );
	if( name.isstring() )
	    shader_vop->setSHADERNAME( name );
    }

    return node;
}

static inline void
husdSetNodeName( VOP_Node *vop, OP_Network &net, 
	const UsdShadeShader &usd_shader )
{
    auto name = husdGetEffectiveShaderPrimName( usd_shader );
    net.renameNode( vop, name );
}

static inline void
husdUnjoinIfNeeded( PI_EditScriptedParms &eparms, int index )
{
    // We want to un-join parameters that follow an unlabeled toggle.
    // Otherwise, having that toggle being preceeded by another unlabeled
    // toggle and other parms looks bad.
    PI_EditScriptedParm *parm = eparms.getParm( index );
    if(parm->getType() != "Toggle" || !parm->myJoinNextFlag || parm->myUseLabel)
	return;

    for( int i = index; ; i++ )
    {
	parm = eparms.getParm(i);
	parm->myUseLabel = true;

	// Continue until join-chain is done.
	if( !parm->myJoinNextFlag || parm->getIsGroupParm())
	    break;

	parm->myJoinNextFlag = false;
    }
}

static inline void
husdInsertActivationToggles( PI_EditScriptedParms &eparms )
{
    static PRM_Name	theActivateName("_sfx_activate_");
    static PRM_Default	theActivateDefault;
    static PRM_Template	theActivateParm( PRM_TOGGLE, PRM_TYPE_TOGGLE_JOIN,
				&theActivateName, &theActivateDefault );


    for( int i = 0, n = eparms.getNParms(); i < n; i++ )
    {
	PI_EditScriptedParm *parm = eparms.getParm(i);
	if( parm->getIsGroupParm() )
	    continue;

	// Create a checkbox spare parm for the main parm.
	PI_EditScriptedParm *chbox = new PI_EditScriptedParm(
		theActivateParm, nullptr, false);

	auto name = OP_Parameters::getParmActivationToggleName( parm->myName );
	chbox->myName		= name;
	chbox->myLabel		= "";
	chbox->myUseLabel	= false;
	chbox->myJoinNextFlag	= true;

	// Hide/disable the checkbox whenever the main parm is hidden/disabled.
	chbox->myConditional[ PRM_CONDTYPE_DISABLE ] = 
	    parm->myConditional[ PRM_CONDTYPE_DISABLE ];
	chbox->myConditional[ PRM_CONDTYPE_HIDE ] = 
	    parm->myConditional[ PRM_CONDTYPE_HIDE ];
	chbox->myInvisible = parm->myInvisible;

	// The checkbox should not be used as a shader parameter itself.
	chbox->setSpareValue( HUSD_IS_SHADER_PARM, "0" );

	// If the current parameter is also a toggle with no label and
	// joined with next, un-join it. Otherwise the parameter row looks bad.
	husdUnjoinIfNeeded( eparms, i );

	// Add the new chekbox parm to the list, and move it just before the
	// main parameter that it controls.
	eparms.addParm( chbox );	    // appends at index 'n'
	eparms.moveParms( n, n, i - n );    // moves just before 'parm'

	i ++;	// next iteration needs to skip over the created & moved parm
	n ++;   // new number of the parms; new parms are appended at this idx
    }

}

static inline void
husdAddShaderNameProperty( PI_EditScriptedParms &eparms )
{
    static PRM_Name	theShaderNameName( HUSD_SHADER_PRIMNAME );
    static PRM_Template	theShaderNameParm( PRM_STRING, 1, &theShaderNameName );

    PI_EditScriptedParm *prop = new PI_EditScriptedParm(
	    theShaderNameParm, nullptr, false);

    prop->myLabel	= "Shader Primitive Name";
    prop->myInvisible	= true;
    prop->setSpareValue( HUSD_IS_SHADER_PARM, "0" );
	
    eparms.addParm( prop );
}

static inline void
husdAddMatEditSpareParameters( OP_Node *node )
{
    PI_EditScriptedParms eparms( node, /*add_reserved_parms=*/ true,
				/*links=*/ false );

    // Insert activation toggles that mark which parameters should be edited.
    husdInsertActivationToggles( eparms );

    // Record the USD shader name, in case the node name is already taken.
    husdAddShaderNameProperty( eparms );

    UT_String	errors;
    OPgetDirector()->changeNodeSpareParms( node, eparms, errors );
    UT_ASSERT( !errors.isstring() );
}

static inline UT_StringHolder
husdGetShaderPrimName( OP_Node *node )
{
    UT_StringHolder shader_prim_name;

    if( node && node->hasParm( HUSD_SHADER_PRIMNAME ))
	node->evalString( shader_prim_name, HUSD_SHADER_PRIMNAME, 0, 0 );

    return shader_prim_name;
}

static inline void
husdSetMatEditSpareParameters( OP_Node *node, const UsdShadeShader &usd_shader )
{
    PRM_Parm *	prop  = node->getParmPtr( HUSD_SHADER_PRIMNAME );
    auto	value = husdGetEffectiveShaderPrimName( usd_shader );

    UT_ASSERT( prop );
    if( prop )
	prop->setValue( 0, value, CH_STRING_LITERAL );
}

static inline OP_Node *
husdCreateUsdPrimvarReaderNode( OP_Network &net, 
	const UsdShadeShader &usd_shader, const UT_StringRef &shader_id)
{
    OP_Node *node = net.createNode( HUSD_USD_PRIMVAR_READER_OPNAME );
    if( node && !node->runCreateScript() )
	node = nullptr;

    VOP_Node *vop = CAST_VOPNODE( node );
    if( !vop )
	return nullptr;

    // Set the signature, based on the shader id's suffix.
    UT_StringHolder suffix( shader_id.buffer() +
	    HUSD_USD_PRIMVAR_READER_PREFIX.length() );
    if( suffix == "float"_sh )
	suffix = "default"_sh;
    vop->setCurrentSignature( suffix );

    // Also set the fallback value parameter, whose name does not match attrib.
    UT_StringHolder fallback_parm_name( "fallback" );
    if( suffix != "default"_sh )
    {
	fallback_parm_name += "_";
	fallback_parm_name += suffix;
    }

    PRM_Parm *parm = vop->getParmPtr( fallback_parm_name );
    auto attrib = usd_shader.GetPrim().GetAttribute(TfToken("inputs:fallback"));
    if( parm && attrib)
	HUSDsetNodeParm( *parm, attrib, UsdTimeCode::Default() );

    return vop;
}

static inline VOP_Node *
husdCreateVopNode( const HUSD_DataHandle &handle,
	OP_Network &net, const UsdShadeShader &usd_shader )
{
    // Validate the usd pirm.
    if( !usd_shader )
	return nullptr;

    // Create  a vop shader node for editing based on USD shader's ID.
    OP_Node	    *node = nullptr;
    UT_StringHolder  shader_id = husdGetUSDShaderID( usd_shader );
    if( shader_id.startsWith( HUSD_USD_PRIMVAR_READER_SHADER_ID ))
    {
	// Special case for USD Primvar Reader, which has unusual ID and parms.
	node = husdCreateUsdPrimvarReaderNode( net, usd_shader, shader_id );
    }
    else if( shader_id.isstring() )
    {
	node = net.createNode( husdGetOpTypeName( shader_id ));
	if( node && !node->runCreateScript() )
	    node = nullptr;
    }
			    
    // If explicit node type could not be found, use the Generic Shader VOP.
    if( !node )
    	node = husdCreateDefaultShaderNode( handle, net, usd_shader );

    // Create the activation toggle parameter for each editable shader parm.
    if( node )
    {
	husdAddMatEditSpareParameters( node );
	husdSetMatEditSpareParameters( node, usd_shader );
    }

    return CAST_VOPNODE( node );
}

static inline void
husdAddShaderToMap( UT_StringMap<VOP_Node *> &input_vops, VOP_Node *vop )
{
    auto shader_prim_name = husdGetShaderPrimName( vop );

    if( shader_prim_name.isstring() )
	input_vops[ shader_prim_name ] = vop;
}

static inline UT_StringMap<VOP_Node *> 
husdGetInputShaderMap( VOP_Node *vop )
{
    UT_StringMap<VOP_Node *> input_vops;

    if( !vop )
	return input_vops; // empty map

    for( int i = 0, n = vop->getInputsArraySize(); i < n; i++ )
	husdAddShaderToMap( input_vops, CAST_VOPNODE( vop->getInput(i) ));

    return input_vops;
}

static inline VOP_Node *
husdFindVopNode( const UT_StringMap<VOP_Node *> &map, const UT_StringRef &key )
{
    auto it = map.find( key );
    if( it != map.end() )
	return it->second;

    return nullptr;
}

static inline VOP_Node *
husdCreateShaderNode( const HUSD_DataHandle &handle, 
	OP_Network &net, const UsdShadeShader &usd_shader,
	const UT_StringMap<VOP_Node *> &old_vops,
	UT_StringMap<VOP_Node *> &processed_vops)
{
    // If already encountered that exact shader, return the node.
    UT_StringHolder key( usd_shader.GetPath().GetString() );
    VOP_Node *vop = husdFindVopNode( processed_vops, key );
    if( vop )
	return vop;

    // Look for an existing vop that needs updating.
    vop = husdFindVopNode( old_vops, 
	    husdGetEffectiveShaderPrimName( usd_shader ));
    bool found_old_vop = (vop != nullptr);

    // It's possible that the usd_shader is part of a material node.
    // In such cases, it has a special suffix in the name.
    UT_StringHolder root_key( husdGetShaderRootPath( usd_shader ));
    if( root_key != key )
    {
	vop = husdFindVopNode( processed_vops, root_key );
	if( vop )
	{
	    // This USD shader may need to set some other parameters than 
	    // the previous USD shader that created this node.
	    husdSetShaderNodeParms( *vop, usd_shader, found_old_vop );
	    return vop;
	}
    }
    
    // Create new VOP node if there was no old one to update.
    if( !vop )
	vop = husdCreateVopNode( handle, net, usd_shader );

    // If VOP node could not be found or created, we can't proceed any further.
    if( !vop )
	return nullptr;

    // Do basic confiuration of the vop.
    if( !found_old_vop )
    {
	husdSetNodeName( vop, net, usd_shader );
	vop->setMaterialFlag( false );
    }

    // Set the node's parameter values based on primitive's attributes
    husdSetShaderNodeParms( *vop, usd_shader, found_old_vop );

    // Update the map for both original path and common mat path.
    processed_vops[ key ] = vop;
    if( root_key != key )
	processed_vops[ root_key ] = vop;

    return vop;

}

static VOP_Node*
husdCreateShaderNodeChain( const HUSD_DataHandle &handle,
	OP_Network &net, const UsdShadeShader &usd_shader,
	const UT_StringMap<VOP_Node *> &old_vops,
	UT_StringMap<VOP_Node *> &processed_vops )
{
    // Create and configure the shader vop node.
    VOP_Node *vop = husdCreateShaderNode( handle, 
	    net, usd_shader, old_vops, processed_vops );
    if( !vop )
	return nullptr;

    // When recursing, we need to pass the map of own input nodes.
    UT_StringMap<VOP_Node *> old_inputs = husdGetInputShaderMap( vop );

    // Follow the USD input connections and recursively create nodes (if needed)
    // and wire the connections between nodes.
    std::vector<UsdShadeInput> usd_inputs(
	    usd_shader.ConnectableAPI().GetInputs() );
    for( auto &&input: usd_inputs )
    {
	UsdShadeConnectableAPI	connectable;
	TfToken			out_name;
	UsdShadeAttributeType	out_type;

	if( !input.GetConnectedSource( &connectable, &out_name, &out_type ))
	    continue;

	// Recursively create a VOP node.
	UT_ASSERT( out_type == UsdShadeAttributeType::Output );
	UsdShadeShader input_shader( connectable.GetPrim() );
	VOP_Node *in_vop = husdCreateShaderNodeChain( handle,
		net, input_shader, old_inputs, processed_vops );
	if( !in_vop )
	    continue;

	// Wire the connections between the VOP nodes.
	int in_idx  = vop->getInputFromName( UT_String( input.GetBaseName() ));
	int out_idx = in_vop->getOutputFromName( UT_String( out_name ));
	if( in_idx >= 0 && out_idx >= 0 )
	    vop->setInput( in_idx, in_vop, out_idx );
    }

    return vop;
}

static inline int
husdGetOutputIdxFromType( VOP_Node *vop, const UT_StringRef &mat_out_name )
{
    // Figure out the VOP type of the USD material output.
    TfToken  mat_out_name_tk( mat_out_name.toStdString() );
    TfToken  mat_out_type_name( SdfPath::StripNamespace( mat_out_name_tk ));
    VOP_Type mat_out_type = VOP_TYPE_UNDEF;
    if( mat_out_type_name == "surface" )
	mat_out_type = VOP_SURFACE_SHADER;
    else if( mat_out_type_name == "displacement" )
	mat_out_type = VOP_DISPLACEMENT_SHADER;
    else if( mat_out_type_name == "volume" )
	mat_out_type = VOP_ATMOSPHERE_SHADER;

    // Match the USD material output type to the VOP node output type.
    for (int i = 0, n = vop->getNumVisibleOutputs(); i < n; ++i)
    {
	VOP_Type vop_out_type = vop->getOutputType(i);
	if( vop_out_type == mat_out_type )
	    return i;

	// Special case for BSDF output type, which is surface shader.
	if( mat_out_type == VOP_SURFACE_SHADER &&
	    vop_out_type == VOP_BSDF_SHADER )
	    return i;
    }

    return -1;
}

static inline void
husdCollectShaderNode( VOP_Node *shader_vop, const UT_StringRef &out_name, 
	VOP_Node *collect_vop, const UT_StringRef &mat_out_name )
{
    int out_idx = shader_vop->getOutputFromName( out_name.c_str() );
    if( out_idx < 0 )
	out_idx = husdGetOutputIdxFromType( shader_vop, mat_out_name );

    int in_idx  = collect_vop->nInputs();
    if( in_idx >= 0 && out_idx >= 0 )
	collect_vop->setInput( in_idx, shader_vop, out_idx );
}

static inline void
husdLayoutAllChildren( OP_Network &parent ) 
{
    OP_Layout	layout(&parent );

    for( int i = 0; i < parent.getNchildren(); i++)
	layout.addLayoutItem( parent.getChild(i) );
    layout.layoutOps( OP_LAYOUT_RIGHT_TO_LEFT, parent.getCurrentNodePtr() );
}

static inline bool
husdNeedsCollectVop( const VOP_NodeList &shader_vops )
{
    // If there are more two or more shader nodes, we need a Collect VOP.
    // A single shader node can represent the USD material, but not if it is
    // a material (which may have a few shader outputs, and only one may
    // actually have been used to create the USD material/shader primitive).
    UT_ASSERT( shader_vops.size() > 0 );
    return shader_vops.size() > 1 
	|| shader_vops[0]->getShaderType() == VOP_VOP_MATERIAL_SHADER;
}

static inline VOP_Node *
husdGetMaterialVop( OP_Network &parent_node, VOP_Node *material_vop,
	const VOP_NodeList &shader_vops,
	const UT_StringArray &shader_vops_output_names,
	const UT_StringArray &mat_output_names)
{
    VOP_Node *result = nullptr;

    if( material_vop )
    {
	// TODO: XXX: if it is a collect vop, may need to add new inputs
	result = material_vop;
    }
    else if( shader_vops.size() <= 0 )
    {
	// Can't find material node without any created nodes.
	result = nullptr;
    }
    else if( husdNeedsCollectVop( shader_vops ))
    {
	// We need a Collect VOP, so create it and wire shader nodes.
	result = CAST_VOPNODE( parent_node.createNode(VOP_COLLECT_NODE_NAME) );
	for( int i = 0, n = shader_vops.size(); i < n; i++ )
	{
	    husdCollectShaderNode( shader_vops[i], shader_vops_output_names[i], 
		    result, mat_output_names[i] );
	}
    }
    else
    {
	// Created a single shader node, so use it as material representation.
	result = shader_vops[0];
	result->setMaterialFlag( true );
    }

    return result;
}

static inline OP_Node *
husdLoadOrUpdateMaterial( const HUSD_DataHandle &handle, 
	OP_Network &parent_node,
	const UsdShadeMaterial &usd_material ,
	const UT_StringRef &material_node_name) 
{
    // Keeps track of all VOPs that make up the material setup. Allows reuse.
    // Map: usd prim path -> corresponding (created or updated) shader vop node 
    UT_StringMap<VOP_Node *> processed_vops;

    // Keep track of the main shader VOPs.
    VOP_NodeList    shader_vops;
    UT_StringArray  shader_vops_output_names;
    UT_StringArray  mat_output_names;

    // See if we need to update an existing material.
    // Map: usd prim name -> already existing shader vop node 
    VOP_Node *material_vop = parent_node.findVOPNode( material_node_name );
    UT_StringMap<VOP_Node *> old_vops = husdGetInputShaderMap( material_vop );

    // The material vop itself may be a shader (if material has just 1 shader).
    husdAddShaderToMap( old_vops, material_vop );

    // Create shader node for each output of the USD material.
    auto outputs = usd_material.GetOutputs();
    for( auto &&output : outputs )
    {
	UsdShadeConnectableAPI	 shader_out;
	TfToken			 src_name;
	UsdShadeAttributeType	 src_type;

	if( !output.GetConnectedSource( &shader_out, &src_name, &src_type ))
	    continue;
	
	UT_ASSERT( shader_out.IsShader() );
	if( !shader_out.IsShader() )
	    continue;

	UsdShadeShader usd_shader( shader_out );
	VOP_Node *shader_vop = husdCreateShaderNodeChain( handle, 
		parent_node, usd_shader, old_vops, processed_vops );
	if( !shader_vop )
	    continue;

	shader_vops.append( shader_vop );
	shader_vops_output_names.append( src_name.GetString() );
	mat_output_names.append( output.GetBaseName().GetString() );
    }

    return husdGetMaterialVop( parent_node, material_vop,
	    shader_vops, shader_vops_output_names, mat_output_names );
}

static inline UT_StringHolder
husdLoadOrUpdateMaterialNode( HUSD_AutoAnyLock &any_lock,
	OP_Network &parent_node,
	const UT_StringRef &material_prim_path,
	const UT_StringRef &material_node_name) 
{
    UT_StringHolder	node_name;

    auto outdata = any_lock.constData();
    if( !outdata || !outdata->isStageValid() )
	return node_name;

    auto		stage( outdata->stage() );
    SdfPath		path( material_prim_path.toStdString() );
    UsdShadeMaterial	usd_material( stage->GetPrimAtPath( path ));
    if( !usd_material )
	return node_name;

    OP_Node *mat_node = husdLoadOrUpdateMaterial( any_lock.dataHandle(),
	    parent_node, usd_material, material_node_name );
    if( !mat_node )
	return node_name;

    // TODO: layout only newly created nodes
    husdLayoutAllChildren( parent_node );
    node_name = mat_node->getName();

    return node_name;
}

UT_StringHolder
HUSD_EditMaterial::loadMaterial( OP_Network &parent_node,
	const UT_StringRef &material_prim_path) const
{
    return husdLoadOrUpdateMaterialNode( myAnyLock, parent_node, 
	    material_prim_path, UT_StringRef() );
}

UT_StringHolder
HUSD_EditMaterial::updateMaterial(OP_Network &parent_node,
	const UT_StringRef &material_prim_path,
	const UT_StringRef &material_node_name) const
{
    return husdLoadOrUpdateMaterialNode( myAnyLock, parent_node, 
	    material_prim_path, material_node_name );
}

