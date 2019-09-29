/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
 *
 * Produced by:
 *	Rafeek Rahamut
 *	Side Effects Software Inc.
 *	123 Front Street West, Suite 1401
 *	Toronto, Ontario
 *	Canada   M5J 2M2
 *	416-504-9876
 *
 */

#ifndef __HUSD_GetAttributes_h__
#define __HUSD_GetAttributes_h__

#include "HUSD_API.h"
#include "HUSD_DataHandle.h"
#include "HUSD_TimeCode.h"
#include <UT/UT_StringHolder.h>

enum class HUSD_TimeSampling;


class HUSD_API HUSD_GetAttributes
{
public:
			 HUSD_GetAttributes(HUSD_AutoAnyLock &lock);
			~HUSD_GetAttributes();

    /// @{ Obtains attribute or primvar value.
    template<typename UtValueType>
    bool		 getAttribute(const UT_StringRef &primpath,
				const UT_StringRef &attribname,
				UtValueType &value,
				const HUSD_TimeCode &timecode) const;

    template<typename UtValueType>
    bool		 getPrimvar(const UT_StringRef &primpath,
				const UT_StringRef &primvarname,
				UtValueType &value,
				const HUSD_TimeCode &timecode) const;
    /// @}


    /// @{ Obtains attribute or primvar array value.
    // The following methods perform exactly the same thing as the aboves,
    // but accept the subclasses from the UT_Array. 
    template<typename UtValueType>
    bool		 getAttributeArray(const UT_StringRef &primpath,
				const UT_StringRef &attribname,
				UT_Array<UtValueType> &value,
				const HUSD_TimeCode &timecode) const
    { return getAttribute(primpath, attribname, value, timecode); }

    template<typename UtValueType>
    bool		 getPrimvarArray(const UT_StringRef &primpath,
				const UT_StringRef &primvarname,
				UT_Array<UtValueType> &value,
				const HUSD_TimeCode &timecode) const
    { return getPrimvar(primpath, primvarname, value, timecode); }
    /// @}

    /// Obtains array value of a flattened primvar.
    template<typename UtValueType>
    bool		 getFlattenedPrimvar(const UT_StringRef &primpath,
				const UT_StringRef &primvarname,
				UT_Array<UtValueType> &value,
				const HUSD_TimeCode &timecode) const;

    /// Returns true if the primvar is indexed.
    bool		 isPrimvarIndexed(const UT_StringRef &primpath,
				const UT_StringRef &primvarname) const;

    /// Returns the index array for indexed primvars.
    bool		 getPrimvarIndices(const UT_StringRef &primpath,
				const UT_StringRef &primvarname, 
				UT_ExintArray &indices,
				const HUSD_TimeCode &timecode) const;

    /// Returns the interpolation style of a primvar.
    UT_StringHolder	 getPrimvarInterpolation(const UT_StringRef &primpath,
				const UT_StringRef &primvarname) const;

    /// Returns the exlement size of a primvar.
    exint		 getPrimvarElementSize(const UT_StringRef &primpath,
				const UT_StringRef &primvarname) const;


    /// Returns true if any attribute we have fetched has many time samples.
    bool		 getIsTimeVarying() const;

    /// Returns ture if any attribute we have fetched has time sample(s).
    bool		 getIsTimeSampled() const;

    /// Returns the overal sampling of fethced attributes.
    HUSD_TimeSampling	 getTimeSampling() const 
			 { return myTimeSampling; }

private:
    HUSD_AutoAnyLock		&myAnyLock;
    mutable HUSD_TimeSampling	 myTimeSampling;
};
#endif
