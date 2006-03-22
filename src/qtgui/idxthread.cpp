#include <stdio.h>
#include <unistd.h>
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


#include <qthread.h>
#include <qmutex.h>

#include "indexer.h"
#include "debuglog.h"
#include "idxthread.h"

static QMutex curfile_mutex;

class IdxThread : public QThread , public DbIxStatusUpdater {
    virtual void run();
 public:
    virtual void update(const string &fn) {
	QMutexLocker locker(&curfile_mutex);
	m_curfile = fn;
	LOGDEB(("IdxThread::update: indexing %s\n", m_curfile.c_str()));
    }
    ConfIndexer *indexer;
    string m_curfile;
    int loglevel;
};

int startindexing;
int indexingdone = 1;
bool indexingstatus = false;

static int stopidxthread;

void IdxThread::run()
{
    DebugLog::getdbl()->setloglevel(loglevel);
    for (;;) {
	if (stopidxthread) {
	    delete indexer;
	    return;
	}
	if (startindexing) {
	    indexingdone = indexingstatus = 0;
	    fprintf(stderr, "Index thread :start index\n");
	    indexingstatus = indexer->index();
	    startindexing = 0;
	    indexingdone = 1;
	} 
	msleep(100);
    }
}

static IdxThread idxthread;

void start_idxthread(const RclConfig& cnf)
{
    // We have to make a copy of the config (setKeydir changes it during 
    // indexation)
    RclConfig *myconf = new RclConfig(cnf);
    ConfIndexer *ix = new ConfIndexer(myconf, &idxthread);
    idxthread.indexer = ix;
    idxthread.loglevel = DebugLog::getdbl()->getlevel();
    idxthread.start();
}

void stop_idxthread()
{
    stopidxthread = 1;
    idxthread.wait();
}

std::string idxthread_currentfile()
{
    QMutexLocker locker(&curfile_mutex);
    return(idxthread.m_curfile);
}
