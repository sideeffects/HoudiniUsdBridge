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

#ifndef __HUSD_ChangeBlock_h__
#define __HUSD_ChangeBlock_h__

#include "HUSD_API.h"
#include <UT/UT_UniquePtr.h>

// This class wraps an SdfChangeBlock. Many forms of USD edits cannot be
// performed safely inside a change block, and HUSD classes often obscure
// the underlying USD operations taking place. So only use this class to put
// changes inside a block after examining the code inside the HUSD classes
// to ensure they are not performing any operations that are not going to
// work inside a change block.
class HUSD_API HUSD_ChangeBlock
{
public:
     HUSD_ChangeBlock();
    ~HUSD_ChangeBlock();

private:
    class husd_ChangeBlockPrivate;

    UT_UniquePtr<husd_ChangeBlockPrivate> myPrivate;
};

#endif

