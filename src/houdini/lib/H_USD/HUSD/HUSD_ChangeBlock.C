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

#include "HUSD_ChangeBlock.h"
#include <pxr/usd/sdf/changeBlock.h>

PXR_NAMESPACE_USING_DIRECTIVE

class HUSD_ChangeBlock::husd_ChangeBlockPrivate
{
public:
    husd_ChangeBlockPrivate()
    { }

    ~husd_ChangeBlockPrivate()
    { }

private:
    SdfChangeBlock		 myChangeBlock;
};

HUSD_ChangeBlock::HUSD_ChangeBlock()
    : myPrivate(new HUSD_ChangeBlock::husd_ChangeBlockPrivate())
{
}

HUSD_ChangeBlock::~HUSD_ChangeBlock()
{
}

