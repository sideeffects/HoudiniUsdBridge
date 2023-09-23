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

#include "HUSD_ShaderTranslator.h"

#include "HUSD_VexShaderTranslator.h"
#include "HUSD_TimeCode.h"

#include <VOP/VOP_Node.h>
#include <PY/PY_CompiledCode.h>
#include <PY/PY_Python.h>
#include <UT/UT_Digits.h>
#include <UT/UT_Function.h>

#include <pxr/pxr.h>

#include <functional>

PXR_NAMESPACE_USING_DIRECTIVE

// ============================================================================ 
void
HUSD_ShaderTranslator::beginMaterialTranslation( HUSD_AutoWriteLock &lock,
        const UT_StringRef &usd_material_path )
{
}

void
HUSD_ShaderTranslator::endMaterialTranslation( HUSD_AutoWriteLock &lock,
        const UT_StringRef &usd_material_path )
{
}

// ============================================================================ 
namespace
{

// We will refer to python translator objects in the manager using an int index.
using husd_PyTranslatorHandle = int;
static constexpr auto  theInvalidPyTranslatorHandle = -1;

static inline bool
husdIsValidHandle( const husd_PyTranslatorHandle &handle )
{
    return handle >= 0;
}

static inline void
husdGetPyShaderTranslatorHandles( husd_PyTranslatorHandle &default_translator,
	UT_Array<husd_PyTranslatorHandle> &non_default_translators,
	const char *manager_var_name, const char *api_function_name, 
	const char *default_class_name, 
	const char *err_header, PY_EvaluationContext &py_ctx )
{
    UT_WorkBuffer   cmd;
    PY_Result	    result;

    // Start with empty translator lists.
    default_translator = theInvalidPyTranslatorHandle;
    non_default_translators.clear();

    // Create the translators manager object in python.
    cmd.sprintf(
	    "%s = husd.pluginmanager.ShaderTranslatorManager('%s')",
	    manager_var_name, api_function_name );
    PYrunPythonStatementsAndExpectNoErrors( cmd.buffer(), err_header, &py_ctx );

    // Construct an expression that will yield the array of indices for
    // non-default translators.
    cmd.sprintf( "%s.translatorIndexOfClass('%s')", 
	    manager_var_name, default_class_name  );
    result = PYrunPythonExpressionAndExpectNoErrors( cmd.buffer(),
	    PY_Result::INT, err_header, &py_ctx );
    if( result.myResultType == PY_Result::INT )
    {
	default_translator = result.myIntValue;
    }

    // Construct an expression that will yield the array of indices for
    // non-default translators.
    cmd.sprintf( "%s.translatorIndicesOfNonClass('%s')", 
	    manager_var_name, default_class_name   );
    result = PYrunPythonExpressionAndExpectNoErrors( cmd.buffer(),
	    PY_Result::INT_ARRAY, err_header, &py_ctx );
    if( result.myResultType == PY_Result::INT_ARRAY )
    {
	for( auto && val : result.myIntArray )
	    non_default_translators.append( val );
    }
}

static inline void
husdDisplayPythonTraceback( const PY_Result &result,
	const char *function_name, const char *return_type )
{
    UT_WorkBuffer detailed_buff;
    detailed_buff.sprintf( "The %s expression should return a %s",
	    function_name, return_type );

    const char *detailed_error = detailed_buff.buffer();
    if (result.myResultType == PY_Result::ERR)
	detailed_error = result.myDetailedErrValue.buffer();

    UT_WorkBuffer heading_buff;
    heading_buff.sprintf( "Error while evaluating %s expression",
	    function_name );

    PYdisplayPythonTraceback( heading_buff.buffer(), detailed_error );
    UT_ASSERT( !"Problem in python shader translator API call." );
}

static inline void
husdRunPython( const UT_StringRef &cmd, const UT_StringRef &err_header,
	PY_EvaluationContext &py_ctx )
{
    PYrunPythonStatementsAndExpectNoErrors( cmd.buffer(), err_header, &py_ctx );
}

static inline UT_StringHolder
husdRunPythonAndReturnString( const UT_StringRef &cmd, 
	const UT_StringRef &function_name, PY_EvaluationContext &py_ctx )
{
    PY_CompiledCode py_code( cmd.buffer(), PY_CompiledCode::EXPRESSION, 
	    NULL /*as_file*/, true /*allow_function_bodies*/ );

    PY_Result result;
    py_code.evaluateInContext( PY_Result::STRING, py_ctx, result );
    if( result.myResultType != PY_Result::STRING )
    {
	husdDisplayPythonTraceback( result, function_name, "string" );
	return UT_StringHolder();
    }

    return UT_StringHolder( result.myStringValue );
}

static inline void
husdInitPythonContext( PY_EvaluationContext &py_ctx )
{
    UT_WorkBuffer cmd;

    cmd.append( 
	    "import husd.pluginmanager\n"
	    "import pxr.Usd\n");

    static const char *const theErrHeader =
	"Error while setting up python context for a USD shader translator";
    husdRunPython( cmd.buffer(), theErrHeader, py_ctx );
}

static inline void
husdAppendTranslatorObj( UT_WorkBuffer &cmd, const char *manager_var_name, 
	const husd_PyTranslatorHandle &h )
{
    cmd.appendSprintf( "%s.translator(%d)", manager_var_name, (int) h );
}

static inline bool
husdMatchesRenderAspect( const UT_StringRef &aspect_name,
	const UT_StringRef& aspect_test_fn_name,
	const char *manager_var_name, const husd_PyTranslatorHandle &handle,
	PY_EvaluationContext &py_ctx )
{
    UT_WorkBuffer cmd;
    cmd.append( "return " );
    husdAppendTranslatorObj( cmd, manager_var_name, handle );
    cmd.appendSprintf( ".%s('%s')\n",
	    aspect_test_fn_name.c_str(), aspect_name.c_str() );

    PY_CompiledCode py_code( cmd.buffer(), PY_CompiledCode::EXPRESSION, 
	    NULL /*as_file*/, true /*allow_function_bodies*/ );

    PY_Result result;
    py_code.evaluateInContext( PY_Result::INT, py_ctx, result );
    if( result.myResultType != PY_Result::INT )
    {
	husdDisplayPythonTraceback( result, aspect_test_fn_name, "int" );
	return false;
    }

    return (bool) result.myIntValue;
}

static inline const char *
husdHomShaderType( VOP_Type shader_type )
{
    if( shader_type <= VOP_SHADER_START || shader_type >= VOP_SHADER_END )
	return "Invalid";

    // NB, based on shaderType enum in HOM_EnumModules.h
    switch( shader_type )
    {
	case VOP_TYPE_UNDEF:		return "Invalid";
	case VOP_SURFACE_SHADER:	return "Surface";
	case VOP_SURFACE_SHADOW_SHADER: return "SurfaceShadow";
	case VOP_DISPLACEMENT_SHADER:	return "Displacement";
	case VOP_GEOMETRY_SHADER:	return "Geometry";
	case VOP_INTERIOR_SHADER:	return "Interior";
	case VOP_LIGHT_SHADER:		return "Light";
	case VOP_LIGHT_SHADOW_SHADER:	return "LightShadow";
	case VOP_LIGHT_FILTER_SHADER:	return "LightFilter";
	case VOP_ATMOSPHERE_SHADER:	return "Atmosphere";
	case VOP_LENS_SHADER:		return "Lens";
	case VOP_OUTPUT_SHADER:		return "Output";
	case VOP_BACKGROUND_SHADER:	return "Background";
	case VOP_PHOTON_SHADER:		return "Photon";
	case VOP_IMAGE3D_SHADER:	return "Image3D";
	case VOP_CVEX_SHADER:		return "CVex";
	case VOP_COSHADER_SHADER:	return "CoShader";
	case VOP_COSHADER_ARRAY:	return "CoShaderArray";
	case VOP_MUTABLE_SHADER:	return "Mutable";
	case VOP_PROPERTIES_SHADER:	return "Properties";
	case VOP_MATERIAL_SHADER:	return "Material";
	case VOP_VOP_MATERIAL_SHADER:	return "VopMaterial";
	case VOP_SHADER_CLASS_SHADER:	return "ShaderClass";
	case VOP_STRUCT_DEF_SHADER:	return "StructDef";
	case VOP_INTEGRATOR_SHADER:	return "Integrator";
	case VOP_GENERIC_SHADER:	return "Generic";
	case VOP_BSDF_SHADER:		return "BSDF";
	default:			break;
    }

    UT_ASSERT( !"Shader type not handled" );
    return "Invalid";
}

static inline void
husdAppendClearArgs( UT_WorkBuffer &cmd )
{
    cmd.append( "kwargs = {}\n" );
}

static inline const char *
husdAppendStageArg( UT_WorkBuffer &cmd )
{
    cmd.append( "kwargs['stage'] = hou.pwd().editableStage()\n" );
    return "kwargs['stage']";
}

static inline const char *
husdAppendMaterialArg( UT_WorkBuffer &cmd, const UT_StringRef &path )
{
    cmd.appendSprintf( "kwargs['materialpath'] = '%s'\n", path.c_str() );
    return "kwargs['materialpath']";
}

static inline const char *
husdAppendShaderArg( UT_WorkBuffer &cmd, const UT_StringRef &path )
{
    cmd.appendSprintf( "kwargs['shaderpath'] = '%s'\n", path.c_str() );
    return "kwargs['shaderpath']";
}

static inline const char *
husdAppendParentPathArg( UT_WorkBuffer &cmd, const UT_StringRef &path )
{
    cmd.appendSprintf( "kwargs['parentpath'] = '%s'\n", path.c_str() );
    return "kwargs['parentpath']";
}

static inline const char *
husdAppendPrimNameArg( UT_WorkBuffer &cmd, const UT_StringRef &name )
{
    cmd.appendSprintf( "kwargs['primname'] = '%s'\n", name.c_str() );
    return "kwargs['primname']";
}

static inline const char *
husdAppendTimeCodeArg( UT_WorkBuffer &cmd, const HUSD_TimeCode &time_code )
{
    if( time_code.isDefault() )
    {
	cmd.append( 
	    "kwargs['timecode'] = pxr.Usd.TimeCode.Default()\n" );
    }
    else
    {
	cmd.append(
	    "kwargs['timecode'] = pxr.Usd.TimeCode(" );
        cmd.append(UT_Digits(time_code.frame()));
        cmd.append(")\n");
    }

    return "kwargs['timecode']";
}

static inline const char *
husdAppendShaderNodeArg( UT_WorkBuffer &cmd, OP_Node &shader_node ) 
{
    UT_String path;
    shader_node.getFullPath( path );

    cmd.appendSprintf( "kwargs['shadernode'] = hou.node('%s')\n", path.c_str());
    return "kwargs['shadernode']";
}

static inline const char *
husdAppendParmNamesArg(  UT_WorkBuffer &cmd, const UT_StringArray &names )
{
    bool first = true;

    cmd.append( "kwargs['parmnames'] = [" );
    for( auto &&name : names )
    {
        if( !first )
            cmd.append( ", " );
	first = false;

        cmd.append( "'" );
        cmd.append( name );
        cmd.append( "'" );
    }
    cmd.append( "]\n" );

    return "kwargs['parmnames']";
}

static inline const char *
husdAppendShaderTypeArg( UT_WorkBuffer &cmd, VOP_Type shader_type )
{
    cmd.appendSprintf( "kwargs['shadertype'] = hou.shaderType.%s\n",
	    husdHomShaderType( shader_type ));
    return "kwargs['shadertype']";

}

static inline const char *
husdAppendShaderOutputArg( UT_WorkBuffer &cmd, const UT_StringRef &name ) 
{
    cmd.appendSprintf( "kwargs['outputname'] = '%s'\n", name.c_str() );
    return "kwargs['outputname']";
}


// ============================================================================ 
// Wrapper class for Python-based shader translators.
class husd_PyShaderTranslator : public HUSD_ShaderTranslator
{
public:
    // A shader translator that uses python shader translators to do the job.
    // NOTE: the object holds `py_ctx` as a reference.
    husd_PyShaderTranslator( const husd_PyTranslatorHandle &handle,
	    PY_EvaluationContext &py_ctx );

    bool matchesRenderMask( 
	    const UT_StringRef &render_mask ) override;

    void beginMaterialTranslation( HUSD_AutoWriteLock &lock,
                        const UT_StringRef &usd_material_path ) override;
    void endMaterialTranslation(HUSD_AutoWriteLock &lock,
                        const UT_StringRef &usd_material_path ) override;

    void createMaterialShader( HUSD_AutoWriteLock &lock,
	    const UT_StringRef &usd_material_path,
	    const HUSD_TimeCode &time_code,
	    OP_Node &shader_node, VOP_Type shader_type,
	    const UT_StringRef &output_name) override;

    UT_StringHolder createShader( HUSD_AutoWriteLock &lock,
	    const UT_StringRef &usd_material_path,
	    const UT_StringRef &usd_parent_path,
	    const UT_StringRef &usd_shader_name,
	    const HUSD_TimeCode &time_code,
	    OP_Node &shader_node, const UT_StringRef &output_name) override;

    void updateShaderParameters( HUSD_AutoWriteLock &lock,
	    const UT_StringRef &usd_shader_path,
	    const HUSD_TimeCode &time_code,
	    OP_Node &shader_node,
            const UT_StringArray &parameter_names ) override;

    UT_StringHolder getRenderContextName( OP_Node &shader_node, 
	    const UT_StringRef &output_name) override;

    void setID( int id ) override;

    /// Returns the handles of the python shader translators known to the
    /// python translator managing module.
    static void	getTranslatorHandles( 
	    husd_PyTranslatorHandle &default_translator,
	    UT_Array<husd_PyTranslatorHandle> &non_default_translators,
	    PY_EvaluationContext &py_ctx );

private:
    /// The handle (descriptor) for the python translator object maintained
    /// by the shader translator python manager (available in the python ctx).
    husd_PyTranslatorHandle myTranslatorHandle;
    
    /// The reference to the python evaluation context in which the 
    /// translator manager exists.
    PY_EvaluationContext    &myPythonContext;
};

// Symbol names used in the Python code.
static constexpr auto  theTranslatorsMgr = "theTranslators";
static constexpr auto  theShaderTranslatorAPI =	"usdShaderTranslator";
static constexpr auto  theDefaultTranslatorClass = "DefaultShaderTranslator";

husd_PyShaderTranslator::husd_PyShaderTranslator( 
	const husd_PyTranslatorHandle &handle, PY_EvaluationContext &py_ctx )
    : myTranslatorHandle( handle )
    , myPythonContext( py_ctx )
{ 
}

void
husd_PyShaderTranslator::getTranslatorHandles( 
	husd_PyTranslatorHandle &default_translator,
	UT_Array<husd_PyTranslatorHandle> &non_default_translators,
	PY_EvaluationContext &py_ctx )
{
    husdGetPyShaderTranslatorHandles(
	    default_translator, non_default_translators,
	    theTranslatorsMgr, theShaderTranslatorAPI, 
	    theDefaultTranslatorClass, "shader translator", py_ctx );
}

bool
husd_PyShaderTranslator::matchesRenderMask( const UT_StringRef &render_mask ) 
{
    return husdMatchesRenderAspect( render_mask, "matchesRenderMask",
	    theTranslatorsMgr, myTranslatorHandle, myPythonContext );
}

void
husd_PyShaderTranslator::beginMaterialTranslation( HUSD_AutoWriteLock &lock,
        const UT_StringRef &usd_material_path )
{
    // Note, using a single kwargs variable to not polute the python 
    // exec context with many local variables.
    UT_WorkBuffer cmd;
    husdAppendClearArgs( cmd );
    auto stage_arg  = husdAppendStageArg( cmd );
    auto mat_arg    = husdAppendMaterialArg( cmd, usd_material_path );

    husdAppendTranslatorObj( cmd, theTranslatorsMgr, myTranslatorHandle );
    cmd.appendSprintf( 
	    ".beginMaterialTranslation( %s, %s )\n", stage_arg, mat_arg );
	
    static auto theErrHeader = "Failed to begin the material translation";
    husdRunPython( cmd.buffer(), theErrHeader, myPythonContext );
}

void
husd_PyShaderTranslator::endMaterialTranslation(HUSD_AutoWriteLock &lock,
        const UT_StringRef &usd_material_path )
{
    // Note, using a single kwargs variable to not polute the python 
    // exec context with many local variables.
    UT_WorkBuffer cmd;
    husdAppendClearArgs( cmd );
    auto stage_arg  = husdAppendStageArg( cmd );
    auto mat_arg    = husdAppendMaterialArg( cmd, usd_material_path );

    husdAppendTranslatorObj( cmd, theTranslatorsMgr, myTranslatorHandle );
    cmd.appendSprintf( 
	    ".endMaterialTranslation( %s, %s )\n", stage_arg, mat_arg );
	
    static auto theErrHeader = "Failed to end the material translation";
    husdRunPython( cmd.buffer(), theErrHeader, myPythonContext );
}

void
husd_PyShaderTranslator::createMaterialShader( HUSD_AutoWriteLock &lock,
	const UT_StringRef &usd_material_path,
	const HUSD_TimeCode &time_code,
	OP_Node &shader_node, VOP_Type shader_type, 
	const UT_StringRef &output_name ) 
{
    // Note, using a single kwargs variable to not polute the python 
    // exec context with many local variables.
    UT_WorkBuffer cmd;
    husdAppendClearArgs( cmd );
    auto stage_arg  = husdAppendStageArg( cmd );
    auto mat_arg    = husdAppendMaterialArg( cmd, usd_material_path );
    auto time_arg   = husdAppendTimeCodeArg( cmd, time_code );
    auto node_arg   = husdAppendShaderNodeArg( cmd, shader_node );
    auto type_arg   = husdAppendShaderTypeArg( cmd, shader_type );
    auto output_arg = husdAppendShaderOutputArg( cmd, output_name );

    husdAppendTranslatorObj( cmd, theTranslatorsMgr, myTranslatorHandle );
    cmd.appendSprintf( 
	    ".createMaterialShader( %s, %s, %s, %s, %s, %s )\n",
	    stage_arg, mat_arg, time_arg, node_arg, type_arg, output_arg );
	
    static auto theErrHeader = "Error while translating a USD shader";
    husdRunPython( cmd.buffer(), theErrHeader, myPythonContext );
}

UT_StringHolder
husd_PyShaderTranslator::createShader( HUSD_AutoWriteLock &lock,
	const UT_StringRef &usd_material_path,
	const UT_StringRef &usd_parent_path,
	const UT_StringRef &usd_shader_name,
	const HUSD_TimeCode &time_code,
	OP_Node &shader_node, const UT_StringRef &output_name ) 
{
    // Note, using a single kwargs variable to not polute the python 
    // exec context with many local variables.
    UT_WorkBuffer cmd;
    husdAppendClearArgs( cmd );
    auto stage_arg  = husdAppendStageArg( cmd );
    auto mat_arg    = husdAppendMaterialArg( cmd, usd_material_path );
    auto parent_arg = husdAppendParentPathArg( cmd, usd_parent_path );
    auto name_arg   = husdAppendPrimNameArg( cmd, usd_shader_name );
    auto time_arg   = husdAppendTimeCodeArg( cmd, time_code );
    auto node_arg   = husdAppendShaderNodeArg( cmd, shader_node );
    auto output_arg = husdAppendShaderOutputArg( cmd, output_name );

    cmd.append( "return " );
    husdAppendTranslatorObj( cmd, theTranslatorsMgr, myTranslatorHandle );
    cmd.appendSprintf( 
	    ".createShader( %s, %s, %s, %s, %s, %s, %s )\n",
	    stage_arg, mat_arg, parent_arg, name_arg, time_arg, 
	    node_arg, output_arg );

    return husdRunPythonAndReturnString( cmd.buffer(), "createShader()",
	    myPythonContext );
}

void
husd_PyShaderTranslator::updateShaderParameters( HUSD_AutoWriteLock &lock,
	const UT_StringRef &usd_shader_path,
	const HUSD_TimeCode &time_code,
	OP_Node &shader_node, const UT_StringArray &parameter_names )
{
    // Note, using a single kwargs variable to not polute the python 
    // exec context with many local variables.
    UT_WorkBuffer cmd;
    husdAppendClearArgs( cmd );
    auto stage_arg  = husdAppendStageArg( cmd );
    auto shader_arg    = husdAppendShaderArg( cmd, usd_shader_path );
    auto time_arg   = husdAppendTimeCodeArg( cmd, time_code );
    auto node_arg   = husdAppendShaderNodeArg( cmd, shader_node );
    auto parms_arg  = husdAppendParmNamesArg( cmd, parameter_names );

    husdAppendTranslatorObj( cmd, theTranslatorsMgr, myTranslatorHandle );
    cmd.appendSprintf( 
	    ".updateShaderParameters( %s, %s, %s, %s, %s )\n",
	    stage_arg, shader_arg, time_arg, node_arg, parms_arg );
	
    static const char *const theErrHeader = 
	"Error while updating USD shader parameters";
    husdRunPython( cmd.buffer(), theErrHeader, myPythonContext );
}

UT_StringHolder
husd_PyShaderTranslator::getRenderContextName( OP_Node &shader_node, 
	const UT_StringRef &output_name ) 
{
    // Note, using a single kwargs variable to not polute the python 
    // exec context with many local variables.
    UT_WorkBuffer cmd;
    husdAppendClearArgs( cmd );
    auto node_arg   = husdAppendShaderNodeArg( cmd, shader_node );
    auto output_arg = husdAppendShaderOutputArg( cmd, output_name );

    cmd.append( "return " );
    husdAppendTranslatorObj( cmd, theTranslatorsMgr, myTranslatorHandle );
    cmd.appendSprintf( ".renderContextName( %s, %s )\n",
	    node_arg, output_arg );

    return husdRunPythonAndReturnString( cmd.buffer(), "renderContextName()",
	    myPythonContext );
}

void
husd_PyShaderTranslator::setID( int id ) 
{
    HUSD_ShaderTranslator::setID( id );

    UT_WorkBuffer cmd;
    husdAppendTranslatorObj( cmd, theTranslatorsMgr, myTranslatorHandle );
    cmd.appendSprintf(
	    ".setTranslatorID(%d)\n",
	    id );

    static const char *const theErrHeader = 
	"Error while seting translator ID";
    husdRunPython( cmd.buffer(), theErrHeader, myPythonContext );
}


// ============================================================================ 
// Wrapper class for Python-based preview shader translators.
class husd_PyPreviewShaderTranslator : public HUSD_PreviewShaderTranslator
{
public:
    // A shader translator that uses python shader translators to author
    // the generic surface preview shader.
    // NOTE: the object holds `py_ctx` as a reference.
    husd_PyPreviewShaderTranslator( const husd_PyTranslatorHandle &handle,
	    PY_EvaluationContext &py_ctx );

    bool matchesRenderContext( 
	    const UT_StringRef &render_context ) override;

    void createMaterialPreviewShader( HUSD_AutoWriteLock &lock,
	    const UT_StringRef &usd_material_path,
	    const UT_StringRef &usd_shader_path,
	    const HUSD_TimeCode &time_code ) override;

    void deleteMaterialPreviewShader( HUSD_AutoWriteLock &lock,
	    const UT_StringRef &usd_material_path ) override;

    void updateMaterialPreviewShaderParameters( HUSD_AutoWriteLock &lock,
	    const UT_StringRef &usd_main_shader_path, 
	    const HUSD_TimeCode &time_code ) override;

    /// Returns the names of the python modules that implement shader encoding.
    static void	getTranslatorHandles( 
	    husd_PyTranslatorHandle &default_translator,
	    UT_Array<husd_PyTranslatorHandle> &non_default_translators,
	    PY_EvaluationContext &py_ctx );

private:
    /// The handle (descriptor) for the python translator object maintained
    /// by the shader translator python manager (available in the python ctx).
    husd_PyTranslatorHandle myTranslatorHandle;
    
    /// The reference to the python evaluation context in which the 
    /// translator manager exists.
    PY_EvaluationContext    &myPythonContext;
};

// Symbol names used in the Python code.
static constexpr auto thePreviewTranslatorsMgr = "thePreviewTranslators";
static constexpr auto thePreviewTranslatorAPI 
			    = "usdPreviewShaderTranslator";
static constexpr auto theDefaultPreviewTranslatorClass 
			    = "DefaultPreviewShaderTranslator";

husd_PyPreviewShaderTranslator::husd_PyPreviewShaderTranslator(
	const husd_PyTranslatorHandle &handle, PY_EvaluationContext &py_ctx )
    : myTranslatorHandle( handle )
    , myPythonContext( py_ctx )
{
}

void
husd_PyPreviewShaderTranslator::getTranslatorHandles( 
	husd_PyTranslatorHandle &default_translator,
	UT_Array<husd_PyTranslatorHandle> &non_default_translators,
	PY_EvaluationContext &py_ctx )
{
    husdGetPyShaderTranslatorHandles(
	    default_translator, non_default_translators,
	    thePreviewTranslatorsMgr, thePreviewTranslatorAPI, 
	    theDefaultPreviewTranslatorClass, "preview shader translator", 
	    py_ctx );
}

bool
husd_PyPreviewShaderTranslator::matchesRenderContext( 
	const UT_StringRef &render_context ) 
{
    return husdMatchesRenderAspect( render_context, "matchesRenderContext",
	    thePreviewTranslatorsMgr, myTranslatorHandle, myPythonContext );
}

void
husd_PyPreviewShaderTranslator::createMaterialPreviewShader( 
	HUSD_AutoWriteLock &lock,
	const UT_StringRef &usd_material_path,
	const UT_StringRef &usd_shader_path,
	const HUSD_TimeCode &time_code )
{
    // Note, using a single kwargs variable to not polute the python 
    // exec context with many local variables.
    UT_WorkBuffer cmd;
    husdAppendClearArgs( cmd );
    auto stage_arg  = husdAppendStageArg( cmd );
    auto mat_arg    = husdAppendMaterialArg( cmd, usd_material_path );
    auto shader_arg = husdAppendShaderArg( cmd, usd_shader_path );
    auto time_arg   = husdAppendTimeCodeArg( cmd, time_code );

    husdAppendTranslatorObj( cmd, thePreviewTranslatorsMgr, myTranslatorHandle);
    cmd.appendSprintf( 
	    ".createMaterialPreviewShader( %s, %s, %s, %s )\n",
	    stage_arg, mat_arg, shader_arg, time_arg );
	
    static const char *const theErrHeader = 
	"Error while authoring a USD Preview Surface shader";
    husdRunPython( cmd.buffer(), theErrHeader, myPythonContext );
}

void 
husd_PyPreviewShaderTranslator::deleteMaterialPreviewShader( 
	HUSD_AutoWriteLock &lock,
	const UT_StringRef &usd_material_path )
{
    // Note, using a single kwargs variable to not polute the python 
    // exec context with many local variables.
    UT_WorkBuffer cmd;
    husdAppendClearArgs( cmd );
    auto stage_arg  = husdAppendStageArg( cmd );
    auto mat_arg    = husdAppendMaterialArg( cmd, usd_material_path );

    husdAppendTranslatorObj( cmd, thePreviewTranslatorsMgr, myTranslatorHandle);
    cmd.appendSprintf( 
	    ".deleteMaterialPreviewShader( %s, %s )\n",
	    stage_arg, mat_arg );
	
    static const char *const theErrHeader = 
	"Error while authoring a USD Preview Surface shader";
    husdRunPython( cmd.buffer(), theErrHeader, myPythonContext );
}

void
husd_PyPreviewShaderTranslator::updateMaterialPreviewShaderParameters( 
	HUSD_AutoWriteLock &lock,
	const UT_StringRef &usd_main_shader_path,
	const HUSD_TimeCode &time_code )
{
    // Note, using a single kwargs variable to not polute the python 
    // exec context with many local variables.
    UT_WorkBuffer cmd;
    husdAppendClearArgs( cmd );
    auto stage_arg  = husdAppendStageArg( cmd );
    auto shader_arg = husdAppendShaderArg( cmd, usd_main_shader_path );
    auto time_arg   = husdAppendTimeCodeArg( cmd, time_code );

    husdAppendTranslatorObj( cmd, thePreviewTranslatorsMgr, myTranslatorHandle);
    cmd.appendSprintf( 
            ".updateMaterialPreviewShaderParameters( %s, %s, %s )\n",
	    stage_arg, shader_arg, time_arg );
	
    static const char *const theErrHeader = 
	"Error while updating the USD Preview Surface shader";
    husdRunPython( cmd.buffer(), theErrHeader, myPythonContext );
}

// ============================================================================ 
/// Helper class that owns the standard shader translators and the registry.
class husd_RegistryHolder
{
public:
				husd_RegistryHolder();
    HUSD_ShaderTranslatorRegistry &getRegistry()
				{ return myRegistry; }

private:
    void	registerPyTranslator( 
			const husd_PyTranslatorHandle &handle );
    void	registerPyPreviewTranslator(
			const husd_PyTranslatorHandle &handle );
    void	registerTranslators();
    void	registerPreviewTranslators();

    static void	clearRegistryCallback(void *data);
    void	clearRegistry();

private:
    HUSD_ShaderTranslatorRegistry	myRegistry;
    HUSD_VexShaderTranslator		myVexTranslator;
    UT_Array< UT_UniquePtr< husd_PyShaderTranslator >>
					    myPyTranslators;
    UT_Array< UT_UniquePtr< husd_PyPreviewShaderTranslator >>
					    myPyPreviewTranslators;
    UT_UniquePtr< PY_EvaluationContext >    myPythonContextPtr;
};


husd_RegistryHolder::husd_RegistryHolder()
{
    // Python evaluation context can't be a direct member because this class
    // is used for a static variable, which will be destroyed at program exit,
    // which is after Python has finalized. So it would lead to crashes.
    // Instead, we use Python exit callback to delete the eval ctx object.
    // Note, translators hold reference to this eval context.
    myPythonContextPtr = UTmakeUnique<PY_EvaluationContext>();
    husdInitPythonContext( *myPythonContextPtr );
    registerTranslators();
    registerPreviewTranslators();

    // Register a callback to clean up the registry at exit time.
    // Note that registry cleanup involves cleaning up Python objects
    // so we want the callback to run at Python exit time.
    UT_Function<void(void)> clear_registry_func =
	std::bind(&husd_RegistryHolder::clearRegistryCallback, this);
    PYregisterAtExitCallback(clear_registry_func);
}

void
husd_RegistryHolder::registerPyTranslator(
	const husd_PyTranslatorHandle &handle )
{
    auto &translator = myPyTranslators[
	myPyTranslators.append( 
		UTmakeUnique<husd_PyShaderTranslator>( 
		    handle, *myPythonContextPtr )) ];

    myRegistry.registerShaderTranslator( *translator );
}

void
husd_RegistryHolder::registerPyPreviewTranslator(
	const husd_PyTranslatorHandle &handle )
{
    auto &translator = myPyPreviewTranslators[
	myPyPreviewTranslators.append( 
		UTmakeUnique<husd_PyPreviewShaderTranslator>(
		    handle, *myPythonContextPtr )) ];

    myRegistry.registerPreviewShaderTranslator( *translator );
}

void
husd_RegistryHolder::registerTranslators()
{
    husd_PyTranslatorHandle		default_translator;
    UT_Array<husd_PyTranslatorHandle>	non_default_translators;
    husd_PyShaderTranslator::getTranslatorHandles( 
	    default_translator, non_default_translators, *myPythonContextPtr );

    // First, register a default translator on which we always can fall back.
    // NOTE, when searching for translators, we will iterate backwards, so 
    // the default translator will always be checked last.
    UT_ASSERT( husdIsValidHandle( default_translator ));
    if( husdIsValidHandle( default_translator ))
	registerPyTranslator( default_translator );

    // Next, register Vex translator.
    myRegistry.registerShaderTranslator( myVexTranslator );

    // Register Python translator last, so they take precedence over C++ ones
    // above, and so it's easier for users to override them.
    for( auto &&handle : non_default_translators )
	registerPyTranslator( handle );
}

void
husd_RegistryHolder::registerPreviewTranslators()
{
    husd_PyTranslatorHandle		default_translator;
    UT_Array<husd_PyTranslatorHandle>	non_default_translators;
    husd_PyPreviewShaderTranslator::getTranslatorHandles( 
	    default_translator, non_default_translators, *myPythonContextPtr );

    // First, register the default preview translator on which we always can 
    // fall back.
    // NOTE, when searching for translators, we will iterate backwards, so 
    // the default translators will always be checked last.
    UT_ASSERT( husdIsValidHandle( default_translator ));
    if( husdIsValidHandle( default_translator ))
	registerPyPreviewTranslator( default_translator );

    // Register Python translators.
    for( auto &&handle : non_default_translators )
	registerPyPreviewTranslator( handle );
}

void
husd_RegistryHolder::clearRegistryCallback(void *data)
{
    husd_RegistryHolder *holder = static_cast<husd_RegistryHolder *>(data);

    holder->clearRegistry();
}

void
husd_RegistryHolder::clearRegistry()
{
    // Clear the registry first.
    myRegistry.clear();

    // Then destroy the translators, which have reference
    // to the python context.
    myPyPreviewTranslators.clear();
    myPyTranslators.clear();

    // Finally, destory the python evaluation context.
    myPythonContextPtr.reset();
}

inline UT_StringHolder
husdGetRenderMask(const OP_Node &node)
{
    const VOP_Node *vop_node = CAST_VOPNODE(&node);
    UT_ASSERT( vop_node );
    
    UT_StringHolder render_mask;
    if( vop_node )
	render_mask = vop_node->getRenderMask();
    if( render_mask.isstring() )
	return render_mask;

    // See if it is a code building-block VOP that need Mantra auto-wrapper.
    static constexpr UT_StringLit theVexRenderMask("VMantra");
    if( vop_node && !vop_node->translatesDirectlyToUSD() )
	return theVexRenderMask.asHolder();

    // Else use the default render mask, which will match default translator.
    static UT_StringHolder theDefaultRenderMask("default");
	return theDefaultRenderMask;
}

template <typename T>
inline exint
husdFindRegistrant( const UT_Array<T*> &registrants, const OP_Node &node )
{
    UT_StringHolder render_mask = husdGetRenderMask( node );

    // Backwards loop, since default registrant is at index 0 (and should
    // be tested last), and also so that newly registered registrants get
    // first try of matching the render mask.
    for( int i = registrants.size() - 1; i >= 0; i-- )
	if( registrants[i]->matchesRenderMask( render_mask ))
	    return i;

    return -1;
}

template <typename T>
inline exint
husdFindRegistrant( const UT_Array<T*> &registrants, 
	const UT_StringRef &render_context )
{
    // Backwards loop, since default registrant is at index 0 (and should
    // be tested last), and also so that newly registered registrants get
    // first try of matching the render mask.
    for( int i = registrants.size() - 1; i >= 0; i-- )
	if( registrants[i]->matchesRenderContext( render_context ))
	    return i;

    return -1;
}

} // end namespace

// ============================================================================ 
HUSD_ShaderTranslatorRegistry &
HUSD_ShaderTranslatorRegistry::get()
{
    static husd_RegistryHolder theRegistryHolder;
    return theRegistryHolder.getRegistry();
}

void
HUSD_ShaderTranslatorRegistry::registerShaderTranslator( 
	HUSD_ShaderTranslator &translator )
{
    if( myTranslators.find( &translator ) < 0 )
    {
	exint id = myTranslators.append( &translator );
	translator.setID( id );
    }
}

void
HUSD_ShaderTranslatorRegistry::unregisterShaderTranslator(
	HUSD_ShaderTranslator &translator)
{
    myTranslators.findAndRemove( &translator );
}

HUSD_ShaderTranslator *
HUSD_ShaderTranslatorRegistry::findShaderTranslator( const OP_Node &node ) const
{
    exint idx = husdFindRegistrant( myTranslators, node );
    if( idx >= 0 )
	return myTranslators[idx];

    return nullptr;
}

int
HUSD_ShaderTranslatorRegistry::findShaderTranslatorID(const OP_Node &node) const
{
    exint idx = husdFindRegistrant( myTranslators, node );
    if( idx >= 0 )
	// NOTE, we return ID rather than index, in case some translator got 
	// removed, which would shift indices/IDs given at registration.
	return myTranslators[idx]->getID();

    return -1;
}

void
HUSD_ShaderTranslatorRegistry::registerPreviewShaderTranslator(
	HUSD_PreviewShaderTranslator &translator )
{
    if( myPreviewTranslators.find( &translator ) < 0 )
	myPreviewTranslators.append( &translator );
}

void
HUSD_ShaderTranslatorRegistry::unregisterPreviewShaderTranslator(
	HUSD_PreviewShaderTranslator &translator )
{
    myPreviewTranslators.findAndRemove( &translator );
}

HUSD_PreviewShaderTranslator *
HUSD_ShaderTranslatorRegistry::findPreviewShaderTranslator( 
	const UT_StringRef &usd_render_context_name ) const
{
    exint idx = husdFindRegistrant( myPreviewTranslators, 
	    usd_render_context_name );
    if( idx >= 0 )
	return myPreviewTranslators[idx];

    return nullptr;
}

void
HUSD_ShaderTranslatorRegistry::clear()
{
    myTranslators.clear();
    myPreviewTranslators.clear();
}

void
HUSD_ShaderTranslatorRegistry::reportShaderTranslation( const OP_Node &node, 
	const UT_StringRef &usd_shader_path )
{
    for( auto &&records : myTranslations )
	records.append( TranslationRecord(node.getUniqueId(), usd_shader_path));
}

void
HUSD_ShaderTranslatorRegistry::addTranslationObserver( const OP_Node &node )
{
    // There should not be too many observers. In fact there should be just 1.
    exint idx = myTranslationObservers.find( node.getUniqueId() );
    if( idx >= 0 )
    {
	myTranslations[ idx ].clear();
    }
    else
    {
	myTranslationObservers.append( node.getUniqueId() );
	myTranslations.append();
    }
}

HUSD_ShaderTranslatorRegistry::TranslationRecords 
HUSD_ShaderTranslatorRegistry::removeTranslationObserver( const OP_Node &node )
{
    TranslationRecords result;

    exint idx = myTranslationObservers.find( node.getUniqueId() );
    if( idx >= 0 )
    {
	result = std::move( myTranslations[ idx ] );
	myTranslations.removeIndex( idx );
	myTranslationObservers.removeIndex( idx );
    }

    return result;
}

