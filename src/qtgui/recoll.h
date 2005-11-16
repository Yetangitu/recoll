#ifndef _RECOLL_H_INCLUDED_
#define _RECOLL_H_INCLUDED_
/* @(#$Id: recoll.h,v 1.5 2005-11-16 15:07:20 dockes Exp $  (C) 2004 J.F.Dockes */
#include <string>
#include <list>
#include "rclconfig.h"
#include "rcldb.h"
#include "idxthread.h"

// Misc declarations in need of sharing between the UI files

extern void recollCleanup();
extern bool maybeOpenDb(std::string &reason);
extern void getQueryStemming(bool &dostem, std::string &stemlang);

extern RclConfig *rclconfig;
extern Rcl::Db *rcldb;
extern std::string tmpdir;
extern bool showicons;
extern string iconsdir;

extern int recollNeedsExit;

#endif /* _RECOLL_H_INCLUDED_ */
