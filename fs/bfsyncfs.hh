/*
  bfsync: Big File synchronization based on Git - FUSE filesystem

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

#include <stdio.h>
#include <stdarg.h>
#include <sqlite3.h>

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

enum
{
  GIT_FILENAME = 1,
  GIT_DIRNAME  = 2
};

std::string name2git_name (const std::string& name, int type = GIT_FILENAME);
std::string make_object_filename (const std::string& hash);
std::string get_dirname (const std::string& filename);

struct Options {
  std::string  repo_path;
  std::string  mount_point;
  bool         mount_debug;
  bool         mount_all;
  bool         mount_fg;

  static Options *the();
};

sqlite3 *sqlite_db();

}

#endif /* BFSYNC_FS_HH */
