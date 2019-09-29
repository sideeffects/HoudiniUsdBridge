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

#ifndef __HUSD_Cvex__
#define __HUSD_Cvex__

#include "HUSD_API.h"
#include "HUSD_DataHandle.h"
#include "HUSD_TimeCode.h"
#include "HUSD_Utils.h"
#include <UT/UT_UniquePtr.h>

class husd_CvexResults;
class HUSD_CvexBindingMap;
class HUSD_CvexCode;
class HUSD_CvexDataCommand;
class HUSD_CvexDataInputs;
class HUSD_CvexRunData;
class HUSD_PrimsBucket;
class HUSD_FacesBucket;
enum class HUSD_TimeSampling;
class UT_OpCaller;
class UT_StringArray;


class HUSD_API HUSD_Cvex
{
public:
		 HUSD_Cvex();
		~HUSD_Cvex();

    /// Sets the ID of a node that executes the CVEX script. 
    /// It is used for channels evaluation in VEX as well as error reporting.
    void	 setCwdNodeId( int cwd_node_id ) ;

    /// Sets the caller object that keeps track of dependencies on any node
    /// referenced with the 'op:' syntax in code.
    void	 setOpCaller( UT_OpCaller *caller );

    /// Sets an object that resolves a stage based on handle, which are used
    /// in calls to VEX functions that operate on USD data (eg, primitives).
    /// Handles are strings that usually refer to LOP node inputs "opinput:0".
    void	 setDataInputs( HUSD_CvexDataInputs *vex_geo_inputs );

    /// Sets an object that processes VEX functions that modify the USD data.
    void	 setDataCommand( HUSD_CvexDataCommand *vex_geo_command );

    /// Sets the time code at which attributes are evaluated and/or set.
    void	 setTimeCode( const HUSD_TimeCode &time_code );

    /// Sets the cvex script bindings map (cvex parm -> usd prim attrib).
    void	 setBindingsMap( const HUSD_CvexBindingMap *map );

    /// Set the name of the array attibute whose length should be used
    /// as a hint about the number of array elements to run cvex on.
    /// NOTE: This is the lower bound, and the actual number may be higher 
    ///	    than that if CVEX code references some larger array attribute.
    void	 setArraySizeHintAttrib( const UT_StringRef &attrib_name );


    /// Runs the CVEX script on the USD primitives, setting their attributes.
    // TODO: implement ability to use vexpression in addition to command,
    //       ie, use HUSD_CvexCode as parameter type.
    bool	 runOverPrimitives( HUSD_AutoAnyLock &lock,
                        const HUSD_FindPrims &findprims,
			const UT_StringRef &cvex_command ) const;
    bool	 applyRunOverPrimitives(HUSD_AutoWriteLock &writelock) const;

    /// Runs the CVEX script on the array attribute of USD primitives, 
    /// setting their elements.
    // TODO: implement ability to use vexpression in addition to command,
    //       ie, use HUSD_CvexCode as parameter type.
    bool	 runOverArrayElements( HUSD_AutoAnyLock &lock,
                        const HUSD_FindPrims &findprims,
			const UT_StringRef &cvex_command ) const;
    bool	 applyRunOverArrayElements(HUSD_AutoWriteLock &writelock) const;

    /// Gets the primitives for which the given cvex command (ie, its first int 
    /// output) or a vexpression returns a non-zero value.
    // TODO: implement ability to specify a set of primitive paths to test
    bool	 matchPrimitives( HUSD_AutoAnyLock &lock,
                        UT_StringArray &matched_prims_paths, 
			const HUSD_CvexCode &code,
			HUSD_PrimTraversalDemands demands) const;

    /// Partitions the primitives specified by findprims, given 
    /// the CVEX script whose output values are used to define buckets.
    /// When @p code's return type is void, all outputs are used for 
    /// partitioning; otherwise, the first output of a given type is used 
    /// (converted to a string keyword value).
    bool	 partitionPrimitives( HUSD_AutoAnyLock &lock,
                        UT_Array<HUSD_PrimsBucket> &buckets,
			const HUSD_FindPrims &findprims,
			const HUSD_CvexCode &code ) const;
    
    /// Get the faces for which the given cvex command (ie, its first int 
    /// output) or a vexpression returns a non-zero value.
    bool	 matchFaces( HUSD_AutoAnyLock &lock,
                        UT_ExintArray &matched_faces_indices,
			const UT_StringRef &geo_prim_path,
			const UT_ExintArray *face_indices,
			const HUSD_CvexCode &code ) const;

    /// Get the faces for which the given cvex command (ie, its first int 
    /// output) or a vexpression returns a non-zero value.
    bool	 matchInstances( HUSD_AutoAnyLock &lock,
                        UT_ExintArray &matched_instance_indices,
			const UT_StringRef &instancer_prim_path,
                        const UT_ExintArray *point_indices,
			const HUSD_CvexCode &code ) const;

    /// Partitions the face set specified by the @p geo_prim_path and
    /// @p face_indices, given the CVEX script whose output values are used
    /// to define buckets. When @p code's return type is void, all outputs
    /// are used for partitioning; otherwise, the first output of a given
    /// type is used (converted to a string keyword value).
    /// If @p face_indices is null, then all faces of the prim are partitioned.
    bool	 partitionFaces( HUSD_AutoAnyLock &lock,
                        UT_Array<HUSD_FacesBucket> &buckets,
			const UT_StringRef &geo_prim_path,
			const UT_ExintArray *face_indices,
			const HUSD_CvexCode &code ) const;

    /// Returns true if any attribute the CVEX has run on has many time samples.
    bool	 getIsTimeVarying() const;

    /// Returns ture if any attribute the CVEX has run on has time sample(s).
    bool	 getIsTimeSampled() const;

protected:
    const HUSD_CvexBindingMap &	    getBindingsMap() const;

private:
    UT_UniquePtr<HUSD_CvexRunData>                       myRunData;
    mutable UT_Array<UT_SharedPtr<husd_CvexResults> >    myResults;
    UT_StringHolder					 myArraySizeHintAttrib;

    // Max level of sampling among bound attributes.
    mutable HUSD_TimeSampling		                myTimeSampling;
};

#endif

