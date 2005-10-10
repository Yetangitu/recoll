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

void Preview::init()
{
    connect(pvTab, SIGNAL(currentChanged(QWidget *)), 
	    this, SLOT(currentChanged(QWidget *)));
    searchTextLine->installEventFilter(this);
    dynSearchActive = false;
    canBeep = true;
}

bool Preview::eventFilter(QObject *target, QEvent *event)
{
    if (event->type() != QEvent::KeyPress) 
	return QWidget::eventFilter(target, event);
    
    fprintf(stderr, "Preview::eventFilter: keyEvent\n");
    QKeyEvent *keyEvent = (QKeyEvent *)event;
    if (dynSearchActive) {
	if (keyEvent->key() == Key_F3) {
	    doSearch(true, false);
	    return true;
	}
	if (target != searchTextLine)
	    return QApplication::sendEvent(searchTextLine, event);
    } else {
	QWidget *tw = pvTab->currentPage();
	QWidget *e = 0;
	if (tw)
	    e = (QTextEdit *)tw->child("pvEdit");
	fprintf(stderr, "Widget: %p, edit %p, target %p\n", tw, e, target);
	if (e && target == tw && keyEvent->key() == Key_Slash) {
	    dynSearchActive = true;
	    return true;
	}
    }

    return QWidget::eventFilter(target, event);
}

void Preview::searchTextLine_textChanged(const QString & text)
{
    fprintf(stderr, "search line text changed. text: '%s'\n", text.ascii());
    if (text.isEmpty()) {
	dynSearchActive = false;
    } else {
	dynSearchActive = true;
	doSearch(false, false);
    }
}


// Perform text search. If next is true, we look for the next match of the
// current search, trying to advance and possibly wrapping around. If next is
// false, the search string has been modified, we search for the new string, 
// starting from the current position
void Preview::doSearch(bool next, bool reverse)
{
    //fprintf(stderr, "Preview::doSearch: next %d rev %d\n",
    // int(next), int(reverse));
    QWidget *tw = pvTab->currentPage();
    QTextEdit *edit = 0;
    if (tw) {
	if ((edit = (QTextEdit*)tw->child("pvEdit")) == 0) {
	    // ??
	    return;
	}
    }
    bool matchCase = matchCheck->isChecked();
    int mspara, msindex, mepara, meindex;
    edit->getSelection(&mspara, &msindex, &mepara, &meindex);
    if (mspara == -1)
	mspara = msindex = mepara = meindex = 0;

    if (next) {
	// We search again, starting from the current match
	if (reverse) {
	    // when searching backwards, have to move back one char
	    if (msindex > 0)
		msindex --;
	    else if (mspara > 0) {
		mspara --;
		msindex = edit->paragraphLength(mspara);
	    }
	} else {
	    // Forward search: start from end of selection
	    int bogus;
	    mspara = mepara;
	    msindex = meindex;
	    //fprintf(stderr, "New para: %d index %d\n", mspara, msindex);
	}
    }

    bool found = edit->find(searchTextLine->text(), matchCase, false, 
			      !reverse, &mspara, &msindex);

    if (!found && next && true) { // need a 'canwrap' test here
	if (reverse) {
	    mspara = edit->paragraphs();
	    msindex = edit->paragraphLength(mspara);
	} else {
	    mspara = msindex = 0;
	}
	found = edit->find(searchTextLine->text(), matchCase, false, 
			     !reverse, &mspara, &msindex);
    }

    if (found) {
	canBeep = true;
    } else {
	if (canBeep)
	    QApplication::beep();
	canBeep = false;
    }
}


void Preview::nextPressed()
{
    doSearch(true, false);
}


void Preview::prevPressed()
{
    doSearch(true, true);
}


void Preview::currentChanged(QWidget * tw)
{
    QObject *o;
    o = tw->child("pvEdit");
    fprintf(stderr, "Preview::currentChanged(). Edit %p\n", o);
    
    if (o == 0) {
	fprintf(stderr, "Editor child not found\n");
    } else {
	tw->installEventFilter(this);
	o->installEventFilter(this);
    }
}
