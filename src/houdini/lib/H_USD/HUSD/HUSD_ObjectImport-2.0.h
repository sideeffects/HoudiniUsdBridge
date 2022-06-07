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
 *        Side Effects Software Inc.
 *        123 Front Street West, Suite 1401
 *        Toronto, Ontario
 *      Canada   M5J 2M2
 *        416-504-9876
 *
 */

#ifndef __HUSD_ObjectImport2_h__
#define __HUSD_ObjectImport2_h__

#include "HUSD_API.h"

class HUSD_FindPrims;
class HUSD_TimeCode;
class OBJ_Node;
class OP_Context;
class SOP_Node;

class HUSD_API HUSD_ObjectImport2
{
public:
    explicit HUSD_ObjectImport2(HUSD_AutoWriteLock &dest);
    ~HUSD_ObjectImport2();

    // Returns the appropriate primtype for an object.
    // Returns an empty string for unsupported object types.
    static UT_StringHolder getPrimKindForObject(const OP_Node *node);

    bool importPrim(
            const OBJ_Node *object,
            const UT_StringHolder &primpath,
            const UT_StringHolder &primtype,
            const UT_StringHolder &primkind) const;

    void recordSOPForImport(
            SOP_Node *sop,
            OP_Context &context,
            const UT_StringMap<UT_StringHolder> &args,
            const UT_StringRef &refprimpath,
            const UT_StringRef &primpath);
    
    void importAllRecordedSOPs(bool asreference = true);

    bool importMaterial(VOP_Node *object, const UT_StringHolder &primpath)
            const;

private:
    HUSD_AutoWriteLock &myWriteLock;
    UT_Array<UT_StringHolder> mySopImportFilePaths;
    UT_Array<UT_StringHolder> mySopImportPrimPaths;
    UT_Array<UT_StringHolder> mySopImportRefPrimPaths;
    UT_Array<UT_StringMap<UT_StringHolder>> mySopImportArgs;
    UT_Array<GU_DetailHandle> mySopImportGDHs;
};

#endif
