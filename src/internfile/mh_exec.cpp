#ifndef lint
static char rcsid[] = "@(#$Id: mh_exec.cpp,v 1.13 2008-10-06 06:22:46 dockes Exp $ (C) 2005 J.F.Dockes";
#endif
/*
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the
 *   Free Software Foundation, Inc.,
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include "execmd.h"
#include "mh_exec.h"
#include "mh_html.h"
#include "debuglog.h"
#include "cancelcheck.h"
#include "smallut.h"

#include <sys/types.h>
#include <sys/wait.h>

#ifndef NO_NAMESPACES
using namespace std;
#endif /* NO_NAMESPACES */

class MEAdv : public ExecCmdAdvise {
public:
    void newData(int) {
	CancelCheck::instance().checkCancel();
    }
};

// Execute an external program to translate a file from its native
// format to text or html.
bool MimeHandlerExec::next_document()
{
    if (m_havedoc == false)
	return false;
    m_havedoc = false;
    if (missingHelper) {
	LOGDEB(("MimeHandlerExec::next_document(): helper known missing\n"));
	return false;
    }
    if (params.empty()) {
	// Hu ho
	LOGERR(("MimeHandlerExec::mkDoc: empty params\n"));
	m_reason = "RECFILTERROR BADCONFIG";
	return false;
    }

    // Command name
    string cmd = params.front();
    
    // Build parameter list: delete cmd name and add the file name
    list<string>::iterator it = params.begin();
    list<string>myparams(++it, params.end());
    myparams.push_back(m_fn);
    if (!m_ipath.empty())
	myparams.push_back(m_ipath);

    // Execute command, store the output
    string& output = m_metaData["content"];
    output.erase();
    ExecCmd mexec;
    MEAdv adv;
    mexec.setAdvise(&adv);
    mexec.putenv(m_forPreview ? "RECOLL_FILTER_FORPREVIEW=yes" :
		"RECOLL_FILTER_FORPREVIEW=no");
    int status = mexec.doexec(cmd, myparams, 0, &output);

    if (status) {
	LOGERR(("MimeHandlerExec: command status 0x%x for %s\n", 
		status, cmd.c_str()));
	if (WIFEXITED(status) && WEXITSTATUS(status) == 127) {
	    // That's how execmd signals a failed exec (most probably
	    // a missing command). Let'hope no filter uses the same value as
	    // an exit status... Disable myself permanently and signal the 
	    // missing cmd.
	    missingHelper = true;
	    m_reason = string("RECFILTERROR HELPERNOTFOUND ") + cmd;
	} else if (output.find("RECFILTERROR") == 0) {
	    // If the output string begins with RECFILTERROR, then it's 
	    // interpretable error information out from a recoll script
	    m_reason = output;
	    list<string> lerr;
	    stringToStrings(output, lerr);
	    if (lerr.size() > 2) {
		list<string>::iterator it = lerr.begin();
		it++;
		if (*it == "HELPERNOTFOUND") {
		    // No use trying again and again to execute this filter, 
		    // it won't work.
		    missingHelper = true;
		}
	    }		    
	}
	return false;
    }

    // Success. Store some external metadata
    m_metaData["origcharset"] = m_defcharset;
    // Default charset: all recoll filters output utf-8, but this
    // could still be overridden by the content-type meta tag.
    m_metaData["charset"] = cfgCharset.empty() ? "utf-8" : cfgCharset;
    m_metaData["mimetype"] = cfgMtype.empty() ? "text/html" : cfgMtype;
    return true;
}
