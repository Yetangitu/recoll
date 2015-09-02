/* Copyright (C) 2004 J.F.Dockes
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
#include "autoconfig.h"

#include <stdio.h>
#include <signal.h>
#include <locale.h>
#include <pthread.h>
#include <cstdlib>
#if !defined(PUTENV_ARG_CONST)
#include <string.h>
#endif

#include "debuglog.h"
#include "rclconfig.h"
#include "rclinit.h"
#include "pathut.h"
#include "unac.h"
#include "smallut.h"
#include "execmd.h"

#ifndef _WIN32
static const int catchedSigs[] = {SIGINT, SIGQUIT, SIGTERM, SIGUSR1, SIGUSR2};
#endif

static pthread_t mainthread_id;

static void siglogreopen(int)
{
    if (recoll_ismainthread())
	DebugLog::reopen();
}

RclConfig *recollinit(RclInitFlags flags, 
		      void (*cleanup)(void), void (*sigcleanup)(int), 
		      string &reason, const string *argcnf)
{
    if (cleanup)
	atexit(cleanup);

    // We ignore SIGPIPE always. All pieces of code which can write to a pipe
    // must check write() return values.
#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);
#endif
    
    // Make sure the locale is set. This is only for converting file names 
    // to utf8 for indexing.
    setlocale(LC_CTYPE, "");

    // We would like to block SIGCHLD globally, but we can't because
    // QT uses it. Have to block it inside execmd.cpp

    // Install app signal handler
    if (sigcleanup) {
#ifndef _WIN32
	struct sigaction action;
	action.sa_handler = sigcleanup;
	action.sa_flags = 0;
	sigemptyset(&action.sa_mask);
	for (unsigned int i = 0; i < sizeof(catchedSigs) / sizeof(int); i++)
	    if (signal(catchedSigs[i], SIG_IGN) != SIG_IGN) {
		if (sigaction(catchedSigs[i], &action, 0) < 0) {
		    perror("Sigaction failed");
		}
	    }
#endif
    }
    DebugLog::getdbl()->setloglevel(DEBDEB1);
    DebugLog::setfilename("stderr");
    if (getenv("RECOLL_LOGDATE"))
        DebugLog::getdbl()->logdate(1);

    RclConfig *config = new RclConfig(argcnf);
    if (!config || !config->ok()) {
	reason = "Configuration could not be built:\n";
	if (config)
	    reason += config->getReason();
	else
	    reason += "Out of memory ?";
	return 0;
    }

    // Retrieve the log file name and level
    string logfilename, loglevel;
    if (flags & RCLINIT_DAEMON) {
	config->getConfParam(string("daemlogfilename"), logfilename);
	config->getConfParam(string("daemloglevel"), loglevel);
    }
    if (logfilename.empty())
	config->getConfParam(string("logfilename"), logfilename);
    if (loglevel.empty())
	config->getConfParam(string("loglevel"), loglevel);

    // Initialize logging
    if (!logfilename.empty()) {
	logfilename = path_tildexpand(logfilename);
	// If not an absolute path or , compute relative to config dir
	if (!path_isabsolute(logfilename) && 
	    !DebugLog::DebugLog::isspecialname(logfilename.c_str())) {
	    logfilename = path_cat(config->getConfDir(), logfilename);
	}
	DebugLog::setfilename(logfilename.c_str());
    }
    if (!loglevel.empty()) {
	int lev = atoi(loglevel.c_str());
	DebugLog::getdbl()->setloglevel(lev);
    }
    // Install log rotate sig handler
#ifndef _WIN32
    {
	struct sigaction action;
	action.sa_handler = siglogreopen;
	action.sa_flags = 0;
	sigemptyset(&action.sa_mask);
	if (signal(SIGHUP, SIG_IGN) != SIG_IGN) {
	    if (sigaction(SIGHUP, &action, 0) < 0) {
		perror("Sigaction failed");
	    }
	}
    }
#endif

    // Make sure the locale charset is initialized (so that multiple
    // threads don't try to do it at once).
    config->getDefCharset();

    mainthread_id = pthread_self();

    // Init unac locking
    unac_init_mt();
    // Init smallut and pathut static values
    pathut_init_mt();
    smallut_init_mt();

    // Init Unac translation exceptions
    string unacex;
    if (config->getConfParam("unac_except_trans", unacex) && !unacex.empty()) 
	unac_set_except_translations(unacex.c_str());

#ifndef IDX_THREADS
#ifndef _WIN32
    ExecCmd::useVfork(true);
#endif
#else
    // Keep threads init behind log init, but make sure it's done before
    // we do the vfork choice ! The latter is not used any more actually, 
    // we always use vfork except if forbidden by config.
    config->initThrConf();

    bool novfork;
    config->getConfParam("novfork", &novfork);
    if (novfork) {
#ifndef _WIN32
	LOGDEB0(("rclinit: will use fork() for starting commands\n"));
        ExecCmd::useVfork(false);
#endif
    } else {
#ifndef _WIN32
	LOGDEB0(("rclinit: will use vfork() for starting commands\n"));
	ExecCmd::useVfork(true);
#endif
    }
#endif

    int flushmb;
    if (config->getConfParam("idxflushmb", &flushmb) && flushmb > 0) {
	LOGDEB1(("rclinit: idxflushmb=%d, set XAPIAN_FLUSH_THRESHOLD to 10E6\n",
		 flushmb));
	static const char *cp = "XAPIAN_FLUSH_THRESHOLD=1000000";
#ifdef PUTENV_ARG_CONST
	::putenv(cp);
#else
	::putenv(strdup(cp));
#endif
    }

    return config;
}

// Signals are handled by the main thread. All others should call this routine
// to block possible signals
void recoll_threadinit()
{
#ifndef _WIN32
    sigset_t sset;
    sigemptyset(&sset);

    for (unsigned int i = 0; i < sizeof(catchedSigs) / sizeof(int); i++)
	sigaddset(&sset, catchedSigs[i]);
    sigaddset(&sset, SIGHUP);
    pthread_sigmask(SIG_BLOCK, &sset, 0);
#endif
}

bool recoll_ismainthread()
{
    return pthread_equal(pthread_self(), mainthread_id);
}


