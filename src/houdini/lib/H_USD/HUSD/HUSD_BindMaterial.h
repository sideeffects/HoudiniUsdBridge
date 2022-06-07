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
 */

#ifndef __HUSD_BindMaterial__
#define __HUSD_BindMaterial__

#include "HUSD_API.h"
#include "HUSD_DataHandle.h"
#include "HUSD_Path.h"


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

    /// Assigns the given material to a subset of the geometry primitive. This
    /// method creates the geometry subset. If a geometry subset already
    /// exists, it can be bound using the regular bind methods above.
    bool	bindSubset(const UT_StringRef &mat_prim_path, 
			const UT_StringRef &geo_prim_path,
			const UT_ExintArray *face_indices) const;

    /// Creates a geometry subset for material binding, but does not actually
    /// bind any material to the prim.
    HUSD_Path	createSubset(const UT_StringRef &subset_name, 
			const UT_StringRef &geo_prim_path,
			const UT_ExintArray &face_indices) const;

    /// Makes sure the primitives are not bound to any material.
    /// I.e, if there is any direct material binding (on the given prim 
    /// or its ancestor), authors a binding block on the given prim,
    /// and if there is any collection-based assignment on the prim or ancestor,
    /// removes the given prim from the collection.
    bool	unbindAll( const HUSD_FindPrims &find_prims ) const;

    /// Removes the material binding from the geometry primitive(s).
    /// The @p unbind_limit determines how many material bindings are blocked.
    //
    /// The binding limit of 1 unassigns the currently bound material,
    /// allowing any other candidate moaterial to take over.
    ///
    /// The binding limit of 2 unassigns the currently bound material (if any),
    /// and the second candidate material (if any), allowing the next candidate 
    /// material to take effect.
    bool	unbind( const HUSD_FindPrims &find_prims,
			const UT_StringHolder &purpose,
			int unbind_limit = 1) const;

    /// Looks for the specified attrname on any prims in the named layer.
    /// This string attribute is turned into an SdfPath that is bound as the
    /// material to the primitive with the attribute. If the attribute string
    /// points to a geometry primitive, we assign the material bound to that
    /// geometry primitive. Return false if the operation fails in an
    /// unrecoverable way. An error will have been added to the error scope
    /// to explain the failure. Warnings may also be generated for more minor
    /// issues such as invalid material paths.
    bool        assignMaterialsFromAttribute(
                        const UT_StringRef &layername,
                        const UT_StringMap<UT_StringHolder> &args,
                        const UT_StringRef &primpath,
                        const UT_StringRef &refprimpath,
                        const UT_StringRef &attrname,
                        bool remove_attr,
                        bool create_empty_materials) const;

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

    /// Sets the collection expansion option when defining collections.
    void                        setBindCollectionExpand( bool expand )
				{ myBindCollectionExpand = expand; }
    bool                        getBindCollectionExpand() const 
				{ return myBindCollectionExpand; }

    /// For collection-based bindings, sets the USD primitive path on which the 
    /// collection-based binding is defined.
    void			setBindPrimPath( const UT_StringRef &p)
				{ myBindPrimPath = p; }
    const UT_StringHolder &	getBindPrimPath() const 
				{ return myBindPrimPath; }
    
    /// For collection-based bindings, sets the USD binding name.
    /// (If not set explicitly, the material name will be used).
    void			setBindName( const UT_StringRef &n)
				{ myBindName = n; }
    const UT_StringHolder &	getBindName() const 
				{ return myBindName; }

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
    bool			myBindCollectionExpand;	// Expand collections
    Strength			myStrength;		// Binding strength
    UT_StringHolder		myPurpose;		// Binding purpose
    UT_StringHolder		myBindPrimPath;		// Collection location
    UT_StringHolder		myBindName;		// Collection bind name 
};

#endif

