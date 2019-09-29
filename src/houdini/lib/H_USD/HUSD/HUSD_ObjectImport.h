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

#ifndef __HUSD_ObjectImport_h__
#define __HUSD_ObjectImport_h__

#include "HUSD_API.h"
#include "HUSD_CreatePrims.h"
#include "HUSD_DataHandle.h"
#include "HUSD_SetRelationships.h"
#include <OBJ/OBJ_Node.h>

class HUSD_FindPrims;
class HUSD_TimeCode;

class HUSD_API HUSD_ObjectImport
{
public:
			 HUSD_ObjectImport(HUSD_AutoWriteLock &dest);
			~HUSD_ObjectImport();

    // Returns the appropriate primtype for an object.
    // Returns an empty string for unsupported object types.
    static UT_StringHolder  getPrimTypeForObject(const OP_Node *node,
				UT_Set<int> *parmindices = nullptr);
    static UT_StringHolder  getPrimKindForObject(const OP_Node *node);

    bool		    importPrim(
				const OBJ_Node *object,
				const UT_StringHolder &primpath,
				const UT_StringHolder &primtype,
				const UT_StringHolder &primkind) const;

    void		    importParameters(
				const UT_StringHolder &primpath,
				const OP_Node *object,
				const HUSD_TimeCode &timecode,
				const fpreal time,
				bool firsttime,
				UT_Set<int> *parmindices = nullptr) const;

    void		    importSOP(
				SOP_Node *sop,
				OP_Context &context,
				const UT_StringRef &refprimpath,
				const UT_StringRef &pathattr,
				const UT_StringRef &primpath,
				const UT_StringRef &pathprefix,
				bool polygonsassubd,
                                const UT_StringRef &subdgroup) const;

    bool		    importMaterial(
				VOP_Node *object,
				const UT_StringHolder &primpath) const;

    bool		    setLightGeometry(
				const UT_StringHolder &lightprimpath,
				const UT_StringHolder &geoprimpath) const;

    bool		    setLightPortal(
				const UT_StringHolder &lightprimpath,
				const UT_StringHolder &geoprimpath) const;

private:
    HUSD_AutoWriteLock	&myWriteLock;
};

#endif

