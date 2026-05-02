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

#ifndef __HUSD_CvexCode__
#define __HUSD_CvexCode__

#include "HUSD_API.h"
#include <UT/UT_StringHolder.h>

/// Abstracts the CVEX source code (either command or vexpression), along
/// with some aspects of it, such as return type and export parameter mask
/// for expressions.
class HUSD_API HUSD_CvexCode
{
public:
    /// Creates the cvex code object given the string and its meaning.
    HUSD_CvexCode( const UT_StringRef &cmd_or_vexpr, bool is_cmd = true );

    /// Returns the source string (either a command or vexpression).
    const UT_StringHolder &	getSource() const
				{ return mySource; }

    /// Returns true if the source is a command, or false if it's a vexpression.
    bool			isCommand() const
				{ return myIsCommand; }

    // Lists return types.
    enum class ReturnType
    {
	NONE,		// void type (usually all output parameters are used)
	BOOLEAN,	// true/false for selection of entities
	STRING,		// for keyword value
    };

    /// @{ Sets the return type for the code. 
    /// Cvex scripts that select (match) primitives or faces return a boolean. 
    /// Cvex scripts that partition with keyword, usually return a string.
    /// Cvex scripts that partition with all outupt values, or run on all 
    /// attributes (ie, are not used for selection or partitionin), 
    /// usually return void (which is a default value).
    void			setReturnType(ReturnType type) 
				{ myReturnType = type; }
    ReturnType			getReturnType() const
				{ return myReturnType; }
    /// @}

    /// @{ Sets the export variables (useful for Vexpressions)
    void			setExportsPattern(const UT_StringRef &pattern)
				{ myExportsPattern = pattern; }
    const UT_StringHolder &	getExportsPattern()const
				{ return myExportsPattern; }

private:
    UT_StringHolder	mySource;	// command or expression source code
    bool		myIsCommand;	// true if source is a command line
    ReturnType		myReturnType;	// return type of the vexpression
    UT_StringHolder	myExportsPattern;   // VEX export variables
};

#endif

