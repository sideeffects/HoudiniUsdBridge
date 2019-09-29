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
 */


#include "HUSD_CvexBindingMap.h"

#include <OP/OP_Node.h>
#include <UT/UT_WorkBuffer.h>

HUSD_CvexBindingMap	
HUSD_CvexBindingMap::constructBindingsMap( const OP_Node &node,
	const char *bindings_num_parm, const char *cvex_parm_name_parm, 
	const char *usd_attrib_name_parm, const char *usd_attrib_type_parm,  
	const char *auto_bind_parm )
{
    HUSD_CvexBindingMap	map;

    // Note, parameter names are assumed to have a 1-based index (ie, default).
    int	prm_idx[2] = { 0, 0 };
    int n = node.evalInt( bindings_num_parm, 0, 0.0f );
    for( int i = 1; i <= n; i++ )
    {
	UT_String		attr_name, parm_name, attr_type;

	prm_idx[0] = i;

	node.evalStringInst( cvex_parm_name_parm, prm_idx, parm_name, 0, 0.0f );
	node.evalStringInst( usd_attrib_name_parm, prm_idx, attr_name, 0, 0.0f);
	if( !attr_name.isstring() && !parm_name.isstring() )
	    continue;
	else if( attr_name.isstring() && !parm_name.isstring() )
	    // Allows users to name attrib to specify type (below)
	    parm_name = attr_name;
	else if( !attr_name.isstring() && parm_name.isstring() )
	    // For symmetry, with having just attrbute
	    attr_name = parm_name; 

	if( UTisstring( usd_attrib_type_parm ))
	    node.evalStringInst( usd_attrib_type_parm, prm_idx, attr_type, 
		    0, 0.0f);

	map.addBinding( parm_name, attr_name, attr_type );
    }

    if( UTisstring( auto_bind_parm ))
	map.setDefaultToIdentity( node.evalInt( auto_bind_parm, 0, 0.0f ));

    return map;
}

void
HUSD_CvexBindingMap::addBinding( const UT_StringHolder &parm_name,
	const UT_StringHolder &attrib_name, const UT_StringHolder &attrib_type )
{
    myAttribFromParm[ parm_name ] = attrib_name;
    if( attrib_type.isstring() )
	myAttribTypeFromParm[ parm_name ] = attrib_type;
}

void
HUSD_CvexBindingMap::setDefaultToIdentity(bool do_identity)
{
    myDefaultToIdentity = do_identity;
}

UT_StringHolder
HUSD_CvexBindingMap::getAttribFromParm(const UT_StringRef &parm) const
{
    auto entry = myAttribFromParm.find( parm );
    if( entry != myAttribFromParm.end() )
	return entry->second;

    if( myDefaultToIdentity )
	return UT_StringHolder( parm );

    return UT_StringHolder();
}


UT_StringHolder
HUSD_CvexBindingMap::getAttribTypeFromParm( const UT_StringRef &parm ) const
{
    auto entry = myAttribTypeFromParm.find( parm );
    if( entry != myAttribTypeFromParm.end() )
	return entry->second;

    return UT_StringHolder();
}

