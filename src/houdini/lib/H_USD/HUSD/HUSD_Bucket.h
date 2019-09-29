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

#ifndef __HUSD_Bucket__
#define __HUSD_Bucket__

#include "HUSD_API.h"
#include <UT/UT_Options.h>
#include <UT/UT_StringArray.h>

// ============================================================================
/// Represents a value associated with the bucket
/// Note, the keyword could be part of options structure, but many situations
/// benefit from having a single default bucketing string, such as a keyword.
class HUSD_API HUSD_BucketValue
{
public:
    /// @{ Sets and gets the keyword (a string).
    void			setKeyword( const UT_StringRef &keyword )
				    { myKeyword = keyword; }
    const UT_StringHolder &	getKeyword() const
				    { return myKeyword; }
    /// @}

    /// @{ Sets and gets the options (ie, parameters and their values).
    void			setOptions( const UT_Options &options )
				    { myOptions = options; }
    const UT_Options &		getOptions() const
				    { return myOptions; }
    /// @}

private:
    UT_StringHolder		myKeyword;  
    UT_Options			myOptions;
};

// ============================================================================
/// A class for grouping entities that belong to (or yield) the same value.
class HUSD_API HUSD_Bucket
{
public:
    /// @{ Accessor for the value associated with the bucket
    const HUSD_BucketValue &	getBucketValue() const
				{ return myBucketValue; }
    HUSD_BucketValue &		getBucketValue() 
				{ return myBucketValue; }
    /// @}

private:
    HUSD_BucketValue		myBucketValue;
};

// ============================================================================
/// Represents a group of primitives in a bucket. 
/// The primitives can be represented using paths and/or indices.
class HUSD_API HUSD_PrimsBucket : public HUSD_Bucket
{
public:
    /// @{ Set and get primitives in the bucket, using path as prim identifier.
    void			setPrimPaths( const UT_StringArray &paths )
				    { myPrimPaths = paths; }
    void			addPrimPath( const UT_StringRef &path )
				    { myPrimPaths.append( path ); }
    const UT_StringArray &	getPrimPaths() const
				    { return myPrimPaths; }
    /// @}

    /// @{ Set and get primitives in the bucket, using index as prim identifier.
    void			setPrimIndices( const UT_ExintArray &indices )
				    { myPrimIndices = indices; }
    void			addPrimIndex( exint index )
				    { myPrimIndices.append( index ); }
    const UT_ExintArray &	getPrimIndices() const
				    { return myPrimIndices; }
    /// @}

private:
    UT_StringArray	myPrimPaths;	// paths of prims in the bucket
    UT_ExintArray	myPrimIndices;	// indices of prims in the bucket
};

// ============================================================================
/// Represents a group of primitive faces (ie, a geometry subset) in a bucket.
/// The faces are represented by indices into a primitive of a given path.
class HUSD_API HUSD_FacesBucket : public HUSD_Bucket
{
public:
    /// @{ Set and get the primitive path to which the faces belong.
    void			setPrimPath( const UT_StringRef &path )
				    { myPrimPath = path; }
    const UT_StringHolder &	getPrimPath() const
				    { return myPrimPath; }
    /// @}

    /// @{ Set and get the faces in the bucket.
    void			setFaceIndices( const UT_ExintArray &indices )
				    { myFaceIndices = indices; }
    void			addFaceIndex( exint index )
				    { myFaceIndices.append( index ); }
    const UT_ExintArray &	getFaceIndices() const
				    { return myFaceIndices; }
    /// @}

private:
    UT_StringHolder	myPrimPath;	// path of the geo prim containing faces
    UT_ExintArray	myFaceIndices;	// indices of faces in the bucket 
};

#endif

