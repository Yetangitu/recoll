#ifndef lint
static char rcsid[] = "@(#$Id: rcldb.cpp,v 1.88 2006-11-09 17:37:16 dockes Exp $ (C) 2004 J.F.Dockes";
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
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fnmatch.h>
#include <regex.h>

#include <iostream>
#include <string>
#include <vector>
#include <algorithm>

#ifndef NO_NAMESPACES
using namespace std;
#endif /* NO_NAMESPACES */
#define RCLDB_INTERNAL

#include "rcldb.h"
#include "stemdb.h"
#include "textsplit.h"
#include "transcode.h"
#include "unacpp.h"
#include "conftree.h"
#include "debuglog.h"
#include "pathut.h"
#include "smallut.h"
#include "pathhash.h"
#include "utf8iter.h"
#include "searchdata.h"

#include "xapian.h"

#ifndef MAX
#define MAX(A,B) (A>B?A:B)
#endif
#ifndef MIN
#define MIN(A,B) (A<B?A:B)
#endif

#undef MTIME_IN_VALUE

#ifndef NO_NAMESPACES
namespace Rcl {
#endif
    
// Max length for path terms stored for each document. Truncate
// longer path and uniquize with hash. The goal for this is to avoid
// xapian max term length limitations, not to gain space (we gain very
// little even with very short maxlens like 30)
#define PATHHASHLEN 150

// Synthetic abstract marker (to discriminate from abstract actually
// found in doc)
const static string rclSyntAbs = "?!#@";

// A class for data and methods that would have to expose
// Xapian-specific stuff if they were in Rcl::Db. There could actually be
// 2 different ones for indexing or query as there is not much in
// common.
class Native {
 public:
    Db *m_db;
    bool m_isopen;
    bool m_iswritable;

    // Indexing
    Xapian::WritableDatabase wdb;

    // Querying
    Xapian::Database db;
    Xapian::Query    query; // query descriptor: terms and subqueries
			    // joined by operators (or/and etc...)
    Xapian::Enquire *enquire; // Open query descriptor.
    Xapian::MSet     mset;    // Partial result set

    Native(Db *db) 
	: m_db(db),
	  m_isopen(false), m_iswritable(false), enquire(0) 
    { }

    ~Native() {
	if (m_iswritable)
	    LOGDEB(("Rcl::Db: xapian will close. Flush may take some time\n"));
	delete enquire;
    }

    string makeAbstract(Xapian::docid id, const list<string>& terms);

    bool dbDataToRclDoc(std::string &data, Doc &doc, 
			int qopts,
			Xapian::docid docid,
			const list<string>& terms);

    /** Compute list of subdocuments for a given path (given by hash) 
     *  We look for all Q terms beginning with the path/hash
     *  As suggested by James Aylett, a better method would be to add 
     *  a single term (ie: XP/path/to/file) to all subdocs, then finding
     *  them would be a simple matter of retrieving the posting list for the
     *  term. There would still be a need for the current Qterm though, as a
     *  unique term for replace_document, and for retrieving by
     *  path/ipath (history)
     */
    bool subDocs(const string &hash, vector<Xapian::docid>& docids);

    /** Keep this inline */
    bool filterMatch(Db *rdb, Xapian::Document &xdoc) {
	// Parse xapian document's data and populate doc fields
	string data = xdoc.get_data();
	ConfSimple parms(&data);

	// The only filtering for now is on file path (subtree)
	string url;
	parms.get(string("url"), url);
	url = url.substr(7);
	LOGDEB2(("Rcl::Db::Native:filter filter [%s] fn [%s]\n",
		 rdb->m_filterTopDir.c_str(), url.c_str()));
	if (url.find(rdb->m_filterTopDir) == 0) 
	    return true;
	return false;
    }
};

    /* See comment in class declaration */
bool Native::subDocs(const string &hash, vector<Xapian::docid>& docids) 
{
    docids.clear();
    string qterm = "Q"+ hash + "|";
    string ermsg;

    for (int tries = 0; tries < 2; tries++) {
	try {
	    Xapian::TermIterator it = db.allterms_begin(); 
	    it.skip_to(qterm);
	    for (;it != db.allterms_end(); it++) {
		// If current term does not begin with qterm or has
		// another |, not the same file
		if ((*it).find(qterm) != 0 || 
		    (*it).find_last_of("|") != qterm.length() -1)
		    break;
		docids.push_back(*(db.postlist_begin(*it)));
	    }
	    return true;
	} catch (const Xapian::DatabaseModifiedError &e) {
	    LOGDEB(("Db::subDocs: got modified error. reopen/retry\n"));
	    // Can't use reOpen here, it would delete *me*
	    db = Xapian::Database(m_db->m_basedir);
	} catch (const Xapian::Error &e) {
	    ermsg = e.get_msg().c_str();
	    break;
	} catch (...) {
	    ermsg= "Unknown error";
	    break;
	}
    }
    LOGERR(("Rcl::Db::subDocs: %s\n", ermsg.c_str()));
    return false;
}


/* Rcl::Db methods ///////////////////////////////// */

Db::Db() 
    : m_ndb(0), m_qOpts(QO_NONE), m_idxAbsTruncLen(250), m_synthAbsLen(250),
      m_synthAbsWordCtxLen(4), m_mode(Db::DbRO)
{
    m_ndb = new Native(this);
}

Db::~Db()
{
    LOGDEB1(("Db::~Db\n"));
    if (m_ndb == 0)
	return;
    LOGDEB(("Db::~Db: isopen %d m_iswritable %d\n", m_ndb->m_isopen, 
	    m_ndb->m_iswritable));
    const char *ermsg = "Unknown error";
    try {
	// Used to do a flush here, but doesnt seem necessary
	delete m_ndb;
	m_ndb = 0;
	return;
    } catch (const Xapian::Error &e) {
	ermsg = e.get_msg().c_str();
    } catch (const string &s) {
	ermsg = s.c_str();
    } catch (const char *s) {
	ermsg = s;
    } catch (...) {
	ermsg = "Caught unknown exception";
    }
    LOGERR(("Db::~Db: got exception: %s\n", ermsg));
}

bool Db::open(const string& dir, OpenMode mode, int qops)
{
    bool keep_updated = (qops & QO_KEEP_UPDATED) != 0;
    qops &= ~QO_KEEP_UPDATED;

    if (m_ndb == 0)
	return false;
    LOGDEB(("Db::open: m_isopen %d m_iswritable %d\n", m_ndb->m_isopen, 
	    m_ndb->m_iswritable));

    if (m_ndb->m_isopen) {
	// We used to return an error here but I see no reason to
	if (!close())
	    return false;
    }
    const char *ermsg = "Unknown";
    try {
	switch (mode) {
	case DbUpd:
	case DbTrunc: 
	    {
		int action = (mode == DbUpd) ? Xapian::DB_CREATE_OR_OPEN :
		    Xapian::DB_CREATE_OR_OVERWRITE;
		m_ndb->wdb = Xapian::WritableDatabase(dir, action);
		m_ndb->m_iswritable = true;
		// We open a readonly object in addition to the r/w
		// one because some operations are faster when
		// performed through a Database (no forced flushes on
		// allterms_begin(), ie, used in subDocs()
		m_ndb->db = Xapian::Database(dir);
		LOGDEB(("Db::open: lastdocid: %d\n", 
			m_ndb->wdb.get_lastdocid()));
		if (!keep_updated) {
		    LOGDEB2(("Db::open: resetting updated\n"));
		    updated.resize(m_ndb->wdb.get_lastdocid() + 1);
		    for (unsigned int i = 0; i < updated.size(); i++)
			updated[i] = false;
		}
	    }
	    break;
	case DbRO:
	default:
	    m_ndb->m_iswritable = false;
	    m_ndb->db = Xapian::Database(dir);
	    for (list<string>::iterator it = m_extraDbs.begin();
		 it != m_extraDbs.end(); it++) {
		string aerr;
		LOGDEB(("Db::Open: adding query db [%s]\n", it->c_str()));
		aerr.erase();
		try {
		    // Make this non-fatal
		    m_ndb->db.add_database(Xapian::Database(*it));
		} catch (const Xapian::Error &e) {
		    aerr = e.get_msg().c_str();
		} catch (const string &s) {
		    aerr = s.c_str();
		} catch (const char *s) {
		    aerr = s;
		} catch (...) {
		    aerr = "Caught unknown exception";
		}
		if (!aerr.empty())
		    LOGERR(("Db::Open: error while trying to add database "
			    "from [%s]: %s\n", it->c_str(), aerr.c_str()));
	    }
	    break;
	}
	m_mode = mode;
	m_ndb->m_isopen = true;
	m_basedir = dir;
	return true;
    } catch (const Xapian::Error &e) {
	ermsg = e.get_msg().c_str();
    } catch (const string &s) {
	ermsg = s.c_str();
    } catch (const char *s) {
	ermsg = s;
    } catch (...) {
	ermsg = "Caught unknown exception";
    }
    LOGERR(("Db::open: exception while opening [%s]: %s\n", 
	    dir.c_str(), ermsg));
    return false;
}

string Db::getDbDir()
{
    return m_basedir;
}

// Note: xapian has no close call, we delete and recreate the db
bool Db::close()
{
    if (m_ndb == 0)
	return false;
    LOGDEB(("Db::close(): m_isopen %d m_iswritable %d\n", m_ndb->m_isopen, 
	    m_ndb->m_iswritable));
    if (m_ndb->m_isopen == false)
	return true;
    const char *ermsg = "Unknown";
    try {
	// Used to do a flush here. Cant see why it should be necessary.
	delete m_ndb;
	m_ndb = new Native(this);
	if (m_ndb)
	    return true;
    } catch (const Xapian::Error &e) {
	ermsg = e.get_msg().c_str();
    } catch (const string &s) {
	ermsg = s.c_str();
    } catch (const char *s) {
	ermsg = s;
    } catch (...) {
	ermsg = "Caught unknown exception";
    }
    LOGERR(("Db:close: exception while deleting db: %s\n", ermsg));
    return false;
}

bool Db::reOpen()
{
    if (m_ndb && m_ndb->m_isopen) {
	if (!close())
	    return false;
	if (!open(m_basedir, m_mode, m_qOpts | QO_KEEP_UPDATED)) {
	    return false;
	}
    }
    return true;
}

int Db::docCnt()
{
    if (m_ndb && m_ndb->m_isopen) {
	return m_ndb->m_iswritable ? m_ndb->wdb.get_doccount() : 
	    m_ndb->db.get_doccount();
    }
    return -1;
}

bool Db::addQueryDb(const string &dir) 
{
    LOGDEB(("Db::addQueryDb: ndb %p iswritable %d db [%s]\n", m_ndb,
	      (m_ndb)?m_ndb->m_iswritable:0, dir.c_str()));
    if (!m_ndb)
	return false;
    if (m_ndb->m_iswritable)
	return false;
    if (find(m_extraDbs.begin(), m_extraDbs.end(), dir) == m_extraDbs.end()) {
	m_extraDbs.push_back(dir);
    }
    return reOpen();
}

bool Db::rmQueryDb(const string &dir)
{
    if (!m_ndb)
	return false;
    if (m_ndb->m_iswritable)
	return false;
    if (dir.empty()) {
	m_extraDbs.clear();
    } else {
	list<string>::iterator it = find(m_extraDbs.begin(), 
					 m_extraDbs.end(), dir);
	if (it != m_extraDbs.end()) {
	    m_extraDbs.erase(it);
	}
    }
    return reOpen();
}
bool Db::testDbDir(const string &dir)
{
    string aerr;
    LOGDEB(("Db::testDbDir: [%s]\n", dir.c_str()));
    try {
	Xapian::Database db(dir);
    } catch (const Xapian::Error &e) {
	aerr = e.get_msg().c_str();
    } catch (const string &s) {
	aerr = s.c_str();
    } catch (const char *s) {
	aerr = s;
    } catch (...) {
	aerr = "Caught unknown exception";
    }
    if (!aerr.empty()) {
	LOGERR(("Db::Open: error while trying to open database "
		"from [%s]: %s\n", dir.c_str(), aerr.c_str()));
	return false;
    }
    return true;
}

bool Db::isopen()
{
    if (m_ndb == 0)
	return false;
    return m_ndb->m_isopen;
}

// A small class to hold state while splitting text
class mySplitterCB : public TextSplitCB {
 public:
    Xapian::Document &doc;
    Xapian::termpos basepos; // Base for document section
    Xapian::termpos curpos;  // Last position sent to callback
    mySplitterCB(Xapian::Document &d) : doc(d), basepos(1), curpos(0)
    {}
    bool takeword(const std::string &term, int pos, int, int);
};

// Callback for the document to word splitting class during indexation
bool mySplitterCB::takeword(const std::string &term, int pos, int, int)
{
#if 0
    LOGDEB(("mySplitterCB::takeword:splitCb: [%s]\n", term.c_str()));
    string printable;
    if (transcode(term, printable, "UTF-8", "ISO-8859-1")) {
	LOGDEB(("                                [%s]\n", printable.c_str()));
    }
#endif

    const char *ermsg;
    try {
	// Note: 1 is the within document frequency increment. It would 
	// be possible to assign different weigths to doc parts (ie title)
	// by using a higher value
	curpos = pos;
	doc.add_posting(term, basepos + curpos, 1);
	return true;
    } catch (const Xapian::Error &e) {
	ermsg = e.get_msg().c_str();
    } catch (...) {
	ermsg= "Unknown error";
    }
    LOGERR(("Db: xapian add_posting error %s\n", ermsg));
    return false;
}

// Unaccent and lowercase data, replace \n\r with spaces
// Removing crlfs is so that we can use the text in the document data fields.
// Use unac (with folding extension) for removing accents and casefolding
//
// Note that we always return true (but set out to "" on error). We don't
// want to stop indexation because of a bad string
bool dumb_string(const string &in, string &out)
{
    out.erase();
    if (in.empty())
	return true;

    string s1 = neutchars(in, "\n\r");
    if (!unacmaybefold(s1, out, "UTF-8", true)) {
	LOGINFO(("dumb_string: unac failed for [%s]\n", in.c_str()));
	out.erase();
	// See comment at start of func
	return true;
    }
    return true;
}

// Let our user set the parameters for abstract processing
void Db::setAbstractParams(int idxtrunc, int syntlen, int syntctxlen)
{
    LOGDEB1(("Db::setAbstractParams: trunc %d syntlen %d ctxlen %d\n",
	    idxtrunc, syntlen, syntctxlen));
    if (idxtrunc > 0)
	m_idxAbsTruncLen = idxtrunc;
    if (syntlen > 0)
	m_synthAbsLen = syntlen;
    if (syntctxlen > 0)
	m_synthAbsWordCtxLen = syntctxlen;
}

// Add document in internal form to the database: index the terms in
// the title abstract and body and add special terms for file name,
// date, mime type ... , create the document data record (more
// metadata), and update database
bool Db::add(const string &fn, const Doc &idoc, 
		  const struct stat *stp)
{
    LOGDEB1(("Db::add: fn %s\n", fn.c_str()));
    if (m_ndb == 0)
	return false;

    Doc doc = idoc;

    // Truncate abstract, title and keywords to reasonable lengths. If
    // abstract is currently empty, we make up one with the beginning
    // of the document. This is then not indexed, but part of the doc
    // data so that we can return it to a query without having to
    // decode the original file.
    bool syntabs = false;
    if (doc.abstract.empty()) {
	syntabs = true;
	doc.abstract = rclSyntAbs + 
	    truncate_to_word(doc.text, m_idxAbsTruncLen);
    } else {
	doc.abstract = truncate_to_word(doc.abstract, m_idxAbsTruncLen);
    }
    doc.abstract = neutchars(doc.abstract, "\n\r");
    doc.title = truncate_to_word(doc.title, 100);
    doc.keywords = truncate_to_word(doc.keywords, 300);

    Xapian::Document newdocument;

    mySplitterCB splitData(newdocument);

    TextSplit splitter(&splitData);

    // /////// Split and index terms in document body and auxiliary fields
    string noacc;

    // Split and index file name as document term(s)
    LOGDEB2(("Db::add: split file name [%s]\n", fn.c_str()));
    if (dumb_string(doc.utf8fn, noacc)) {
	splitter.text_to_words(noacc);
	splitData.basepos += splitData.curpos + 100;
    }

    // Split and index title
    LOGDEB2(("Db::add: split title [%s]\n", doc.title.c_str()));
    if (!dumb_string(doc.title, noacc)) {
	LOGERR(("Db::add: dumb_string failed\n"));
	return false;
    }
    splitter.text_to_words(noacc);
    splitData.basepos += splitData.curpos + 100;

    // Split and index body
    LOGDEB2(("Db::add: split body\n"));
    if (!dumb_string(doc.text, noacc)) {
	LOGERR(("Db::add: dumb_string failed\n"));
	return false;
    }
    splitter.text_to_words(noacc);
    splitData.basepos += splitData.curpos + 100;

    // Split and index keywords
    LOGDEB2(("Db::add: split kw [%s]\n", doc.keywords.c_str()));
    if (!dumb_string(doc.keywords, noacc)) {
	LOGERR(("Db::add: dumb_string failed\n"));
	return false;
    }
    splitter.text_to_words(noacc);
    splitData.basepos += splitData.curpos + 100;

    // Split and index abstract. We don't do this if it is synthetic
    // any more (this used to give a relevance boost to the beginning
    // of text, why ?)
    LOGDEB2(("Db::add: split abstract [%s]\n", doc.abstract.c_str()));
    if (!syntabs) {
	// syntabs indicator test kept here in case we want to go back
	// to indexing synthetic abstracts one day
	if (!dumb_string(syntabs ? doc.abstract.substr(rclSyntAbs.length()) : 
			 doc.abstract, noacc)) {
	    LOGERR(("Db::add: dumb_string failed\n"));
	    return false;
	}
	splitter.text_to_words(noacc);
    }
    splitData.basepos += splitData.curpos + 100;

    ////// Special terms for metadata
    // Mime type
    newdocument.add_term("T" + doc.mimetype);

    // Simple file name. This is used for file name searches only. We index
    // it with a term prefix. utf8fn used to be the full path, but it's now
    // the simple file name.
    if (dumb_string(doc.utf8fn, noacc) && !noacc.empty()) {
	noacc = string("XSFN") + noacc;
	newdocument.add_term(noacc);
    }

    // Pathname/ipath terms. This is used for file existence/uptodate
    // checks, and unique id for the replace_document() call 

    // Truncate the filepath part to a reasonable length and
    // replace the truncated part with a hopefully unique hash
    string hash;
    pathHash(fn, hash, PATHHASHLEN);
    LOGDEB2(("Db::add: pathhash [%s]\n", hash.c_str()));

    // Unique term: makes unique identifier for documents
    // either path or path+ipath inside multidocument files.
    // We only add a path term if ipath is empty. Else there will be a qterm
    // (path+ipath), and a pseudo-doc will be created to stand for the file 
    // itself (for up to date checks). This is handled by 
    // DbIndexer::processone() 
    string uniterm;
    if (doc.ipath.empty()) {
	uniterm = "P" + hash;
#ifdef MTIME_IN_VALUE
	newdocument.add_value(1, doc.fmtime);
#endif
    } else {
	uniterm = "Q" + hash + "|" + doc.ipath;
    }
    newdocument.add_term(uniterm);

    // Dates etc...
    time_t mtime = atol(doc.dmtime.empty() ? doc.fmtime.c_str() : 
			doc.dmtime.c_str());
    struct tm *tm = localtime(&mtime);
    char buf[9];
    sprintf(buf, "%04d%02d%02d",tm->tm_year+1900, tm->tm_mon + 1, tm->tm_mday);
    newdocument.add_term("D" + string(buf)); // Date (YYYYMMDD)
    buf[7] = '\0';
    if (buf[6] == '3') buf[6] = '2';
    newdocument.add_term("W" + string(buf)); // "Weak" - 10ish day interval
    buf[6] = '\0';
    newdocument.add_term("M" + string(buf)); // Month (YYYYMM)
    buf[4] = '\0';
    newdocument.add_term("Y" + string(buf)); // Year (YYYY)

    // Document data record. omindex has the following nl separated fields:
    // - url
    // - sample
    // - caption (title limited to 100 chars)
    // - mime type 
    string record = "url=file://" + fn;
    record += "\nmtype=" + doc.mimetype;
    record += "\nfmtime=" + doc.fmtime;
    if (!doc.dmtime.empty()) {
	record += "\ndmtime=" + doc.dmtime;
    }
    record += "\norigcharset=" + doc.origcharset;
    char sizebuf[20]; 
    sizebuf[0] = 0;
    if (stp)
	sprintf(sizebuf, "%ld", (long)stp->st_size);
    if (sizebuf[0])
	record += string("\nfbytes=") + sizebuf;
    sprintf(sizebuf, "%u", (unsigned int)doc.text.length());
    record += string("\ndbytes=") + sizebuf;
    if (!doc.ipath.empty()) {
	record += "\nipath=" + doc.ipath;
    }
    record += "\ncaption=" + doc.title;
    record += "\nkeywords=" + doc.keywords;
    record += "\nabstract=" + doc.abstract;
    record += "\n";
    LOGDEB1(("Newdocument data: %s\n", record.c_str()));
    newdocument.set_data(record);

    const char *fnc = fn.c_str();
    // Add db entry or update existing entry:
    try {
	Xapian::docid did = 
	    m_ndb->wdb.replace_document(uniterm, newdocument);
	if (did < updated.size()) {
	    updated[did] = true;
	    LOGDEB(("Db::add: docid %d updated [%s , %s]\n", did, fnc,
		    doc.ipath.c_str()));
	} else {
	    LOGDEB(("Db::add: docid %d added [%s , %s]\n", did, fnc, 
		    doc.ipath.c_str()));
	}
    } catch (...) {
	// FIXME: is this ever actually needed?
	try {
	    m_ndb->wdb.add_document(newdocument);
	    LOGDEB(("Db::add: %s added (failed re-seek for duplicate)\n", 
		    fnc));
	} catch (...) {
	    LOGERR(("Db::add: failed again after replace_document\n"));
	    return false;
	}
    }
    return true;
}

// Test if given filename has changed since last indexed:
bool Db::needUpdate(const string &filename, const struct stat *stp)
{
    //    Chrono chron;
    if (m_ndb == 0)
	return false;

    string hash;
    pathHash(filename, hash, PATHHASHLEN);
    string pterm  = "P" + hash;
    string ermsg;

    // Look for all documents with this path. We need to look at all
    // to set their existence flag.  We check the update time on the
    // fmtime field which will be identical for all docs inside a
    // multi-document file (we currently always reindex all if the
    // file changed)
    for (int tries = 0; tries < 2; tries++) {
	try {
	    // Check the date using the Pterm doc or pseudo-doc
	    Xapian::PostingIterator docid = m_ndb->db.postlist_begin(pterm);
	    if (docid == m_ndb->db.postlist_end(pterm)) {
		// If no document exist with this path, we do need update
		LOGDEB2(("Db::needUpdate: no path: [%s]\n", pterm.c_str()));
		return true;
	    }
	    Xapian::Document doc = m_ndb->db.get_document(*docid);

	    // Retrieve file modification time from db stored value
#ifdef MTIME_IN_VALUE
	    // This is slightly faster, but we'd need to setup a conversion
	    // for old dbs, and it's not really worth it
	    string value = doc.get_value(1);
	    const char *cp = value.c_str();
#else
	    string data = doc.get_data();
	    const char *cp = strstr(data.c_str(), "fmtime=");
	    if (cp) {
		cp += 7;
	    } else {
		cp = strstr(data.c_str(), "mtime=");
		if (cp)
		    cp+= 6;
	    }
#endif
	    time_t mtime = cp ? atoll(cp) : 0;

	    // Retrieve file size as stored in db data
	    cp = strstr(data.c_str(), "fbytes=");
	    if (cp)
		cp += 7; 
	    off_t fbytes = cp ? atoll(cp) : 0;

	    // Compare db time and size data to filesystem's
	    if (mtime != stp->st_mtime || fbytes != stp->st_size) {
		LOGDEB2(("Db::needUpdate:yes: mtime: D %ld F %ld."
			 "sz D %ld F %ld\n", long(mtime), long(stp->st_mtime),
			 long(fbytes), long(stp->st_size)));
		// Db is not up to date. Let's index the file
		return true;
	    } 

	    LOGDEB2(("Db::needUpdate: uptodate: [%s]\n", pterm.c_str()));

	    // Up to date. 

	    // Set the uptodate flag for doc / pseudo doc
	    updated[*docid] = true;

	    // Set the existence flag for all the subdocs (if any)
	    vector<Xapian::docid> docids;
	    if (!m_ndb->subDocs(hash, docids)) {
		LOGERR(("Rcl::Db::needUpdate: can't get subdocs list\n"));
		return true;
	    }
	    for (vector<Xapian::docid>::iterator it = docids.begin();
		 it != docids.end(); it++) {
		if (*it < updated.size()) {
		    LOGDEB2(("Db::needUpdate: set flag for docid %d\n", *it));
		    updated[*it] = true;
		}
	    }
	    //	    LOGDEB(("Db::needUpdate: used %d mS\n", chron.millis()));
	    return false;
	} catch (const Xapian::DatabaseModifiedError &e) {
	    LOGDEB(("Db::needUpdate: got modified error. reopen/retry\n"));
	    reOpen();
	} catch (const Xapian::Error &e) {
	    ermsg = e.get_msg();
	    break;
	} catch (...) {
	    ermsg= "Unknown error";
	    break;
	}
    }
    LOGERR(("Db::needUpdate: error while checking existence: %s\n", 
	    ermsg.c_str()));
    return true;
}


// Return list of existing stem db languages
list<string> Db::getStemLangs()
{
    LOGDEB(("Db::getStemLang\n"));
    list<string> dirs;
    if (m_ndb == 0 || m_ndb->m_isopen == false)
	return dirs;
    dirs = StemDb::getLangs(m_basedir);
    return dirs;
}

/**
 * Delete stem db for given language
 */
bool Db::deleteStemDb(const string& lang)
{
    LOGDEB(("Db::deleteStemDb(%s)\n", lang.c_str()));
    if (m_ndb == 0 || m_ndb->m_isopen == false)
	return false;
    return StemDb::deleteDb(m_basedir, lang);
}

/**
 * Create database of stem to parents associations for a given language.
 * We walk the list of all terms, stem them, and create another Xapian db
 * with documents indexed by a single term (the stem), and with the list of
 * parent terms in the document data.
 */
bool Db::createStemDb(const string& lang)
{
    LOGDEB(("Db::createStemDb(%s)\n", lang.c_str()));
    if (m_ndb == 0 || m_ndb->m_isopen == false)
	return false;

    return StemDb:: createDb(m_ndb->m_iswritable ? m_ndb->wdb : m_ndb->db, 
			     m_basedir, lang);
}

/**
 * This is called at the end of an indexing session, to delete the
 * documents for files that are no longer there. This can ONLY be called
 * after a full file-system tree walk, else the file existence flags will 
 * be wrong.
 */
bool Db::purge()
{
    LOGDEB(("Db::purge\n"));
    if (m_ndb == 0)
	return false;
    LOGDEB(("Db::purge: m_isopen %d m_iswritable %d\n", m_ndb->m_isopen, 
	    m_ndb->m_iswritable));
    if (m_ndb->m_isopen == false || m_ndb->m_iswritable == false) 
	return false;

    // There seems to be problems with the document delete code, when
    // we do this, the database is not actually updated. Especially,
    // if we delete a bunch of docs, so that there is a hole in the
    // docids at the beginning, we can't add anything (appears to work
    // and does nothing). Maybe related to the exceptions below when
    // trying to delete an unexistant document ?
    // Flushing before trying the deletes seeems to work around the problem
    try {
	m_ndb->wdb.flush();
    } catch (...) {
	LOGDEB(("Db::purge: 1st flush failed\n"));
    }
    for (Xapian::docid docid = 1; docid < updated.size(); ++docid) {
	if (!updated[docid]) {
	    try {
		m_ndb->wdb.delete_document(docid);
		LOGDEB(("Db::purge: deleted document #%d\n", docid));
	    } catch (const Xapian::DocNotFoundError &) {
		LOGDEB(("Db::purge: document #%d not found\n", docid));
	    }
	}
    }
    try {
	m_ndb->wdb.flush();
    } catch (...) {
	LOGDEB(("Db::purge: 2nd flush failed\n"));
    }
    return true;
}

/** Delete document(s) for given filename */
bool Db::purgeFile(const string &fn)
{
    LOGDEB(("Db:purgeFile: [%s]\n", fn.c_str()));
    if (m_ndb == 0)
	return false;
    Xapian::WritableDatabase db = m_ndb->wdb;
    string hash;
    pathHash(fn, hash, PATHHASHLEN);
    string pterm  = "P" + hash;
    const char *ermsg = "";
    try {
	Xapian::PostingIterator docid = db.postlist_begin(pterm);
	if (docid == db.postlist_end(pterm))
	    return true;
	LOGDEB(("purgeFile: delete docid %d\n", *docid));
	db.delete_document(*docid);
	vector<Xapian::docid> docids;
	m_ndb->subDocs(hash, docids);
	LOGDEB(("purgeFile: subdocs cnt %d\n", docids.size()));
	for (vector<Xapian::docid>::iterator it = docids.begin();
	     it != docids.end(); it++) {
	    LOGDEB2(("Db::purgeFile: delete subdoc %d\n", *it));
	    db.delete_document(*it);
	}
	return true;
    } catch (const Xapian::Error &e) {
	ermsg = e.get_msg().c_str();
    } catch (const string &s) {
	ermsg = s.c_str();
    } catch (const char *s) {
	ermsg = s;
    } catch (...) {
	ermsg = "Caught unknown exception";
    }
    if (*ermsg) {
	LOGERR(("Db::purgeFile: %s\n", ermsg));
    }
    return false;
}

// Splitter callback for breaking query into terms
class wsQData : public TextSplitCB {
 public:
    vector<string> terms;
    string catterms() {
	string s;
	for (unsigned int i=0;i<terms.size();i++) {
	    s += "[" + terms[i] + "] ";
	}
	return s;
    }
    bool takeword(const std::string &term, int , int, int) {
	LOGDEB1(("wsQData::takeword: %s\n", term.c_str()));
	terms.push_back(term);
	return true;
    }
    void dumball() {
	for (vector<string>::iterator it=terms.begin(); it !=terms.end();it++){
	    string dumb;
	    dumb_string(*it, dumb);
	    *it = dumb;
	}
    }
};

// Turn string into list of xapian queries. There is little
// interpretation done on the string (no +term -term or filename:term
// stuff). We just separate words and phrases, and interpret
// capitalized terms as wanting no stem expansion. 
// The final list contains one query for each term or phrase
//   - Elements corresponding to a stem-expanded part are an OP_OR
//     composition of the stem-expanded terms (or a single term query).
//   - Elements corresponding to a phrase are an OP_PHRASE composition of the
//     phrase terms (no stem expansion in this case)
static void stringToXapianQueries(const string &iq,
				  const string& stemlang,
				  Db *db,
				  list<Xapian::Query> &pqueries,
				  unsigned int opts = Db::QO_NONE)
{
    string qstring = iq;

    // Split into (possibly single word) phrases ("this is a phrase"):
    list<string> phrases;
    stringToStrings(qstring, phrases);

    // Then process each phrase: split into terms and transform into
    // appropriate Xapian Query

    for (list<string>::iterator it=phrases.begin(); it !=phrases.end(); it++) {
	LOGDEB(("strToXapianQ: phrase or word: [%s]\n", it->c_str()));

	wsQData splitData;
	TextSplit splitter(&splitData, true);
	splitter.text_to_words(*it);
	LOGDEB1(("strToXapianQ: splitter term count: %d\n", 
		splitData.terms.size()));
	switch(splitData.terms.size()) {
	case 0: continue;// ??
	case 1: // Not a real phrase: one term
	    {
		string term = splitData.terms.front();
		bool nostemexp = false;
		// Check if the first letter is a majuscule in which
		// case we do not want to do stem expansion. Note that
		// the test is convoluted and possibly problematic
		if (term.length() > 0) {
		    string noacterm,noaclowterm;
		    if (unacmaybefold(term, noacterm, "UTF-8", false) &&
			unacmaybefold(noacterm, noaclowterm, "UTF-8", true)) {
			Utf8Iter it1(noacterm);
			Utf8Iter it2(noaclowterm);
			if (*it1 != *it2)
			    nostemexp = true;
		    }
		}
		LOGDEB1(("Term: %s stem expansion: %s\n", 
			term.c_str(), nostemexp?"no":"yes"));

		list<string> exp;  
		string term1;
		dumb_string(term, term1);
		// Possibly perform stem compression/expansion
		if (!nostemexp && (opts & Db::QO_STEM)) {
		    exp = db->stemExpand(stemlang, term1);
		} else {
		    exp.push_back(term1);
		}

		// Push either term or OR of stem-expanded set
		pqueries.push_back(Xapian::Query(Xapian::Query::OP_OR, 
						 exp.begin(), exp.end()));
	    }
	    break;

	default:
	    // Phrase: no stem expansion
	    splitData.dumball();
	    LOGDEB(("Pushing phrase: [%s]\n", splitData.catterms().c_str()));
	    pqueries.push_back(Xapian::Query(Xapian::Query::OP_PHRASE,
					     splitData.terms.begin(),
					     splitData.terms.end()));
	}
    }
}

// Prepare query out of "advanced search" data
bool Db::setQuery(AdvSearchData &sdata, int opts, const string& stemlang)
{
    LOGDEB(("Db::setQuery: adv:\n"));
    LOGDEB((" allwords: %s\n", sdata.allwords.c_str()));
    LOGDEB((" phrase:   %s\n", sdata.phrase.c_str()));
    LOGDEB((" orwords:  %s\n", sdata.orwords.c_str()));
    LOGDEB((" orwords1:  %s\n", sdata.orwords1.c_str()));
    LOGDEB((" nowords:  %s\n", sdata.nowords.c_str()));
    LOGDEB((" filename:  %s\n", sdata.filename.c_str()));

    string ft;
    for (list<string>::iterator it = sdata.filetypes.begin(); 
    	 it != sdata.filetypes.end(); it++) {ft += *it + " ";}
    if (!ft.empty()) 
	LOGDEB((" searched file types: %s\n", ft.c_str()));
    if (!sdata.topdir.empty())
	LOGDEB((" restricted to: %s\n", sdata.topdir.c_str()));
    LOGDEB((" Options: 0x%x\n", opts));

    m_filterTopDir = sdata.topdir;
    m_dbindices.clear();

    if (!m_ndb)
	return false;
    list<Xapian::Query> pqueries;
    Xapian::Query xq;

    m_qOpts = opts;

    if (!sdata.filename.empty()) {
	LOGDEB((" filename search\n"));
	// File name search, with possible wildcards. 
	// We expand wildcards by scanning the filename terms (prefixed 
        // with XSFN) from the database. 
	// We build an OR query with the expanded values if any.
	string pattern;
	dumb_string(sdata.filename, pattern);

	// If pattern is not quoted, and has no wildcards, we add * at
	// each end: match any substring
	if (pattern[0] == '"' && pattern[pattern.size()-1] == '"') {
	    pattern = pattern.substr(1, pattern.size() -2);
	} else if (pattern.find_first_of("*?[") == string::npos) {
	    pattern = "*" + pattern + "*";
	} // else let it be

	LOGDEB((" pattern: [%s]\n", pattern.c_str()));

	// Match pattern against all file names in the db
	Xapian::TermIterator it = m_ndb->db.allterms_begin(); 
	it.skip_to("XSFN");
	list<string> names;
	for (;it != m_ndb->db.allterms_end(); it++) {
	    if ((*it).find("XSFN") != 0)
		break;
	    string fn = (*it).substr(4);
	    LOGDEB2(("Matching [%s] and [%s]\n", pattern.c_str(), fn.c_str()));
	    if (fnmatch(pattern.c_str(), fn.c_str(), 0) != FNM_NOMATCH) {
		names.push_back((*it).c_str());
	    }
	    // Limit the match count
	    if (names.size() > 1000) {
		LOGERR(("Db::SetQuery: too many matched file names\n"));
		break;
	    }
	}
	if (names.empty()) {
	    // Build an impossible query: we know its impossible because we
	    // control the prefixes!
	    names.push_back("XIMPOSSIBLE");
	}
	// Build a query out of the matching file name terms.
	xq = Xapian::Query(Xapian::Query::OP_OR, names.begin(), names.end());
    }

    if (!sdata.allwords.empty()) {
	stringToXapianQueries(sdata.allwords, stemlang, this,pqueries,m_qOpts);
	if (!pqueries.empty()) {
	    Xapian::Query nq = 
		Xapian::Query(Xapian::Query::OP_AND, pqueries.begin(),
			      pqueries.end());
	    xq = xq.empty() ? nq :
		Xapian::Query(Xapian::Query::OP_AND, xq, nq);
	    pqueries.clear();
	}
    }

    if (!sdata.orwords.empty()) {
	stringToXapianQueries(sdata.orwords, stemlang, this,pqueries,m_qOpts);
	if (!pqueries.empty()) {
	    Xapian::Query nq = 
		Xapian::Query(Xapian::Query::OP_OR, pqueries.begin(),
			       pqueries.end());
	    xq = xq.empty() ? nq :
		Xapian::Query(Xapian::Query::OP_AND, xq, nq);
	    pqueries.clear();
	}
    }

    if (!sdata.orwords1.empty()) {
	stringToXapianQueries(sdata.orwords1, stemlang, this,pqueries,m_qOpts);
	if (!pqueries.empty()) {
	    Xapian::Query nq = 
		Xapian::Query(Xapian::Query::OP_OR, pqueries.begin(),
			       pqueries.end());
	    xq = xq.empty() ? nq :
		Xapian::Query(Xapian::Query::OP_AND, xq, nq);
	    pqueries.clear();
	}
    }

    if (!sdata.phrase.empty()) {
	Xapian::Query nq;
	string s = string("\"") + sdata.phrase + string("\"");
	stringToXapianQueries(s, stemlang, this, pqueries);
	if (!pqueries.empty()) {
	    // There should be a single list element phrase query.
	    xq = xq.empty() ? *pqueries.begin() : 
		Xapian::Query(Xapian::Query::OP_AND, xq, *pqueries.begin());
	    pqueries.clear();
	}
    }

    if (!sdata.filetypes.empty()) {
	Xapian::Query tq;
	for (list<string>::iterator it = sdata.filetypes.begin(); 
	     it != sdata.filetypes.end(); it++) {
	    string term = "T" + *it;
	    LOGDEB(("Adding file type term: [%s]\n", term.c_str()));
	    tq = tq.empty() ? Xapian::Query(term) : 
		Xapian::Query(Xapian::Query::OP_OR, tq, Xapian::Query(term));
	}
	xq = xq.empty() ? tq : Xapian::Query(Xapian::Query::OP_FILTER, xq, tq);
    }

    // "And not" part. Must come last, as we have to check it's not
    // the only term in the query.  We do no stem expansion on 'No'
    // words. Should we ?
    if (!sdata.nowords.empty()) {
	stringToXapianQueries(sdata.nowords, stemlang, this, pqueries);
	if (!pqueries.empty()) {
	    Xapian::Query nq;
	    nq = Xapian::Query(Xapian::Query::OP_OR, pqueries.begin(),
			       pqueries.end());
	    if (xq.empty()) {
		// Xapian cant do this currently. Have to have a positive 
		// part!
		sdata.description = "Error: pure negative query\n";
		LOGERR(("Rcl::Db::setQuery: error: pure negative query\n"));
		return false;
	    }
	    xq = Xapian::Query(Xapian::Query::OP_AND_NOT, xq, nq);
	    pqueries.clear();
	}
    }

    m_ndb->query = xq;
    delete m_ndb->enquire;
    m_ndb->enquire = new Xapian::Enquire(m_ndb->db);
    m_ndb->enquire->set_query(m_ndb->query);
    m_ndb->mset = Xapian::MSet();
    // Get the query description and trim the "Xapian::Query"
    sdata.description = m_ndb->query.get_description();
    if (sdata.description.find("Xapian::Query") == 0)
	sdata.description = sdata.description.substr(strlen("Xapian::Query"));
    LOGDEB(("Db::SetQuery: Q: %s\n", sdata.description.c_str()));
    return true;
}

// Characters that can begin a wildcard or regexp expression. We use skipto
// to begin the allterms search with terms that begin with the portion of
// the input string prior to these chars.
const string wildSpecChars = "*?[";
const string regSpecChars = "(.[{";

// Find all index terms that match a wildcard or regular expression
bool Db::termMatch(MatchType typ, const string &root, list<string>& res,
		     const string &lang, int max)
{
    if (!m_ndb || !m_ndb->m_isopen)
	return false;
    Xapian::Database db = m_ndb->m_iswritable ? m_ndb->wdb: m_ndb->db;
    res.clear();
    // Get rid of capitals and accents
    string droot;
    dumb_string(root, droot);
    string nochars = typ == ET_WILD ? wildSpecChars : regSpecChars;

    regex_t reg;
    int errcode;
    // Compile regexp. We anchor the input by enclosing it in ^ and $
    if (typ == ET_REGEXP) {
	string mroot = droot;
	if (mroot.at(0) != '^')
	    mroot = string("^") + mroot;
	if (mroot.at(mroot.length()-1) != '$')
	    mroot += "$";
	if ((errcode = regcomp(&reg, mroot.c_str(), REG_EXTENDED|REG_NOSUB))) {
	    char errbuf[200];
	    regerror(errcode, &reg, errbuf, 199);
	    LOGERR(("termMatch: regcomp failed: %s\n", errbuf));
	    res.push_back(errbuf);
	    regfree(&reg);
	    return false;
	}
    }

    // Find the initial section before any special char
    string::size_type es = droot.find_first_of(nochars);
    string is;
    switch (es) {
    case string::npos: is = droot;break;
    case 0: break;
    default: is = droot.substr(0, es);break;
    }
    LOGDEB(("termMatch: initsec: [%s]\n", is.c_str()));

    Xapian::TermIterator it = db.allterms_begin(); 
    if (!is.empty())
	it.skip_to(is.c_str());
    for (int n = 0;it != db.allterms_end(); it++) {
        // If we're beyond the terms matching the initial string, end
	if (!is.empty() && (*it).find(is) != 0)
	    break;
	// Don't match special internal terms beginning with uppercase ascii
	if ((*it).at(0) >= 'A' && (*it).at(0) <= 'Z')
	    continue;
	if (typ == ET_WILD) {
	    if (fnmatch(droot.c_str(), (*it).c_str(), 0) == FNM_NOMATCH)
		continue;
	} else {
	    if (regexec(&reg, (*it).c_str(), 0, 0, 0))
		continue;
	}
	// Do we want stem expansion here? We don't do it for now
	if (1 || lang.empty()) {
	    res.push_back(*it);
	    ++n;
	} else {
	    list<string> stemexps = stemExpand(lang, *it);
	    unsigned int cnt = 
		(int)stemexps.size() > max - n ? max - n : stemexps.size();
	    list<string>::iterator sit = stemexps.begin();
	    while (cnt--) {
		res.push_back(*sit++);
		n++;
	    }
	}
	if (n >= max)
	    break;
    }
    res.sort();
    res.unique();
    if (typ == ET_REGEXP) {
	regfree(&reg);
    }
    return true;
}

/** Term list walking. */
class TermIter {
public:
    Xapian::TermIterator it;
    Xapian::Database db;
};
TermIter *Db::termWalkOpen()
{
    if (!m_ndb || !m_ndb->m_isopen)
	return 0;
    TermIter *tit = new TermIter;
    if (tit) {
	tit->db = m_ndb->m_iswritable ? m_ndb->wdb: m_ndb->db;
	tit->it = tit->db.allterms_begin();
    }
    return tit;
}
bool Db::termWalkNext(TermIter *tit, string &term)
{
    
    if (tit && tit->it != tit->db.allterms_end()) {
	term = *(tit->it)++;
	return true;
    }
    return false;
}
void Db::termWalkClose(TermIter *tit)
{
    delete tit;
}


bool Db::termExists(const string& word)
{
    if (!m_ndb || !m_ndb->m_isopen)
	return 0;
    Xapian::Database db = m_ndb->m_iswritable ? m_ndb->wdb: m_ndb->db;
    if (!db.term_exists(word))
	return false;
    return true;
}

list<string> Db::stemExpand(const string& lang, const string& term) 
{
    list<string> dirs = m_extraDbs;
    dirs.push_front(m_basedir);
    list<string> exp;
    for (list<string>::iterator it = dirs.begin();
	 it != dirs.end(); it++) {
	list<string> more = StemDb::stemExpand(*it, lang, term);
	LOGDEB1(("Db::stemExpand: Got %d from %s\n", 
		 more.size(), it->c_str()));
	exp.splice(exp.end(), more);
    }
    exp.sort();
    exp.unique();
    LOGDEB1(("Db:::stemExpand: final count %d \n", exp.size()));
    return exp;
}

bool Db::stemDiffers(const string& lang, const string& word, 
		     const string& base)
{
    Xapian::Stem stemmer(lang);
    if (!stemmer.stem_word(word).compare(stemmer.stem_word(base))) {
	LOGDEB2(("Rcl::Db::stemDiffers: same for %s and %s\n", 
		word.c_str(), base.c_str()));
	return false;
    }
    return true;
}

bool Db::getQueryTerms(list<string>& terms)
{
    if (!m_ndb)
	return false;

    terms.clear();
    Xapian::TermIterator it;
    try {
	for (it = m_ndb->query.get_terms_begin(); 
	     it != m_ndb->query.get_terms_end(); it++) {
	    terms.push_back(*it);
	}
    } catch (...) {
	return false;
    }
    return true;
}

bool Db::getMatchTerms(const Doc& doc, list<string>& terms)
{
    if (!m_ndb || !m_ndb->enquire) {
	LOGERR(("Db::getMatchTerms: no query opened\n"));
	return -1;
    }

    terms.clear();
    Xapian::TermIterator it;
    Xapian::docid id = Xapian::docid(doc.xdocid);
    try {
	for (it=m_ndb->enquire->get_matching_terms_begin(id);
	     it != m_ndb->enquire->get_matching_terms_end(id); it++) {
	    terms.push_back(*it);
	}
    } catch (...) {
	return false;
    }
    return true;
}

// Mset size
static const int qquantum = 30;

int Db::getResCnt()
{
    if (!m_ndb || !m_ndb->enquire) {
	LOGERR(("Db::getResCnt: no query opened\n"));
	return -1;
    }
    if (m_ndb->mset.size() <= 0) {
	try {
	    m_ndb->mset = m_ndb->enquire->get_mset(0, qquantum);
	} catch (const Xapian::DatabaseModifiedError &error) {
	    m_ndb->db.reopen();
	    m_ndb->mset = m_ndb->enquire->get_mset(0, qquantum);
	} catch (const Xapian::Error & error) {
	    LOGERR(("enquire->get_mset: exception: %s\n", 
		    error.get_msg().c_str()));
	    return -1;
	}
    }

    return m_ndb->mset.get_matches_lower_bound();
}

bool Native::dbDataToRclDoc(std::string &data, Doc &doc, 
			    int qopts,
			    Xapian::docid docid, const list<string>& terms)
{
    LOGDEB1(("Db::dbDataToRclDoc: opts %x data: %s\n", qopts, data.c_str()));
    ConfSimple parms(&data);
    if (!parms.ok())
	return false;
    parms.get(string("url"), doc.url);
    parms.get(string("mtype"), doc.mimetype);
    parms.get(string("fmtime"), doc.fmtime);
    parms.get(string("dmtime"), doc.dmtime);
    parms.get(string("origcharset"), doc.origcharset);
    parms.get(string("caption"), doc.title);
    parms.get(string("keywords"), doc.keywords);
    parms.get(string("abstract"), doc.abstract);
    // Possibly remove synthetic abstract indicator (if it's there, we
    // used to index the beginning of the text as abstract).
    bool syntabs = false;
    if (doc.abstract.find(rclSyntAbs) == 0) {
	doc.abstract = doc.abstract.substr(rclSyntAbs.length());
	syntabs = true;
    }
    // If the option is set and the abstract is synthetic or empty , build 
    // abstract from position data. 
    if ((qopts & Db::QO_BUILD_ABSTRACT) && !terms.empty()) {
	LOGDEB(("dbDataToRclDoc:: building abstract from position data\n"));
	if (doc.abstract.empty() || syntabs || 
	    (qopts & Db::QO_REPLACE_ABSTRACT))
	    doc.abstract = makeAbstract(docid, terms);
    } 
    parms.get(string("ipath"), doc.ipath);
    parms.get(string("fbytes"), doc.fbytes);
    parms.get(string("dbytes"), doc.dbytes);
    doc.xdocid = docid;
    return true;
}

// Get document at rank i in query (i is the index in the whole result
// set, as in the enquire class. We check if the current mset has the
// doc, else ask for an other one. We use msets of 10 documents. Don't
// know if the whole thing makes sense at all but it seems to work.
//
// If there is a postquery filter (ie: file names), we have to
// maintain a correspondance from the sequential external index
// sequence to the internal Xapian hole-y one (the holes being the documents 
// that dont match the filter).
bool Db::getDoc(int exti, Doc &doc, int *percent)
{
    LOGDEB1(("Db::getDoc: exti %d\n", exti));
    if (!m_ndb || !m_ndb->enquire) {
	LOGERR(("Db::getDoc: no query opened\n"));
	return false;
    }

    // For now the only post-query filter is on dir subtree
    bool postqfilter = !m_filterTopDir.empty();
    LOGDEB1(("Topdir %s postqflt %d\n", m_asdata.topdir.c_str(), postqfilter));

    int xapi;
    if (postqfilter) {
	// There is a postquery filter, does this fall in already known area ?
	if (exti >= (int)m_dbindices.size()) {
	    // Have to fetch xapian docs and filter until we get
	    // enough or fail
	    m_dbindices.reserve(exti+1);
	    // First xapian doc we fetch is the one after last stored 
	    int first = m_dbindices.size() > 0 ? m_dbindices.back() + 1 : 0;
	    // Loop until we get enough docs
	    while (exti >= (int)m_dbindices.size()) {
		LOGDEB(("Db::getDoc: fetching %d starting at %d\n",
			qquantum, first));
		try {
		    m_ndb->mset = m_ndb->enquire->get_mset(first, qquantum);
		} catch (const Xapian::DatabaseModifiedError &error) {
		    m_ndb->db.reopen();
		    m_ndb->mset = m_ndb->enquire->get_mset(first, qquantum);
		} catch (const Xapian::Error & error) {
		  LOGERR(("enquire->get_mset: exception: %s\n", 
			  error.get_msg().c_str()));
		  abort();
		}

		if (m_ndb->mset.empty()) {
		    LOGDEB(("Db::getDoc: got empty mset\n"));
		    return false;
		}
		first = m_ndb->mset.get_firstitem();
		for (unsigned int i = 0; i < m_ndb->mset.size() ; i++) {
		    LOGDEB(("Db::getDoc: [%d]\n", i));
		    Xapian::Document xdoc = m_ndb->mset[i].get_document();
		    if (m_ndb->filterMatch(this, xdoc)) {
			m_dbindices.push_back(first + i);
		    }
		}
		first = first + m_ndb->mset.size();
	    }
	}
	xapi = m_dbindices[exti];
    } else {
	xapi = exti;
    }


    // From there on, we work with a xapian enquire item number. Fetch it
    int first = m_ndb->mset.get_firstitem();
    int last = first + m_ndb->mset.size() -1;

    if (!(xapi >= first && xapi <= last)) {
	LOGDEB(("Fetching for first %d, count %d\n", xapi, qquantum));
	try {
	  m_ndb->mset = m_ndb->enquire->get_mset(xapi, qquantum);
	} catch (const Xapian::DatabaseModifiedError &error) {
	    m_ndb->db.reopen();
	    m_ndb->mset = m_ndb->enquire->get_mset(xapi, qquantum);
	} catch (const Xapian::Error & error) {
	  LOGERR(("enquire->get_mset: exception: %s\n", 
		  error.get_msg().c_str()));
	  abort();
	}
	if (m_ndb->mset.empty())
	    return false;
	first = m_ndb->mset.get_firstitem();
	last = first + m_ndb->mset.size() -1;
    }

    LOGDEB1(("Db::getDoc: Qry [%s] win [%d-%d] Estimated results: %d",
	     m_ndb->query.get_description().c_str(), 
	     first, last,
	     m_ndb->mset.get_matches_lower_bound()));

    Xapian::Document xdoc = m_ndb->mset[xapi-first].get_document();
    Xapian::docid docid = *(m_ndb->mset[xapi-first]);
    if (percent)
	*percent = m_ndb->mset.convert_to_percent(m_ndb->mset[xapi-first]);

    // Parse xapian document's data and populate doc fields
    string data = xdoc.get_data();
    list<string> terms;
    getQueryTerms(terms);
    return m_ndb->dbDataToRclDoc(data, doc, m_qOpts, docid, terms);
}


// Retrieve document defined by file name and internal path. 
bool Db::getDoc(const string &fn, const string &ipath, Doc &doc, int *pc)
{
    LOGDEB(("Db:getDoc: [%s] (%d) [%s]\n", fn.c_str(), fn.length(),
	    ipath.c_str()));
    if (m_ndb == 0)
	return false;

    // Initialize what we can in any case. If this is history, caller
    // will make partial display in case of error
    doc.ipath = ipath;
    doc.url = string("file://") + fn;
    if (*pc)
	*pc = 100;

    string hash;
    pathHash(fn, hash, PATHHASHLEN);
    string pqterm  = ipath.empty() ? "P" + hash : "Q" + hash + "|" + ipath;
    const char *ermsg = "";
    try {
	if (!m_ndb->db.term_exists(pqterm)) {
	    // Document found in history no longer in the database.
	    // We return true (because their might be other ok docs further)
	    // but indicate the error with pc = -1
	    if (*pc) 
		*pc = -1;
	    LOGINFO(("Db:getDoc: no such doc in index: [%s] (len %d)\n",
		     pqterm.c_str(), pqterm.length()));
	    return true;
	}
	Xapian::PostingIterator docid = m_ndb->db.postlist_begin(pqterm);
	Xapian::Document xdoc = m_ndb->db.get_document(*docid);
	string data = xdoc.get_data();
	list<string> terms;
	return m_ndb->dbDataToRclDoc(data, doc, QO_NONE, *docid, terms);
    } catch (const Xapian::Error &e) {
	ermsg = e.get_msg().c_str();
    } catch (const string &s) {
	ermsg = s.c_str();
    } catch (const char *s) {
	ermsg = s;
    } catch (...) {
	ermsg = "Caught unknown exception";
    }
    if (*ermsg) {
	LOGERR(("Db::getDoc: %s\n", ermsg));
    }
    return false;
}

list<string> Db::expand(const Doc &doc)
{
    list<string> res;
    if (!m_ndb || !m_ndb->enquire) {
	LOGERR(("Db::expand: no query opened\n"));
	return res;
    }
    Xapian::RSet rset;
    rset.add_document(Xapian::docid(doc.xdocid));
    // We don't exclude the original query terms.
    Xapian::ESet eset = m_ndb->enquire->get_eset(20, rset, false);
    LOGDEB(("ESet terms:\n"));
    // We filter out the special terms
    for (Xapian::ESetIterator it = eset.begin(); it != eset.end(); it++) {
	LOGDEB((" [%s]\n", (*it).c_str()));
	if ((*it).empty() || ((*it).at(0)>='A' && (*it).at(0)<='Z'))
	    continue;
	res.push_back(*it);
	if (res.size() >= 10)
	    break;
    }
    return res;
}


// We build a possibly full size but sparsely populated (only around
// the search term occurrences) reconstruction of the document. It
// would be possible to compress the array, by having only multiple
// chunks around the terms, but this would seriously complicate the
// data structure.
string Native::makeAbstract(Xapian::docid docid, const list<string>& terms)
{
    LOGDEB(("Native::makeAbstract: maxlen %d wWidth %d\n",
	    m_db->m_synthAbsLen, m_db->m_synthAbsWordCtxLen));

    Chrono chron;

    // The terms array that we populate with the document terms, at
    // their position:
    vector<string> termsVec;

    // For each of the query terms, query xapian for its positions
    // list in the document. For each position entry, remember it in qtermposs
    // and insert it and its neighbours in the set of 'interesting' positions

    // All the query term positions. We remember this mainly because we are
    // going to random-shuffle it for selecting the chunks that we actually 
    // print.
    vector<unsigned int> qtermposs; 
    // The set of all the positions we shall populate with the query terms and
    // their neighbour words.
    set<unsigned int> chunkposs; 
    // Limit the total number of slots we populate.
    const unsigned int maxtotaloccs = 200;
    // Max occurrences per term. We initially know nothing about the
    // occurrences repartition (it would be possible that only one
    // term in the list occurs, or that all do). So this is a rather
    // arbitrary choice.
    const unsigned int maxoccperterm = maxtotaloccs / 10;
    unsigned int totaloccs = 0;

    for (list<string>::const_iterator qit = terms.begin(); qit != terms.end();
	 qit++) {
	Xapian::PositionIterator pos;
	// There may be query terms not in this doc. This raises an
	// exception when requesting the position list, we just catch it.
	try {
	    unsigned int occurrences = 0;
	    for (pos = db.positionlist_begin(docid, *qit); 
		 pos != db.positionlist_end(docid, *qit); pos++) {
		unsigned int ipos = *pos;
		LOGDEB2(("Abstract: [%s] at %d\n", qit->c_str(), ipos));
		// Possibly extend the array. Do it in big chunks
		if (ipos + m_db->m_synthAbsWordCtxLen >= termsVec.size()) {
		    termsVec.resize(ipos + m_db->m_synthAbsWordCtxLen + 1000);
		}
		// Remember the term position
		qtermposs.push_back(ipos);
		// Add adjacent slots to the set to populate at next step
		for (unsigned int ii = MAX(0, ipos-m_db->m_synthAbsWordCtxLen);
		 ii <= MIN(ipos+m_db->m_synthAbsWordCtxLen, termsVec.size()-1);
		     ii++) {
		    chunkposs.insert(ii);
		}
		// Limit the number of occurences we keep for each
		// term. The abstract has a finite length anyway !
		if (occurrences++ > maxoccperterm)
		    break;
	    }
	} catch (...) {
	    // Term does not occur. No problem.
	}
	// Limit total size
	if (totaloccs++ > maxtotaloccs)
	    break;
    }

    LOGDEB(("Abstract:%d:chosen number of positions %d. Populating\n", 
	    chron.millis(), qtermposs.size()));

    // Walk the full document position list and populate slots around
    // the query terms. We arbitrarily truncate the list to avoid
    // taking forever. If we do cutoff, the abstract may be
    // inconsistant, which is bad...
    { 
	Xapian::TermIterator term;
	int cutoff = 500 * 1000;

	for (term = db.termlist_begin(docid);
	     term != db.termlist_end(docid); term++) {
	    if (cutoff-- < 0) {
		LOGDEB(("Abstract: max term count cutoff\n"));
		break;
	    }

	    Xapian::PositionIterator pos;
	    for (pos = db.positionlist_begin(docid, *term); 
		 pos != db.positionlist_end(docid, *term); pos++) {
		if (cutoff-- < 0) {
		    LOGDEB(("Abstract: max term count cutoff\n"));
		    break;
		}
		unsigned int ipos = *pos;
		if (chunkposs.find(ipos) != chunkposs.end()) {
		    // Don't replace a term: the terms list is in
		    // alphabetic order, and we may have several terms
		    // at the same position, we want to keep only the
		    // first one (ie: dockes and dockes@wanadoo.fr)
		    if (termsVec[ipos].empty()) {
			LOGDEB2(("Abstract: populating: [%s] at %d\n", 
				(*term).c_str(), ipos));
			termsVec[ipos] = *term;
		    }
		}
	    }
	}
    }

#if 0
    // Debug only: output the full term[position] vector
    bool epty = false;
    int ipos = 0;
    for (vector<string>::iterator it = termsVec.begin(); it != termsVec.end();
	 it++, ipos++) {
	if (it->empty()) {
	    if (!epty)
		LOGDEB(("Abstract:vec[%d]: [%s]\n", ipos, it->c_str()));
	    epty=true;
	} else {
	    epty = false;
	    LOGDEB(("Abstract:vec[%d]: [%s]\n", ipos, it->c_str()));
	}
    }
#endif

    LOGDEB(("Abstract:%d: randomizing and extracting\n", chron.millis()));

    // We randomize the selection of term positions, from which we
    // shall pull, starting at the beginning, until the abstract is
    // big enough. The abstract is finally built in correct position
    // order, thanks to the position map.
    random_shuffle(qtermposs.begin(), qtermposs.end());
    map<unsigned int, string> mabs;
    unsigned int abslen = 0;

    // Extract data around the first (in random order) query term
    // positions, and store the terms in the map. Don't concatenate
    // immediately into chunks because there might be overlaps
    for (vector<unsigned int>::const_iterator it = qtermposs.begin();
	 it != qtermposs.end(); it++) {

	if (int(abslen) > m_db->m_synthAbsLen)
	    break;

	unsigned int ipos = *it;
	unsigned int beg = MAX(0, ipos-m_db->m_synthAbsWordCtxLen);
	unsigned int fin = MIN(ipos+m_db->m_synthAbsWordCtxLen, 
			       termsVec.size()-1);
	LOGDEB2(("Abstract: %d<-%d->%d\n", beg, ipos, fin));

	for (unsigned int ii = beg; ii <= fin; ii++) {

	    if (int(abslen) > m_db->m_synthAbsLen)
		break;

	    if (!termsVec[ii].empty()) {
		LOGDEB2(("Abstract: position %d -> [%s]\n", 
			ii, termsVec[ii].c_str()));
		mabs[ii] = termsVec[ii];
		abslen += termsVec[ii].length();
	    } else {
		LOGDEB2(("Abstract: empty position at %d\n", ii));
	    }
	}

	// Possibly add a ... at the end of chunk if it's not
	// overlapping and not at the end of doc
	if (fin != termsVec.size()-1) {
	    if (mabs.find(fin+1) == mabs.end())
		mabs[fin+1] = "...";
	}
    }

    // Build the abstract by walking the map (in order of position)
    string abstract;
    for (map<unsigned int, string>::const_iterator it = mabs.begin();
	 it != mabs.end(); it++) {
	LOGDEB2(("Abtract: output [%s]\n", (*it).second.c_str()));
	abstract += (*it).second + " ";
    }
    LOGDEB(("Abtract: done in %d mS\n", chron.millis()));
    return abstract;
}
#ifndef NO_NAMESPACES
}
#endif
