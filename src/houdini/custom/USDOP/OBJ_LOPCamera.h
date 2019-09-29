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
 * NAME:	OBJ_LOPCamera.h (Custom Library, C++)
 *
 * COMMENTS:    An object to fetch it's transform from another object.
 *
 */

#ifndef __OBJ_LOPCamera__
#define __OBJ_LOPCamera__

#include <OBJ/OBJ_Camera.h>
#include <pxr/pxr.h>

PXR_NAMESPACE_OPEN_SCOPE

enum OBJ_LOPCameraIndex
{
    I_LOPCAMERA_LOPPATH = I_N_CAM_INDICES,
    I_LOPCAMERA_PRIMPATH,
    I_LOPCAMERA_XFORMTYPE,

    I_N_LOPCAMERA_INDICES
};


class OBJ_LOPCamera : public OBJ_Camera
{
public:
    static void			 Register(OP_OperatorTable* table);
    static OP_Node              *Create(OP_Network *net,
					const char *name,
					OP_Operator *entry);

				 OBJ_LOPCamera(OP_Network *net,
					   const char *name,
					   OP_Operator *op);
    virtual			~OBJ_LOPCamera();

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

