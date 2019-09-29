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

#ifndef __HUSD_CreateMaterial_h__
#define __HUSD_CreateMaterial_h__

#include "HUSD_API.h"
#include "HUSD_DataHandle.h"
#include "HUSD_TimeCode.h"

class VOP_Node;
class UT_Options;

class HUSD_API HUSD_CreateMaterial
{
public:
    /// Standard c-tor.
		HUSD_CreateMaterial(HUSD_AutoWriteLock &lock);

    /// Defines a USD material primitive at a given @p usd_mat_path 
    /// based on given @p material_vop material node.
    /// @param auto_generate_preview_shader If true, an attempt is made 
    ///		to ensure the created material has a preview shader
    ///		(for the universal render context). Ie, if the material node
    ///		does not contain any explicit preview shader node to translate, 
    ///		then an ad-hoc preview shader USD primitive will be generated.
    bool	createMaterial( VOP_Node &material_vop, 
			const UT_StringRef &usd_mat_path,
			bool auto_generate_preview_shader ) const;

    /// Creates a new USD material primitive at @p usd_mat_path, which inherits 
    /// from the material given by @p base_material_path, and sets 
    /// the parameter override values on the created material.
    bool	createDerivedMaterial( 
			const UT_StringRef &base_material_path,
			const UT_Options &material_parameters,
			const UT_StringRef &usd_mat_path) const;

    /// Sets the time code at which shader parameters are evaluated.
    void	 setTimeCode( const HUSD_TimeCode &time_code )
			{ myTimeCode = time_code; }

    /// Sets the primitive type that should be used when creating parents
    /// that don't exist yet in the USD hierarchy.
    void	setParentPrimType( const UT_StringHolder &type )
			{ myParentType = type; }

private:
    HUSD_AutoWriteLock	&myWriteLock;
    UT_StringHolder	 myParentType;	// Type of intermediate ancestors.
    HUSD_TimeCode	 myTimeCode;	// Time at which to eval shader parms.
};


#endif

