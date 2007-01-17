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
#ifndef _RCLDOC_H_INCLUDED_
#define _RCLDOC_H_INCLUDED_
/* @(#$Id: rcldoc.h,v 1.2 2007-01-17 13:53:41 dockes Exp $  (C) 2006 J.F.Dockes */

#include <string>

#ifndef NO_NAMESPACES
using std::string;
namespace Rcl {
#endif

/**
 * Dumb holder for document attributes and data
 */
class Doc {
 public:
    // These fields potentially go into the document data record
    // We indicate the routine that sets them up during indexing
    string url;          // This is just "file://" + binary filename. 
                         // No transcoding: this is used to access files
                         // Computed from fn by Db::add
    string utf8fn;       // Transcoded version of the simple file name for
                         // SFN-prefixed specific file name indexation
                         // Set by DbIndexer::processone
    string ipath;        // Internal path for multi-doc files. Ascii
                         // Set by DbIndexer::processone
    string mimetype;     // Set by FileInterner::internfile
    string fmtime;       // File modification time as decimal ascii unix time
                         // Set by DbIndexer::processone
    string dmtime;       // Data reference date (same format). Ie: mail date
                         // Possibly set by handler
    string origcharset;  // Charset we transcoded from (in case we want back)
                         // Possibly set by handler
    string title;        // Possibly set by handler
    string author;       // Possibly set by handler
    string keywords;     // Possibly set by handler
    string abstract;     // Possibly set by handler
    bool   syntabs;      // true if abstract is just the top of doc, not an 
                         // explicit document attribute
    string fbytes;       // File size. Set by Db::Add
    string dbytes;       // Doc size. Set by Db::Add from text length

    // The following fields don't go to the db record
    
    string text; // During indexing only: text returned by input handler will
                 // be split and indexed 

    int pc; // used by sortseq, convenience
    unsigned long xdocid; // Opaque: rcldb doc identifier.

    void erase() {
	url.erase();
	utf8fn.erase();
	ipath.erase();
	mimetype.erase();
	fmtime.erase();
	dmtime.erase();
	origcharset.erase();
	title.erase();
	keywords.erase();
	abstract.erase();
	syntabs = false;
	fbytes.erase();
	dbytes.erase();

	text.erase();
	pc = 0;
	xdocid = 0;
    }
};


#ifndef NO_NAMESPACES
}
#endif

#endif /* _RCLDOC_H_INCLUDED_ */
