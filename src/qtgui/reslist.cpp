#ifndef lint
static char rcsid[] = "@(#$Id: reslist.cpp,v 1.19 2007-01-12 09:01:11 dockes Exp $ (C) 2005 J.F.Dockes";
#endif

#include <time.h>

#include <qapplication.h>
#include <qvariant.h>
#include <qevent.h>
#include <qpushbutton.h>
#include <qlayout.h>
#include <qtooltip.h>
#include <qwhatsthis.h>
#include <qtimer.h>
#include <qmessagebox.h>
#include <qimage.h>
#include <qclipboard.h>
#include <qscrollbar.h>

#if (QT_VERSION < 0x040000)
#include <qpopupmenu.h>
#else
#include <q3popupmenu.h>
#include <q3stylesheet.h>
#include <q3mimefactory.h>
#define QStyleSheetItem Q3StyleSheetItem
#define QMimeSourceFactory Q3MimeSourceFactory
#endif

#include "debuglog.h"
#include "recoll.h"
#include "guiutils.h"
#include "pathut.h"
#include "docseq.h"
#include "transcode.h"
#include "pathut.h"
#include "mimehandler.h"
#include "plaintorich.h"
#include "refcntr.h"

#include "reslist.h"
#include "moc_reslist.cpp"

#ifndef MIN
#define MIN(A,B) ((A) < (B) ? (A) : (B))
#endif

ResList::ResList(QWidget* parent, const char* name)
    : QTEXTBROWSER(parent, name)
{
    if (!name)
	setName("resList");
    setTextFormat(Qt::RichText);
    setReadOnly(TRUE);
    setUndoRedoEnabled(FALSE);
    languageChange();

    setTabChangesFocus(true);

    // signals and slots connections
    connect(this, SIGNAL(clicked(int, int)), this, SLOT(clicked(int,int)));
    connect(this, SIGNAL(linkClicked(const QString &, int)), 
	    this, SLOT(linkWasClicked(const QString &, int)));
    connect(this, SIGNAL(headerClicked()), this, SLOT(showQueryDetails()));
    connect(this, SIGNAL(doubleClicked(int,int)), 
	    this, SLOT(doubleClicked(int, int)));
    m_winfirst = -1;
    m_curPvDoc = -1;
    m_lstClckMod = 0;
}

ResList::~ResList()
{
}

void ResList::resetSearch() 
{
    m_winfirst = -1;
    clear();
}

void ResList::languageChange()
{
    setCaption(tr("Result list"));
}

// Acquire new docsource
void ResList::setDocSource(RefCntr<DocSequence> docsource, 
			   RefCntr<Rcl::SearchData> sdt)
{
    m_winfirst = -1;
    m_docSource = docsource;
    m_searchData = sdt;
    m_curPvDoc = -1;

    resultPageNext();
}

// Get document number from paragraph number
int ResList::docnumfromparnum(int par)
{
    if (m_winfirst == -1)
	return -1;
    std::map<int,int>::iterator it = m_pageParaToReldocnums.find(par);
    int dn;
    if (it != m_pageParaToReldocnums.end()) {
        dn = m_winfirst + it->second;
    } else {
        dn = -1;
    }
    return dn;
}

// Get paragraph number from document number
int ResList::parnumfromdocnum(int docnum)
{
    if (m_winfirst == -1 || docnum - m_winfirst < 0)
	return -1;
    docnum -= m_winfirst;
    for (std::map<int,int>::iterator it = m_pageParaToReldocnums.begin();
	 it != m_pageParaToReldocnums.end(); it++) {
	if (docnum == it->second)
	    return it->first;
    }
    return -1;
}

// Return doc from current or adjacent result pages
bool ResList::getDoc(int docnum, Rcl::Doc &doc)
{
    if (docnum < 0)
	return false;
    // Is docnum in current page ? Then all Ok
    if (docnum >= int(m_winfirst) && 
	docnum < int(m_winfirst + m_curDocs.size())) {
	doc = m_curDocs[docnum - m_winfirst];
	goto found;
    }

    // Else we accept to page down or up but not further
    if (docnum < int(m_winfirst) && 
	docnum >= int(m_winfirst) - prefs.respagesize) {
	resultPageBack();
    } else if (docnum < 
	       int(m_winfirst + m_curDocs.size()) + prefs.respagesize) {
	resultPageNext();
    }
    if (docnum >= int(m_winfirst) && 
	docnum < int(m_winfirst + m_curDocs.size())) {
	doc = m_curDocs[docnum - m_winfirst];
	goto found;
    }
    return false;

 found:
    return true;
}

void ResList::keyPressEvent(QKeyEvent * e)
{
    if (e->key() == Qt::Key_Q && (e->state() & Qt::ControlButton)) {
	recollNeedsExit = 1;
	return;
    } else if (e->key() == Qt::Key_Prior) {
	resPageUpOrBack();
	return;
    } else if (e->key() == Qt::Key_Next) {
	resPageDownOrNext();
	return;
    }
    QTEXTBROWSER::keyPressEvent(e);
}

void ResList::contentsMouseReleaseEvent(QMouseEvent *e)
{
    m_lstClckMod = 0;
    if (e->state() & Qt::ControlButton) {
	m_lstClckMod |= Qt::ControlButton;
    } 
    if (e->state() & Qt::ShiftButton) {
	m_lstClckMod |= Qt::ShiftButton;
    }
    QTEXTBROWSER::contentsMouseReleaseEvent(e);
}

// Return total result list count
int ResList::getResCnt()
{
    if (m_docSource.getcnt() == 0)
	return -1;
    return m_docSource->getResCnt();
}


#if 1 || (QT_VERSION < 0x040000)
#define SCROLLYPOS contentsY()
#else
#define SCROLLYPOS verticalScrollBar()->value()
#endif

// Page Up/Down: we don't try to check if current paragraph is last or
// first. We just page up/down and check if viewport moved. If it did,
// fair enough, else we go to next/previous result page.
void ResList::resPageUpOrBack()
{
    int vpos = SCROLLYPOS;
    moveCursor(QTEXTBROWSER::MovePgUp, false);
    if (vpos == SCROLLYPOS)
	resultPageBack();
}

void ResList::resPageDownOrNext()
{
    int vpos = SCROLLYPOS;
    moveCursor(QTEXTBROWSER::MovePgDown, false);
    LOGDEB(("ResList::resPageDownOrNext: vpos before %d, after %d\n",
	    vpos, SCROLLYPOS));
    if (vpos == SCROLLYPOS) 
	resultPageNext();
}

// Show previous page of results. We just set the current number back
// 2 pages and show next page.
void ResList::resultPageBack()
{
    if (m_winfirst <= 0)
	return;
    m_winfirst -= 2 * prefs.respagesize;
    resultPageNext();
}

// Convert byte count into unit (KB/MB...) appropriate for display
static string displayableBytes(long size)
{
    char sizebuf[30];
    const char * unit = " B ";

    if (size > 1024 && size < 1024*1024) {
	unit = " KB ";
	size /= 1024;
    } else if (size  >= 1024*1204) {
	unit = " MB ";
	size /= (1024*1024);
    }
    sprintf(sizebuf, "%ld%s", size, unit);
    return string(sizebuf);
}

// Fill up result list window with next screen of hits
void ResList::resultPageNext()
{
    if (m_docSource.getcnt() == 0)
	return;

    int resCnt = m_docSource->getResCnt();
    m_pageParaToReldocnums.clear();

    LOGDEB(("resultPageNext: rescnt %d, winfirst %d\n", resCnt,
	    m_winfirst));

    // If we are already on the last page, nothing to do:
    if (m_winfirst >= 0 && 
	(m_winfirst + prefs.respagesize > resCnt)) {
	emit nextPageAvailable(false);
	return;
    }

    bool hasPrev = false;
    if (m_winfirst < 0) {
	m_winfirst = 0;
    } else {
	hasPrev = true;
	m_winfirst += prefs.respagesize;
    }
    emit prevPageAvailable(hasPrev);

    bool gotone = false;
    clear();

    int last = MIN(resCnt - m_winfirst, prefs.respagesize);

    m_curDocs.clear();

    // Query term colorization
    QStyleSheetItem *item = 
	new QStyleSheetItem(styleSheet(), "termtag" );
    item->setColor("blue");
    // item->setFontWeight(QFont::Bold);

    // Result paragraph format
    string sformat = string(prefs.reslistformat.utf8());
    LOGDEB(("resultPageNext: format: [%s]\n", sformat.c_str()));

    // Insert results if any in result list window. We have to send
    // the text to the widgets, because we need the paragraph number
    // each time we add a result paragraph (its diffult and
    // error-prone to compute the paragraph numbers in parallel). We
    // would like to disable updates while we're doing this, but
    // couldn't find a way to make it work, the widget seems to become
    // confused if appended while updates are disabled
    //      setUpdatesEnabled(false);
    for (int i = 0; i < last; i++) {
	string sh;
	Rcl::Doc doc;
	int percent;
	if (!m_docSource->getDoc(m_winfirst + i, doc, &percent, &sh)) {
	    // Error or end of docs, stop.
	    break;
	}
	if (percent == -1) {
	    percent = 0;
	    // Document not available, maybe other further, will go on.
	    doc.abstract = string(tr("Unavailable document").utf8());
	}

	if (i == 0) {
	    // Display list header
	    // We could use a <title> but the textedit doesnt display
	    // it prominently
	    // Note: have to append text in chunks that make sense
	    // html-wise. If we break things up to much, the editor
	    // gets confused. Hence the use of the 'chunk' text
	    // accumulator
	    QString chunk = "<qt><head></head><body><p>";

	    chunk += "<font size=+1><b>";
	    chunk += QString::fromUtf8(m_docSource->title().c_str());
	    chunk += ".</b></font>";

	    chunk += "&nbsp;&nbsp;&nbsp;";

	    chunk += tr("Documents <b>%1-%2</b> out of <b>%3</b> for ")
		.arg(m_winfirst+1)
		.arg(m_winfirst+last)
		.arg(resCnt);

	    chunk += "<a href=\"H-1\">";
	    chunk += tr("(show query)");
	    chunk += "</a>";

	    append(chunk);
	}
	   
	gotone = true;
	
	// Determine icon to display if any
	string img_name;
	if (prefs.showicons) {
	    string iconpath;
	    string iconname = rclconfig->getMimeIconName(doc.mimetype,
							 &iconpath);
	    LOGDEB1(("Img file; %s\n", iconpath.c_str()));
	    QImage image(iconpath.c_str());
	    if (!image.isNull()) {
		img_name = string("img_") + iconname;
		QMimeSourceFactory::defaultFactory()->
		    setImage(img_name.c_str(), image);
	    }
	}

	// Percentage of 'relevance'
	char perbuf[10];
	sprintf(perbuf, "%3d%% ", percent);

	// Printable url: either utf-8 if transcoding succeeds, or url-encoded
	string url;
	printableUrl(doc.url, url);

	// Make title out of file name if none yet
	if (doc.title.empty()) {
	    doc.title = path_getsimple(url);
	}

	// Result number
	char numbuf[20];
	int docnumforlinks = m_winfirst+1+i;
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

	// Abstract. The docsequence should deal with this as we don't
	// know if a query is open or if we're displaying
	// history. OTOH, if the docsequence does it, we're going to
	// generate a lot of unneeded abstracts for sorted sequences
	// (for all the queried for but undisplayed entries)
	string richabst;
	string abstract;
	LOGDEB2(("Abstract: clcnt %d prfs.build %d syntabs %d prfs.repl %d\n",
		m_searchData->clauseCount(), prefs.queryBuildAbstract,
		doc.syntabs, prefs.queryReplaceAbstract));
	if (m_searchData->clauseCount() > 0 && prefs.queryBuildAbstract && 
	    (doc.syntabs || prefs.queryReplaceAbstract)) {
	    rcldb->makeDocAbstract(doc, abstract);
	    if (abstract.empty())
		abstract = doc.abstract;
	} else {
	    abstract = doc.abstract;
	}
	plaintorich(abstract, richabst, m_searchData, true);


	// Links;
	string linksbuf;
	char vlbuf[100];
	if (canIntern(doc.mimetype, rclconfig)) { 
	    sprintf(vlbuf, "\"P%d\"", docnumforlinks);
	    linksbuf += string("<a href=") + vlbuf + ">" + "Preview" + "</a>" 
		+ "&nbsp;&nbsp;";
	}
	if (!rclconfig->getMimeViewerDef(doc.mimetype).empty()) {
	    sprintf(vlbuf, "E%d", docnumforlinks);
	    linksbuf += string("<a href=") + vlbuf + ">" + "Edit" + "</a>";
	}

	// Concatenate chunks to build the result list paragraph:
	string result;

	// Subheader: this is used by history
	if (!sh.empty())
	    result += string("<p><b>") + sh + "</p>\n<p>";
	else
	    result += "<p>";

	if (!img_name.empty()) {
	    result += "<img source=\"" + img_name + "\" align=\"left\">";
	}

	// Configurable stuff
	map<char,string> subs;
	subs['A'] = !richabst.empty() ? richabst + "<br>" : "";
	subs['D'] = datebuf;
	subs['K'] = !doc.keywords.empty() ? doc.keywords + "<br>" : "";
	subs['L'] = linksbuf;
	subs['N'] = numbuf;
	subs['M'] = doc.mimetype;
	subs['R'] = perbuf;
	subs['S'] = sizebuf;
	subs['T'] = doc.title;
	subs['U'] = url;

	string formatted;
	pcSubst(sformat, formatted, subs);
	result += formatted;

	result += "</p>\n";

	LOGDEB2(("Chunk: [%s]\n", result.c_str()));
	QString str = QString::fromUtf8(result.c_str(), result.length());
	append(str);
	setCursorPosition(0,0);
	ensureCursorVisible();

        m_pageParaToReldocnums[paragraphs()-1] = i;
	m_curDocs.push_back(doc);
    }

    bool hasNext = false;
    if (m_winfirst >= 0 && m_winfirst + prefs.respagesize < resCnt) {
	hasNext = true;
    }

    if (gotone) {
	QString chunk = "<p align=\"center\">";
	if (hasPrev || hasNext) {
	    if (hasPrev) {
		chunk += "<a href=\"p-1\"><b>";
		chunk += tr("Previous");
		chunk += "</b></a>&nbsp;&nbsp;&nbsp;";
	    }
	    if (hasNext) {
		chunk += "<a href=\"n-1\"><b>";
		chunk += tr("Next");
		chunk += "</b></a>";
	    }
	    chunk += "</p>\n";
	    append(chunk);
	}
	append("</body></qt>");
	ensureCursorVisible();
    } else {
	// Restore first in win parameter that we shouln't have incremented
	QString chunk = "<p><font size=+1><b>";
	chunk += QString::fromUtf8(m_docSource->title().c_str());
	chunk += "</b></font><br>";
	chunk += "<a href=\"H-1\">";
	chunk += tr("Show query details");
	chunk += "</a><br>";
	append(chunk);
	append(tr("<p><b>No results found</b><br>"));
	m_winfirst -= prefs.respagesize;
	if (m_winfirst < 0)
	    m_winfirst = -1;
	hasNext = false;
    }
    // Possibly color paragraph of current preview if any
    previewExposed(m_curPvDoc);
    emit nextPageAvailable(hasNext);
}

// Single click in result list use this for document selection, if no
// text selection active:
void ResList::clicked(int par, int)
{
    LOGDEB2(("ResList::clicked\n"));

    // It's very ennoying, textBrowser always has selected text. This
    // is bound to change with qt releases!
    if (hasSelectedText()&& selectedText().compare("<!--StartFragment-->")) {
	// Give priority to text selection, do nothing
	LOGDEB2(("%s\n", (const char *)(selectedText().ascii())));
	return;
    }
    LOGDEB(("click at par %d (with %s %s)\n", par, 
	    (m_lstClckMod & Qt::ControlButton) ? "Ctrl" : "",
	    (m_lstClckMod & Qt::ShiftButton) ? "Shft" : ""));

}

// Color paragraph (if any) of currently visible preview
void ResList::previewExposed(int docnum)
{
    LOGDEB(("ResList::previewExposed: doc %d\n", docnum));

    // Possibly erase old one to white
    int par;
    if (m_curPvDoc != -1 && (par = parnumfromdocnum(m_curPvDoc)) != -1) {
	QColor color("white");
	setParagraphBackgroundColor(par, color);
	m_curPvDoc = -1;
    }
    m_curPvDoc = docnum;
    par = parnumfromdocnum(docnum);
    // Maybe docnum is -1 or not in this window, 
    if (par < 0)
	return;

    setCursorPosition(par, 1);
    ensureCursorVisible();

    // Color the new active paragraph
    QColor color("LightBlue");
    setParagraphBackgroundColor(par, color);
}

// Double click in res list: add selection to simple search
void ResList::doubleClicked(int, int)
{
    if (hasSelectedText())
	emit(wordSelect(selectedText()));
}

void ResList::linkWasClicked(const QString &s, int clkmod)
{
    LOGDEB(("ResList::linkClicked: [%s]\n", s.ascii()));
    int i = atoi(s.ascii()+1) -1;
    int what = s.ascii()[0];
    switch (what) {
    case 'H': 
	emit headerClicked(); 
	break;
    case 'P': 
	emit docPreviewClicked(i, clkmod); 
	break;
    case 'E': 
	emit docEditClicked(i);
	break;
    case 'n':
	resultPageNext();
	break;
    case 'p':
	resultPageBack();
	break;
    default: break;// ?? 
    }
}

RCLPOPUP *ResList::createPopupMenu(const QPoint& pos)
{
    int para = paragraphAt(pos);
    clicked(para, 0);
    m_popDoc = docnumfromparnum(para);
    RCLPOPUP *popup = new RCLPOPUP(this);
    popup->insertItem(tr("&Preview"), this, SLOT(menuPreview()));
    popup->insertItem(tr("&Edit"), this, SLOT(menuEdit()));
    popup->insertItem(tr("Copy &File Name"), this, SLOT(menuCopyFN()));
    popup->insertItem(tr("Copy &Url"), this, SLOT(menuCopyURL()));
    popup->insertItem(tr("Find &similar documents"), this, SLOT(menuExpand()));
    popup->insertItem(tr("P&arent document/folder"), 
		      this, SLOT(menuSeeParent()));
    return popup;
}

void ResList::menuPreview()
{
    emit docPreviewClicked(m_popDoc, 0);
}

void ResList::menuSeeParent()
{
    Rcl::Doc doc;
    if (getDoc(m_popDoc, doc)) {
	Rcl::Doc doc1;
	if (doc.ipath.empty()) {
	    // No parent doc: show enclosing folder with app configured for
	    // directories
	    doc1.url = path_getfather(doc.url);
	    doc1.mimetype = "application/x-fsdirectory";
	    emit editRequested(doc1);
	} else {
	    doc1.url = doc.url;
	    doc1.ipath = doc.ipath;
	    string::size_type colon;
	    LOGDEB(("Ipath: [%s]\n", doc1.ipath.c_str()));
	    if ((colon=doc1.ipath.find_last_of(":")) != string::npos) {
		doc1.ipath.erase(colon);
	    } else {
		doc1.ipath.erase();
	    }
	    LOGDEB(("Ipath after: [%s]\n", doc1.ipath.c_str()));

	    list<string> lipath;
	    stringToTokens(doc.ipath, lipath, ":");
	    if (lipath.size() >= 1)
		lipath.pop_back();
	    emit previewRequested(doc1);
	}
    }
}

void ResList::menuEdit()
{
    emit docEditClicked(m_popDoc);
}
void ResList::menuCopyFN()
{
    Rcl::Doc doc;
    if (getDoc(m_popDoc, doc)) {
	// Our urls currently always begin with "file://"
	QApplication::clipboard()->setText(doc.url.c_str()+7, 
					   QClipboard::Selection);
    }
}
void ResList::menuCopyURL()
{
    Rcl::Doc doc;
    if (getDoc(m_popDoc, doc)) {
	string url =  url_encode(doc.url, 7);
	QApplication::clipboard()->setText(url.c_str(), 
					   QClipboard::Selection);
    }
}
void ResList::menuExpand()
{
    Rcl::Doc doc;
    if (rcldb && getDoc(m_popDoc, doc)) {
	emit docExpand(m_popDoc);
    }
}

QString ResList::getDescription()
{
    return QString::fromUtf8(m_searchData->getDescription().c_str());
}

/** Show detailed expansion of a query */
void ResList::showQueryDetails()
{
    // Break query into lines of reasonable length, avoid cutting words,
    // Also limit the total number of lines. 
    const unsigned int ll = 100;
    const unsigned int maxlines = 50;
    string query = m_searchData->getDescription();
    string oq;
    unsigned int nlines = 0;
    while (query.length() > 0) {
	string ss = query.substr(0, ll);
	if (ss.length() == ll) {
	    string::size_type pos = ss.find_last_of(" ");
	    if (pos == string::npos) {
		pos = query.find_first_of(" ");
		if (pos != string::npos)
		    ss = query.substr(0, pos+1);
		else 
		    ss = query;
	    } else {
		ss = ss.substr(0, pos+1);
	    }
	}
	// This cant happen, but anyway. Be very sure to avoid an infinite loop
	if (ss.length() == 0) {
	    LOGDEB(("showQueryDetails: Internal error!\n"));
	    oq = query;
	    break;
	}
	oq += ss + "\n";
	if (nlines++ >= maxlines) {
	    oq += " ... \n";
	    break;
	}
	query= query.substr(ss.length());
	LOGDEB1(("oq [%s]\n, query [%s]\n, ss [%s]\n",
		oq.c_str(), query.c_str(), ss.c_str()));
    }

    QString desc = tr("Query details") + ": " + 
	QString::fromUtf8(oq.c_str());
    QMessageBox::information(this, tr("Query details"), desc);
}
