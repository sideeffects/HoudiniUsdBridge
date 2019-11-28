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
#include <pxr/usd/usdShade/material.h>
#include <pxr/usd/usd/inherits.h>
#include <pxr/usd/usd/specializes.h>

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

bool
HUSD_CreateMaterial::createMaterial( VOP_Node &mat_vop,
	const UT_StringRef &usd_mat_path, 
	bool auto_generate_preview_shader ) const
{
    auto outdata = myWriteLock.data();
    if( !outdata || !outdata->isStageValid() )
	return false;

    // Non-USD shader nodes (ie, building-blocks) can't be USD node graphs. 
    // But, they can be VEX-wrapped and become materials though.
    bool is_material = (!mat_vop.isUSDShader() || !mat_vop.isUSDNodeGraph());

    // Create the material or graph.
    auto stage = outdata->stage();
    auto usd_mat_or_graph = husdCreateMainPrim( stage, usd_mat_path, 
	    myParentType, is_material );
    if( !usd_mat_or_graph.GetPrim().IsValid() )
	return false;

    HUSDsetPrimEditorNodeId( usd_mat_or_graph.GetPrim(), mat_vop.getUniqueId());

    // Create the shaders inside the material.
    VOP_NodeList	shader_nodes;
    VOP_ShaderTypeList	shader_types;
    UT_StringArray	output_names;
    bool		ok = true; 
    mat_vop.findAllShaders( shader_nodes, shader_types, output_names );
    UT_ASSERT( shader_nodes.size() == shader_types.size() );
    for( int i = 0; i < shader_nodes.size(); i++ )
    {
	if( !husdCreateMaterialShader( myWriteLock, usd_mat_path, myTimeCode,
		    *shader_nodes[i], shader_types[i], output_names[i]))
	{
	    ok = false;
	}
    }

    // Generate a standard USD Preview Surface shader.
    UsdShadeMaterial usd_mat( usd_mat_or_graph );
    if( usd_mat && auto_generate_preview_shader && 
	!husdHasUniversalShader( usd_mat ))
    {
	int surface_idx = shader_types.find( VOP_SURFACE_SHADER );
	if( surface_idx < 0 )
	    surface_idx = shader_types.find( VOP_BSDF_SHADER );

	if( surface_idx >= 0 )
	    husdCreatePreviewShader( myWriteLock, usd_mat_path, myTimeCode,
		    *shader_nodes[ surface_idx ], output_names[ surface_idx ]);
    }

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
    if( !usd_mat_or_graph.GetPrim().IsValid() )
	return false;

    /* TODO: make it a choice between inheriting and specializing:
    auto inherits = usd_mat_or_graph.GetPrim().GetInherits();
    inherits.AddInherit( HUSDgetSdfPath( base_material_path ), 
	    UsdListPositionBackOfAppendList );
	    */
    auto specializes = usd_mat_or_graph.GetPrim().GetSpecializes();
    specializes.AddSpecialize( HUSDgetSdfPath( base_material_path ), 
	    UsdListPositionBackOfAppendList );

    husdOverrideMatParms( usd_mat_or_graph, material_parameters );

    return true;
}


// ============================================================================ 
// TODO:
// - could/should we move Karma translator to python?
// - in Add Material LOP, reuse previously defined usd shaders, if a vop
//   node participates in two (or more) materials
//   - Can we even do it? If so, we still need to be careful about overriding 
//     inputs/outputs and overriding parameters on shared shader? 
//     Maybe "extend" rather than "inherit" will do the trick.
// - in Add Material LOP, reuse the inlined shader code for several
//   network-based material HDA VOPs.
//   - reuse in a material prim (since it's supposed to be self contained?)
//   - reuse across material prims (for greater reuse)
// - in Edit Material recognize and create outputs on Generic Shader VOP
//   so they can be wired too
// - in Edit Material, emit only changed attribute values
//   - might be able to rely on Temporary Default if they are saved to .hip file
// - in Edit Material, allow disconnecting old and connecting new wires
// - in Edit Material, allow deleting old and adding new shader nodes
// - factor out Add and Edit Material LOPs (in LOP library)
//
// XXX:
// Q: How about OSL shaders as a final shader in a material?
// A: Well, RMan uses them only for patterns, so there is no issue 
//    with mat-shader relationship naming, since we use OSL relationships 
//    only for shader-to-shader inputs/outputs and not for associating material 
//    with final OSL shader.
//    If Karma starts using them the same should hold true.
//    If OSL ever becomes the final shader then "outputs:osl:surface.connect"
//    should be fine for both Karma and RenderMan to consume (?)
//
// Q: Currently we deduce the VOP language from the render mask. 
//    Is it a problem?
// A: Yes, it is because custom render mask is treated like Karma's VEX,
//    where shaders get wrapped in auto-shader, which is Karma-specific.
//    See r294491.
//
//    Also, it may become a problem if Karma starts using OSL shaders too. 
//    Currently we specify OSL in render mask and accept it for RMan only.
//    But using render mask for language is somewhat confusing, so maybe
//    we need to explicitly separat them? But then how about Materials
//    which may contain shaders that use different languages? Most likely
//    that info is needed only when dealing with shader rather than material
//    as a whole, and then the material can encode the language per shader.
//
//    Solution: add a language field to HDA op type editor (r294491)
//
// Q: Are the parameters promoted from VOPs to the LOP level?
// A: Yes, using Parm VOP. Even for RIS shaders. The LOP will know how
//    to build the USD material prim based on that setup (ie, to take 
//    parm value from the LOP node's parameter). 
//
