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
#ifndef _PATHUT_H_INCLUDED_
#define _PATHUT_H_INCLUDED_

#include <string>
#include <vector>
#include <set>

// Must be called in main thread before starting other threads
extern void pathut_init_mt();

/// Add a / at the end if none there yet.
extern void path_catslash(std::string& s);
/// Concatenate 2 paths
extern std::string path_cat(const std::string& s1, const std::string& s2);
/// Get the simple file name (get rid of any directory path prefix
extern std::string path_getsimple(const std::string& s);
/// Simple file name + optional suffix stripping
extern std::string path_basename(const std::string& s,
                                 const std::string& suff = std::string());
/// Component after last '.'
extern std::string path_suffix(const std::string& s);
/// Get the father directory
extern std::string path_getfather(const std::string& s);
/// Get the current user's home directory
extern std::string path_home();
/// Expand ~ at the beginning of std::string
extern std::string path_tildexpand(const std::string& s);
/// Use getcwd() to make absolute path if needed. Beware: ***this can fail***
/// we return an empty path in this case.
extern std::string path_absolute(const std::string& s);
/// Clean up path by removing duplicated / and resolving ../ + make it absolute
extern std::string path_canon(const std::string& s, const std::string *cwd = 0);
/// Use glob(3) to return the file names matching pattern inside dir
extern std::vector<std::string> path_dirglob(const std::string& dir,
        const std::string pattern);
/// Encode according to rfc 1738
extern std::string url_encode(const std::string& url,
                              std::string::size_type offs = 0);
//// Convert to file path if url is like file://. This modifies the
//// input (and returns a copy for convenience)
extern std::string fileurltolocalpath(std::string url);
/// Test for file:/// url
extern bool urlisfileurl(const std::string& url);
///
extern std::string url_parentfolder(const std::string& url);

/// Return the host+path part of an url. This is not a general
/// routine, it does the right thing only in the recoll context
extern std::string url_gpath(const std::string& url);

/// Stat parameter and check if it's a directory
extern bool path_isdir(const std::string& path);

/// Retrieve file size
extern long long path_filesize(const std::string& path);

/// Retrieve essential file attributes. This is used rather than a
/// bare stat() to ensure consistent use of the time fields (on
/// windows, we set ctime=mtime as ctime is actually the creation
/// time, for which we have no use).
/// Only st_mtime, st_ctime, st_size, st_mode (file type bits) are set on
/// all systems. st_dev and st_ino are set for special posix usage.
/// The rest is zeroed.
/// @ret 0 for success
struct stat;
extern int path_fileprops(const std::string path, struct stat *stp,
                          bool follow = true);

/// Check that path is traversable and last element exists
/// Returns true if last elt could be checked to exist. False may mean that
/// the file/dir does not exist or that an error occurred.
extern bool path_exists(const std::string& path);

/// Return separator for PATH environment variable
extern std::string path_PATHsep();

/// Dump directory
extern bool readdir(const std::string& dir, std::string& reason,
                    std::set<std::string>& entries);

/** A small wrapper around statfs et al, to return percentage of disk
    occupation
    @param[output] pc percent occupied
    @param[output] avmbs Mbs available to non-superuser. Mb=1024*1024
*/
bool fsocc(const std::string& path, int *pc, long long *avmbs = 0);

/// mkdir -p
extern bool path_makepath(const std::string& path, int mode);

/// Where we create the user data subdirs
extern std::string path_homedata();
/// Test if path is absolute
extern bool path_isabsolute(const std::string& s);

/// Test if path is root (x:/). root is defined by root/.. == root
extern bool path_isroot(const std::string& p);

/// Turn absolute path into file:// url
extern std::string path_pathtofileurl(const std::string& path);

#ifdef _WIN32
/// Convert \ separators to /
void path_slashize(std::string& s);
void path_backslashize(std::string& s);
#endif

/// Lock/pid file class. This is quite close to the pidfile_xxx
/// utilities in FreeBSD with a bit more encapsulation. I'd have used
/// the freebsd code if it was available elsewhere
class Pidfile {
public:
    Pidfile(const std::string& path)    : m_path(path), m_fd(-1) {}
    ~Pidfile();
    /// Open/create the pid file.
    /// @return 0 if ok, > 0 for pid of existing process, -1 for other error.
    pid_t open();
    /// Write pid into the pid file
    /// @return 0 ok, -1 error
    int write_pid();
    /// Close the pid file (unlocks)
    int close();
    /// Delete the pid file
    int remove();
    const std::string& getreason() {
        return m_reason;
    }
private:
    std::string m_path;
    int    m_fd;
    std::string m_reason;
    pid_t read_pid();
    int flopen();
};

#endif /* _PATHUT_H_INCLUDED_ */
