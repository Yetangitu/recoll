#ifndef lint
static char rcsid[] = "@(#$Id: rclconfig.cpp,v 1.17 2005-12-13 12:42:59 dockes Exp $ (C) 2004 J.F.Dockes";
#endif
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <iostream>

#include "pathut.h"
#include "rclconfig.h"
#include "conftree.h"
#include "debuglog.h"
#include "smallut.h"
#include "copyfile.h"

#ifndef NO_NAMESPACES
using namespace std;
#endif /* NO_NAMESPACES */

static const char *configfiles[] = {"recoll.conf", "mimemap", "mimeconf"};
static int ncffiles = sizeof(configfiles) / sizeof(char *);

static bool createConfig(string &reason)
{
    const char *cprefix = getenv("RECOLL_PREFIX");
    if (cprefix == 0)
	cprefix = RECOLL_PREFIX;
    string prefix = path_cat(cprefix, "share/recoll/examples");

    string recolldir = path_tildexpand("~/.recoll");
    if (mkdir(recolldir.c_str(), 0755) < 0) {
	reason += string("mkdir(") + recolldir + ") failed: " + 
	    strerror(errno);
	return false;
    }
    for (int i = 0; i < ncffiles; i++) {
	string src = path_cat((const string&)prefix, string(configfiles[i]));
	string dst = path_cat((const string&)recolldir, string(configfiles[i])); 
	if (!copyfile(src.c_str(), dst.c_str(), reason)) {
	    LOGERR(("Copyfile failed: %s\n", reason.c_str()));
	    return false;
	}
    }
    return true;
}


RclConfig::RclConfig()
    : m_ok(false), conf(0), mimemap(0), mimeconf(0), stopsuffixes(0)

{
    static int loginit = 0;
    if (!loginit) {
	DebugLog::setfilename("stderr");
	DebugLog::getdbl()->setloglevel(10);
	loginit = 1;
    }

    const char *cp = getenv("RECOLL_CONFDIR");
    if (cp) {
	confdir = cp;
    } else {
	confdir = path_home();
	confdir += ".recoll/";
    }
    string cfilename = path_cat(confdir, "recoll.conf");

    if (access(confdir.c_str(), 0) != 0 || access(cfilename.c_str(), 0) != 0) {
	if (!createConfig(reason))
	    return;
    }

    // Open readonly here so as not to casually create a config file
    conf = new ConfTree(cfilename.c_str(), true);
    if (conf == 0 || 
	(conf->getStatus() != ConfSimple::STATUS_RO && 
	 conf->getStatus() != ConfSimple::STATUS_RW)) {
	reason = string("No main configuration file: ") + cfilename + 
	    " does not exist or cannot be parsed";
	return;
    }

    string mimemapfile;
    if (!conf->get("mimemapfile", mimemapfile, "")) {
	mimemapfile = "mimemap";
    }
    string mpath  = path_cat(confdir, mimemapfile);
    mimemap = new ConfTree(mpath.c_str(), true);
    if (mimemap == 0 ||
	(mimemap->getStatus() != ConfSimple::STATUS_RO && 
	 mimemap->getStatus() != ConfSimple::STATUS_RW)) {
	reason = string("No mime map configuration file: ") + mpath + 
	    " does not exist or cannot be parsed";
	return;
    }
    // mimemap->list();

    string mimeconffile;
    if (!conf->get("mimeconffile", mimeconffile, "")) {
	mimeconffile = "mimeconf";
    }
    mpath = path_cat(confdir, mimeconffile);
    mimeconf = new ConfTree(mpath.c_str(), true);
    if (mimeconf == 0 ||
	(mimeconf->getStatus() != ConfSimple::STATUS_RO && 
	 mimeconf->getStatus() != ConfSimple::STATUS_RW)) {
	reason = string("No mime configuration file: ") + mpath + 
	    " does not exist or cannot be parsed";
	return;
    }
    //    mimeconf->list();

    setKeyDir(string(""));

    m_ok = true;
    return;
}

bool RclConfig::getConfParam(const std::string &name, int *ivp)
{
    string value;
    if (!getConfParam(name, value))
	return false;
    errno = 0;
    long lval = strtol(value.c_str(), 0, 0);
    if (lval == 0 && errno)
	return 0;
    if (ivp)
	*ivp = int(lval);
    return true;
}

bool RclConfig::getConfParam(const std::string &name, bool *bvp)
{
    if (!bvp) 
	return false;

    *bvp = false;
    string s;
    if (!getConfParam(name, s))
	return false;
    *bvp = stringToBool(s);
    return true;
}

// Get all known document mime values. We get them from the mimeconf
// 'index' submap: values not in there (ie from mimemap or idfile) can't
// possibly belong to documents in the database.
std::list<string> RclConfig::getAllMimeTypes()
{
    std::list<string> lst;
    if (mimeconf == 0)
	return lst;
    //    mimeconf->sortwalk(mtypesWalker, &lst);
    lst = mimeconf->getNames("index");
    lst.sort();
    lst.unique();
    return lst;
}

bool RclConfig::getStopSuffixes(list<string>& sufflist)
{
    if (stopsuffixes == 0 && (stopsuffixes = new list<string>) != 0) {
	string stp;
	if (mimemap->get("recoll_noindex", stp, keydir)) {
	    stringToStrings(stp, *stopsuffixes);
	}
    }

    if (stopsuffixes) {
	sufflist = *stopsuffixes;
	return true;
    }
    return false;
}

string RclConfig::getMimeTypeFromSuffix(const string &suff)
{
    string mtype;
    mimemap->get(suff, mtype, keydir);
    return mtype;
}

string RclConfig::getMimeHandlerDef(const std::string &mtype)
{
    string hs;
    if (!mimeconf->get(mtype, hs, "index")) {
	LOGDEB(("getMimeHandler: no handler for '%s'\n", mtype.c_str()));
    }
    return hs;
}

string RclConfig::getMimeViewerDef(const string &mtype)
{
    string hs;
    mimeconf->get(mtype, hs, "view");
    return hs;
}

/**
 * Return icon name
 */
string RclConfig::getMimeIconName(const string &mtype)
{
    string hs;
    mimeconf->get(mtype, hs, "icons");
    return hs;
}

// Look up an executable filter.  
// We look in RECOLL_FILTERSDIR, filtersdir param, then
// let the system use the PATH
string find_filter(RclConfig *conf, const string &icmd)
{
    // If the path is absolute, this is it
    if (icmd[0] == '/')
	return icmd;

    string cmd;
    const char *cp;

    if (cp = getenv("RECOLL_FILTERSDIR")) {
	cmd = path_cat(cp, icmd);
	if (access(cmd.c_str(), X_OK) == 0)
	    return cmd;
    } else if (conf->getConfParam(string("filtersdir"), cmd)) {
	cmd = path_cat(cmd, icmd);
	if (access(cmd.c_str(), X_OK) == 0)
	    return cmd;
    } else {
	cmd = path_cat(conf->getConfDir(), icmd);
	if (access(cmd.c_str(), X_OK) == 0)
	    return cmd;
    }
    return icmd;
}

/** 
 * Return decompression command line for given mime type
 */
bool RclConfig::getUncompressor(const string &mtype, list<string>& cmd)
{
    string hs;

    mimeconf->get(mtype, hs, "");
    if (hs.empty())
	return false;
    list<string> tokens;
    stringToStrings(hs, tokens);
    if (tokens.empty()) {
	LOGERR(("getUncompressor: empty spec for mtype %s\n", mtype.c_str()));
	return false;
    }
    if (stringlowercmp("uncompress", tokens.front())) 
	return false;
    list<string>::iterator it = tokens.begin();
    cmd.assign(++it, tokens.end());
    return true;
}
