#ifndef lint
static char rcsid[] = "@(#$Id: rclmain.cpp,v 1.16 2006-04-01 08:07:43 dockes Exp $ (C) 2005 J.F.Dockes";
#endif
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

#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <regex.h>

#include <utility>
#ifndef NO_NAMESPACES
using std::pair;
#endif /* NO_NAMESPACES */

#include <qapplication.h>
#include <qmessagebox.h>
#include <qcstring.h>
#include <qtabwidget.h>
#include <qtimer.h>
#include <qstatusbar.h>
#include <qwindowdefs.h>
#include <qcheckbox.h>
#include <qfontdialog.h>
#include <qspinbox.h>
#include <qcombobox.h>
#include <qtextbrowser.h>
#include <qlineedit.h>
#include <qaction.h>
#include <qpushbutton.h>
#include <qimage.h>
#include <qiconset.h>

#include "recoll.h"
#include "debuglog.h"
#include "mimehandler.h"
#include "pathut.h"
#include "smallut.h"
#include "advsearch.h"
#include "rclversion.h"
#include "sortseq.h"
#include "uiprefs.h"
#include "guiutils.h"
#include "rclreslist.h"
#include "transcode.h"

#include "rclmain.h"
#include "moc_rclmain.cpp"

extern "C" int XFlush(void *);

// Taken from qt designer. Don't know why it's needed.
static QIconSet createIconSet( const QString &name )
{
    QIconSet ic( QPixmap::fromMimeSource( name ) );
    QString iname = "d_" + name;
    ic.setPixmap(QPixmap::fromMimeSource(iname),
		 QIconSet::Small, QIconSet::Disabled );
    return ic;
}

void RclMain::init()
{
    curPreview = 0;
    asearchform = 0;
    sortform = 0;
    sortwidth = 0;
    uiprefs = 0;

    // Set the focus to the search terms entry:
    sSearch->queryText->setFocus();

    // Set result list font according to user preferences.
    if (prefs.reslistfontfamily.length()) {
	QFont nfont(prefs.reslistfontfamily, prefs.reslistfontsize);
	resList->setFont(nfont);
    }
    string historyfile = path_cat(rclconfig->getConfDir(), "history");
    m_history = new RclDHistory(historyfile);
    connect(sSearch, SIGNAL(startSearch(Rcl::AdvSearchData)), 
		this, SLOT(startAdvSearch(Rcl::AdvSearchData)));

    nextPageAction->setIconSet(createIconSet("nextpage.png"));
    prevPageAction->setIconSet(createIconSet("prevpage.png"));
}

// We also want to get rid of the advanced search form and previews
// when we exit (not our children so that it's not systematically
// created over the main form). 
bool RclMain::close(bool)
{
    fileExit();
    return false;
}

//#define SHOWEVENTS
#if defined(SHOWEVENTS)
static const char *eventTypeToStr(int tp)
{
    switch (tp) {
    case 0: return "None";
    case 1: return "Timer";
    case 2: return "MouseButtonPress";
    case 3: return "MouseButtonRelease";
    case 4: return "MouseButtonDblClick";
    case 5: return "MouseMove";
    case 6: return "KeyPress";
    case 7: return "KeyRelease";
    case 8: return "FocusIn";
    case 9: return "FocusOut";
    case 10: return "Enter";
    case 11: return "Leave";
    case 12: return "Paint";
    case 13: return "Move";
    case 14: return "Resize";
    case 15: return "Create";
    case 16: return "Destroy";
    case 17: return "Show";
    case 18: return "Hide";
    case 19: return "Close";
    case 20: return "Quit";
    case 21: return "Reparent";
    case 22: return "ShowMinimized";
    case 23: return "ShowNormal";
    case 24: return "WindowActivate";
    case 25: return "WindowDeactivate";
    case 26: return "ShowToParent";
    case 27: return "HideToParent";
    case 28: return "ShowMaximized";
    case 29: return "ShowFullScreen";
    case 30: return "Accel";
    case 31: return "Wheel";
    case 32: return "AccelAvailable";
    case 33: return "CaptionChange";
    case 34: return "IconChange";
    case 35: return "ParentFontChange";
    case 36: return "ApplicationFontChange";
    case 37: return "ParentPaletteChange";
    case 38: return "ApplicationPaletteChange";
    case 39: return "PaletteChange";
    case 40: return "Clipboard";
    case 42: return "Speech";
    case 50: return "SockAct";
    case 51: return "AccelOverride";
    case 52: return "DeferredDelete";
    case 60: return "DragEnter";
    case 61: return "DragMove";
    case 62: return "DragLeave";
    case 63: return "Drop";
    case 64: return "DragResponse";
    case 70: return "ChildInserted";
    case 71: return "ChildRemoved";
    case 72: return "LayoutHint";
    case 73: return "ShowWindowRequest";
    case 74: return "WindowBlocked";
    case 75: return "WindowUnblocked";
    case 80: return "ActivateControl";
    case 81: return "DeactivateControl";
    case 82: return "ContextMenu";
    case 83: return "IMStart";
    case 84: return "IMCompose";
    case 85: return "IMEnd";
    case 86: return "Accessibility";
    case 87: return "TabletMove";
    case 88: return "LocaleChange";
    case 89: return "LanguageChange";
    case 90: return "LayoutDirectionChange";
    case 91: return "Style";
    case 92: return "TabletPress";
    case 93: return "TabletRelease";
    case 94: return "OkRequest";
    case 95: return "HelpRequest";
    case 96: return "WindowStateChange";
    case 97: return "IconDrag";
    case 1000: return "User";
    case 65535: return "MaxUser";
    default: return "Unknown";
    }
}
#endif

void RclMain::fileExit()
{
    LOGDEB1(("RclMain: fileExit\n"));
    if (asearchform)
	delete asearchform;
    // Let the exit handler clean up things
    exit(0);
}

// This is called on a 100ms timer checks the status of the indexing
// thread and a possible need to exit
void RclMain::periodic100()
{
    static int toggle = 0;
    // Check if indexing thread done
    if (indexingstatus) {
	statusBar()->message("");
	indexingstatus = false;
	// Make sure we reopen the db to get the results.
	LOGINFO(("Indexing done: closing query database\n"));
	rcldb->close();
    } else if (indexingdone == 0) {
	if (toggle == 0) {
	    QString msg = tr("Indexing in progress: ");
	    string cf = idxthread_currentfile();
	    string mf;int ecnt = 0;
	    string fcharset = rclconfig->getDefCharset(true);
	    if (!transcode(cf, mf, fcharset, "UTF-8", &ecnt) || ecnt) {
		mf = url_encode(cf, 0);
	    }
	    msg += QString::fromUtf8(mf.c_str());
	    statusBar()->message(msg);
	} else if (toggle == 9) {
	    statusBar()->message("");
	}
	if (++toggle >= 10)
	    toggle = 0;
    }
    if (recollNeedsExit)
	fileExit();
}

void RclMain::fileStart_IndexingAction_activated()
{
    if (indexingdone == 1)
	startindexing = 1;
}

// Note that all our 'urls' are like : file://...
static string urltolocalpath(string url)
{
    return url.substr(7, string::npos);
}

// Execute an advanced search query. The parameters normally come from
// the advanced search dialog
void RclMain::startAdvSearch(Rcl::AdvSearchData sdata)
{
    LOGDEB(("RclMain::startAdvSearch\n"));
    // The db may have been closed at the end of indexing
    string reason;
    if (!maybeOpenDb(reason)) {
	QMessageBox::critical(0, "Recoll", QString(reason.c_str()));
	exit(1);
    }

    resList->resetSearch();

    if (!rcldb->setQuery(sdata, prefs.queryStemLang.length() > 0 ? 
			 Rcl::Db::QO_STEM : Rcl::Db::QO_NONE, 
			 prefs.queryStemLang.ascii()))
	return;
    curPreview = 0;

    DocSequence *docsource;
    if (sortwidth > 0) {
	DocSequenceDb myseq(rcldb, string(tr("Query results").utf8()));
	docsource = new DocSeqSorted(myseq, sortwidth, sortspecs,
				     string(tr("Query results (sorted)").utf8()));
    } else {
	docsource = new DocSequenceDb(rcldb, string(tr("Query results").utf8()));
    }
    currentQueryData = sdata;
    resList->setDocSource(docsource);
}

// Open advanced search dialog.
void RclMain::showAdvSearchDialog()
{
    if (asearchform == 0) {
	asearchform = new advsearch(0, tr("Advanced search"), FALSE,
				    WStyle_Customize | WStyle_NormalBorder | 
				    WStyle_Title | WStyle_SysMenu);
	asearchform->setSizeGripEnabled(FALSE);
	connect(asearchform, SIGNAL(startSearch(Rcl::AdvSearchData)), 
		this, SLOT(startAdvSearch(Rcl::AdvSearchData)));
	asearchform->show();
    } else {
	// Close and reopen, in hope that makes us visible...
	asearchform->close();
	asearchform->show();
    }
}

void RclMain::showSortDialog()
{
    if (sortform == 0) {
	sortform = new SortForm(0, tr("Sort criteria"), FALSE,
				    WStyle_Customize | WStyle_NormalBorder | 
				    WStyle_Title | WStyle_SysMenu);
	sortform->setSizeGripEnabled(FALSE);
	connect(sortform, SIGNAL(sortDataChanged(int, RclSortSpec)), 
		this, SLOT(sortDataChanged(int, RclSortSpec)));
	sortform->show();
    } else {
	// Close and reopen, in hope that makes us visible...
	sortform->close();
        sortform->show();
    }

}

void RclMain::showUIPrefs()
{
    if (uiprefs == 0) {
	uiprefs = new UIPrefsDialog(0, tr("User interface preferences"), FALSE,
				    WStyle_Customize | WStyle_NormalBorder | 
				    WStyle_Title | WStyle_SysMenu);
	uiprefs->setSizeGripEnabled(FALSE);
	connect(uiprefs, SIGNAL(uiprefsDone()), this, SLOT(setUIPrefs()));
	uiprefs->show();
    } else {
	// Close and reopen, in hope that makes us visible...
	uiprefs->close();
        uiprefs->show();
    }
}

// If a preview (toplevel) window gets closed by the user, we need to
// clean up because there is no way to reopen it. And check the case
// where the current one is closed
void RclMain::previewClosed(QWidget *w)
{
    if (w == (QWidget *)curPreview) {
	LOGDEB(("Active preview closed\n"));
	curPreview = 0;
    } else {
	LOGDEB(("Old preview closed\n"));
    }
    delete w;
}

/** 
 * Open a preview window for a given document, or load it into new tab of 
 * existing window.
 *
 * @param docnum db query index
 */
void RclMain::startPreview(int docnum)
{
    Rcl::Doc doc;
    if (!resList->getDoc(docnum, doc)) {
	QMessageBox::warning(0, "Recoll",
			     tr("Cannot retrieve document info" 
				     " from database"));
	return;
    }
	
    // Check file exists in file system
    string fn = urltolocalpath(doc.url);
    struct stat st;
    if (stat(fn.c_str(), &st) < 0) {
	QMessageBox::warning(0, "Recoll", tr("Cannot access document file: ") +
			     fn.c_str());
	return;
    }

    if (curPreview == 0) {
	curPreview = new Preview(0, tr("Preview"));
	if (curPreview == 0) {
	    QMessageBox::warning(0, tr("Warning"), 
				 tr("Can't create preview window"),
				 QMessageBox::Ok, 
				 QMessageBox::NoButton);
	    return;
	}

	curPreview->setCaption(QString::fromUtf8(currentQueryData.description.c_str()));
	connect(curPreview, SIGNAL(previewClosed(QWidget *)), 
		this, SLOT(previewClosed(QWidget *)));
	curPreview->show();
    } else {
	if (curPreview->makeDocCurrent(fn, doc)) {
	    // Already there
	    return;
	}
	(void)curPreview->addEditorTab();
    }
    m_history->enterDocument(fn, doc.ipath);
    if (!curPreview->loadFileInCurrentTab(fn, st.st_size, doc))
	curPreview->closeCurrentTab();
}

void RclMain::startNativeViewer(int docnum)
{
    Rcl::Doc doc;
    if (!resList->getDoc(docnum, doc)) {
	QMessageBox::warning(0, "Recoll",
			     tr("Cannot retrieve document info" 
				" from database"));
	return;
    }
    
    // Look for appropriate viewer
    string cmd = rclconfig->getMimeViewerDef(doc.mimetype);
    if (cmd.length() == 0) {
	QMessageBox::warning(0, "Recoll", 
			     tr("No external viewer configured for mime type ")
			     + doc.mimetype.c_str());
	return;
    }

    string fn = urltolocalpath(doc.url);
    string url = url_encode(doc.url, 7);

    // Substitute %u (url) and %f (file name) inside prototype command
    string ncmd;
    string::const_iterator it1;
    for (it1 = cmd.begin(); it1 != cmd.end();it1++) {
	if (*it1 == '%') {
	    if (++it1 == cmd.end()) {
		ncmd += '%';
		break;
	    }
	    if (*it1 == '%')
		ncmd += '%';
	    if (*it1 == 'u')
		ncmd += "'" + url + "'";
	    if (*it1 == 'f')
		ncmd += "'" + fn + "'";
	} else {
	    ncmd += *it1;
	}
    }

    ncmd += " &";
    QStatusBar *stb = statusBar();
    if (stb) {
	string fcharset = rclconfig->getDefCharset(true);
	string prcmd;
	transcode(ncmd, prcmd, fcharset, "UTF-8");
	QString msg = tr("Executing: [") + 
	    QString::fromUtf8(prcmd.c_str()) + "]";
	stb->message(msg, 5000);
    }
    m_history->enterDocument(fn, doc.ipath);
    system(ncmd.c_str());
}


void RclMain::showAboutDialog()
{
    string vstring = string("Recoll ") + rclversion + 
	"<br>" + "http://www.recoll.org";
    QMessageBox::information(this, tr("About Recoll"), vstring.c_str());
}

void RclMain::startManual()
{
    QString msg = tr("Starting help browser ");
    if (prefs.htmlBrowser != QString(""))
	msg += prefs.htmlBrowser;
    statusBar()->message(msg, 3000);
    startHelpBrowser();
}

void RclMain::showDocHistory()
{
    LOGDEB(("RclMain::showDocHistory\n"));
    resList->resetSearch();
    curPreview = 0;

    DocSequence *docsource;
    if (sortwidth > 0) {
	DocSequenceHistory myseq(rcldb, m_history, tr("Document history"));
	docsource = new DocSeqSorted(myseq, sortwidth, sortspecs,
				     string(tr("Document history (sorted)").utf8()));
    } else {
	docsource = new DocSequenceHistory(rcldb, m_history, 
					   string(tr("Document history").utf8()));
    }
    currentQueryData.erase();
    currentQueryData.description = tr("History data").utf8();
    resList->setDocSource(docsource);
}


void RclMain::sortDataChanged(int cnt, RclSortSpec spec)
{
    LOGDEB(("RclMain::sortDataChanged\n"));
    sortwidth = cnt;
    sortspecs = spec;
}

// Called when the uiprefs dialog is ok'd
void RclMain::setUIPrefs()
{
    if (!uiprefs)
	return;
    LOGDEB(("Recollmain::setUIPrefs\n"));
    if (prefs.reslistfontfamily.length()) {
	QFont nfont(prefs.reslistfontfamily, prefs.reslistfontsize);
	resList->setFont(nfont);
    } else {
	resList->setFont(this->font());
    }
}

void RclMain::enableNextPage(bool yesno)
{
    nextPageAction->setEnabled(yesno);
}

void RclMain::enablePrevPage(bool yesno)
{
    prevPageAction->setEnabled(yesno);
}

/** Show detailed expansion of a query */
void RclMain::showQueryDetails()
{
    // Break query into lines of reasonable length, avoid cutting words!
    const unsigned int ll = 80;
    string query = currentQueryData.description;
    string oq;
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
	query= query.substr(ss.length());
	LOGDEB1(("oq [%s]\n, query [%s]\n, ss [%s]\n",
		oq.c_str(), query.c_str(), ss.c_str()));
    }

    QString desc = tr("Query details") + ": " + 
	QString::fromUtf8(oq.c_str());
    QMessageBox::information(this, tr("Query details"), desc);
}
