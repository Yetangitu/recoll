#ifndef lint
static char rcsid[] = "@(#$Id: history.cpp,v 1.5 2006-01-23 13:32:28 dockes Exp $ (C) 2005 J.F.Dockes";
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

#ifndef TEST_HISTORY
#include <stdio.h>
#include <time.h>

#include "history.h"
#include "base64.h"
#include "smallut.h"
#include "debuglog.h"

#ifndef NO_NAMESPACES
using namespace std;
#endif

static const char *docSubkey = "docs";

RclDHistory::RclDHistory(const string &fn, unsigned int mxs)
    : m_mlen(mxs), m_data(fn.c_str())
{
}

bool RclDHistory::decodeValue(const string &value, RclDHistoryEntry *e)
{
    list<string> vall;
    stringToStrings(value, vall);
    list<string>::const_iterator it1 = vall.begin();
    if (vall.size() < 2)
	return false;
    e->unixtime = atol((*it1++).c_str());
    base64_decode(*it1++, e->fn);
    if (vall.size() == 3)
	base64_decode(*it1, e->ipath);
    else
	e->ipath.erase();
    return true;
}

bool RclDHistory::enterDocument(const string fn, const string ipath)
{
    LOGDEB(("RclDHistory::enterDocument: [%s] [%s] into %s\n", 
	    fn.c_str(), ipath.c_str(), m_data.getFilename().c_str()));
    // Encode value part: Unix time + base64 of fn + base64 of ipath
    // separated by a space. If ipath is not set, there are only 2 parts
    char chartime[20];
    time_t now = time(0);
    sprintf(chartime, "%ld", (long)now);
    string bfn, bipath;
    base64_encode(fn, bfn);
    base64_encode(ipath, bipath);
    string value = string(chartime) + " " + bfn + " " + bipath;


    LOGDEB1(("Encoded value [%s] (%d)\n", value.c_str(), value.size()));
    // Is this doc already in history ? If it is we remove the old entry
    list<string> names = m_data.getNames(docSubkey);
    list<string>::const_iterator it;
    bool changed = false;
    for (it = names.begin(); it != names.end(); it++) {
	string oval;
	if (!m_data.get(*it, oval, docSubkey)) {
	    LOGDEB(("No data for %s\n", (*it).c_str()));
	    continue;
	}
	RclDHistoryEntry entry;
	decodeValue(oval, &entry);

	if (entry.fn == fn && entry.ipath == ipath) {
	    LOGDEB(("Erasing old entry\n"));
	    m_data.erase(*it, docSubkey);
	    changed = true;
	}
    }

    // Maybe reget list
    if (changed)
	names = m_data.getNames(docSubkey);

    // How many do we have
    if (names.size() >= m_mlen) {
	// Need to erase entries until we're back to size. Note that
	// we don't ever reset numbers. Problems will arise when
	// history is 4 billion entries old
	it = names.begin();
	for (unsigned int i = 0; i < names.size() - m_mlen + 1; i++, it++) {
	    m_data.erase(*it, docSubkey);
	}
    }

    // Increment highest number
    unsigned int hi = names.empty() ? 0 : 
	(unsigned int)atoi(names.back().c_str());
    hi++;
    char nname[20];
    sprintf(nname, "%010u", hi);

    if (!m_data.set(string(nname), value, docSubkey)) {
	LOGERR(("RclDHistory::enterDocument: set failed\n"));
	return false;
    }
    return true;
}

list<RclDHistoryEntry> RclDHistory::getDocHistory()
{
    list<RclDHistoryEntry> mlist;
    RclDHistoryEntry entry;
    list<string> names = m_data.getNames(docSubkey);
    for (list<string>::const_iterator it = names.begin(); 
	 it != names.end(); it++) {
	string value;
	if (m_data.get(*it, value, docSubkey)) {
	    if (!decodeValue(value, &entry))
		continue;
	    mlist.push_front(entry);
	}
    }
    return mlist;
}


#else
#include <string>

#include "history.h"
#include "debuglog.h"

#ifndef NO_NAMESPACES
using namespace std;
#endif

int main(int argc, char **argv)
{
    RclDHistory hist("toto", 5);
    DebugLog::getdbl()->setloglevel(DEBDEB1);
    DebugLog::setfilename("stderr");

    for (int i = 0; i < 10; i++) {
	char docname[100];
	sprintf(docname, "A very long document document name is very long indeed and this is the end of it here and exactly here:\n%d", i);
	hist.enterDocument(string(docname), "ipathx");
    }

    list< pair<string, string> > hlist = hist.getDocHistory();
    for (list< pair<string, string> >::const_iterator it = hlist.begin();
	 it != hlist.end(); it++) {
	printf("[%s] [%s]\n", it->first.c_str(), it->second.c_str());
    }
}

#endif
