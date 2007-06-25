#ifndef lint
static char rcsid[] = "@(#$Id: rcldb.cpp,v 1.119 2007-06-25 10:25:39 dockes Exp $ (C) 2004 J.F.Dockes";
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
#include <math.h>

#include <iostream>
#include <string>
#include <vector>
#include <algorithm>

#ifndef NO_NAMESPACES
using namespace std;
#endif /* NO_NAMESPACES */

#include "rclconfig.h"
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

// This is the word position offset at which we index the body text
// (abstract, keywords, etc.. are stored before this)
static const unsigned int baseTextPosition = 100000;

#undef MTIME_IN_VALUE
#ifdef MTIME_IN_VALUE
// Omega compatible values
#define enum value_slot {
    VALUE_LASTMOD = 0,	// 4 byte big endian value - seconds since 1970.
    VALUE_MD5 = 1	// 16 byte MD5 checksum of original document.
};
#endif

#ifndef NO_NAMESPACES
namespace Rcl {
#endif
    
// Max length for path terms stored for each document. Truncate
// longer path and uniquize with hash. The goal for this is to avoid
// xapian max term length limitations, not to gain space (we gain very
// little even with very short maxlens like 30)
// Note that Q terms add the ipath to this, and that the xapian max
// key length seems to be around 250
// The value for PATHHASHLEN includes the length of the hash part.
#define PATHHASHLEN 150

// Synthetic abstract marker (to discriminate from abstract actually
// found in doc)
const static string rclSyntAbs = "?!#@";
const static string emptystring;

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

    // Term frequencies for current query. See makeAbstract, setQuery
    map<string, double>  m_termfreqs; 
    
    Native(Db *db) 
	: m_db(db),
	  m_isopen(false), m_iswritable(false), enquire(0) 
    { }

    ~Native() {
	delete enquire;
    }

    string makeAbstract(Xapian::docid id, const list<string>& terms);

    bool dbDataToRclDoc(Xapian::docid docid, std::string &data, Doc &doc);

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

// Turn data record from db into document fields
bool Native::dbDataToRclDoc(Xapian::docid docid, std::string &data, Doc &doc)
{
    LOGDEB1(("Db::dbDataToRclDoc: data: %s\n", data.c_str()));
    ConfSimple parms(&data);
    if (!parms.ok())
	return false;
    parms.get(string("url"), doc.url);
    parms.get(string("mtype"), doc.mimetype);
    parms.get(string("fmtime"), doc.fmtime);
    parms.get(string("dmtime"), doc.dmtime);
    parms.get(string("origcharset"), doc.origcharset);
    parms.get(string("caption"), doc.meta["title"]);
    parms.get(string("keywords"), doc.meta["keywords"]);
    parms.get(string("abstract"), doc.meta["abstract"]);
    // Possibly remove synthetic abstract indicator (if it's there, we
    // used to index the beginning of the text as abstract).
    doc.syntabs = false;
    if (doc.meta["abstract"].find(rclSyntAbs) == 0) {
	doc.meta["abstract"] = doc.meta["abstract"].substr(rclSyntAbs.length());
	doc.syntabs = true;
    }
    parms.get(string("ipath"), doc.ipath);
    parms.get(string("fbytes"), doc.fbytes);
    parms.get(string("dbytes"), doc.dbytes);
    doc.xdocid = docid;
    return true;
}

static list<string> noPrefixList(const list<string>& in) 
{
    list<string> out;
    for (list<string>::const_iterator qit = in.begin(); 
	 qit != in.end(); qit++) {
	if ('A' <= qit->at(0) && qit->at(0) <= 'Z') {
	    string term = *qit;
	    while (term.length() && 'A' <= term.at(0) && term.at(0) <= 'Z')
		term.erase(0, 1);
	    if (term.length())
		out.push_back(term);
	    continue;
	} else {
	    out.push_back(*qit);
	}
    }
    return out;
}

//#define DEBUGABSTRACT 
#ifdef DEBUGABSTRACT
#define LOGABS LOGDEB
#else
#define LOGABS LOGDEB2
#endif

// Build a document abstract by extracting text chunks around the query terms
// This uses the db termlists, not the original document.
string Native::makeAbstract(Xapian::docid docid, const list<string>& iterms)
{
    Chrono chron;
    LOGDEB(("makeAbstract:%d: maxlen %d wWidth %d\n", chron.ms(),
	     m_db->m_synthAbsLen, m_db->m_synthAbsWordCtxLen));

    list<string> terms = noPrefixList(iterms);
    if (terms.empty()) {
	return "";
    }

    // Retrieve db-wide frequencies for the query terms
    if (m_termfreqs.empty()) {
	double doccnt = db.get_doccount();
	if (doccnt == 0) doccnt = 1;
	for (list<string>::const_iterator qit = terms.begin(); 
	     qit != terms.end(); qit++) {
	    m_termfreqs[*qit] = db.get_termfreq(*qit) / doccnt;
	    LOGABS(("makeAbstract: [%s] db freq %.1e\n", qit->c_str(), 
		     m_termfreqs[*qit]));
	}
	LOGABS(("makeAbstract:%d: got termfreqs\n", chron.ms()));
    }

    // Compute a term quality coefficient by retrieving the term
    // Within Document Frequencies and multiplying by overal term
    // frequency, then using log-based thresholds. We are going to try
    // and show text around the less common search terms.
    map<string, double> termQcoefs;
    double totalweight = 0;
    double doclen = db.get_doclength(docid);
    if (doclen == 0) doclen = 1;
    for (list<string>::const_iterator qit = terms.begin(); 
	 qit != terms.end(); qit++) {
	Xapian::TermIterator term = db.termlist_begin(docid);
	term.skip_to(*qit);
	if (term != db.termlist_end(docid) && *term == *qit) {
	    double q = (term.get_wdf() / doclen) * m_termfreqs[*qit];
	    q = -log10(q);
	    if (q < 3) {
		q = 0.05;
	    } else if (q < 4) {
		q = 0.3;
	    } else if (q < 5) {
		q = 0.7;
	    } else if (q < 6) {
		q = 0.8;
	    } else {
		q = 1;
	    }
	    termQcoefs[*qit] = q;
	    totalweight += q;
	}
    }    
    LOGABS(("makeAbstract:%d: computed Qcoefs.\n", chron.ms()));

    // Build a sorted by quality term list.
    multimap<double, string> byQ;
    for (list<string>::const_iterator qit = terms.begin(); 
	 qit != terms.end(); qit++) {
	if (termQcoefs.find(*qit) != termQcoefs.end())
	    byQ.insert(pair<double,string>(termQcoefs[*qit], *qit));
    }

#ifdef DEBUGABSTRACT
    for (multimap<double, string>::reverse_iterator qit = byQ.rbegin(); 
	 qit != byQ.rend(); qit++) {
	LOGDEB(("%.1e->[%s]\n", qit->first, qit->second.c_str()));
    }
#endif


    // For each of the query terms, ask xapian for its positions list
    // in the document. For each position entry, remember it in
    // qtermposs and insert it and its neighbours in the set of
    // 'interesting' positions

    // The terms 'array' that we partially populate with the document
    // terms, at their positions around the search terms positions:
    map<unsigned int, string> sparseDoc;

    // All the chosen query term positions. 
    vector<unsigned int> qtermposs; 

    // Limit the total number of slots we populate. The 7 is taken as
    // average word size. It was a mistake to have the user max
    // abstract size parameter in characters, we basically only deal
    // with words. We used to limit the character size at the end, but
    // this damaged our careful selection of terms
    const unsigned int maxtotaloccs = 
	m_db->m_synthAbsLen /(7 * (m_db->m_synthAbsWordCtxLen+1));
    LOGABS(("makeAbstract:%d: mxttloccs %d\n", chron.ms(), maxtotaloccs));
    // This can't happen, but would crash us
    if (totalweight == 0.0) {
	LOGERR(("makeAbstract: 0 totalweight!\n"));
	return "";
    }

    // Let's go populate
    for (multimap<double, string>::reverse_iterator qit = byQ.rbegin(); 
	 qit != byQ.rend(); qit++) {
	string qterm = qit->second;
	unsigned int maxoccs;
	if (byQ.size() == 1) {
	    maxoccs = maxtotaloccs;
	} else {
	    // We give more slots to the better terms
	    float q = qit->first / totalweight;
	    maxoccs = int(ceil(maxtotaloccs * q));
	    LOGABS(("makeAbstract: [%s] %d max occs (coef %.2f)\n", 
		    qterm.c_str(), maxoccs, q));
	}
		
	Xapian::PositionIterator pos;
	// There may be query terms not in this doc. This raises an
	// exception when requesting the position list, we catch it.
	string emptys;
	try {
	    unsigned int occurrences = 0;
	    for (pos = db.positionlist_begin(docid, qterm); 
		 pos != db.positionlist_end(docid, qterm); pos++) {
		unsigned int ipos = *pos;
		if (ipos < baseTextPosition) // Not in text body
		    continue;
		LOGABS(("makeAbstract: [%s] at %d occurrences %d maxoccs %d\n",
			qterm.c_str(), ipos, occurrences, maxoccs));
		// Remember the term position
		qtermposs.push_back(ipos);
		// Add adjacent slots to the set to populate at next step
		unsigned int sta = MAX(0, ipos-m_db->m_synthAbsWordCtxLen);
		unsigned int sto = ipos+m_db->m_synthAbsWordCtxLen;
		for (unsigned int ii = sta; ii <= sto;  ii++) {
		    if (ii == ipos)
			sparseDoc[ii] = qterm;
		    else
			sparseDoc[ii] = emptys;
		}
		// Limit to allocated occurences and total size
		if (++occurrences >= maxoccs || 
		    qtermposs.size() >= maxtotaloccs)
		    break;
	    }
	} catch (...) {
	    // Term does not occur. No problem.
	}
	if (qtermposs.size() >= maxtotaloccs)
	    break;
    }
    LOGABS(("makeAbstract:%d:chosen number of positions %d\n", 
	    chron.millis(), qtermposs.size()));

    // This can happen if there are term occurences in the keywords
    // etc. but not elsewhere ?
    if (qtermposs.size() == 0) 
	return "";

    // Walk all document's terms position lists and populate slots
    // around the query terms. We arbitrarily truncate the list to
    // avoid taking forever. If we do cutoff, the abstract may be
    // inconsistant (missing words, potentially altering meaning),
    // which is bad...
    { 
	Xapian::TermIterator term;
	int cutoff = 500 * 1000;

	for (term = db.termlist_begin(docid);
	     term != db.termlist_end(docid); term++) {
	    // Ignore prefixed terms
	    if ('A' <= (*term).at(0) && (*term).at(0) <= 'Z')
		continue;
	    if (cutoff-- < 0) {
		LOGDEB(("makeAbstract: max term count cutoff\n"));
		break;
	    }

	    Xapian::PositionIterator pos;
	    for (pos = db.positionlist_begin(docid, *term); 
		 pos != db.positionlist_end(docid, *term); pos++) {
		if (cutoff-- < 0) {
		    LOGDEB(("makeAbstract: max term count cutoff\n"));
		    break;
		}
		map<unsigned int, string>::iterator vit;
		if ((vit=sparseDoc.find(*pos)) != sparseDoc.end()) {
		    // Don't replace a term: the terms list is in
		    // alphabetic order, and we may have several terms
		    // at the same position, we want to keep only the
		    // first one (ie: dockes and dockes@wanadoo.fr)
		    if (vit->second.empty()) {
			LOGABS(("makeAbstract: populating: [%s] at %d\n", 
				(*term).c_str(), *pos));
			sparseDoc[*pos] = *term;
		    }
		}
	    }
	}
    }

#if 0
    // Debug only: output the full term[position] vector
    bool epty = false;
    int ipos = 0;
    for (map<unsigned int, string>::iterator it = sparseDoc.begin(); 
	 it != sparseDoc.end();
	 it++, ipos++) {
	if (it->empty()) {
	    if (!epty)
		LOGDEB(("makeAbstract:vec[%d]: [%s]\n", ipos, it->c_str()));
	    epty=true;
	} else {
	    epty = false;
	    LOGDEB(("makeAbstract:vec[%d]: [%s]\n", ipos, it->c_str()));
	}
    }
#endif

    LOGDEB(("makeAbstract:%d: extracting\n", chron.millis()));

    // Add "..." at ends of chunks
    for (vector<unsigned int>::const_iterator pos = qtermposs.begin();
	 pos != qtermposs.end(); pos++) {
	unsigned int sto = *pos + m_db->m_synthAbsWordCtxLen;

	// Possibly add a ... at the end of chunk if it's not
	// overlapping
	if (sparseDoc.find(sto) != sparseDoc.end() && 
	    sparseDoc.find(sto+1) == sparseDoc.end())
	    sparseDoc[sto+1] = "...";
    }

    // Finally build the abstract by walking the map (in order of position)
    string abstract;
    for (map<unsigned int, string>::const_iterator it = sparseDoc.begin();
	 it != sparseDoc.end(); it++) {
	LOGDEB2(("Abtract:output %u -> [%s]\n", it->first,it->second.c_str()));
	abstract += it->second + " ";
    }

    // This happens for docs with no terms (only filename) indexed? I'll fix 
    // one day (yeah)
    if (!abstract.compare("... "))
	abstract.clear();

    LOGDEB(("makeAbtract: done in %d mS\n", chron.millis()));
    return abstract;
}

/* Rcl::Db methods ///////////////////////////////// */

Db::Db() 
    : m_ndb(0), m_qOpts(QO_NONE), m_idxAbsTruncLen(250), m_synthAbsLen(250),
      m_synthAbsWordCtxLen(4), m_flushMb(-1), 
      m_curtxtsz(0), m_flushtxtsz(0), m_occtxtsz(0),
      m_maxFsOccupPc(0), m_mode(Db::DbRO)
{
    m_ndb = new Native(this);
    RclConfig *config = RclConfig::getMainConfig();
    if (config) {
	config->getConfParam("maxfsoccuppc", &m_maxFsOccupPc);
	config->getConfParam("idxflushmb", &m_flushMb);
    }
}

Db::~Db()
{
    LOGDEB1(("Db::~Db\n"));
    if (m_ndb == 0)
	return;
    LOGDEB(("Db::~Db: isopen %d m_iswritable %d\n", m_ndb->m_isopen, 
	    m_ndb->m_iswritable));
    i_close(true);
}

bool Db::open(const string& dir, const string &stops, OpenMode mode, int qops)
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
    if (!stops.empty())
	m_stops.setFile(stops);

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
    return i_close(false);
}

bool Db::i_close(bool final)
{
    if (m_ndb == 0)
	return false;
    LOGDEB(("Db::i_close(%d): m_isopen %d m_iswritable %d\n", final,
	    m_ndb->m_isopen, m_ndb->m_iswritable));
    if (m_ndb->m_isopen == false && !final) 
	return true;

    const char *ermsg = "Unknown";
    try {
	bool w = m_ndb->m_iswritable;
	if (w)
	    LOGDEB(("Rcl::Db:close: xapian will close. May take some time\n"));
	// Used to do a flush here. Cant see why it should be necessary.
	delete m_ndb;
	m_ndb = 0;
	if (w)
	    LOGDEB(("Rcl::Db:close() xapian close done.\n"));
	if (final) {
	    return true;
	}
	m_ndb = new Native(this);
	if (m_ndb) {
	    return true;
	}
	LOGERR(("Rcl::Db::close(): cant recreate db object\n"));
	return false;
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
	if (!open(m_basedir, "", m_mode, m_qOpts | QO_KEEP_UPDATED)) {
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

// Try to translate field specification into field prefix.  We have a
// default table used if translations are not in the config for some
// reason (old config not updated ?). We use it only if the config
// translation fails. Also we add in there fields which should be
// indexed with no prefix (ie: abstract)
bool Db::fieldToPrefix(const string& fldname, string &pfx)
{
    // This is the default table
    static map<string, string> fldToPrefs;
    if (fldToPrefs.empty()) {
	fldToPrefs["abstract"] = "";
	fldToPrefs["ext"] = "XE";

	fldToPrefs["title"] = "S";
	fldToPrefs["caption"] = "S";
	fldToPrefs["subject"] = "S";

	fldToPrefs["author"] = "A";
	fldToPrefs["creator"] = "A";
	fldToPrefs["from"] = "A";

	fldToPrefs["keyword"] = "K";
	fldToPrefs["tag"] = "K";
	fldToPrefs["keywords"] = "K";
	fldToPrefs["tags"] = "K";
    }

    string fld(fldname);
    stringtolower(fld);

    RclConfig *config = RclConfig::getMainConfig();
    if (config && config->getFieldPrefix(fld, pfx))
	return true;

    map<string, string>::const_iterator it = fldToPrefs.find(fld);
    if (it != fldToPrefs.end()) {
	pfx = it->second;
	return true;
    }
    return false;
}


// The text splitter callback class which receives words from the
// splitter and adds postings to the Xapian document.
class mySplitterCB : public TextSplitCB {
 public:
    Xapian::Document &doc;   // Xapian document 
    Xapian::termpos basepos; // Base for document section
    Xapian::termpos curpos;  // Current position. Used to set basepos for the
                             // following section
    StopList &stops;
    mySplitterCB(Xapian::Document &d, StopList &_stops) 
	: doc(d), basepos(1), curpos(0), stops(_stops)
    {}
    bool takeword(const std::string &term, int pos, int, int);
    void setprefix(const string& pref) {prefix = pref;}

private:
    // If prefix is set, we also add a posting for the prefixed terms
    // (ie: for titles, add postings for both "term" and "Sterm")
    string  prefix; 
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
	if (stops.hasStops() && stops.isStop(term)) {
	    LOGDEB1(("Db: takeword [%s] in stop list\n", term.c_str()));
	    return true;
	}
	// Note: 1 is the within document frequency increment. It would 
	// be possible to assign different weigths to doc parts (ie title)
	// by using a higher value
	curpos = pos;
	pos += basepos;
	doc.add_posting(term, pos, 1);
	if (!prefix.empty()) {
	    doc.add_posting(prefix + term, pos, 1);
	}
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

static const int MB = 1024 * 1024;

// Add document in internal form to the database: index the terms in
// the title abstract and body and add special terms for file name,
// date, mime type ... , create the document data record (more
// metadata), and update database
bool Db::add(const string &fn, const Doc &idoc, const struct stat *stp)
{
    LOGDEB1(("Db::add: fn %s\n", fn.c_str()));
    if (m_ndb == 0)
	return false;
    static int first = 1;
    // Check file system full every mbyte of indexed text.
    if (m_maxFsOccupPc > 0 && (first || (m_curtxtsz - m_occtxtsz) / MB >= 1)) {
	LOGDEB(("Db::add: checking file system usage\n"));
	int pc;
	first = 0;
	if (fsocc(m_basedir, &pc) && pc >= m_maxFsOccupPc) {
	    LOGERR(("Db::add: stop indexing: file system "
		     "%d%% full > max %d%%\n", pc, m_maxFsOccupPc));
	    return false;
	}
	m_occtxtsz = m_curtxtsz;
    }

    Doc doc = idoc;

    // The title, author, abstract and keywords fields are special, they
    // get stored in the document data record.
    // Truncate abstract, title and keywords to reasonable lengths. If
    // abstract is currently empty, we make up one with the beginning
    // of the document. This is then not indexed, but part of the doc
    // data so that we can return it to a query without having to
    // decode the original file.
    bool syntabs = false;
    // Note that the map accesses by operator[] create empty entries if they
    // don't exist yet.
    if (doc.meta["abstract"].empty()) {
	syntabs = true;
	doc.meta["abstract"] = rclSyntAbs + 
	    neutchars(truncate_to_word(doc.text, m_idxAbsTruncLen), "\n\r");
    } else {
	doc.meta["abstract"] = 
	    neutchars(truncate_to_word(doc.meta["abstract"], m_idxAbsTruncLen),
		      "\n\r");
    }
    if (doc.meta["title"].empty())
	doc.meta["title"] = doc.utf8fn, "\n\r";
    doc.meta["title"] = 
	neutchars(truncate_to_word(doc.meta["title"], 150), "\n\r");
    doc.meta["author"] = 
	neutchars(truncate_to_word(doc.meta["author"], 150), "\n\r");
    doc.meta["keywords"] = 
	neutchars(truncate_to_word(doc.meta["keywords"], 300),"\n\r");


    Xapian::Document newdocument;
    mySplitterCB splitData(newdocument, m_stops);
    TextSplit splitter(&splitData);
    string noacc;

    // Split and index file name as document term(s)
    LOGDEB2(("Db::add: split file name [%s]\n", fn.c_str()));
    if (dumb_string(doc.utf8fn, noacc)) {
	splitter.text_to_words(noacc);
	splitData.basepos += splitData.curpos + 100;
    }

    // Index textual metadata.  These are all indexed as text with
    // positions, as we may want to do phrase searches with them (this
    // makes no sense for keywords by the way).
    //
    // The order has no importance, and we set a position gap of 100
    // between fields to avoid false proximity matches.
    map<string,string>::iterator meta_it;
    string pfx;
    for (meta_it = doc.meta.begin(); meta_it != doc.meta.end(); meta_it++) {
	if (!meta_it->second.empty()) {
	    if (meta_it->first == "abstract" && syntabs)
		continue;
	    if (!fieldToPrefix(meta_it->first, pfx)) {
		LOGDEB(("Db::add: no prefix for field [%s], no indexing\n",
			meta_it->first.c_str()));
		continue;
	    }
	    LOGDEB(("Db::add: field [%s] pfx [%s]: [%s]\n", 
		    meta_it->first.c_str(), pfx.c_str(), 
		    meta_it->second.c_str()));
	    if (!dumb_string(meta_it->second, noacc)) {
		LOGERR(("Db::add: dumb_string failed\n"));
		return false;
	    }
	    splitData.setprefix(pfx); // Subject
	    splitter.text_to_words(noacc);
	    splitData.setprefix(emptystring);
	    splitData.basepos += splitData.curpos + 100;
	}
    }

    if (splitData.curpos < baseTextPosition)
	splitData.basepos = baseTextPosition;
    else
	splitData.basepos += splitData.curpos + 100;

    // Finally: split and index body text
    LOGDEB2(("Db::add: split body\n"));
    if (!dumb_string(doc.text, noacc)) {
	LOGERR(("Db::add: dumb_string failed\n"));
	return false;
    }
    splitter.text_to_words(noacc);

    ////// Special terms for other metadata. No positions for these.
    // Mime type
    newdocument.add_term("T" + doc.mimetype);

    // Simple file name. This is used for file name searches only. We index
    // it with a term prefix. utf8fn used to be the full path, but it's now
    // the simple file name.
    // We also add a term for the filename extension if any.
    if (dumb_string(doc.utf8fn, noacc) && !noacc.empty()) {
	string::size_type pos = noacc.rfind('.');
	if (pos != string::npos && pos != noacc.length() -1) {
	    newdocument.add_term(string("XE") + noacc.substr(pos+1));
	}
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
#error need to fix fmtime to be stored as omega does it (bin net order str)
	newdocument.add_value(VALUE_LASTMOD, doc.fmtime);
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
    if (!doc.meta["title"].empty())
	record += "\ncaption=" + doc.meta["title"];
    if (!doc.meta["keywords"].empty())
	record += "\nkeywords=" + doc.meta["keywords"];
    if (!doc.meta["abstract"].empty())
	record += "\nabstract=" + doc.meta["abstract"];
    if (!doc.meta["author"].empty()) {
	record += "\nauthor=" + doc.meta["author"];
    }
    record += "\n";
    LOGDEB1(("Newdocument data: %s\n", record.c_str()));
    newdocument.set_data(record);

    const char *fnc = fn.c_str();
    string ermsg;

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
    } catch (const Xapian::Error &e) {
	ermsg = e.get_msg();
	if (ermsg.empty()) 
	    ermsg = "Empty error message";
    } catch (...) {
	ermsg= "Unknown error";
    }

    if (!ermsg.empty()) {
	LOGERR(("Db::add: replace_document failed: %s\n", ermsg.c_str()));
	ermsg.erase();
	// FIXME: is this ever actually needed?
	try {
	    m_ndb->wdb.add_document(newdocument);
	    LOGDEB(("Db::add: %s added (failed re-seek for duplicate)\n", 
		    fnc));
	} catch (const Xapian::Error &e) {
	    ermsg = e.get_msg();
	    if (ermsg.empty()) 
		ermsg = "Empty error message";
	} catch (...) {
	    ermsg= "Unknown error";
	}
	if (!ermsg.empty()) {
	    LOGERR(("Db::add: add_document failed: %s\n", ermsg.c_str()));
	    return false;
	}
    }

    // Test if we're over the flush threshold (limit memory usage):
    m_curtxtsz += doc.text.length();
    if (m_flushMb > 0) {
	if ((m_curtxtsz - m_flushtxtsz) / MB >= m_flushMb) {
	    ermsg.erase();
	    LOGDEB(("Db::add: text size >= %d Mb, flushing\n", m_flushMb));
	    try {
		m_ndb->wdb.flush();
	    } catch (const Xapian::Error &e) {
		ermsg = e.get_msg();
		if (ermsg.empty()) 
		    ermsg = "Empty error message";
	    } catch (...) {
		ermsg= "Unknown error";
	    }
	    if (!ermsg.empty()) {
		LOGERR(("Db::add: flush() failed: %s\n", ermsg.c_str()));
		return false;
	    }
	    m_flushtxtsz = m_curtxtsz;
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
	    string value = doc.get_value(VALUE_LASTMOD);
#error fixme make storage format compatible with omega
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

    // For xapian versions up to 1.0.1, deleting a non-existant
    // document would trigger an exception that would discard any
    // pending update. This could lose both previous added documents
    // or deletions. Adding the flush before the delete pass ensured
    // that any added document would go to the index. Kept here
    // because it doesn't really hurt.
    try {
	m_ndb->wdb.flush();
    } catch (...) {
	LOGERR(("Db::purge: 1st flush failed\n"));

    }

    // Walk the document array and delete any xapian document whose
    // flag is not set (we did not see its source during indexing).
    for (Xapian::docid docid = 1; docid < updated.size(); ++docid) {
	if (!updated[docid]) {
	    try {
		m_ndb->wdb.delete_document(docid);
		LOGDEB(("Db::purge: deleted document #%d\n", docid));
	    } catch (const Xapian::DocNotFoundError &) {
		LOGDEB(("Db::purge: document #%d not found\n", docid));
	    } catch (const Xapian::Error &e) {
		LOGERR(("Db::purge: document #%d: %s\n", e.get_msg().c_str()));
	    } catch (...) {
		LOGERR(("Db::purge: document #%d: unknown error\n"));
	    }
	}
    }

    try {
	m_ndb->wdb.flush();
    } catch (...) {
	LOGERR(("Db::purge: 2nd flush failed\n"));
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

bool Db::filenameWildExp(const string& fnexp, list<string>& names)
{
    // File name search, with possible wildcards. 
    // We expand wildcards by scanning the filename terms (prefixed 
    // with XSFN) from the database. 
    // We build an OR query with the expanded values if any.
    string pattern;
    dumb_string(fnexp, pattern);

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
	    LOGERR(("Db::filenameWildExp: too many matched file names\n"));
	    break;
	}
    }
    if (names.empty()) {
	// Build an impossible query: we know its impossible because we
	// control the prefixes!
	names.push_back("XIMPOSSIBLE");
    }
    return true;
}

// Prepare query out of user search data
bool Db::setQuery(RefCntr<SearchData> sdata, int opts, 
		  const string& stemlang)
{
    if (!m_ndb) {
	LOGERR(("Db::setQuery: no db!\n"));
	return false;
    }
    m_reason.erase();
    LOGDEB(("Db::setQuery:\n"));

    m_filterTopDir = sdata->getTopdir();
    m_dbindices.clear();
    m_qOpts = opts;
    m_ndb->m_termfreqs.clear();

    Xapian::Query xq;
    if (!sdata->toNativeQuery(*this, &xq, 
			      (opts & Db::QO_STEM) ? stemlang : "")) {
	m_reason += sdata->getReason();
	return false;
    }
    m_ndb->query = xq;
    delete m_ndb->enquire;
    m_ndb->enquire = new Xapian::Enquire(m_ndb->db);
    m_ndb->enquire->set_query(m_ndb->query);
    m_ndb->mset = Xapian::MSet();
    // Get the query description and trim the "Xapian::Query"
    string d = m_ndb->query.get_description();
    if (d.find("Xapian::Query") == 0)
	d.erase(0, strlen("Xapian::Query"));
    sdata->setDescription(d);
    LOGDEB(("Db::SetQuery: Q: %s\n", sdata->getDescription().c_str()));
    return true;
}

class TermMatchCmpByWcf {
public:
    int operator()(const TermMatchEntry& l, const TermMatchEntry& r) {
	return r.wcf - l.wcf < 0;
    }
};
class TermMatchCmpByTerm {
public:
    int operator()(const TermMatchEntry& l, const TermMatchEntry& r) {
	return l.term.compare(r.term) > 0;
    }
};
class TermMatchTermEqual {
public:
    int operator()(const TermMatchEntry& l, const TermMatchEntry& r) {
	return !l.term.compare(r.term);
    }
};

bool Db::stemExpand(const string &lang, const string &term, 
		    list<TermMatchEntry>& result, int max)
{
    list<string> dirs = m_extraDbs;
    dirs.push_front(m_basedir);
    for (list<string>::iterator it = dirs.begin();
	 it != dirs.end(); it++) {
	list<string> more;
	StemDb::stemExpand(*it, lang, term, more);
	LOGDEB1(("Db::stemExpand: Got %d from %s\n", 
		 more.size(), it->c_str()));
	result.insert(result.end(), more.begin(), more.end());
    }
    LOGDEB1(("Db:::stemExpand: final count %d \n", result.size()));
    return true;
}

// Characters that can begin a wildcard or regexp expression. We use skipto
// to begin the allterms search with terms that begin with the portion of
// the input string prior to these chars.
const string wildSpecChars = "*?[";
const string regSpecChars = "(.[{";

// Find all index terms that match a wildcard or regular expression
bool Db::termMatch(MatchType typ, const string &lang,
		   const string &root, 
		   list<TermMatchEntry>& res,
		   int max)
{
    if (!m_ndb || !m_ndb->m_isopen)
	return false;

    Xapian::Database db = m_ndb->m_iswritable ? m_ndb->wdb: m_ndb->db;

    res.clear();

    // Get rid of capitals and accents
    string droot;
    dumb_string(root, droot);
    string nochars = typ == ET_WILD ? wildSpecChars : regSpecChars;

    if (typ == ET_STEM) {
	if (!stemExpand(lang, root, res, max))
	    return false;
	res.sort();
	res.unique();
	for (list<TermMatchEntry>::iterator it = res.begin(); 
	     it != res.end(); it++) {
	    it->wcf = db.get_collection_freq(it->term);
	    LOGDEB1(("termMatch: %d [%s]\n", it->wcf, it->term.c_str()));
	}
    } else {
	regex_t reg;
	int errcode;
	if (typ == ET_REGEXP) {
	    string mroot = droot;
	    if ((errcode = regcomp(&reg, mroot.c_str(), 
				   REG_EXTENDED|REG_NOSUB))) {
		char errbuf[200];
		regerror(errcode, &reg, errbuf, 199);
		LOGERR(("termMatch: regcomp failed: %s\n", errbuf));
		res.push_back(string(errbuf));
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
	    res.push_back(TermMatchEntry(*it, it.get_termfreq()));
	    ++n;
	}
	if (typ == ET_REGEXP) {
	    regfree(&reg);
	}

    }

    TermMatchCmpByTerm tcmp;
    res.sort(tcmp);
    TermMatchTermEqual teq;
    res.unique(teq);
    TermMatchCmpByWcf wcmp;
    res.sort(wcmp);
    if (max > 0) {
	res.resize(MIN(res.size(), (unsigned int)max));
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


bool Db::stemDiffers(const string& lang, const string& word, 
		     const string& base)
{
    Xapian::Stem stemmer(lang);
    if (!stemmer(word).compare(stemmer(base))) {
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
    return m_ndb->dbDataToRclDoc(docid, data, doc);
}

bool Db::makeDocAbstract(Doc &doc, string& abstract)
{
    LOGDEB1(("Db::makeDocAbstract: exti %d\n", exti));
    if (!m_ndb || !m_ndb->enquire) {
	LOGERR(("Db::makeDocAbstract: no query opened\n"));
	return false;
    }
    list<string> terms;
    getQueryTerms(terms);
    abstract = m_ndb->makeAbstract(doc.xdocid, terms);
    return true;
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
	return m_ndb->dbDataToRclDoc(*docid, data, doc);
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


#ifndef NO_NAMESPACES
}
#endif
