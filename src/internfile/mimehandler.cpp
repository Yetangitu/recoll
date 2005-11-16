#ifndef lint
static char rcsid[] = "@(#$Id: mimehandler.cpp,v 1.11 2005-11-16 15:07:20 dockes Exp $ (C) 2004 J.F.Dockes";
#endif

#include <iostream>
#include <string>
using namespace std;

#include "mimehandler.h"
#include "readfile.h"
#include "csguess.h"
#include "transcode.h"
#include "debuglog.h"
#include "smallut.h"
#include "html.h"
#include "mail.h"
#include "execmd.h"
#include "pathut.h"

class MimeHandlerText : public MimeHandler {
 public:
    MimeHandler::Status mkDoc(RclConfig *conf, const string &fn, 
		const string &mtype, Rcl::Doc &docout, string&);
    
};

// Process a plain text file
MimeHandler::Status MimeHandlerText::mkDoc(RclConfig *conf, const string &fn, 
			     const string &mtype, Rcl::Doc &docout, string&)
{
    string otext;
    if (!file_to_string(fn, otext))
	return MimeHandler::MHError;
	
    // Try to guess charset, then convert to utf-8, and fill document
    // fields The charset guesser really doesnt work well in general
    // and should be avoided (especially for short documents)
    string charset;
    if (conf->getGuessCharset()) {
	charset = csguess(otext, conf->getDefCharset());
    } else
	charset = conf->getDefCharset();
    string utf8;
    LOGDEB1(("textPlainToDoc: transcod from %s to %s\n", charset, "UTF-8"));

    if (!transcode(otext, utf8, charset, "UTF-8")) {
	cerr << "textPlainToDoc: transcode failed: charset '" << charset
	     << "' to UTF-8: "<< utf8 << endl;
	otext.erase();
	return MimeHandler::MHError;
    }

    Rcl::Doc out;
    out.origcharset = charset;
    out.text = utf8;
    docout = out;
    return MimeHandler::MHDone;
}

class MimeHandlerExec : public MimeHandler {
 public:
    list<string> params;
    virtual ~MimeHandlerExec() {}
    virtual MimeHandler::Status mkDoc(RclConfig *conf, const string &fn, 
				       const string &mtype, Rcl::Doc &docout, 
				       string&);

};

    
// Execute an external program to translate a file from its native format
// to html. Then call the html parser to do the actual indexing
MimeHandler::Status 
MimeHandlerExec::mkDoc(RclConfig *conf, const string &fn, 
			const string &mtype, Rcl::Doc &docout, string&)
{
    if (params.empty()) {
	// Hu ho
	LOGERR(("MimeHandlerExec::mkDoc: empty params for mime %s\n",
		mtype.c_str()));
	return MimeHandler::MHError;
    }
    // Command name
    string cmd = find_filter(conf, params.front());
    
    // Build parameter list: delete cmd name and add the file name
    list<string>::iterator it = params.begin();
    list<string>myparams(++it, params.end());
    myparams.push_back(fn);

    // Execute command and store the result text, which is supposedly html
    string html;
    ExecCmd exec;
    int status = exec.doexec(cmd, myparams, 0, &html);
    if (status) {
	LOGERR(("MimeHandlerExec: command status 0x%x: %s\n", 
		status, cmd.c_str()));
	return MimeHandler::MHError;
    }

    // Process/index  the html
    MimeHandlerHtml hh;
    return hh.mkDoc(conf, fn, html, mtype, docout);
}

static MimeHandler *mhfact(const string &mime)
{
    if (!stringlowercmp("text/plain", mime))
	return new MimeHandlerText;
    else if (!stringlowercmp("text/html", mime))
	return new MimeHandlerHtml;
    else if (!stringlowercmp("text/x-mail", mime))
	return new MimeHandlerMail;
    else if (!stringlowercmp("message/rfc822", mime))
	return new MimeHandlerMail;
    return 0;
}

/**
 * Return handler function for given mime type
 */
MimeHandler *getMimeHandler(const std::string &mtype, ConfTree *mhandlers)
{
    // Return handler definition for mime type
    string hs;
    if (!mhandlers->get(mtype, hs, "index")) {
	LOGDEB(("getMimeHandler: no handler for '%s'\n", mtype.c_str()));
	return 0;
    }

    // Break definition into type and name 
    list<string> toks;
    ConfTree::stringToStrings(hs, toks);
    if (toks.empty()) {
	LOGERR(("getMimeHandler: bad mimeconf line for %s\n", mtype.c_str()));
	return 0;
    }

    // Retrieve handler function according to type
    if (!stringlowercmp("internal", toks.front())) {
	return mhfact(mtype);
    } else if (!stringlowercmp("dll", toks.front())) {
	return 0;
    } else if (!stringlowercmp("exec", toks.front())) {
	if (toks.size() < 2) {
	    LOGERR(("getMimeHandler: bad line for %s: %s\n", mtype.c_str(),
		    hs.c_str()));
	    return 0;
	}
	MimeHandlerExec *h = new MimeHandlerExec;
	list<string>::const_iterator it1 = toks.begin();
	it1++;
	for (;it1 != toks.end();it1++)
	    h->params.push_back(*it1);
	return h;
    }
    return 0;
}

/**
 * Return external viewer exec string for given mime type
 */
string getMimeViewer(const std::string &mtype, ConfTree *mhandlers)
{
    string hs;
    mhandlers->get(mtype, hs, "view");
    return hs;
}

/**
 * Return icon name
 */
string getMimeIconName(const std::string &mtype, ConfTree *mhandlers)
{
    string hs;
    mhandlers->get(mtype, hs, "icons");
    return hs;
}

/** 
 * Return decompression command line for given mime type
 */
bool getUncompressor(const std::string &mtype, ConfTree *mhandlers, 
		     list<string>& cmd)
{
    string hs;

    mhandlers->get(mtype, hs, "");
    if (hs.empty())
	return false;
    list<string> tokens;
    ConfTree::stringToStrings(hs, tokens);
    if (tokens.empty()) {
	LOGERR(("getUncompressor: empty spec for mtype %s\n", mtype.c_str()));
	return false;
    }
    if (stringlowercmp("uncompress", tokens.front())) 
	return false;
    list<string>::iterator it = tokens.begin();
    cmd.assign(++it, tokens.end());
    return true;
}
