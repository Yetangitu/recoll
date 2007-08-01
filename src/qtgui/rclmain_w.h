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
#ifndef RCLMAIN_W_H
#define RCLMAIN_W_H

#include <qvariant.h>
#include <qmainwindow.h>
#include "sortseq.h"
#include "preview_w.h"
#include "recoll.h"
#include "advsearch_w.h"
#include "sort_w.h"
#include "uiprefs_w.h"
#include "rcldb.h"
#include "searchdata.h"
#include "spell_w.h"
#include "refcntr.h"
#include "pathut.h"

#if QT_VERSION < 0x040000
#include "rclmain.h"
#else
#include "ui_rclmain.h"
#endif

//MOC_SKIP_BEGIN
#if QT_VERSION < 0x040000
class DummyRclMainBase : public RclMainBase
{
 public: DummyRclMainBase(QWidget* parent = 0) : RclMainBase(parent) {}
};
#define RCLMAINPARENT QWidget
#else
class DummyRclMainBase : public Q3MainWindow, public Ui::RclMainBase
{
public: DummyRclMainBase(QWidget *parent) :Q3MainWindow(parent){setupUi(this);}
#define RCLMAINPARENT Q3MainWindow
};
#endif
//MOC_SKIP_END

class Preview;

class RclMain : public DummyRclMainBase
{
    Q_OBJECT

public:
    RclMain(RCLMAINPARENT * parent = 0) 
	: DummyRclMainBase(parent) 
    {
	init();
    }
    ~RclMain() {}

public slots:
    virtual bool close();
    virtual void fileExit();
    virtual void periodic100();
    virtual void startIndexing();
    virtual void startSearch(RefCntr<Rcl::SearchData> sdata);
    virtual void setDocSequence();
    virtual void previewClosed(Preview *w);
    virtual void showAdvSearchDialog();
    virtual void showSortDialog();
    virtual void showSpellDialog();
    virtual void showAboutDialog();
    virtual void startManual();
    virtual void showDocHistory();
    virtual void showExtIdxDialog();
    virtual void sortDataChanged(DocSeqSortSpec spec);
    virtual void showUIPrefs();
    virtual void setUIPrefs();
    virtual void enableNextPage(bool);
    virtual void enablePrevPage(bool);
    virtual void docExpand(int);
    virtual void ssearchAddTerm(QString);
    virtual void startPreview(int docnum, int);
    virtual void startPreview(Rcl::Doc doc);
    virtual void startNativeViewer(int docnum);
    virtual void startNativeViewer(Rcl::Doc doc);
    virtual void previewNextInTab(Preview *, int sid, int docnum);
    virtual void previewPrevInTab(Preview *, int sid, int docnum);
    virtual void previewExposed(Preview *, int sid, int docnum);
    virtual void resetSearch();
    virtual void eraseDocHistory();
    // Callback for setting the stemming language through the prefs menu
    virtual void setStemLang(int id);
    // Prefs menu about to show, set the checked lang entry
    virtual void adjustPrefsMenu();

signals:
    void stemLangChanged(const QString& lang);

protected:
    virtual void closeEvent( QCloseEvent * );

private:
    Preview *curPreview;
    AdvSearch *asearchform;
    SortForm *sortform;
    UIPrefsDialog *uiprefs;
    SpellW *spellform;

    RefCntr<Rcl::SearchData> m_searchData;
    DocSeqSortSpec           m_sortspecs;
    RefCntr<DocSequence>     m_docSource;
    
    vector<TempFile>         m_tempfiles;
    // Serial number of current search for this process.
    // Used to match to preview windows
    int                      m_searchId; 
    map<QString, int>        m_stemLangToId;
    int                      m_idNoStem;

    virtual void init();
    virtual void previewPrevOrNextInTab(Preview *, int sid, int docnum, 
					bool next);
    virtual void setStemLang(const QString& lang);
};

#endif // RCLMAIN_W_H
