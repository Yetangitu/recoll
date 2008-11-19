#ifndef lint
static char rcsid[] = "@(#$Id: reslistpager.cpp,v 1.1 2008-11-19 12:19:40 dockes Exp $ (C) 2007 J.F.Dockes";
#endif

#include "reslistpager.h"
#include "debuglog.h"
#include "rclconfig.h"
#include "smallut.h"
#include "plaintorich.h"
#include "mimehandler.h"

// This should be passed as an input object to the pager instead
class PlainToRichHtReslist : public PlainToRich {
public:
    virtual ~PlainToRichHtReslist() {}
    virtual string startMatch() {return string("<font color=\"blue\">");}
    virtual string endMatch() {return string("</font>");}
};
// IDEM
struct Prefs {
    bool queryBuildAbstract;
    bool queryReplaceAbstract;
};
Prefs prefs = {true, true};

void ResListPager::resultPageNext()
{
    if (m_docSource.isNull())
	return;

    int resCnt = m_docSource->getResCnt();
    LOGDEB(("ResListPager::resultPageNext: rescnt %d, winfirst %d\n", 
	    resCnt, m_winfirst));

    if (m_winfirst < 0) {
	m_winfirst = 0;
    } else {
	m_winfirst += m_pagesize;
    }
    // Get the next page of results.
    vector<ResListEntry> npage;
    int pagelen = m_docSource->getSeqSlice(m_winfirst, m_pagesize, npage);

    // If page was truncated, there is no next
    m_hasNext = (pagelen == m_pagesize);

    if (pagelen <= 0) {
	// No results ? This can only happen on the first page or if the
	// actual result list size is a multiple of the page pref (else
	// there would have been no Next on the last page)
	if (m_winfirst > 0) {
	    // Have already results. Let them show, just disable the
	    // Next button. We'd need to remove the Next link from the page
	    // too.
	    // Restore the m_winfirst value, let the current result vector alone
	    m_winfirst -= m_pagesize;
	} else {
	    // No results at all (on first page)
	    m_winfirst = -1;
	}
	return;
    }
    m_respage = npage;
}

bool ResListPager::append(const string& data)
{
    fprintf(stderr, "%s", data.c_str());
    return true;
}

string ResListPager::tr(const string& in) 
{
    return in;
}

void ResListPager::displayPage()
{
    string chunk;
    if (pageEmpty()) {
	chunk = "<html><head></head><body>"
	    "<p><font size=+1><b>";
	chunk += m_docSource->title();
	chunk += "</b></font>";
	chunk += "<a href=\"H-1\">";
	chunk += tr("Show query details");
	chunk += "</a><br>";
	append(chunk);
	return;
    }

    // Display list header
    // We could use a <title> but the textedit doesnt display
    // it prominently
    // Note: have to append text in chunks that make sense
    // html-wise. If we break things up too much, the editor
    // gets confused. Hence the use of the 'chunk' text
    // accumulator
    // Also note that there can be results beyond the estimated resCnt.
    chunk = "<qt><head></head><body><p>"
	"<font size=+1><b>";
    chunk += m_docSource->title();
    chunk += "</b></font>"
	"&nbsp;&nbsp;&nbsp;";

    if (pageEmpty()) {
	chunk += tr("<p><b>No results found</b> for ");
    } else {
	unsigned int resCnt = m_docSource->getResCnt();
	if (m_winfirst + m_respage.size() < resCnt) {
	    string f1 =	tr("Documents <b>%d-%d</b> out of at least <b>%d</b> for ");
	    char buf[1024];
	    snprintf(buf, 1023, f1.c_str(), m_winfirst+1, 
		     m_winfirst + m_respage.size(), resCnt);
	    chunk += buf;
	} else {
	    string f1 = tr("Documents <b>%d-%d</b> for ");
	    char buf[1024];
	    snprintf(buf, 1023, f1.c_str(), m_winfirst + 1, 
		     m_winfirst + m_respage.size());
	    chunk += buf;
	}
    }
    chunk += "<a href=\"H-1\">";
    chunk += tr("(show query)");
    chunk += "</a></p>";

    append(chunk);
    if (pageEmpty())
	return;


    HiliteData hdata;
    m_docSource->getTerms(hdata.terms, hdata.groups, hdata.gslks);

    // Emit data for result entry paragraph. Do it in chunks that make sense
    // html-wise, else our client may get confused
    for (int i = 0; i < (int)m_respage.size(); i++) {

	Rcl::Doc &doc(m_respage[i].doc);
	string& sh(m_respage[i].subHeader);
	int percent;
	if (doc.pc == -1) {
	    percent = 0;
	    // Document not available, maybe other further, will go on.
	    doc.meta[Rcl::Doc::keyabs] = string(tr("Unavailable document"));
	} else {
	    percent = doc.pc;
	}
	// Percentage of 'relevance'
	char perbuf[10];
	sprintf(perbuf, "%3d%% ", percent);

	// Determine icon to display if any
	string iconpath;
	RclConfig::getMainConfig()->getMimeIconName(doc.mimetype, &iconpath);

	// Printable url: either utf-8 if transcoding succeeds, or url-encoded
	string url;
	printableUrl(RclConfig::getMainConfig()->getDefCharset(), doc.url, url);

	// Make title out of file name if none yet
	if (doc.meta[Rcl::Doc::keytt].empty()) {
	    doc.meta[Rcl::Doc::keytt] = path_getsimple(url);
	}

	// Result number
	char numbuf[20];
	int docnumforlinks = m_winfirst + 1 + i;
	sprintf(numbuf, "%d", docnumforlinks);

	// Document date: either doc or file modification time
	char datebuf[100];
	datebuf[0] = 0;
	if (!doc.dmtime.empty() || !doc.fmtime.empty()) {
	    time_t mtime = doc.dmtime.empty() ?
		atol(doc.fmtime.c_str()) : atol(doc.dmtime.c_str());
	    struct tm *tm = localtime(&mtime);
#ifndef sun
	    strftime(datebuf, 99, "&nbsp;%Y-%m-%d&nbsp;%H:%M:%S&nbsp;%z", tm);
#else
	    strftime(datebuf, 99, "&nbsp;%Y-%m-%d&nbsp;%H:%M:%S&nbsp;%Z", tm);
#endif
	}

	// Size information. We print both doc and file if they differ a lot
	long fsize = -1, dsize = -1;
	if (!doc.dbytes.empty())
	    dsize = atol(doc.dbytes.c_str());
	if (!doc.fbytes.empty())
	    fsize = atol(doc.fbytes.c_str());
	string sizebuf;
	if (dsize > 0) {
	    sizebuf = displayableBytes(dsize);
	    if (fsize > 10 * dsize && fsize - dsize > 1000)
		sizebuf += string(" / ") + displayableBytes(fsize);
	} else if (fsize >= 0) {
	    sizebuf = displayableBytes(fsize);
	}

	string abstract;
	if (prefs.queryBuildAbstract && 
	    (doc.syntabs || prefs.queryReplaceAbstract)) {
	    abstract = m_docSource->getAbstract(doc);
	} else {
	    abstract = doc.meta[Rcl::Doc::keyabs];
	}
	// No need to call escapeHtml(), plaintorich handles it
	list<string> lr;
	PlainToRichHtReslist ptr;
	ptr.plaintorich(abstract, lr, hdata);
	string richabst = lr.front();

	// Links;
	string linksbuf;
	char vlbuf[100];
	if (canIntern(doc.mimetype, RclConfig::getMainConfig())) { 
	    sprintf(vlbuf, "\"P%d\"", docnumforlinks);
	    linksbuf += string("<a href=") + vlbuf + ">" + "Preview" + "</a>" 
		+ "&nbsp;&nbsp;";
	}
	if (!RclConfig::getMainConfig()->getMimeViewerDef(doc.mimetype).empty()) {
	    sprintf(vlbuf, "E%d", docnumforlinks);
	    linksbuf += string("<a href=") + vlbuf + ">" + "Open" + "</a>";
	}

	// Build the result list paragraph:
	chunk = "";

	// Subheader: this is used by history
	if (!sh.empty())
	    chunk += "<p><b>" + sh + "</p>\n<p>";
	else
	    chunk += "<p>";

	// Configurable stuff
	map<char,string> subs;
	subs['A'] = !richabst.empty() ? richabst + "<br>" : "";
	subs['D'] = datebuf;
	subs['I'] = iconpath;
	subs['K'] = !doc.meta[Rcl::Doc::keykw].empty() ? 
	    escapeHtml(doc.meta[Rcl::Doc::keykw]) + "<br>" : "";
	subs['L'] = linksbuf;
	subs['N'] = numbuf;
	subs['M'] = doc.mimetype;
	subs['R'] = perbuf;
	subs['S'] = sizebuf;
	subs['T'] = escapeHtml(doc.meta[Rcl::Doc::keytt]);
	subs['U'] = url;

	string formatted;
	pcSubst(m_parformat, formatted, subs);
	chunk += formatted;

	chunk += "</p>\n";

	LOGDEB2(("Chunk: [%s]\n", (const char *)chunk.c_str()));
	append(chunk);
    }

    // Footer
    chunk = "<p align=\"center\">";
    if (hasPrev() || hasNext()) {
	if (hasPrev()) {
	    chunk += "<a href=\"p-1\"><b>";
	    chunk += tr("Previous");
	    chunk += "</b></a>&nbsp;&nbsp;&nbsp;";
	}
	if (hasNext()) {
	    chunk += "<a href=\"n-1\"><b>";
	    chunk += tr("Next");
	    chunk += "</b></a>";
	}
    }
    chunk += "</p>\n";
    chunk += "</body></html>\n";
    append(chunk);
}
