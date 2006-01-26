#ifndef lint
static char rcsid[] = "@(#$Id: mh_exec.cpp,v 1.6 2006-01-26 17:59:50 dockes Exp $ (C) 2005 J.F.Dockes";
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

#ifndef NO_NAMESPACES
using namespace std;
#endif /* NO_NAMESPACES */

class MEAdv : public ExecCmdAdvise {
public:
    void newData(int) {
	CancelCheck::instance().checkCancel();
    }
};

// Execute an external program to translate a file from its native format
// to html. Then call the html parser to do the actual indexing
MimeHandler::Status 
MimeHandlerExec::mkDoc(RclConfig *conf, const string &fn, 
		       const string &mtype, Rcl::Doc &docout, string&)
{
    if (params.empty()) {
	// Hu ho
	LOGERR(("MimeHandlerExec::mkDoc: empty params for mime %s\n",
		mtype.c_str()));
	return MimeHandler::MHError;
    }
    // Command name
    string cmd = conf->findFilter(params.front());
    
    // Build parameter list: delete cmd name and add the file name
    list<string>::iterator it = params.begin();
    list<string>myparams(++it, params.end());
    myparams.push_back(fn);

    // Execute command and store the result text, which is supposedly html
    string html;
    ExecCmd mexec;
    MEAdv adv;
    mexec.setAdvise(&adv);
    mexec.putenv(m_forPreview ? "RECOLL_FILTER_FORPREVIEW=yes" :
		"RECOLL_FILTER_FORPREVIEW=no");
    int status = mexec.doexec(cmd, myparams, 0, &html);
    if (status) {
	LOGERR(("MimeHandlerExec: command status 0x%x: %s\n", 
		status, cmd.c_str()));
	return MimeHandler::MHError;
    }

    // Process/index  the html
    MimeHandlerHtml hh;
    return hh.mkDoc(conf, fn, html, mtype, docout);
}
