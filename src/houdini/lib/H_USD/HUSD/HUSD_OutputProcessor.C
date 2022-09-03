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

using husd_PyProcessorHandle = int;

static inline void
husdGetPyOutputProcessorHandlesAndNames( 
	UT_Array<husd_PyProcessorHandle> &processor_handles,
	UT_StringArray &processor_names,
	const char *manager_var_name, const char *api_function_name, 
	const char *err_header, PY_EvaluationContext &py_ctx )
{
    UT_WorkBuffer   cmd;

    // Start with empty lists.
    processor_handles.clear();
    processor_names.clear();

    // Create the processors manager object in python.
    cmd.sprintf(
	    "%s = husd.pluginmanager.PluginManager('outputprocessors', '%s',"
	    "	    include_shadowed=True, reverse=True)",
	    manager_var_name, api_function_name );
    PYrunPythonStatementsAndExpectNoErrors( cmd.buffer(), err_header, &py_ctx );

    // Construct an expression that will yield the number of known processors.
    cmd.sprintf( "%s.pluginCount()", manager_var_name );
    PY_Result py_count = PYrunPythonExpressionAndExpectNoErrors( cmd.buffer(),
	    PY_Result::INT, err_header, &py_ctx );
    if( py_count.myResultType != PY_Result::INT )
	return;

    // Build the array of processor names.
    for( int i = 0; i < py_count.myIntValue; i++ )
    {
	cmd.sprintf( "%s.plugin(%d).name()", manager_var_name, i );
	
	PY_Result py_name = PYrunPythonExpressionAndExpectNoErrors( 
		cmd.buffer(), PY_Result::STRING, err_header, &py_ctx );
	UT_ASSERT( py_name.myResultType == PY_Result::STRING );
	if( py_name.myResultType == PY_Result::STRING )
	{
	    processor_handles.append( i );
	    processor_names.append( py_name.myStringValue );
	}
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

static inline void
husdInitPythonContext( PY_EvaluationContext &py_ctx )
{
    UT_WorkBuffer cmd;

    cmd.append( "import husd.pluginmanager\n" );

    static const char *const theErrHeader =
	"Error while setting up python context for a USD shader translator";
    husdRunPython( cmd.buffer(), theErrHeader, py_ctx );
}

static inline void
husdAppendProcessorObj( UT_WorkBuffer &cmd, const char *manager_var_name, 
	const husd_PyProcessorHandle &h )
{
    cmd.appendSprintf( "%s.plugin(%d)", manager_var_name, (int) h );
}

// ============================================================================ 
// HUSD_OutputProcessor subclass for Python-based output processors.
//
class husd_PyOutputProcessor : public HUSD_OutputProcessor
{
public:
    // NOTE: the object holds `py_ctx` as a reference.
		         husd_PyOutputProcessor(const husd_PyProcessorHandle &h,
				 const UT_StringRef &name,
				 PY_EvaluationContext &py_ctx );
                        ~husd_PyOutputProcessor() override;

    // Returns the names of the known output processors.
    static void          getOutputProcessorHandlesAndNames(
				UT_Array<husd_PyProcessorHandle> &handles,
                                UT_StringArray &processor_names,
				PY_EvaluationContext &py_ctx );

    void                 beginSave(OP_Node *config_node,
                                   const UT_Options &parms,
                                   fpreal t) override;
    void                 endSave() override;

    bool                 processAsset(const UT_StringRef &asset_path,
                                const UT_StringRef &asset_path_for_save,
                                const UT_StringRef &referencign_layer_path,
                                bool asset_is_layer,
                                bool for_save,
                                UT_String &newpath,
                                UT_String &error) override;

    bool                         hidden() const override;
    const UT_StringHolder       &displayName() const override;
    const PI_EditScriptedParms  *parameters() const override;

    // Check for the required API entry points.
    bool                 isValid() const;

private:
    // The handle (index) of the python processor object in the manager.
    husd_PyProcessorHandle		 myProcessorHandle;
    // Cache the hidden flag returned from the python implementation.
    bool                                 myHidden;
    // Cache the display name returned from the python implementation.
    UT_StringHolder	                 myDisplayName;
    // The parameters used to configure this output processor.
    UT_UniquePtr<PI_EditScriptedParms>   myParms;
    // The reference to the evaluation context for this processor.
    PY_EvaluationContext                &myPythonContext;
};

// Symbol names used in the Python code.
static constexpr auto  theTranslatorsMgr = "theProcessors";
static constexpr auto  theOutputProcessorAPI = "usdOutputProcessor";

husd_PyOutputProcessor::husd_PyOutputProcessor(
	const husd_PyProcessorHandle &handle,
	const UT_StringRef &name,
	PY_EvaluationContext &py_ctx )
    : myProcessorHandle( handle )
    , myPythonContext( py_ctx )
{ 
    UT_WorkBuffer        cmd;
    UT_StringHolder      ds;

    cmd.sprintf("%s.plugin(%d).hidden()",
	theTranslatorsMgr, (int) myProcessorHandle);
    myHidden = husdRunPythonAndReturnBool(
        cmd.buffer(), "hidden()", false, myPythonContext);

    cmd.sprintf("%s.plugin(%d).displayName()",
	theTranslatorsMgr, (int) myProcessorHandle);
    myDisplayName = husdRunPythonAndReturnString(
        cmd.buffer(), "displayName()", myPythonContext);

    if (!myDisplayName.isstring())
        myDisplayName = name;

    cmd.sprintf("%s.plugin(%d).parameters()",
	theTranslatorsMgr, (int) myProcessorHandle);
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
husd_PyOutputProcessor::getOutputProcessorHandlesAndNames(
	UT_Array<husd_PyProcessorHandle> &handles,
	UT_StringArray &names,
	PY_EvaluationContext &py_ctx)
{
    husdGetPyOutputProcessorHandlesAndNames( handles, names,
	    theTranslatorsMgr, theOutputProcessorAPI, 
	     "output processor", py_ctx );
}

void
husd_PyOutputProcessor::beginSave(OP_Node *config_node,
                                  const UT_Options &config_overrides,
                                  fpreal t)
{
    UT_WorkBuffer        cmd, overridesDict;

    config_overrides.appendPyDictionary(overridesDict);
    if (config_node)
        cmd.sprintf("%s.plugin(%d).beginSave(hou.node('%s'), %s, %g)",
            theTranslatorsMgr, (int) myProcessorHandle,
            config_node->getFullPath().c_str(), overridesDict.buffer(), t);
    else
        cmd.sprintf("%s.plugin(%d).beginSave(None, %s, %g)",
            theTranslatorsMgr, (int) myProcessorHandle,
            overridesDict.buffer(), t);
    husdRunPython(cmd.buffer(), "beginSave()", myPythonContext);
}

void
husd_PyOutputProcessor::endSave()
{
    UT_WorkBuffer        cmd;

    cmd.sprintf("%s.plugin(%d).endSave()",
	theTranslatorsMgr, (int) myProcessorHandle);
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

    cmd.sprintf("%s.plugin(%d).processAsset('%s', '%s', '%s', %s, %s)",
	theTranslatorsMgr, (int) myProcessorHandle,
        asset_path.c_str(),
        asset_path_for_save.c_str(),
        referencing_layer_path.c_str(),
        asset_is_layer ? "True" : "False",
        for_save ? "True" : "False");
    // In case any paths have backslashes, convert them all to forward
    // slashes. We don't want every output processor to have to worry
    // about platform-specific slashes.
    cmd.substitute("\\", "/");
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

    // Register Python processors last, so they take precedence over C++ ones
    // above, and so it's easier for users to override them.
    UT_StringArray names;
    UT_Array<husd_PyProcessorHandle> handles;
    husd_PyOutputProcessor::getOutputProcessorHandlesAndNames(handles, names, 
	    *myPythonContextPtr);
    UT_ASSERT( handles.size() == names.size() );
    for( int i = 0; i < names.size(); i++ )
    {
	auto &name = names[i];

        UT_SharedPtr<husd_PyOutputProcessor> processor;
        processor.reset(new husd_PyOutputProcessor(handles[i], name, 
		    *myPythonContextPtr));

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
    // Python processors in the registry hold reference to the python
    // context, so delete them first, and then delete the python context.
    myRegistry.clear();
    myPythonContextPtr.reset();
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

