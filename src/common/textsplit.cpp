#ifndef lint
static char rcsid[] = "@(#$Id: textsplit.cpp,v 1.6 2005-02-08 09:34:46 dockes Exp $ (C) 2004 J.F.Dockes";
#endif
#ifndef TEST_TEXTSPLIT

#include <iostream>
#include <string>

#include "textsplit.h"
#include "debuglog.h"

using namespace std;

/**
 * Splitting a text into words. The code in this file will work with any 
 * charset where the basic separators (.,- etc.) have their ascii values 
 * (ok for UTF-8, ascii, iso8859* and quite a few others).
 *
 * We work in a way which would make it quite difficult to handle non-ascii
 * separator chars (en-dash,etc.). We would then need to actually parse the 
 * utf-8 stream, and use a different way to classify the characters (instead 
 * of a 256 slot array).
 *
 * We are also not using capitalization information.
 */

// Character classes: we have three main groups, and then some chars
// are their own class because they want special handling.
// We have an array with 256 slots where we keep the character types. 
// The array could be fully static, but we use a small function to fill it 
// once.
enum CharClass {LETTER=256, SPACE=257, DIGIT=258};
static int charclasses[256];
static void setcharclasses()
{
    static int init = 0;
    if (init)
	return;
    unsigned int i;
    for (i = 0 ; i < 256 ; i ++)
	charclasses[i] = LETTER;

    for (i = 0; i < ' ';i++)
	charclasses[i] = SPACE;

    char digits[] = "0123456789";
    for (i = 0; i  < strlen(digits); i++)
	charclasses[int(digits[i])] = DIGIT;

    char blankspace[] = "\t\v\f ";
    for (i = 0; i < strlen(blankspace); i++)
	charclasses[int(blankspace[i])] = SPACE;

    char seps[] = "!\"$%&()/<=>[\\]^{|}~:;,*`?";
    for (i = 0; i  < strlen(seps); i++)
	charclasses[int(seps[i])] = SPACE;

    char special[] = ".@+-,#'\n\r";
    for (i = 0; i  < strlen(special); i++)
	charclasses[int(special[i])] = special[i];

    init = 1;
    //for (i=0;i<256;i++)cerr<<i<<" -> "<<charclasses[i]<<endl;
}

// Do some cleanup (the kind which is simpler to do here than in the main loop,
// then send term to our client.
bool TextSplit::emitterm(bool isspan, string &w, int pos, bool doerase,
			 int btstart, int btend)
{
    LOGDEB2(("TextSplit::emitterm: '%s' pos %d\n", w.c_str(), pos));
    if (fq && !isspan)
	return true;
    if (!cb)
	return false;

    // Maybe trim end of word. These are chars that we would keep inside 
    // a word or span, but not at the end
    while (w.length() > 0) {
	switch (w[w.length()-1]) {
	case '.':
	case ',':
	case '@':
	case '\'':
	    w.erase(w.length()-1);
	    break;
	default:
	    goto breakloop1;
	}
    }
 breakloop1:

    // In addition, it doesn't make sense currently to keep ' at the beginning
    while (w.length() > 0) {
	switch (w[0]) {
	case ',':
	case '\'':
	    w.erase(w.length()-1);
	    break;
	default:
	    goto breakloop2;
	}
    }
 breakloop2:

    // 1 char word: we index single letters, but nothing else
    if (w.length() == 1) {
	int c = (int)w[0];
	if (charclasses[c] != LETTER && charclasses[c] != DIGIT) {
	    //cerr << "ERASING single letter term " << c << endl;
	    w.erase();
	}
    }
    if (w.length() > 0 && w.length() < (unsigned)maxWordLength) {
	bool ret = cb->takeword(w, pos, btstart, btend);
	if (doerase)
	    w.erase();
	return ret;
    }
    return true;
}

/* 
 * We basically emit a word every time we see a separator, but some chars are
 * handled specially so that special cases, ie, c++ and dockes@okyz.com etc, 
 * are handled properly,
 */
bool TextSplit::text_to_words(const string &in)
{
    LOGDEB2(("TextSplit::text_to_words: cb %p\n", cb));
    setcharclasses();
    string span;
    string word;
    bool number = false;
    int wordpos = 0;
    int spanpos = 0;
    unsigned int i;

    for (i = 0; i < in.length(); i++) {
	int c = in[i];
	int cc = charclasses[c]; 
	switch (cc) {
	case SPACE:
	SPACE:
	    if (word.length()) {
		if (span.length() != word.length()) {
		    if (!emitterm(true, span, spanpos, true, i-span.length(), i)) 
			return false;
		}
		if (!emitterm(false, word, wordpos++, true, i-word.length(), i))
		    return false;
		number = false;
	    }
	    spanpos = wordpos;
	    span.erase();
	    break;
	case '-':
	case '+':
	    if (word.length() == 0) {
		if (i < in.length() && charclasses[int(in[i+1])] == DIGIT) {
		    number = true;
		    word += c;
		    span += c;
		}
	    } else {
		if (span.length() != word.length()) {
		    if (!emitterm(true, span, spanpos, false, i-span.length(), i))
			return false;
		}
		if (!emitterm(false, word, wordpos++, true, i-word.length(), i))
		    return false;
		number = false;
		span += c;
	    }
	    break;
	case '@':
	    if (word.length()) {
		if (span.length() != word.length()) {
		    if (!emitterm(true, span, spanpos, false, i-span.length(), i))
			return false;
		}
		if (!emitterm(false, word, wordpos++, true, i-word.length(), i))
		    return false;
		number = false;
	    } else
		word += c;
	    span += c;
	    break;
	case '\'':
	    if (word.length()) {
		if (span.length() != word.length()) {
		    if (!emitterm(true, span, spanpos, false, i-span.length(), i))
			return false;
		}
		if (!emitterm(false, word, wordpos++, true, i-word.length(), i))
		    return false;
		number = false;
		span += c;
	    }
	    break;
	case '.':
	    if (number) {
		word += c;
	    } else {
		if (word.length()) {
		    if (!emitterm(false, word, wordpos++, true, i-word.length(), i))
			return false;
		    number = false;
		} else 
		    word += c;
	    }
	    span += c;
	    break;
	case '#': 
	    // Keep it only at end of word...
	    if (word.length() > 0 && 
		(i == in.length() -1 || charclasses[int(in[i+1])] == SPACE ||
		 in[i+1] == '\n' || in[i+1] == '\r')) {
		word += c;
		span += c;
	    }
		
	    break;
	case '\n':
	case '\r':
	    if (span.length() && span[span.length() - 1] == '-') {
		// if '-' is the last char before end of line, just
		// ignore the line change. This is the right thing to
		// do almost always. We'd then need a way to check if
		// the - was added as part of the word hyphenation, or was 
		// there in the first place, but this would need a dictionary.
	    } else {
		// Handle like a normal separator
		goto SPACE;
	    }
	    break;
	case LETTER:
	case DIGIT:
	default:
	    if (word.length() == 0) {
		if (cc == DIGIT)
		    number = true;
		else
		    number = false;
	    }
	    word += (char)c;
	    span += (char)c;
	    break;
	}
    }
    if (word.length()) {
	if (span.length() != word.length())
	    if (!emitterm(true, span, spanpos, true, i-span.length(), i))
		return false;
	return emitterm(false, word, wordpos, true, i-word.length(), i);
    }
    return true;
}

#else  // TEST driver ->

#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>

#include <iostream>

#include "textsplit.h"
#include "readfile.h"
#include "debuglog.h"

using namespace std;

// A small class to hold state while splitting text
class mySplitterCB : public TextSplitCB {
 public:
    bool takeword(const std::string &term, int pos, int bs, int be) {
	printf("%3d %-20s %d %d\n", pos, term.c_str(), bs, be);
	return true;
    }
};

static string teststring = 
    "jfd@okyz.com "
    "Ceci. Est;Oui 1.24 n@d @net .net t@v@c c# c++ -10 o'brien l'ami "
    "a 134 +134 -14 -1.5 +1.5 1.54e10 a "
    "@^#$(#$(*) "
    "one\n\rtwo\nthree-\nfour "
    "[olala][ululu] "
    "'o'brien' "						
    "\n"							      
;

int main(int argc, char **argv)
{
    DebugLog::getdbl()->setloglevel(DEBDEB1);
    DebugLog::setfilename("stderr");
    mySplitterCB cb;
    TextSplit splitter(&cb);
    if (argc == 2) {
	string data;
	if (!file_to_string(argv[1], data)) 
	    exit(1);
	splitter.text_to_words(data);
    } else {
	cout << teststring << endl;  
	splitter.text_to_words(teststring);
    }
    
}
#endif // TEST
