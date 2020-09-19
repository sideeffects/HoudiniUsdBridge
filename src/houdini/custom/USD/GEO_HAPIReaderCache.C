/*
 * Copyright 2020 Side Effects Software Inc.
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
 */

#include "GEO_HAPIReaderCache.h"
#include <UT/UT_EnvControl.h>
#include <UT/UT_Exit.h>
#include <UT/UT_FileUtil.h>
#include <UT/UT_Lock.h>
#include <UT/UT_Thread.h>
#include <thread>
#include <typeinfo>

#define READER_CACHE_TIMEOUT 90

//
// GEO_HAPIReaderKey
//

GEO_HAPIReaderKey::GEO_HAPIReaderKey() : myFileModTime(-1) {}

GEO_HAPIReaderKey::GEO_HAPIReaderKey(
        const UT_StringRef& filePath,
        const UT_StringRef& assetName)
    : myFilePath(filePath), myAssetName(assetName)
{
    myFileModTime = UT_FileUtil::getFileModTime(filePath);
}

UT_CappedKey*
GEO_HAPIReaderKey::duplicate() const
{
    GEO_HAPIReaderKey* out = new GEO_HAPIReaderKey(*this);
    return out;
}

unsigned int
GEO_HAPIReaderKey::getHash() const
{
    size_t hash = SYShash(myAssetName);
    SYShashCombine(hash, myFilePath);
    SYShashCombine(hash, myFileModTime);
    return (unsigned int)hash;
}

bool
GEO_HAPIReaderKey::isEqual(const UT_CappedKey& key) const
{
    const GEO_HAPIReaderKey* other
            = UTverify_cast<const GEO_HAPIReaderKey*>(&key);

    return (other->myAssetName == myAssetName)
           && (other->myFilePath == myFilePath)
           && (other->myFileModTime == myFileModTime);
}

//
// GEO_HAPIReaderCache
//

static bool theExitInitialized = false;

static UT_Thread&
timeoutThread()
{
    static UT_Thread* theTimeoutThread(
            UT_Thread::allocThread(UT_Thread::SpinMode::ThreadLowUsage, false));

    return *theTimeoutThread;
}

static UT_CappedCache&
readerCache()
{
    static UT_CappedCache theCache(
            "GEO_HAPIReaderCache",
            UT_EnvControl::getInt(
                    UT_IntControl::ENV_HOUDINI_HDADYNAMICPAYLOAD_CACHESIZE));
    if (!theExitInitialized)
    {
        theExitInitialized = true;
        GEO_HAPIReaderCache::initExitCallback();
    }
    return theCache;
}

static void
readerCacheExitCB(void* data)
{
    timeoutThread().killThread();
    delete &timeoutThread();
    readerCache().clear();
}

void
GEO_HAPIReaderCache::initExitCallback()
{
    UT_Exit::addExitCallback(readerCacheExitCB, nullptr);
}

static UT_SpinLock theTimeoutLock;

static UT_StopWatch&
timeoutStopWatch()
{
    static UT_StopWatch theStopWatch;
    return theStopWatch;
}

// Clear the cache after a period of inactivity
static void*
waitForTimeout(void* data)
{
    theTimeoutLock.lock();
    fpreal64 t = timeoutStopWatch().getTime();
    theTimeoutLock.unlock();

    while (t < READER_CACHE_TIMEOUT)
    {
	// Sleep for the minimum time until a timeout is required
        int diff = (int)(READER_CACHE_TIMEOUT - t + 1);
        std::this_thread::sleep_for(std::chrono::seconds(diff));

	// The stopwatch may have reset while this thread was sleeping
        theTimeoutLock.lock();
        t = timeoutStopWatch().getTime();
        theTimeoutLock.unlock();
    }

    readerCache().clear();

    return nullptr;
}

static void
startTimeout()
{
    UT_AutoSpinLock l(theTimeoutLock);
    timeoutStopWatch().start();

    if (!timeoutThread().isActive())
        timeoutThread().startThread(waitForTimeout, nullptr);
}

GEO_HAPIReaderHandle
GEO_HAPIReaderCache::pop(const GEO_HAPIReaderKey& key)
{
    GEO_HAPIReaderHandle ret(
            static_cast<GEO_HAPIReader*>(readerCache().findItem(key).get()));
    readerCache().deleteItem(key);
    startTimeout();
    return ret;
}

void
GEO_HAPIReaderCache::push(
        const GEO_HAPIReaderKey& key,
        const GEO_HAPIReaderHandle& reader)
{
    readerCache().addItem(key, reader);
    startTimeout();
}
