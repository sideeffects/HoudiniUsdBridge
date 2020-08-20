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

#ifndef __HUSD_Token_h__
#define __HUSD_Token_h__

#include "HUSD_API.h"
#include <UT/UT_StringHolder.h>

// Simple subclass of UT_StringHolder that can be used to indicate that a
// string actually represents an asset path, rather than a raw string. Allows
// templated functions to match a string to a TfToken.
class HUSD_API HUSD_Token : public UT_StringHolder
{
public:
			 HUSD_Token();
			 HUSD_Token(const char *src);
			 HUSD_Token(const std::string &src);
			 HUSD_Token(const UT_StringHolder &src);
};

#endif

