/*
  bfsync: Big File synchronization tool - FUSE filesystem

  Copyright (C) 2011 Stefan Westerfeld

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef BFSYNC_FS_HH
#define BFSYNC_FS_HH

#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdio.h>
#include <stdarg.h>
#include <sqlite3.h>
#include <pthread.h>
#include <glib.h>
#include <sys/stat.h>

#include <string>
#include <vector>

namespace BFSync
{

class Mutex
{
public:
  pthread_mutex_t mutex;

  Mutex();
  ~Mutex();

  void lock()   { pthread_mutex_lock (&mutex); }
  void unlock() { pthread_mutex_unlock (&mutex); }
};

class Lock
{
  Mutex& mutex;
public:
  Lock (Mutex& mutex) :
    mutex (mutex)
  {
    mutex.lock();
  }
  ~Lock()
  {
    mutex.unlock();
  }
};

class Cond
{
  pthread_cond_t cond;
public:
  Cond();
  ~Cond();

  void broadcast()            { pthread_cond_broadcast (&cond); }
  void wait (Mutex& mutex)    { pthread_cond_wait (&cond, &mutex.mutex); }
};

struct FSLock
{
  enum LockType {
    READ,
    WRITE,
    REORG,
    RDONLY
  } lock_type;
  FSLock (FSLock::LockType lock_type);
  ~FSLock();
};

#define BF_DEBUG 0

FILE *debug_file();

static inline void
debug (const char *fmt, ...)
{
  // no debugging -> return as quickly as possible
  if (!BF_DEBUG)
    return;

  va_list ap;

  va_start (ap, fmt);
  vfprintf (debug_file(), fmt, ap);
  fflush (debug_file());
  va_end (ap);
}

double gettime();

enum
{
  GIT_FILENAME = 1,
  GIT_DIRNAME  = 2
};

std::string make_object_filename (const std::string& hash);
std::string get_dirname (const std::string& filename);

class SplitPath
{
  char *m_path;
  char *m_ptr;
public:
  SplitPath (const char *path)
  {
    m_path  = g_strdup (path);
    m_ptr   = m_path;
  }
  const char*
  next()
  {
    if (!m_ptr)
      return NULL;

    const char *start_ptr = m_ptr;

    while (*m_ptr != '/' && *m_ptr != 0)
      m_ptr++;

    int len = m_ptr - start_ptr;
    if (*m_ptr == 0) // end of string
      {
        m_ptr = NULL;
      }
    else // found "/"
      {
        *m_ptr++ = 0;
      }
    // return path component only if non-empty
    if (len > 0)
      {
        return start_ptr;
      }
    else
      {
        return next();
      }
  }
  ~SplitPath()
  {
    g_free (m_path);
  }
};


struct Options {
  std::string  repo_path;
  std::string  mount_point;
  bool         mount_debug;
  bool         mount_all;
  bool         mount_fg;
  bool         sqlite_sync;
  bool         ignore_uid_gid;

  static Options *the();
};

int bfsync_getattr (const char *path_arg, struct stat *stbuf);

class Context
{
private:
  Context (const Context& other);

public:
  Context();

  const fuse_context *fc;
  int                 version;
};

std::string string_printf (const char *format, ...) __attribute__ ((__format__ (__printf__, 1, 2)));

}

int bfsyncfs_main (int argc, char **argv);

#endif /* BFSYNC_FS_HH */
