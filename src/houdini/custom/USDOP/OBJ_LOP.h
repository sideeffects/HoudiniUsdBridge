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

