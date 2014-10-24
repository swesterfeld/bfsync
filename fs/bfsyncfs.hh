// Licensed GNU GPL v3 or later: http://www.gnu.org/licenses/gpl.html

#ifndef BFSYNC_FS_HH
#define BFSYNC_FS_HH

#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdio.h>
#include <stdarg.h>
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
std::string get_basename (const std::string& filename);

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
  bool         cache_attributes;
  bool         use_uid_gid;
  int          cache_size_mb;
  std::string  bfsync_group;

  void debug() const;
  void parse_or_exit (int argc, char **argv);

  static Options *the();
};

int   bfsync_getattr (const char *path_arg, struct stat *stbuf);
void  bfsyncfs_update_read_only();

class Context
{
private:
  Context (const Context& other);

public:
  Context();

  const fuse_context *fc;
  unsigned int        version;
};

std::string string_printf (const char *format, ...) __attribute__ ((__format__ (__printf__, 1, 2)));

const unsigned int VERSION_INF = 0xffffffff;

}

int   bfsyncfs_main (int argc, char **argv);

#endif /* BFSYNC_FS_HH */
