/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
 *
 * Produced by:
 *	Side Effects Software Inc.
 *	123 Front Street West, Suite 1401
 *	Toronto, Ontario
 *      Canada   M5J 2M2
 *	416-504-9876
 *
 */

#ifndef __HUSD_OutputProcessor_h__
#define __HUSD_OutputProcessor_h__

#include "HUSD_API.h"
#include <UT/UT_StringArray.h>
#include <UT/UT_StringMap.h>
#include <UT/UT_SharedPtr.h>

class UT_String;
class OP_Node;
class PI_EditScriptedParms;

// ============================================================================ 
/// Performs processing on a USD output path during a save operation.
///
class HUSD_API HUSD_OutputProcessor
{
public:
    /// Standard virtual destructor for this abstract base class.
    virtual             ~HUSD_OutputProcessor() = default;

    virtual void         beginSave(OP_Node *config_node, fpreal t) = 0;
    virtual void         endSave() = 0;

    virtual bool         processAsset(const UT_StringRef &asset_path,
                                const UT_StringRef &asset_path_for_save,
                                const UT_StringRef &referencing_layer_path,
                                bool asset_is_layer,
                                bool for_save,
                                UT_String &newpath,
                                UT_String &error) = 0;

    virtual const UT_StringHolder       &displayName() const = 0;
    virtual const PI_EditScriptedParms  *parameters() const = 0;

    virtual bool         hidden() const
                         { return false; }
};
typedef UT_SharedPtr<HUSD_OutputProcessor> HUSD_OutputProcessorPtr;
typedef UT_Array<HUSD_OutputProcessorPtr> HUSD_OutputProcessorArray;

// ============================================================================ 
/// Keeps a list of known processors that can translate a USD output path
/// during a save operation.
///
class HUSD_API HUSD_OutputProcessorRegistry
{
public:
    /// Returns a singelton instance.
    static HUSD_OutputProcessorRegistry &get();

    /// Returns a list of the names of all available processors.
    UT_StringArray           processorNames() const;

    /// Returns the processor that matches the supplied name.
    HUSD_OutputProcessorPtr  processor(const UT_StringRef &name) const;

    /// Adds the processor to the list of known processors.
    void                     registerOutputProcessor(
                                    const UT_StringHolder &name,
                                    const HUSD_OutputProcessorPtr &processor);

    /// Removes the processor from the list of known processors.
    void                     unregisterOutputProcessor(
                                    const UT_StringRef &name);

    /// Removes all processors from the registry. Should only be called on
    /// shutdown of the process.
    void                     clear();

private:
    /// Map of known output processors, keyed by their internal names.
    UT_StringMap<HUSD_OutputProcessorPtr>	    myProcessors;
};

HUSD_API HUSD_OutputProcessorPtr
HUSDgetOutputProcessor(const UT_StringRef &name);

#endif
