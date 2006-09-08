#ifndef lint
static char rcsid[] = "@(#$Id: recollindex.cpp,v 1.20 2006-09-08 09:02:47 dockes Exp $ (C) 2004 J.F.Dockes";
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

#include <stdio.h>
#include <signal.h>
#include <sys/stat.h>

#include <iostream>
#include <list>
#include <string>

using namespace std;

#include "debuglog.h"
#include "rclinit.h"
#include "indexer.h"
#include "smallut.h"
#include "pathut.h"


// Globals for exit cleanup
ConfIndexer *confindexer;
DbIndexer *dbindexer;

// Index a list of files 
static bool indexfiles(RclConfig *config, const list<string> &filenames)
{
    if (filenames.empty())
	return true;

    // Note that we do not bother to check for multiple databases,
    // which are currently a fiction anyway. 
    config->setKeyDir(path_getfather(*filenames.begin()));
    string dbdir = config->getDbDir();
    if (dbdir.empty()) {
	LOGERR(("indexfiles: no database directory in "
		"configuration for %s\n", filenames.begin()->c_str()));
	return false;
    }
    dbindexer = new DbIndexer(config, dbdir);
    return dbindexer->indexFiles(filenames);
}

// Create additional stem database 
static bool createstemdb(RclConfig *config, const string &lang)
{
    // Note that we do not bother to check for multiple databases,
    // which are currently a fiction anyway. 
    string dbdir = config->getDbDir();
    if (dbdir.empty()) {
	LOGERR(("createstemdb: no database directory in configuration\n"));
	return false;
    }
    dbindexer = new DbIndexer(config, dbdir);
    return dbindexer->createStemDb(lang);
}

static void cleanup()
{
    delete confindexer;
    confindexer = 0;
    delete dbindexer;
    dbindexer = 0;
}

int stopindexing;
// Mainly used to request indexing stop, we currently do not use the
// current file name
class MyUpdater : public DbIxStatusUpdater {
 public:
    virtual bool update() {
	if (stopindexing) {
	    return false;
	}
	return true;
    }
};
MyUpdater updater;

static void sigcleanup(int sig)
{
    fprintf(stderr, "sigcleanup\n");
    stopindexing = 1;
}

static const char *thisprog;
static int     op_flags;
#define OPT_MOINS 0x1
#define OPT_z     0x2 
#define OPT_h     0x4 
#define OPT_i     0x8
#define OPT_s     0x10
#define OPT_c     0x20

static const char usage [] =
"\n"
"recollindex [-h] \n"
"    Print help\n"
"recollindex [-z] \n"
"    Index everything according to configuration file\n"
"    -z : reset database before starting indexation\n"
"recollindex -i <filename [filename ...]>\n"
"    Index individual files. No database purge or stem database updates\n"
"recollindex -s <lang>\n"
"    Build stem database for additional language <lang>\n"
"Common options:\n"
"    -c <configdir> : specify config directory, overriding $RECOLL_CONFDIR\n"
;

static void
Usage(void)
{
    FILE *fp = (op_flags & OPT_h) ? stdout : stderr;
    fprintf(fp, "%s: Usage: %s", thisprog, usage);
    exit((op_flags & OPT_h)==0);
}


int main(int argc, const char **argv)
{
    string a_config;
    thisprog = argv[0];
    argc--; argv++;

    while (argc > 0 && **argv == '-') {
	(*argv)++;
	if (!(**argv))
	    Usage();
	while (**argv)
	    switch (*(*argv)++) {
	    case 'c':	op_flags |= OPT_c; if (argc < 2)  Usage();
		a_config = *(++argv);
		argc--; goto b1;
	    case 'h': op_flags |= OPT_h; break;
	    case 'i': op_flags |= OPT_i; break;
	    case 's': op_flags |= OPT_s; break;
	    case 'z': op_flags |= OPT_z; break;
	    default: Usage(); break;
	    }
    b1: argc--; argv++;
    }
    if (op_flags & OPT_h)
	Usage();
    if ((op_flags & OPT_z) && (op_flags & OPT_i))
	Usage();

    string reason;
    RclConfig *config = recollinit(cleanup, sigcleanup, reason, &a_config);
    if (config == 0 || !config->ok()) {
	cerr << "Configuration problem: " << reason << endl;
	exit(1);
    }
    
    if (op_flags & OPT_i) {
	list<string> filenames;
	if (argc == 0) {
	    // Read from stdin
	    char line[1024];
	    while (fgets(line, 1023, stdin)) {
		string sl(line);
		trimstring(sl, "\n\r");
		filenames.push_back(sl);
	    }
	} else {
	    while (argc--) {
		filenames.push_back(*argv++);
	    }
	}
	exit(!indexfiles(config, filenames));
    } else if (op_flags & OPT_s) {
	if (argc != 1) 
	    Usage();
	string lang = *argv++; argc--;
	exit(!createstemdb(config, lang));
    } else {
	confindexer = new ConfIndexer(config, &updater);
	bool rezero(op_flags & OPT_z);
	exit(!confindexer->index(rezero));
    }
}
