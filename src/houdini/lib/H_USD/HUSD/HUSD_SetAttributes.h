/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
 *
 * Produced by:
 *      Calvin Gu
 *	Side Effects Software Inc.
 *	123 Front Street West, Suite 1401
 *	Toronto, Ontario
 *      Canada   M5J 2M2
 *	416-504-9876
 *
 */

#ifndef __HUSD_SetAttributes_h__
#define __HUSD_SetAttributes_h__

#include "HUSD_API.h"
#include "HUSD_DataHandle.h"
#include "HUSD_TimeCode.h"
#include <UT/UT_StringHolder.h>

class HUSD_API HUSD_SetAttributes
{
public:
			 HUSD_SetAttributes(HUSD_AutoWriteLock &lock);
			~HUSD_SetAttributes();

    /// @{ Create an attribute or primvar on a primitive.
    bool		 addAttribute(const UT_StringRef &primpath,
				const UT_StringRef &attrname,
				const UT_StringRef &type) const;

    bool		 addPrimvar(const UT_StringRef &primpath,
				const UT_StringRef &primvarname,
				const UT_StringRef &interpolation,
				const UT_StringRef &type) const;
    /// @}

    /// @{ Set an attribute or primvar value on a primitive.
    template<typename UtValueType>
    bool		 setAttribute(const UT_StringRef &primpath,
				const UT_StringRef &attrname,
				const UtValueType &value,
				const HUSD_TimeCode &timecode,
				const UT_StringRef &valueType = 
				    UT_String::getEmptyString()) const;

    template<typename UtValueType>
    bool		 setPrimvar(const UT_StringRef &primpath,
				const UT_StringRef &primvarname,
				const UT_StringRef &interpolation,
				const UtValueType &value,
				const HUSD_TimeCode &timecode,
				const UT_StringRef &valueType = 
				    UT_String::getEmptyString(),
                                int elementsize = 1) const;
    /// @}


    /// @{ Set an attribute or primvar value on a primitive,
    /// but accept the *subclasses* of the UT_Array. 
    template<typename UtValueType>
    bool		 setAttributeArray(const UT_StringRef &primpath,
				const UT_StringRef &attrname,
				const UT_Array<UtValueType> &value,
				const HUSD_TimeCode &timecode,
				const UT_StringRef &valueType = 
				    UT_String::getEmptyString()) const
			 { return setAttribute(primpath, attrname,
				 value, timecode, valueType); }

    template<typename UtValueType>
    bool		 setPrimvarArray(const UT_StringRef &primpath,
				const UT_StringRef &primvarname,
				const UT_StringRef &interpolation,
				const UT_Array<UtValueType> &value,
				const HUSD_TimeCode &timecode,
				const UT_StringRef &valueType = 
				    UT_String::getEmptyString(),
                                int elementsize = 1) const
			 { return setPrimvar(primpath, primvarname, 
				 interpolation, value, timecode, valueType,
                                 elementsize); }
    /// @}

    /// @{ Blocks an attribute or primvar.
    bool		 blockAttribute(const UT_StringRef &primpath,
				const UT_StringRef &attrname) const;
    bool		 blockPrimvar(const UT_StringRef &primpath,
				const UT_StringRef &primvarname) const;
    bool		 blockPrimvarIndices(const UT_StringRef &primpath,
				const UT_StringRef &primvarname) const;
    /// @}

    /// Sets primvar's indices, making it an indexed primvar.
    bool		 setPrimvarIndices( const UT_StringRef &primpath,
				const UT_StringRef &primvar_name,
				const UT_ExintArray &indices,
				const HUSD_TimeCode &timecode) const;

    /// @{ Returns effective time code at which the value should be set.
    /// Eg, if attribute has time samples and the given time code is default,
    /// it needs to be "promoted" to time code for the specific frame/time.
    /// Otherwise, setting value at default time will not take effect, if
    /// there is already a value at that specific frame/time.
    /// This ensures that getAttribute() returns the same value as set here.
    HUSD_TimeCode	 getAttribEffectiveTimeCode(
				const UT_StringRef &primpath,
				const UT_StringRef &attribname,
				const HUSD_TimeCode &timecode) const;
    HUSD_TimeCode	 getPrimvarEffectiveTimeCode(
				const UT_StringRef &primpath,
				const UT_StringRef &primvarname,
				const HUSD_TimeCode &timecode) const;
    HUSD_TimeCode	 getPrimvarIndicesEffectiveTimeCode(
				const UT_StringRef &primpath,
				const UT_StringRef &primvarname,
				const HUSD_TimeCode &timecode) const;
    /// @}

private:
    HUSD_AutoWriteLock	&myWriteLock;
};

#endif
