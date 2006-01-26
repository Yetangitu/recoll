/****************************************************************************
** ui.h extension file, included from the uic-generated form implementation.
**
** If you want to add, delete, or rename functions or slots, use
** Qt Designer to update this file, preserving your code.
**
** You should not define a constructor or destructor in this file.
** Instead, write your code in functions called init() and destroy().
** These will automatically be called by the form's constructor and
** destructor.
*****************************************************************************/

#include "qfontdialog.h"
#include "qspinbox.h"
#include "qmessagebox.h"

#include "recoll.h"
#include "guiutils.h"

void UIPrefsDialog::init()
{
    // Entries per result page spinbox
    pageLenSB->setValue(prefs.respagesize);
    // Show icons checkbox
    useIconsCB->setChecked(prefs.showicons);
    // Result list font family and size
    reslistFontFamily = prefs.reslistfontfamily;
    reslistFontSize = prefs.reslistfontsize;
    QString s;
    if (prefs.reslistfontfamily.length() == 0) {
	reslistFontPB->setText(this->font().family() + "-" +
			       s.setNum(this->font().pointSize()));
    } else {
	reslistFontPB->setText(reslistFontFamily + "-" +
			       s.setNum(reslistFontSize));
    }
    // Stemming language combobox
    stemLangCMB->insertItem("(no stemming)");
    list<string> langs;
    string reason;
    if (!maybeOpenDb(reason)) {
	QMessageBox::critical(0, "Recoll", QString(reason.c_str()));
	exit(1);
    }
    langs = rcldb->getStemLangs();

    int i = 0, cur = -1;
    for (list<string>::const_iterator it = langs.begin(); 
	 it != langs.end(); it++) {
	stemLangCMB->
	    insertItem(QString::fromAscii(it->c_str(), it->length()));
	i++;
	if (cur == -1) {
	    if (!strcmp(prefs.queryStemLang.ascii(), it->c_str()))
		cur = i;
	}
    }
    if (cur < 0)
	cur = 0;
    stemLangCMB->setCurrentItem(cur);

    buildAbsCB->setDown(prefs.queryBuildAbstract);
    if (!prefs.queryBuildAbstract) {
	replAbsCB->setEnabled(false);
    }
    replAbsCB->setDown(prefs.queryReplaceAbstract);
    
    connect(reslistFontPB, SIGNAL(clicked()), this, SLOT(showFontDialog()));
    connect(resetFontPB, SIGNAL(clicked()), this, SLOT(resetReslistFont()));
}

void UIPrefsDialog::accept()
{
    prefs.showicons = useIconsCB->isChecked();
    prefs.respagesize = pageLenSB->value();

    prefs.reslistfontfamily = reslistFontFamily;
    prefs.reslistfontsize = reslistFontSize;

    if (stemLangCMB->currentItem() == 0) {
	prefs.queryStemLang = "";
    } else {
	prefs.queryStemLang = stemLangCMB->currentText();
    }
    prefs.queryBuildAbstract = buildAbsCB->isChecked();
    prefs.queryReplaceAbstract = buildAbsCB->isChecked() && 
	replAbsCB->isChecked();
    rwSettings(true);
    string reason;
    maybeOpenDb(reason, true);
    emit uiprefsDone();
    QDialog::accept();
}

void UIPrefsDialog::showFontDialog()
{
    bool ok;
    QFont font;
    if (prefs.reslistfontfamily.length()) {
	font.setFamily(prefs.reslistfontfamily);
	font.setPointSize(prefs.reslistfontsize);
    }

    font = QFontDialog::getFont(&ok, font, this );
    if (ok) {
	// Check if the default font was set, in which case we
	// erase the preference
	if (font.family().compare(this->font().family()) || 
	    font.pointSize() != this->font().pointSize()) {
	    reslistFontFamily = font.family();
	    reslistFontSize = font.pointSize();
	    QString s;
	    reslistFontPB->setText(reslistFontFamily + "-" +
			       s.setNum(reslistFontSize));
	} else {
	    reslistFontFamily = "";
	    reslistFontSize = 0;
	}
    }
}


void UIPrefsDialog::resetReslistFont()
{
    reslistFontFamily = "";
    reslistFontSize = 0;
    reslistFontPB->setText(this->font().family() + "-" +
			   QString().setNum(this->font().pointSize()));
}
