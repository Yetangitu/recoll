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
class DummyRclMainBase : public QMainWindow, public Ui::RclMainBase
{
public: DummyRclMainBase(Q3MainWindow*parent) {setupUi(parent);}
#define RCLMAINPARENT Q3MainWindow
};
#endif
//MOC_SKIP_END

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

    virtual bool close( bool );

public slots:
    virtual void fileExit();
    virtual void periodic100();
    virtual void startIndexing();
    virtual void startAdvSearch(RefCntr<Rcl::SearchData> sdata);
    virtual void previewClosed(QWidget * w);
    virtual void showAdvSearchDialog();
    virtual void showSortDialog();
    virtual void showSpellDialog();
    virtual void showAboutDialog();
    virtual void startManual();
    virtual void showDocHistory();
    virtual void sortDataChanged(DocSeqSortSpec spec);
    virtual void showUIPrefs();
    virtual void setUIPrefs();
    virtual void enableNextPage(bool);
    virtual void enablePrevPage(bool);
    virtual void docExpand(int);
    virtual void ssearchAddTerm(QString);
    virtual void startPreview(int docnum, int);
    virtual void startNativeViewer(int docnum);
    virtual void previewNextInTab(int sid, int docnum);
    virtual void previewPrevInTab(int sid, int docnum);
    virtual void previewExposed(int sid, int docnum);

private:
    Preview *curPreview;
    AdvSearch *asearchform;
    SortForm *sortform;
    UIPrefsDialog *uiprefs;
    SpellW *spellform;

    DocSeqSortSpec sortspecs;
    int m_searchId; // Serial number of current search for this process.
                  // Used to match to preview windows
    virtual void init();
};

#endif // RCLMAIN_W_H
