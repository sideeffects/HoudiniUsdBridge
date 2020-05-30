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

#include "HUSD_KarmaShaderTranslator.h"
#include "HUSD_TimeCode.h"

#include <VOP/VOP_Node.h>
#include <PY/PY_CompiledCode.h>
#include <PY/PY_EvaluationContext.h>
#include <PY/PY_Python.h>

#include <pxr/pxr.h>

#include <functional>

PXR_NAMESPACE_USING_DIRECTIVE

namespace
{

static constexpr auto  thePkgName	    = "husdshadertranslators";
static constexpr auto  theListerModuleName  = "modulelister";
static constexpr auto  theDefaultModuleName = "default";

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
    UT_ASSERT( !"Problem in python shader translator/generator API call." );
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

static inline bool
husdHasAPIFunction( const char *module_name, const char *api_function_name,
	const char *err_header, PY_EvaluationContext &py_ctx )
{
    UT_WorkBuffer   cmd;

    cmd.sprintf( "import %s\n", module_name );
    cmd.append(  "import inspect\n" );
    PYrunPythonStatementsAndExpectNoErrors( cmd.buffer(), err_header, &py_ctx );

    cmd.sprintf( "inspect.isfunction( getattr( %s, '%s', None ))",
	    module_name, api_function_name );
    auto result = PYrunPythonExpressionAndExpectNoErrors( cmd.buffer(),
	    PY_Result::INT, err_header, &py_ctx );
    if( result.myResultType != PY_Result::INT )
	return false;

    return (bool) result.myIntValue;
}


static inline UT_StringArray
husdGetListedModules( const char *err_header, PY_EvaluationContext &py_ctx )
{
    // The multi-directory package importing does not seem to work with 
    // __import('thePkgName')__ expression, but it does with the import
    // statement, so we load that module with the statement.
    // Especially that we also import the inspect module for testing.
    UT_WorkBuffer   cmd;
    cmd.sprintf( "import %s.%s\n", thePkgName, theListerModuleName );
    PYrunPythonStatementsAndExpectNoErrors( cmd.buffer(), err_header, &py_ctx );

    // Construct an expression that will yield the array of module names.
    cmd.sprintf( "%s.%s.translatorModulesNames()", 
	    thePkgName, theListerModuleName);
    auto result = PYrunPythonExpressionAndExpectNoErrors( cmd.buffer(),
	    PY_Result::STRING_ARRAY, err_header, &py_ctx );
    if( result.myResultType != PY_Result::STRING_ARRAY )
	return UT_StringArray();

    return result.myStringArray;
}

static inline void
husdGetListedFullModules( 
	UT_StringArray &module_names, UT_StringHolder &default_module_name,
	const char *api_function_name, const char *err_subject)
{
    UT_WorkBuffer err_header;
    err_header.sprintf( "Error while loading %s", err_subject );

    PY_EvaluationContext py_ctx;
    UT_StringArray listed_names = husdGetListedModules( 
	    err_header.buffer(), py_ctx );

    UT_WorkBuffer full_module_name;
    for( auto &&name : listed_names )
    {
	full_module_name.sprintf( "%s.%s", thePkgName, name.c_str() );
	err_header.sprintf( "Error while verifying %s API on %s", 
		err_subject, full_module_name.buffer() );

	if( !husdHasAPIFunction( full_module_name.buffer(), api_function_name,
		    err_header.buffer(), py_ctx ))
	    continue;

	if( name == theDefaultModuleName )
	    default_module_name = full_module_name;
	else
	    module_names.append( full_module_name );
    }
}

static inline void
husdInitPythonContext( const UT_StringRef &module, PY_EvaluationContext &py_ctx)
{
    UT_WorkBuffer cmd;

    cmd.appendSprintf( "import %s\n", module.c_str() );
    cmd.append( "import pxr.Usd\n" );
    cmd.append( "from pxr import UsdShade\n" );

    static const char *const theErrHeader =
	"Error while setting up python context for a USD shader translator";
    husdRunPython( cmd.buffer(), theErrHeader, py_ctx );
}

static inline bool
husdMatchesRenderMask( const UT_StringRef &render_mask, 
	const UT_StringRef &module, const UT_StringRef &api_function_name, 
	PY_EvaluationContext &py_ctx )
{
    UT_WorkBuffer cmd;
    cmd.sprintf( "return %s.%s().matchesRenderMask('%s')\n",
	    module.c_str(), api_function_name.c_str(), render_mask.c_str() );

    PY_CompiledCode py_code( cmd.buffer(), PY_CompiledCode::EXPRESSION, 
	    NULL /*as_file*/, true /*allow_function_bodies*/ );

    PY_Result result;
    py_code.evaluateInContext( PY_Result::INT, py_ctx, result );
    if( result.myResultType != PY_Result::INT )
    {
	husdDisplayPythonTraceback( result, "matchesRenderMask()", "int" );
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
husdAppendTimeCodeArg( UT_WorkBuffer &cmd, const HUSD_TimeCode &time_code )
{
    if( time_code.isDefault() )
    {
	cmd.append( 
	    "kwargs['timecode'] = pxr.Usd.TimeCode.Default()\n" );
    }
    else
    {
	cmd.appendSprintf( 
	    "kwargs['timecode'] = pxr.Usd.TimeCode(" SYS_DBL_DIG_FMT ")\n", 
	    time_code.frame() );
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
		husd_PyShaderTranslator( const char *module );

    bool matchesRenderMask( 
	    const UT_StringRef &render_mask ) override;

    void createMaterialShader( HUSD_AutoWriteLock &lock,
	    const UT_StringRef &usd_material_path,
	    const HUSD_TimeCode &time_code,
	    OP_Node &shader_node, VOP_Type shader_type,
	    const UT_StringRef &output_name) override;

    UT_StringHolder createShader( HUSD_AutoWriteLock &lock,
	    const UT_StringRef &usd_material_path,
	    const UT_StringRef &usd_parent_path,
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

    /// Returns the names of the python modules that implement shader encoding.
    static void	getShaderTranslatorModules( UT_StringArray &module_names,
	    UT_StringHolder &default_module_name );

private:
    /// The name of the python module that implements this shader translator.
    UT_StringHolder	    myModule;
    
    /// The evaluation context for this translator.
    PY_EvaluationContext    myPythonContext;
};

static constexpr auto  theShaderTranslatorAPI =	"usdShaderTranslator";

husd_PyShaderTranslator::husd_PyShaderTranslator( const char *module )
    : myModule( module )
{ 
    husdInitPythonContext( myModule, myPythonContext ); 
}

void
husd_PyShaderTranslator::getShaderTranslatorModules( 
	UT_StringArray &module_names, UT_StringHolder &default_module_name )
{
    husdGetListedFullModules( module_names, default_module_name,
	theShaderTranslatorAPI, "shader translator" );
}

bool
husd_PyShaderTranslator::matchesRenderMask( const UT_StringRef &render_mask ) 
{
    return husdMatchesRenderMask( render_mask, 
	    myModule, theShaderTranslatorAPI, myPythonContext );
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

    cmd.appendSprintf( 
	    "%s.%s().createMaterialShader( %s, %s, %s, %s, %s, %s )\n",
	    myModule.c_str(), theShaderTranslatorAPI,
	    stage_arg, mat_arg, time_arg, node_arg, type_arg, output_arg );
	
    static const char *const theErrHeader = "Error while encoding USD shader";
    husdRunPython( cmd.buffer(), theErrHeader, myPythonContext );
}

UT_StringHolder
husd_PyShaderTranslator::createShader( HUSD_AutoWriteLock &lock,
	const UT_StringRef &usd_material_path,
	const UT_StringRef &usd_parent_path,
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
    auto time_arg   = husdAppendTimeCodeArg( cmd, time_code );
    auto node_arg   = husdAppendShaderNodeArg( cmd, shader_node );
    auto output_arg = husdAppendShaderOutputArg( cmd, output_name );

    cmd.appendSprintf( 
	    "return %s.%s().createShader( %s, %s, %s, %s, %s, %s )\n",
	    myModule.c_str(), theShaderTranslatorAPI,
	    stage_arg, mat_arg, parent_arg, time_arg, node_arg, output_arg );

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

    cmd.appendSprintf( 
	    "%s.%s().updateShaderParameters( %s, %s, %s, %s, %s )\n",
	    myModule.c_str(), theShaderTranslatorAPI,
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

    cmd.appendSprintf( "return %s.%s().renderContextName( %s, %s )\n",
	    myModule.c_str(), theShaderTranslatorAPI,
	    node_arg, output_arg );

    return husdRunPythonAndReturnString( cmd.buffer(), "renderContextName()",
	    myPythonContext );
}

void
husd_PyShaderTranslator::setID( int id ) 
{
    HUSD_ShaderTranslator::setID( id );

    UT_WorkBuffer cmd;
    cmd.sprintf( "%s.%s().setTranslatorID(%d)\n",
	    myModule.c_str(), theShaderTranslatorAPI, id );

    static const char *const theErrHeader = 
	"Error while seting translator ID";
    husdRunPython( cmd.buffer(), theErrHeader, myPythonContext );
}


// ============================================================================ 
// Wrapper class for Python-based preview shader generators.
class husd_PyPreviewShaderGenerator : public HUSD_PreviewShaderGenerator
{
public:
		husd_PyPreviewShaderGenerator ( const char *module );

    bool matchesRenderMask( 
	    const UT_StringRef &render_mask ) override;

    void createMaterialPreviewShader( HUSD_AutoWriteLock &lock,
	    const UT_StringRef &usd_material_path,
	    const HUSD_TimeCode &time_code,
	    OP_Node &shader_node, const UT_StringRef &output_name) override;

    void updateMaterialPreviewShaderParameters( HUSD_AutoWriteLock &lock,
	    const UT_StringRef &usd_shader_path,
	    const HUSD_TimeCode &time_code,
	    OP_Node &shader_node,
            const UT_StringArray &parameter_names ) override;

    /// Returns the names of the python modules that implement shader encoding.
    static void	getPreviewShaderGeneratorModules( UT_StringArray &module_names,
	    UT_StringHolder &default_module_name );

private:
    /// The name of the python module that implements this shader translator.
    UT_StringHolder	    myModule;
    
    /// The evaluation context for this translator.
    PY_EvaluationContext    myPythonContext;
};

static constexpr auto thePreviewShaderGeneratorAPI ="usdPreviewShaderGenerator";

husd_PyPreviewShaderGenerator::husd_PyPreviewShaderGenerator(const char *module)
    : myModule( module )
{
    husdInitPythonContext( myModule, myPythonContext ); 
}

void
husd_PyPreviewShaderGenerator::getPreviewShaderGeneratorModules( 
	UT_StringArray &module_names, UT_StringHolder &default_module_name )
{
    husdGetListedFullModules( module_names, default_module_name,
	thePreviewShaderGeneratorAPI , "preview shader generator" );
}

bool
husd_PyPreviewShaderGenerator::matchesRenderMask( 
	const UT_StringRef &render_mask ) 
{
    return husdMatchesRenderMask( render_mask, 
	    myModule, thePreviewShaderGeneratorAPI , myPythonContext );
}

void
husd_PyPreviewShaderGenerator::createMaterialPreviewShader( 
	HUSD_AutoWriteLock &lock,
	const UT_StringRef &usd_material_path,
	const HUSD_TimeCode &time_code,
	OP_Node &shader_node, const UT_StringRef &output_name ) 
{
    // Note, using a single kwargs variable to not polute the python 
    // exec context with many local variables.
    UT_WorkBuffer cmd;
    husdAppendClearArgs( cmd );
    auto stage_arg  = husdAppendStageArg( cmd );
    auto mat_arg    = husdAppendMaterialArg( cmd, usd_material_path );
    auto time_arg   = husdAppendTimeCodeArg( cmd, time_code );
    auto node_arg   = husdAppendShaderNodeArg( cmd, shader_node );
    auto output_arg = husdAppendShaderOutputArg( cmd, output_name );

    cmd.appendSprintf( 
	    "%s.%s().createMaterialPreviewShader( %s, %s, %s, %s, %s )\n",
	    myModule.c_str(), thePreviewShaderGeneratorAPI,
	    stage_arg, mat_arg, time_arg, node_arg, output_arg );
	
    static const char *const theErrHeader = 
	"Error while generating a USD Preview Surface shader";
    husdRunPython( cmd.buffer(), theErrHeader, myPythonContext );
}

void
husd_PyPreviewShaderGenerator::updateMaterialPreviewShaderParameters( 
	HUSD_AutoWriteLock &lock,
	const UT_StringRef &usd_shader_path,
	const HUSD_TimeCode &time_code,
	OP_Node &shader_node, const UT_StringArray &parameter_names ) 
{
    // Note, using a single kwargs variable to not polute the python 
    // exec context with many local variables.
    UT_WorkBuffer cmd;
    husdAppendClearArgs( cmd );
    auto stage_arg  = husdAppendStageArg( cmd );
    auto shader_arg = husdAppendShaderArg( cmd, usd_shader_path );
    auto time_arg   = husdAppendTimeCodeArg( cmd, time_code );
    auto node_arg   = husdAppendShaderNodeArg( cmd, shader_node );
    auto parms_arg  = husdAppendParmNamesArg( cmd, parameter_names );

    cmd.appendSprintf( "%s.%s()."
            "updateMaterialPreviewShaderParameters( %s, %s, %s, %s, %s )\n",
	    myModule.c_str(), thePreviewShaderGeneratorAPI,
	    stage_arg, shader_arg, time_arg, node_arg, parms_arg );
	
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
    void	registerPyTranslator( const UT_StringRef &module_name );
    void	registerPyGenerator(  const UT_StringRef &module_name );
    void	registerTranslators();
    void	registerGenerators();

    static void	clearRegistryCallback(void *data);
    void	clearRegistry();

private:
    HUSD_ShaderTranslatorRegistry	myRegistry;
    HUSD_KarmaShaderTranslator		myKarmaTranslator;
    UT_Array< UT_UniquePtr< husd_PyShaderTranslator >>		myPyTranslators;
    UT_Array< UT_UniquePtr< husd_PyPreviewShaderGenerator >>	myPyGenerators;
};


husd_RegistryHolder::husd_RegistryHolder()
{
    registerTranslators();
    registerGenerators();

    // Register a callback to clean up the registry at exit time.
    // Note that registry cleanup involves cleaning up Python objects
    // so we want the callback to run at Python exit time.
    std::function<void(void)> clear_registry_func =
	std::bind(&husd_RegistryHolder::clearRegistryCallback, this);
    PYregisterAtExitCallback(clear_registry_func);
}

void
husd_RegistryHolder::registerPyTranslator(const UT_StringRef &module_name)
{
    auto &translator = myPyTranslators[
	myPyTranslators.append( 
		UTmakeUnique<husd_PyShaderTranslator>( module_name )) ];

    myRegistry.registerShaderTranslator( *translator );
}

void
husd_RegistryHolder::registerPyGenerator(const UT_StringRef &module_name)
{
    auto &generator = myPyGenerators[
	myPyGenerators.append( 
		UTmakeUnique<husd_PyPreviewShaderGenerator>(
		    module_name )) ];

    myRegistry.registerPreviewShaderGenerator( *generator );
}

void
husd_RegistryHolder::registerTranslators()
{
    UT_StringArray  modules;
    UT_StringHolder default_module;
    husd_PyShaderTranslator::getShaderTranslatorModules( 
	    modules, default_module );

    // First, register a default translator on which we always can fall back.
    // NOTE, when searching for translators, we will iterate backwards, so 
    // the default translator will always be checked last.
    UT_ASSERT( default_module.isstring() );
    if( default_module.isstring() )
	registerPyTranslator( default_module );

    // Next, register Karma translator.
    myRegistry.registerShaderTranslator( myKarmaTranslator );

    // Register Python translator last, so they take precedence over C++ ones
    // above, and so it's easier for users to override them.
    for( auto &&name : modules )
	registerPyTranslator( name );
}

void
husd_RegistryHolder::registerGenerators()
{
    UT_StringArray  modules;
    UT_StringHolder default_module;
    husd_PyPreviewShaderGenerator::getPreviewShaderGeneratorModules( 
	    modules, default_module );

    // First, register the default generator on which we always can fall back.
    // NOTE, when searching for generators, we will iterate backwards, so 
    // the default generators will always be checked last.
    UT_ASSERT( default_module.isstring() );
    if( default_module.isstring() )
	registerPyGenerator( default_module );

    // Register Python generators.
    for( auto &&name : modules )
	registerPyGenerator( name );
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

    // The destroy the generators and translators.
    myPyGenerators.clear();
    myPyTranslators.clear();
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
    static UT_StringHolder theKarmaRenderMask("VMantra");
    if( vop_node && !vop_node->translatesDirectlyToUSD() )
	return theKarmaRenderMask;

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

} // end namespace

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
HUSD_ShaderTranslatorRegistry::registerPreviewShaderGenerator(
	HUSD_PreviewShaderGenerator &generator )
{
    if( myGenerators.find( &generator ) < 0 )
	myGenerators.append( &generator );
}

void
HUSD_ShaderTranslatorRegistry::unregisterPreviewShaderGenerator(
	HUSD_PreviewShaderGenerator &generator )
{
    myGenerators.findAndRemove( &generator );
}

HUSD_PreviewShaderGenerator *
HUSD_ShaderTranslatorRegistry::findPreviewShaderGenerator( 
	const OP_Node &node ) const
{
    exint idx = husdFindRegistrant( myGenerators, node );
    if( idx >= 0 )
	return myGenerators[idx];

    return nullptr;
}

void
HUSD_ShaderTranslatorRegistry::clear()
{
    myTranslators.clear();
    myGenerators.clear();
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

