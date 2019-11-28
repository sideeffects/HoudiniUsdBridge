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
 *      Side Effects Software Inc
 *      123 Front Street West, Suite 1401
 *      Toronto, Ontario
 *      Canada   M5J 2M2
 *      416-504-9876
 *
 * NAME:    FS_UsdzReaderHelper.H
 *
 * COMMENTS:
 *
 */

#ifndef __FS_USDZ_READER_HELPER_H__
#define __FS_USDZ_READER_HELPER_H__

#include <FS/FS_Reader.h>
#include <FS/FS_Info.h>
#include <HUSD/HUSD_Asset.h>
#include <UT/UT_SharedPtr.h>

class FS_UsdzReaderHelper : public FS_ReaderHelper
{
public:
    FS_UsdzReaderHelper()	    = default;
    virtual ~FS_UsdzReaderHelper()  = default;

    virtual FS_ReaderStream	    *createStream(const char* source,
						  const UT_Options* options);
private:
};

class FS_UsdzInfoHelper : public FS_InfoHelper
{
public:
    FS_UsdzInfoHelper()		    = default;
    virtual ~FS_UsdzInfoHelper()    = default;

    virtual bool		    canHandle(const char* source);
    virtual bool		    hasAccess(const char* source, int mode);
    virtual bool		    getIsDirectory(const char* source);
    virtual int			    getModTime(const char* source);
    virtual int64		    getSize(const char* source);
    virtual bool		    getContents(const char* source,
						UT_StringArray& contents,
						UT_StringArray* dirs);
};

#endif
