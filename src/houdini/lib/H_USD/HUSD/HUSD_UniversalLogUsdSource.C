/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
 *
 * Produced by:
 *      Side Effects Software Inc
 *      123 Front Street West, Suite 1401
 *      Toronto, Ontario
 *      Canada   M5J 2M2
 *      416-504-9876
 *
 * NAME:        FS_UniversalLogSource.C ( FS Library, C++)
 *
 */

#include "HUSD_UniversalLogUsdSource.h"
#include <UT/UT_UniquePtr.h>
#include <pxr/base/tf/diagnosticMgr.h>
#include <pxr/base/tf/errorMark.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace {

    UT_Lock                                  theUsdSourceLock;
    HUSD_UniversalLogUsdSource              *theUsdSource;

    class HUSD_UniversalLoggingDelegate : public TfDiagnosticMgr::Delegate
    {
    public:
        HUSD_UniversalLoggingDelegate()
        { }
        ~HUSD_UniversalLoggingDelegate() override
        { }

        void IssueError(const TfError &e) override
        {
            if (theUsdSource)
                theUsdSource->sendToSinks(UT_UniversalLogEntry(
                    HUSD_UniversalLogUsdSource::staticName(),
                    e.GetCommentary().c_str(),
                    UT_StringHolder::theEmptyString,
                    UT_ERROR_ABORT));
        }
        void IssueStatus(const TfStatus &e) override
        {
            if (theUsdSource)
                theUsdSource->sendToSinks(UT_UniversalLogEntry(
                    HUSD_UniversalLogUsdSource::staticName(),
                    e.GetCommentary().c_str(),
                    UT_StringHolder::theEmptyString,
                    UT_ERROR_MESSAGE));
        }
        void IssueWarning(const TfWarning &e) override
        {
            if (theUsdSource)
                theUsdSource->sendToSinks(UT_UniversalLogEntry(
                    HUSD_UniversalLogUsdSource::staticName(),
                    e.GetCommentary().c_str(),
                    UT_StringHolder::theEmptyString,
                    UT_ERROR_WARNING));
        }
        void IssueFatalError(const TfCallContext &ctx,
                const std::string &e) override
        {
            if (theUsdSource)
                theUsdSource->sendToSinks(UT_UniversalLogEntry(
                    HUSD_UniversalLogUsdSource::staticName(),
                    e.c_str(),
                    UT_StringHolder::theEmptyString,
                    UT_ERROR_FATAL));
        }
    };

    UT_UniquePtr<HUSD_UniversalLoggingDelegate> theDelegate;

};

HUSD_UniversalLogUsdSource::HUSD_UniversalLogUsdSource()
{
    UT_AutoLock scope(theUsdSourceLock);

    UT_ASSERT(!theUsdSource);
    theUsdSource = this;
    if (!theDelegate)
    {
        theDelegate.reset(new HUSD_UniversalLoggingDelegate());
        TfDiagnosticMgr::GetInstance().AddDelegate(theDelegate.get());
    }
}

HUSD_UniversalLogUsdSource::~HUSD_UniversalLogUsdSource()
{
    UT_AutoLock scope(theUsdSourceLock);

    UT_ASSERT(theUsdSource == this);
    if (theUsdSource == this)
    {
        TfDiagnosticMgr::GetInstance().RemoveDelegate(theDelegate.get());
        theUsdSource = nullptr;
    }
}

const UT_StringHolder &
HUSD_UniversalLogUsdSource::staticName()
{
    static const UT_StringLit    theName("USD Logging");

    return theName.asHolder();
}
