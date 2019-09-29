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


#ifndef __HUSD_GeoSubset__
#define __HUSD_GeoSubset__

#include "HUSD_API.h"
#include "HUSD_DataHandle.h"
class HUSD_TimeCode;


class HUSD_API HUSD_GeoSubset
{
public:
    /// Standard constructor.
		HUSD_GeoSubset(HUSD_AutoWriteLock &lock);

    /// Creates geometry subset in a given primitive.
    bool	createGeoSubset( const UT_StringRef &prim_path,
			const UT_ExintArray &face_indices,
			const UT_StringRef &subset_name ) const;


    /// @{ Get and set the geometry subset family name. 
    /// The subsets that have the same family name are logically tied
    /// together, and can be validated for overlaps, partitioning, etc.
    void		    setFamilyName( const UT_StringRef &name )
			    { myFamilyName = name; }
    const UT_StringHolder & getFamilyName() const
			    { return myFamilyName; }
    /// @} 

    /// Enumerates the possible values for geo subset family type
    enum class FamilyType
    {
	UNRESTRICTED,	    // each face can belong to zero or more subsets
	NONOVERLAPPING,	    // each face belongs to at most one subset
	PARTITION,	    // each face belongs to exactly one subset
    };
    void		    setFamilyType( FamilyType type )
			    { myFamilyType = type; }
    FamilyType		    getFamilyType() const
			    { return myFamilyType; }
		    

    /// Returns true if the @p subset_prim_path refers to a geometry subset,
    /// and if so also returns the geometry parent path and face idices. 
    /// Otherwise, returns false.
    bool	    getGeoPrimitiveAndFaceIndices( 
				UT_StringHolder &geo_prim_path,
				UT_ExintArray &face_indices,
				const UT_StringRef &subset_prim_path,
				const HUSD_TimeCode &time_code ) const;

private:
    HUSD_AutoWriteLock &	myWriteLock;
    UT_StringHolder		myFamilyName;
    FamilyType			myFamilyType;
};

#endif

