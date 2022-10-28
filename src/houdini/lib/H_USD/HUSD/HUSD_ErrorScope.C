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

#include "HUSD_ErrorScope.h"
#include <OP/OP_Node.h>
#include <UT/UT_ErrorManager.h>
#include <UT/UT_ThreadSpecificValue.h>
#include <pxr/base/tf/errorMark.h>
#include <pxr/base/tf/diagnosticMgr.h>
#include <iostream>
#include <UT/UT_Lock.h>

PXR_NAMESPACE_USING_DIRECTIVE

typedef UT_Map<UT_ErrorSeverity, UT_ErrorSeverity> HUSD_SeverityMapping;

class HUSD_ErrorDelegate : public TfDiagnosticMgr::Delegate
{
public:
			 HUSD_ErrorDelegate();
                        ~HUSD_ErrorDelegate() override;

    void	         IssueError(const TfError &e) override;
    void	         IssueStatus(const TfStatus &e) override;
    void	         IssueWarning(const TfWarning &e) override;
    void	         IssueFatalError(const TfCallContext &ctx,
				const std::string &e) override;

    virtual void         getFormattedMessage(const char *msg_in,
				UT_String &msg_out);

    UT_Lock		 myLock;
    UT_ErrorManager	*myMgr;
    OP_Node		*myNode;
    HUSD_SeverityMapping*mySeverityMapping;
    bool		 myPrintStatus;
    bool		 myPrintWarning;
    bool		 myPrintError;
    bool		 myPrintFatal;
};

class HUSD_FallbackDelegate : public HUSD_ErrorDelegate
{
public:
                         HUSD_FallbackDelegate();
                        ~HUSD_FallbackDelegate() override;

    void                 getFormattedMessage(const char *msg_in,
                                UT_String &msg_out) override;
};

static UT_ThreadSpecificValue<HUSD_ErrorDelegate *>  theErrorDelegate;
static HUSD_FallbackDelegate                         theFallbackDelegate;

HUSD_ErrorDelegate::HUSD_ErrorDelegate()
    : myMgr(nullptr),
      myNode(nullptr),
      mySeverityMapping(nullptr),
      myPrintStatus(false),
      myPrintWarning(false),
      myPrintError(false),
      myPrintFatal(true)
{
    TfDiagnosticMgr::GetInstance().AddDelegate(this);
}

HUSD_ErrorDelegate::~HUSD_ErrorDelegate()
{
    TfDiagnosticMgr::GetInstance().RemoveDelegate(this);

    // After removing this delegate from the global list, grab our lock to make
    // sure we aren't in the middle of issuing an error (which may be coming in
    // from some background thread while we are leaving scope on the main
    // thread).
    UT_AutoLock lock(myLock);
}

void
HUSD_ErrorDelegate::getFormattedMessage(const char *msg_in, UT_String &msg_out)
{
    msg_out = msg_in;
}

void
HUSD_ErrorDelegate::IssueError(const TfError &e)
{
    UT_AutoLock lock(myLock);
    UT_String	msg;

    getFormattedMessage(e.GetCommentary().c_str(), msg);
    if (msg.isstring())
    {
        if (myNode)
        {
            if ((*mySeverityMapping)[UT_ERROR_ABORT] != UT_ERROR_NONE)
                myNode->appendError("Common", UT_ERROR_JUST_STRING, msg.c_str(),
                    (*mySeverityMapping)[UT_ERROR_ABORT]);
        }
        else if (myMgr)
            myMgr->addWarning("Common", UT_ERROR_JUST_STRING, msg.c_str());
        else if (myPrintError)
            std::cout << msg << std::endl;
    }
}

void
HUSD_ErrorDelegate::IssueStatus(const TfStatus &e)
{
    UT_AutoLock lock(myLock);
    UT_String	msg;

    getFormattedMessage(e.GetCommentary().c_str(), msg);
    if (msg.isstring())
    {
        if (myNode)
        {
            if ((*mySeverityMapping)[UT_ERROR_MESSAGE] != UT_ERROR_NONE)
                myNode->appendError("Common", UT_ERROR_JUST_STRING, msg.c_str(),
                    (*mySeverityMapping)[UT_ERROR_MESSAGE]);
        }
        else if (myMgr)
            myMgr->addMessage("Common", UT_ERROR_JUST_STRING, msg.c_str());
        else if (myPrintStatus)
            std::cout << msg << std::endl;
    }
}

void
HUSD_ErrorDelegate::IssueWarning(const TfWarning &e)
{
    UT_AutoLock lock(myLock);
    UT_String	msg;

    getFormattedMessage(e.GetCommentary().c_str(), msg);
    if (msg.isstring())
    {
        if (myNode)
        {
            if ((*mySeverityMapping)[UT_ERROR_WARNING] != UT_ERROR_NONE)
                myNode->appendError("Common", UT_ERROR_JUST_STRING, msg.c_str(),
                    (*mySeverityMapping)[UT_ERROR_WARNING]);
        }
        else if (myMgr)
            myMgr->addWarning("Common", UT_ERROR_JUST_STRING, msg.c_str());
        else if (myPrintWarning)
            std::cout << msg << std::endl;
    }
}

void
HUSD_ErrorDelegate::IssueFatalError(const TfCallContext &ctx,
	const std::string &e)
{
    UT_AutoLock lock(myLock);
    UT_String	msg;

    getFormattedMessage(e.c_str(), msg);
    if (msg.isstring())
    {
        if (myNode)
        {
            if ((*mySeverityMapping)[UT_ERROR_FATAL] != UT_ERROR_NONE)
                myNode->appendError("Common", UT_ERROR_JUST_STRING, msg.c_str(),
                    (*mySeverityMapping)[UT_ERROR_FATAL]);
        }
        else if (myMgr)
            myMgr->addWarning("Common", UT_ERROR_JUST_STRING, msg.c_str());
        else if (myPrintFatal)
            std::cout << msg << std::endl;
    }
}

HUSD_FallbackDelegate::HUSD_FallbackDelegate()
    : HUSD_ErrorDelegate()
{
    myPrintFatal = true;
}

HUSD_FallbackDelegate::~HUSD_FallbackDelegate()
{
}

void
HUSD_FallbackDelegate::getFormattedMessage(const char *msg_in,
        UT_String &msg_out)
{
    // If there is a non-fallback error delegate, the fallback shouldn't do
    // anything. Otherwise let the fallback delegate handle the message.
    if (!theErrorDelegate.get())
        HUSD_ErrorDelegate::getFormattedMessage(msg_in, msg_out);
}

UT_ErrorSeverity
HUSD_ErrorScope::usdOutputMinimumSeverity()
{
    if (theFallbackDelegate.myPrintStatus)
        return UT_ERROR_MESSAGE;
    else if (theFallbackDelegate.myPrintWarning)
        return UT_ERROR_WARNING;
    else if (theFallbackDelegate.myPrintError)
        return UT_ERROR_ABORT;

    return UT_ERROR_FATAL;
}

void
HUSD_ErrorScope::setUsdOutputMinimumSeverity(UT_ErrorSeverity severity)
{
    theFallbackDelegate.myPrintFatal = true;

    if (severity <= UT_ERROR_ABORT)
        theFallbackDelegate.myPrintError = true;
    if (severity <= UT_ERROR_WARNING)
        theFallbackDelegate.myPrintWarning = true;
    if (severity <= UT_ERROR_MESSAGE)
        theFallbackDelegate.myPrintStatus = true;
}

class HUSD_ErrorScope::husd_ErrorScopePrivate
{
public:
    husd_ErrorScopePrivate(UT_ErrorManager *mgr, OP_Node *node)
	: myPrevMgr(nullptr),
	  myPrevNode(nullptr),
	  myOwnsErrorDelegate(false)
    {
        // By default USD messages are turned into Houdini message, but
        // USD warnings and errors are both recorded as Houdini warnings.
        // This is because we don't generally want USD "errors" to result
        // in node cook errors (which can be extremely disruptive).
        mySeverityMapping[UT_ERROR_MESSAGE] = UT_ERROR_MESSAGE;
        mySeverityMapping[UT_ERROR_WARNING] = UT_ERROR_WARNING;
        mySeverityMapping[UT_ERROR_ABORT] = UT_ERROR_WARNING;
        mySeverityMapping[UT_ERROR_FATAL] = UT_ERROR_WARNING;

	if (!mgr && !node)
	    mgr = UTgetErrorManager();

        auto thread_error_delegate = theErrorDelegate.get();

	// The first scope object creates the error delegate.
	if (!thread_error_delegate)
	{
            thread_error_delegate = new HUSD_ErrorDelegate();
            theErrorDelegate.get() = thread_error_delegate;
	    myOwnsErrorDelegate = true;
	}

        myPrevMgr = thread_error_delegate->myMgr;
	myPrevNode = thread_error_delegate->myNode;
        myPrevSeverityMapping = thread_error_delegate->mySeverityMapping;
	{
	    UT_AutoLock lock(thread_error_delegate->myLock);

            thread_error_delegate->myMgr = mgr;
            thread_error_delegate->myNode = node;
            thread_error_delegate->mySeverityMapping = &mySeverityMapping;
	}
    }

    ~husd_ErrorScopePrivate()
    {
        auto thread_error_delegate = theErrorDelegate.get();
	{
	    UT_AutoLock lock(thread_error_delegate->myLock);

            thread_error_delegate->myMgr = myPrevMgr;
            thread_error_delegate->myNode = myPrevNode;
            thread_error_delegate->mySeverityMapping = myPrevSeverityMapping;
	}

	// If we were the first scope, clean up the error delegate.
	if (myOwnsErrorDelegate)
	{
	    delete thread_error_delegate;
	    theErrorDelegate.get() = nullptr;
	}
    }

    static HUSD_ErrorDelegate *delegate()
    { return theErrorDelegate.get(); }

    void adoptPrevSeverityMapping()
    {
        if (myPrevSeverityMapping)
            mySeverityMapping = *myPrevSeverityMapping;
    }
    void setErrorSeverityMapping(UT_ErrorSeverity usd_severity,
            UT_ErrorSeverity hou_severity)
    {
        mySeverityMapping[usd_severity] = hou_severity;
    }

private:
    UT_ErrorManager         *myPrevMgr;
    OP_Node                 *myPrevNode;
    HUSD_SeverityMapping    *myPrevSeverityMapping;
    HUSD_SeverityMapping     mySeverityMapping;
    bool                     myOwnsErrorDelegate;
};

HUSD_ErrorScope::HUSD_ErrorScope()
    : myPrivate(new HUSD_ErrorScope::
	husd_ErrorScopePrivate(nullptr, nullptr))
{
}

HUSD_ErrorScope::HUSD_ErrorScope(UT_ErrorManager *mgr)
    : myPrivate(new HUSD_ErrorScope::
	husd_ErrorScopePrivate(mgr, nullptr))
{
}

HUSD_ErrorScope::HUSD_ErrorScope(OP_Node *node)
    : myPrivate(new HUSD_ErrorScope::
	husd_ErrorScopePrivate(nullptr, node))
{
}

HUSD_ErrorScope::HUSD_ErrorScope(CopyExistingScopeTag)
    : myPrivate(new HUSD_ErrorScope::husd_ErrorScopePrivate(
          theErrorDelegate.get() ? theErrorDelegate.get()->myMgr : nullptr,
          theErrorDelegate.get() ? theErrorDelegate.get()->myNode : nullptr))
{
    myPrivate->adoptPrevSeverityMapping();
}

HUSD_ErrorScope::~HUSD_ErrorScope()
{
}

void
HUSD_ErrorScope::setErrorSeverityMapping(UT_ErrorSeverity usd_severity,
    UT_ErrorSeverity hou_severity)
{
    myPrivate->setErrorSeverityMapping(usd_severity, hou_severity);
}

void
HUSD_ErrorScope::addMessage(int code, const char *msg)
{
    if (husd_ErrorScopePrivate::delegate())
    {
	UT_ErrorManager	*mgr = husd_ErrorScopePrivate::delegate()->myMgr;
	OP_Node		*node = husd_ErrorScopePrivate::delegate()->myNode;

	if (node)
	    node->appendError("HUSD", code, msg, UT_ERROR_MESSAGE);
	else if (mgr)
	    mgr->addMessage("HUSD", code, msg);
    }
}

void
HUSD_ErrorScope::addWarning(int code, const char *msg)
{
    if (husd_ErrorScopePrivate::delegate())
    {
	UT_ErrorManager	*mgr = husd_ErrorScopePrivate::delegate()->myMgr;
	OP_Node		*node = husd_ErrorScopePrivate::delegate()->myNode;

	if (node)
	    node->appendError("HUSD", code, msg, UT_ERROR_WARNING);
	else if (mgr)
	    mgr->addWarning("HUSD", code, msg);
    }
}

void
HUSD_ErrorScope::addError(int code, const char *msg)
{
    if (husd_ErrorScopePrivate::delegate())
    {
	UT_ErrorManager	*mgr = husd_ErrorScopePrivate::delegate()->myMgr;
	OP_Node		*node = husd_ErrorScopePrivate::delegate()->myNode;

	if (node)
	    node->appendError("HUSD", code, msg, UT_ERROR_ABORT);
	else if (mgr)
	    mgr->addError("HUSD", code, msg);
    }
}

