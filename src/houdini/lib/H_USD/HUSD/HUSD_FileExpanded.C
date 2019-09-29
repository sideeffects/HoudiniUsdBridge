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

#include <tools/henv.h>
#include <UT/UT_WorkBuffer.h>
#include <UT/UT_VarScan.h>

#include "HUSD_FileExpanded.h"

 /// Holds data for a frame
struct FrameVars
{
    FrameVars(fpreal ff, fpreal inc, int i)
    {
	myFF = ff + i * inc;
	myF = SYSrint(myFF);
	myN = i + 1;
    };

    UT_WorkBuffer	myBuf;
    fpreal		myFF;
    int		myF;
    int		myN;
};

static const char*
doExpand(const char* str, void* vdata)
{
    FrameVars* fvars = (FrameVars*)vdata;
    if (!strcmp(str, "FF"))
    {
	fvars->myBuf.sprintf("%g", fvars->myFF);
    }
    else
    {
	int	ival;
	switch (*str)
	{
	case 'F':
	    ival = fvars->myF;
	    break;
	case 'N':
	    ival = fvars->myN;
	    break;
	default:
	    return HoudiniGetenv(str);
	}

	int	dig = 0;
	for (int i = 1; str[i]; ++i)
	{
	    if (!isdigit(str[i]))
		return HoudiniGetenv(str);
	    dig = dig * 10 + (str[i] - '0');
	}
	if (dig > 255)
	    return HoudiniGetenv(str);
	if (dig > 0)
	    fvars->myBuf.sprintf("%0*d", dig, ival);
	else
	    fvars->myBuf.sprintf("%d", ival);
    }
    return fvars->myBuf.buffer();
}

static const char*
expandPercent(UT_WorkBuffer& store, const char* str,
	      FrameVars& vars, bool& changed)
{
    UT_WorkBuffer	pfmt;
    for (int i = 0; str[i]; ++i)
    {
	if (str[i] == '%')
	{
	    pfmt.clear();
	    for (; str[i]; ++i)
	    {
		if (strchr("eEfFgGaA", str[i]))
		{
		    // Floating point value
		    pfmt.append(str[i]);
		    store.appendSprintf(pfmt.buffer(), vars.myFF);
		    changed = true;
		    break;
		}
		if (strchr("diouxX", str[i]))
		{
		    pfmt.append(str[i]);
		    store.appendSprintf(pfmt.buffer(), vars.myF);
		    changed = true;
		    break;
		}
		if (isdigit(str[i]) || strchr("+-.%", str[i]))
		    pfmt.append(str[i]);
	    }
	}
	else if (str[i] == '<')
	{
	    pfmt.clear();
	    for (++i; str[i]; ++i)
	    {
		if (str[i] == '>')
		{
		    const char* e = doExpand(pfmt.buffer(), &vars);
		    if (e)
		    {
			store.append(e);
			changed = true;
		    }
		    else
		    {
			store.append(pfmt);
			store.append('>');
		    }
		    break;
		}
		pfmt.append(str[i]);
	    }
	}
	else
	{
	    store.append(str[i]);
	}
    }
    return store.buffer();
}

UT_StringHolder
HUSD_FileExpanded::expand(const char* str, fpreal ff, fpreal inc, int i,
			  bool& changed)
{
    FrameVars		fvars(ff, inc, i);
    const char* ofile = str;
    UT_WorkBuffer	 percent_store;

    ofile = expandPercent(percent_store, str, fvars, changed);

    UT_WorkBuffer	expanded;
    if (!UTVariableScan(expanded,
			ofile,
			doExpand,
			&fvars,
			true,		// Tilde expand
			false))		// Comment expand
    {
	return UT_StringHolder(ofile);
    }
    changed = true;
    return UT_StringHolder(expanded);
}