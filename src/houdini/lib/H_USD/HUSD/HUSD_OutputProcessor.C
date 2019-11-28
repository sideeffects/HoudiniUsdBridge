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

#include "HUSD_OutputProcessor.h"
#include <PI/PI_EditScriptedParms.h>
#include <PY/PY_CompiledCode.h>
#include <PY/PY_EvaluationContext.h>
#include <PY/PY_Python.h>
#include <UT/UT_Exit.h>
#include <pxr/pxr.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace
{

static constexpr auto  thePkgName	     = "husdoutputprocessors";
static constexpr auto  theListerModuleName   = "modulelister";
static constexpr auto  theOutputProcessorAPI = "usdOutputProcessor";

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
    UT_ASSERT( !"Problem in python output processor API call." );
}

static inline void
husdRunPython(const UT_StringRef &cmd, const
        UT_StringRef &err_header,
	PY_EvaluationContext &py_ctx)
{
    PYrunPythonStatementsAndExpectNoErrors(cmd.buffer(), err_header, &py_ctx);
}

static inline bool
husdRunPythonAndReturnBool(const UT_StringRef &cmd, 
	const UT_StringRef &function_name,
        bool default_value,
        PY_EvaluationContext &py_ctx)
{
    PY_CompiledCode py_code( cmd.buffer(), PY_CompiledCode::EXPRESSION, 
	    NULL /*as_file*/, true /*allow_function_bodies*/ );

    PY_Result result;
    py_code.evaluateInContext( PY_Result::INT, py_ctx, result );
    if( result.myResultType != PY_Result::INT )
    {
	husdDisplayPythonTraceback( result, function_name, "integer" );
	return default_value;
    }

    return (result.myIntValue != 0);
}

static inline UT_StringHolder
husdRunPythonAndReturnString(const UT_StringRef &cmd, 
	const UT_StringRef &function_name,
        PY_EvaluationContext &py_ctx)
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
    cmd.sprintf( "%s.%s.processorModulesNames()", 
	    thePkgName, theListerModuleName);
    auto result = PYrunPythonExpressionAndExpectNoErrors( cmd.buffer(),
	    PY_Result::STRING_ARRAY, err_header, &py_ctx );
    if( result.myResultType != PY_Result::STRING_ARRAY )
	return UT_StringArray();

    return result.myStringArray;
}

static inline void
husdGetListedFullModules(UT_StringArray &module_names,
	const char *api_function_name,
        const char *err_subject)
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

        module_names.append( full_module_name );
    }
}

// ============================================================================ 
// HUSD_OutputProcessor subclass for Python-based output processors.
//
class husd_PyOutputProcessor : public HUSD_OutputProcessor
{
public:
		         husd_PyOutputProcessor(const char *modulename);
    virtual             ~husd_PyOutputProcessor();

    // Returns the names of the python modules that implement shader encoding.
    static void          getOutputProcessorModules(
                                UT_StringArray &module_names);

    virtual void         beginSave(OP_Node *config_node, fpreal t) override;
    virtual void         endSave() override;

    virtual bool         processAsset(const UT_StringRef &asset_path,
                                const UT_StringRef &asset_path_for_save,
                                const UT_StringRef &referencign_layer_path,
                                bool asset_is_layer,
                                bool for_save,
                                UT_String &newpath,
                                UT_String &error) override;

    virtual bool                         hidden() const override;
    virtual const UT_StringHolder       &displayName() const override;
    virtual const PI_EditScriptedParms  *parameters() const override;

    // Check for the required API entry points.
    bool                 isValid() const;

private:
    // The name of the python module that implements this shader processor.
    UT_StringHolder	                 myModuleName;
    // Cache the hidden flag returned from the python implementation.
    bool                                 myHidden;
    // Cache the display name returned from the python implementation.
    UT_StringHolder	                 myDisplayName;
    // The parameters used to configure this output processor.
    UT_UniquePtr<PI_EditScriptedParms>   myParms;
    // The evaluation context for this processor.
    PY_EvaluationContext                 myPythonContext;
};

husd_PyOutputProcessor::husd_PyOutputProcessor(const char *modulename)
    : myModuleName(modulename)
{ 
    static const char *const theErrHeader =
	"Error while setting up python context for an output processor";
    UT_WorkBuffer        cmd;
    UT_StringHolder      ds;

    cmd.sprintf("import %s\n", myModuleName.c_str());

    husdRunPython(cmd.buffer(), theErrHeader, myPythonContext);

    cmd.sprintf("%s.%s().hidden()",
        myModuleName.c_str(), theOutputProcessorAPI);
    myHidden = husdRunPythonAndReturnBool(
        cmd.buffer(), "hidden()", false, myPythonContext);

    cmd.sprintf("%s.%s().displayName()",
        myModuleName.c_str(), theOutputProcessorAPI);
    myDisplayName = husdRunPythonAndReturnString(
        cmd.buffer(), "displayName()", myPythonContext);

    if (!myDisplayName.isstring())
        myDisplayName = myModuleName;

    cmd.sprintf("%s.%s().parameters()",
        myModuleName.c_str(), theOutputProcessorAPI);
    ds = husdRunPythonAndReturnString(
        cmd.buffer(), "parameters()", myPythonContext);

    if (ds.isstring())
    {
        UT_IStream   dsstream(ds.c_str(), ds.length(), UT_ISTREAM_ASCII);
        myParms.reset(new PI_EditScriptedParms(nullptr, dsstream,
            false, false, false, false));
    }
}

husd_PyOutputProcessor::~husd_PyOutputProcessor()
{
}

void
husd_PyOutputProcessor::getOutputProcessorModules(UT_StringArray &module_names)
{
    husdGetListedFullModules(module_names,
	theOutputProcessorAPI, "output processor");
}

void
husd_PyOutputProcessor::beginSave(OP_Node *config_node, fpreal t)
{
    UT_WorkBuffer        cmd;

    cmd.sprintf("%s.%s().beginSave(hou.node('%s'), %g)",
        myModuleName.c_str(), theOutputProcessorAPI,
        config_node->getFullPath().c_str(), t);
    husdRunPython(cmd.buffer(), "beginSave()", myPythonContext);
}

void
husd_PyOutputProcessor::endSave()
{
    UT_WorkBuffer        cmd;

    cmd.sprintf("%s.%s().endSave()",
        myModuleName.c_str(), theOutputProcessorAPI);
    husdRunPython(cmd.buffer(), "endSave()", myPythonContext);
}

bool
husd_PyOutputProcessor::processAsset(const UT_StringRef &asset_path,
        const UT_StringRef &asset_path_for_save,
        const UT_StringRef &referencing_layer_path,
        bool asset_is_layer,
        bool for_save,
        UT_String &newpath,
        UT_String &error)
{
    UT_WorkBuffer        cmd;

    cmd.sprintf("%s.%s().processAsset('%s', '%s', '%s', %s, %s)",
        myModuleName.c_str(), theOutputProcessorAPI,
        asset_path.c_str(),
        asset_path_for_save.c_str(),
        referencing_layer_path.c_str(),
        asset_is_layer ? "True" : "False",
        for_save ? "True" : "False");
    newpath = husdRunPythonAndReturnString(
        cmd.buffer(), "processAsset()", myPythonContext);

    return true;
}

bool
husd_PyOutputProcessor::hidden() const
{
    return myHidden;
}

const UT_StringHolder &
husd_PyOutputProcessor::displayName() const
{
    return myDisplayName;
}

const PI_EditScriptedParms *
husd_PyOutputProcessor::parameters() const
{
    return myParms.get();
}

bool
husd_PyOutputProcessor::isValid() const
{
    return true;
}

// ============================================================================ 
// Helper class that owns the standard output processors and the registry.
//
class husd_RegistryHolder
{
public:
				husd_RegistryHolder();

    HUSD_OutputProcessorRegistry &getRegistry()
				{ return myRegistry; }

private:
    static void                 clearRegistryCallback(void *data);
    void                        clearRegistry();

    HUSD_OutputProcessorRegistry myRegistry;
};

husd_RegistryHolder::husd_RegistryHolder()
{
    UT_StringArray  modules;

    // Register Python processors last, so they take precedence over C++ ones
    // above, and so it's easier for users to override them.
    husd_PyOutputProcessor::getOutputProcessorModules(modules);
    for( auto &&name : modules )
    {
        UT_SharedPtr<husd_PyOutputProcessor> processor;

        processor.reset(new husd_PyOutputProcessor(name));
        if (processor->isValid())
        {
            UT_StringHolder      basename;
            int                  lastdot = name.lastCharIndex('.');

            // It shouldn't be possible for the module name to end with a ".",
            // but protect against it just in case.
            if (lastdot >= 0 && lastdot < (name.length()-1))
                basename = name.c_str() + name.lastCharIndex('.') + 1;
            else
                basename = name;
            myRegistry.registerOutputProcessor(basename, processor);
        }
    }

    // Register a callback to clean up the registry at exit time.
    // Note that registry cleanup can involve executing Python code
    // so we want the callback to run at Python exit time.
    std::function<void(void)> clear_registry_func = 
	std::bind(&husd_RegistryHolder::clearRegistryCallback, this); 
    PYregisterAtExitCallback(clear_registry_func);
}

void
husd_RegistryHolder::clearRegistryCallback(void *data)
{
    husd_RegistryHolder *holder = (husd_RegistryHolder *)data;

    holder->clearRegistry();
}

void
husd_RegistryHolder::clearRegistry()
{
    myRegistry.clear();
}

} // end namespace

HUSD_OutputProcessorRegistry &
HUSD_OutputProcessorRegistry::get()
{
    static husd_RegistryHolder theRegistryHolder;

    return theRegistryHolder.getRegistry();
}

UT_StringArray
HUSD_OutputProcessorRegistry::processorNames() const
{
    UT_StringArray   names;

    for (auto it = myProcessors.begin(); it != myProcessors.end(); ++it)
    {
        // Don't return the names of hidden processors.
        if (!it->second->hidden())
            names.append(it->first);
    }

    return names;
}

HUSD_OutputProcessorPtr
HUSD_OutputProcessorRegistry::processor(const UT_StringRef &name) const
{
    auto it = myProcessors.find(name);

    if (it != myProcessors.end())
        return it->second;

    return HUSD_OutputProcessorPtr();
}

void
HUSD_OutputProcessorRegistry::registerOutputProcessor(
        const UT_StringHolder &name,
        const HUSD_OutputProcessorPtr &processor)
{
    myProcessors[name] = processor;
}

void
HUSD_OutputProcessorRegistry::unregisterOutputProcessor(
        const UT_StringRef &name)
{
    myProcessors.erase(name);
}

void
HUSD_OutputProcessorRegistry::clear()
{
    myProcessors.clear();
}

HUSD_OutputProcessorPtr
HUSDgetOutputProcessor(const UT_StringRef &name)
{
    return HUSD_OutputProcessorRegistry::get().processor(name);
}

