/* Copyright (C) 2008 J.F.Dockes
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
#ifndef _rclquery_h_included_
#define _rclquery_h_included_
#include <string>
#include <vector>

#include "refcntr.h"
#include "searchdata.h"

#ifndef NO_NAMESPACES
namespace Rcl {
#endif

class Db;
class Doc;

enum abstract_result {
    ABSRES_ERROR = 0,
    ABSRES_OK = 1,
    ABSRES_TRUNC = 2
};

// Snippet entry for makeDocAbstract
class Snippet {
public:
    Snippet(int page, const std::string& snip) 
	: page(page), snippet(snip)
    {
    }
    Snippet& setTerm(const std::string& trm)
    {
	term = trm;
	return *this;
    }
    int page;
    std::string term;
    std::string snippet;
};

	
/**
 * An Rcl::Query is a question (SearchData) applied to a
 * database. Handles access to the results. Somewhat equivalent to a
 * cursor in an rdb.
 *
 */
class Query {
 public:
    /** The constructor only allocates memory */
    Query(Db *db);
    ~Query();

    /** Get explanation about last error */
    std::string getReason() const;

    /** Choose sort order. Must be called before setQuery */
    void setSortBy(const std::string& fld, bool ascending = true);
    const std::string& getSortBy() const {return m_sortField;}
    bool getSortAscending() const {return m_sortAscending;}

    /** Return or filter results with identical content checksum */
    void setCollapseDuplicates(bool on) {m_collapseDuplicates = on;}

    /** Accept data describing the search and query the index. This can
     * be called repeatedly on the same object which gets reinitialized each
     * time.
     */
    bool setQuery(RefCntr<SearchData> q);

    /** Get results count for current query */
    int getResCnt();

    /** Get document at rank i in current query results. */
    bool getDoc(int i, Doc &doc);

    /** Get possibly expanded list of query terms */
    bool getQueryTerms(std::vector<std::string>& terms);

    /** Return a list of terms which matched for a specific result document */
    bool getMatchTerms(const Doc& doc, std::vector<std::string>& terms);
    bool getMatchTerms(unsigned long xdocid, std::vector<std::string>& terms);

    /** Build synthetic abstract for document, extracting chunks relevant for
     * the input query. This uses index data only (no access to the file) */
    // Abstract return as one string
    bool makeDocAbstract(Doc &doc, std::string& abstract);
    // Returned as a snippets vector
    bool makeDocAbstract(Doc &doc, std::vector<std::string>& abstract);
    // Returned as a vector of pair<page,snippet> page is 0 if unknown
    abstract_result makeDocAbstract(Doc &doc, std::vector<Snippet>& abst, 
				    int maxoccs= -1, int ctxwords = -1);
    /** Retrieve detected page breaks positions */
    int getFirstMatchPage(Doc &doc, std::string& term);

    /** Expand query to look for documents like the one passed in */
    std::vector<std::string> expand(const Doc &doc);

    /** Return the Db we're set for */
    Db *whatDb();

    /* make this public for access from embedded Db::Native */
    class Native;
    Native *m_nq;

private:
    std::string m_reason; // Error explanation
    Db    *m_db;
    void  *m_sorter;
    std::string m_sortField;
    bool   m_sortAscending;
    bool   m_collapseDuplicates;     
    int    m_resCnt;
    RefCntr<SearchData> m_sd;

    /* Copyconst and assignement private and forbidden */
    Query(const Query &) {}
    Query & operator=(const Query &) {return *this;};
};

#ifndef NO_NAMESPACES
}
#endif // NO_NAMESPACES


#endif /* _rclquery_h_included_ */
