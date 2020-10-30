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

#ifndef __XUSD_ShaderRegistry_h__
#define __XUSD_ShaderRegistry_h__

#include "HUSD_API.h"
#include <UT/UT_StringHolder.h>
#include <pxr/pxr.h>

class UT_StringArray;

PXR_NAMESPACE_OPEN_SCOPE
class UsdPrim;
class SdfValueTypeName;
class VtValue;

class HUSD_API XUSD_ShaderRegistry
{
public:
    /// Obtains shader input names from the given primitive, if that primitive
    /// is a shader. 
    /// @return	    True on success, false otherwise.
    static bool	    getShaderInputNames( const UsdPrim &prim,
			    UT_StringArray &input_names);

    /// Obtains information about the given shader primitive input.
    /// @return	    True on success, false otherwise.
    static bool	    getShaderInputInfo( const UsdPrim &prim,
			    const UT_StringRef &input_name,
			    SdfValueTypeName *type = nullptr,
			    VtValue *default_value = nullptr,
			    UT_StringHolder *label = nullptr );
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif

