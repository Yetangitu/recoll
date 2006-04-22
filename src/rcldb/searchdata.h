#ifndef _SEARCHDATA_H_INCLUDED_
#define _SEARCHDATA_H_INCLUDED_
/* @(#$Id: searchdata.h,v 1.2 2006-04-22 06:27:37 dockes Exp $  (C) 2004 J.F.Dockes */

namespace Rcl {
/**
 * Holder for query data 
 */
class AdvSearchData {
    public:
    string allwords;
    string phrase;
    string orwords;
    string orwords1; // Have two instances of orwords for and'ing them
    string nowords;
    string filename; 
    list<string> filetypes; // restrict to types. Empty if inactive
    string topdir; // restrict to subtree. Empty if inactive
    string description; // Printable expanded version of the complete query
                        // returned after setQuery.
    void erase() {
	allwords.erase();
	phrase.erase();
	orwords.erase();
	orwords1.erase();
	nowords.erase();
	filetypes.clear(); 
	topdir.erase();
	filename.erase();
	description.erase();
    }
    bool fileNameOnly() {
	return allwords.empty() && phrase.empty() && orwords.empty() && 
	    orwords1.empty() && nowords.empty();
    }
};

}

#endif /* _SEARCHDATA_H_INCLUDED_ */
