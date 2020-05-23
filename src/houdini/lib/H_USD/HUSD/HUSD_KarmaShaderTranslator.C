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

#include "HUSD_KarmaShaderTranslator.h"

#include "HUSD_TimeCode.h"
#include "XUSD_AttributeUtils.h"
#include "XUSD_Data.h"
#include "XUSD_Utils.h"

#include <PRM/PRM_SpareData.h>
#include <VOP/VOP_Node.h>
#include <VOP/VOP_ParmGenerator.h>
#include <VOP/VOP_Constant.h>
#include <VOP/VOP_CodeGenerator.h>
#include <OP/OP_Input.h>
#include <OP/OP_Utils.h>
#include <VEX/VEX_VexResolver.h>
#include <UT/UT_Ramp.h>
#include <UT/UT_StringStream.h>

#include <pxr/usd/usdShade/material.h>

using namespace UT::Literal;

PXR_NAMESPACE_USING_DIRECTIVE

// ============================================================================ 
static TfToken	theKarmaContextToken( "karma", TfToken::Immortal );


// ============================================================================ 
/// Creates and sets an attribute or attributes on the given USD shader
/// primitive to represent the given node parameter.
class husd_ParameterTranslator
{
public:
    virtual		~husd_ParameterTranslator() = default;

    /// Creates an attrbute on the @p shader according to the @p def_parm,
    /// and sets its value according to the value of the @p val_parm.
    /// If @p val_parm is null, the @p def_parm value is used.
    virtual void	addAndSetShaderAttrib( UsdShadeShader &shader,
				const HUSD_TimeCode &time_code,
				const PRM_Parm &def_parm, 
				const PRM_Parm *val_parm = nullptr) const = 0;

    /// Set's attribute value.
    static bool		setAttribValue( const UsdAttribute &shader_attrib,
				const PRM_Parm &parm,
				const HUSD_TimeCode &time_code );
protected:  
    /// Adds a parameter to the given shader.
    UsdAttribute	addShaderParmAttrib( UsdShadeShader &shader,
				const UT_StringRef &name, 
				const SdfValueTypeName &type ) const;

};

UsdAttribute
husd_ParameterTranslator::addShaderParmAttrib( UsdShadeShader &shader,
	const UT_StringRef &name, const SdfValueTypeName &type ) const
{
    // Make sure name and type are valid, or CreateInput may carash.
    if( !name.isstring() || !type )
	return UsdAttribute();

    // Shader parameter attrubutes are always in inputs namespace.
    return shader.CreateInput( TfToken( name.toStdString() ), type );
}

bool
husd_ParameterTranslator::setAttribValue( const UsdAttribute &attrib,
	const PRM_Parm &parm, const HUSD_TimeCode &time_code ) 
{
    // For time-independent parameters, use "default" time code (ie, 
    // set the "default" value for the attribute). Otherwise, use the time code
    // passed from the material class, which could still be "default", but
    // which could also be some non-zero time/frame (in which case we set
    // the attribute at some explicit time sample).
    // TODO: just comput usd_tc without going thru tc
    HUSD_TimeCode tc;
    if( parm.isTimeDependent() )
	tc = time_code;
    UsdTimeCode usd_tc = HUSDgetUsdTimeCode( tc );

    return HUSDsetAttribute( attrib, parm, usd_tc );
}

// ============================================================================ 
class husd_SimpleParameterTranslator : public husd_ParameterTranslator
{
public:
    void            addAndSetShaderAttrib( UsdShadeShader &shader,
			const HUSD_TimeCode &time_code,
			const PRM_Parm &def_parm, 
			const PRM_Parm *val_parm = nullptr) const override;
};

void
husd_SimpleParameterTranslator::addAndSetShaderAttrib( 
	UsdShadeShader &shader, const HUSD_TimeCode &time_code,
	const PRM_Parm &def_parm, const PRM_Parm *val_parm ) const
{
    UT_StringHolder name( def_parm.getToken() );
    auto	    type   = HUSDgetShaderAttribSdfTypeName( def_parm );
    auto	    attrib = addShaderParmAttrib( shader, name, type );

    if( !attrib.IsValid() )
	return;

    if( !val_parm )
	val_parm = &def_parm;
    setAttribValue( attrib, *val_parm, time_code );
}

// ============================================================================ 
class husd_RampParameterTranslator : public husd_ParameterTranslator
{
public:
    void            addAndSetShaderAttrib( UsdShadeShader &shader,
			    const HUSD_TimeCode &time_code,
			    const PRM_Parm &def_parm, 
			    const PRM_Parm *val_parm = nullptr ) const override;

protected:
    virtual void    addAndSetRampBasisAttrib( UsdShadeShader &shader,
			    const UT_StringRef &name, 
			    const UT_Ramp &ramp_val ) const;
    virtual void    addAndSetRampKeysAttrib( UsdShadeShader &shader,
			    const UT_StringRef &name, 
			    const UT_Ramp &ramp_val ) const;
    virtual void    addAndSetRampValuesAttrib( UsdShadeShader &shader,
			    const UT_StringRef &name, 
			    const UT_Ramp &ramp_val, bool is_color) const;

    virtual const char *    toBasisString( UT_SPLINE_BASIS basis_enum ) const;

};


void
husd_RampParameterTranslator::addAndSetShaderAttrib(
	UsdShadeShader &shader, const HUSD_TimeCode &time_code,
	const PRM_Parm &def_parm, const PRM_Parm *val_parm ) const
{
    if( !val_parm )
	val_parm = &def_parm;
    UT_ASSERT( def_parm.isRampType() );
    UT_ASSERT( val_parm->isRampType() );

    const PRM_SpareData *spare = def_parm.getSparePtr();
    UT_ASSERT( spare );
    if( !spare )
	return;

    OP_Node *node = val_parm->getParmOwner()->castToOPNode();
    UT_ASSERT( node );
    if( !node )
	return;

    UT_Ramp ramp_val;
    node->updateRampFromMultiParm( 0.0, *val_parm, ramp_val );

    UT_StringHolder basis_name(  spare->getRampBasisVar() );
    addAndSetRampBasisAttrib( shader, basis_name, ramp_val );

    UT_StringHolder keys_name(   spare->getRampKeysVar() );
    addAndSetRampKeysAttrib( shader, keys_name, ramp_val );

    bool	    is_color = def_parm.isRampTypeColor();
    UT_StringHolder values_name( spare->getRampValuesVar() );
    addAndSetRampValuesAttrib( shader, values_name, ramp_val, is_color );
}

void
husd_RampParameterTranslator::addAndSetRampBasisAttrib( 
	UsdShadeShader &shader,
	const UT_StringRef &name, const UT_Ramp &ramp_val ) const
{
    auto &type  = SdfValueTypeNames->StringArray;
    auto attrib = addShaderParmAttrib( shader, name, type );

    int n = ramp_val.getNodeCount();
    VtArray<std::string>	vt_val(n);
    for (int i = 0; i < n; i++)
	vt_val[i] = toBasisString( ramp_val.getNode(i)->basis );
    attrib.Set( vt_val );
}

void
husd_RampParameterTranslator::addAndSetRampKeysAttrib( 
	UsdShadeShader &shader,
	const UT_StringRef &name, const UT_Ramp &ramp_val ) const
{
    auto &type  = SdfValueTypeNames->DoubleArray;
    auto attrib = addShaderParmAttrib( shader, name, type );

    int n = ramp_val.getNodeCount();
    VtArray<double> vt_val(n);
    for (int i = 0; i < n; i++)
	vt_val[i] = ramp_val.getNode(i)->t;
    attrib.Set( vt_val );
}

void
husd_RampParameterTranslator::addAndSetRampValuesAttrib(
	UsdShadeShader &shader,
	const UT_StringRef &name, const UT_Ramp &ramp_val, bool is_color ) const
{
    if( is_color )
    {
	auto &type  = SdfValueTypeNames->Vector3dArray;
	auto attrib = addShaderParmAttrib( shader, name, type );

	int n = ramp_val.getNodeCount();
	VtArray<GfVec3d> vt_val(n);
	for (int i = 0; i < n; i++)
	    vt_val[i] = GfVec3d(
		    ramp_val.getNode(i)->rgba.r,
		    ramp_val.getNode(i)->rgba.g,
		    ramp_val.getNode(i)->rgba.b );
	attrib.Set( vt_val );
    }
    else
    {
	auto &type  = SdfValueTypeNames->DoubleArray;
	auto attrib = addShaderParmAttrib( shader, name, type );

	int n = ramp_val.getNodeCount();
	VtArray<double> vt_val(n);
	for (int i = 0; i < n; i++)
	    vt_val[i] = ramp_val.getNode(i)->rgba.r;
	attrib.Set( vt_val );
    }
}

const char *
husd_RampParameterTranslator::toBasisString( UT_SPLINE_BASIS basis_enum ) const
{
    switch( basis_enum )
    {
	case UT_SPLINE_CONSTANT:	return "constant";
	case UT_SPLINE_LINEAR:		return "linear";
	case UT_SPLINE_CATMULL_ROM:	return "catmull-rom";
	case UT_SPLINE_BEZIER:		return "bezier";
	case UT_SPLINE_BSPLINE:		return "bspline";
	case UT_SPLINE_HERMITE:		return "hermite";
	default:
	    UT_ASSERT(!"LinearSolve is not supported");
	    break;
    }

    UT_ASSERT(!"Unknown spline basis type");
    return "linear";
}

// ============================================================================ 
// Renderer-independent class. Potentially, factor it out as an XUSD class.
class husd_ShaderTranslatorHelper
{
public:
			 husd_ShaderTranslatorHelper(
				HUSD_AutoWriteLock &lock,
				const HUSD_TimeCode &time_code,
				VOP_Node &shader_vop );
    virtual		~husd_ShaderTranslatorHelper() = default;


    /// @{ Utility functions
    UsdShadeMaterial	getUsdMaterial( const UT_StringRef &prim_path ) const;
    UsdShadeNodeGraph	getUsdNodeGraph( const UT_StringRef &prim_path ) const;
    UsdShadeShader	getUsdShader( const UT_StringRef &prim_path ) const;
    UsdShadeShader	defineUsdShader( const UT_StringRef &prim_path ) const;
    /// @}

    /// @{ Accessors for the member variables.
    VOP_Node		&getShaderNode() const
				{ return myShaderNode; }
    const HUSD_TimeCode &getTimeCode() const
				{ return myTimeCode; }
    /// @}
    
protected:
    /// Creates and sets attributes on the shader USD primitive. 
    /// They correspond to the shader parameters on the shader vop node.
    /// The node may implement a few shader types, in which case
    /// @p shader_type disambiguates the specific shader implementation.
    /// @p parameter_names narrows down the parms to encode: if empty,
    ///	    all parms are encoded; if non-empty, attempts to encode only
    ///	    parameters that are contained in that array.
    void		encodeShaderParms( 
				UsdShadeShader &shader,
				VOP_Node &vop, VOP_Type shader_type,
				const UT_StringArray &parameter_names) const;

    /// Connects shder inputs to material inputs, if any of teh node inputs
    /// has a Parm VOP wired into it.
    void		connectShaderInputs( 
				const UT_StringRef &usd_material_path,
				const UT_StringRef &usd_parent_path,
				UsdShadeShader &shader,
				VOP_Node &vop, VOP_Type shader_type ) const;

    /// Encodes the given node parameter @p def_parm as an attribute on a USD 
    /// shader primitive @p shader, then sets the attribute value to
    /// the @p val_parm if not null, else to the @p def_parm value.
    void		encodeShaderParm( UsdShadeShader &shader,
				const PRM_Parm &def_parm, 
				const PRM_Parm *val_parm = nullptr ) const;

    /// Sets the attribute value on the parameter.
    void		encodeAttribValue( const UsdAttribute &attrib,
				const PRM_Parm &parm ) const;

    /// Creates an input on the ancestral primitive (ie Material or NodeGraph).
    UsdShadeInput	createAncestorInput( 
				const UT_StringRef &usd_material_path,
				const UT_StringRef &usd_parent_path,
				VOP_ParmGenerator *parm_vop, int output_idx,
				OP_Node *container_node ) const;

    /// Connects shader input to NodeGraph or Materil input.
    void		setOrConnectShaderInput( 
				const UT_StringRef &usd_material_path,
				const UT_StringRef &usd_parent_path,
				UsdShadeShader &shader,
				VOP_Node &vop, int input_idx) const;
    void		connectShaderInput( 
				const UT_StringRef &usd_material_path,
				const UT_StringRef &usd_parent_path,
				UsdShadeShader &shader,
				VOP_Node &vop, int in_idx, 
				VOP_ParmGenerator *parm_vop, int out_idx) const;
    void		setShaderInput( UsdShadeShader &shader,
				VOP_Node &vop, int in_idx, 
				VOP_Constant *parm_vop) const;

    /// Creates and connects a USD Primvar Reader shader prim as input.
    UsdShadeOutput	createPrimvarReader( 
				const UT_StringRef &usd_parent_path,
				VOP_ParmGenerator &parm_vop ) const;
    void		createAndConnectPrimvarReader( 
				const UT_StringRef &usd_parent_path,
				UsdShadeShader &shader,
				VOP_Node &vop, int in_idx, 
				VOP_ParmGenerator *parm_vop ) const;


    /// Returns an translator suitable for defining a usd attribute that
    /// corresponds to the given parameter.
    virtual const husd_ParameterTranslator *
			getParmTranslator( const PRM_Parm &parm ) const;

private:
    HUSD_AutoWriteLock		&myWriteLock;
    HUSD_TimeCode		 myTimeCode; 
    VOP_Node			&myShaderNode;
};

husd_ShaderTranslatorHelper::husd_ShaderTranslatorHelper( 
	HUSD_AutoWriteLock &lock,
	const HUSD_TimeCode &time_code,
	VOP_Node &shader_vop )
    : myWriteLock( lock )
    , myTimeCode( time_code )
    , myShaderNode( shader_vop )
{
}


template <typename T>
T
husdDefineUsdPrimOfTypedSchema( HUSD_AutoWriteLock &lock,
	const UT_StringRef &prim_path )
{
    auto outdata = lock.data();
    if( outdata && outdata->isStageValid() )
	return T::Define( outdata->stage(), SdfPath( prim_path.toStdString() ));

    return T();
}

UsdShadeShader
husd_ShaderTranslatorHelper::defineUsdShader( const UT_StringRef &path ) const
{
    return husdDefineUsdPrimOfTypedSchema<UsdShadeShader>( myWriteLock, path );
}

template <typename T>
T
husdGetUsdTypedPrimSchema( HUSD_AutoWriteLock &lock,
	const UT_StringRef &prim_path )
{
    auto outdata = lock.data();
    if( outdata && outdata->isStageValid() )
	return T::Get( outdata->stage(), SdfPath( prim_path.toStdString() ));

    return T();
}

UsdShadeMaterial
husd_ShaderTranslatorHelper::getUsdMaterial( const UT_StringRef &path ) const
{ 
    return husdGetUsdTypedPrimSchema<UsdShadeMaterial>( myWriteLock, path );
}

UsdShadeNodeGraph
husd_ShaderTranslatorHelper::getUsdNodeGraph( const UT_StringRef &path ) const
{ 
    return husdGetUsdTypedPrimSchema<UsdShadeNodeGraph>( myWriteLock, path );
}

UsdShadeShader
husd_ShaderTranslatorHelper::getUsdShader( const UT_StringRef &path ) const
{
    return husdGetUsdTypedPrimSchema<UsdShadeShader>( myWriteLock, path );
}

void
husd_ShaderTranslatorHelper::encodeShaderParms( 
	UsdShadeShader &shader,
	VOP_Node &vop, VOP_Type shader_type,
	const UT_StringArray &parameter_names) const
{
    bool has_context_tag = OPhasShaderContextTag(vop.getShaderParmTemplates());

    // Translate the node parameters to USD shader attributes.
    auto  parms = vop.getUSDShaderParms();
    for( auto &&parm : parms )
    {
	// TODO: Special care will need to be taken of ramp parameters.
	//	  See $SHS/python/husdshadertranslator/default.py.
	if( !parameter_names.isEmpty() &&
	     parameter_names.find( parm->getTokenRef()) < 0)
	    continue;

	if( !vop.isParmForShaderType( *parm, shader_type, has_context_tag ))
	    continue;

	encodeShaderParm( shader, *parm );
    }
}

void
husd_ShaderTranslatorHelper::connectShaderInputs( 
	const UT_StringRef &usd_material_path,
	const UT_StringRef &usd_parent_path,
	UsdShadeShader &shader,
	VOP_Node &vop, VOP_Type shader_type ) const
{
    bool has_context_tag = OPhasShaderContextTag(vop.getShaderParmTemplates());

    // Translate the node inputs to USD shader connections.
    // NOTE: Karma shader node connections are encoded as VEX code,
    //	     but we do handle connecting to Parm and Const VOPs, which
    //	     are respectively interpreted as material input and attrib value.
    for( int i = 0, n = vop.getInputsArraySize(); i < n; i++ )
    {
	if( !vop.getInput(i) )
	    continue;

	// Check if input belongs to the shader type.
	UT_String parm_name;
	vop.getParmNameFromInput( parm_name, i );
	PRM_Parm *parm = vop.getParmPtr( parm_name );
	if( parm 
	    && !vop.isParmForShaderType( *parm, shader_type, has_context_tag ))
	    continue;

	setOrConnectShaderInput( usd_material_path, usd_parent_path, 
		shader, vop, i );
    }
}


void
husd_ShaderTranslatorHelper::encodeShaderParm( UsdShadeShader &shader,
	const PRM_Parm &def_parm, const PRM_Parm *val_parm ) const
{
    const husd_ParameterTranslator *parm_xtor = getParmTranslator( def_parm );
    if( parm_xtor )
	parm_xtor->addAndSetShaderAttrib( shader, myTimeCode, def_parm, 
		val_parm );
}

void
husd_ShaderTranslatorHelper::encodeAttribValue( const UsdAttribute &attrib,
	const PRM_Parm &parm ) const
{
    husd_ParameterTranslator::setAttribValue( attrib, parm, myTimeCode );
}

void
husd_ShaderTranslatorHelper::setOrConnectShaderInput( 
	const UT_StringRef &usd_material_path,
	const UT_StringRef &usd_parent_path,
	UsdShadeShader &shader,
	VOP_Node &vop, int input_idx) const
{
    OP_Input *input = vop.getInputReference( input_idx, false );
    if( !input )
	return;

    VOP_Node *input_vop = CAST_VOPNODE( input->getNode() );
    if( !input_vop )
	return;
    int output_idx = input->getNodeOutputIndex();

    VOP_ParmGenerator *parm_vop = dynamic_cast<VOP_ParmGenerator *>(input_vop);
    if( parm_vop )
    {
	if( parm_vop->getOperator()->getName() == VOP_BIND_NAME )
	    createAndConnectPrimvarReader( usd_parent_path, 
		    shader, vop, input_idx, parm_vop );
	else
	    connectShaderInput( usd_material_path, usd_parent_path,
		    shader, vop, input_idx, parm_vop, output_idx );

	return;
    }

    VOP_Constant *const_vop = dynamic_cast<VOP_Constant*>( input_vop );
    if( const_vop )
    {
	setShaderInput( shader, vop, input_idx, const_vop );
	return;
    }
}

UsdShadeInput
husd_ShaderTranslatorHelper::createAncestorInput(
	const UT_StringRef &usd_material_path,
	const UT_StringRef &usd_parent_path,
	VOP_ParmGenerator *parm_vop, int output_idx,
	OP_Node *container_node ) const
{
    // Create material prim input.
    const UT_StringHolder &ancestor_input_name = parm_vop->getParmNameCache();
    TfToken ancestor_input_name_tk( ancestor_input_name.toStdString() );

    PRM_Parm *value_parm = nullptr;
    if( container_node )
	value_parm = container_node->getParmPtr( ancestor_input_name );

    if( !value_parm )
	value_parm = parm_vop->getParmPtr( 
		parm_vop->getParameterDefaultValueParmName() );

    SdfValueTypeName ancestor_input_sdf_type = 
	HUSDgetShaderOutputSdfTypeName( *parm_vop, output_idx, value_parm );

    UsdShadeInput ancestor_input;
    if( parm_vop->isSubnetInput() )
	ancestor_input = getUsdNodeGraph( usd_parent_path ).CreateInput(
	    ancestor_input_name_tk, ancestor_input_sdf_type );
    else
	ancestor_input = getUsdMaterial( usd_material_path ).CreateInput(
	    ancestor_input_name_tk, ancestor_input_sdf_type );

    // Set value on the material input.
    if( value_parm )
	encodeAttribValue( ancestor_input.GetAttr(), *value_parm );

    // Set some input metadata.
    if( parm_vop )
    {
	UT_String  parm_val;

	parm_vop->parmLabel( parm_val );
	if( parm_val.isstring() )
	    ancestor_input.GetAttr().SetDisplayName( parm_val.toStdString() );

	parm_vop->parmComment( parm_val );
	if( parm_val.isstring() )
	    ancestor_input.SetDocumentation( parm_val.toStdString() );
    }

    return ancestor_input;
}

static inline UsdShadeInput 
husdCreateShaderInput( UsdShadeShader &shader, VOP_Node &vop, int input_idx )
{
    UT_StringHolder shader_input_name;
    vop.getInputName( shader_input_name, input_idx );

    TfToken shader_input_name_tk( shader_input_name.toStdString() );
    SdfValueTypeName shader_input_sdf_type = 
	HUSDgetShaderInputSdfTypeName( vop, input_idx );

    return shader.CreateInput( shader_input_name_tk, shader_input_sdf_type );
}

void
husd_ShaderTranslatorHelper::connectShaderInput( 
	const UT_StringRef &usd_material_path,
	const UT_StringRef &usd_parent_path,
	UsdShadeShader &shader,
	VOP_Node &vop, int input_idx, 
	VOP_ParmGenerator *parm_vop, int output_idx) const
{
    OP_Node  *container_node = vop.getParent();
    UsdShadeInput ancestor_input = createAncestorInput( 
	    usd_material_path, usd_parent_path,
	    parm_vop, output_idx, container_node );

    UsdShadeInput shader_input = husdCreateShaderInput( shader, vop, input_idx);
    UsdShadeConnectableAPI::ConnectToSource(shader_input, ancestor_input );
}

static inline void
husdAddUSDShaderID( UsdShadeShader &shader, const UT_StringRef &shader_name )
{
    TfToken shader_id( shader_name.toStdString() );
    shader.SetShaderId( shader_id );
}

static inline void
husdAddUSDShaderPath( UsdShadeShader &shader, const UT_StringRef &shader_name )
{
    SdfAssetPath  sdf_path( shader_name.toStdString() );
    shader.SetSourceAsset( sdf_path );
}

static inline SdfValueTypeName
husdGetPrimvarReaderSdfType( VOP_ParmGenerator &parm_vop )
{
    // See the standard specification for UsPrimvarReader in:
    // pxr/usdImagin/plugin/usdShader/shaders/shaderDef.usda
    switch( parm_vop.getParameterType() )
    {
	case VOP_TYPE_INTEGER:	
	    return SdfValueTypeNames->Int;
	case VOP_TYPE_STRING:	
	    return SdfValueTypeNames->String;
	case VOP_TYPE_FLOAT:	
	    return SdfValueTypeNames->Float;
	case VOP_TYPE_VECTOR2:	
	    return SdfValueTypeNames->Float2;
	case VOP_TYPE_VECTOR:	
	    return SdfValueTypeNames->Vector3f;
	case VOP_TYPE_VECTOR4:
	    return SdfValueTypeNames->Float4;
	case VOP_TYPE_NORMAL:
	    return SdfValueTypeNames->Normal3f;
	case VOP_TYPE_POINT:
	    return SdfValueTypeNames->Point3f;
	case VOP_TYPE_MATRIX4:	
	    return SdfValueTypeNames->Matrix4d;
	case VOP_TYPE_COLOR:
	    return SdfValueTypeNames->Float3;
	default:
	    break;
    }

    UT_ASSERT( !"Non-standard Primvar type" );
    return SdfValueTypeName();
}

static inline UT_StringHolder
husdGetPrimvarReaderID( VOP_ParmGenerator &parm_vop )
{
    // See the standard specification for UsPrimvarReader in:
    // pxr/usdImagin/plugin/usdShader/shaders/shaderDef.usda
    switch( parm_vop.getParameterType() )
    {
	case VOP_TYPE_INTEGER:	
	    return "UsdPrimvarReader_int"_sh;
	case VOP_TYPE_STRING:	
	    return "UsdPrimvarReader_string"_sh;
	case VOP_TYPE_FLOAT:	
	    return "UsdPrimvarReader_float"_sh;
	case VOP_TYPE_VECTOR2:	
	    return "UsdPrimvarReader_float2"_sh;
	case VOP_TYPE_VECTOR:	
	    return "UsdPrimvarReader_vector"_sh;
	case VOP_TYPE_VECTOR4:
	    return "UsdPrimvarReader_float4"_sh;
	case VOP_TYPE_NORMAL:
	    return "UsdPrimvarReader_normal"_sh;
	case VOP_TYPE_POINT:
	    return "UsdPrimvarReader_point"_sh;
	case VOP_TYPE_MATRIX4:	
	    return "UsdPrimvarReader_matrix"_sh;
	case VOP_TYPE_COLOR:
	    return "UsdPrimvarReader_float3"_sh;
	default:
	    break;
    }

    UT_ASSERT( !"Non-standard Primvar type" );
    return "UsdPrimvarReader"_sh;
}

UsdShadeOutput 
husd_ShaderTranslatorHelper::createPrimvarReader( 
	const UT_StringRef &usd_parent_path,
	VOP_ParmGenerator &parm_vop ) const
{
    TfToken		shader_name( parm_vop.getName().toStdString() );
    SdfPath		parent_path( usd_parent_path.toStdString() );
    SdfPath		shader_path = parent_path.AppendChild( shader_name );
    UT_StringHolder	shader_path_str( shader_path.GetString() );

    UsdShadeShader shader = defineUsdShader( shader_path_str );
    if( !shader )
	return UsdShadeOutput();

    // Set the shader ID attribute.
    husdAddUSDShaderID( shader, husdGetPrimvarReaderID( parm_vop ));

    // Create and set 'varname' attrib.
    UsdShadeInput varname_attr = shader.CreateInput( 
	    TfToken("varname"), SdfValueTypeNames->String );
    UT_StringHolder varname = parm_vop.getParmNameCache();
    HUSDsetAttribute( varname_attr.GetAttr(), varname, UsdTimeCode::Default() );

    // Create and set the 'fallback' attrib.
    SdfValueTypeName sdf_type = husdGetPrimvarReaderSdfType( parm_vop );
    UsdShadeInput fallback_attr = shader.CreateInput( 
	    TfToken("fallback"), sdf_type );
    PRM_Parm *fallback_parm = parm_vop.getParmPtr(
	    parm_vop.getParameterDefaultValueParmName() );
    encodeAttribValue( fallback_attr.GetAttr(), *fallback_parm );

    // Create and return the prim output.
    return shader.CreateOutput( TfToken("result"), sdf_type );
}

void
husd_ShaderTranslatorHelper::createAndConnectPrimvarReader( 
	const UT_StringRef &usd_parent_path,
	UsdShadeShader &shader,
	VOP_Node &vop, int input_idx, 
	VOP_ParmGenerator *parm_vop ) const
{
    if( !parm_vop )
    {
	// TODO: implement a code path when there is no parm_vop:
	//  - define primvar reader based on vop's input its corresponding parm
	UT_ASSERT( !"No parm vop");
	return;
    }

    UsdShadeOutput primvar_output = createPrimvarReader( 
	    usd_parent_path, *parm_vop );
    if( !primvar_output )
	return;

    UsdShadeInput shader_input = husdCreateShaderInput(shader, vop, input_idx);
    UsdShadeConnectableAPI::ConnectToSource( shader_input, primvar_output );
}

void
husd_ShaderTranslatorHelper::setShaderInput( UsdShadeShader &shader,
	VOP_Node &vop, int input_idx, VOP_Constant *const_vop) const
{
    PRM_Parm *value_parm = const_vop->getParmPtr( 
		const_vop->getConstantValueParmName() );
    if( !value_parm )
	return;

    // Create shader prim input and set (or override) its value.
    UsdShadeInput shader_input = husdCreateShaderInput(shader, vop, input_idx);
    encodeAttribValue( shader_input.GetAttr(), *value_parm );
}

const husd_ParameterTranslator *
husd_ShaderTranslatorHelper::getParmTranslator( const PRM_Parm &parm ) const
{
    // Check ramps.
    if( parm.isRampType() )
    {
	static husd_RampParameterTranslator theRampTranslator;
	return &theRampTranslator;
    }

    // Must be one of the simple parm types, like int, vector, etc.
    static husd_SimpleParameterTranslator theParmTranslator;
    return &theParmTranslator;
}


// ============================================================================ 
class husd_KarmaShaderTranslatorHelper : public husd_ShaderTranslatorHelper
{
public:
			husd_KarmaShaderTranslatorHelper(
				HUSD_AutoWriteLock &lock,
				const HUSD_TimeCode &time_code,
				VOP_Node &shader_vop );

    /// Performs the actual shader encoding (ie, defining it on the stage).
    void		createMaterialShader(
				const UT_StringRef &usd_material_path,
				const UT_StringRef &usd_parent_path,
				VOP_Type requested_shader_type,
				const UT_StringHolder &output_name ) const;
    UT_StringHolder	createShader(
				const UT_StringRef &usd_material_path,
				const UT_StringRef &usd_parent_path,
				VOP_Type requested_shader_type,
				const UT_StringHolder &output_name ) const;

    /// Updates the shader input attributes. 
    /// If 'parameter_names' is empty, all shader parameters are updated;
    /// otherwise, attempts to update only the parameters contained in it.
    void		updateShaderParameters(
				const UT_StringRef &usd_shader_path,
				const UT_StringArray &parameter_names) const;
				

private:
    VOP_Type		getEffectiveShaderType( VOP_Node &vop,
				VOP_Type requested_shader_type ) const;
    VOP_Node &		getEffectiveShaderNode(
				VOP_Type requested_shader_type ) const;
    UT_StringHolder	getEffectiveShaderName(
				VOP_Type requested_shader_type ) const;
    bool		isProcedural( VOP_Type requested_shader_type ) const;
    bool		isEncapsulated( VOP_Type requested_shader_type ) const;
    void		defineShaderDependencies(
				const UT_StringRef &usd_material_path,
				const UT_StringRef &usd_parent_path,
				VOP_Type requested_shader_type ) const;
    void		defineDependencyShaderIfNeeded( 
				const UT_StringRef &usd_material_path,
				const UT_StringRef &usd_parent_path,
				const UT_StringRef &shader_id,
				VOP_Type requested_shader_type ) const;
    UsdShadeShader	defineShaderForNode(
				const UT_StringRef &usd_material_path,
				const UT_StringRef &usd_parent_path,
				VOP_Node &vop,
				VOP_Type requested_shader_type,
				const UT_StringRef &shader_id ) const;
    UsdShadeShader	createUsdShaderPrim( 
				const UT_StringRef &usd_parent_path,
				VOP_Node &vop, 
				VOP_Type requested_shader_type,
				bool is_auto_shader ) const;
    void		connectShaderWrapperInput( 
				const UT_StringRef &usd_material_path,
				const UT_StringRef &usd_parent_path,
				UsdShadeShader &shader,
				VOP_ParmGenerator *parm_vop) const;
    void		createAndConnectShaderWrapperPrimvarReader( 
				const UT_StringRef &usd_parent_path,
				UsdShadeShader &shader,
				VOP_ParmGenerator *parm_vop ) const;
    void		encodeShaderWrapperParms(
				const UT_StringRef &usd_material_path,
				const UT_StringRef &usd_parent_path,
				UsdShadeShader &shader,
				VOP_Node &vop, 
				VOP_Type requested_shader_type ) const;
    void		encodeEncapsulatedShaderParms(
				UsdShadeShader &shader,
				VOP_Node &child_vop ) const;
    void		addAndSetCoShaderInputs( 
				const UT_StringRef &usd_parent_path,
				UsdShadeShader &shader,
				VOP_Node &vop,
				VOP_Type requested_shader_type ) const;

};

static inline TfToken
husdGetUSDShaderName( VOP_Node &vop, VOP_Type shader_type, bool is_auto_shader )
{
    UT_WorkBuffer   buff;

    // TODO: Decide on the conventions for shader name. If material has
    //	     several shaders, each needs a unique name.
    //	     But no need for suffix for single-context materials?
    if( is_auto_shader )
	buff.append( "auto_" );
    buff.append( vop.getName() );
    // TODO: use a parsable separator, since we want to decude
    //	    the node name in Mat Edit LOP. Without stripping this suffix off,
    //	    the edited prim name will be "mat_hda_surface_surface".
    //	    See husdGetShaderNodeName().
    buff.append( '_' );
    buff.append( VOPgetShaderTypeName( shader_type ));

    // Ensure the shader prim name is valid.
    std::string name_str = buff.toStdString();
    if( !TfIsValidIdentifier( name_str ))
	name_str = TfMakeValidIdentifier( name_str );

    return TfToken( name_str );
}

static inline TfToken
husdGetUSDMaterialOutputName( VOP_Type shader_type )
{
    TfToken		shader_type_token;

    if( shader_type == VOP_SURFACE_SHADER )
	shader_type_token = UsdShadeTokens->surface;
    else if( shader_type == VOP_DISPLACEMENT_SHADER )
	shader_type_token = UsdShadeTokens->displacement;
    else if( shader_type == VOP_ATMOSPHERE_SHADER )
	shader_type_token = UsdShadeTokens->volume;
    else
	shader_type_token = TfToken( VOPgetShaderTypeName( shader_type ));

    return TfToken( SdfPath::JoinIdentifier(
		theKarmaContextToken, shader_type_token ));
}

static inline void
husdConnectShaders( 
	UsdShadeShader &input_shader,  const TfToken &output_name, 
	UsdShadeShader &output_shader, const TfToken &input_name,
	const SdfValueTypeName &type )
{
    auto output = input_shader.CreateOutput( output_name, type );
    auto input  = output_shader.CreateInput( input_name, type );
    input.ConnectToSource( output );
}

static inline void
husdSetTarget( UsdPrim prim, const TfToken &relationship_name,
	const SdfPath &target_path )
{
    SdfPathVector targets{ target_path };
    prim.CreateRelationship( relationship_name, false ).SetTargets( targets );
}

static inline void
husdConnectMaterialTerminal( 
	const UsdShadeMaterial &material, const TfToken &mat_out_name, 
	UsdShadeShader &shader, const UT_StringHolder &shader_out_name )
{
    TfToken output_name_tk( shader_out_name.toStdString() );
    if( output_name_tk.IsEmpty() )
	output_name_tk = TfToken("out");
    auto shader_out = shader.CreateOutput( output_name_tk,
	    SdfValueTypeNames->Token );
    auto material_terminal = material.CreateOutput( mat_out_name, 
	    SdfValueTypeNames->Token );

    // When connecting an output to an output, it is the container's output 
    // that connect's to the shader's output.
   material_terminal.ConnectToSource( shader_out );
}

static inline VOP_Node *
husdGetShaderNode(const char *str)
{
    UT_String	path(str);

    // Shader string may refer to an auto-generated wrapper.
    path.replacePrefix("op:_auto_/", "op:/");
    return CAST_VOPNODE( OPgetDirector()->getCwd()->findNode( path ));
}

static inline bool
husdIsAutoVopShaderName( const char *shader_id )
{
    return UT_StringWrap( shader_id ).startsWith("op:_auto_/");
}

static inline UT_StringArray 
husdGetGeoProcDependencies( VOP_Node &shader_node )
{
    UT_StringArray shader_deps;

    UT_StringArray shader_map;
    shader_node.getShaderInputMap( shader_map );
    for(int i = 0; i < shader_map.entries(); i += 2 )
    {
	const UT_StringHolder &node_path = shader_map[i+1];
	if( node_path.startsWith( OPREF_PREFIX ))
	{
	    shader_deps.append( node_path );
	}
	else
	{
	    UT_WorkBuffer buffer;
	    buffer.append( OPREF_PREFIX );
	    buffer.append( node_path );

	    shader_deps.append( UT_StringHolder( buffer ));
	}
    }

    return shader_deps;
}

static inline void
husdAddShaderCode( UsdShadeShader &shader, 
	const UT_StringRef &shader_id, VOP_ContextType context_type )
{
    // TODO: look thru SOHO_Scene::scanForOpsMantra() and implement
    //	    the edge cases there (eg, HDAs in Embedded lib, pure compiled, etc).

    // Set up the compiler flags for shaders embedded in the USD.
    VEX_CodeGenFlags	 cg_flags = 
	  VEX_CG_OMIT_PRAGMAS	// We don't need #pragmas in the IFD.
	| VEX_CG_OMIT_COMMENTS	// Neither do we need comments in IFD.
	| VEX_CG_NO_SHADER_IMPORT_CHECK;    // Shaders may not be available yet.

    // Try first to resolve VEX code. However, if the generation failed, don't
    // attempt to sub in empty VEX code. We'll only do that if the VEX assembly
    // resolution failed.
    UT_OStringStream os;
    VEX_VexResolver::getVflCode( shader_id, os, cg_flags, context_type );

    const UT_WorkBuffer &shader_code = os.str();
    shader.SetSourceCode( shader_code.toStdString() );

    // TODO: add compile error checking, and propagate it to the LOP node
#if 0
    // Error checking
    VEX_ErrorString errors;
    bool	    is_compiled_node;
    os.reset();
    VEX_VexResolver::getVexCode( shader_path, os, cg_flags, context_type,
				&is_compiled_node, &errors);
    if (errors.errors().isstring())
	addMessage(2 /*error_code*/, (const char *)errors.errors());
    if (errors.warnings().isstring())
	addMessage(1 /*warning_code*/, (const char *)errors.warnings());
#endif
}

static inline bool
husdIsProcedural( VOP_Type shader_type )
{
    return shader_type == VOP_GEOMETRY_SHADER;
}

static inline VOP_Node *
husdGetProcedural( VOP_Node &vop, VOP_Type shader_type, 
	bool encapsulated_only = false )
{
    if( !husdIsProcedural( shader_type ))
	return nullptr;

    // The vop may be just a material containing the actual (encapsulated)
    // procedural shader vop child.
    VOP_Node *procedural_vop = vop.getProcedural( shader_type );
    if( encapsulated_only || procedural_vop )
	return procedural_vop;

    // Otherwise, the vop itself is a procedural geometry shader. 
    return &vop;
}

static inline VOP_ContextType
husdGetContextType( VOP_Node &vop, VOP_Type shader_type )
{
    return VOPconvertToContextType( shader_type,
	    vop.getLanguage()->getLanguageType() );
}

husd_KarmaShaderTranslatorHelper::husd_KarmaShaderTranslatorHelper( 
	HUSD_AutoWriteLock &lock, const HUSD_TimeCode &tc,
	VOP_Node &shader_vop )
    : husd_ShaderTranslatorHelper( lock, tc, shader_vop )
{
}

void
husd_KarmaShaderTranslatorHelper::createMaterialShader(
	const UT_StringRef &usd_material_path,
	const UT_StringRef &usd_parent_path,
	VOP_Type requested_shader_type,
	const UT_StringHolder &output_name ) const
{
    // Karma shader (or procedural) may be importing (or using) some 
    // usd-inlined shaders, so need to define them too.
    defineShaderDependencies( usd_material_path, usd_parent_path,
	    requested_shader_type );

    // Define the shader USD primitive.
    VOP_Node &	    vop    = getEffectiveShaderNode( requested_shader_type );
    UT_StringHolder name   = getEffectiveShaderName( requested_shader_type );
    UsdShadeShader  shader = defineShaderForNode( 
	    usd_material_path, usd_parent_path, 
	    vop, requested_shader_type, name );

    if( !shader.GetPrim().IsValid() )
	return;

    TfToken terminal_name = husdGetUSDMaterialOutputName( 
	    requested_shader_type );
    husdConnectMaterialTerminal( getUsdMaterial( usd_material_path ), 
	    terminal_name, shader, output_name );
}

UT_StringHolder	
husd_KarmaShaderTranslatorHelper::createShader(
	const UT_StringRef &usd_material_path,
	const UT_StringRef &usd_parent_path,
	VOP_Type requested_shader_type,
	const UT_StringHolder &output_name ) const
{
    // Karma shader (or procedural) may be importing (or using) some 
    // usd-inlined shaders, so need to define them too.
    defineShaderDependencies( usd_material_path, usd_parent_path,
	    requested_shader_type );

    // Define the shader USD primitive.
    VOP_Node &	    vop    = getEffectiveShaderNode( requested_shader_type );
    UT_StringHolder name   = getEffectiveShaderName( requested_shader_type );
    UsdShadeShader  shader = defineShaderForNode( 
	    usd_material_path, usd_parent_path, 
	    vop, requested_shader_type, name );

    if( !shader.GetPrim().IsValid() )
	return UT_StringHolder();

    TfToken full_output_name( UsdShadeTokens->outputs.GetString() + 
	output_name.toStdString() );
    auto full_output_path = shader.GetPath().AppendProperty( full_output_name );

    return UT_StringHolder( full_output_path.GetString() );
}

void
husd_KarmaShaderTranslatorHelper::updateShaderParameters(
	const UT_StringRef &usd_shader_path,
	const UT_StringArray &parameter_names) const
{
    UsdShadeShader shader = getUsdShader( usd_shader_path );
    if( !shader )
	return;

    // Figure out the VOP shader type for the given USD shader.
    // Karma materials use suffix in the prim name, so use it 
    // just the way we do in HUSD_EditMaterial.
    VOP_Type	    shader_type = VOP_SURFACE_SHADER;
    UT_StringHolder prim_name( shader.GetPrim().GetName().GetString() );
    if( prim_name.endsWith("_displace"))
	shader_type = VOP_DISPLACEMENT_SHADER;

    // TODO: Use the 'parameter_names' parameter above to limit re-translation.
    //	     Special care needs to be taken of ramp parameters,
    //	     and inputs whose name come from parm tag rather than parm name.
    //	     See $SHS/python/husdshadertranslator/default.py.
    //	     Until then just use empty list to re-encode all relevant parms.
    UT_StringArray empty_parameter_names; // empty list means "all parameters".
   
    // Remove input attributes because some of them may now be at default value,
    // and therefore should not be authored anymore.
    for( auto &&input : shader.GetInputs() )
    {
	if( empty_parameter_names.isEmpty() || 
	    empty_parameter_names.find( input.GetBaseName().GetString() ) >= 0)
	{
	    shader.GetPrim().RemoveProperty( input.GetFullName() );
	}
    }

    // Now that shader has no inputs, re-encode them anew.
    VOP_Node &vop = getShaderNode();
    encodeShaderParms( shader, vop, shader_type, empty_parameter_names );
}

VOP_Type
husd_KarmaShaderTranslatorHelper::getEffectiveShaderType( VOP_Node &vop,
	VOP_Type requested_shader_type ) const
{
    VOP_Type vop_type = vop.getShaderType();

    if( vop_type == VOP_VOP_MATERIAL_SHADER )
	return requested_shader_type;

    return vop_type;
}

VOP_Node &
husd_KarmaShaderTranslatorHelper::getEffectiveShaderNode(
	VOP_Type requested_shader_type ) const
{
    // Check if we are dealing with a procedural geometry shader.  It may be 
    // represented by an encapsulated node, and we'll need that node's parms.
    VOP_Node *vop = husdGetProcedural( getShaderNode(), 
	    requested_shader_type );
    if( vop )
	return *vop;

    return getShaderNode();
}

UT_StringHolder
husd_KarmaShaderTranslatorHelper::getEffectiveShaderName(
	VOP_Type requested_shader_type ) const
{ 
    return getEffectiveShaderNode( requested_shader_type ).getShaderName( 
	    VOP_ShaderNameStyle::RELAXED_AUTO, requested_shader_type );
}

bool
husd_KarmaShaderTranslatorHelper::isProcedural(
	VOP_Type requested_shader_type ) const
{
    return husdIsProcedural( requested_shader_type );
}

bool
husd_KarmaShaderTranslatorHelper::isEncapsulated(
	VOP_Type requested_shader_type ) const
{
    return husdGetProcedural( getShaderNode(), requested_shader_type, true );
}

void
husd_KarmaShaderTranslatorHelper::defineShaderDependencies(
	const UT_StringRef &usd_material_path,
	const UT_StringRef &usd_parent_path,
	VOP_Type requested_shader_type ) const
{
    UT_StringArray shader_deps;

    VOP_Node *procedural_vop = husdGetProcedural( getShaderNode(), 
	    requested_shader_type );
    if( procedural_vop )
    {
	// Geometry shader does not have a corresponding VEX context type,
	// so resolver can't use code generator to figure out call dependencies.
	// However, geometry shaders can take "callback" cvex shaders, which
	// it invokes during execution of procedural geometry generation.
	shader_deps = husdGetGeoProcDependencies( *procedural_vop );
    }
    else
    {
	VOP_ContextType ctx = husdGetContextType( 
		getShaderNode(), requested_shader_type );
	VEX_VexResolver::getDependencies( 
		getEffectiveShaderName( requested_shader_type ), 
		shader_deps, ctx ); 
    }

    for (exint i = 0; i < shader_deps.entries(); i++)
	defineDependencyShaderIfNeeded( usd_material_path, usd_parent_path, 
		shader_deps(i), requested_shader_type );
}

void
husd_KarmaShaderTranslatorHelper::defineDependencyShaderIfNeeded( 
	const UT_StringRef &usd_material_path,
	const UT_StringRef &usd_parent_path,
	const UT_StringRef &shader_id,
	VOP_Type requested_shader_type ) const
{
    bool is_procedural = isProcedural( requested_shader_type );

    // For procedural co-shaders, we need to define USD shader to encode
    // the parameters they need to be invoked from the procedural.
    if( !is_procedural )
    {
	// For shaders that were referenced as shader calls, we need to save
	// the source code, but only if Karma does not have access to it
	// via HDAs with cached code, etc.
	if(!VEX_VexResolver::needsVexResolverForMantraOutput( shader_id ))
	    return;

	// Even if shader_id provides VEX code (tested above), it may do so
	// as an external shader rather than as code generated from children.
	// We need to only define a shader prim for shaders with source code
	// This test is following SOHO_Scene::sohoEmbedVex() as an example.
	UT_String ctx_name;
	VOP_ContextType requested_ctx = VOPconvertToContextType(
		 requested_shader_type, VOP_LANGUAGE_VEX );
	VEX_VexResolver::getVexContext( shader_id, ctx_name, requested_ctx );
	if( !ctx_name.isstring() )
	    return;
    }

    // Get shader node to encode.
    VOP_Node *vop = husdGetShaderNode( shader_id );
    if( !vop )
	return;

    // Encapsulated shaders may actually be HDAs with code. 
    // Note, the reason why we passed the shader_id explicitly is that
    // some nodes may report auto shader name, while the dependency 
    // saving requires non-auto version, and we can't really differentiate
    // between the two cases here, implicitly.
    UT_StringHolder final_shader_id( shader_id );
    if( is_procedural )
	final_shader_id = vop->getShaderName( true, vop->getShaderType() );

    defineShaderForNode( usd_material_path, usd_parent_path, 
	    *vop, requested_shader_type, final_shader_id );
}

UsdShadeShader
husd_KarmaShaderTranslatorHelper::defineShaderForNode( 
	const UT_StringRef &usd_material_path,
	const UT_StringRef &usd_parent_path,
	VOP_Node &vop, 
	VOP_Type requested_shader_type,
	const UT_StringRef &shader_id ) const
{
    // Create USD shader primitive.
    UsdShadeShader shader = createUsdShaderPrim( usd_parent_path,
	    vop, requested_shader_type,
	    husdIsAutoVopShaderName( shader_id ));
    UT_ASSERT( shader );
    if( !shader )
	return UsdShadeShader();

    VOP_Type shader_type = getEffectiveShaderType(vop, requested_shader_type);
    UT_StringArray parameter_names; // empty list means "all parameters".
    
    // Currently, auto-wrapper shaders don't have a way of specifying 
    // argument values other than defaults.
    if( isEncapsulated( requested_shader_type ))
    {
	encodeEncapsulatedShaderParms( shader, vop );
    }
    else if( husdIsAutoVopShaderName( shader_id ))
    {
	encodeShaderWrapperParms( usd_material_path, usd_parent_path, 
		shader, vop, requested_shader_type );
    }
    else // regular shader node
    {
	encodeShaderParms( 
		shader, vop, shader_type, parameter_names );
	connectShaderInputs( usd_material_path, usd_parent_path, 
		shader, vop, shader_type );
    }

    // Geometry procedurals use input connections for CVEX shaders.
    VOP_Node *procedural_vop = husdGetProcedural( vop, shader_type );
    if( procedural_vop )
	addAndSetCoShaderInputs( usd_parent_path, shader, 
		*procedural_vop, requested_shader_type );

    // Save the shader code, if the vop node generates it.
    VOP_ContextType context_type = husdGetContextType( vop, shader_type );
    if( context_type != VOP_CONTEXT_TYPE_INVALID &&
	VEX_VexResolver::needsVexResolverForMantraOutput( shader_id ))
    {
	husdAddShaderCode( shader, shader_id, context_type );
    }
    else
    {
	husdAddUSDShaderPath( shader, shader_id );
    }

    return shader;
}

UsdShadeShader 
husd_KarmaShaderTranslatorHelper::createUsdShaderPrim( 
	const UT_StringRef &usd_parent_path,
	VOP_Node &vop, VOP_Type requested_shader_type,
	bool is_auto_shader ) const
{
    VOP_Type		 shader_type = getEffectiveShaderType( vop, 
					requested_shader_type );
    TfToken		 shader_token = husdGetUSDShaderName( vop, shader_type, 
					is_auto_shader );
    SdfPath		 parent_path( usd_parent_path.toStdString() );
    SdfPath		 shader_path = parent_path.AppendChild( shader_token );
    UT_StringHolder	 shader_path_str( shader_path.GetString() );

    HUSD_ShaderTranslatorRegistry::get()
	.reportShaderTranslation( vop, shader_path_str );

    return defineUsdShader( shader_path_str );
}

void
husd_KarmaShaderTranslatorHelper::connectShaderWrapperInput(
	const UT_StringRef &usd_material_path,
	const UT_StringRef &usd_parent_path,
	UsdShadeShader &shader,
	VOP_ParmGenerator *parm_vop) const
{
    UsdShadeInput mat_input = createAncestorInput( 
	    usd_material_path, usd_parent_path,
	    parm_vop, 0, nullptr );
    UsdShadeInput shader_input = shader.CreateInput( 
	    mat_input.GetBaseName(), mat_input.GetTypeName() );

    UsdShadeConnectableAPI::ConnectToSource( shader_input, mat_input );
}

void
husd_KarmaShaderTranslatorHelper::createAndConnectShaderWrapperPrimvarReader( 
	const UT_StringRef &usd_parent_path,
	UsdShadeShader &shader,
	VOP_ParmGenerator *parm_vop ) const
{
    UsdShadeOutput primvar_output = createPrimvarReader( 
	    usd_parent_path, *parm_vop );
    if( !primvar_output )
	return;

    TfToken   input_name( parm_vop->getParmNameCache().toStdString() );
    PRM_Parm *value_parm = parm_vop->getParmPtr( 
		parm_vop->getParameterDefaultValueParmName() );
    SdfValueTypeName input_type = 
	HUSDgetShaderOutputSdfTypeName( *parm_vop, 0, value_parm );

    UsdShadeInput shader_input = shader.CreateInput( input_name, input_type );
    UsdShadeConnectableAPI::ConnectToSource( shader_input, primvar_output );
}

void
husd_KarmaShaderTranslatorHelper::encodeShaderWrapperParms( 
	const UT_StringRef &usd_material_path,
	const UT_StringRef &usd_parent_path,
	UsdShadeShader &shader, 
	VOP_Node &vop, VOP_Type requested_shader_type ) const
{
    VOP_CodeGenerator *auto_gen = vop.getVopAutoCodeGenerator();
    if( !auto_gen )
	return;

    VOP_NodeList	parm_vops;
    auto_gen->getShaderParameterNodes( parm_vops, 
	    getEffectiveShaderType( vop, requested_shader_type ));

    for( auto && vop : parm_vops )
    {
	VOP_ParmGenerator *parm_vop = dynamic_cast<VOP_ParmGenerator *>( vop );
	if( !parm_vop )
	    continue;

	if( parm_vop->getOperator()->getName() == VOP_BIND_NAME )
	    createAndConnectShaderWrapperPrimvarReader( usd_material_path, 
		    shader, parm_vop );
	else
	    connectShaderWrapperInput( usd_material_path, usd_parent_path,
		    shader, parm_vop );
    }
}

void
husd_KarmaShaderTranslatorHelper::encodeEncapsulatedShaderParms( 
	UsdShadeShader &shader, VOP_Node &child_vop ) const
{
    // Some of the procedural shader (or cvex co-shader) parameters may be 
    // promoted to the parent, but the parameter names might not necessarily 
    // be the same.
    // Process them first, to know which ones need evaluation on proc child.
    UT_StringArray parm_map;
    child_vop.getFixedParameterMap( parm_map );

    UT_StringSet used_child_parms;    
    for( int i = 0; i < parm_map.entries(); i += 2 )
    {
	const UT_StringHolder &child_parm_name  = parm_map[ i ];
	const UT_StringHolder &parent_parm_name = parm_map[ i+1 ];

	const PRM_Parm *parent_parm = 
	    getShaderNode().getParmPtr( parent_parm_name );
	if( !parent_parm )
	    continue;

	const PRM_Template *tplate = nullptr;
	const PRM_Parm *child_parm = child_vop.getParmPtr( child_parm_name );
	if( child_parm )
	    tplate = child_parm->getTemplatePtr();

	// Skip parms that are at default value.
	if( getShaderNode().isParmAtDefaultValue( parent_parm_name, tplate ))
	    continue;

	used_child_parms.insert( child_parm_name );
	encodeShaderParm( shader, *child_parm, parent_parm );
    }
    
    // Get the parameters of the procedural (or cvex) shader. Add the ones that 
    // are set on child node itself and were not promoted to the parent.
    const PRM_Template *child_parms = child_vop.getShaderParmTemplates();
    for( int i = 0; child_parms[i].getType() != PRM_LIST_TERMINATOR; i++ )
    {
	UT_StringHolder	    parm_name( child_parms[i].getToken() );

	const PRM_Parm *parm = child_vop.getParmPtr( parm_name );
	if( !parm )
	    continue;

	// Skip parms that were already evaluated and added.
	if( used_child_parms.contains( parm_name ))
	    continue;

	// Skip parms that represent a shader; we will add them later
	// using getShaderInputMap().
	if( child_vop.isCVEXPathParm( parm_name ))
	    continue;

	// Skip parms that are at default value.
	if( child_vop.isParmAtDefaultValue( parm_name ))
	    continue;

	encodeShaderParm( shader, *parm );
    }
}

void
husd_KarmaShaderTranslatorHelper::addAndSetCoShaderInputs( 
	const UT_StringRef &usd_parent_path,
	UsdShadeShader &shader, 
	VOP_Node &vop, VOP_Type requested_shader_type ) const
{
    UT_StringArray shader_map;
    vop.getShaderInputMap( shader_map );

    for (int i = 0; i < shader_map.entries(); i += 2)
    {
	const UT_StringHolder &parm_name = shader_map[i];
	const UT_StringHolder &node_path = shader_map[i+1];

	VOP_Node *cvex_vop = vop.findVOPNode( node_path );
	if( !cvex_vop )
	    continue;

	auto cvex_shader = createUsdShaderPrim( usd_parent_path, 
		*cvex_vop, requested_shader_type, false );

	TfToken			output_name( "out" );
	TfToken			input_name( parm_name.toStdString() );
	SdfValueTypeName	type( SdfValueTypeNames->Token );
	husdConnectShaders( cvex_shader, output_name, shader, input_name, type);
    }
}

// ============================================================================ 
bool 
HUSD_KarmaShaderTranslator::matchesRenderMask(const UT_StringRef &render_mask) 
{
    return UT_StringWrap("VMantra").multiMatch( render_mask );
}

void
HUSD_KarmaShaderTranslator::createMaterialShader( HUSD_AutoWriteLock &lock,
	const UT_StringRef &usd_material_path,
	const HUSD_TimeCode &time_code,
	OP_Node &shader_node, VOP_Type shader_type,
	const UT_StringRef &output_name) 
{
    VOP_Node *shader_vop = CAST_VOPNODE( &shader_node );
    UT_ASSERT( shader_vop );

    husd_KarmaShaderTranslatorHelper  helper( lock, time_code, *shader_vop );
    helper.createMaterialShader( usd_material_path, usd_material_path,
	    shader_type, output_name );
}

UT_StringHolder
HUSD_KarmaShaderTranslator::createShader( HUSD_AutoWriteLock &lock,
	const UT_StringRef &usd_material_path,
	const UT_StringRef &usd_parent_path,
	const HUSD_TimeCode &time_code,
	OP_Node &shader_node, 
	const UT_StringRef &output_name) 
{
    VOP_Node *shader_vop = CAST_VOPNODE( &shader_node );
    UT_ASSERT( shader_vop );

    VOP_Type shader_type = shader_vop->getOutputType( 
	    shader_vop->getOutputFromName( output_name.c_str() ));

    husd_KarmaShaderTranslatorHelper  helper( lock, time_code, *shader_vop );
    return helper.createShader( usd_material_path, usd_parent_path, 
	    shader_type, output_name );
}

void
HUSD_KarmaShaderTranslator::updateShaderParameters( HUSD_AutoWriteLock &lock,
	const UT_StringRef &usd_shader_path,
	const HUSD_TimeCode &time_code,
	OP_Node &shader_node, const UT_StringArray &parameter_names )
{
    VOP_Node *shader_vop = CAST_VOPNODE( &shader_node );
    UT_ASSERT( shader_vop );

    husd_KarmaShaderTranslatorHelper  helper( lock, time_code, *shader_vop );
    return helper.updateShaderParameters( usd_shader_path,
	    parameter_names );
}

UT_StringHolder
HUSD_KarmaShaderTranslator::getRenderContextName( OP_Node &shader_node, 
	const UT_StringRef &output_name )
{
    return UT_StringHolder( theKarmaContextToken.GetString() );
}

