#ifndef _IDXTHREAD_H_INCLUDED_
#define _IDXTHREAD_H_INCLUDED_
/* @(#$Id: idxthread.h,v 1.2 2006-01-20 10:01:59 dockes Exp $  (C) 2004 J.F.Dockes */

class RclConfig;

// These two deal with starting / stopping the thread itself, not indexing
// sessions.
extern void start_idxthread(const RclConfig& cnf);
extern void stop_idxthread();

extern int startindexing;
extern int indexingdone;
extern bool indexingstatus;

#endif /* _IDXTHREAD_H_INCLUDED_ */
