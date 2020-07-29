
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

#ifndef __XUSD_AttributeUtils_h__
#define __XUSD_AttributeUtils_h__

#include "HUSD_API.h"
#include <SYS/SYS_Types.h>
#include <pxr/pxr.h>
#include <pxr/usd/sdf/attributeSpec.h>

class VOP_Node;
class PRM_Parm;

PXR_NAMESPACE_OPEN_SCOPE
class UsdObject;
class UsdAttribute;
class UsdRelationship;
class UsdTimeCode;

/// Returns the SdfValueTypeName string best corresponding to the UT type.
template<typename UT_VALUE_TYPE>
HUSD_API const char *
HUSDgetSdfTypeName();

/// Sets the given @p attribute to the given @p value.
template<typename UT_VALUE_TYPE>
HUSD_API bool
HUSDsetAttribute(const UsdAttribute &attribute,
        const UT_VALUE_TYPE &value,
	const UsdTimeCode &timecode);

HUSD_API bool
HUSDsetAttribute(const UsdAttribute &attribute,
        const PRM_Parm &parm, 
	const UsdTimeCode &timecode); 

HUSD_API bool
HUSDsetNodeParm(PRM_Parm &parm,
        const UsdAttribute &attribute, 
	const UsdTimeCode &timecode,
        bool save_for_undo = false); 

HUSD_API bool
HUSDsetNodeParm(PRM_Parm &parm,
        const UsdRelationship &relationship, 
        bool save_for_undo = false); 

/// Gets the @p value of the given @p attribute at specified @p timecode.
template<typename UT_VALUE_TYPE>
HUSD_API bool
HUSDgetAttribute(const UsdAttribute &attribute, UT_VALUE_TYPE &value,
	const UsdTimeCode &timecode);

template<typename UT_VALUE_TYPE>
HUSD_API bool
HUSDgetAttributeSpecDefault(const SdfAttributeSpec &spec,
	UT_VALUE_TYPE &value);

/// Gets obj's metadata given its name (eg, "active" or  "customData:foo:bar").
template<typename UT_VALUE_TYPE>
HUSD_API bool
HUSDsetMetadata(const UsdObject &object, const TfToken &name,
	const UT_VALUE_TYPE &value);

/// Gets obj's metadata given its name (eg, "active" or  "customData:foo:bar").
template<typename UT_VALUE_TYPE>
HUSD_API bool
HUSDgetMetadata(const UsdObject &object, const TfToken &name,
	UT_VALUE_TYPE &value);

HUSD_API bool
HUSDclearMetadata(const UsdObject &object, const TfToken &name);

/// @{ Metadata utilities
HUSD_API bool HUSDhasMetadata(const UsdObject &object, const TfToken &name);
HUSD_API bool HUSDisArrayMetadata(const UsdObject &object, const TfToken &name);
HUSD_API exint	HUSDgetMetadataLength(const UsdObject &object, 
	const TfToken &name);
/// @}

/// Conversion function between VtValue and UT_* value objects.
template<typename UT_VALUE_TYPE>
HUSD_API bool
HUSDgetValue( const VtValue &vt_value, UT_VALUE_TYPE &ut_value );

/// Conversion function between from UT_* value objects and a VtValue with
/// a matching GfValue inside.
template<typename UT_VALUE_TYPE>
HUSD_API VtValue
HUSDgetVtValue( const UT_VALUE_TYPE &ut_value );


/// Returns the type of a shader input attribute given the VOP node input.
HUSD_API SdfValueTypeName   HUSDgetShaderAttribSdfTypeName( 
	const PRM_Parm &parm );
HUSD_API SdfValueTypeName   HUSDgetShaderInputSdfTypeName(
	const VOP_Node &vop, int input_idx, 
	const PRM_Parm *parm_hint = nullptr );
HUSD_API SdfValueTypeName   HUSDgetShaderOutputSdfTypeName(
	const VOP_Node &vop, int output_idx, 
	const PRM_Parm *parm_hint = nullptr );


PXR_NAMESPACE_CLOSE_SCOPE

#endif
