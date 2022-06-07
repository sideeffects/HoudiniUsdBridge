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
	    "	    include_shadowed = True)",
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
husdRunPython(const UT_StringRef &cmd,
        const UT_StringRef &err_header,
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

    cmd.append("import husd.pluginmanager\n");
    cmd.append("from pxr import Sdf\n");

    static const char *const theErrHeader =
	"Error while setting up python context for a USD shader translator";
    husdRunPython( cmd.buffer(), theErrHeader, py_ctx );
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

    // Check for the required API entry points.
    bool                 isValid() const;

    void                 beginSave(OP_Node *config_node,
                                const UT_Options &parms,
                                OP_Node *lop_node,
                                fpreal t) override;

    bool                 processSavePath(const UT_StringRef &asset_path,
                                const UT_StringRef &referencing_layer_path,
                                bool asset_is_layer,
                                UT_String &newpath,
                                UT_String &error) override;

    bool                 processReferencePath(const UT_StringRef &asset_path,
                                const UT_StringRef &referencing_layer_path,
                                bool asset_is_layer,
                                UT_String &newpath,
                                UT_String &error) override;

    bool                 processLayer(const UT_StringRef &identifier,
                                UT_String &error) override;

    bool                         hidden() const override;
    UT_StringHolder              displayName() const override;
    const PI_EditScriptedParms  *parameters() const override;

private:
    void                 createPythonObject();

    // The handle (index) of the python processor object in the manager.
    husd_PyProcessorHandle		         myProcessorHandle;
    // The unique name for this python object.
    UT_StringHolder                              myPythonObjectName;
    // The reference to the evaluation context for this processor.
    PY_EvaluationContext                        &myPythonContext;
    // The parameters used to configure this output processor.
    mutable UT_UniquePtr<PI_EditScriptedParms>   myParms;
};

// Symbol names used in the Python code.
static constexpr auto  theProcessorsMgr = "theProcessors";
static constexpr auto  theOutputProcessorAPI = "usdOutputProcessor";

husd_PyOutputProcessor::husd_PyOutputProcessor(
	const husd_PyProcessorHandle &handle,
	const UT_StringRef &name,
	PY_EvaluationContext &py_ctx )
    : myProcessorHandle( handle )
    , myPythonContext( py_ctx )
{
}

husd_PyOutputProcessor::~husd_PyOutputProcessor()
{
    if (myPythonObjectName.isstring())
    {
        UT_WorkBuffer        cmd;

        cmd.sprintf("del %s", myPythonObjectName.c_str());
        husdRunPython(cmd.buffer(), "delete", myPythonContext);
    }
}

void
husd_PyOutputProcessor::getOutputProcessorHandlesAndNames(
	UT_Array<husd_PyProcessorHandle> &handles,
	UT_StringArray &names,
	PY_EvaluationContext &py_ctx)
{
    husdGetPyOutputProcessorHandlesAndNames( handles, names,
        theProcessorsMgr, theOutputProcessorAPI,
	     "output processor", py_ctx );
}

bool
husd_PyOutputProcessor::isValid() const
{
    return true;
}

void
husd_PyOutputProcessor::createPythonObject()
{
    if (!myPythonObjectName.isstring())
    {
        UT_WorkBuffer        cmd;
        static int           theUniqueObjectId = 0;

        myPythonObjectName.format("output_processor_{}", theUniqueObjectId++);
        cmd.sprintf("%s = %s.plugin(%d)()", myPythonObjectName.c_str(),
            theProcessorsMgr, (int)myProcessorHandle);
        husdRunPython(cmd.buffer(), "create", myPythonContext);
    }
}

void
husd_PyOutputProcessor::beginSave(OP_Node *config_node,
        const UT_Options &config_overrides,
        OP_Node *lop_node,
        fpreal t)
{
    UT_WorkBuffer        cmd, config_node_buf, lop_node_buf, overridesDict;

    createPythonObject();
    config_overrides.appendPyDictionary(overridesDict);
    if (config_node)
        config_node_buf.format("hou.node('{}')", config_node->getFullPath());
    else
        config_node_buf.strcpy("None");
    if (lop_node)
        lop_node_buf.format("hou.node('{}')", lop_node->getFullPath());
    else
        lop_node_buf.strcpy("None");
    cmd.format("{}.beginSave({}, {}, {}, {})",
        myPythonObjectName.c_str(),
        config_node_buf.buffer(),
        overridesDict.buffer(),
        lop_node_buf.buffer(),
        t);
    husdRunPython(cmd.buffer(), "beginSave()", myPythonContext);
}

bool
husd_PyOutputProcessor::processSavePath(const UT_StringRef &asset_path,
        const UT_StringRef &referencing_layer_path,
        bool asset_is_layer,
        UT_String &newpath,
        UT_String &error)
{
    UT_WorkBuffer        cmd;

    createPythonObject();
    cmd.sprintf("%s.processSavePath('%s', '%s', %s)",
        myPythonObjectName.c_str(),
        asset_path.c_str(),
        referencing_layer_path.c_str(),
        asset_is_layer ? "True" : "False");
    // In case any paths have backslashes, convert them all to forward
    // slashes. We don't want every output processor to have to worry
    // about platform-specific slashes.
    cmd.substitute("\\", "/");
    newpath = husdRunPythonAndReturnString(
        cmd.buffer(), "processSavePath()", myPythonContext);

    return true;
}

bool
husd_PyOutputProcessor::processReferencePath(const UT_StringRef &asset_path,
        const UT_StringRef &referencing_layer_path,
        bool asset_is_layer,
        UT_String &newpath,
        UT_String &error)
{
    UT_WorkBuffer        cmd;

    createPythonObject();
    cmd.sprintf("%s.processReferencePath('%s', '%s', %s)",
        myPythonObjectName.c_str(),
        asset_path.c_str(),
        referencing_layer_path.c_str(),
        asset_is_layer ? "True" : "False");
    // In case any paths have backslashes, convert them all to forward
    // slashes. We don't want every output processor to have to worry
    // about platform-specific slashes.
    cmd.substitute("\\", "/");
    newpath = husdRunPythonAndReturnString(
        cmd.buffer(), "processReferencePath()", myPythonContext);

    return true;
}

bool
husd_PyOutputProcessor::processLayer(const UT_StringRef &identifier,
        UT_String &error)
{
    UT_WorkBuffer        cmd;

    createPythonObject();
    cmd.sprintf("%s.processLayer(Sdf.Layer.Find('%s'))",
        myPythonObjectName.c_str(),
        identifier.c_str());

    return husdRunPythonAndReturnBool(
        cmd.buffer(), "processLayer()", false, myPythonContext);
}

bool
husd_PyOutputProcessor::hidden() const
{
    UT_WorkBuffer        cmd;

    cmd.sprintf("%s.plugin(%d).hidden()",
        theProcessorsMgr, (int)myProcessorHandle);

    return husdRunPythonAndReturnBool(
        cmd.buffer(), "hidden()", false, myPythonContext);
}

UT_StringHolder
husd_PyOutputProcessor::displayName() const
{
    UT_WorkBuffer        cmd;
    UT_StringHolder      display_name;

    cmd.sprintf("%s.plugin(%d).displayName()",
        theProcessorsMgr, (int)myProcessorHandle);
    display_name = husdRunPythonAndReturnString(
        cmd.buffer(), "displayName()", myPythonContext);
    if (!display_name.isstring())
    {
        cmd.sprintf("%s.plugin(%d).displayName()",
            theProcessorsMgr, (int)myProcessorHandle);
        display_name = husdRunPythonAndReturnString(
            cmd.buffer(), "name()", myPythonContext);
    }

    return display_name;
}

const PI_EditScriptedParms *
husd_PyOutputProcessor::parameters() const
{
    if (!myParms)
    {
        UT_WorkBuffer cmd;
        UT_StringHolder ds;

        cmd.sprintf("%s.plugin(%d).parameters()",
            theProcessorsMgr, (int)myProcessorHandle);
        ds = husdRunPythonAndReturnString(
            cmd.buffer(), "parameters()", myPythonContext);

        if (ds.isstring())
        {
            UT_IStream dsstream(ds.c_str(), ds.length(), UT_ISTREAM_ASCII);
            myParms.reset(new PI_EditScriptedParms(
                nullptr, dsstream, false, false, false, false));
        }
    }

    return myParms.get();
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

    HUSD_OutputProcessorRegistry         myRegistry;
    UT_UniquePtr<PY_EvaluationContext>   myPythonContextPtr;
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
    husd_PyOutputProcessor::getOutputProcessorHandlesAndNames(
        handles, names, *myPythonContextPtr);

    UT_ASSERT( handles.size() == names.size() );
    for( int i = 0; i < names.size(); i++ )
    {
	auto &name = names[i];

        UT_StringHolder      basename;
        int                  lastdot = name.lastCharIndex('.');

        // It shouldn't be possible for the module name to end with a ".",
        // but protect against it just in case.
        if (lastdot >= 0 && lastdot < (name.length()-1))
            basename = name.c_str() + name.lastCharIndex('.') + 1;
        else
            basename = name;

        husd_PyProcessorHandle handle = handles[i];
        const UT_UniquePtr<PY_EvaluationContext> &context = myPythonContextPtr;
        myRegistry.registerOutputProcessor(basename, [handle, name, &context]() {
            UT_SharedPtr<husd_PyOutputProcessor> processor;
            processor.reset(new husd_PyOutputProcessor(handle, name, *context));
            return processor;
        });
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

    for (auto it = myProcessorFactories.begin();
              it != myProcessorFactories.end(); ++it)
    {
        // Don't return the names of hidden processors. But we have to create
        // the processor to find out if it should be hidden.
        if (!it->second()->hidden())
            names.append(it->first);
    }

    return names;
}

HUSD_OutputProcessorPtr
HUSD_OutputProcessorRegistry::createProcessor(const UT_StringRef &name) const
{
    auto it = myProcessorFactories.find(name);

    if (it != myProcessorFactories.end())
        return it->second();

    return HUSD_OutputProcessorPtr();
}

void
HUSD_OutputProcessorRegistry::registerOutputProcessor(
        const UT_StringHolder &name,
        const HUSD_OutputProcessorFactory &factory)
{
    myProcessorFactories[name] = factory;
}

void
HUSD_OutputProcessorRegistry::unregisterOutputProcessor(
        const UT_StringRef &name)
{
    myProcessorFactories.erase(name);
}

void
HUSD_OutputProcessorRegistry::clear()
{
    myProcessorFactories.clear();
}

HUSD_OutputProcessorPtr
HUSDcreateOutputProcessor(const UT_StringRef &name)
{
    return HUSD_OutputProcessorRegistry::get().createProcessor(name);
}

