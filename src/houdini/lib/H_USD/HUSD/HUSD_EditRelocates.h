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
 *
 */

#ifndef __HUSD_EditRelocates_h__
#define __HUSD_EditRelocates_h__

#include "HUSD_API.h"
#include "HUSD_DataHandle.h"
#include "HUSD_Path.h"
#include <UT/UT_StringHolder.h>
#include <UT/UT_StringMap.h>
#include <UT/UT_Error.h>

class HUSD_API HUSD_EditRelocates
{
public:
    HUSD_EditRelocates(HUSD_AutoWriteLock &lock);
    ~HUSD_EditRelocates();

    // Leaves existing relocate arcs in place by taking the union of existing
    // relocates with the provided map.
    bool setRelocates(const UT_StringMap<UT_StringHolder> &relocatesMap) const;

    // Specify the severity of errors found when validating relocates.
    void setReportingSeverity(UT_ErrorSeverity severity)
    { myReportingSeverity = severity; }
    UT_ErrorSeverity reportingSeverity() const
    { return myReportingSeverity; }

private:
    // Makes sure the specified relocate is allowed. Adds a warning and
    // returns false if not.
    bool validateRelocate(const HUSD_Path &from, const HUSD_Path &to) const;
    // Helper function for the validateRelocate method that constructs and
    // adds a warning, and always returns false.
    bool addRelocateError(const HUSD_Path &from, const HUSD_Path &to,
                          const char *msg) const;

    HUSD_AutoWriteLock  &myWriteLock;
    UT_ErrorSeverity     myReportingSeverity = UT_ERROR_NONE;
};

#endif
