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

#ifndef __HUSD_BindMaterial__
#define __HUSD_BindMaterial__

#include "HUSD_API.h"
#include "HUSD_DataHandle.h"


class HUSD_API HUSD_BindMaterial
{
public:
    /// Standard constructor.
		 HUSD_BindMaterial(HUSD_AutoWriteLock &lock);

    /// Assigns the given material to the given geometry primitive(s).
    bool	bind(const UT_StringRef &mat_prim_path,
			const UT_StringRef &geo_prim_path) const;
    bool	bind(const UT_StringRef &mat_prim_path,
			const HUSD_FindPrims &find_geo_prims) const;

    /// Performs a collection based binding with explicit values for the
    /// collection path, binding prim path, and binding name. The bind
    /// method and bind prim path set on this object are ignored.
    bool	bindAsCollection(const UT_StringRef &mat_prim_path, 
			const UT_StringRef &collection_path,
			const UT_StringRef &binding_prim_path,
			const UT_StringRef &binding_name) const;

    /// Assigns the given material a subset of the geometry primitive. This
    /// method creates the geometry subset. If a geometry subset already
    /// exists, it can be bound using the regular bind methods above.
    bool	bindSubset(const UT_StringRef &mat_prim_path, 
			const UT_StringRef &geo_prim_path,
			const UT_ExintArray *face_indices) const;

    /// Enumeration of the ways in which a binding can be performed.
    enum class BindMethod
    {
	DIRECT,		// direct binding
	COLLECTION	// collection-based binding
    };

    /// Sets the method of defining bindings.
    void			setBindMethod( BindMethod method )
				{ myBindMethod = method; }
    BindMethod			getBindMethod() const 
				{ return myBindMethod; }

    /// For non-direct bindings, sets the USD primitive path on which the 
    /// collection-based binding is defined.
    void			setBindPrimPath( const UT_StringRef &p)
				{ myBindPrimPath = p; }
    const UT_StringHolder &	getBindPrimPath() const 
				{ return myBindPrimPath; }

    /// Enumeration of the material binding strength.
    enum class Strength
    {
	DEFAULT,	// fallback
	STRONG,		// stronger than descendents
	WEAK		// weaker than descendents
    };

    /// Sets the strength preference for material assignments.
    void			setStrength( Strength strength )
				{ myStrength = strength; }
    Strength			getStrength() const 
				{ return myStrength; }

    /// Sets the purpose for material assignments.
    void			setPurpose( const UT_StringHolder &purpose )
				{ myPurpose = purpose; }
    const UT_StringHolder &	getPurpose() const 
				{ return myPurpose; }

private:
    HUSD_AutoWriteLock &	myWriteLock;
    BindMethod			myBindMethod;		// Collection based?
    Strength			myStrength;		// Binding strength
    UT_StringHolder		myPurpose;		// Binding purpose
    UT_StringHolder		myBindPrimPath;		// Collection location
};

#endif

