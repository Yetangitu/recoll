/* Copyright (C) 2011 J.F.Dockes
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
#ifndef _PTMUTEX_H_INCLUDED_
#define _PTMUTEX_H_INCLUDED_

#include <pthread.h>

/// A trivial wrapper/helper for pthread mutex locks


/// Init lock. Used as a single PTMutexInit static object.
class PTMutexInit {
public:
    pthread_mutex_t m_mutex;
    PTMutexInit() 
    {
	pthread_mutex_init(&m_mutex, 0);
    }
};

/// Take the lock when constructed, release when deleted
class PTMutexLocker {
public:
    PTMutexLocker(PTMutexInit& l) : m_lock(l)
    {
	m_status = pthread_mutex_lock(&m_lock.m_mutex);
    }
    ~PTMutexLocker()
    {
	pthread_mutex_unlock(&m_lock.m_mutex);
    }
    int ok() {return m_status == 0;}
private:
    PTMutexInit& m_lock;
    int m_status;
};

#endif /* _PTMUTEX_H_INCLUDED_ */
