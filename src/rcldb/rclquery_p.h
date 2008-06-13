#ifndef _rclquery_p_h_included_
#define _rclquery_p_h_included_
/* @(#$Id: rclquery_p.h,v 1.1 2008-06-13 18:22:46 dockes Exp $  (C) 2007 J.F.Dockes */

#include <map>
#include <vector>

using std::map;
using std::vector;

#include <xapian.h>
#include "rclquery.h"

namespace Rcl {

class Query::Native {
public:
    Xapian::Query    query; // query descriptor: terms and subqueries
			    // joined by operators (or/and etc...)

    vector<int> m_dbindices; // In case there is a postq filter: sequence of 
                             // db indices that match

    // Filtering results on location. There are 2 possible approaches
    // for this:
    //   - Set a "MatchDecider" to be used by Xapian during the query
    //   - Filter the results out of Xapian (this also uses a
    //     Xapian::MatchDecider object, but applied to the results by Recoll.
    // 
    // The result filtering approach was the first implemented. 
    //
    // The efficiency of both methods depend on the searches, so the code
    // for both has been kept.  A nice point for the Xapian approach is that
    // the result count estimate are correct (they are wrong with
    // the postfilter approach). It is also faster in some worst case scenarios
    // so this now the default (but the post-filtering is faster in many common
    // cases).
    // 
    // Which is used is decided in SetQuery(), by setting either of
    // the two following members. This in turn is controlled by a
    // preprocessor directive.

#define XAPIAN_FILTERING 1

    Xapian::MatchDecider *decider;   // Xapian does the filtering
    Xapian::MatchDecider *postfilter; // Result filtering done by Recoll

    Xapian::Enquire      *enquire; // Open query descriptor.
    Xapian::MSet          mset;    // Partial result set
    Query *m_q;
    // Term frequencies for current query. See makeAbstract, setQuery
    map<string, double>  termfreqs; 

    Native(Query *q)
	: decider(0), postfilter(0), enquire(0), m_q(q)
    { }

    ~Native() {
	delete decider;
	delete postfilter;
	delete enquire;
    }
};

}
#endif /* _rclquery_p_h_included_ */
