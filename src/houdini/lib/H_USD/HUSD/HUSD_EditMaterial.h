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

#ifndef __HUSD_EditMaterial_h__
#define __HUSD_EditMaterial_h__

#include "HUSD_API.h"
#include "HUSD_DataHandle.h"

class OP_Network;

class HUSD_API HUSD_EditMaterial
{
public:
    /// Standard c-tor.
			HUSD_EditMaterial(HUSD_AutoAnyLock &lock);

    /// Creates the VOP children inside the LOP @p parent_node 
    /// to reflect the USD material defined at the given @p path.
    /// If succeeds, returns the name of the created material child node.
    UT_StringHolder	loadMaterial(OP_Network &parent_node,
				const UT_StringRef &material_prim_path) const;

    /// Updates the VOP children inside the LOP @ parent_node
    /// to bring them in synch with the USD material at the given @p path.
    UT_StringHolder	updateMaterial(OP_Network &parent_node,
				const UT_StringRef &material_prim_path,
				const UT_StringRef &material_node_name) const;

private:
    HUSD_AutoAnyLock	&myAnyLock;
};

#endif

