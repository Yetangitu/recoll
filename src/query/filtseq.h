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
#ifndef _FILTSEQ_H_INCLUDED_
#define _FILTSEQ_H_INCLUDED_
/* @(#$Id: filtseq.h,v 1.3 2008-09-29 08:59:20 dockes Exp $  (C) 2004 J.F.Dockes */

#include <vector>
#include <string>

#include "refcntr.h"
#include "docseq.h"

class DocSeqFiltSpec {
 public:
    DocSeqFiltSpec() {}
    enum Crit {DSFS_MIMETYPE};
    void orCrit(Crit crit, const string& value) {
	crits.push_back(crit);
	values.push_back(value);
    }
    std::vector<Crit> crits;
    std::vector<string> values;
    void reset() {crits.clear(); values.clear();}
    bool isNotNull() {return crits.size() != 0;}
};

/** 
 * A filtered sequence is created from another one by selecting entries
 * according to the given criteria.
 */
class DocSeqFiltered : public DocSequence {
 public:
    DocSeqFiltered(RefCntr<DocSequence> iseq, DocSeqFiltSpec &filtspec, 
		 const std::string &t);
    virtual ~DocSeqFiltered() {}
    virtual bool getDoc(int num, Rcl::Doc &doc, string *sh = 0);
    virtual int getResCnt() {return m_seq->getResCnt();}
    virtual string getAbstract(Rcl::Doc& doc) {
	return m_seq->getAbstract(doc);
    }
    virtual string getDescription() {return m_seq->getDescription();}

 private:
    RefCntr<DocSequence>    m_seq;
    DocSeqFiltSpec          m_spec;
    vector<int>             m_dbindices;
};

#endif /* _FILTSEQ_H_INCLUDED_ */
