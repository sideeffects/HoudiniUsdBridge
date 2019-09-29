/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
 *
 * Produced by:
 *	Rob Bairos
 *	Side Effects Software Inc
 *	477 Richmond Street West
 *	Toronto, Ontario
 *	Canada   M5V 3E7
 *	416-504-9876
 *
 * NAME:	OBJ_LOP.h (Custom Library, C++)
 *
 * COMMENTS:    An object to fetch it's transform from another object.
 *
 */

#ifndef __OBJ_LOP__
#define __OBJ_LOP__

#include <OBJ/OBJ_Geometry.h>
#include <pxr/pxr.h>

PXR_NAMESPACE_OPEN_SCOPE

enum OBJ_LOPIndex
{
    I_LOP_LOPPATH = I_N_GEO_INDICES,
    I_LOP_PRIMPATH,
    I_LOP_XFORMTYPE,

    I_N_LOP_INDICES
};


class OBJ_LOP : public OBJ_Geometry
{
public:
    static void			 Register(OP_OperatorTable* table);
    static OP_Node              *Create(OP_Network *net,
					const char *name,
					OP_Operator *entry);

				 OBJ_LOP(OP_Network *net,
					   const char *name,
					   OP_Operator *op);
    virtual			~OBJ_LOP();

    virtual OBJ_OBJECT_TYPE	 getObjectType() const;
    static PRM_Template		*getTemplateList();

protected:
    // Used to get pointer to indirection indices for each object type
    virtual int			*getIndirect() const
				 { return fetchIndirect; }

    virtual OP_ERROR		 cookMyObj(OP_Context &context);

private:
    void			 LOPPATH(UT_String &str);
    void			 PRIMPATH(UT_String &str);
    void			 XFORMTYPE(UT_String &str);

    static int			*fetchIndirect;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif

