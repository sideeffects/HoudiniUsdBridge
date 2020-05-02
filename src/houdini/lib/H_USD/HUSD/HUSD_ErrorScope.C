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
#include <pxr/base/tf/errorMark.h>
#include <pxr/base/tf/diagnosticMgr.h>
#include <iostream>
#include <UT/UT_Lock.h>

PXR_NAMESPACE_USING_DIRECTIVE

class HUSD_ErrorDelegate : public TfDiagnosticMgr::Delegate
{
public:
			 HUSD_ErrorDelegate(bool for_html);
                        ~HUSD_ErrorDelegate() override;

    void	         IssueError(const TfError &e) override;
    void	         IssueStatus(const TfStatus &e) override;
    void	         IssueWarning(const TfWarning &e) override;
    void	         IssueFatalError(const TfCallContext &ctx,
				const std::string &e) override;

    void		 getFormattedMessage(const char *msg_in,
				UT_String &msg_out);

    UT_Lock		 myLock;
    UT_ErrorManager	*myMgr;
    OP_Node		*myNode;
    bool		 myForHtml;
    bool		 myPrintStatus;
    bool		 myPrintWarning;
    bool		 myPrintError;
    bool		 myPrintFatal;
};

static HUSD_ErrorDelegate	 theFallbackDelegate(false);

HUSD_ErrorDelegate::HUSD_ErrorDelegate(bool for_html)
    : myMgr(nullptr),
      myNode(nullptr),
      myForHtml(for_html),
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
    if (myNode)
	myNode->appendError(
	    "Common", UT_ERROR_JUST_STRING, msg.c_str(), UT_ERROR_WARNING);
    else if (myMgr)
	myMgr->addWarning("Common", UT_ERROR_JUST_STRING, msg.c_str());
    else if (myPrintError)
	std::cout << msg << std::endl;
}

void
HUSD_ErrorDelegate::IssueStatus(const TfStatus &e)
{
    UT_AutoLock lock(myLock);
    UT_String	msg;

    getFormattedMessage(e.GetCommentary().c_str(), msg);
    if (myNode)
	myNode->appendError(
	    "Common", UT_ERROR_JUST_STRING, msg.c_str(), UT_ERROR_MESSAGE);
    else if (myMgr)
	myMgr->addMessage("Common", UT_ERROR_JUST_STRING, msg.c_str());
    else if (myPrintStatus)
	std::cout << msg << std::endl;
}

void
HUSD_ErrorDelegate::IssueWarning(const TfWarning &e)
{
    UT_AutoLock lock(myLock);
    UT_String	msg;

    getFormattedMessage(e.GetCommentary().c_str(), msg);
    if (myNode)
	myNode->appendError(
	    "Common", UT_ERROR_JUST_STRING, msg.c_str(), UT_ERROR_WARNING);
    else if (myMgr)
	myMgr->addWarning("Common", UT_ERROR_JUST_STRING, msg.c_str());
    else if (myPrintWarning)
	std::cout << msg << std::endl;
}

void
HUSD_ErrorDelegate::IssueFatalError(const TfCallContext &ctx,
	const std::string &e)
{
    UT_AutoLock lock(myLock);
    UT_String	msg;

    getFormattedMessage(e.c_str(), msg);
    if (myNode)
	myNode->appendError(
	    "Common", UT_ERROR_JUST_STRING, msg.c_str(), UT_ERROR_WARNING);
    else if (myMgr)
	myMgr->addWarning("Common", UT_ERROR_JUST_STRING, msg.c_str());
    else if (myPrintFatal)
	std::cout << msg << std::endl;
}

class HUSD_ErrorScope::husd_ErrorScopePrivate
{
public:
    husd_ErrorScopePrivate(UT_ErrorManager *mgr, OP_Node *node, bool for_html)
	: myPrevMgr(nullptr),
	  myPrevNode(nullptr),
	  myOwnsErrorDelegate(false)
    {
	if (!mgr && !node)
	    mgr = UTgetErrorManager();

	// The first scope object creates the error delegate.
	if (!theErrorDelegate)
	{
	    theErrorDelegate = new HUSD_ErrorDelegate(for_html);
	    myOwnsErrorDelegate = true;
	}

	myPrevMgr = theErrorDelegate->myMgr;
	myPrevNode = theErrorDelegate->myNode;
	{
	    UT_AutoLock lock(theErrorDelegate->myLock);

	    theErrorDelegate->myMgr = mgr;
	    theErrorDelegate->myNode = node;
	}
    }

    ~husd_ErrorScopePrivate()
    {
	{
	    UT_AutoLock lock(theErrorDelegate->myLock);

	    theErrorDelegate->myMgr = myPrevMgr;
	    theErrorDelegate->myNode = myPrevNode;
	}

	// If we were the first scope, clean up the error delegate.
	if (myOwnsErrorDelegate)
	{
	    delete theErrorDelegate;
	    theErrorDelegate = nullptr;
	}
    }

    static HUSD_ErrorDelegate	*delegate()
    { return theErrorDelegate; }

private:
    static HUSD_ErrorDelegate	*theErrorDelegate;
    UT_ErrorManager		*myPrevMgr;
    OP_Node			*myPrevNode;
    bool			 myOwnsErrorDelegate;
};

HUSD_ErrorDelegate *
HUSD_ErrorScope::husd_ErrorScopePrivate::theErrorDelegate = nullptr;

HUSD_ErrorScope::HUSD_ErrorScope(bool for_html)
    : myPrivate(new HUSD_ErrorScope::
	husd_ErrorScopePrivate(nullptr, nullptr, for_html))
{
}

HUSD_ErrorScope::HUSD_ErrorScope(UT_ErrorManager *mgr, bool for_html)
    : myPrivate(new HUSD_ErrorScope::
	husd_ErrorScopePrivate(mgr, nullptr, for_html))
{
}

HUSD_ErrorScope::HUSD_ErrorScope(OP_Node *node, bool for_html)
    : myPrivate(new HUSD_ErrorScope::
	husd_ErrorScopePrivate(nullptr, node, for_html))
{
}

HUSD_ErrorScope::~HUSD_ErrorScope()
{
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

