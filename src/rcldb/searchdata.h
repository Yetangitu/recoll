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
#ifndef _SEARCHDATA_H_INCLUDED_
#define _SEARCHDATA_H_INCLUDED_
/* @(#$Id: searchdata.h,v 1.12 2008-01-29 10:11:08 dockes Exp $  (C) 2004 J.F.Dockes */

/** 
 * Structures to hold data coming almost directly from the gui
 * search fields and handle its translation to Xapian queries. 
 * This is not generic code, it reflects the choices made for the user 
 * interface, and it also knows some specific of recoll's usage of Xapian 
 * (ie: term prefixes)
 */
#include <string>
#include <vector>

#include "rcldb.h"

#ifndef NO_NAMESPACES
using std::vector;
using std::string;
namespace Rcl {
#endif // NO_NAMESPACES

/** Search clause types */
enum SClType {
    SCLT_AND, 
    SCLT_OR, SCLT_EXCL, SCLT_FILENAME, SCLT_PHRASE, SCLT_NEAR,
    SCLT_SUB
};

class SearchDataClause;

/** 
  Data structure representing A Recoll query.
  This is currently simply a list of search clauses. 

  For now, clauses in the list just reflect user entry in a query
  field: some text, a clause type (AND/OR/NEAR etc.) and possibly a
  distance. Each clause may hold several queries in the Xapian sense,
  for exemple several terms and phrases as would result from 
  ["this is a phrase" term1 term2]

  This means that SearchData will be translated into a Xapian
  Query tree of depth 2.

  The structure might be extended in the future so that some of the
  clauses may be references to other subqueries (there doesn't seem to
  be an urgent need for this)

 */
class SearchData {
public:
    SearchData(SClType tp) : m_tp(tp), m_haveWildCards(false) {}
    ~SearchData() {erase();}

    /** Make pristine */
    void erase();

    /** Is there anything but a file name search in here ? */
    bool fileNameOnly();

    /** Do we have wildcards anywhere apart from filename searches ? */
    bool haveWildCards() {return m_haveWildCards;}

    /** Translate to Xapian query. rcldb knows about the void*  */
    bool toNativeQuery(Rcl::Db &db, void *, const string& stemlang);

    /** We become the owner of cl and will delete it */
    bool addClause(SearchDataClause *cl);

    /** Retrieve error description */
    string getReason() {return m_reason;}

    /** Get terms and phrase/near groups. Used in the GUI for highlighting 
     * The groups and gslks vectors are parallel and hold the phrases/near
     * string groups and their associated slacks (distance in excess of group
     * size)
     */
    bool getTerms(vector<string>& terms, 
		  vector<vector<string> >& groups, vector<int>& gslks) const;
    /** 
     * Get/set the description field which is retrieved from xapian after
     * initializing the query. It is stored here for usage in the GUI.
     */
    string getDescription() {return m_description;}
    void setDescription(const string& d) {m_description = d;}
    string getTopdir() {return m_topdir;}
    void setTopdir(const string& t) {m_topdir = t;}
    void addFiletype(const string& ft) {m_filetypes.push_back(ft);}
    int  clauseCount() {return m_query.size();}

private:
    SClType                    m_tp; // Only SCLT_AND or SCLT_OR here
    vector<SearchDataClause *> m_query;
    vector<string>             m_filetypes; // Restrict to filetypes if set.
    string                     m_topdir; // Restrict to subtree.
    // Printable expanded version of the complete query, retrieved/set
    // from rcldb after the Xapian::setQuery() call
    string m_description; 
    string m_reason;
    bool   m_haveWildCards;
    /* Copyconst and assignment private and forbidden */
    SearchData(const SearchData &) {}
    SearchData& operator=(const SearchData&) {return *this;};
};

class SearchDataClause {
public:
    SearchDataClause(SClType tp) 
	: m_tp(tp), m_parentSearch(0), m_haveWildCards(0), 
	  m_modifiers(SDCM_NONE)
    {}

    virtual ~SearchDataClause() {}

    virtual bool toNativeQuery(Rcl::Db &db, void *, const string&) = 0;

    bool isFileName() const {return m_tp==SCLT_FILENAME ? true: false;}

    virtual string getReason() const {return m_reason;}

    virtual bool getTerms(vector<string>&, vector<vector<string> >&,
			  vector<int>&) const
    {return true;}

    SClType getTp() {return m_tp;}

    void setParent(SearchData *p) {m_parentSearch = p;}

    friend class SearchData;
    
    enum Modifier {SDCM_NONE=0, SDCM_NOSTEMMING=1};
    virtual void setModifiers(Modifier mod) {m_modifiers = mod;}

protected:
    string      m_reason;
    SClType     m_tp;
    SearchData *m_parentSearch;
    bool        m_haveWildCards;
    Modifier    m_modifiers;
};
    
/**
 * "Simple" data clause with user-entered query text. This can include 
 * multiple phrases and words, but no specified distance.
 */
class SearchDataClauseSimple : public SearchDataClause {
public:
    SearchDataClauseSimple(SClType tp, const string& txt, 
			   const string& fld = "")
	: SearchDataClause(tp), m_text(txt), m_field(fld), m_slack(0) {
	m_haveWildCards = (txt.find_first_of("*?[") != string::npos);
    }

    virtual ~SearchDataClauseSimple() {}

    /** Translate to Xapian query */
    virtual bool toNativeQuery(Rcl::Db &db, void *, const string& stemlang);

    /** Retrieve query terms and term groups. This is used for highlighting */
    virtual bool getTerms(vector<string>& terms, /* Single terms */
			  vector<vector<string> >& groups, /* Prox grps */
			  vector<int>& gslks) const        /* Prox slacks */
    {
	terms.insert(terms.end(), m_terms.begin(), m_terms.end());
	groups.insert(groups.end(), m_groups.begin(), m_groups.end());
	gslks.insert(gslks.end(), m_groups.size(), m_slack);
	return true;
    }

protected:
    string  m_text;  // Raw user entry text.
    string  m_field; // Field specification if any
    // Single terms and phrases resulting from breaking up m_text;
    // valid after toNativeQuery() call
    vector<string>          m_terms;
    vector<vector<string> > m_groups; 
    // Declare m_slack here. Always 0, but allows getTerms to work for
    // SearchDataClauseDist
    int m_slack;
};

/** Filename search clause. */
class SearchDataClauseFilename : public SearchDataClauseSimple {
public:
    SearchDataClauseFilename(const string& txt)
	: SearchDataClauseSimple(SCLT_FILENAME, txt) {
	// File name searches don't count when looking for wild cards.
	m_haveWildCards = false;
    }
    virtual ~SearchDataClauseFilename() {}
    virtual bool toNativeQuery(Rcl::Db &db, void *, const string& stemlang);
};

/** 
 * A clause coming from a NEAR or PHRASE entry field. There is only one 
 * string group, and a specified distance, which applies to it.
 */
class SearchDataClauseDist : public SearchDataClauseSimple {
public:
    SearchDataClauseDist(SClType tp, const string& txt, int slack, 
			 const string& fld = "")
	: SearchDataClauseSimple(tp, txt, fld) {m_slack = slack;}
    virtual ~SearchDataClauseDist() {}

    virtual bool toNativeQuery(Rcl::Db &db, void *, const string& stemlang);

    // m_slack is declared in SearchDataClauseSimple
};

/** Future pointer to subquery ? */
class SearchDataClauseSub : public SearchDataClause {
public:
    // Note that we take charge of the SearchData * and will delete it.
    SearchDataClauseSub(SClType tp, SearchData *sub) 
	: SearchDataClause(tp), m_sub(sub) {}
    virtual ~SearchDataClauseSub() {delete m_sub; m_sub = 0;}
    virtual bool toNativeQuery(Rcl::Db &db, void *, const string& stemlang);

protected:
    SearchData *m_sub;
};

} // Namespace Rcl
#endif /* _SEARCHDATA_H_INCLUDED_ */
