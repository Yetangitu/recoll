/* This file was copied from omega-0.8.5 and modified */

/* myhtmlparse.cc: subclass of HtmlParser for extracting text
 *
 * ----START-LICENCE----
 * Copyright 1999,2000,2001 BrightStation PLC
 * Copyright 2002,2003,2004 Olly Betts
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 * -----END-LICENCE-----
 */
#include <time.h>
#include <stdio.h>

#include "myhtmlparse.h"
#include "indextext.h" // for lowercase_term()
#include "mimeparse.h"
#include "smallut.h"
#include "cancelcheck.h"
#include "debuglog.h"

// Compress whitespace and suppress newlines
// Note that we independantly add some newlines to the output text in the
// tag processing code. Like this, the preview looks a bit more like what a
// browser would display.
// We keep whitespace inside <pre> tags
void
MyHtmlParser::process_text(const string &text)
{
    LOGDEB2(("process_text: pending_space %d txt [%s]\n", pending_space,
	    text.c_str()));
    CancelCheck::instance().checkCancel();

    if (!in_script_tag && !in_style_tag) {
	if (!in_pre_tag) {
	    string::size_type b = 0;
	    bool only_space = true;
	    while ((b = text.find_first_not_of(WHITESPACE, b)) != string::npos) {
		only_space = false;
		// If space specifically needed or chunk begins with
		// whitespace, add exactly one space
		if (pending_space || b != 0) {
			dump += ' ';
		}
		pending_space = true;
		string::size_type e = text.find_first_of(WHITESPACE, b);
		if (e == string::npos) {
		    dump += text.substr(b);
		    pending_space = false;
		    break;
		}
		dump += text.substr(b, e - b);
		b = e + 1;
	    }
	    if (only_space)
		pending_space = true;
	} else {
	    if (pending_space)
		dump += ' ';
	    dump += text;
	}
    }
}

void
MyHtmlParser::opening_tag(const string &tag, const map<string,string> &p)
{
    LOGDEB2(("opening_tag: [%s]\n", tag.c_str()));
#if 0
    cout << "TAG: " << tag << ": " << endl;
    map<string, string>::const_iterator x;
    for (x = p.begin(); x != p.end(); x++) {
	cout << "  " << x->first << " -> '" << x->second << "'" << endl;
    }
#endif
    if (tag.empty()) return;
    switch (tag[0]) {
	case 'a':
	    if (tag == "address") pending_space = true;
	    break;
	case 'b':
	    if (tag == "body") {
		dump = "";
		in_body_tag = true;
		break;
	    }
	    if (tag == "blockquote" || tag == "br") {
		dump += '\n';
		pending_space = true;
	    }
	    break;
	case 'c':
	    if (tag == "center") pending_space = true;
	    break;
	case 'd':
	    if (tag == "dd" || tag == "dir" || tag == "div" || tag == "dl" ||
		tag == "dt") pending_space = true;
	    if (tag == "dt")
		dump += '\n';
	    break;
	case 'e':
	    if (tag == "embed") pending_space = true;
	    break;
	case 'f':
	    if (tag == "fieldset" || tag == "form") pending_space = true;
	    break;
	case 'h':
	    // hr, and h1, ..., h6
	    if (tag.length() == 2 && strchr("r123456", tag[1])) {
		dump += '\n';
		pending_space = true;
	    }
	    break;
	case 'i':
	    if (tag == "iframe" || tag == "img" || tag == "isindex" ||
		tag == "input") pending_space = true;
	    break;
	case 'k':
	    if (tag == "keygen") pending_space = true;
	    break;
	case 'l':
	    if (tag == "legend" || tag == "li" || tag == "listing") {
		dump += '\n';
		pending_space = true;
	    }
	    break;
	case 'm':
	    if (tag == "meta") {
		map<string, string>::const_iterator i, j;
		if ((i = p.find("content")) != p.end()) {
		    if ((j = p.find("name")) != p.end()) {
			string name = j->second;
			lowercase_term(name);
			if (name == "description") {
			    if (sample.empty()) {
				sample = i->second;
				decode_entities(sample);
			    }
			} else if (name == "keywords") {
			    if (!keywords.empty()) keywords += ' ';
			    string tmp = i->second;
			    decode_entities(tmp);
			    keywords += tmp;
			} else if (name == "date") {
			    // Yes this doesnt exist. It's output by filters
			    // And the format isn't even standard http/html
			    // FIXME
			    string tmp = i->second;
			    decode_entities(tmp);
			    struct tm tm;
			    if (strptime(tmp.c_str(), 
					 " %Y-%m-%d %H:%M:%S ", &tm)) {
				char ascuxtime[100];
				sprintf(ascuxtime, "%ld", (long)mktime(&tm));
				dmtime = ascuxtime;
			    }
			} 
#if 0 // We're not a robot, so we don't care about robots metainfo
			else if (name == "robots") {
			    string val = i->second;
			    decode_entities(val);
			    lowercase_term(val);
			    if (val.find("none") != string::npos ||
				val.find("noindex") != string::npos) {
				indexing_allowed = false;
				LOGDEB1(("myhtmlparse: robots/noindex\n"));
				throw false;
			    }
			}
#endif // 0
		    } else if ((j = p.find("http-equiv")) != p.end()) {
			string hequiv = j->second;
			lowercase_term(hequiv);
			if (hequiv == "content-type") {
			    string value = i->second;
			    MimeHeaderValue p;
			    parseMimeHeaderValue(value, p);
			    map<string, string>::const_iterator k;
			    if ((k = p.params.find("charset")) != 
				p.params.end()) {
				doccharset = k->second;
				if (!samecharset(doccharset, ocharset)) {
				    LOGDEB1(("Doc specified charset '%s' "
					     "differs from announced '%s'\n",
					     doccharset.c_str(), 
					     ocharset.c_str()));
				    throw false;
				}
			    }
			}
		    }
		}
		break;
	    }
	    if (tag == "marquee" || tag == "menu" || tag == "multicol")
		pending_space = true;
	    break;
	case 'o':
	    if (tag == "ol" || tag == "option") pending_space = true;
	    break;
	case 'p':
	    if (tag == "p" || tag == "plaintext") {
		dump += '\n';
		pending_space = true;
	    } else if (tag == "pre") {
		in_pre_tag = true;
		dump += '\n';
		pending_space = true;
	    }
	    break;
	case 'q':
	    if (tag == "q") pending_space = true;
	    break;
	case 's':
	    if (tag == "style") {
		in_style_tag = true;
		break;
	    }
	    if (tag == "script") {
		in_script_tag = true;
		break;
	    }
	    if (tag == "select") pending_space = true;
	    break;
	case 't':
	    if (tag == "table" || tag == "td" || tag == "textarea" ||
		tag == "th") pending_space = true;
	    break;
	case 'u':
	    if (tag == "ul") pending_space = true;
	    break;
	case 'x':
	    if (tag == "xmp") pending_space = true;
	    break;
    }
}

void
MyHtmlParser::closing_tag(const string &tag)
{
    LOGDEB2(("closing_tag: [%s]\n", tag.c_str()));
    if (tag.empty()) return;
    switch (tag[0]) {
	case 'a':
	    if (tag == "address") pending_space = true;
	    break;
	case 'b':
	    if (tag == "body") {
		LOGDEB1(("Myhtmlparse: body close tag found\n"));
		in_body_tag = false;
		throw true;
	    }
	    if (tag == "blockquote" || tag == "br") pending_space = true;
	    break;
	case 'c':
	    if (tag == "center") pending_space = true;
	    break;
	case 'd':
	    if (tag == "dd" || tag == "dir" || tag == "div" || tag == "dl" ||
		tag == "dt") pending_space = true;
	    break;
	case 'f':
	    if (tag == "fieldset" || tag == "form") pending_space = true;
	    break;
	case 'h':
	    // hr, and h1, ..., h6
	    if (tag.length() == 2 && strchr("r123456", tag[1]))
		pending_space = true;
	    break;
	case 'i':
	    if (tag == "iframe") pending_space = true;
	    break;
	case 'l':
	    if (tag == "legend" || tag == "li" || tag == "listing")
		pending_space = true;
	    break;
	case 'm':
	    if (tag == "marquee" || tag == "menu") pending_space = true;
	    break;
	case 'o':
	    if (tag == "ol" || tag == "option") pending_space = true;
	    break;
	case 'p':
	    if (tag == "p") {
		pending_space = true;
	    } else if  (tag == "pre") {
		pending_space = true;
		in_pre_tag = false;
	    }
	    break;
	case 'q':
	    if (tag == "q") pending_space = true;
	    break;
	case 's':
	    if (tag == "style") {
		in_style_tag = false;
		break;
	    }
	    if (tag == "script") {
		in_script_tag = false;
		break;
	    }
	    if (tag == "select") pending_space = true;
	    break;
	case 't':
	    if (tag == "title") {
		if (title.empty()) {
		    title = dump;
		    dump = "";
		}
		break;
	    }
	    if (tag == "table" || tag == "td" || tag == "textarea" ||
		tag == "th") pending_space = true;
	    break;
	case 'u':
	    if (tag == "ul") pending_space = true;
	    break;
	case 'x':
	    if (tag == "xmp") pending_space = true;
	    break;
    }
}

// This gets called when hitting eof. 
// We used to do: 
//    > If the <body> is open, do
//    > something with the text (that is, don't throw up). Else, things are
//    > too weird, throw an error. We don't get called if the parser finds
//    > a closing body tag (exception gets thrown by closing_tag())
// But we don't throw any more. Whatever text we've extracted up to now is
// better than nothing.
void
MyHtmlParser::do_eof()
{
    //    if (!in_body_tag)
    //	throw(false);
}
