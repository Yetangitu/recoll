/* Copyright (C) 2011 J.F.Dockes
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

#ifndef _CSTR_H_INCLUDED_
#define _CSTR_H_INCLUDED_

// recoll mostly uses STL strings. In many places we had automatic
// conversion from a C string to an STL one. This costs, and can
// become significant if used often.
//
// This file and the associated .cpp file declares/defines constant
// strings used in the program. Strings are candidates for a move here
// when they are used in a fast loop or are shared.

#include <string>
using std::string;

// The following slightly hacky preprocessing directives and the
// companion code in the cpp file looks complicated, but it just
// ensures that we only have to write the strings once to get the
// extern declaration and the definition.
#ifdef RCLIN_CSTR_CPPFILE
#undef DEF_CSTR
#define DEF_CSTR(NM, STR) const string cstr_##NM(STR)
#else
#define DEF_CSTR(NM, STR) extern const string cstr_##NM
#endif

DEF_CSTR(author, "author");
DEF_CSTR(caption, "caption");
DEF_CSTR(charset, "charset");
DEF_CSTR(content, "content");
DEF_CSTR(dmtime, "dmtime");
DEF_CSTR(dquote, "\"");
DEF_CSTR(fbytes, "fbytes");
DEF_CSTR(fileu, "file://");
DEF_CSTR(fmtime, "fmtime");
DEF_CSTR(ipath, "ipath");
DEF_CSTR(iso_8859_1, "ISO-8859-1");
DEF_CSTR(mimetype, "mimetype");
DEF_CSTR(minwilds, "*?[");
DEF_CSTR(newline, "\n");
DEF_CSTR(null, "");
DEF_CSTR(plus, "+");
DEF_CSTR(textplain, "text/plain");
DEF_CSTR(url, "url");

#endif /* _CSTR_H_INCLUDED_ */