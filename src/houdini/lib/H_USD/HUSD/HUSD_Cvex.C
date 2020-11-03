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
 */


#include "HUSD_Cvex.h"
#include "XUSD_AttributeUtils.h"
#include "HUSD_Bucket.h"
#include "HUSD_CvexBindingMap.h"
#include "HUSD_CvexCode.h"
#include "HUSD_CvexDataCommand.h"
#include "HUSD_CvexDataInputs.h"
#include "HUSD_ErrorScope.h"
#include "HUSD_FindPrims.h"
#include "HUSD_PathSet.h"
#include "XUSD_Data.h"
#include "XUSD_FindPrimsTask.h"
#include "XUSD_PathSet.h"
#include "XUSD_Utils.h"
#include <VOP/VOP_Node.h>
#include <VOP/VOP_Snippet.h>
#include <VCC/VCC_Utils.h>
#include <CVEX/CVEX_Context.h>
#include <CVEX/CVEX_Data.h>
#include <UT/UT_BitArray.h>
#include <UT/UT_Debug.h>
#include <UT/UT_IStream.h>
#include <UT/UT_UniquePtr.h>
#include <UT/UT_WorkArgs.h>
#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/usd/modelAPI.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/primvar.h>
#include <pxr/usd/usdGeom/imageable.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/pointInstancer.h>
#include <pxr/usd/usdGeom/modelAPI.h>

PXR_NAMESPACE_USING_DIRECTIVE

// ===========================================================================
// Maximum buffer chunk size to process at a time. Arbitrary, but based on 
// SOP_VEX_ARRAY_SIZE, aimed at fitting matrix4 array into a single cache line.
// It's also the same as in vexexec, which uses float-array size of 16x1024:
//     batch_size = SYSmin(size, 16*VEX_DataPool::getDataSize());
static constexpr exint HUSD_CVEX_DATA_BLOCK_SIZE = 1024;

// ===========================================================================
// Helper functions for USD VEX built-ins.
namespace {

namespace BindType
{
    using Int = CVEX_DataBinder<HUSD_VEX_PREC>::Int;
    using Flt = CVEX_DataBinder<HUSD_VEX_PREC>::Float;
    using Str = CVEX_DataBinder<HUSD_VEX_PREC>::String;
}

template<typename DATA_T> bool
husdSetDataFromElemnum( DATA_T &data, exint start, exint end, 
	const UT_ExintArray *indices )
{
    return false;
}

template<> bool
husdSetDataFromElemnum( UT_Array<BindType::Int> &data, 
	exint start, exint end, const UT_ExintArray *indices )
{
    UT_ASSERT( start >= 0 && (!indices || end <= indices->size() ));

    for( exint i = start; i < end && i - start < data.size(); i++ )
	data[i - start] = indices ? (*indices)[i] : i;
    return true;
}

template<typename DATA_T> bool
husdSetDataFromNumelem( DATA_T &data, exint numelem )
{
    return false;
}

template<> bool
husdSetDataFromNumelem( UT_Array<BindType::Int> &data, exint numelem )
{
    // The size should be 1, since numelem parmeter is always uniform.
    UT_ASSERT( data.size() == 1 );
    data.constant(numelem);
    return true;
}

template<typename DATA_T> bool
husdSetDataFromFrame( DATA_T &data, const UsdTimeCode &tc )
{
    return false;
}

template<> bool
husdSetDataFromFrame( UT_Array<BindType::Flt> &data, const UsdTimeCode &tc )
{
    // The size should be 1, since Frame parmeter is always uniform.
    UT_ASSERT( data.size() == 1 );

    UT_ASSERT( tc.IsNumeric() );
    BindType::Flt f = tc.IsNumeric() ? tc.GetValue() : 0.0;
    data.constant(f);
    return true;
}

template<typename DATA_T> bool
husdSetDataFromPrimpath( DATA_T &data, 
	const UT_Array<UsdPrim> &prims, exint start, exint end )
{
    return false;
}

template<> bool
husdSetDataFromPrimpath( UT_Array<BindType::Str> &data,
	const UT_Array<UsdPrim> &prims, exint start, exint end )
{
    UT_ASSERT( start >= 0 && end <= prims.size() );
    for( exint i = start; i < end && i - start < data.size(); i++ )
	data[i - start] = prims[i].GetPath().GetString();
    return true;
}

template<typename DATA_T> bool
husdSetDataFromPrimname( DATA_T &data, 
	const UT_Array<UsdPrim> &prims, exint start, exint end )
{
    return false;
}

template<> bool
husdSetDataFromPrimname( UT_Array<BindType::Str> &data,
	const UT_Array<UsdPrim> &prims, exint start, exint end )
{
    UT_ASSERT( start >= 0 && end <= prims.size() );
    for( exint i = start; i < end && i - start < data.size(); i++ )
	data[i - start] = prims[i].GetName().GetString();
    return true;
}


template<typename DATA_T> bool
husdSetDataFromPrimtype( DATA_T &data, 
	const UT_Array<UsdPrim> &prims, exint start, exint end )
{
    return false;
}

template<> bool
husdSetDataFromPrimtype( UT_Array<BindType::Str> &data,
	const UT_Array<UsdPrim> &prims, exint start, exint end )
{
    UT_ASSERT( start >= 0 && end <= prims.size() );
    for( exint i = start; i < end && i - start < data.size(); i++ )
	data[i - start] = prims[i].GetTypeName().GetString();
    return true;
}

template<typename DATA_T> bool
husdSetDataFromKind( DATA_T &data, 
	const UT_Array<UsdPrim> &prims, exint start, exint end )
{
    return false;
}

template<> bool
husdSetDataFromKind( UT_Array<BindType::Str> &data,
	const UT_Array<UsdPrim> &prims, exint start, exint end )
{
    TfToken	    kind;

    UT_ASSERT( start >= 0 && end <= prims.size() );
    for( exint i = start; i < end && i - start < data.size(); i++ )
    {
	UsdModelAPI api( prims[i] );
	data[i - start] = (api && api.GetKind(&kind)) ? kind.GetString() : "";
    }
    return true;
}

template<typename DATA_T> bool
husdSetDataFromDrawmode( DATA_T &data, 
	const UT_Array<UsdPrim> &prims, exint start, exint end )
{
    return false;
}

template<> bool
husdSetDataFromDrawmode( UT_Array<BindType::Str> &data,
	const UT_Array<UsdPrim> &prims, exint start, exint end )
{
    UT_ASSERT( start >= 0 && end <= prims.size() );
    for( exint i = start; i < end && i - start < data.size(); i++ )
    {
	UsdGeomModelAPI api( prims[i] );
	data[i - start] = api ? api.ComputeModelDrawMode().GetString(): "";
    }
    return true;
}

template<typename DATA_T> bool
husdSetDataFromPurpose( DATA_T &data, 
	const UT_Array<UsdPrim> &prims, exint start, exint end )
{
    return false;
}

template<> bool
husdSetDataFromPurpose( UT_Array<BindType::Str> &data,
	const UT_Array<UsdPrim> &prims, exint start, exint end )
{
    UT_ASSERT( start >= 0 && end <= prims.size() );
    for( exint i = start; i < end && i - start < data.size(); i++ )
    {
	UsdGeomImageable api( prims[i] );
	data[i - start] = api ? api.ComputePurpose().GetString(): "";
    }
    return true;
}

template<typename DATA_T> bool
husdSetDataFromActive( DATA_T &data, 
	const UT_Array<UsdPrim> &prims, exint start, exint end )
{
    return false;
}

template<> bool
husdSetDataFromActive( UT_Array<BindType::Int> &data, 
	const UT_Array<UsdPrim> &prims, exint start, exint end )
{
    UT_ASSERT( start >= 0 && end <= prims.size() );
    for( exint i = start; i < end && i - start < data.size(); i++ )
	data[i - start] = prims[i].IsActive();
    return true;
}

template<typename DATA_T> bool
husdSetDataFromVisible( DATA_T &data, 
	const UT_Array<UsdPrim> &prims, exint start, exint end,
	const UsdTimeCode &tc)
{
    return false;
}

template<> bool
husdSetDataFromVisible( UT_Array<BindType::Int> &data, 
	const UT_Array<UsdPrim> &prims, exint start, exint end,
	const UsdTimeCode &tc)
{
    UT_ASSERT( start >= 0 && end <= prims.size() );
    for( exint i = start; i < end && i - start < data.size(); i++ )
    {
	UsdGeomImageable api( prims[i] );
	data[i - start] = 
	    api ? api.ComputeVisibility(tc) != UsdGeomTokens->invisible : true;
    }
    return true;
}

} // end: anonymous namespace for setting cvex data from builtins

// ===========================================================================
// Built-in (global) CVEX parameters.
namespace {

// Utility class for holding info about a builtin.
class husdBuiltin
{
public:
    enum class VaryingMode  { NEVER, ALWAYS, IN_PRIMS_MODE };

			    husdBuiltin( const char *name, CVEX_Type type,
				    VaryingMode var_mode);
    const UT_StringHolder & name() const    { return myName; }
    CVEX_Type		    type() const    { return myType; }
    VaryingMode		    varmode() const { return myVarMode; }

private:
    UT_StringHolder	myName;
    CVEX_Type		myType;
    VaryingMode		myVarMode;
};

// Set of known built-ins
static UT_StringMap<husdBuiltin*>	theBuiltins;

husdBuiltin::husdBuiltin( const char *name, CVEX_Type type, VaryingMode varmode)
    : myName( UTmakeUnsafeRefHash( name ))
    , myType( type )
    , myVarMode( varmode )
{
    theBuiltins[ myName ] = this;
}

static inline husdBuiltin *
husdFindBuiltin( const UT_StringRef &name )
{
    auto it = theBuiltins.find( name );
    return it != theBuiltins.end() ? it->second : nullptr;
}

static inline bool
operator==(const UT_StringRef &name, const husdBuiltin &builtin )
{
    return name == builtin.name();
}


#define HUSD_BUILTIN(STRNAME, TYPE, VARMODE) \
static const husdBuiltin theBuiltin_##STRNAME(#STRNAME, TYPE, \
	husdBuiltin::VaryingMode::VARMODE); \

// Note about varying mode.  Elemnum and Numelem refers to VEX elements,
// so elemnum is always varying, while number of elements VEX run on is always 
// uniform.  The primitive-specific built-ins are uniform when run on array 
// elements and varying when run on primitives.
HUSD_BUILTIN( elemnum,	    CVEX_TYPE_INTEGER,  ALWAYS )
HUSD_BUILTIN( numelem,	    CVEX_TYPE_INTEGER,  NEVER )
HUSD_BUILTIN( primpath,	    CVEX_TYPE_STRING,   IN_PRIMS_MODE )
HUSD_BUILTIN( primname,	    CVEX_TYPE_STRING,   IN_PRIMS_MODE )
HUSD_BUILTIN( primtype,	    CVEX_TYPE_STRING,   IN_PRIMS_MODE )
HUSD_BUILTIN( primpurpose,  CVEX_TYPE_STRING,   IN_PRIMS_MODE )
HUSD_BUILTIN( primkind,	    CVEX_TYPE_STRING,   IN_PRIMS_MODE )
HUSD_BUILTIN( primdrawmode, CVEX_TYPE_STRING,   IN_PRIMS_MODE )
HUSD_BUILTIN( primactive,   CVEX_TYPE_INTEGER,  IN_PRIMS_MODE )
HUSD_BUILTIN( primvisible,  CVEX_TYPE_INTEGER,  IN_PRIMS_MODE )
HUSD_BUILTIN( Frame,	    CVEX_TYPE_FLOAT,	NEVER )
#undef HUSD_BUILTIN


inline bool
husdIsBuiltin( const UT_StringRef &name, CVEX_Type type )
{
    husdBuiltin *builtin = husdFindBuiltin( name );
    return builtin && type == builtin->type();
}

inline bool
husdIsBuiltinVarying( const UT_StringRef &name, bool is_prims_mode )
{
    husdBuiltin *builtin = husdFindBuiltin( name );

    if( builtin->varmode() == husdBuiltin::VaryingMode::ALWAYS )
	return true;
    else if( builtin->varmode() == husdBuiltin::VaryingMode::IN_PRIMS_MODE )
	return is_prims_mode;
    return false;
}

inline bool
husdIsBuiltinTimeDependent( const UT_StringRef &name )
{
    // TODO: factor it out into the husdBuiltin class, if this list grows.
    if( name == theBuiltin_Frame )
	return true;
    return false;
}

template<typename DATA_T>
inline bool
husdSetDataFromPrimBuiltin( DATA_T &data, const UT_StringRef &name,
	const UT_Array<UsdPrim> &prims, exint start, exint end,
	const UsdTimeCode &tc )
{
    if( name == theBuiltin_primpath )
	return husdSetDataFromPrimpath( data, prims, start, end );
    if( name == theBuiltin_primtype )
	return husdSetDataFromPrimtype( data, prims, start, end );
    if( name == theBuiltin_primkind )
	return husdSetDataFromKind( data, prims, start, end );
    if( name == theBuiltin_primname )
	return husdSetDataFromPrimname( data, prims, start, end );
    if( name == theBuiltin_primdrawmode )
	return husdSetDataFromDrawmode( data, prims, start, end );
    if( name == theBuiltin_primpurpose )
	return husdSetDataFromPurpose( data, prims, start, end );
    if( name == theBuiltin_primactive )
	return husdSetDataFromActive( data, prims, start, end );
    if( name == theBuiltin_primvisible )
	return husdSetDataFromVisible( data, prims, start, end, tc );

    return false;
}

template<typename DATA_T>
bool
husdSetDataFromBuiltin( DATA_T &data, exint size, const UT_StringRef &name,
	const UT_Array<UsdPrim> &prims, exint start, exint end,
	const UsdTimeCode &tc )
{
    // Setting built-ins for the mode that runs over the primitives.
    if( name == theBuiltin_elemnum )
	return husdSetDataFromElemnum( data, start, end, nullptr );
    if( name == theBuiltin_numelem )
	return husdSetDataFromNumelem( data, prims.size() );
    if( name == theBuiltin_Frame )
	return husdSetDataFromFrame( data, tc );
    if( husdSetDataFromPrimBuiltin( data, name, prims, start, end, tc ))
	return true;

    return false;
}

template<typename DATA_T>
bool
husdSetDataFromBuiltin( DATA_T &data, exint size, const UT_StringRef &name,
	const UsdPrim &prim, const UT_ExintArray *indices, 
	exint start, exint end, exint elem_count, const UsdTimeCode &tc )
{
    UT_Array<UsdPrim> prims(1, 1);
    prims[0] = prim;

    // Setting built-ins for the mode that runs over array elements.
    if( name == theBuiltin_elemnum )
	return husdSetDataFromElemnum( data, start, end, indices );
    if( name == theBuiltin_numelem )
	return husdSetDataFromNumelem( data, elem_count );
    if( name == theBuiltin_Frame )
	return husdSetDataFromFrame( data, tc );
    if( husdSetDataFromPrimBuiltin( data, name, prims, 0, 1, tc ))
	return true;

    return false;
}

} // end: anonymous namespace for managing builtins

// ===========================================================================
// Static helper functions
static inline bool
husdIsAttribVarying( const UsdAttribute &attrib, bool is_prims_mode )
{
    // When running on usd prims, all attribs are varying. But when
    // running on array alements, only array attribs are varying.
    return is_prims_mode || attrib.GetTypeName().IsArray();
}

static inline UsdAttribute
husdFindPrimAttrib( const UsdPrim &prim, const TfToken &name )
{
    return prim.HasAttribute( name )
	? prim.GetAttribute( name )
	: UsdAttribute();

}

static inline UsdAttribute
husdFindPrimAttrib( const UsdPrim &prim, const UT_StringRef &name )
{
    return name.isstring() 
	? husdFindPrimAttrib( prim, TfToken( name.toStdString() ))
	: UsdAttribute();
}

static inline UsdAttribute
husdFindOrCreatePrimAttrib( const UsdPrim &prim, 
	const TfToken &name, const SdfValueTypeName &type )
{
    return prim.HasAttribute( name )
	? prim.GetAttribute( name )
	: prim.CreateAttribute( name, type, true );
}

static inline UsdAttribute
husdFindOrCreatePrimAttrib( const UsdPrim &prim, 
	const UT_StringRef &name, const SdfValueTypeName &type )
{
    return husdFindOrCreatePrimAttrib(prim, TfToken(name.toStdString()), type);
}

template <VEX_Precision PREC>
static inline UT_StringHolder
husdGetCvexError( const char *header, CVEX_ContextT<PREC> &cvex_ctx )
{
    UT_WorkBuffer	msg;
    msg.sprintf("%s: %s\nVex Errors: %s\n", header,
	    (const char *) cvex_ctx.getLastError(),
	    (const char *) cvex_ctx.getVexErrors());

    UT_StringHolder	result;
    msg.stealIntoStringHolder( result );
    return result;
}

static inline HUSD_TimeCode
husdGetEffectiveTimeCode(const HUSD_TimeCode &tc, HUSD_TimeSampling sampling )
{
    return HUSDgetEffectiveTimeCode( tc, sampling );
}

static inline UsdTimeCode
husdGetEffectiveUsdTimeCode(const HUSD_TimeCode &tc, const UsdAttribute &attrib)
{
    // We want to author an attribute at a time sample (rather than at its 
    // default value) if it already has any time samples. Otherwise, we may
    // be setting a default value which does not take effect on current farame.
    return HUSDgetEffectiveUsdTimeCode( tc, attrib );
}

static inline void
husdUpdateTimeSampling(HUSD_TimeSampling &sampling, 
	const HUSD_TimeSampling new_sampling)
{
    if( new_sampling > sampling ) 
	sampling = new_sampling;
}

static inline void
husdUpdateIsTimeVarying( HUSD_TimeSampling &sampling, bool is_time_varying )
{
    // Has more than one sample.
    if( is_time_varying )
	husdUpdateTimeSampling( sampling, HUSD_TimeSampling::MULTIPLE );
}

static inline void
husdUpdateIsTimeSampled( HUSD_TimeSampling &sampling, bool is_time_sampled )
{
    // Has at least one time sample.
    if( is_time_sampled )
	husdUpdateTimeSampling( sampling, HUSD_TimeSampling::SINGLE );
}
    

// ===========================================================================
// Bundles the code with some additional options that depend on how it's run.
class HUSD_CvexCodeInfo
{
public:
    HUSD_CvexCodeInfo( const HUSD_CvexCode &code, bool is_prims_mode,
	    bool has_single_output = false,
	    const UT_StringRef &output_name = UT_StringRef())
	: myCode( code )
	, myIsRunOnPrims( is_prims_mode )
	, myHasSingleOutput( has_single_output )
	, myOutputName( output_name )
    {}

    void			setOutputName( const UT_StringRef &name ) 
				    { myOutputName = name; }

    const HUSD_CvexCode &	getCode() const
				    { return myCode; }
    bool			isCommand() const
				    { return myCode.isCommand(); }
    bool			isRunOnPrims() const
				    { return myIsRunOnPrims; }
    HUSD_CvexCode::ReturnType	getReturnType() const
				    { return myCode.getReturnType(); }
    bool			hasSingleOutput() const 
				    {return myHasSingleOutput; }
    const UT_StringHolder &	getOutputName() const
				    { return myOutputName; }

private:
    const HUSD_CvexCode &   myCode;		// Program code 
    bool		    myIsRunOnPrims;	// Code run on prims or faces?
    bool		    myHasSingleOutput;	// Expecting only one output?
    UT_StringHolder	    myOutputName;	// If so, that's the name.
};

// ===========================================================================
// Collection of variables used when running Cvex on USD data.
class HUSD_CvexRunData
{
public:
    HUSD_CvexRunData();

    /// LOP node that runs the CVEX program.
    void			setCwdNodeId( int node_id) 
				    { myCwdNodeId = node_id; }
    int				getCwdNodeId() const
				    { return myCwdNodeId; }

    /// OP callback is used to set up dependencies on nodes referenced
    /// from op: expressions. 
    void			setOpCaller( UT_OpCaller *caller) 
				    { myOpCaller = caller; }
    UT_OpCaller *		getOpCaller() const
				    { return myOpCaller; }
    
    /// Time at which attributes should be evaluated.
    void			setTimeCode(const HUSD_TimeCode &time_code) 
				    { myTimeCode = time_code; }
    const HUSD_TimeCode &	getTimeCode() const 
				    { return myTimeCode; }
    HUSD_TimeCode		getEffectiveTimeCode(
					HUSD_TimeSampling time_sampling ) const;
    
    /// Map between attribute names and cvex parameters.
    void			setBindingsMap(const HUSD_CvexBindingMap *map) 
				    { myBindingsMap = map; }
    const HUSD_CvexBindingMap &getBindingMap() const;

    /// Structure for providing USD stages that come in on the LOP node inputs.
    void			setDataInputs(HUSD_CvexDataInputs *inputs) 
				    { myDataInputs = inputs; }
    const HUSD_CvexDataInputs &	getDataInputs() const;

    /// USD data edit requests that originate from VEX functions.
    void			setDataCommand(HUSD_CvexDataCommand *command) 
				    { myDataCommand = command; }
    HUSD_CvexDataCommand *	getDataCommand() const
				    { return myDataCommand; }

    class FallbackLockBinder
    {
    public:
        FallbackLockBinder(HUSD_CvexRunData &rundata, HUSD_AutoAnyLock &lock)
            : myRunData(rundata)
        { myRunData.myFallbackDataInputs.setInputData(0, &lock); }
        ~FallbackLockBinder()
        { myRunData.myFallbackDataInputs.removeInputData(0); }

    private:
        HUSD_CvexRunData  &myRunData;
    };

private:
    friend class                FallbackLockBinder;

    int				myCwdNodeId;
    UT_OpCaller *		myOpCaller;
    HUSD_CvexDataInputs *	myDataInputs;
    HUSD_CvexDataCommand *	myDataCommand; 
    const HUSD_CvexBindingMap * myBindingsMap; 
    HUSD_TimeCode		myTimeCode;
    HUSD_CvexDataInputs		myFallbackDataInputs;
};

HUSD_CvexRunData::HUSD_CvexRunData()
    : myCwdNodeId(-1)
    , myOpCaller(nullptr)
    , myDataInputs(nullptr)
    , myDataCommand(nullptr)
    , myBindingsMap(nullptr)
{
}

static inline bool
husdIsCwdTimeDep(int cwd_node_id)
{
    OP_Node *node = OP_Node::lookupNode( cwd_node_id );
    return node && node->getParmList()->getTimeDependent();
}

HUSD_TimeCode
HUSD_CvexRunData::getEffectiveTimeCode( HUSD_TimeSampling time_sampling ) const
{ 
    // Note: the cwd node may have become time-dependent during VEX execution,
    // eg, when chf() VEX function evaluates an animated parameter.  Hence, 
    // we need to check if VEX execution results in time-dependent values.
    husdUpdateIsTimeVarying( time_sampling,  husdIsCwdTimeDep( myCwdNodeId ));
    return husdGetEffectiveTimeCode( myTimeCode, time_sampling );
}

const HUSD_CvexBindingMap &
HUSD_CvexRunData::getBindingMap() const
{
    static const HUSD_CvexBindingMap theEmptyMap;
    return myBindingsMap ? *myBindingsMap : theEmptyMap;
}

const HUSD_CvexDataInputs &
HUSD_CvexRunData::getDataInputs() const
{ 
    if (myDataInputs)
        return *myDataInputs;

    return myFallbackDataInputs;
}

// ===========================================================================
class HUSD_CvexBinding
{
public:
    HUSD_CvexBinding()
	: myParmType( CVEX_TYPE_INVALID ), 
	  myIsVarying( false ), myIsInput( false ), myIsOutput( false ), 
	  myIsBuiltin( false )
    {}

    HUSD_CvexBinding( 
	    const UT_StringRef &attrib_name, const UT_StringRef &attrib_type,
	    const UT_StringRef &parm_name, CVEX_Type parm_type,
	    bool is_varying, bool is_input, bool is_output, bool is_builtin )
	: myAttribName( attrib_name )
	, myAttribType( attrib_type )
	, myParmName( parm_name )
	, myParmType( parm_type )
	, myIsVarying( is_varying )
	, myIsInput( is_input )
	, myIsOutput( is_output )
	, myIsBuiltin( is_builtin )
    {}

    const UT_StringHolder &	getAttribName() const	{ return myAttribName; }
    const UT_StringHolder &	getAttribType() const	{ return myAttribType; }
    const UT_StringHolder &	getParmName() const	{ return myParmName; }
    CVEX_Type			getParmType() const	{ return myParmType; }
    bool			isVarying() const	{ return myIsVarying; }
    bool			isInput() const		{ return myIsInput; }
    bool			isOutput() const	{ return myIsOutput; }
    bool			isBuiltin() const	{ return myIsBuiltin; }

private: 
    UT_StringHolder	myAttribName;	// Name of the USD primitive attribute.
    UT_StringHolder	myAttribType;	// Name of the attribute type.
    UT_StringHolder	myParmName;	// Name of the CVEX function parameter.
    CVEX_Type		myParmType;	// Type of the CVEX function parameter.
    bool		myIsVarying;	// True, if CVEX parm is varying.
    bool		myIsInput;	// True, if CVEX parm is an input.
    bool		myIsOutput;	// True, if CVEX parm is an export.
    bool		myIsBuiltin;	// True, if CVEX parm is bound to a 
					//  built-in rather than an USD attrib.
};

using HUSD_CvexBindingList = UT_Array<HUSD_CvexBinding>;

static inline SdfValueTypeName 
husdGetAttribType( const HUSD_CvexBinding *binding, 
	const SdfValueTypeName &default_type )
{
    SdfValueTypeName result;

    // Explicit type takes precedence, so check it first.
    if( binding && binding->getAttribType().isstring() )
    {
	const UT_StringHolder &type_name = binding->getAttribType();
	result = SdfSchema::GetInstance().FindType(type_name.toStdString());
    }

    // Special cases of attributes that generally have a known type
    // (but USD does not provide generic way to determine them).
    if( !result && binding )
    {
	const UT_StringHolder &attrib_name = binding->getAttribName();

	// Note: for flexibility, specify Sdf scalar rather than array. 
	// Scalar works for both `v@` and `v[]@`, since below it gets promoted 
	// to array if needed.
	if( attrib_name == "primvars:displayColor"_UTsh )
	    result = SdfValueTypeNames->Color3f;
	if( attrib_name == "primvars:displayOpacity"_UTsh )
	    result = SdfValueTypeNames->Float;
    }

    // Fallback on the default type provided.
    if( !result )
	result = default_type;

    // Relax the final type before returning it: infer array type even 
    // if scalar type is provided.  This reduces the type menu by half, 
    // by allowing "color3f" even for arrays.
    // It's not possible to impose scalar type on array value, anyway.
    if( default_type.IsArray() && !result.IsArray() )
	result = result.GetArrayType();

    return result;
}

// ===========================================================================
// Binds USD primitive attribute to CVEX input parameters, for a block of data.
class HUSD_CvexBlockBinder : protected CVEX_DataBinder<HUSD_VEX_PREC>
{
public:
    HUSD_CvexBlockBinder( CVEX_ContextT<HUSD_VEX_PREC> &cvex_ctx,
	    CVEX_Data &data, exint start, exint end,
	    const HUSD_TimeCode &time_code )
	: CVEX_DataBinder<HUSD_VEX_PREC>( data, end - start )
	, myCvexContext( cvex_ctx )
	, myCurrBinding( nullptr )
	, myStart( start )
	, myEnd( end )
	, myTimeSampling( HUSD_TimeSampling::NONE )
    {
	myTimeCode = HUSDgetNonDefaultUsdTimeCode( time_code );
    }

    void bind( const HUSD_CvexBinding &binding ) 
    {
	CVEX_ValueT<HUSD_VEX_PREC> *v =
            myCvexContext.findInput( binding.getParmName() );
	if( v )
	{
	    myCurrBinding = &binding;
	    setAndBindData( *v );
	    myCurrBinding = nullptr;
	}
    }

    /// Returns maximum level of time sampling among bound attributes.
    HUSD_TimeSampling		getSourceDataTimeSampling() const
				    { return myTimeSampling; }

    /// Returns attributes that encountered problems when binding.
    const UT_StringArray &	getBadAttribs() const
				    { return myBadAttribs; }

protected:
    CVEX_ContextT<HUSD_VEX_PREC> &		getCvexContext() const	{ return myCvexContext;}
    const HUSD_CvexBinding *	getCurrBinding() const	{ return myCurrBinding;}
    exint			getStart() const	{ return myStart; }
    exint			getEnd() const		{ return myEnd; }
    UsdTimeCode			getUsdTimeCode() const  { return myTimeCode; }

    void			updateTimeSampling( const UsdAttribute &attrib);
    void			updateTimeSampling( HUSD_TimeSampling sampling);
    void			updateTimeVarying( bool is_time_varying );
    void			appendBadAttrib( const UT_StringRef &name )
				    { myBadAttribs.append( name ); }

private:
    CVEX_ContextT<HUSD_VEX_PREC>		&myCvexContext;
    const HUSD_CvexBinding	*myCurrBinding;
    exint			 myStart;
    exint			 myEnd;
    UsdTimeCode			 myTimeCode;

    // This are queried after binding has occured
    HUSD_TimeSampling		 myTimeSampling;// time sample count summary
    UT_StringArray		 myBadAttribs;	// What didn't bind cleanly?
};

void
HUSD_CvexBlockBinder::updateTimeSampling( const UsdAttribute &attrib )
{
    HUSDupdateValueTimeSampling( myTimeSampling, attrib );
}

void
HUSD_CvexBlockBinder::updateTimeSampling( HUSD_TimeSampling new_sampling )
{
    HUSDupdateTimeSampling( myTimeSampling, new_sampling );
}

void
HUSD_CvexBlockBinder::updateTimeVarying( bool is_time_varying )
{ 
    husdUpdateIsTimeVarying( myTimeSampling, is_time_varying );
}

// ===========================================================================
// Binds the USD data to CVEX data.
class HUSD_CvexDataBinder 
{
public:
    /// Constructor and destructor.
			 HUSD_CvexDataBinder( const HUSD_TimeCode &time_code );
    virtual		~HUSD_CvexDataBinder() = default;

    /// Structure to return the result and status of the binding call.
    class Status
    {
    public:
	Status( HUSD_TimeSampling sampling, const UT_StringArray &bad_attribs )
	    : myTimeSampling( sampling )
	    , myBadAttribs( bad_attribs )  
	{}

	HUSD_TimeSampling     getTimeSampling() const { return myTimeSampling; }
	const UT_StringArray &getBadAttribs() const   { return myBadAttribs; }
	
    private:
	HUSD_TimeSampling   myTimeSampling;
	UT_StringArray	    myBadAttribs;
    };

    /// Binds the USD data to CVEX data, storing it in the @p cvex_data
    /// buffers, and registering the buffers as inputs in the @p cvex_ctx.
    /// Only a block of data in the block [@p start, @p end) should be bound.
    /// The output parameter @is_time_varying is set to true, if 
    /// any USD data (attribute) is non-uniform.
    virtual Status	 bind( CVEX_ContextT<HUSD_VEX_PREC> &cvex_ctx, 
				CVEX_Data &cvex_input_data, 
				const HUSD_CvexBindingList &bindings,
				exint start, exint end ) const = 0;

protected:
    const HUSD_TimeCode &getTimeCode() const	{ return myTimeCode; }

private:
    HUSD_TimeCode	 myTimeCode;
};

HUSD_CvexDataBinder::HUSD_CvexDataBinder( const HUSD_TimeCode &time_code )
    : myTimeCode( time_code )
{
}

// ===========================================================================
static inline void 
husdBindOutputs( CVEX_ContextT<HUSD_VEX_PREC> &cvex_ctx, CVEX_Data &cvex_output_data, 
	const HUSD_CvexBindingList &bindings, exint block_data_size )
{
    CVEX_DataBinder<HUSD_VEX_PREC> binder( cvex_output_data, block_data_size );

    for( auto &&binding : bindings )
    {
	if( !binding.isOutput() )
	    continue;
	
	CVEX_ValueT<HUSD_VEX_PREC> *output = cvex_ctx.findOutput( binding.getParmName() );
	if( output )
	    binder.bindData( *output );
    }
}

// ===========================================================================
// Holds the CVEX output data in thread-friently structure. In paticular,
// packed arrays don't work well with threads, so using array-of-arrays.
class HUSD_CvexResultData
{
public:
    /// Constructor and destructor.
			HUSD_CvexResultData( exint data_size, 
				const HUSD_CvexBindingList &bindings );

    /// Returns the size of the result data buffers.
    exint		getDataSize() const
			{ return myDataSize; }

    /// Returns the data buffer of a given name.
    template<typename T>
    UT_Array<T> *	findDataBuffer( const UT_StringRef &name ) const
			    { return myData.findDataBuffer<T>( name ); }

    /// Returns the CVEX output type associated with the data buffer.
    CVEX_Type		findCvexType( const UT_StringRef &name ) const
			    { return myData.getCvexType( name ); }

private:
    template<typename T>
    bool		createDataBuffer( const UT_StringRef &name,
				CVEX_Type type );
    bool		addDataBuffer( const UT_StringRef &name,
				CVEX_Type type );

private:
    exint		myDataSize;
    CVEX_Data		myData;
};

HUSD_CvexResultData::HUSD_CvexResultData( exint data_size, 
	    const HUSD_CvexBindingList &bindings )
    : myDataSize( data_size )
{
    for( auto &&b : bindings )
	if( b.isOutput() )
	    addDataBuffer( b.getParmName(), b.getParmType() );
}

template<typename T>
bool
HUSD_CvexResultData::createDataBuffer( const UT_StringRef &n, CVEX_Type t )
{
    auto *buffer = myData.addDataBuffer<T>( n, t );

    UT_ASSERT( buffer );
    buffer->setSize( myDataSize );
    return true;
}

bool
HUSD_CvexResultData::addDataBuffer( const UT_StringRef &name,
	CVEX_Type type )
{
    using Type   = CVEX_DataType<HUSD_VEX_PREC>;
    using String = UT_StringHolder;

    switch( type )
    {
	case CVEX_TYPE_INTEGER:
	    return createDataBuffer<Type::Int>( name, type );
	case CVEX_TYPE_FLOAT:
	    return createDataBuffer<Type::Float>( name, type );
	case CVEX_TYPE_STRING:
	    return createDataBuffer<String>( name, type );
	case CVEX_TYPE_DICT:
	    return createDataBuffer<UT_OptionsHolder>( name, type );
	case CVEX_TYPE_VECTOR2:
	    return createDataBuffer<Type::Vec2>( name, type );
	case CVEX_TYPE_VECTOR3:
	    return createDataBuffer<Type::Vec3>( name, type );
	case CVEX_TYPE_VECTOR4:
	    return createDataBuffer<Type::Vec4>( name, type );
	case CVEX_TYPE_MATRIX2:
	    return createDataBuffer<Type::Mat2>( name, type );
	case CVEX_TYPE_MATRIX3:
	    return createDataBuffer<Type::Mat3>( name, type );
	case CVEX_TYPE_MATRIX4:
	    return createDataBuffer<Type::Mat4>( name, type );
	case CVEX_TYPE_INTEGER_ARRAY:
	    return createDataBuffer<UT_Array<Type::Int>>( name, type );
	case CVEX_TYPE_FLOAT_ARRAY:
	    return createDataBuffer<UT_Array<Type::Float>>( name, type );
	case CVEX_TYPE_STRING_ARRAY:
	    return createDataBuffer<UT_Array<String>>( name, type );
	case CVEX_TYPE_DICT_ARRAY:
	    return createDataBuffer<UT_Array<UT_OptionsHolder>>( name, type );
	case CVEX_TYPE_VECTOR2_ARRAY:
	    return createDataBuffer<UT_Array<Type::Vec2>>( name, type );
	case CVEX_TYPE_VECTOR3_ARRAY:
	    return createDataBuffer<UT_Array<Type::Vec3>>( name, type );
	case CVEX_TYPE_VECTOR4_ARRAY:
	    return createDataBuffer<UT_Array<Type::Vec4>>( name, type );
	case CVEX_TYPE_MATRIX2_ARRAY:
	    return createDataBuffer<UT_Array<Type::Mat2>>( name, type );
	case CVEX_TYPE_MATRIX3_ARRAY:
	    return createDataBuffer<UT_Array<Type::Mat3>>( name, type );
	case CVEX_TYPE_MATRIX4_ARRAY:
	    return createDataBuffer<UT_Array<Type::Mat4>>( name, type );
	default:
	    UT_ASSERT( !"Unhandled CVEX data type." );
	    break;
    }

    return false;
}

// ===========================================================================
template <VEX_Precision PREC>
class HUSD_CvexResultProcessor
{
public: 
    /// Constructor and destructor.
			 HUSD_CvexResultProcessor( 
				const HUSD_CvexResultData &data )
			    : myResultData( data ) {}
    virtual		~HUSD_CvexResultProcessor() = default;

    /// Processes the final output buffer of the given name.
    bool		 processResult( const UT_StringRef &name );

protected:
    DECLARE_CVEX_DATA_TYPES

    virtual bool processResultData( const UT_Array<Int> &data,
	    const UT_StringRef &name ) { return false; }
    virtual bool processResultData( const UT_Array<Float> &data,
	    const UT_StringRef &name ) { return false; }
    virtual bool processResultData( const UT_Array<String> &data,
	    const UT_StringRef &name ) { return false; }
    virtual bool processResultData( const UT_Array<Dict> &data,
	    const UT_StringRef &name ) { return false; }
    virtual bool processResultData( const UT_Array<Vec2> &data,
	    const UT_StringRef &name ) { return false; }
    virtual bool processResultData( const UT_Array<Vec3> &data,
	    const UT_StringRef &name ) { return false; }
    virtual bool processResultData( const UT_Array<Vec4> &data,
	    const UT_StringRef &name ) { return false; }
    virtual bool processResultData( const UT_Array<Mat2> &data,
	    const UT_StringRef &name  ) { return false; }
    virtual bool processResultData( const UT_Array<Mat3> &data,
	    const UT_StringRef &name  ) { return false; }
    virtual bool processResultData( const UT_Array<Mat4> &data,
	    const UT_StringRef &name  ) { return false; }

    virtual bool processResultData( const UT_Array<UT_Array<Int>> &data,
	    const UT_StringRef &name ) { return false; }
    virtual bool processResultData( const UT_Array<UT_Array<Float>> &data,
	    const UT_StringRef &name ) { return false; }
    virtual bool processResultData( const UT_Array<UT_Array<String>> &data,
	    const UT_StringRef &name ) { return false; }
    virtual bool processResultData( const UT_Array<UT_Array<Dict>> &data,
	    const UT_StringRef &name ) { return false; }
    virtual bool processResultData( const UT_Array<UT_Array<Vec2>> &data,
	    const UT_StringRef &name ) { return false; }
    virtual bool processResultData( const UT_Array<UT_Array<Vec3>> &data,
	    const UT_StringRef &name ) { return false; }
    virtual bool processResultData( const UT_Array<UT_Array<Vec4>> &data,
	    const UT_StringRef &name ) { return false; }
    virtual bool processResultData( const UT_Array<UT_Array<Mat2>> &data,
	    const UT_StringRef &name ) { return false; }
    virtual bool processResultData( const UT_Array<UT_Array<Mat3>> &data,
	    const UT_StringRef &name ) { return false; }
    virtual bool processResultData( const UT_Array<UT_Array<Mat4>> &data,
	    const UT_StringRef &name ) { return false; }


private:
    template<typename T>
    bool processResultHelper( const UT_StringRef &name )
    { 
	auto buffer = myResultData.findDataBuffer<T>(name);
	UT_ASSERT( buffer );
	if( !buffer )
	    return false;

	return processResultData( *buffer, name );
    }

private:
    const HUSD_CvexResultData	&myResultData;
};

template <VEX_Precision PREC>
bool
HUSD_CvexResultProcessor<PREC>::processResult( const UT_StringRef &name )
{
    switch( myResultData.findCvexType( name ))
    {
	case CVEX_TYPE_INTEGER:
	    return processResultHelper<Int>( name );
	case CVEX_TYPE_FLOAT:
	    return processResultHelper<Float>( name );
	case CVEX_TYPE_STRING:
	    return processResultHelper<String>( name );
	case CVEX_TYPE_DICT:
	    return processResultHelper<Dict>( name );
	case CVEX_TYPE_VECTOR2:
	    return processResultHelper<Vec2>( name );
	case CVEX_TYPE_VECTOR3:
	    return processResultHelper<Vec3>( name );
	case CVEX_TYPE_VECTOR4:
	    return processResultHelper<Vec4>( name );
	case CVEX_TYPE_MATRIX2:
	    return processResultHelper<Mat2>( name );
	case CVEX_TYPE_MATRIX3:
	    return processResultHelper<Mat3>( name );
	case CVEX_TYPE_MATRIX4:
	    return processResultHelper<Mat4>( name );
	case CVEX_TYPE_INTEGER_ARRAY:
	    return processResultHelper<UT_Array<Int>>( name );
	case CVEX_TYPE_FLOAT_ARRAY:
	    return processResultHelper<UT_Array<Float>>( name );
	case CVEX_TYPE_STRING_ARRAY:
	    return processResultHelper<UT_Array<String>>( name );
	case CVEX_TYPE_DICT_ARRAY:
	    return processResultHelper<UT_Array<Dict>>( name );
	case CVEX_TYPE_VECTOR2_ARRAY:
	    return processResultHelper<UT_Array<Vec2>>( name );
	case CVEX_TYPE_VECTOR3_ARRAY:
	    return processResultHelper<UT_Array<Vec3>>( name );
	case CVEX_TYPE_VECTOR4_ARRAY:
	    return processResultHelper<UT_Array<Vec4>>( name );
	case CVEX_TYPE_MATRIX2_ARRAY:
	    return processResultHelper<UT_Array<Mat2>>( name );
	case CVEX_TYPE_MATRIX3_ARRAY:
	    return processResultHelper<UT_Array<Mat3>>( name );
	case CVEX_TYPE_MATRIX4_ARRAY:
	    return processResultHelper<UT_Array<Mat4>>( name );
	default:
	    UT_ASSERT( !"Unhandled CVEX data type." );
	    break;
    }

    return false;
}

// ===========================================================================
// Binds USD primitive attribute data to CVEX inputs, for a data block.
class HUSD_PrimAttribBlockBinder : public HUSD_CvexBlockBinder 
{
public:
    HUSD_PrimAttribBlockBinder( CVEX_ContextT<HUSD_VEX_PREC> &cvex_ctx, CVEX_Data &data, 
	    const UT_Array<UsdPrim> &prims, exint start, exint end, 
	    const HUSD_TimeCode &time_code)
	: HUSD_CvexBlockBinder( cvex_ctx, data, start, end, time_code )
	, myPrims( prims )
    {}

protected:
    #define DATA_BINDER_METHOD_PAIR(TYPE) \
    bool setData( UT_Array<TYPE> &data, exint size, \
		  const UT_StringRef &name ) override \
	{ return setDataFromAttrib( data, size, name ); } \
    bool setData( UT_PackedArrayOfArrays<TYPE> &data, exint size, \
		  const UT_StringRef &name ) override \
	{ return setArrayDataFromAttrib( data, size, name ); } \

    DATA_BINDER_METHOD_PAIR( Int    )
    DATA_BINDER_METHOD_PAIR( Float  )
    DATA_BINDER_METHOD_PAIR( String )
    DATA_BINDER_METHOD_PAIR( Vec2   )
    DATA_BINDER_METHOD_PAIR( Vec3   )
    DATA_BINDER_METHOD_PAIR( Vec4   )
    DATA_BINDER_METHOD_PAIR( Mat2   )
    DATA_BINDER_METHOD_PAIR( Mat3   )
    DATA_BINDER_METHOD_PAIR( Mat4   )

    bool setData( UT_Array<Dict> &data, exint size,
                  const UT_StringRef &name ) override
	{ UT_ASSERT(!"Unhandled dictionary types"); return false; }
    bool setData( UT_PackedArrayOfArrays<Dict> &data, exint size,
                  const UT_StringRef &name ) override
	{ UT_ASSERT(!"Unhandled dictionary types"); return false; }

    #undef DATA_BINDER_METHOD_PAIR

private:
    template<typename SET_FN>
    bool setDataWithCallback(const UT_StringRef &name, exint size,
	    SET_FN set_fn)
    {
	UT_ASSERT( name == getCurrBinding()->getParmName() );
	const UT_StringHolder &attr_name = getCurrBinding()->getAttribName();

	TfToken	attrib_token( attr_name.toStdString() );
	for( exint i = getStart(); i < getEnd(); i++ )
	{
	    exint data_idx = i - getStart();
	    if( data_idx >= size )
	    {
		// This should happen only for uniform values.
		UT_ASSERT( size == 1 && !isVarying( name ));
		break;
	    }

	    auto attrib = husdFindPrimAttrib( myPrims[i], attrib_token );
	    if( !set_fn( attrib, data_idx ))
		appendBadAttrib( attr_name );
	}

	return true; 
    }

    template<typename DATA_T>
    bool setDataFromAttrib(DATA_T &data, exint size, const UT_StringRef &name)
    {
	if( getCurrBinding()->isBuiltin() )
	{
	    auto &attrib_name = getCurrBinding()->getAttribName();
	    updateTimeVarying( husdIsBuiltinTimeDependent( attrib_name ));
	    return husdSetDataFromBuiltin( data, size, attrib_name, myPrims, 
		    getStart(), getEnd(), getUsdTimeCode());
	}

	return setDataWithCallback( name, size,
		[&](const UsdAttribute &attrib, exint data_index)
		{
		    if( !attrib )
			return false;

		    updateTimeSampling( attrib );
		    return HUSDgetAttribute( attrib, data[ data_index ], 
			    getUsdTimeCode() );
		});
    }

    template<typename DATA_T>
    bool setArrayDataFromAttrib(DATA_T &data, exint size, 
	    const UT_StringRef &name)
    {
	data.clear();
	return setDataWithCallback( name, size,
		[&](const UsdAttribute &attrib, exint data_index)
		{
		    typename DATA_T::value_type temp_arr;
		    bool ok = (bool) attrib;
		    if( ok )
		    {
			updateTimeSampling( attrib );
			ok = HUSDgetAttribute( attrib, temp_arr, 
				getUsdTimeCode() );
		    }
		    data.append( temp_arr );
		    return ok;
		});
    }


private:
    const UT_Array<UsdPrim>	&myPrims;
};

// ===========================================================================
// Binds the USD primitive attribute to CVEX inputs.
class HUSD_PrimAttribDataBinder : public HUSD_CvexDataBinder
{
public:
    HUSD_PrimAttribDataBinder( const UT_Array<UsdPrim> &prims,
	    const HUSD_TimeCode &time_code )
	: HUSD_CvexDataBinder( time_code )
	, myPrims( prims )
    {}

    Status  bind( CVEX_ContextT<HUSD_VEX_PREC> &cvex_ctx, 
	  	  CVEX_Data &cvex_input_data, 
	  	  const HUSD_CvexBindingList &bindings,
	  	  exint start, exint end ) const override;

private:
    const UT_Array<UsdPrim>	&myPrims; 
};

HUSD_CvexDataBinder::Status	 
HUSD_PrimAttribDataBinder::bind( CVEX_ContextT<HUSD_VEX_PREC> &cvex_ctx, 
	CVEX_Data &cvex_input_data, const HUSD_CvexBindingList &bindings,
	exint start, exint end ) const

{
    HUSD_PrimAttribBlockBinder binder( cvex_ctx, cvex_input_data,
	    myPrims, start, end, getTimeCode() );

    for( auto &&binding : bindings )
	if( binding.isInput() )
	    binder.bind( binding );

    return Status( binder.getSourceDataTimeSampling(), binder.getBadAttribs() );
}

// ===========================================================================
// Holds the cached CVEX input data in thread-friently structure to avoid
// repeated query to USD array attribute.
class HUSD_ArrayElementAttribCache
{
public:
    HUSD_ArrayElementAttribCache( const UsdPrim &prim, 
	    const UT_ExintArray *indices, const HUSD_TimeCode &time_code )
	: myPrim( prim )
	, myIndices( indices )
	, myTimeCode( time_code )
    {}

    /// Get and store the array data.
    bool		prefetchData( const UT_StringRef &attrib_name, 
				CVEX_Type data_type );

    /// Returns the data buffer of a given name.
    template<typename T>
    UT_Array<T> *	findDataBuffer( const UT_StringRef &attrib_name ) const
			    { return myData.findDataBuffer<T>( attrib_name ); }

    /// Returns true if cache has data for a given attribute name.
    bool		hasData( const UT_StringRef &attrib_name ) const
			    { return myData.hasBuffer( attrib_name ); }

    /// Returns true if there were no issues prefetching the attrib.
    bool		isDataOK( const UT_StringRef &attrib_name ) const
			    { return !myBadAttribs.contains( attrib_name ); }

    /// Returns the CVEX output type associated with the data buffer.
    bool		isScalarData( const UT_StringRef &attrib_name ) const
			    { return !myData.isVarying( attrib_name ); }

    /// Returns level of time sampling for the given cached attribute.
    HUSD_TimeSampling	getTimeSampling( const UT_StringRef &attrib_name) const;


private:
    template<typename T>
    bool		prefetchDataBuffer( const UT_StringRef &name,
				CVEX_Type type );

private:
    const UsdPrim		&myPrim; 
    const UT_ExintArray		*myIndices;
    HUSD_TimeCode		 myTimeCode;
    CVEX_Data			 myData;

    UT_StringMap<HUSD_TimeSampling>	myAttribTimeSampling;
    UT_StringSet			myBadAttribs;
};

bool
HUSD_ArrayElementAttribCache::prefetchData( 
	const UT_StringRef &attrib_name, CVEX_Type data_type )
{
    using Type   = CVEX_DataType<HUSD_VEX_PREC>;
    using String = UT_StringHolder;

    switch( data_type )
    {
	case CVEX_TYPE_INTEGER:
	    return prefetchDataBuffer<Type::Int>( attrib_name, data_type );
	case CVEX_TYPE_FLOAT:
	    return prefetchDataBuffer<Type::Float>( attrib_name, data_type );
	case CVEX_TYPE_STRING:
	    return prefetchDataBuffer<String>( attrib_name, data_type );
	case CVEX_TYPE_VECTOR2:
	    return prefetchDataBuffer<Type::Vec2>( attrib_name, data_type );
	case CVEX_TYPE_VECTOR3:
	    return prefetchDataBuffer<Type::Vec3>( attrib_name, data_type );
	case CVEX_TYPE_VECTOR4:
	    return prefetchDataBuffer<Type::Vec4>( attrib_name, data_type );
	case CVEX_TYPE_MATRIX2:
	    return prefetchDataBuffer<Type::Mat2>( attrib_name, data_type );
	case CVEX_TYPE_MATRIX3:
	    return prefetchDataBuffer<Type::Mat3>( attrib_name, data_type );
	case CVEX_TYPE_MATRIX4:
	    return prefetchDataBuffer<Type::Mat4>( attrib_name, data_type );
	default:
	    UT_ASSERT( !"Unhandled CVEX data type." );
	    break;
    }
    return false;
}

template<typename T>
bool
HUSD_ArrayElementAttribCache::prefetchDataBuffer( 
	const UT_StringRef &attrib_name, CVEX_Type data_type )
{
    auto *buffer = myData.addDataBuffer<T>( attrib_name, data_type );
    UT_ASSERT( buffer );

    auto attrib = husdFindPrimAttrib( myPrim, attrib_name );
    if( !attrib )
    { 
	myData.setIsVarying( attrib_name, false ); // Flag as scalar value.
	buffer->setSize(1);
	buffer->zero();
	return true;
    }

    VtValue value;
    attrib.Get( &value, HUSDgetNonDefaultUsdTimeCode( myTimeCode ));

    bool ok = true;
    if( value.IsEmpty() )
    {
	// Attribute may have been authored without a value, or it may have
	// come from the schema with no real fallback value. Pretend it's zero.
	myData.setIsVarying( attrib_name, false ); // Flag as scalar value.
	buffer->setSize(1);
	buffer->zero();
    }
    else if( !value.IsArrayValued() )
    {
	T uniform_val;

	ok = HUSDgetValue( value, uniform_val );
	if( ok )
	{
	    myData.setIsVarying( attrib_name, false ); // Flag as scalar value.
	    buffer->setSize(1);
	    (*buffer)[0] = uniform_val;
	}
    }
    else 
    {
	UT_Array<T> full_array;

	ok = HUSDgetValue( value, full_array );
	if( ok )
	{
	    myData.setIsVarying( attrib_name, true ); // Flag as array value.

	    if( myIndices )
	    {
		buffer->setSize( myIndices->size() );
		for( exint i = 0; i < myIndices->size(); i++ )
		{
		    exint j = (*myIndices)[i];
		    if( full_array.isValidIndex(j))
			(*buffer)[i] = full_array[j];
		}
	    }
	    else
	    {
		*buffer = std::move( full_array );
	    }
	}
    }

    myAttribTimeSampling[ attrib_name ] = HUSDgetValueTimeSampling( attrib );
    if( !ok )
	myBadAttribs.insert( attrib_name );

    return ok;
}
    
HUSD_TimeSampling
HUSD_ArrayElementAttribCache::getTimeSampling( const UT_StringRef &name ) const
{
    auto it = myAttribTimeSampling.find( name );
    if( it == myAttribTimeSampling.end() )
	return HUSD_TimeSampling::NONE;

    return it->second;
}

// ===========================================================================
// Binds USD primitive array attribute data to CVEX inputs, for a data block.
class HUSD_ArrayElementBlockBinder : public HUSD_CvexBlockBinder 
{
public:
    HUSD_ArrayElementBlockBinder( 
	    CVEX_ContextT<HUSD_VEX_PREC> &cvex_ctx, CVEX_Data &data, 
	    const UsdPrim &prim, const UT_ExintArray *indices,
	    exint start, exint end, exint elem_count,
	    const HUSD_ArrayElementAttribCache &attrib_data_cache,
	    const HUSD_TimeCode &time_code)
	: HUSD_CvexBlockBinder( cvex_ctx, data, start, end, time_code )
	, myPrim( prim )
	, myIndices( indices )
	, myElemCount( elem_count )
	, myAttribDataCache( attrib_data_cache )
    {
	UT_ASSERT( !myIndices || myIndices->size() >= end );
    }

    /// Finds out the max size needed for the input values data buffers.
    static exint findMaxArraySize( const UsdPrim &prim,
	    const HUSD_CvexBindingList &bindings,
	    const HUSD_TimeCode &time_code );

protected:
    #define DATA_BINDER_METHOD(TYPE) \
    bool setData( UT_Array<TYPE> &data,	exint size, \
		  const UT_StringRef &name ) override \
	{ return setDataFromArray( data, size, name ); } \

    DATA_BINDER_METHOD( Int    )
    DATA_BINDER_METHOD( Float  )
    DATA_BINDER_METHOD( String )
    DATA_BINDER_METHOD( Vec2   )
    DATA_BINDER_METHOD( Vec3   )
    DATA_BINDER_METHOD( Vec4   )
    DATA_BINDER_METHOD( Mat2   )
    DATA_BINDER_METHOD( Mat3   )
    DATA_BINDER_METHOD( Mat4   )

    bool setData( UT_Array<Dict> &data,	exint size,
		  const UT_StringRef &name ) override
	{ UT_ASSERT(!"Unhandled dictionary types"); return false; }

    #undef DATA_BINDER_METHOD

    // CVEX does not have array of arrays data type, so elements can't be
    // arrays. So no need to implement methods for arrays; just use base class.
    using CVEX_DataBinder<HUSD_VEX_PREC>::setData;

private:
    template<typename DATA_T>
    bool setDataFromArray( DATA_T &data, exint size, const UT_StringRef &name );
    template<typename DATA_T>
    bool setDataFromPrefetchedArrayAttrib( DATA_T &data, exint size, 
	    const UT_StringRef &name );
    template<typename DATA_T>
    bool setDataFromLiveArrayAttrib( DATA_T &data, exint size,
	    const UT_StringRef &name );

private:
    const UsdPrim		&myPrim;
    const UT_ExintArray		*myIndices;
    exint			 myElemCount;
    const HUSD_ArrayElementAttribCache &myAttribDataCache;
};

static inline exint 
husdGetArraySize( const UsdAttribute &attrib, const UsdTimeCode &time_code )
{
    VtValue value;

    UT_ASSERT( attrib );
    attrib.Get( &value, time_code );
    if( value.IsArrayValued() )
	return value.GetArraySize();

    return 1;
}

exint 
HUSD_ArrayElementBlockBinder::findMaxArraySize( 
	const UsdPrim &prim, const HUSD_CvexBindingList &bindings,
	const HUSD_TimeCode &time_code) 
{
    UsdTimeCode	usd_time_code = HUSDgetNonDefaultUsdTimeCode( time_code );
    exint	max_size = 1;

    for( auto &&b : bindings )
    {
	if( !b.isInput() && !b.isOutput() )
	    continue;

	auto attrib = husdFindPrimAttrib( prim, b.getAttribName() );
	if( !attrib )
	    continue;

	exint size = husdGetArraySize( attrib, usd_time_code );
	if( size > max_size )
	    max_size = size;
    }

    return max_size;
}

template<typename DATA_T>
bool 
HUSD_ArrayElementBlockBinder::setDataFromArray(DATA_T &data, exint data_size,
	const UT_StringRef &data_name)
{
    UT_ASSERT( data_name == getCurrBinding()->getParmName() );
    const auto &attrib_name = getCurrBinding()->getAttribName();
    if( getCurrBinding()->isBuiltin() )
    {
	updateTimeVarying( husdIsBuiltinTimeDependent( attrib_name ));
        return husdSetDataFromBuiltin( data, data_size, attrib_name,
		myPrim, myIndices, getStart(), getEnd(), myElemCount,
		getUsdTimeCode());
    }

    if( myAttribDataCache.hasData( attrib_name ))
    {
	return setDataFromPrefetchedArrayAttrib( data, data_size, data_name );
    }
    
    return setDataFromLiveArrayAttrib( data, data_size, data_name );
}

template<typename DATA_T>
bool 
HUSD_ArrayElementBlockBinder::setDataFromPrefetchedArrayAttrib(DATA_T &data, 
	exint data_size, const UT_StringRef &data_name)
{
    UT_ASSERT( data_name == getCurrBinding()->getParmName() );
    const auto &attrib_name = getCurrBinding()->getAttribName();

    if( !myAttribDataCache.isDataOK( attrib_name ))
    {
	appendBadAttrib( attrib_name );
	return false;
    }
   
    auto *buffer = myAttribDataCache.
	findDataBuffer<typename DATA_T::value_type>( attrib_name );
    if( !buffer || buffer->size() <= 0 )
    {
	UT_ASSERT( !"Empty buffer" );
	return false;
    }

     if( myAttribDataCache.isScalarData( attrib_name ))
     {
	 data.constant( (*buffer)[0] );
     }
     else
     {
	for( exint i = getStart(); i < getEnd(); i++ )
	{
	    exint data_idx = i - getStart();
	    if( data_idx >= data_size )
	    {
		// This should happen only for uniform values.
		UT_ASSERT( data_size == 1 && !isVarying(data_name) );
		break;
	    }

	    // Note, the data cache already took myIndices into account.
	    if( buffer->isValidIndex(i) )
		data[ data_idx ] = (*buffer)[i];
	}
    }

    updateTimeSampling( myAttribDataCache.getTimeSampling( attrib_name ));
    return true;
}

template<typename DATA_T>
bool 
HUSD_ArrayElementBlockBinder::setDataFromLiveArrayAttrib(DATA_T &data, 
	exint data_size, const UT_StringRef &data_name)
{
    UT_ASSERT( data_name == getCurrBinding()->getParmName() );
    const auto &attrib_name = getCurrBinding()->getAttribName();
    auto attrib = husdFindPrimAttrib(myPrim, attrib_name);
    if( !attrib )
    {
	data.zero();	// Default the value to "zero".
	return true;
    }

    VtValue value;
    attrib.Get( &value, getUsdTimeCode() );

    bool ok = true;
    if( value.IsEmpty() )
    {
	// Attribute may have been authored without a value, or it may have
	// come from the schema with no real fallback value. Pretend it's zero.
	data.zero();
    }
    else if( !value.IsArrayValued() )
    {
	typename DATA_T::value_type uniform_val;
	ok = HUSDgetValue( value, uniform_val );
	if( ok )
	    data.constant( uniform_val );
    }
    else 
    {
	DATA_T full_array;
	ok = HUSDgetValue( value, full_array );

	UT_ASSERT( !myIndices || myIndices->size() >= getEnd() );
	for( exint i = getStart(); i < getEnd(); i++ )
	{
	    exint data_idx = i - getStart();
	    if( data_idx >= data_size )
	    {
		// This should happen only for uniform values.
		UT_ASSERT( data_size == 1 && !isVarying(data_name) );
		break;
	    }

	    exint arr_idx  = myIndices ? (*myIndices)[i] : i;
	    if( full_array.isValidIndex( arr_idx ))
		data[ data_idx ] = full_array[ arr_idx ];
	}
    }

    updateTimeSampling( attrib );
    if( !ok )
	appendBadAttrib( attrib_name );

    return true;
}

// ===========================================================================
// Binds the USD array attribute elements data to CVEX inputs.
class HUSD_ArrayElementDataBinder : public HUSD_CvexDataBinder
{
public:
    HUSD_ArrayElementDataBinder( exint array_size,
	    const UsdPrim &prim, const UT_ExintArray *indices,
	    const HUSD_TimeCode &time_code )
	: HUSD_CvexDataBinder( time_code )
	, myPrim( prim )
	, myIndices( indices )
	, myArraySize( array_size )
	, myAttribDataCache( prim, indices, time_code )
    {}

    // Pre-caches USD array attribute for later use in binding CVEX data.
    void		 prefetchAttribValues(
				const HUSD_CvexBindingList &bindings );

    Status	         bind( CVEX_ContextT<HUSD_VEX_PREC> &cvex_ctx, 
			       CVEX_Data &cvex_input_data, 
			       const HUSD_CvexBindingList &bindings,
                               exint start, exint end ) const override;

    static exint findArraySize( 
	    const UsdPrim &prim, const UT_ExintArray *indices, exint size_hint,
	    const HUSD_CvexBindingList &bindings, const HUSD_TimeCode &tc ) 
    {
	// CVEX needs to run on the elements specified by index array.
	if( indices )
	    return indices->size();

	// If the size is already known, restricted, or relaxed, then return it.
	if( size_hint > 0 )
	    return size_hint;

	// CVEX needs to run on all array elements, so use the max array length.
	return HUSD_ArrayElementBlockBinder::findMaxArraySize( prim, bindings, 
		tc);
    }

private:
    const UsdPrim		&myPrim; 
    const UT_ExintArray		*myIndices;
    exint		         myArraySize;
    HUSD_ArrayElementAttribCache myAttribDataCache;
};

void
HUSD_ArrayElementDataBinder::prefetchAttribValues( 
	const HUSD_CvexBindingList &bindings)
{
    for( auto &&b : bindings )
	if( b.isInput() && !b.isBuiltin() )
	    myAttribDataCache.prefetchData( b.getAttribName(), b.getParmType());
}
    
HUSD_CvexDataBinder::Status	 
HUSD_ArrayElementDataBinder::bind( CVEX_ContextT<HUSD_VEX_PREC> &cvex_ctx, 
	CVEX_Data &cvex_input_data, const HUSD_CvexBindingList &bindings,
	exint start, exint end ) const
{
    HUSD_ArrayElementBlockBinder  binder( cvex_ctx, cvex_input_data,
	    myPrim, myIndices, start, end, myArraySize, myAttribDataCache,
	    getTimeCode() );

    for( auto &&binding : bindings )
	if( binding.isInput() )
	    binder.bind( binding );

    return Status( binder.getSourceDataTimeSampling(), binder.getBadAttribs() );
}


// ===========================================================================
static inline exint
husdGetDataIndex( exint i, exint start, exint data_size )
{
    exint data_idx = i - start;
    if( data_idx >= data_size )
	data_idx = data_size - 1;

    UT_ASSERT( data_idx >= 0 && data_idx < data_size );
    return data_idx;
}

// ===========================================================================
// Transfers the computed data from CVEX buffers into the final buffers.
class HUSD_CvexBlockRetriever : private CVEX_DataRetriever<HUSD_VEX_PREC>
{
public:
    HUSD_CvexBlockRetriever( HUSD_CvexResultData &result, 
	    const CVEX_Data &data, exint start, exint end )
	: CVEX_DataRetriever<HUSD_VEX_PREC>( data )
	, myResultData( result )
	, myStart( start )
	, myEnd( end )
    {}

    /// Copies the data from the CVEX buffers to the final result buffer.
    bool retrieve( const HUSD_CvexBinding &binding )
    {
	return retrieveData( binding.getParmName() );
    }

protected:
    #define DATA_RETRIEVER_METHOD_PAIR( UT_TYPE )			\
    bool takeData( const UT_Array<UT_TYPE> &data,		        \
		   const UT_StringRef &name ) override	                \
	{ return transferSclrData( data, name ); }		        \
    bool takeData( const UT_PackedArrayOfArrays<UT_TYPE> &data,	        \
		   const UT_StringRef &name ) override	                \
	{ return transferArrData( data, name ); }		        \

    DATA_RETRIEVER_METHOD_PAIR( Int    )
    DATA_RETRIEVER_METHOD_PAIR( Float  )
    DATA_RETRIEVER_METHOD_PAIR( String )
    DATA_RETRIEVER_METHOD_PAIR( Dict )
    DATA_RETRIEVER_METHOD_PAIR( Vec2   )
    DATA_RETRIEVER_METHOD_PAIR( Vec3   )
    DATA_RETRIEVER_METHOD_PAIR( Vec4   )
    DATA_RETRIEVER_METHOD_PAIR( Mat2   )
    DATA_RETRIEVER_METHOD_PAIR( Mat3   )
    DATA_RETRIEVER_METHOD_PAIR( Mat4   )

    #undef DATA_RETRIEVER_METHOD_PAIR

private:
    template<typename T, typename F>
    bool transferData( UT_Array<T> *result_buffer, exint data_size, F callback )
    {
	UT_ASSERT( result_buffer );
	UT_ASSERT( myStart >= 0 && myEnd <= result_buffer->size() );
	UT_ASSERT( (myEnd - myStart <= data_size) || data_size == 1 );

	// Note: Other threads may be setting entries outside the given range.
	//       This also implies that we cannot trigger buffer reallocation!
	for( exint i = myStart; i < myEnd; i++ )
	{
	    exint data_idx = husdGetDataIndex( i, myStart, data_size );
	    callback( (*result_buffer)[i], data_idx );
	}

	return true;
    }

    template<typename T>
    bool transferSclrData(const UT_Array<T> &data, 
	    const UT_StringRef &name )
    {
	auto *result_buffer = myResultData.findDataBuffer<T>( name );
	return transferData( result_buffer, data.size(),
		[&]( T &result_entry, exint data_index )
		{
		    result_entry = data[ data_index ];
		});
    }

    template<typename T>
    bool transferArrData(const UT_PackedArrayOfArrays<T> &data, 
	    const UT_StringRef &name )
    {
	auto *result_buffer = myResultData.findDataBuffer<UT_Array<T>>( name );
	return transferData( result_buffer, data.size(),
		[&]( UT_Array<T> &result_entry, exint data_index )
		{
		    data.extract( result_entry, data_index );
		});
    }

private:
    HUSD_CvexResultData		&myResultData;	// destination buffer
    exint			 myStart;	// dest block start index
    exint			 myEnd;		// dest block end index
};

// ===========================================================================
// Transfers the computed data from CVEX buffers into the final buffers.
class HUSD_CvexDataRetriever 
{
public:
    HUSD_CvexDataRetriever( HUSD_CvexResultData &result )
	: myResultData( result )
    {}

    /// Copies the data from the CVEX buffers to the final result buffer.
    bool transferResultData( CVEX_Data &cvex_output_data, 
	    const HUSD_CvexBindingList &bindings,
	    exint start, exint end ) const
    {
	bool			ok = true;
	HUSD_CvexBlockRetriever	retriever( myResultData, cvex_output_data, 
		start, end );

	for( auto &&binding : bindings )
	    if( binding.isOutput() && !retriever.retrieve( binding ))
		ok = false;

	// We have control over defining buffers, their sizes, ranges, 
	// and types, so all there should be no problems. 
	UT_ASSERT( ok );
	return ok;
    }
    
    exint getResultDataSize() const
    {
	return myResultData.getDataSize();
    }

private:
    HUSD_CvexResultData		&myResultData;
};

// ===========================================================================
// Transfers the computed data from CVEX arrays to USD primitive attributes.
class HUSD_AttribSetter : private HUSD_CvexResultProcessor<HUSD_VEX_PREC>
{
public:
    HUSD_AttribSetter( const HUSD_CvexResultData &data, 
	    const UT_Array<UsdPrim> &prims, const HUSD_TimeCode &tc )
	: HUSD_CvexResultProcessor<HUSD_VEX_PREC>( data )
	, myPrims( prims ), myTimeCode( tc ), myCurrBinding( nullptr )
    {}

    bool setAttrib( const HUSD_CvexBinding &binding ) 
    {
	myCurrBinding = &binding;
	bool ok = processResult( binding.getParmName() );
	myCurrBinding = nullptr;
	return ok;
    }

protected:
    #define DATA_PROCESSOR_METHOD(UT_TYPE, SDF_TYPE)		\
    bool processResultData( const UT_Array<UT_TYPE> &data,	\
			    const UT_StringRef &name ) override	\
		{ return setAttribFromData( data, name,		\
			SdfValueTypeNames->SDF_TYPE ); }	\

    DATA_PROCESSOR_METHOD( Int,			Int      )
    DATA_PROCESSOR_METHOD( Float,		Double   )
    DATA_PROCESSOR_METHOD( String,		String   )
    DATA_PROCESSOR_METHOD( Vec2,		Double2  )
    DATA_PROCESSOR_METHOD( Vec3,		Double3  )
    DATA_PROCESSOR_METHOD( Vec4,		Double4  )
    DATA_PROCESSOR_METHOD( Mat2,		Matrix2d )
    DATA_PROCESSOR_METHOD( Mat3,		Matrix3d )
    DATA_PROCESSOR_METHOD( Mat4,		Matrix4d )
    DATA_PROCESSOR_METHOD( UT_Array<Int>,	IntArray      )
    DATA_PROCESSOR_METHOD( UT_Array<Float>,	DoubleArray   )
    DATA_PROCESSOR_METHOD( UT_Array<String>,	StringArray   )
    DATA_PROCESSOR_METHOD( UT_Array<Vec2>,	Double2Array  )
    DATA_PROCESSOR_METHOD( UT_Array<Vec3>,	Double3Array  )
    DATA_PROCESSOR_METHOD( UT_Array<Vec4>,	Double4Array  )
    DATA_PROCESSOR_METHOD( UT_Array<Mat2>,	Matrix2dArray )
    DATA_PROCESSOR_METHOD( UT_Array<Mat3>,	Matrix3dArray )
    DATA_PROCESSOR_METHOD( UT_Array<Mat4>,	Matrix4dArray )

    bool processResultData( const UT_Array<Dict> &data,
			    const UT_StringRef &name ) override
		{ UT_ASSERT("!Unhandled type dictionary"); return false; }
    bool processResultData( const UT_Array<UT_Array<Dict>> &data,
			    const UT_StringRef &name ) override
		{ UT_ASSERT("!Unhandled type dictionary"); return false; }
    #undef DATA_PROCESSOR_METHOD

private:
    template<typename T>
    bool setAttribFromData(const UT_Array<T> &data, 
	    const UT_StringRef &data_name, const SdfValueTypeName &type)
    {
	const auto	&attrib_name = myCurrBinding->getAttribName();
	SdfValueTypeName attrib_type = husdGetAttribType( myCurrBinding, type );
	
	bool ok = true;
	UT_ASSERT( data_name == myCurrBinding->getParmName() );
	for( exint i = 0; i < data.size(); i++ )
	{
	    UsdAttribute attrib = husdFindOrCreatePrimAttrib( 
		    myPrims[i], attrib_name, attrib_type );
	    setPrimvarInterpolation(attrib);

	    UsdTimeCode tc( husdGetEffectiveUsdTimeCode( myTimeCode, attrib ));
	    if( !HUSDsetAttribute( attrib, data[i], tc ))
		ok = false;
	}

	return ok;
    }

    void setPrimvarInterpolation(UsdAttribute &attrib)
    {
	// For prim mode, we infer the per-primitive interpolation (ie, "const")
	// This can be overriden with usd_setinterpolation() VEX function.
	UsdGeomPrimvar primvar(attrib);
	if( primvar && !primvar.HasAuthoredInterpolation() )
	    primvar.SetInterpolation(UsdGeomTokens->constant);
    }

private:
    const UT_Array<UsdPrim>	&myPrims;
    HUSD_TimeCode		 myTimeCode;
    const HUSD_CvexBinding	*myCurrBinding;
};

// ===========================================================================
// Transfers the computed data from CVEX arrays to USD array attributes.
class HUSD_ArraySetter : private HUSD_CvexResultProcessor<HUSD_VEX_PREC>
{
public:
    HUSD_ArraySetter( const HUSD_CvexResultData &data, 
	    UsdPrim &prim, const HUSD_TimeCode &tc )
	: HUSD_CvexResultProcessor<HUSD_VEX_PREC>( data )
	, myPrim( prim ), myTimeCode( tc ), myCurrBinding( nullptr )
    {}

    bool setAttrib( const HUSD_CvexBinding &binding ) 
    {
	myCurrBinding = &binding;
	bool ok = processResult( binding.getParmName() );
	myCurrBinding = nullptr;
	return ok;
    }

protected:
    #define DATA_PROCESSOR_METHOD(UT_TYPE, SDF_TYPE)			\
    bool processResultData( const UT_Array<UT_TYPE> &data,	        \
			    const UT_StringRef &name ) override	        \
		{ return setAttribFromData( data, name,			\
			SdfValueTypeNames->SDF_TYPE##Array ); }		\

    DATA_PROCESSOR_METHOD( Int,     Int      )
    DATA_PROCESSOR_METHOD( Float,   Double   )
    DATA_PROCESSOR_METHOD( String,  String   )
    DATA_PROCESSOR_METHOD( Vec2,    Double2  )
    DATA_PROCESSOR_METHOD( Vec3,    Double3  )
    DATA_PROCESSOR_METHOD( Vec4,    Double4  )
    DATA_PROCESSOR_METHOD( Mat2,    Matrix2d )
    DATA_PROCESSOR_METHOD( Mat3,    Matrix3d )
    DATA_PROCESSOR_METHOD( Mat4,    Matrix4d )

    bool processResultData( const UT_Array<Dict> &data,
			    const UT_StringRef &name ) override
            { UT_ASSERT(!"Invalid Dictionary Type"); return false; }

    #undef DATA_PROCESSOR_METHOD

    // There is no USD array-of-array attribute data type; use base class.
    using HUSD_CvexResultProcessor<HUSD_VEX_PREC>::processResultData;

private:
    template<typename T>
    bool setAttribFromData(const UT_Array<T> &data, 
	    const UT_StringRef &data_name, const SdfValueTypeName &type)
    {
	UT_ASSERT( data_name == myCurrBinding->getParmName() );
	const auto	&attrib_name = myCurrBinding->getAttribName();
	SdfValueTypeName attrib_type = husdGetAttribType( myCurrBinding, type );

	UsdAttribute attrib = 
	    husdFindOrCreatePrimAttrib( myPrim, attrib_name, attrib_type );
	setPrimvarInterpolation(attrib, data.size() > 1);

	UsdTimeCode tc( husdGetEffectiveUsdTimeCode( myTimeCode, attrib ));
	return HUSDsetAttribute( attrib, data, tc );
    }

    void setPrimvarInterpolation(UsdAttribute &attrib, bool is_vertex)
    {
	// For array mode, we infer the per-point interpolation (ie, "vertex"), 
	// because array mode most often is used for processing points.
	// Unless there is just one point, in which case we use "const".
	// This can be overriden with usd_setinterpolation() VEX function.
	UsdGeomPrimvar primvar(attrib);
	if( primvar && !primvar.HasAuthoredInterpolation() )
	    primvar.SetInterpolation( is_vertex ? 
		    UsdGeomTokens->vertex : UsdGeomTokens->constant);
    }


private:
    UsdPrim			&myPrim;
    HUSD_TimeCode		 myTimeCode;
    const HUSD_CvexBinding	*myCurrBinding;
};

// ===========================================================================
// Obtains data indices for which the integer output value is non-zero (true).
class HUSD_SelectionCollector : private HUSD_CvexResultProcessor<HUSD_VEX_PREC>
{ 
public:
    HUSD_SelectionCollector( UT_ExintArray &selected_indices, 
	    const HUSD_CvexResultData &data, const UT_StringRef &output_name )
	: HUSD_CvexResultProcessor<HUSD_VEX_PREC>( data )
	, mySelectedIndices( selected_indices )
	, myOutputName( output_name )
    {}

    bool getSelection() 
    {
	return processResult( myOutputName );
    }

protected:
    // We only check integer parameters.
    bool processResultData( const UT_Array<Int> &data,		
	                    const UT_StringRef &name ) override;
    using HUSD_CvexResultProcessor<HUSD_VEX_PREC>::processResultData;

private:
    UT_ExintArray		&mySelectedIndices;
    UT_StringHolder		myOutputName;
};

bool
HUSD_SelectionCollector::processResultData(const UT_Array<Int> &data,
	const UT_StringRef &)
{
    for( int i = 0; i < data.size(); i++ )
	if( data[i] )
	    mySelectedIndices.append(i);
    return true;
}

// ===========================================================================
// Partitions the CVEX data indices based on a string value of the CVEX output.
class HUSD_KeywordPartitioner : private HUSD_CvexResultProcessor<HUSD_VEX_PREC>
{ 
public:
    HUSD_KeywordPartitioner( UT_StringMap<UT_ExintArray> &map,
	    const HUSD_CvexResultData &data, const UT_StringRef &output_name )
	: HUSD_CvexResultProcessor<HUSD_VEX_PREC>( data )
	, myMap( map ), myOutputName( output_name )
    {}

    bool partition()
    {
	return processResult( myOutputName );
    }

protected:
    bool processResultData( const UT_Array<Int> &data,		
	                    const UT_StringRef &name ) override;
    bool processResultData( const UT_Array<String> &data,		
	                    const UT_StringRef &name ) override;
    using HUSD_CvexResultProcessor<HUSD_VEX_PREC>::processResultData;

private:
    UT_StringMap<UT_ExintArray>	&myMap;
    UT_StringHolder		 myOutputName;
};

bool
HUSD_KeywordPartitioner::processResultData(const UT_Array<Int> &data, 
	const UT_StringRef &name)
{
    UT_String	keyword;

    for( int i = 0; i < data.size(); i++ )
    {
	keyword.itoa( data[i] );
	myMap[ keyword ].append(i);
    }

    return true;
}

bool
HUSD_KeywordPartitioner::processResultData(const UT_Array<String> &data, 
	const UT_StringRef &name)
{
    for( int i = 0; i < data.size(); i++ )
	myMap[ data[i] ].append(i);

    return true;
}

// ===========================================================================
// Code for partitioning CVEX data indices according to multiple values
// of different types. Each value has a (parameter) name associated with it.
// HUSD_PartitionValue represents a parameter value used for partitioning.
using HUSD_PartitionValue = UT_OptionEntryPtr;

#define MAKE_PARTITION_VALUE(UT_TYPE, OPTION_TYPE)			\
static inline HUSD_PartitionValue					\
husdPartitionValue( UT_TYPE value )					\
{									\
    return HUSD_PartitionValue( new OPTION_TYPE( value ));		\
}									\

#define MAKE_PARTITION_VALUE_CAST(UT_TYPE, UT_CAST_TYPE, OPTION_TYPE)	\
static inline HUSD_PartitionValue					\
husdPartitionValue( UT_TYPE value )					\
{									\
    UT_CAST_TYPE    cast_value( value );				\
    return HUSD_PartitionValue( new OPTION_TYPE( cast_value )); 	\
}									\

// String signature.
MAKE_PARTITION_VALUE( const UT_StringRef&, UT_OptionString )
MAKE_PARTITION_VALUE( const UT_Array<UT_StringHolder>&, UT_OptionStringArray )

// The signatures for VEX_32 precision:
MAKE_PARTITION_VALUE( int32, UT_OptionInt )
MAKE_PARTITION_VALUE( fpreal32, UT_OptionFpreal )
MAKE_PARTITION_VALUE( const UT_Vector2F&, UT_OptionVector2 )
MAKE_PARTITION_VALUE( const UT_Vector3F&, UT_OptionVector3 )
MAKE_PARTITION_VALUE( const UT_Vector4F&, UT_OptionVector4 )
MAKE_PARTITION_VALUE_CAST( const UT_Matrix2F&, UT_Matrix2D, UT_OptionMatrix2 )
MAKE_PARTITION_VALUE_CAST( const UT_Matrix3F&, UT_Matrix3D, UT_OptionMatrix3 )
MAKE_PARTITION_VALUE_CAST( const UT_Matrix4F&, UT_Matrix4D, UT_OptionMatrix4 )
MAKE_PARTITION_VALUE( const UT_Array<int32>&, UT_OptionInt64Array )
MAKE_PARTITION_VALUE( const UT_Array<fpreal32>&, UT_OptionFpreal64Array )

// The signatures for VEX_64 precision:
MAKE_PARTITION_VALUE( int64, UT_OptionInt )
MAKE_PARTITION_VALUE( fpreal64, UT_OptionFpreal )
MAKE_PARTITION_VALUE( const UT_Vector2D&, UT_OptionVector2 )
MAKE_PARTITION_VALUE( const UT_Vector3D&, UT_OptionVector3 )
MAKE_PARTITION_VALUE( const UT_Vector4D&, UT_OptionVector4 )
MAKE_PARTITION_VALUE( const UT_Matrix2D&, UT_OptionMatrix2 )
MAKE_PARTITION_VALUE( const UT_Matrix3D&, UT_OptionMatrix3 )
MAKE_PARTITION_VALUE( const UT_Matrix4D&, UT_OptionMatrix4 )
MAKE_PARTITION_VALUE( const UT_Array<int64>&, UT_OptionInt64Array )
MAKE_PARTITION_VALUE( const UT_Array<fpreal64>&, UT_OptionFpreal64Array )

#undef MAKE_PARTITION_VALUE_CAST
#undef MAKE_PARTITION_VALUE


// ===========================================================================
// A hash (and a dictionary key) for the HUSD_PartitionValue.
class HUSD_PartitionKey
{
public:
			HUSD_PartitionKey() = default;
			HUSD_PartitionKey(UT_OptionEntryPtr opt)
			    : myValue(std::move(opt)) {}
    template<typename T>HUSD_PartitionKey( const T &value )
			    : myValue( husdPartitionValue( value )) {}

    const UT_OptionEntry *asOption() const
			    { return myValue.get(); }

    size_t		hash() const
			    { return myValue ? myValue->hash() : (size_t) 0; }
    bool		operator==( const HUSD_PartitionKey &k ) const
			    { 
				return (!myValue && !k.myValue) 
				    || (myValue && k.myValue && 
					myValue->isEqual(*k.myValue));
			    }
    bool		operator!=( const HUSD_PartitionKey &k ) const
			    { return !(*this == k); }

private:
    HUSD_PartitionValue	myValue;
};

size_t hash_value(const HUSD_PartitionKey &m)
{
    return m.hash();
}


// ===========================================================================
// Tree-like structure for partitioning entities according to the
// parameter values. The tree branch path leading to a leaf represents 
// the set of values (and their names), and the leaf node contains 
// entities belonging to that partition.
class HUSD_PartitionNode;
using HUSD_PartitionNodePtr = UT_UniquePtr<HUSD_PartitionNode>;
using HUSD_PartitionMap	    = UT_Map<HUSD_PartitionKey, HUSD_PartitionNodePtr>;

class HUSD_PartitionNode 
{
public:
    /// @{ Sets/gets the name of a value based on which the entities 
    /// (stored as @p myIndices) are partitioned into children of this node.
    void			setValueName( const UT_StringRef &name )
				    { myValueName = name; }
    const UT_StringHolder &	getValueName() const 
				    { return myValueName; }
    /// @}
    
    /// @{ Adds an index of an entity belonging to this partition.
    void			addIndex( exint idx )
				    { myIndices.append( idx ); }
    const UT_ExintArray &	getIndices() const
				    { return myIndices; }
    /// @}

    /// Returns true if the node is a leaf of the tree structure.
    bool			isLeaf() const
				    { return !myValueName.isstring(); }

    /// Returns a sub-partition given the value key.
    const HUSD_PartitionNodePtr &   findOrAddSubPartition(
					const HUSD_PartitionKey &key );

    /// Returns the partition children of this partition node.
    const HUSD_PartitionMap &	    getSubPartitions() const 
					{ return myChildren; }

    /// Recursively traverses the tree until the leaves are found,
    /// and invokes the callback on each found leaf. 
    /// The callback is given the name-values that lead to the leaf,
    /// as well as the element indices stored at that leaf.
    template<typename FUNC> void    traverseLeaves( const UT_Options &values,
					FUNC callback ) const;

private:
    UT_StringHolder		myValueName;	// Value name.
    HUSD_PartitionMap		myChildren;	// Partition for each value.
    UT_ExintArray		myIndices;	// Elements in partition.
};

const HUSD_PartitionNodePtr &
HUSD_PartitionNode::findOrAddSubPartition( const HUSD_PartitionKey &key )
{
    auto it = myChildren.find( key );
    if( it != myChildren.end() )
	return it->second;

    const HUSD_PartitionNodePtr &new_partition = myChildren.emplace( 
	    HUSD_PartitionKey( key.asOption()->clone() ),
	    UTmakeUnique< HUSD_PartitionNode >() )
	.first->second;

    return new_partition;
}

template<typename FUNC>
void
HUSD_PartitionNode::traverseLeaves( const UT_Options &values,
	FUNC callback ) const
{
    if( isLeaf() )
    {
	UT_ASSERT( getSubPartitions().empty() );
	callback( values, getIndices() );
    }
    else
    {
	// Non-leaf; recurse into children
	for( auto && it : getSubPartitions() )
	{
	    const HUSD_PartitionNode *child = it.second.get(); 
	    UT_ASSERT( child );

	    UT_Options child_values( values );
	    child_values.setOption( getValueName(),
		    it.first.asOption()->clone() );

	    child->traverseLeaves( child_values, callback );
	}
    }
}

// ===========================================================================
// Partitions the CEVX data indices contained in the given @p root node 
// based on the values (and names) of the CVEX outputs.
class HUSD_ValuePartitioner : private HUSD_CvexResultProcessor<HUSD_VEX_PREC>
{
public:
    HUSD_ValuePartitioner( const HUSD_CvexResultData &data, 
	    HUSD_PartitionNode &root )
	: HUSD_CvexResultProcessor<HUSD_VEX_PREC>( data )
	, myRoot( root ), myCurrBinding( nullptr )
    {
	myLeafMap.setSize( myRoot.getIndices().size() );
	for( exint i = 0; i < myLeafMap.size(); i++ )
	    myLeafMap[i] = &myRoot;
    }

    bool partition( const HUSD_CvexBinding &binding ) 
    {
	myCurrBinding = &binding;
	bool ok = processResult( binding.getParmName() );
	myCurrBinding = nullptr;
	return ok;
    }
	
protected:
    #define VAL_PARTITIONER_METHOD(UT_TYPE )			\
    bool processResultData( UT_TYPE d,			        \
	                    const UT_StringRef &n ) override    \
	{ return createSubPartitions( d, n ); }			\

    VAL_PARTITIONER_METHOD( const UT_Array<Int> & )
    VAL_PARTITIONER_METHOD( const UT_Array<Float> & )
    VAL_PARTITIONER_METHOD( const UT_Array<String> & )
    VAL_PARTITIONER_METHOD( const UT_Array<Vec2> & )
    VAL_PARTITIONER_METHOD( const UT_Array<Vec3> & )
    VAL_PARTITIONER_METHOD( const UT_Array<Vec4> & )
    VAL_PARTITIONER_METHOD( const UT_Array<Mat2> & )
    VAL_PARTITIONER_METHOD( const UT_Array<Mat3> & )
    VAL_PARTITIONER_METHOD( const UT_Array<Mat4> & )
    VAL_PARTITIONER_METHOD( const UT_Array<UT_Array<Int>> & )
    VAL_PARTITIONER_METHOD( const UT_Array<UT_Array<Float>> & )
    VAL_PARTITIONER_METHOD( const UT_Array<UT_Array<String>> & )

    bool processResultData( const UT_Array<Dict> &ad,
	                    const UT_StringRef &n ) override
	{ UT_ASSERT(!"Unhandled dictionary types"); return false; }
    bool processResultData( const UT_Array<UT_Array<Dict>> &ad,
	                    const UT_StringRef &n ) override
	{ UT_ASSERT(!"Unhandled dictionary types"); return false; }

    #undef VAL_PARTITIONER_METHOD
    using HUSD_CvexResultProcessor<HUSD_VEX_PREC>::processResultData;

private:
    template<typename T>
    HUSD_PartitionKey 
    getPartitionValueKey( const UT_Array<T> &data, exint i )
    {
	return HUSD_PartitionKey( data[i] );
    }

    template<typename ARR_T>
    bool
    createSubPartitions( const ARR_T &data, const UT_StringRef &data_name)
    { 
	UT_ASSERT( data_name == myCurrBinding->getParmName() );
	const UT_StringHolder &name = myCurrBinding->getAttribName();

	for( exint i = 0; i < data.size(); i++ )
	{
	    // When adding sub-partitions (one for a unique parameter value), 
	    // ensure we have the parameter name as well.
	    if( !myLeafMap[i]->getValueName().isstring() )
		myLeafMap[i]->setValueName( name );
	    UT_ASSERT( myLeafMap[i]->getValueName() == name );

	    HUSD_PartitionKey key( getPartitionValueKey( data, i ));
	    auto &sub_partition = myLeafMap[i]->findOrAddSubPartition( key );
	    sub_partition->addIndex(i);
	    myLeafMap[i] = sub_partition.get();
	}

	return true;
    }

private:
    HUSD_PartitionNode		    &myRoot;	// bucket to partition 
    UT_Array< HUSD_PartitionNode* >  myLeafMap;	// bucket for each value
    const HUSD_CvexBinding	    *myCurrBinding;
};

// ===========================================================================
// Utility functions for setting up and running CVEX code.
static inline CVEX_Function
husdPreloadCvexFnFromCommand( CVEX_ContextT<HUSD_VEX_PREC> &cvex_ctx, const UT_StringRef &cmd,
	UT_StringHolder &error_msg )
{
    UT_String	buff( cmd.buffer() );
    UT_WorkArgs	args;

    buff.parse( args );
    if( args.entries() <= 0 )
    {
	error_msg = "Empty CVEX command";
	return CVEX_Function();
    }

    CVEX_Function func = cvex_ctx.preloadGlobalFunction( args.getArg(0) );
    if( !func.isValid() )
    {
	error_msg = husdGetCvexError( "Error pre-loading", cvex_ctx );
	cvex_ctx.clearFunction( args.getArg(0) );
	return CVEX_Function();
    }

    return func;
}

static inline CVEX_Function
husdPreloadCvexFnFromSourceCode( CVEX_ContextT<HUSD_VEX_PREC> &cvex_ctx, 
	const UT_WorkBuffer &source_code, UT_StringHolder &error_msg )
{
    UT_AutoErrorManager error_scope;
    CVEX_Function func = VCC_Utils::preloadLocalFunction(cvex_ctx, source_code);

    if( !func.isValid() )
    {
	UT_String   msg_str;

	error_scope.getErrorMessages( msg_str );
	error_msg = msg_str;
    }

    return func;
}

template <VEX_Precision PREC>
static inline bool
husdLoadCvexFn( CVEX_ContextT<PREC> &cvex_ctx, CVEX_Function &func,
	const UT_StringRef &command, UT_StringHolder &error_msg )
{
    UT_String	buff( command.buffer() );
    UT_WorkArgs	args;

    buff.parse( args );
    if( args.entries() <= 0 )
    {
	error_msg = "Empty CVEX command";
	return false;
    }

    if( !cvex_ctx.loadFunction( func, 
		args.getArgc(), args.getArgv() ))
    {
	error_msg = husdGetCvexError( "Error loading", cvex_ctx );
	return false;
    }

    return true;
}

static inline UT_StringHolder
husdFindFirstExportOfType( CVEX_Type output_type,
    const UT_StringArray &parm_names, const UT_Array<CVEX_Type> &parm_types,
    const UT_IntArray &parm_exports )
{
    UT_ASSERT( parm_names.size() == parm_types.size() );
    UT_ASSERT( parm_names.size() == parm_exports.size() );

    for( exint i = 0; i < parm_names.size(); i++ )
	if( parm_exports[i] && parm_types[i] == output_type )
	    return parm_names[i];

    return UT_StringHolder();
}

static inline void
husdSetOutputName( HUSD_CvexCodeInfo &code_info,
    const UT_StringArray &parm_names, const UT_Array<CVEX_Type> &parm_types,
    const UT_IntArray &parm_exports )
{
    UT_StringHolder		output_name;
    HUSD_CvexCode::ReturnType	return_type = code_info.getReturnType();

    if( return_type == HUSD_CvexCode::ReturnType::STRING )
    {
	// Try finding the first string type parameter.
	output_name = husdFindFirstExportOfType( CVEX_TYPE_STRING,
		parm_names, parm_types, parm_exports );

	// However, fall back on integer, which can be converted to string.
	if( !output_name.isstring() )
	    output_name = husdFindFirstExportOfType( CVEX_TYPE_INTEGER,
		    parm_names, parm_types, parm_exports );
    }
    else if( return_type == HUSD_CvexCode::ReturnType::BOOLEAN )
    {
	// Output is for boolean selection: use int.
	output_name = husdFindFirstExportOfType( CVEX_TYPE_INTEGER,
		parm_names, parm_types, parm_exports );
    }

    code_info.setOutputName( output_name );
}

static inline UT_StringHolder
husdGetAttribName(const UT_StringRef &parm_name, const HUSD_CvexBindingMap &map)
{
    return map.getAttribFromParm( VOP_Node::decodeVarName( parm_name ));
}

static inline UT_StringHolder
husdGetAttribType(const UT_StringRef &parm_name, const HUSD_CvexBindingMap &map)
{
    return map.getAttribTypeFromParm( VOP_Node::decodeVarName( parm_name ));
}

static inline HUSD_CvexBindingList
husdGetBindingsFromFunction( HUSD_CvexCodeInfo &code_info,
	const CVEX_Function &func, const UT_Array<UsdPrim> &prims,
	const HUSD_CvexBindingMap &map )
{
    HUSD_CvexBindingList    result;
    bool		    is_prims_mode  = code_info.isRunOnPrims();

    UT_ASSERT( func.isValid() );
    if( !func.isValid() )
	return result;

    // Ask the CVEX shader function for its parameters.
    UT_StringArray	parm_names;
    UT_Array<CVEX_Type> parm_types;
    UT_IntArray		parm_exports;
    func.getParameters( parm_names, parm_types, parm_exports );
    UT_ASSERT( parm_names.size() == parm_types.size() );
    UT_ASSERT( parm_names.size() == parm_exports.size() );

    // See if the code is supposed to have only one output parameter.
    UT_StringHolder output_name;
    if( code_info.hasSingleOutput() )
    {
	if( !code_info.getOutputName().isstring() )
	    husdSetOutputName(code_info, parm_names, parm_types, parm_exports);

	output_name = code_info.getOutputName();
    }

    // See which parameters have corresponding attributes among the prims.
    // They will be used as inputs for sure, and some possibly as outputs too.
    UT_BitArray		processed_names( parm_names.size() );
    for( auto && prim : prims )
    {
	for( int i = 0; i < parm_names.size(); i++ )
	{
	    if( processed_names.getBitFast(i) )
		continue;

	    const auto	    &parm_name = parm_names[i];
	    CVEX_Type	     parm_type = parm_types[i];
	    UT_StringHolder  attrib_name = husdGetAttribName( parm_name, map );
	    UT_StringHolder  attrib_type = husdGetAttribType( parm_name, map );

	    auto attrib = husdFindPrimAttrib( prim, attrib_name );
	    bool is_builtin = !attrib && husdIsBuiltin(attrib_name, parm_type);
	    if( !attrib && !is_builtin )
		continue;

	    bool is_output = 
		(!output_name.isstring() || output_name == parm_name)
		? parm_exports[i] 
		: false;
	    
	    bool is_varying = attrib 
		? husdIsAttribVarying( attrib, is_prims_mode )
		: husdIsBuiltinVarying( attrib_name, is_prims_mode );


	    result.append( HUSD_CvexBinding( 
			attrib_name, attrib_type, parm_name, parm_type,
			is_varying, true, is_output, is_builtin ));
	    processed_names.setBitFast( i, true );
	}

	// If we processed all possible parameter names, then we are done.
	if( processed_names.allBitsSet() )
	    break;
    }

    // One last pass to add any remaining outputs to the binding list;
    // such bindings are for attributes yet to be created (ie, output-only).
    if( !processed_names.allBitsSet() )
    {
	for( int i = 0; i < parm_names.size(); i++ )
	{
	    if( !parm_exports[i] || processed_names.getBitFast(i) )
		continue;

	    const auto	    &parm_name = parm_names[i];
	    CVEX_Type	     parm_type = parm_types[i];
	    UT_StringHolder  attrib_name = husdGetAttribName( parm_name, map );
	    UT_StringHolder  attrib_type = husdGetAttribType( parm_name, map );

	    if( !attrib_name.isstring() && parm_name == output_name )
		attrib_name = output_name;
	    if( !attrib_name.isstring() )
		continue;   // nothing to bind to

	    bool is_builtin = husdIsBuiltin( attrib_name, parm_type );
	    is_builtin = false;
	    if( is_builtin )
		continue;   // currently we don't write to any builtins

	    // Varying arbitrarily set to true, since it's not used for outputs.
	    result.append( HUSD_CvexBinding( 
			attrib_name, attrib_type, parm_name, parm_type,
			true, false, true, is_builtin ));
	    processed_names.setBitFast( i, true );
	}
    }

    return result;
}

static inline void
husdAddCvexInputsAndOutputs( CVEX_ContextT<HUSD_VEX_PREC> &ctx, 
	const HUSD_CvexBindingList &bindings )
{
    for( auto &&b: bindings )
    {
	if( b.isInput() )
	    ctx.addInput( b.getParmName(), b.getParmType(), 
		    b.isVarying());
	if( b.isOutput() )
	    ctx.addRequiredOutput( b.getParmName(), b.getParmType());
    }
}

static inline bool
husdLoadCommand( CVEX_ContextT<HUSD_VEX_PREC> &cvex_ctx, const HUSD_CvexCodeInfo &code_info,
	const HUSD_CvexBindingList &bindings, UT_StringHolder &error_msg )
{
    const UT_StringHolder &cvex_cmd = code_info.getCode().getSource();

    // Preload a function to know its parameters for adding inputs/outputs.
    CVEX_Function func = 
	husdPreloadCvexFnFromCommand( cvex_ctx, cvex_cmd, error_msg );
    if( !func.isValid() )
	return false;

    // Declare inputs and outputs.
    husdAddCvexInputsAndOutputs( cvex_ctx, bindings );

    // Load the cvex, which optimizes the code.
    return husdLoadCvexFn( cvex_ctx, func, cvex_cmd, error_msg );
}

static inline void
husdWrapVexpression(UT_WorkBuffer &source_code, const HUSD_CvexCode &code,
	const char *function_name, const char *result_parm_name, int node_id)
{
    UT_WorkBuffer vexpr_buff;
    UT_String	  export_vars;

    UT_String vexpr_str( code.getSource() );
    vexpr_str.trimBoundingSpace();

    if( code.getReturnType() == HUSD_CvexCode::ReturnType::NONE )
    {
	vexpr_buff.append( vexpr_str );
	export_vars = code.getExportsPattern();
    }
    else
    {
	UT_WorkBuffer result_parm_name_str;
	if( code.getReturnType() == HUSD_CvexCode::ReturnType::BOOLEAN )
	    result_parm_name_str.append('i');
	else // HUSD_CvexCode::ReturnType::STRING 
	    result_parm_name_str.append('s');

	result_parm_name_str.append('@');
	result_parm_name_str.append( result_parm_name );
	result_parm_name_str.append( " = " );

	if( vexpr_str.findWord( "return" ))
	{
	    vexpr_str.changeWord( "return", result_parm_name_str.buffer() );
	    vexpr_buff.append( vexpr_str );
	}
	else
	{
	    vexpr_buff.append( result_parm_name_str );
	    vexpr_buff.append( vexpr_str );
	    if( vexpr_buff.last() != ';' )
		vexpr_buff.append(';');
	}

	export_vars = result_parm_name;
    }

    UT_String full_path;
    OP_Node *node = OP_Node::lookupNode( node_id );
    if( node )
	node->getFullPath( full_path );

    source_code.strcpy( VOP_Snippet::buildOuterCode(
		"cvex", function_name, "_bound_", vexpr_buff.buffer(),
		"",			// inputs
		export_vars,		// export vars
		true,			// top level
		false,			// strict bindings: false - allow ':'
		false,			// only standard chars in var names
		full_path.isstring(),	// line hints
		full_path,		// owner for line hints
		VOP_Language::getVex(),
		nullptr			// globals
		));
}

static constexpr auto	HUSD_VEXPR_FN_NAME	= "vexpression_code";
static constexpr auto	HUSD_VEXPR_RESULT_NAME	= "_result_";

static inline bool
husdLoadVexpression( CVEX_ContextT<HUSD_VEX_PREC> &cvex_ctx, 
	const HUSD_CvexCodeInfo &code_info,
	const HUSD_CvexBindingList &bindings, int node_id, 
	UT_StringHolder &error_msg )
{
    // Construct the proper shader source code out of vexpression.
    UT_WorkBuffer source_code;
    husdWrapVexpression( source_code, code_info.getCode(), 
	    HUSD_VEXPR_FN_NAME, HUSD_VEXPR_RESULT_NAME, node_id );

    // Preload a function to know its parameters for adding inputs/outputs.
    CVEX_Function func = 
	husdPreloadCvexFnFromSourceCode( cvex_ctx, source_code, error_msg );
    if( !func.isValid() )
	return false;

    // Declare inputs and outupts.
    husdAddCvexInputsAndOutputs( cvex_ctx, bindings );

    // Load the final function entry point with arguments.
    return husdLoadCvexFn( cvex_ctx, func, HUSD_VEXPR_FN_NAME, error_msg );
}

static inline bool
husdLoadCode( CVEX_ContextT<HUSD_VEX_PREC> &cvex_ctx, const HUSD_CvexCodeInfo &code_info,
	const HUSD_CvexBindingList &bindings, int node_id,
	UT_StringHolder &error_msg)
{
    if( code_info.isCommand() )
	return husdLoadCommand( cvex_ctx, code_info, bindings, error_msg );
    else
	return husdLoadVexpression( cvex_ctx, code_info, bindings, node_id, 
		error_msg );

    return false;
}

static inline HUSD_CvexBindingList
husdGetBindingsFromCommand( HUSD_CvexCodeInfo &code_info,
	const HUSD_CvexBindingMap &map, int node_id,
	const UT_Array<UsdPrim> &prims, UT_StringHolder &error_msg )
{
    // Obtain the CVEX function object.
    CVEX_ContextT<HUSD_VEX_PREC>	     cvex_ctx;
    const UT_StringHolder   &cvex_cmd = code_info.getCode().getSource();
    CVEX_Function func = 
	husdPreloadCvexFnFromCommand( cvex_ctx, cvex_cmd, error_msg );
    if( !func.isValid() )
	return HUSD_CvexBindingList();

    // See which parameters have corresponding attributes among the prims.
    // Note, the output name of the code_info may also be set!!!
    return husdGetBindingsFromFunction( code_info, func, prims, map );
}

static inline HUSD_CvexBindingList
husdGetBindingsFromVexpression( HUSD_CvexCodeInfo &code_info,
	const HUSD_CvexBindingMap &map, int node_id,
	const UT_Array<UsdPrim> &prims, UT_StringHolder &error_msg )
{
    // Obtain the CVEX function object.
    CVEX_ContextT<HUSD_VEX_PREC>	     cvex_ctx;

    UT_WorkBuffer source_code;
    husdWrapVexpression( source_code, code_info.getCode(), 
	    HUSD_VEXPR_FN_NAME, HUSD_VEXPR_RESULT_NAME, node_id );

    CVEX_Function func = 
	husdPreloadCvexFnFromSourceCode( cvex_ctx, source_code, error_msg );
    if( !func.isValid() )
	return HUSD_CvexBindingList();

    // See which parameters have corresponding attributes among the prims.
    code_info.setOutputName( HUSD_VEXPR_RESULT_NAME );
    return husdGetBindingsFromFunction( code_info, func, prims, map );
}

static inline HUSD_CvexBindingList
husdGetBindings( HUSD_CvexCodeInfo &code,
	const HUSD_CvexBindingMap &map, int node_id,
	const UT_Array<UsdPrim> &prims, UT_StringHolder &err )
{
    if( code.isCommand() )
    {
	return husdGetBindingsFromCommand( code, map, node_id, prims, err );
    }
    else
    {
	return husdGetBindingsFromVexpression( code, map, node_id, prims, err );
    }

    return HUSD_CvexBindingList();
}

// ===========================================================================
// Utility functions for reporting errors and warnings.
static inline void 
husdAddErrorOrWarning(int node_id, const char *message, bool is_error)
{
    OP_Node *node = OP_Node::lookupNode( node_id );
    if( !node )
	return;

    UT_WorkBuffer node_path;
    node->getFullPath(node_path);

    UT_WorkBuffer buf;
    buf.sprintf("%s : %s", node_path.buffer(), message);

    if( is_error )
	HUSD_ErrorScope::addError(HUSD_ERR_STRING, buf.buffer());
    else
	HUSD_ErrorScope::addWarning(HUSD_ERR_STRING, buf.buffer());
}

static inline void 
husdAddError(int node_id, const char *message)
{
    husdAddErrorOrWarning( node_id, message, true );
}

static inline void 
husdAddWarning(int node_id, const char *message)
{
    husdAddErrorOrWarning( node_id, message, false );
}

static inline void 
husdAddBindWarning(int node_id, const UT_SortedStringSet &bad_attribs )
{
    UT_WorkBuffer   msg;
    bool	    first = true;

    // The attribute did not exist or the type did not match the CVEX parameter.
    msg.append(
	"Could not bind VEX parameters to USD attributes for some primitives.\n"
	"Attributes are missing or have incompatible type:\n");
    for( auto &&bad_attrib : bad_attribs )
    {
	if( !first )
	    msg.append(", ");
	msg.append( bad_attrib );
	first = false;
    }

    husdAddWarning( node_id, msg.buffer() );
}

// ===========================================================================
// Runs the cvex code in a threaded fashion.
class HUSD_ThreadedExec
{
public:
    /// Constructor
		HUSD_ThreadedExec( const HUSD_CvexCodeInfo &code_info,
			const HUSD_CvexRunData &rundata,
			const HUSD_CvexDataBinder &input_data_binder,
			const HUSD_CvexDataRetriever &output_data_retriever,
			const HUSD_CvexBindingList &bindings );

    /// Run the CVEX program on the data supplied to the constructor.
    bool	runCvex();

    /// Returns the maximum sampling level of any attribute data bound 
    /// in the course of running the CVEX program.
    HUSD_TimeSampling getTimeSampling() const;

private:
    // Sets up the threading methods
    THREADED_METHOD( HUSD_ThreadedExec, shouldMultithread(), doRunCvex )
    void	doRunCvexPartial( const UT_JobInfo &info );

    /// Helper function that returns the next block to process within
    /// the total data array.
    bool	getNextBlock( exint &block_start, exint &block_end, 
		    const UT_JobInfo &info ) const;

    /// Run CVEX program on the block of data.
    bool	processBlock( CVEX_ContextT<HUSD_VEX_PREC> &cvex_ctx, 
		    CVEX_RunData &cvex_rundata,
		    CVEX_InOutData &storage, 
		    exint block_start, exint block_end );

    /// Reports any errors and warnings.
    bool	checkErrorsAndWarnings();

    /// Retuns true if multi-threading should be engaged.
    bool	shouldMultithread() const;

private:
    /// Thread-specific data. Threads will update this data while running.
    struct ThreadData
    {
	// Maximum level of sampling among bound attributes:
	HUSD_TimeSampling	myTimeSampling = HUSD_TimeSampling::NONE;
	UT_SortedStringSet	myBadAttribs;	// What didn't bind cleanly?
	UT_StringHolder		myExecError;	// Any code execution error?
	UT_StringHolder		myExecWarning;	// Any code execution warning?
    };

private:
    const HUSD_CvexCodeInfo		&myCodeInfo;
    const HUSD_CvexRunData		&myUsdRunData;
    const HUSD_CvexDataBinder		&myInputDataBinder;
    const HUSD_CvexDataRetriever	&myOutputDataRetriever;
    const HUSD_CvexBindingList		&myBindings;
    UT_ThreadSpecificValue<ThreadData>	 myThreadData;
};

HUSD_ThreadedExec::HUSD_ThreadedExec( const HUSD_CvexCodeInfo &code_info,
	const HUSD_CvexRunData &rundata,
	const HUSD_CvexDataBinder &input_data_binder,
	const HUSD_CvexDataRetriever &output_data_retriever,
	const HUSD_CvexBindingList &bindings )
    : myCodeInfo( code_info )
    , myUsdRunData( rundata )
    , myBindings( bindings )
    , myInputDataBinder( input_data_binder )
    , myOutputDataRetriever( output_data_retriever )
{
}

bool
HUSD_ThreadedExec::shouldMultithread() const
{ 
    // There is some cost to starting up the threads, but the exact payoff
    // depends on the data buffer size, the nature of the CVEX program
    // computations, and the thread count availability (usually decent these
    // days).  Using an arbitrary metric of 5 blocks running in parallel 
    // compensating for the threading startup (similar to SOP_AttribVop).
    exint total_data_size = myOutputDataRetriever.getResultDataSize();
    return total_data_size >= 5 * HUSD_CVEX_DATA_BLOCK_SIZE;
}

bool
HUSD_ThreadedExec::runCvex()
{
    // Ensure there is a queue for each thread.
    int thread_count = shouldMultithread() ? UT_Thread::getNumProcessors() : 1;
    if( myUsdRunData.getDataCommand() )
	myUsdRunData.getDataCommand()->setCommandQueueCount( thread_count );

    // The following call will run in threads if needed.
    doRunCvex();

    return checkErrorsAndWarnings();
}

bool
HUSD_ThreadedExec::checkErrorsAndWarnings()
{
    // Collect issues from all threads.
    UT_SortedStringSet    unique_exec_errors;
    UT_SortedStringSet    unique_exec_warnings;
    UT_SortedStringSet    unique_bad_attribs;

    for( auto it = myThreadData.begin(); it != myThreadData.end(); ++it )
    {
	UT_StringHolder	&exec_error = it.get().myExecError;
	if( !exec_error.isEmpty() )
	    unique_exec_errors.insert( exec_error );

	UT_StringHolder	&exec_warning = it.get().myExecWarning;
	if( !exec_warning.isEmpty() )
	    unique_exec_warnings.insert( exec_warning );

	UT_SortedStringSet &bad_attribs = it.get().myBadAttribs;
	if( !bad_attribs.empty() )
	    unique_bad_attribs |= bad_attribs;
    }

    bool ok = (unique_exec_errors.size() == 0);

    // Report errors.
    for( auto &&err : unique_exec_errors )
	husdAddError( myUsdRunData.getCwdNodeId(), err );

    // Report warnings.
    for( auto &&warn : unique_exec_warnings )
	husdAddWarning( myUsdRunData.getCwdNodeId(), warn );

    // Report bad attributes, but only if there are no errors.
    if( ok && !unique_bad_attribs.empty() )
	husdAddBindWarning( myUsdRunData.getCwdNodeId(), unique_bad_attribs );

    return ok;
}

void
HUSD_ThreadedExec::doRunCvexPartial( const UT_JobInfo &info ) 
{
    // Set up the cvex run data.
    CVEX_RunData	cvex_rundata;
    cvex_rundata.setCWDNodeId(myUsdRunData.getCwdNodeId());
    cvex_rundata.setOpCaller(myUsdRunData.getOpCaller());
    cvex_rundata.setGeoInputs(&myUsdRunData.getDataInputs());
    cvex_rundata.setTime(myUsdRunData.getTimeCode().time());

    // Set the command queue for this thread.
    UT_ExintArray	proc_ids;
    if( myUsdRunData.getDataCommand() )
    {
	proc_ids.setSize( HUSD_CVEX_DATA_BLOCK_SIZE );
	cvex_rundata.setProcId( proc_ids.data() );
	cvex_rundata.setGeoCommandQueue( 
		&myUsdRunData.getDataCommand()->getCommandQueue( info.job() ));
    }
   
    // Prepare CVEX context: add inputs/outputs and load code. 
    // We'll perform late binding in loop later, when processing each block.
    CVEX_ContextT<HUSD_VEX_PREC>	cvex_ctx;
    int			node_id = myUsdRunData.getCwdNodeId();
    if( !husdLoadCode( cvex_ctx, myCodeInfo, myBindings, node_id,
		myThreadData.get().myExecError ))
    {
	return;
    }

    // Loop thru buffer blocks and process the next available one.
    CVEX_InOutData	storage;
    exint		block_start = 0;
    exint		block_end   = 0;
    while( getNextBlock( block_start, block_end, info ))
    {
	// Note, cvex_rundata keeps a pointer to proc_ids, so it gets 
	// updated values without the need to call setProcId() again.
	if( myUsdRunData.getDataCommand() )
	    for( exint i = block_start; i < block_end; i++ )
		proc_ids[ i - block_start ] = i;

	// Set up stuff and run cvex on the block of data.
	if( !processBlock( cvex_ctx, cvex_rundata, 
		storage, block_start, block_end ))
	    break;
    }
}

bool
HUSD_ThreadedExec::processBlock( CVEX_ContextT<HUSD_VEX_PREC> &cvex_ctx,
	CVEX_RunData &cvex_rundata, CVEX_InOutData &storage,
	exint block_start, exint block_end ) 
{
    // Bind inputs to the cvex values. Use the storage's input data buffers
    // to hold data. The binder will draw the data from USD attributes.
    auto status = myInputDataBinder.bind( cvex_ctx, storage.getInputData(), 
	    myBindings, block_start, block_end );

    // Update info obtained from the bind call.
    husdUpdateTimeSampling( myThreadData.get().myTimeSampling,
	    status.getTimeSampling() );
    for( auto &&bad_attrib : status.getBadAttribs() )
	myThreadData.get().myBadAttribs.insert( bad_attrib );

    // Bind output buffers, resetting them before next cvex run.
    husdBindOutputs( cvex_ctx, storage.getOutputData(), myBindings, 
	    block_end - block_start );

    // Run the CVEX code over the block.
    if( !cvex_ctx.run( block_end - block_start, true, &cvex_rundata ))
    {
	myThreadData.get().myExecError = 
	    husdGetCvexError( "Error executing", cvex_ctx );
	return false;
    }

    // Just because execution succeeded, doesn't guarantee there aren't
    // any errors. The VEX code may have run the error() function.
    myThreadData.get().myExecError = cvex_ctx.getVexErrors();
    myThreadData.get().myExecWarning = cvex_ctx.getVexWarnings();

    // Some VEX function calls may have accessed time-varying attributes.
    husdUpdateIsTimeSampled( myThreadData.get().myTimeSampling,
	    cvex_rundata.isTimeSampleEncountered() );
    husdUpdateIsTimeVarying( myThreadData.get().myTimeSampling,
	    cvex_rundata.isTimeDependent() );

    // Retrieve the computed output data and add it to the final buffer.
    return myOutputDataRetriever.transferResultData( storage.getOutputData(), 
	    myBindings, block_start, block_end );
}

bool
HUSD_ThreadedExec::getNextBlock( exint &block_start, exint &block_end, 
	const UT_JobInfo &info ) const
{
    exint   total_data_size = myOutputDataRetriever.getResultDataSize();
    exint   block_data_size = HUSD_CVEX_DATA_BLOCK_SIZE;

    block_start = info.nextTask() * block_data_size;
    block_end   = SYSmin( block_start + block_data_size, total_data_size );
    return block_start < total_data_size;
}

HUSD_TimeSampling
HUSD_ThreadedExec::getTimeSampling() const
{
    HUSD_TimeSampling sampling = HUSD_TimeSampling::NONE;
    for( auto it = myThreadData.begin(); it != myThreadData.end(); ++it )
	husdUpdateTimeSampling( sampling, it.get().myTimeSampling );
    return sampling;
}

static inline bool
husdRunCvex( const HUSD_CvexCodeInfo &code_info,
	const HUSD_CvexRunData &usd_rundata,
	const HUSD_CvexDataBinder &input_data_binder,
	const HUSD_CvexDataRetriever &output_data_retriever, 
	const HUSD_CvexBindingList &bindings,
	HUSD_TimeSampling &time_sampling )
{
    HUSD_ThreadedExec exec( code_info, usd_rundata, input_data_binder, 
			    output_data_retriever, bindings );

    if( !exec.runCvex() )
	return false;

    husdUpdateTimeSampling( time_sampling, exec.getTimeSampling() );
    return true;
}


// ===========================================================================
class HUSD_PrimAttribData
{
public:
		HUSD_PrimAttribData( const UT_Array<UsdPrim> &prims,
			const HUSD_CvexBindingList &bindings,
			const HUSD_TimeCode &time_code );

    /// Run CVEX program on the data.
    bool	runCvex(const HUSD_CvexCodeInfo &code_info,
			const HUSD_CvexRunData &usd_rundata,
			const HUSD_CvexBindingList &bindings );

    /// Returns max level of sampling of any attribute bound during the run.
    HUSD_TimeSampling		getTimeSampling() const
				    { return myTimeSampling; }

    /// Returns the data resulting from running CVEX program (ie, its outputs).
    const HUSD_CvexResultData &	getResult() const 
				    { return myResultData; }

private:
    HUSD_PrimAttribDataBinder	myInputBinder;
    HUSD_CvexResultData		myResultData;
    HUSD_CvexDataRetriever	myResultRetriever;
    HUSD_TimeSampling		myTimeSampling;
};

HUSD_PrimAttribData::HUSD_PrimAttribData( const UT_Array<UsdPrim> &prims,
	const HUSD_CvexBindingList &bindings,
	const HUSD_TimeCode &time_code )
    : myInputBinder( prims, time_code )
    , myResultData( prims.size(), bindings )
    , myResultRetriever( myResultData )
    , myTimeSampling( HUSD_TimeSampling::NONE )
{
}

bool
HUSD_PrimAttribData::runCvex( const HUSD_CvexCodeInfo &code_info,
	const HUSD_CvexRunData &usd_rundata,
	const HUSD_CvexBindingList &bindings )
{
    return husdRunCvex( code_info, usd_rundata, 
	    myInputBinder, myResultRetriever, bindings, 
	    myTimeSampling );
}

// ===========================================================================
class HUSD_ArrayElementData
{
public:
    /// Holds data when running VEX onarray elements.
		HUSD_ArrayElementData( 
			const UsdPrim &prim, const UT_ExintArray *face_indices,
			exint size_hint,
			const HUSD_CvexBindingList &bindings,
			const HUSD_TimeCode &time_code );

    /// Run CVEX program on the data.
    bool	runCvex(const HUSD_CvexCodeInfo &code_info,
			const HUSD_CvexRunData &usd_rundata,
			const HUSD_CvexBindingList &bindings );

    /// Returns max level of sampling of any attribute bound during the run.
    HUSD_TimeSampling		getTimeSampling() const
				    { return myData.myTimeSampling; }

    /// Returns the data resulting from running CVEX program (ie, its outputs).
    const HUSD_CvexResultData &	getResult() const 
				    { return myData.myResultData; }

private:
    /// The construction of two members depends on array size, thus using
    /// this helper class to abstract the calculation of the array size.
    class Data
    {
    public:
	Data( exint array_size,
		const UsdPrim &prim, const UT_ExintArray *face_indices,
		const HUSD_CvexBindingList &bindings,
		const HUSD_TimeCode &time_code );

    public:
	HUSD_ArrayElementDataBinder	myInputBinder;
	HUSD_CvexResultData		myResultData;
	HUSD_CvexDataRetriever		myResultRetriever;
	HUSD_TimeSampling		myTimeSampling;
    };

private:
    Data				myData;
};

HUSD_ArrayElementData::Data::Data( exint array_size,
	const UsdPrim &prim, const UT_ExintArray *face_indices,
	const HUSD_CvexBindingList &bindings,
	const HUSD_TimeCode &time_code )
    : myInputBinder( array_size, prim, face_indices, time_code )
    , myResultData( array_size, bindings )
    , myResultRetriever( myResultData )
    , myTimeSampling( HUSD_TimeSampling::NONE )
{
}

HUSD_ArrayElementData::HUSD_ArrayElementData(
	const UsdPrim &prim, const UT_ExintArray *face_indices,
	exint size_hint,
	const HUSD_CvexBindingList &bindings,
	const HUSD_TimeCode &time_code )
    : myData( HUSD_ArrayElementDataBinder::findArraySize( prim, face_indices, 
		size_hint, bindings, time_code ),
	    prim, face_indices, bindings, time_code )
{
}

bool
HUSD_ArrayElementData::runCvex( const HUSD_CvexCodeInfo &code_info,
	const HUSD_CvexRunData &usd_rundata,
	const HUSD_CvexBindingList &bindings )
{
    // CVEX will be executed 1k on elements at a time. For each such block,
    // a different (1k sized) portion of the *same* array attribute will be 
    // copied to the CVEX buffer. If we don't cache the array attribute, 
    // we will keep asking USD for the same (large!) array attribute many times.
    // So, we prefetch the arrays to avoid repeated work and slowdowns.
    myData.myInputBinder.prefetchAttribValues( bindings );

    return husdRunCvex( code_info, usd_rundata, 
	    myData.myInputBinder, myData.myResultRetriever, bindings, 
	    myData.myTimeSampling );
}

// ===========================================================================
// Stores the results from running VEX code so that we can perform the
// application of this data back to the USD separately from the process
// of calculating these results.
class husd_CvexResults
{
public:
    UT_Array<UsdPrim>                        myPrims;
    HUSD_CvexBindingList                     myBindings;
    UT_UniquePtr<HUSD_PrimAttribData>        myPrimData;
    UT_UniquePtr<HUSD_ArrayElementData>      myArrayData;
};

// ===========================================================================
HUSD_Cvex::HUSD_Cvex()
    : myRunData(new HUSD_CvexRunData())
    , myTimeSampling( HUSD_TimeSampling::NONE )
{
}

HUSD_Cvex::~HUSD_Cvex()
{
}

void
HUSD_Cvex::setCwdNodeId( int cwd_node_id ) 
{ 
    myRunData->setCwdNodeId( cwd_node_id );
}
    
void
HUSD_Cvex::setOpCaller( UT_OpCaller *caller ) 
{ 
    myRunData->setOpCaller( caller );
}
    
void
HUSD_Cvex::setTimeCode( const HUSD_TimeCode &time_code )
{ 
    myRunData->setTimeCode(  time_code );
}

void
HUSD_Cvex::setBindingsMap( const HUSD_CvexBindingMap *map )
{
    myRunData->setBindingsMap( map );
}

void
HUSD_Cvex::setArraySizeHintAttrib( const UT_StringRef &attrib_name )
{
    myArraySizeHintAttrib = attrib_name;
}

void
HUSD_Cvex::setDataInputs( HUSD_CvexDataInputs *vex_geo_inputs )
{ 
    myRunData->setDataInputs( vex_geo_inputs );
}

void
HUSD_Cvex::setDataCommand( HUSD_CvexDataCommand *vex_geo_command )
{ 
    myRunData->setDataCommand( vex_geo_command );
}

static inline UT_Array<UsdPrim>
husdGetReadOnlyPrims( HUSD_AutoAnyLock &lock, const HUSD_FindPrims &findprims )
{
    UT_Array<UsdPrim>	result;

    // Find out the primitives over which to run the cvex.
    const XUSD_ConstDataPtr &data = lock.constData();
    if( !data || !data->isStageValid() )
	return result;

    UsdStageRefPtr stage = data->stage();
    const XUSD_PathSet &sdfpaths = findprims.getExpandedPathSet().sdfPathSet();

    result.setCapacity( sdfpaths.size() );
    for( auto &&sdfpath : sdfpaths )
    {
	auto usdprim = stage->GetPrimAtPath( sdfpath );
	if( usdprim.IsValid() )
	    result.append( usdprim );
    }

    return result;
}

static inline bool
husdGetBindingsAndOutputs( 
	HUSD_CvexBindingList &bindings, HUSD_CvexCodeInfo &code_info,
	const HUSD_CvexRunData &usd_rundata, const UT_Array<UsdPrim> &prims )
{
    UT_StringHolder	 error_msg;

    // Obtains  bindings and updates code_info with an output name, if needed.
    bindings = husdGetBindings( code_info, usd_rundata.getBindingMap(), 
	    usd_rundata.getCwdNodeId(), prims, error_msg );

    bool ok = error_msg.isEmpty();
    if( !ok )
	husdAddError( usd_rundata.getCwdNodeId(), error_msg );

    return ok;
}

static inline void 
husdAddAttribError( int node_id, const UT_StringArray &bad_attribs )
{
    UT_WorkBuffer   msg;
    bool	    first = true;

    msg.sprintf("Could not set attribute (incompatible types): " );
    for( auto &&bad_attrib : bad_attribs )
    {
	if( !first )
	    msg.append(", ");
	msg.append( bad_attrib );
	first = false;
    }

    husdAddError( node_id, msg.buffer() );
}

template<typename SETTER, typename PRIM_T>
bool
husdSetAttributes( PRIM_T &prims, 
	const HUSD_CvexRunData &usd_rundata,
	const HUSD_CvexResultData &result_data, 
	const HUSD_CvexBindingList &bindings, 
	HUSD_TimeSampling time_sampling)
{
    HUSD_TimeCode   time_code = usd_rundata.getEffectiveTimeCode(time_sampling);
    SETTER	    retriever( result_data, prims, time_code );
    UT_StringArray  bad_attribs;

    
    for( auto &&binding : bindings )
    {
	if( !binding.isOutput() || binding.isBuiltin() )
	    continue; // currently we don't write out to built-ins

	if( !retriever.setAttrib( binding ))
	    bad_attribs.append( binding.getAttribName() );
    }

    if( !bad_attribs.isEmpty() )
    {
	int node_id   = usd_rundata.getCwdNodeId();
	husdAddAttribError( node_id, bad_attribs );
        return false;
    }

    return true;
}

static inline void
husdApplyDataCommands( HUSD_AutoWriteLock &writelock,
	const HUSD_CvexRunData &usd_rundata,
	HUSD_TimeSampling time_sampling)
{
    HUSD_TimeCode time_code = usd_rundata.getEffectiveTimeCode( time_sampling );

    // Apply the edit commands that were queued up.
    if( usd_rundata.getDataCommand() )
	usd_rundata.getDataCommand()->apply(writelock, time_code);
}

bool
HUSD_Cvex::runOverPrimitives( HUSD_AutoAnyLock &lock,
        const HUSD_FindPrims &findprims,
	const UT_StringRef &cvex_cmd ) const
{
    // Find out the primitives over which to run the cvex.
    myResults.append(UTmakeUnique<husd_CvexResults>());
    husd_CvexResults &result = *myResults.last();
    result.myPrims = husdGetReadOnlyPrims(lock, findprims);
    // If there are no prims to run over, we want to delete this result so
    // we don't try to apply any changes from it later. But this still
    // counts as a successful run.
    if( result.myPrims.size() == 0 )
    {
        myResults.clear();
	return true;
    }

    HUSD_CvexCode	code(cvex_cmd);
    HUSD_CvexCodeInfo	code_info(code, /*run_on_prims=*/ true);
    HUSD_CvexRunData::FallbackLockBinder binder(*myRunData, lock);

    // Find the bindings between primitive attribs and cvex function parms.
    if( !husdGetBindingsAndOutputs(result.myBindings,
            code_info, *myRunData, result.myPrims))
	return false;

    // Create data object and run CVEX code on it.
    result.myPrimData.reset(new HUSD_PrimAttribData(
        result.myPrims,
        result.myBindings,
        myRunData->getTimeCode()));
    if( !result.myPrimData->runCvex( code_info,
            *myRunData, result.myBindings ))
	return false;

    husdUpdateTimeSampling(myTimeSampling,result.myPrimData->getTimeSampling());
    return true;
}

bool
HUSD_Cvex::applyRunOverPrimitives(HUSD_AutoWriteLock &writelock) const
{
    const XUSD_DataPtr &data = writelock.data();
    if (!data || !data->isStageValid())
	return false;

    UsdStageRefPtr stage = data->stage();
    if (!stage)
        return false;

    HUSD_CvexRunData::FallbackLockBinder binder(*myRunData, writelock);
    HUSD_TimeSampling time_sampling = HUSD_TimeSampling::NONE;
    bool ok = true;


    // Set the computed attributes on the primitives.
    for (auto &&result : myResults)
    {
        UT_Array<UsdPrim> writableprims;

        for (auto &&prim : result->myPrims)
        {
            UsdPrim writableprim=stage->GetPrimAtPath(prim.GetPath());

            if (writableprim)
                writableprims.append(writableprim);
        }
        ok &= husdSetAttributes<HUSD_AttribSetter>(
            writableprims, 
            *myRunData,
            result->myPrimData->getResult(),
            result->myBindings,
            result->myPrimData->getTimeSampling());

	HUSDupdateTimeSampling( time_sampling, 
            result->myPrimData->getTimeSampling());
    }

    // To be consistent with SOP wrangles and SOP attribute vop nodes,
    // we process the commands last (after the export variables).
    // This has impact on code like this:
    //	    usd_setattrib(0, @primpath, "foo", 2);
    //	    @foo = 1;
    // where the usd_setattrib() function call will take precedence 
    // and 'foo' attrib will be set to 2.
    //
    // Call it outside the loop, because data commands should be applied
    // only once.
    husdApplyDataCommands( writelock, *myRunData, time_sampling );

    return ok;
}

static inline exint
husdGetArraySizeHint( const UsdPrim &prim, const UT_StringRef &attrib_name,
	const HUSD_TimeCode &time_code )
{
    if( !prim.IsValid() || attrib_name.isEmpty() )
	return 0;

    auto attrib = husdFindPrimAttrib( prim, attrib_name );
    if( !attrib )
	return 0;

    VtValue value;
    attrib.Get(&value, HUSDgetNonDefaultUsdTimeCode( time_code ));
    return value.GetArraySize();
}

bool
HUSD_Cvex::runOverArrayElements( HUSD_AutoAnyLock &lock,
        const HUSD_FindPrims &findprims,
	const UT_StringRef &cvex_cmd) const
{
    // Find out the primitives over which to run the cvex.
    UT_Array<UsdPrim> prims = husdGetReadOnlyPrims( lock, findprims );
    if( prims.size() == 0 )
	return true;

    HUSD_CvexRunData::FallbackLockBinder binder(*myRunData, lock);
    for( auto &&prim : prims )
    {
        myResults.append(UTmakeUnique<husd_CvexResults>());
        husd_CvexResults &result = *myResults.last();
        result.myPrims.append(prim);

        HUSD_CvexCode	         code(cvex_cmd);
        HUSD_CvexCodeInfo        code_info(code, /*run_on_prims=*/ false);

        // Find the bindings between primitive attribs and cvex function parms.
        if( !husdGetBindingsAndOutputs(result.myBindings,
                code_info, *myRunData, result.myPrims))
            return false;

	exint size_hint = husdGetArraySizeHint(prim, myArraySizeHintAttrib,
		myRunData->getTimeCode());

        // Create data object and run CVEX code on it.
        result.myArrayData.reset(new HUSD_ArrayElementData(
            prim,
            nullptr,
            size_hint,
            result.myBindings,
            myRunData->getTimeCode()));
        if( !result.myArrayData->runCvex(code_info,
                *myRunData, result.myBindings))
            return false;

	husdUpdateTimeSampling( myTimeSampling, 
		result.myArrayData->getTimeSampling() );
    }

    return true;
}

bool
HUSD_Cvex::applyRunOverArrayElements(HUSD_AutoWriteLock &writelock) const
{
    const XUSD_DataPtr &data = writelock.data();
    if (!data || !data->isStageValid())
	return false;

    UsdStageRefPtr stage = data->stage();
    if (!stage)
        return false;

    HUSD_CvexRunData::FallbackLockBinder binder(*myRunData, writelock);
    HUSD_TimeSampling time_sampling = HUSD_TimeSampling::NONE;
    bool ok = true;

    // Set the computed array attributes on the primitive.
    for (auto &&result : myResults)
    {
        UsdPrim writableprim=stage->GetPrimAtPath(result->myPrims(0).GetPath());

        if (writableprim)
            ok &= husdSetAttributes<HUSD_ArraySetter>(
                writableprim,
                *myRunData,
                result->myArrayData->getResult(),
                result->myBindings,
                result->myArrayData->getTimeSampling());

	HUSDupdateTimeSampling( time_sampling, 
            result->myArrayData->getTimeSampling());
    }

    // To be consistent with SOP wrangles and SOP attribute vop nodes,
    // we process the commands last (after the export variables).
    // This has impact on code like this:
    //	    usd_setattrib(0, @primpath, "foo", 2);
    //	    @foo = 1;
    // where the usd_setattrib() function call will take precedence 
    // and 'foo' attrib will be set to 2.
    //
    // Call it outside the loop, because data commands should be applied
    // only once.
    husdApplyDataCommands( writelock, *myRunData, time_sampling );

    return ok;
}

static inline UT_Array<UsdPrim>
husdGetPrims(HUSD_AutoAnyLock &lock,
	HUSD_PrimTraversalDemands demands,
        const UT_PathPattern *pruning_pattern)
{
    UT_Array<UsdPrim>		 result;
    Usd_PrimFlagsPredicate	 predicate(HUSDgetUsdPrimPredicate(demands));

    const XUSD_ConstDataPtr &data = lock.constData();
    if( !data || !data->isStageValid() )
	return result;

    UsdStageRefPtr stage = data->stage();
    UsdPrim root = stage->GetPseudoRoot();

    if (root)
    {
        XUSD_FindUsdPrimsTaskData data;
        auto &task = *new(UT_Task::allocate_root())
            XUSD_FindPrimsTask(root, data, predicate, pruning_pattern, nullptr);
        UT_Task::spawnRootAndWait(task);

        data.gatherPrimsFromThreads(result);
    }

    return result;
}

static inline bool
husdCollectMatchedPrims( UT_StringArray &matched_prims_paths, 
	const HUSD_CvexResultData &data, const char *output_name, 
	const UT_Array<UsdPrim> &prims )
{
    UT_ExintArray prims_indices;
    HUSD_SelectionCollector collector( prims_indices, data, output_name );
    collector.getSelection();

    matched_prims_paths.clear();
    for( auto &&i : prims_indices )
	matched_prims_paths.append( prims[i].GetPath().GetString() );

    return true;
}

bool
HUSD_Cvex::matchPrimitives( HUSD_AutoAnyLock &lock,
        UT_StringArray &matched_prims_paths, 
	const HUSD_CvexCode &code,
	HUSD_PrimTraversalDemands demands,
        const UT_PathPattern *pruning_pattern) const
{
    UT_Array<UsdPrim> prims = husdGetPrims(lock, demands, pruning_pattern);
    if( prims.size() <= 0 )
	return true;	// All good, even though there's no prims to match.
    HUSD_CvexRunData::FallbackLockBinder binder(*myRunData, lock);

    // This code path has not been tested for commands, though it may work.
    UT_ASSERT( !code.isCommand() );
    HUSD_CvexCodeInfo	 code_info( code, /*run_on_prims=*/ true, true );

    // Find the bindings between primitive attribs and cvex function parms.
    HUSD_CvexBindingList bindings;
    if( !husdGetBindingsAndOutputs( bindings, code_info, *myRunData, prims ))
	return false;

    // Create data object and run CVEX code on it.
    HUSD_PrimAttribData	data( prims, bindings, myRunData->getTimeCode() );
    if( !data.runCvex( code_info, *myRunData, bindings ))
	return false;

    husdUpdateTimeSampling( myTimeSampling, data.getTimeSampling() );

    // Retrieve values, analyze the bool output, and fill the paths array.
    return husdCollectMatchedPrims( matched_prims_paths, 
	    data.getResult(), code_info.getOutputName(), prims );
}

template <typename FUNC>
bool
husdPartitionUsingKeyword( const HUSD_CvexResultData &data, 
	const char *output_name, FUNC bucket_creator )
{
    UT_StringMap<UT_ExintArray>  map;
    HUSD_KeywordPartitioner	 partitioner( map, data, output_name );

    partitioner.partition();
    for( auto && it : map )
	bucket_creator( it.first, it.second );

    return true;
}

static inline bool
husdPartitionPrimsDataUsingKeyword( UT_Array<HUSD_PrimsBucket> &buckets,
	const HUSD_CvexResultData &data, const char *output_name, 
	const UT_Array<UsdPrim> &prims )
{
    return husdPartitionUsingKeyword( data, output_name,
	[&](const UT_StringHolder &keyword, const UT_ExintArray &indices)
	{
	    HUSD_PrimsBucket &b = buckets[ buckets.append() ];

	    b.getBucketValue().setKeyword( keyword );
	    b.setPrimIndices( indices );
	    for( exint i : b.getPrimIndices() )
		b.addPrimPath( prims[i].GetPath().GetString() );
	});
}

static inline bool
husdPartitionPrimsUsingKeyword( UT_Array<HUSD_PrimsBucket> &buckets,
	const UT_Array<UsdPrim> &prims, const HUSD_CvexCode &code, 
	const HUSD_CvexRunData &usd_rundata, HUSD_TimeSampling &time_sampling )
{
    HUSD_CvexCodeInfo	 code_info( code, /*run_on_prims=*/ true, true );

    // Find the bindings between primitive attribs and cvex function parms.
    HUSD_CvexBindingList bindings;
    if( !husdGetBindingsAndOutputs( bindings, code_info, usd_rundata, prims ))
	return false;

    // Create data object and run CVEX code on it.
    HUSD_PrimAttribData	data( prims, bindings, usd_rundata.getTimeCode() );
    if( !data.runCvex( code_info, usd_rundata, bindings ))
	return false;

    husdUpdateTimeSampling( time_sampling, data.getTimeSampling() );

    // Retrieve the computed output data.
    return husdPartitionPrimsDataUsingKeyword( buckets,
	    data.getResult(), code_info.getOutputName(), prims );
}

template<typename FUNC>
bool
husdPartitionUsingValues( const HUSD_CvexResultData &data, 
	const HUSD_CvexBindingList &bindings, FUNC bucket_creator )
{
    HUSD_PartitionNode root;
    for( exint i = 0; i < data.getDataSize(); i++ )
	root.addIndex( i );
    
    // Create the partition tree based on the outputs and their values.
    HUSD_ValuePartitioner partitioner( data, root );
    for( auto &&binding : bindings )
    {
	if( !binding.isOutput() || binding.isBuiltin() )
	    continue; // currently we don't write out to built-ins

	partitioner.partition( binding );
    }
    
    // Leaves contain the final partitions of primitives, so fetch those.
    UT_Options root_values;
    root.traverseLeaves( root_values, bucket_creator );

    return true;
}

static inline bool
husdPartitionPrimsDataUsingValues( UT_Array<HUSD_PrimsBucket> &buckets,
	const HUSD_CvexResultData &data, const HUSD_CvexBindingList &bindings,
	const UT_Array<UsdPrim> &prims )
{
    return husdPartitionUsingValues( data, bindings, 
	[&]( const UT_Options &parm_values, const UT_ExintArray &indices )
	{
	    HUSD_PrimsBucket &b = buckets[ buckets.append() ];

	    b.getBucketValue().setOptions( parm_values );
	    b.setPrimIndices( indices );
	    for( exint i : b.getPrimIndices() )
		b.addPrimPath( prims[i].GetPath().GetString() );
	});
}

static inline bool
husdPartitionPrimsUsingValues( UT_Array<HUSD_PrimsBucket>&buckets,
	const UT_Array<UsdPrim> &prims, const HUSD_CvexCode &code,
	const HUSD_CvexRunData &usd_rundata, HUSD_TimeSampling &time_sampling )
{
    HUSD_CvexCodeInfo	 code_info( code, /*run_on_prims=*/ true );

    // Find the bindings between primitive attribs and cvex function parms.
    HUSD_CvexBindingList bindings;
    if( !husdGetBindingsAndOutputs( bindings, code_info, usd_rundata, prims ))
	return false;

    // Create data object and run CVEX code on it.
    HUSD_PrimAttribData	data( prims, bindings, usd_rundata.getTimeCode() );
    if( !data.runCvex( code_info, usd_rundata, bindings ))
	return false;

    husdUpdateTimeSampling( time_sampling, data.getTimeSampling() );

    // Retrieve the computed output data.
    return husdPartitionPrimsDataUsingValues( buckets, 
	    data.getResult(), bindings, prims );
}

bool
HUSD_Cvex::partitionPrimitives( HUSD_AutoAnyLock &lock,
        UT_Array<HUSD_PrimsBucket> &buckets,
	const HUSD_FindPrims &findprims,
        const HUSD_CvexCode &code ) const
{
    // Find out the primitives over which to run the cvex.
    UT_Array<UsdPrim> prims = husdGetReadOnlyPrims( lock, findprims );
    if( prims.size() <= 0 )
	return true;
    HUSD_CvexRunData::FallbackLockBinder binder(*myRunData, lock);

    HUSD_TimeSampling time_sampling = HUSD_TimeSampling::NONE;
    bool ok = (code.getReturnType() == HUSD_CvexCode::ReturnType::NONE)
	? husdPartitionPrimsUsingValues( buckets, prims, code, *myRunData, 
		time_sampling )
	: husdPartitionPrimsUsingKeyword( buckets, prims, code, *myRunData, 
		time_sampling );
    husdUpdateTimeSampling( myTimeSampling, time_sampling );

    return ok;
}

static inline UsdPrim
husdGetReadOnlyPrim( HUSD_AutoAnyLock &lock, const UT_StringRef &prim_path )
{
    const XUSD_ConstDataPtr &data = lock.constData();
    if( !data || !data->isStageValid() )
	return UsdPrim();

    return data->stage()->GetPrimAtPath( HUSDgetSdfPath( prim_path ));
}

static inline UT_ExintArray 
husdRemapIndices(const UT_ExintArray &data_indices, 
	const UT_ExintArray &face_indices)
{
    exint		n = data_indices.size();
    UT_ExintArray	result( n, n );
    for( exint i = 0; i < n; i++ )
    {
	exint data_index = data_indices[i];
	if( face_indices.isValidIndex( data_index ))
	    result[i] = face_indices[ data_index ];
	else
	    UT_ASSERT( !"Index out of bounds" );
    }

    return result;
}

static inline bool
husdPartitionFacesDataUsingKeyword( UT_Array<HUSD_FacesBucket> &buckets,
	const HUSD_CvexResultData &data, const char *out_name,
	const UsdPrim &prim, const UT_ExintArray *face_indices )
{
    UT_StringHolder prim_path( prim.GetPath().GetString() );
    return husdPartitionUsingKeyword( data, out_name,
	[&]( const UT_StringHolder &keyword, const UT_ExintArray &indices )
	{
	    HUSD_FacesBucket &b = buckets[ buckets.append() ];

	    b.getBucketValue().setKeyword( keyword );
	    b.setPrimPath( prim_path );
	    if( face_indices )
		b.setFaceIndices( husdRemapIndices(indices, *face_indices));
	    else
		b.setFaceIndices( indices );
	});
}

static inline bool
husdPartitionFacesUsingKeyword( UT_Array<HUSD_FacesBucket> &buckets,
	const UsdPrim &prim, const UT_ExintArray *face_indices, exint size_hint,
	const HUSD_CvexCode &code, const HUSD_CvexRunData &usd_rundata,
	HUSD_TimeSampling &time_sampling )
{
    HUSD_CvexCodeInfo	 code_info( code, /*run_on_prims=*/ false, true );

    // Find the bindings between primitive attribs and cvex function parms.
    UT_Array<UsdPrim>	 prims{ prim };
    HUSD_CvexBindingList bindings;
    if( !husdGetBindingsAndOutputs( bindings, code_info, usd_rundata, prims ))
	return false;

    // Create data object and run CVEX code on it.
    HUSD_ArrayElementData data( prim, face_indices, size_hint, bindings, 
	    usd_rundata.getTimeCode() );
    if( !data.runCvex( code_info, usd_rundata, bindings ))
	return false;

    husdUpdateTimeSampling( time_sampling, data.getTimeSampling() );

    // Retrieve the computed output data.
    return husdPartitionFacesDataUsingKeyword( buckets, 
	    data.getResult(), code_info.getOutputName(), prim, face_indices );
}

static inline bool
husdPartitionFacesDataUsingValues( UT_Array<HUSD_FacesBucket> &buckets,
	const HUSD_CvexResultData &data, 
	const HUSD_CvexBindingList &bindings,
	const UsdPrim &prim, const UT_ExintArray *face_indices )
{
    UT_StringHolder prim_path( prim.GetPath().GetString() );

    return husdPartitionUsingValues( data, bindings, 
	[&]( const UT_Options &parm_values, const UT_ExintArray &indices )
	{
	    HUSD_FacesBucket &b = buckets[ buckets.append() ];

	    b.getBucketValue().setOptions( parm_values );
	    b.setPrimPath( prim_path );
	    if( face_indices )
		b.setFaceIndices( husdRemapIndices(indices, *face_indices));
	    else
		b.setFaceIndices( indices );
	});
}

static inline bool
husdPartitionFacesUsingValues( UT_Array<HUSD_FacesBucket> &buckets,
	const UsdPrim &prim, const UT_ExintArray *face_indices, exint size_hint,
	const HUSD_CvexCode &code, const HUSD_CvexRunData &usd_rundata,
	HUSD_TimeSampling &time_sampling )
{
    HUSD_CvexCodeInfo	 code_info( code, /*run_on_prims=*/ false );

    // Find the bindings between primitive attribs and cvex function parms.
    UT_Array<UsdPrim>	 prims{ prim };
    HUSD_CvexBindingList bindings;
    if( !husdGetBindingsAndOutputs( bindings, code_info, usd_rundata, prims ))
	return false;

    // Create data object and run CVEX code on it.
    HUSD_ArrayElementData data( prim, face_indices, size_hint, bindings, 
	    usd_rundata.getTimeCode() );
    if( !data.runCvex( code_info, usd_rundata, bindings ))
	return false;

    husdUpdateTimeSampling( time_sampling, data.getTimeSampling() );

    // Retrieve the computed output data.
    return husdPartitionFacesDataUsingValues( buckets, 
	    data.getResult(), bindings, prim, face_indices );
}

static inline exint
husdGetFaceCount( const UsdPrim &prim, const HUSD_TimeCode &time_code )
{
    UsdGeomMesh mesh(prim);
    if (!mesh)
	return 0;

    VtArray<int> vertex_counts;
    mesh.GetFaceVertexCountsAttr().Get(&vertex_counts,
	    HUSDgetNonDefaultUsdTimeCode(time_code));

    return vertex_counts.size();
}

bool
HUSD_Cvex::partitionFaces( HUSD_AutoAnyLock &lock,
        UT_Array<HUSD_FacesBucket> &buckets,
	const UT_StringRef &geo_prim_path,
        const UT_ExintArray *face_indices,
	const HUSD_CvexCode &code ) const
{
    UsdPrim prim = husdGetReadOnlyPrim( lock, geo_prim_path );
    if( !prim.IsValid() )
	return true;
    HUSD_CvexRunData::FallbackLockBinder binder(*myRunData, lock);

    // If we are not passed a set of face indices to process, 
    // ensure the VEX runs over array of all faces.
    exint size_hint = 0;
    if (!face_indices)
	size_hint = husdGetFaceCount( prim, myRunData->getTimeCode() );

    HUSD_TimeSampling time_sampling = HUSD_TimeSampling::NONE;
    bool ok = (code.getReturnType() == HUSD_CvexCode::ReturnType::NONE)
	? husdPartitionFacesUsingValues( buckets, prim, face_indices, size_hint,
		code, *myRunData, time_sampling )
	: husdPartitionFacesUsingKeyword( buckets, prim, face_indices,size_hint,
		code, *myRunData, time_sampling );
    husdUpdateTimeSampling( myTimeSampling, time_sampling );

    return ok;
}

static inline bool
husdCollectMatchedFaces( UT_ExintArray &matched_faces_indices, 
	const HUSD_CvexResultData &data, const char *output_name, 
	const UT_ExintArray *face_indices )
{
    UT_ExintArray &result = matched_faces_indices;
    
    result.clear();
    HUSD_SelectionCollector collector( result, data, output_name );
    collector.getSelection();

    if( face_indices )
	result = husdRemapIndices( result, *face_indices );

    return true;
}

bool
HUSD_Cvex::matchFaces( HUSD_AutoAnyLock &lock,
        UT_ExintArray &matched_faces_indices,
	const UT_StringRef &geo_prim_path,
        const UT_ExintArray *face_indices,
	const HUSD_CvexCode &code ) const
{
    UsdPrim prim = husdGetReadOnlyPrim( lock, geo_prim_path );
    if( !prim.IsValid() )
	return false;
    HUSD_CvexRunData::FallbackLockBinder binder(*myRunData, lock);

    // If we are not passed a set of face indices to process, 
    // ensure the VEX runs over array of all faces.
    exint size_hint = 0;
    if (!face_indices)
	size_hint = husdGetFaceCount( prim, myRunData->getTimeCode() );

    HUSD_CvexCodeInfo	 code_info( code, /*run_on_prims=*/ false, true );

    // Find the bindings between primitive attribs and cvex function parms.
    UT_Array<UsdPrim>	 prims{ prim };
    HUSD_CvexBindingList bindings;
    if( !husdGetBindingsAndOutputs( bindings, code_info, *myRunData, prims ))
	return false;

    // Create data object and run CVEX code on it.
    HUSD_ArrayElementData data( prim, face_indices, size_hint, bindings, 
	    myRunData->getTimeCode() );
    if( !data.runCvex( code_info, *myRunData, bindings ))
	return false;

    husdUpdateTimeSampling( myTimeSampling, data.getTimeSampling() );

    // Retrieve the computed output data.
    return husdCollectMatchedFaces( matched_faces_indices, 
	    data.getResult(), code_info.getOutputName(), face_indices );
}

static inline bool
husdCollectMatchedInstances( UT_ExintArray &matched_instance_indices, 
	const HUSD_CvexResultData &data, const char *output_name, 
	const UT_ExintArray *instance_indices )
{
    UT_ExintArray &result = matched_instance_indices;
    
    result.clear();
    HUSD_SelectionCollector collector( result, data, output_name );
    collector.getSelection();

    if( instance_indices )
	result = husdRemapIndices( result, *instance_indices );

    return true;
}

static inline exint
husdGetInstanceCount( const UsdPrim &prim, const HUSD_TimeCode &time_code )
{
    UsdGeomPointInstancer instancer(prim);
    if (!instancer)
	return false;

    VtArray<int> proto_indices;
    instancer.GetProtoIndicesAttr().Get(&proto_indices,
	    HUSDgetNonDefaultUsdTimeCode(time_code));

    return proto_indices.size();
}

bool
HUSD_Cvex::matchInstances( HUSD_AutoAnyLock &lock,
        UT_ExintArray &matched_instance_indices,
        const UT_StringRef &instancer_prim_path,
        const UT_ExintArray *instance_indices,
        const HUSD_CvexCode &code ) const
{
    UsdPrim prim = husdGetReadOnlyPrim( lock, instancer_prim_path );
    if( !prim.IsValid() )
	return false;
    HUSD_CvexRunData::FallbackLockBinder binder(*myRunData, lock);

    // If we are not passed a set of instance indices to process, 
    // ensure the VEX runs over array of all instances.
    exint size_hint = 0;
    if (!instance_indices)
	size_hint = husdGetInstanceCount( prim, myRunData->getTimeCode() );

    HUSD_CvexCodeInfo	 code_info( code, /*run_on_prims=*/ false, true );

    // Find the bindings between primitive attribs and cvex function parms.
    UT_Array<UsdPrim>	 prims{ prim };
    HUSD_CvexBindingList bindings;
    if( !husdGetBindingsAndOutputs( bindings, code_info, *myRunData, prims ))
	return false;

    // Create data object and run CVEX code on it.
    HUSD_ArrayElementData data( prim, instance_indices, size_hint,
	    bindings, myRunData->getTimeCode() );

    if( !data.runCvex( code_info, *myRunData, bindings ))
	return false;

    husdUpdateTimeSampling( myTimeSampling, data.getTimeSampling() );

    // Retrieve the computed output data.
    return husdCollectMatchedInstances( matched_instance_indices, 
	    data.getResult(), code_info.getOutputName(), instance_indices );
}

bool
HUSD_Cvex::getIsTimeVarying() const
{
    return HUSDisTimeVarying( myTimeSampling );
}

bool
HUSD_Cvex::getIsTimeSampled() const
{
    return HUSDisTimeSampled( myTimeSampling );
}

