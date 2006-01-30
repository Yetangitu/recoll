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
#ifndef _DB_H_INCLUDED_
#define _DB_H_INCLUDED_
/* @(#$Id: rcldb.h,v 1.24 2006-01-30 11:15:27 dockes Exp $  (C) 2004 J.F.Dockes */

#include <string>
#include <list>
#include <vector>
#ifndef NO_NAMESPACES
using std::string;
using std::list;
using std::vector;
#endif

// rcldb defines an interface for a 'real' text database. The current 
// implementation uses xapian only, and xapian-related code is in rcldb.cpp
// If support was added for other backend, the xapian code would be moved in 
// rclxapian.cpp, another file would be created for the new backend, and the
// configuration/compile/link code would be adjusted to allow choosing. There 
// is no plan for supporting multiple different backends.
// 
// In no case does this try to implement a useful virtualized text-db interface
// The main goal is simplicity and good matching to usage inside the recoll
// user interface. In other words, this is not exhaustive or well-designed or 
// reusable.


struct stat;

#ifndef NO_NAMESPACES
namespace Rcl {
#endif

/**
 * Dumb holder for document attributes and data
 */
class Doc {
 public:
    // These fields potentially go into the document data record
    string url;
    string ipath;
    string mimetype;
    string fmtime;       // File modification time as decimal ascii unix time
    string dmtime;       // Data reference date (same format). Ie: mail date
    string origcharset;
    string title;
    string keywords;
    string abstract;
    string fbytes;        // File size
    string dbytes;        // Doc size

    // The following fields don't go to the db. text is only used when
    // indexing
    string text;

    int pc; // used by sortseq, convenience

    void erase() {
	url.erase();
	ipath.erase();
	mimetype.erase();
	fmtime.erase();
	dmtime.erase();
	origcharset.erase();
	title.erase();
	keywords.erase();
	abstract.erase();
	fbytes.erase();
	dbytes.erase();

	text.erase();
    }
};

/**
 * Holder for the advanced query data 
 */
class AdvSearchData {
    public:
    string allwords;
    string phrase;
    string orwords;
    string nowords;
    list<string> filetypes; // restrict to types. Empty if inactive
    string topdir; // restrict to subtree. Empty if inactive
    string description; // Printable expanded version of the complete query
                        // returned after setQuery.
    void erase() {
	allwords.erase();
	phrase.erase();
	orwords.erase();
	nowords.erase();
	filetypes.clear(); 
	topdir.erase();
	description.erase();
    }
};
 
class DbPops;

/**
 * Wrapper class for the native database.
 */
class Db {
 public:
    Db();
    ~Db();

    enum OpenMode {DbRO, DbUpd, DbTrunc};
    enum QueryOpts {QO_NONE=0, QO_STEM = 1, QO_BUILD_ABSTRACT = 2,
		    QO_REPLACE_ABSTRACT = 4};

    bool open(const string &dbdir, OpenMode mode, int qops = 0);
    bool close();
    bool isopen();

    // Update-related functions
    bool add(const string &filename, const Doc &doc, const struct stat *stp);
    bool needUpdate(const string &filename, const struct stat *stp);
    bool purge();
    bool createStemDb(const string &lang);
    bool deleteStemDb(const string &lang);

    // Query-related functions

    // Parse query string and initialize query
    bool setQuery(const string &q, QueryOpts opts = QO_NONE, 
		  const string& stemlang = "english");
    bool setQuery(AdvSearchData &q, QueryOpts opts = QO_NONE,
		  const string& stemlang = "english");
    bool getQueryTerms(list<string>& terms);

    /** Get document at rank i in current query. 

	This is probably vastly inferior to the type of interface in
	Xapian, but we have to start with something simple to
	experiment with the GUI. i is sequential from 0 to some value.
    */
    bool getDoc(int i, Doc &doc, int *percent = 0);

    /** Get document for given filename and ipath */
    bool getDoc(const string &fn, const string &ipath, Doc &doc);

    /** Get results count for current query */
    int getResCnt();

    /** Get a list of existing stemming databases */
    std::list<std::string> getStemLangs();

    /** Things we don't want to have here. */
    friend class Rcl::DbPops;

private:

    AdvSearchData asdata;
    vector<int> dbindices; // In case there is a postq filter: sequence of 
                           // db indices that match
    void *pdata; // Pointer to private data. We don't want db(ie
                 // xapian)-specific defs to show in here
    unsigned int m_qOpts;

    /* Copyconst and assignemt private and forbidden */
    Db(const Db &) {}
    Db & operator=(const Db &) {return *this;};
};

// Unaccent and lowercase data.
extern bool dumb_string(const string &in, string &out);

#ifndef NO_NAMESPACES
}
#endif // NO_NAMESPACES


#endif /* _DB_H_INCLUDED_ */
