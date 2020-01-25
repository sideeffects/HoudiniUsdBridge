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

#include "XUSD_AttributeUtils.h"
#include "HUSD_ShaderTranslator.h"
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


PXR_NAMESPACE_USING_DIRECTIVE


HUSD_CreateMaterial::HUSD_CreateMaterial(HUSD_AutoWriteLock &lock)
    : myWriteLock(lock)
{
}


static void 
husdCreateAncestors( const UsdStageRefPtr &stage, 
	const SdfPath &parent_path, const TfToken &type_name )
{
    if( parent_path.IsEmpty() )
	return;

    UsdPrim parent = stage->GetPrimAtPath( parent_path );
    if( parent && parent.IsDefined() )
	return;

    husdCreateAncestors( stage, parent_path.GetParentPath(), type_name );
    stage->DefinePrim( parent_path, type_name );
}

static inline UsdShadeNodeGraph
husdCreateMainPrim( const UsdStageRefPtr &stage, const UT_StringRef &usd_path,
	const UT_StringRef &parent_usd_prim_type, bool is_material )
{
    static SYS_AtomicCounter     theMaterialIdCounter;

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

    // Add a unique id to this material/nodegraph so that nodes downstream 
    // can know if its definition has changed.
    main_prim.GetPrim().SetCustomDataByKey(HUSDgetMaterialIdToken(),
            VtValue(theMaterialIdCounter.add(1)));

    return main_prim;
}

static inline bool
husdCreateMaterialShader(HUSD_AutoWriteLock &lock,
	const UT_StringRef &usd_material_path, const HUSD_TimeCode &tc,
	VOP_Node &shader_node, VOP_Type shader_type, 
	const UT_StringRef &output_name )
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

    // TODO: should enoder return a bool? In general, how are errors reported?
    translator->createMaterialShader(lock, usd_material_path, tc,
	    shader_node, shader_type, output_name);
    return true;
}

static inline UT_StringHolder
husdCreateShader(HUSD_AutoWriteLock &lock,
	const UT_StringRef &usd_material_path, const HUSD_TimeCode &tc,
	VOP_Node &shader_node, const UT_StringRef &output_name )
{
    // Find a translator for the given render target.
    HUSD_ShaderTranslator *translator = 
	HUSD_ShaderTranslatorRegistry::get().findShaderTranslator(shader_node);
    UT_ASSERT( translator );
    if( !translator )
	return UT_StringHolder();

    return translator->createShader(lock, usd_material_path, usd_material_path,
	    tc, shader_node, output_name);
}

static inline void
husdCreatePreviewShader( HUSD_AutoWriteLock &lock,
	const UT_StringRef &usd_material_path, const HUSD_TimeCode &tc,
	VOP_Node &shader_node, const UT_StringRef &output_name )
{
    HUSD_PreviewShaderGenerator *generator = 
	HUSD_ShaderTranslatorRegistry::get().findPreviewShaderGenerator(
		shader_node );

    UT_ASSERT( generator );
    if( !generator )
	return;

    generator->createMaterialPreviewShader(lock, usd_material_path, tc,
	    shader_node, output_name);
}

static inline void
husdCreateAndSetMaterialAttribs( UsdShadeNodeGraph &usd_graph, VOP_Node &vop )
{
    UsdTimeCode usd_tc( UsdTimeCode::Default() );

    auto parms = vop.getUSDShaderParms();
    for( const PRM_Parm *parm: parms )
    {
	auto sdf_type = HUSDgetShaderAttribSdfTypeName( *parm );
	if( !sdf_type )
	    continue;

	UsdAttribute attrib( usd_graph.CreateInput( 
		    TfToken(parm->getToken()), sdf_type ));
	HUSDsetAttribute( attrib, *parm, usd_tc );
    }
}

static inline bool
husdCreateMaterialInputsIfNeeded( HUSD_AutoWriteLock &lock,
	UsdShadeNodeGraph &usd_graph,
	const HUSD_TimeCode &time_code, 
	VOP_Node &mat_vop )
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
		usd_mat_path, time_code, *input_vop, output_name );
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
	UT_StringHolder prim_path;
	if( vop.hasParm( HUSD_SHADER_BASEPRIM ))
	    vop.evalString( prim_path, HUSD_SHADER_BASEPRIM, 0, 0 );

	UT_StringHolder asset_path;
	if( vop.hasParm( HUSD_SHADER_BASEASSET ))
	    vop.evalString( asset_path, HUSD_SHADER_BASEASSET, 0, 0 );

	return husdAddBasePrim( prim, ref_type, prim_path, &asset_path );
    }

    inline bool 
    husdAddBasePrim( UsdPrim &prim, VOP_Node &vop )
    {
	UT_StringHolder ref_type;
	if( vop.hasParm( HUSD_SHADER_REFTYPE ))
	    vop.evalString( ref_type, HUSD_SHADER_REFTYPE, 0, 0 );

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

    inline bool
    husdRepresentsExistingPrim( VOP_Node &vop )
    {
	UT_StringHolder ref_type;
	if( vop.hasParm( HUSD_SHADER_REFTYPE ))
	    vop.evalString( ref_type, HUSD_SHADER_REFTYPE, 0, 0 );

	return ref_type == HUSD_REFTYPE_REP;
    }
} // anonymous namespace

static inline void
husdRewireConnectionsThruNodeGraphs( UsdShadeNodeGraph &graph_prim )
{
    // NOTE: This is a workaround for USD bug. Remove this function when fixed.
    for( auto &&child : graph_prim.GetPrim().GetChildren() )
    {
	UsdShadeShader shader_child( child );
	if( !shader_child )
	    continue;

	for( auto &&input : shader_child.GetInputs() )
	{
	    UsdShadeConnectableAPI  curr_prim;
	    TfToken		    curr_name;
	    UsdShadeAttributeType   curr_type;
	    if( !input.GetConnectedSource( &curr_prim, &curr_name, &curr_type )
		|| curr_prim.GetPrim() != graph_prim.GetPrim() )
		continue; // Shader's input is not connected

	    UsdShadeConnectableAPI  new_prim;
	    TfToken		    new_name;
	    UsdShadeAttributeType   new_type;
	    UsdShadeInput graph_input = graph_prim.GetInput( curr_name );
	    if( !graph_input.GetConnectedSource(&new_prim, &new_name,&new_type))
		continue; // NodeGraph's input is not connected
	    
	    // To work around the USD bug, we wire Shader's input directly to
	    // whatever NodeGraph's input connects to, thus bypassing
	    // the NodeGraph's input altogether.
	    input.ConnectToSource( new_prim, new_name, new_type );
	}
    }

    // Recurse into sub-graphs.
    for( auto &&child : graph_prim.GetPrim().GetChildren() )
    {
	UsdShadeNodeGraph graph_child( child );
	if( graph_child )
	    husdRewireConnectionsThruNodeGraphs( graph_child );
    }
}

static inline bool
husdHasUniversalShader( UsdShadeMaterial &usd_material )
{
    UsdShadeOutput surf_out = usd_material.GetSurfaceOutput();
    return surf_out && surf_out.HasConnectedSource();
}

static inline void
husdGeneratePreviewShader( HUSD_AutoWriteLock &lock,
	UsdShadeNodeGraph &usd_mat_or_graph, const HUSD_TimeCode &time_code,
	VOP_NodeList &shader_nodes, VOP_ShaderTypeList &shader_types,
	UT_StringArray &output_names )
{
    UsdShadeMaterial usd_mat( usd_mat_or_graph );
    if( !usd_mat || husdHasUniversalShader( usd_mat ))
	return;

    UT_StringHolder usd_mat_path( usd_mat_or_graph.GetPath().GetString() );
    int surface_idx = shader_types.find( VOP_SURFACE_SHADER );
    if( surface_idx < 0 )
	surface_idx = shader_types.find( VOP_BSDF_SHADER );

    if( surface_idx >= 0 )
	husdCreatePreviewShader( lock, usd_mat_path, time_code,
		*shader_nodes[ surface_idx ], output_names[ surface_idx ]);
}

bool
HUSD_CreateMaterial::createMaterial( VOP_Node &mat_vop,
	const UT_StringRef &usd_mat_path, 
	bool auto_generate_preview_shader ) const
{
    auto outdata = myWriteLock.data();
    if( !outdata || !outdata->isStageValid() )
	return false;

    // If node represents an existing USD primitive, no need to create it.
    if( husdRepresentsExistingPrim( mat_vop ))
	return true; 

    // Create the material or graph.
    bool is_graph = mat_vop.isUSDNodeGraph();
    auto stage = outdata->stage();
    auto usd_mat_or_graph = husdCreateMainPrim( stage, usd_mat_path, 
	    myParentType, !is_graph );
    auto usd_mat_or_graph_prim = usd_mat_or_graph.GetPrim();
    if( !usd_mat_or_graph_prim.IsValid() )
	return false;

    bool has_base_prim = husdAddBasePrim( usd_mat_or_graph_prim, mat_vop );
    HUSDsetPrimEditorNodeId( usd_mat_or_graph_prim, mat_vop.getUniqueId());

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

	// If node specifies a base material prim, then it represents a derived
	// material and not a shader, so don't translate it into a shader.
	if( is_mat_vop && has_base_prim )
	    continue;

	if( !husdCreateMaterialShader( myWriteLock, usd_mat_path, myTimeCode,
		    *shader_nodes[i], shader_types[i], output_names[i]))
	{
	    ok = false;
	}

	if( is_mat_vop )
	    is_mat_vop_translated = true;
    }

    // If the material node represents a derived material, we need to
    // translate its parameters, because that node was not translated yet.
    if( has_base_prim )
	husdCreateAndSetMaterialAttribs( usd_mat_or_graph, mat_vop );

    // If the material node has not been translated as a shader (because it
    // corresponds to the material primitive we just created), we may need
    // to do some further work, like connect input wires to a sibling graph.
    if( ok && !is_mat_vop_translated && mat_vop.translatesDirectlyToUSDPrim() )
	ok = husdCreateMaterialInputsIfNeeded( myWriteLock, usd_mat_or_graph, 
		myTimeCode, mat_vop );

    // Generate a standard USD Preview Surface shader.
    if( auto_generate_preview_shader )
	husdGeneratePreviewShader( myWriteLock, usd_mat_or_graph, myTimeCode, 
		shader_nodes, shader_types, output_names );

    // NOTE: thre is a USD bug that does not resolve shader parameter values 
    // correctly, when shader parameter is connected to a NodeGraph input, 
    // which is connected to something else. We work around it by adjusting
    // the connections directly to outputs.
    // Remove this call when the bug is fixed.
#if 0
    husdRewireConnectionsThruNodeGraphs( usd_mat_or_graph );
#endif

    return ok;
}

template< typename UT_TYPE >
void
husdCreateAndSetParmAttrib( const UsdTimeCode &tc, UsdPrim &prim, 
	const UT_StringHolder &name, const UT_OptionEntry *opt_value,
	const SdfValueTypeName &sdf_type )
{
    UsdAttribute    attrib( UsdShadeShader( prim ).CreateInput( 
			    TfToken(name.toStdString()), sdf_type ));

    UT_TYPE	    ut_value;
    opt_value->importOption( ut_value );
    HUSDsetAttribute( attrib, ut_value, tc );
}

template<>
void
husdCreateAndSetParmAttrib< UT_StringArray >( 
	const UsdTimeCode &tc, UsdPrim &prim, 
	const UT_StringHolder &name, const UT_OptionEntry *opt_value,
	const SdfValueTypeName &sdf_type )
{
    UsdAttribute    attrib( UsdShadeShader( prim ).CreateInput( 
			    TfToken(name.toStdString()), sdf_type ));

    UT_StringArray  ut_value;
    opt_value->importOption( ut_value );

    UT_Array<UT_StringHolder>	ut_cast( ut_value );
    HUSDsetAttribute( attrib, ut_cast, tc );
}

static inline void
husdCreateAndSetAttribute( UsdPrim &prim, 
	const UT_StringHolder &name, const UT_OptionEntry *value )
{
    UsdTimeCode		tc( UsdTimeCode::Default() );
    
    switch( value->getType() )
    {
	case UT_OPTION_INT:
	    husdCreateAndSetParmAttrib<int64>( tc, prim, name, value,
		    SdfValueTypeNames->Int );
	    break;

	case UT_OPTION_FPREAL:
	    husdCreateAndSetParmAttrib<double>( tc, prim, name, value,
		    SdfValueTypeNames->Double );
	    break;

	case UT_OPTION_STRING:
	    husdCreateAndSetParmAttrib<UT_StringHolder>( tc, prim, name, value,
		    SdfValueTypeNames->String );
	    break;

	case UT_OPTION_VECTOR2:
	    husdCreateAndSetParmAttrib<UT_Vector2D>( tc, prim, name, value,
		    SdfValueTypeNames->Double2 );
	    break;

	case UT_OPTION_VECTOR3:
	    husdCreateAndSetParmAttrib<UT_Vector3D>( tc, prim, name, value,
		    SdfValueTypeNames->Vector3d );
	    break;

	case UT_OPTION_VECTOR4:
	    husdCreateAndSetParmAttrib<UT_Vector4D>( tc, prim, name, value,
		    SdfValueTypeNames->Double4 );
	    break;

	case UT_OPTION_MATRIX2:
	    husdCreateAndSetParmAttrib<UT_Matrix2D>( tc, prim, name, value,
		    SdfValueTypeNames->Matrix2d );
	    break;

	case UT_OPTION_MATRIX3:
	    husdCreateAndSetParmAttrib<UT_Matrix3D>( tc, prim, name, value,
		    SdfValueTypeNames->Matrix3d );
	    break;

	case UT_OPTION_MATRIX4:
	    husdCreateAndSetParmAttrib<UT_Matrix4D>( tc, prim, name, value,
		    SdfValueTypeNames->Matrix4d );
	    break;

	case UT_OPTION_INTARRAY:
	    husdCreateAndSetParmAttrib< UT_Array<int32> >( tc, prim, 
		    name, value, SdfValueTypeNames->IntArray );
	    break;

	case UT_OPTION_FPREALARRAY:
	    husdCreateAndSetParmAttrib< UT_Array<fpreal64> >( tc, prim, 
		    name, value, SdfValueTypeNames->DoubleArray );
	    break;

	case UT_OPTION_STRINGARRAY:
	    husdCreateAndSetParmAttrib< UT_StringArray >( tc, prim, 
		    name, value, SdfValueTypeNames->StringArray );
	    break;

	default:
	    UT_ASSERT( !"Unhandled option type" );
	    break;
    }
}

static inline void
husdOverrideMatParms( const UsdShadeNodeGraph &usd_mat_or_graph, 
	const UT_Options &parms )
{
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
	    husdCreateAndSetAttribute( shader, parm_name, value );
	}
	else
	{
	    husdCreateAndSetAttribute( material, parm_name, value );
	}
    }
}

bool
HUSD_CreateMaterial::createDerivedMaterial( 
	const UT_StringRef &base_material_path,
	const UT_Options &material_parameters, 
	const UT_StringRef &usd_mat_path) const
{
    auto outdata = myWriteLock.data();
    if( !outdata || !outdata->isStageValid() )
	return false;

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
    husdOverrideMatParms( usd_mat_or_graph, material_parameters );

    return true;
}

