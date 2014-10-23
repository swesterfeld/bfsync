// Licensed GNU GPL v3 or later: http://www.gnu.org/licenses/gpl.html

#include "bfinode.hh"
#include "bflink.hh"
#include "bfsyncserver.hh"
#include "bfsyncfs.hh"
#include "bfhistory.hh"
#include "bfcfgparser.hh"
#include "bfbdb.hh"
#include "bftimeprof.hh"
#include "bfgroup.hh"
#include "config.h"

#include <sys/time.h>
#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <stdlib.h>
#include <assert.h>

#include <string>
#include <vector>
#include <set>

#include <boost/program_options.hpp>

using std::string;
using std::vector;
using std::set;
using std::map;
using std::max;

using namespace BFSync;

namespace BFSync {

Options options;

Options*
Options::the()
{
  return &options;
}

struct FileHandle
{
  int fd;
  enum { NONE, INFO } special_file;
  bool open_for_write;
};

struct SpecialFiles
{
  string info;
} special_files;

string
string_printf (const char *format, ...)
{
  string str;
  va_list args;
  va_start (args, format);
  char *c_str = NULL;
  if (vasprintf (&c_str, format, args) >= 0 && c_str)
    {
      str = c_str;
      free (c_str);
    }
  else
    {
      str = format;
    }
  va_end (args);
  return str;
}

string
get_info()
{
  string info = special_files.info;
  info += string_printf ("cached-inodes %d;\n", INodeRepo::the()->cached_inode_count());
  info += string_printf ("cached-dirs %d;\n", INodeRepo::the()->cached_dir_count());
  return info;
}

static FILE *bf_debug_file = NULL;

FILE*
debug_file()
{
  if (!bf_debug_file)
    {
      string debug_filename = string_printf ("/tmp/bfsyncfs.log.%d", getpid());
      bf_debug_file = fopen (debug_filename.c_str(), "w");
    }

  return bf_debug_file;
}

static bool bfsyncfs_read_only = false;

void
bfsyncfs_update_read_only()
{
  vector<JournalEntry> jvec;
  INodeRepo::the()->bdb->load_journal_entries (jvec);
  bfsyncfs_read_only = (jvec.size() != 0);
}

string
get_dirname (const string& dirname)
{
  char *dirname_c = g_path_get_dirname (dirname.c_str());
  string result = dirname_c;
  g_free (dirname_c);

  return result;
}

string
get_basename (const string& filename)
{
  char *basename_c = g_path_get_basename (filename.c_str());
  string result = basename_c;
  g_free (basename_c);

  return result;
}

double
gettime()
{
  timeval tv;
  gettimeofday (&tv, 0);

  return tv.tv_sec + tv.tv_usec / 1000000.0;
}

Mutex::Mutex()
{
  pthread_mutex_init (&mutex, NULL);
}

Mutex::~Mutex()
{
  pthread_mutex_destroy (&mutex);
}

Cond::Cond()
{
  pthread_cond_init (&cond, NULL);
}

Cond::~Cond()
{
  pthread_cond_destroy (&cond);
}

struct LockState
{
  Mutex mutex;
  Cond  cond;
  bool  fs_busy;
  bool  fs_rdonly;

  LockState();

  void lock (FSLock::LockType lock_type);
  void unlock (FSLock::LockType lock_type);
} lock_state;

LockState::LockState() :
  fs_busy (false),
  fs_rdonly (false)
{
}

void
LockState::lock (FSLock::LockType lock_type)
{
  Lock lock (mutex);
  while (1)
    {
      switch (lock_type)
        {
          /* READ is allowed if:
             - no other thread reads or writes at the same time
             - no other thread performs data reorganization (like during commit) at the same time

             Its ok to read if the filesystem is in readonly mode.
           */
          case FSLock::READ:
            if (!fs_busy)
              {
                fs_busy = true;
                return;
              }
            break;
          /* WRITE is allowed if:
             - no other thread reads or writes at the same time
             - no other thread performs data reorganization (like during commit) at the same time
             - the filesystem is not in readonly mode
           */
          case FSLock::WRITE:
            if (!fs_busy && !fs_rdonly)
              {
                fs_busy = true;
                return;
              }
            break;
          /* REORG is allowed if:
             - no other thread reads or writes at the same time
             - no other thread performs data reorganization (like during commit) at the same time
             - the filesystem is in readonly mode

             Reorg is ok during readonly mode (although reorg writes to the disk, it doesn't
             change the contents of the filesystem, therefore its technically something different
             than write).
           */
          case FSLock::REORG:
            if (!fs_busy && fs_rdonly)
              {
                fs_busy = true;
                return;
              }
            break;
          /* RDONLY (making the filesystem readonly) is allowed if:
             - no other thread reads or writes at the same time
             - no other thread performs data reorganization (like during commit) at the same time
             - the filesystem is not in readonly mode

             Reads performed in other threads do not affect making the FS readonly.
           */
          case FSLock::RDONLY:
            if (!fs_busy && !fs_rdonly)
              {
                fs_rdonly = true;
                return;
              }
            break;
          default:
            g_assert_not_reached();
        }
      cond.wait (mutex);
    }
}

void
LockState::unlock (FSLock::LockType lock_type)
{
  Lock lock (mutex);
  if (lock_type == FSLock::READ)
    {
      assert (fs_busy);
      fs_busy = false;
    }
  if (lock_type == FSLock::WRITE)
    {
      assert (fs_busy);
      fs_busy = false;
    }
  if (lock_type == FSLock::REORG)
    {
      assert (fs_busy && fs_rdonly);
      fs_busy = false;
    }
  if (lock_type == FSLock::RDONLY)
    {
      assert (fs_rdonly);
      fs_rdonly = false;
    }
  cond.broadcast();
}

FSLock::FSLock (LockType lock_type) :
  lock_type (lock_type)
{
  lock_state.lock (lock_type);
}

FSLock::~FSLock()
{
  lock_state.unlock (lock_type);
}

Context::Context () :
  fc (fuse_get_context()),
  version (INodeRepo::the()->bdb->history()->current_version())
{
}

int
version_map_path (string& path)
{
  static const char *prefix = "/.bfsync/commits/";
  static int prefix_len = strlen (prefix);

  if (strncmp (path.c_str(), prefix, prefix_len) != 0)
    return -1; // no need to do mapping

  SplitPath s_path (path.c_str());
  vector<string> p_vec;
  const char *p;
  while ((p = s_path.next()))
    p_vec.push_back (p);

  if (p_vec.size() < 3)
    return -1;
  if (p_vec[0] != ".bfsync" || p_vec[1] != "commits")
    return -1;
  // search version in history
  int version = -1;

  const unsigned int path_version = atoi (p_vec[2].c_str());
  const History *history = INodeRepo::the()->bdb->history();

  if (history->have_version (path_version) && path_version != history->current_version())
    {
      char buffer[64];
      sprintf (buffer, "%u", path_version);
      if (buffer == p_vec[2])
        version = path_version;
    }

  if (version == -1) // not found
    return -1;

  path.clear();
  for (size_t i = 3; i < p_vec.size(); i++)
    path += "/" + p_vec[i];
  if (path.empty())
    path = "/";

  return version;
}

enum IFPStatus { IFP_OK, IFP_ERR_NOENT, IFP_ERR_PERM };

INodePtr
inode_from_path (const Context& ctx, const string& path, IFPStatus& status)
{
  INodePtr inode (ctx, ID::root());
  if (!inode)
    {
      printf ("root not found\n");
      fflush (stdout);
      status = IFP_ERR_NOENT;
      return INodePtr::null();
    }

  SplitPath s_path = SplitPath (path.c_str());
  const char *pi;
  while ((pi = s_path.next()))
    {
      if (!inode->search_perm_ok (ctx))
        {
          status = IFP_ERR_PERM;
          return INodePtr::null();
        }
      inode = inode->get_child (ctx, pi);
      if (!inode)
        {
          status = IFP_ERR_NOENT;
          return INodePtr::null();
        }
    }
  status = IFP_OK;
  return inode;
}

Mutex               intern_inode_mutex;
map<string, ino_t>  intern_inode_map;
ino_t               intern_inode_next = 1;

static ino_t
intern_inode (const string& path)
{
  Lock lock (intern_inode_mutex);
  ino_t& result = intern_inode_map[path];
  if (!result)
    result = intern_inode_next++;

  return result;
}

int
bfsyncdir_getattr (const string& path, struct stat *stbuf)
{
  SplitPath s_path (path.c_str());
  vector<string> pvec;

  const char *p;
  while ((p = s_path.next()))
    {
      pvec.push_back (p);
    }
  if (pvec.empty())
    return -EIO;

  if (pvec[0] != ".bfsync")
    return -ENOENT;

  if (pvec.size() == 2)
    {
      if (pvec[1] == "info")
        {
          string info = get_info();

          memset (stbuf, 0, sizeof (struct stat));
          stbuf->st_mode = 0644 | S_IFREG;
          stbuf->st_uid  = getuid();
          stbuf->st_gid  = getgid();
          stbuf->st_size = info.size();
          stbuf->st_ino  = intern_inode (path);
          return 0;
        }
      if (pvec[1] == "commits")
        {
          memset (stbuf, 0, sizeof (struct stat));
          stbuf->st_mode = 0755 | S_IFDIR;
          stbuf->st_uid  = getuid();
          stbuf->st_gid  = getgid();
          stbuf->st_ino  = intern_inode (path);
          return 0;
        }
    }
  return -ENOENT;
}

int
bfsync_getattr (const char *path_arg, struct stat *stbuf)
{
  string path = path_arg;

  FSLock lock (FSLock::READ);

  Context ctx;
  const string& bfsync_group = Options::the()->bfsync_group;
  if (!bfsync_group.empty() && get_bfsync_group (ctx.fc->pid) != bfsync_group)
    return -EACCES;

  int version = version_map_path (path);
  if (version > 0)
    {
      ctx.version = version;
    }
  else
    {
      if (path.substr (0, 9) == "/.bfsync/")
        return bfsyncdir_getattr (path, stbuf);

      if (path == "/.bfsync")
        {
          memset (stbuf, 0, sizeof (struct stat));
          stbuf->st_mode = 0755 | S_IFDIR;
          stbuf->st_uid  = getuid();
          stbuf->st_gid  = getgid();
          stbuf->st_ino  = intern_inode (path);
          return 0;
        }
    }

  IFPStatus ifp;
  INodePtr  inode = inode_from_path (ctx, path, ifp);
  if (!inode)
    {
      if (ifp == IFP_ERR_NOENT)
        return -ENOENT;
      if (ifp == IFP_ERR_PERM)
        return -EACCES;
    }

  int inode_mode = inode->mode & ~S_IFMT;

  memset (stbuf, 0, sizeof (struct stat));
  if (Options::the()->use_uid_gid)
    {
      stbuf->st_uid = inode->uid;
      stbuf->st_gid = inode->gid;
    }
  else
    {
      stbuf->st_uid = getuid();
      stbuf->st_gid = getgid();
    }
  stbuf->st_mtime        = inode->mtime;
  stbuf->st_mtim.tv_nsec = inode->mtime_ns;
  stbuf->st_ctime        = inode->ctime;
  stbuf->st_ctim.tv_nsec = inode->ctime_ns;
  stbuf->st_atim         = stbuf->st_mtim;    // we don't track atime, so set atime == mtime
  stbuf->st_nlink        = inode->nlink;
  stbuf->st_ino          = inode->ino;
  if (inode->type == FILE_REGULAR)
    {
      if (inode->hash == "new")
        {
          // take size from new file
          struct stat new_stat;
          lstat (inode->file_path().c_str(), &new_stat);

          stbuf->st_size = new_stat.st_size;
        }
      else
        {
          stbuf->st_size = inode->size;
        }
      stbuf->st_blocks = (stbuf->st_size + 511) / 512;
      stbuf->st_mode = inode_mode | S_IFREG;
    }
  else if (inode->type == FILE_SYMLINK)
    {
      stbuf->st_mode = inode_mode | S_IFLNK;
      stbuf->st_size = inode->link.size();
    }
  else if (inode->type == FILE_DIR)
    {
      stbuf->st_mode = inode_mode | S_IFDIR;
    }
  else if (inode->type == FILE_FIFO)
    {
      stbuf->st_mode = inode_mode | S_IFIFO;
    }
  else if (inode->type == FILE_SOCKET)
    {
      stbuf->st_mode = inode_mode | S_IFSOCK;
    }
  else if (inode->type == FILE_BLOCK_DEV)
    {
      stbuf->st_mode = inode_mode | S_IFBLK;
      stbuf->st_rdev = makedev (inode->major, inode->minor);
    }
  else if (inode->type == FILE_CHAR_DEV)
    {
      stbuf->st_mode = inode_mode | S_IFCHR;
      stbuf->st_rdev = makedev (inode->major, inode->minor);
    }
  return 0;
}

bool
read_dir_contents (const Context& ctx, const string& path, vector<string>& entries)
{
  bool            dir_ok = true;

  IFPStatus ifp;
  INodePtr inode = inode_from_path (ctx, path, ifp);
  if (inode)
    {
      inode->get_child_names (ctx, entries);
    }

  // bfsync directory (not in .bfsync/commits/N)
  if (ctx.version != INodeRepo::the()->bdb->history()->current_version())
    return dir_ok;

  if (path == "/")
    {
      entries.push_back (".bfsync");
    }
  else if (path == "/.bfsync")
    {
      entries.push_back ("info");
      entries.push_back ("commits");
    }
  else if (path == "/.bfsync/commits")
    {
      const History *history = INodeRepo::the()->bdb->history();

      for (unsigned int v = history->vbegin(); v != history->vend(); v++)
        {
          if (v != history->current_version() && history->have_version (v))
            {
              string v_str = string_printf ("%u", v);
              entries.push_back (v_str);
            }
        }
    }

  return dir_ok;
}

static int
bfsync_opendir (const char *path_arg, struct fuse_file_info *fi)
{
  string path = path_arg;

  FSLock lock (FSLock::READ);

  Context ctx;
  int version = version_map_path (path);
  if (version > 0)
    {
      ctx.version = version;
    }
  else
    {
      if (path == "/.bfsync" || path == "/.bfsync/commits")
        return 0;
    }

  IFPStatus ifp;
  INodePtr dir_inode = inode_from_path (ctx, path, ifp);
  if (!dir_inode)
    {
      if (ifp == IFP_ERR_NOENT)
        return -ENOENT;
      if (ifp == IFP_ERR_PERM)
        return -EACCES;
    }

  if (!dir_inode->search_perm_ok (ctx))
    return -EACCES;

  if (!dir_inode->read_perm_ok (ctx))
    return -EACCES;

  return 0;
}

static int
bfsync_readdir (const char *path_arg, void *buf, fuse_fill_dir_t filler,
                off_t offset, struct fuse_file_info *fi)
{
  string path = path_arg;

  FSLock lock (FSLock::READ);
  Context ctx;
  int version = version_map_path (path);
  if (version > 0)
    ctx.version = version;

  debug ("readdir (\"%s\")\n", path.c_str());

  (void) offset;
  (void) fi;

  vector<string> entries;
  if (read_dir_contents (ctx, path, entries))
    {
      debug ("=> %zd entries\n", entries.size());

      for (vector<string>::iterator ei = entries.begin(); ei != entries.end(); ei++)
        filler (buf, ei->c_str(), NULL, 0);

      // . and .. are always there
      filler (buf, ".", NULL, 0);
      filler (buf, "..", NULL, 0);

      return 0;
    }
  else
    {
      debug ("=> ENOENT\n");
      return -ENOENT;
    }
}

static int
bfsync_open (const char *path_arg, struct fuse_file_info *fi)
{
  string path = path_arg;
  int accmode = fi->flags & O_ACCMODE;
  // can both be true (for O_RDWR)
  bool open_for_write = (accmode == O_WRONLY || accmode == O_RDWR);
  bool open_for_read  = (accmode == O_RDONLY || accmode == O_RDWR);

  FSLock lock (open_for_write ? FSLock::WRITE : FSLock::READ);
  Context ctx;
  int version = version_map_path (path);
  if (version > 0)
    {
      if (open_for_write)
        return -EROFS;
      ctx.version = version;
    }
  if (open_for_write && bfsyncfs_read_only)
    return -EROFS;

  if (string (path) == "/.bfsync/info")
    {
      FileHandle *fh = new FileHandle;
      fh->fd = -1;
      fh->special_file = FileHandle::INFO;
      fh->open_for_write = false;
      fi->fh = reinterpret_cast<uint64_t> (fh);
      return 0;
    }

  IFPStatus ifp;
  INodePtr  inode = inode_from_path (ctx, path, ifp);
  if (!inode)
    {
      if (ifp == IFP_ERR_NOENT)
        return -ENOENT;
      if (ifp == IFP_ERR_PERM)
        return -EACCES;
    }
  if (open_for_write && !inode->write_perm_ok (ctx))
    return -EACCES;

  if (open_for_read && !inode->read_perm_ok (ctx))
    return -EACCES;

  if (open_for_write)
    inode.update()->copy_on_write();

  string filename = inode->file_path();
  int fd = open (filename.c_str(), fi->flags);

  if (fd != -1)
    {
      FileHandle *fh = new FileHandle;
      fh->fd = fd;
      fh->special_file = FileHandle::NONE;
      fh->open_for_write = open_for_write;
      fi->fh = reinterpret_cast<uint64_t> (fh);
      return 0;
    }
  else
    {
      return -errno;
    }
}

static int
bfsync_release (const char *path, struct fuse_file_info *fi)
{
  FileHandle *fh = reinterpret_cast<FileHandle *> (fi->fh);

  FSLock lock (fh->open_for_write ? FSLock::WRITE : FSLock::READ);

  if (fh->fd != -1)
    close (fh->fd);
  delete fh;
  return 0;
}

static int
bfsync_read (const char *path, char *buf, size_t size, off_t offset,
             struct fuse_file_info *fi)
{
  FSLock lock (FSLock::READ);

  debug ("read (\"%s\")\n", path);

  FileHandle *fh = reinterpret_cast<FileHandle *> (fi->fh);

  ssize_t bytes_read = 0;

  if (fh->fd != -1)
    bytes_read = pread (fh->fd, buf, size, offset);

  if (fh->special_file == FileHandle::INFO)
    {
      string info = get_info();
      if (offset < (off_t) info.size())
        {
          bytes_read = size;
          if (offset + bytes_read > (off_t) info.size())
            bytes_read = info.size() - offset;
          memcpy (buf, &info[offset], bytes_read);
        }
      else
        {
          bytes_read = 0;
        }
    }

  return bytes_read;
}

static int
bfsync_write (const char *path_arg, const char *buf, size_t size, off_t offset,
              struct fuse_file_info *fi)
{
  string path = path_arg;

  FSLock lock (FSLock::WRITE);
  Context ctx;
  int version = version_map_path (path);
  if (version > 0 || bfsyncfs_read_only)
    return -EROFS;

  FileHandle *fh = reinterpret_cast<FileHandle *> (fi->fh);

  ssize_t bytes_written = 0;

  if (fh->fd != -1)
    {
      bytes_written = pwrite (fh->fd, buf, size, offset);
      if (bytes_written > 0)
        {
          IFPStatus ifp;
          INodePtr inode = inode_from_path (ctx, path, ifp);
          if (inode)
            inode.update()->set_mtime_ctime_now();
        }
    }

  return bytes_written;
}

static int
bfsync_mknod (const char *path_arg, mode_t mode, dev_t dev)
{
  string path = path_arg;

  FSLock lock (FSLock::WRITE);
  Context ctx;
  int version = version_map_path (path);
  if (version > 0 || bfsyncfs_read_only)
    return -EROFS;

  IFPStatus ifp;
  INodePtr dir_inode = inode_from_path (ctx, get_dirname (path), ifp);
  if (!dir_inode)
    {
      if (ifp == IFP_ERR_NOENT)
        return -ENOENT;
      if (ifp == IFP_ERR_PERM)
        return -EACCES;
    }
  if (!dir_inode->search_perm_ok (ctx) || !dir_inode->write_perm_ok (ctx))
    return -EACCES;

  INodePtr inode (ctx, path_arg);  // create new inode

  inode.update()->mode = mode & ~S_IFMT;

  if (S_ISREG (mode))
    {
      string filename = inode.update()->gen_new_file_path();
      int rc = mknod (filename.c_str(), 0600, dev);
      if (rc == 0)
        {
          inode.update()->type = FILE_REGULAR;
          inode.update()->hash = "new";
        }
      else
        {
          return -errno;
        }
    }
  else if (S_ISFIFO (mode))
    {
      inode.update()->type = FILE_FIFO;
    }
  else if (S_ISSOCK (mode))
    {
      inode.update()->type = FILE_SOCKET;
    }
  else if (S_ISBLK (mode))
    {
      inode.update()->type = FILE_BLOCK_DEV;
      inode.update()->major = major (dev);
      inode.update()->minor = minor (dev);
    }
  else if (S_ISCHR (mode))
    {
      inode.update()->type = FILE_CHAR_DEV;
      inode.update()->major = major (dev);
      inode.update()->minor = minor (dev);
    }
  else
    {
      return -ENOENT;
    }

  dir_inode.update()->set_mtime_ctime_now();
  dir_inode.update()->add_link (ctx, inode, get_basename (path));
  return 0;
}

static int
bfsync_create (const char *path_arg, mode_t mode, struct fuse_file_info *fi)
{
  /* create the file */
  int mknod_result = bfsync_mknod (path_arg, mode | S_IFREG, 0);
  if (mknod_result != 0)
    return mknod_result;

  /* create ok, now we can open (since mknod did all checks, we'll "just" open the file) */
  string path = path_arg;

  FSLock lock (FSLock::WRITE);
  Context ctx;

  IFPStatus ifp;
  INodePtr  inode = inode_from_path (ctx, path, ifp);
  if (!inode)
    {
      if (ifp == IFP_ERR_NOENT)
        return -ENOENT;
      if (ifp == IFP_ERR_PERM)
        return -EACCES;
    }

  inode.update()->copy_on_write();

  // open "real" file
  fi->flags &= ~O_CREAT;

  string filename = inode->file_path();
  int fd = open (filename.c_str(), fi->flags);

  if (fd != -1)
    {
      FileHandle *fh = new FileHandle;
      fh->fd = fd;
      fh->special_file = FileHandle::NONE;
      fh->open_for_write = true;
      fi->fh = reinterpret_cast<uint64_t> (fh);
      return 0;
    }
  else
    {
      return -errno;
    }
}


int
bfsync_chmod (const char *name_arg, mode_t mode)
{
  string name = name_arg;

  FSLock lock (FSLock::WRITE);
  Context ctx;
  int version = version_map_path (name);
  if (version > 0 || bfsyncfs_read_only)
    return -EROFS;

  IFPStatus ifp;
  INodePtr inode = inode_from_path (ctx, name, ifp);
  if (!inode)
    {
      if (ifp == IFP_ERR_NOENT)
        return -ENOENT;
      if (ifp == IFP_ERR_PERM)
        return -EACCES;
    }

  if (ctx.fc->uid != 0 && ctx.fc->uid != inode->uid)
    return -EPERM;

  if (ctx.fc->uid != 0 && ctx.fc->gid != inode->gid)
    mode &= ~S_ISGID;

  inode.update()->mode = mode;
  inode.update()->set_ctime_now();
  return 0;
}

int
bfsync_chown (const char *name_arg, uid_t uid, gid_t gid)
{
  string name = name_arg;

  FSLock lock (FSLock::WRITE);
  Context ctx;
  int version = version_map_path (name);
  if (version > 0 || bfsyncfs_read_only)
    return -EROFS;

  IFPStatus ifp;
  INodePtr inode = inode_from_path (ctx, name, ifp);
  if (!inode)
    {
      if (ifp == IFP_ERR_NOENT)
        return -ENOENT;
      if (ifp == IFP_ERR_PERM)
        return -EACCES;
    }

  uid_t context_uid = ctx.fc->uid;
  bool  root_user = (context_uid == 0);

  if (inode->uid == uid)   // check if this is a nop (change uid to same value)
    uid = -1;
  if (inode->gid == gid)   // check if this is a nop (change uid to same value)
    gid = -1;

  if (uid != -1 && !root_user)    // only root can change ownership
    return -EPERM;

  if (gid != -1 && !root_user)
    {
      if (inode->uid != context_uid) // chgrp only allowed if we own the file (or if we're root)
        return -EPERM;

      // user may change the group to any group he is member of, if he owns the file
      vector<gid_t> groups (1);
      size_t n_groups = fuse_getgroups (groups.size(), &groups[0]);
      groups.resize (n_groups);
      n_groups = fuse_getgroups (groups.size(), &groups[0]);
      if (n_groups != groups.size())
        return -EIO;

      bool can_chown = false;
      for (vector<gid_t>::iterator gi = groups.begin(); gi != groups.end(); gi++)
        {
          if (gid == *gi)
            {
              can_chown = true;
              break;
            }
        }
      if (!can_chown)
        return -EPERM;
    }

  if (!root_user)   // clear setuid/setgid bits for non-root chown
    inode.update()->mode &= ~(S_ISUID | S_ISGID);

  if (uid != -1)
    inode.update()->uid = uid;

  if (gid != -1)
    inode.update()->gid = gid;

  inode.update()->set_ctime_now();
  return 0;
}

int
bfsync_utimens (const char *name_arg, const struct timespec times[2])
{
  string name = name_arg;

  FSLock lock (FSLock::WRITE);
  Context ctx;
  int version = version_map_path (name);
  if (version > 0 || bfsyncfs_read_only)
    return -EROFS;

  IFPStatus ifp;
  INodePtr inode = inode_from_path (ctx, name, ifp);
  if (!inode)
    {
      if (ifp == IFP_ERR_NOENT)
        return -ENOENT;
      if (ifp == IFP_ERR_PERM)
        return -EACCES;
    }

  inode.update()->mtime    = times[1].tv_sec;
  inode.update()->mtime_ns = times[1].tv_nsec;
  inode.update()->set_ctime_now();

  return 0;
}

int
bfsync_truncate (const char *name_arg, off_t off)
{
  string name = name_arg;

  FSLock lock (FSLock::WRITE);
  Context ctx;
  int version = version_map_path (name);
  if (version > 0 || bfsyncfs_read_only)
    return -EROFS;

  IFPStatus ifp;
  INodePtr inode = inode_from_path (ctx, name, ifp);
  if (!inode)
    {
      if (ifp == IFP_ERR_NOENT)
        return -ENOENT;
      if (ifp == IFP_ERR_PERM)
        return -EACCES;
    }

  if (!inode->write_perm_ok (ctx))
    return -EACCES;

  inode.update()->copy_on_write();

  int rc = truncate (inode->file_path().c_str(), off);
  if (rc == 0)
    {
      inode.update()->set_mtime_ctime_now();
      return 0;
    }
  return -errno;
}

// FIXME: should check that name is not directory
static int
bfsync_unlink (const char *name_arg)
{
  string name = name_arg;
  FSLock lock (FSLock::WRITE);
  Context ctx;
  int version = version_map_path (name);
  if (version > 0 || bfsyncfs_read_only)
    return -EROFS;

  IFPStatus ifp;
  INodePtr inode_dir = inode_from_path (ctx, get_dirname (name), ifp);
  if (!inode_dir)
    {
      if (ifp == IFP_ERR_NOENT)
        return -ENOENT;
      if (ifp == IFP_ERR_PERM)
        return -EACCES;
    }
  if (!inode_dir->search_perm_ok (ctx) || !inode_dir->write_perm_ok (ctx))
    return -EACCES;

  INodePtr inode = inode_from_path (ctx, name, ifp);
  if (!inode)
    {
      if (ifp == IFP_ERR_NOENT)
        return -ENOENT;
      if (ifp == IFP_ERR_PERM)
        return -EACCES;
    }

  // sticky directory
  if (inode_dir->mode & S_ISVTX)
    {
      const uid_t uid = ctx.fc->uid;

      if (uid != 0 && inode_dir->uid != uid && inode->uid != uid)
        return -EACCES;
    }

  string filename = get_basename (name);
  if (!inode_dir.update()->unlink (ctx, filename))
    return -ENOENT;

  inode.update()->set_ctime_now();
  inode_dir.update()->set_mtime_ctime_now();
  return 0;
}

static int
bfsync_mkdir (const char *path_arg, mode_t mode)
{
  string path = path_arg;

  FSLock lock (FSLock::WRITE);
  Context ctx;
  int version = version_map_path (path);
  if (version > 0 || bfsyncfs_read_only)
    return -EROFS;

  IFPStatus ifp;
  INodePtr inode_dir = inode_from_path (ctx, get_dirname (path), ifp);
  if (!inode_dir)
    {
      if (ifp == IFP_ERR_NOENT)
        return -ENOENT;
      if (ifp == IFP_ERR_PERM)
        return -EACCES;
    }

  if (!inode_dir->write_perm_ok (ctx))
    return -EACCES;

  INodePtr inode (ctx, path_arg);  // create new inode

  inode.update()->type = FILE_DIR;
  inode.update()->mode = mode;

  inode_dir.update()->add_link (ctx, inode, get_basename (path));
  inode_dir.update()->set_mtime_ctime_now();
  return 0;
}

// FIXME: should check that name is a directory
static int
bfsync_rmdir (const char *name_arg)
{
  string name = name_arg;

  FSLock lock (FSLock::WRITE);
  Context ctx;
  int version = version_map_path (name);
  if (version > 0 || bfsyncfs_read_only)
    return -EROFS;

  IFPStatus ifp;
  INodePtr inode_dir = inode_from_path (ctx, get_dirname (name), ifp);
  if (!inode_dir)
    {
      if (ifp == IFP_ERR_NOENT)
        return -ENOENT;
      if (ifp == IFP_ERR_PERM)
        return -EACCES;
    }
  if (!inode_dir->search_perm_ok (ctx) || !inode_dir->write_perm_ok (ctx))
    return -EACCES;

  INodePtr inode = inode_from_path (ctx, name, ifp);
  if (!inode)
    {
      if (ifp == IFP_ERR_NOENT)
        return -ENOENT;
      if (ifp == IFP_ERR_PERM)
        return -EACCES;
    }

  // sticky directory
  if (inode_dir->mode & S_ISVTX)
    {
      const uid_t uid = ctx.fc->uid;

      if (uid != 0 && inode_dir->uid != uid && inode->uid != uid)
        return -EACCES;
    }

  // check that dir is in fact empty
  vector<string> entries;
  if (read_dir_contents (ctx, name, entries))
    if (!entries.empty())
      return -ENOTEMPTY;

  string dirname = get_basename (name);
  if (!inode_dir.update()->unlink (ctx, dirname))
    return -ENOENT;

  inode_dir.update()->set_mtime_ctime_now();
  return 0;
}

static int
bfsync_rename (const char *old_path_arg, const char *new_path_arg)
{
  string old_path = old_path_arg;
  string new_path = new_path_arg;

  FSLock lock (FSLock::WRITE);
  Context ctx;

  int version;
  version = version_map_path (old_path);
  if (version > 0 || bfsyncfs_read_only)
    return -EROFS;

  version = version_map_path (new_path);
  if (version > 0)
    return -EROFS;

  IFPStatus ifp;
  INodePtr inode_old = inode_from_path (ctx, old_path, ifp);
  if (!inode_old)
    {
      if (ifp == IFP_ERR_NOENT)
        return -ENOENT;
      if (ifp == IFP_ERR_PERM)
        return -EACCES;
    }

  INodePtr inode_new = inode_from_path (ctx, new_path, ifp);
  if (inode_new && inode_new->type == FILE_DIR)
    {
      // check that dir is empty
      vector<string> entries;
      if (read_dir_contents (ctx, new_path, entries))
        if (!entries.empty())
          return -EEXIST;
    }


  INodePtr inode_old_dir = inode_from_path (ctx, get_dirname (old_path), ifp);
  if (!inode_old_dir->write_perm_ok (ctx))
    return -EACCES;

  // sticky old directory
  if (inode_old_dir->mode & S_ISVTX)
    {
      const uid_t uid = ctx.fc->uid;

      if (uid != 0 && inode_old_dir->uid != uid && inode_old->uid != uid)
        return -EACCES;
    }

  INodePtr inode_new_dir = inode_from_path (ctx, get_dirname (new_path), ifp);
  if (!inode_new_dir->write_perm_ok (ctx))
    return -EACCES;

  // sticky new directory
  if (inode_new && inode_new_dir->mode & S_ISVTX)
    {
      const uid_t uid = ctx.fc->uid;

      if (uid != 0 && inode_new_dir->uid != uid && inode_new->uid != uid)
        return -EACCES;
    }

  if (inode_new)   // rename-replace
    inode_new_dir.update()->unlink (ctx, get_basename (new_path));

  inode_new_dir.update()->add_link (ctx, inode_old, get_basename (new_path));
  inode_old_dir.update()->unlink (ctx, get_basename (old_path));
  inode_old.update()->set_ctime_now();

  return 0;
}

static int
bfsync_symlink (const char *from_arg, const char *to_arg)
{
  string from = from_arg;
  string to   = to_arg;

  FSLock lock (FSLock::WRITE);
  Context ctx;
  int version = version_map_path (to);
  if (version > 0 || bfsyncfs_read_only)
    return -EROFS;

  IFPStatus ifp;

  INodePtr dir_inode = inode_from_path (ctx, get_dirname (to), ifp);
  if (!dir_inode)
    {
      if (ifp == IFP_ERR_NOENT)
        return -ENOENT;
      if (ifp == IFP_ERR_PERM)
        return -EACCES;
    }
  if (!dir_inode->write_perm_ok (ctx))
    return -EACCES;

  INodePtr check_to = inode_from_path (ctx, to, ifp);
  if (check_to)
    return -EEXIST;

  INodePtr inode (ctx, to_arg);
  inode.update()->mode = 0777;
  inode.update()->type = FILE_SYMLINK;
  inode.update()->link = from;

  dir_inode.update()->add_link (ctx, inode, get_basename (to));
  dir_inode.update()->set_mtime_ctime_now();
  return 0;
}

static int
bfsync_readlink (const char *path_arg, char *buffer, size_t size)
{
  string path = path_arg;

  FSLock lock (FSLock::READ);
  Context ctx;
  int version = version_map_path (path);
  if (version > 0 || bfsyncfs_read_only)
    ctx.version = version;

  IFPStatus ifp;
  INodePtr inode = inode_from_path (ctx, path, ifp);
  if (!inode)
    {
      if (ifp == IFP_ERR_NOENT)
        return -ENOENT;
      if (ifp == IFP_ERR_PERM)
        return -EACCES;
    }

  if (inode->type != FILE_SYMLINK)
    return -EINVAL;

  size_t len = inode->link.size();

  if (len >= size)
    len = size - 1;
  memcpy (buffer, inode->link.c_str(), len);

  buffer[len] = 0;
  return 0;
}

static int
bfsync_link (const char *old_path_arg, const char *new_path_arg)
{
  string old_path = old_path_arg;
  string new_path = new_path_arg;

  FSLock lock (FSLock::WRITE);
  Context ctx;

  int version;
  version = version_map_path (old_path);
  if (version > 0 || bfsyncfs_read_only)
    return -EROFS;

  version = version_map_path (new_path);
  if (version > 0)
    return -EROFS;

  IFPStatus ifp;
  INodePtr inode_old = inode_from_path (ctx, old_path, ifp);
  if (!inode_old)
    {
      if (ifp == IFP_ERR_NOENT)
        return -ENOENT;
      if (ifp == IFP_ERR_PERM)
        return -EACCES;
    }

  INodePtr inode_new = inode_from_path (ctx, new_path, ifp);
  if (inode_new)
    return -EEXIST;

  INodePtr inode_new_dir = inode_from_path (ctx, get_dirname (new_path), ifp);
  if (!inode_new_dir)
    {
      if (ifp == IFP_ERR_NOENT)
        return -ENOENT;
      if (ifp == IFP_ERR_PERM)
        return -EACCES;
    }
  if (!inode_new_dir->search_perm_ok (ctx) || !inode_new_dir->write_perm_ok (ctx))
    return -EACCES;

  inode_new_dir.update()->add_link (ctx, inode_old, get_basename (new_path));
  inode_new_dir.update()->set_mtime_ctime_now();
  inode_old.update()->set_ctime_now();
  return 0;
}


Server server;

static void*
bfsync_init (struct fuse_conn_info *conn)
{
  INodeRepo::the()->bdb->register_pid();

  conn->max_readahead = 10 * 128 * 1024;
  conn->max_write = 128 * 1024;

  conn->capable = FUSE_CAP_BIG_WRITES;
  conn->want    = FUSE_CAP_BIG_WRITES;

  server.start_thread();

  struct fuse_context* context = fuse_get_context();
  return context->private_data;
}

static struct fuse_operations bfsync_oper = { NULL, };

void
exit_usage()
{
  printf ("usage: bfsyncfs [ -d ] repo mount_point\n");
  exit (1);
}

void
check_version_compat_or_die (const vector<string>& info_version)
{
  string vstr = "";
  if (info_version.size() == 1)
    {
      vstr = info_version[0];
    }
  if (vstr != VERSION)
    {
      fprintf (stderr, "bfsyncfs: incompatible repository version, can not mount\n");
      fprintf (stderr, "          need repository version '%s', got repository version '%s'\n", VERSION, vstr.c_str());
      exit (1);
    }
}

static string
make_absolute_path (const string& path)
{
  if (!g_path_is_absolute (path.c_str()))
    return g_get_current_dir() + string (G_DIR_SEPARATOR + path);
  else
    return path;
}

}

int
bfsyncfs_main (int argc, char **argv)
{
  try
    {
      boost::program_options::options_description desc ("Allowed options");
      desc.add_options()
        ("help",                                                  "produce help message")
        ("foreground,f",                                          "run as foreground process")
        ("all,a",                                                 "allow all users to access filesystem")
        ("group,g", boost::program_options::value<string>(),      "set bfsync group")
        ("debug,d",                                               "enable debug mode")
        ("cache-attributes,c",                                    "enable attribute cacheing");

      boost::program_options::options_description hidden ("Hidden options");
      hidden.add_options()
        ("repo-path", boost::program_options::value<string>(),    "repository path")
        ("mount-point", boost::program_options::value<string>(),  "mount point");

      boost::program_options::positional_options_description positional_options;
      positional_options.add ("repo-path", 1);
      positional_options.add ("mount-point", 1);

      boost::program_options::options_description cmdline_options;
      cmdline_options.add (desc).add (hidden);

      boost::program_options::variables_map vm;
      boost::program_options::store (
        boost::program_options::command_line_parser (argc, argv).options (cmdline_options).positional (positional_options).run(),
        vm);
      boost::program_options::notify (vm);

      // flags
      options.mount_fg = vm.count ("foreground") > 0;
      options.mount_all = vm.count ("all") > 0;
      options.mount_debug = vm.count ("debug") > 0;
      options.cache_attributes = vm.count ("cache-attributes") > 0;

      // other options
      if (vm.count ("group"))
        options.bfsync_group = vm["group"].as<string>();

      if (vm.count ("repo-path"))
        options.repo_path = make_absolute_path (vm["repo-path"].as<string>());

      if (vm.count ("mount-point"))
        options.mount_point = make_absolute_path (vm["mount-point"].as<string>());

      if (vm.count ("help"))
        {
          std::cout << desc << "\n";
          return 1;
        }
    }
  catch (boost::program_options::error& e)
    {
      printf ("ERROR: %s\n", e.what());
      return 1;
    }

  if (options.mount_fg)
    printf ("mount_fg\n");
  if (options.mount_all)
    printf ("mount_all\n");
  if (options.mount_debug)
    printf ("mount_debug\n");
  if (options.cache_attributes)
    printf ("cache_attributes\n");
  if (options.bfsync_group != "")
    printf ("group='%s'\n", options.bfsync_group.c_str());
  if (options.repo_path != "")
    printf ("repo_path='%s'\n", options.repo_path.c_str());
  if (options.mount_point != "")
    printf ("mount_point='%s'\n", options.mount_point.c_str());

  exit (0);

  CfgParser repo_cfg_parser;
  if (!repo_cfg_parser.parse (options.repo_path + "/config"))
    {
      printf ("bfsyncfs: parse error in repo config:\n%s\n", repo_cfg_parser.error().c_str());
      exit (1);
    }
  map<string, vector<string> > cfg_values = repo_cfg_parser.values();
  const vector<string>& use_uid_gid = cfg_values["use-uid-gid"];
  options.use_uid_gid = false;
  if (use_uid_gid.size() == 1)
    {
      if (use_uid_gid[0] == "1")
        options.use_uid_gid = true;
    }

  const vector<string>& cache_size = cfg_values["cache-size"];
  options.cache_size_mb = 0;
  if (cache_size.size() == 1)
    {
      options.cache_size_mb = atoi (cache_size[0].c_str());
    }
  if (options.cache_size_mb == 0)
    {
      printf ("bfsyncfs: bad cache-size setting in config file\n");
      exit (1);
    }

  CfgParser repo_info_parser;
  if (!repo_info_parser.parse (options.repo_path + "/info"))
    {
      printf ("bfsyncfs: parse error in repo info:\n%s\n", repo_info_parser.error().c_str());
      exit (1);
    }
  map<string, vector<string> > info_values = repo_info_parser.values();
  const vector<string>& repo_type = info_values["repo-type"];
  bool repo_type_ok = false;
  if (repo_type.size() == 1)
    {
      if (repo_type[0] == "store")
        repo_type_ok = true;
      else
        printf ("bfsyncfs: repo-type info setting is '%s'\n", repo_type[0].c_str());
    }
  if (!repo_type_ok)
    {
      printf ("bfsyncfs: bad repository type, cannot mount\n");
      exit (1);
    }

  check_version_compat_or_die (info_values["version"]);

  special_files.info  = "repo-type mount;\n";
  special_files.info += "repo-path \"" + options.repo_path + "\";\n";
  special_files.info += "mount-point \"" + options.mount_point + "\";\n";
  special_files.info += "use-uid-gid " + string (options.use_uid_gid ? "1" : "0") + ";\n";

  debug ("starting bfsyncfs; info = \n{\n%s}\n", special_files.info.c_str());

  if (!server.init_socket (options.repo_path))
    {
      printf ("bfsyncfs: initialization of socket failed\n");
      exit (1);
    }

  /* read */
  bfsync_oper.getattr  = bfsync_getattr;
  bfsync_oper.opendir  = bfsync_opendir;
  bfsync_oper.readdir  = bfsync_readdir;
  bfsync_oper.read     = bfsync_read;
  bfsync_oper.readlink = bfsync_readlink;
  bfsync_oper.init     = bfsync_init;

  /* read/write */
  bfsync_oper.open     = bfsync_open;

  /* write */
  bfsync_oper.create   = bfsync_create;
  bfsync_oper.mknod    = bfsync_mknod;
  bfsync_oper.chown    = bfsync_chown;
  bfsync_oper.chmod    = bfsync_chmod;
  bfsync_oper.utimens  = bfsync_utimens;
  bfsync_oper.truncate = bfsync_truncate;
  bfsync_oper.release  = bfsync_release;
  bfsync_oper.write    = bfsync_write;
  bfsync_oper.unlink   = bfsync_unlink;
  bfsync_oper.mkdir    = bfsync_mkdir;
  bfsync_oper.rename   = bfsync_rename;
  bfsync_oper.symlink  = bfsync_symlink;
  bfsync_oper.rmdir    = bfsync_rmdir;
  bfsync_oper.link     = bfsync_link;

  char *my_argv[32] = { NULL, };
  int my_argc = 0;

  my_argv[my_argc++] = g_strdup ("bfsyncfs");
  my_argv[my_argc++] = g_strdup (options.mount_point.c_str());
  if (options.mount_debug)
    my_argv[my_argc++] = g_strdup ("-d");
  if (options.mount_fg)
    my_argv[my_argc++] = g_strdup ("-f");
  if (options.mount_all)
    my_argv[my_argc++] = g_strdup ("-oallow_other");
  if (!options.cache_attributes)
    my_argv[my_argc++] = g_strdup ("-oattr_timeout=0");
  my_argv[my_argc++] = g_strdup ("-ouse_ino");
  my_argv[my_argc] = NULL;

  BDB *bdb = bdb_open (options.repo_path, options.cache_size_mb, false);
  if (!bdb)
    {
      printf ("bfsyncfs: error opening bdb\n");
      return 1;
    }

  INodeRepo inode_repo (bdb);

  inode_repo.bdb->history()->read();

  // set readonly mode if there is a journal entry of some "running" operation
  bfsyncfs_update_read_only();
  if (bfsyncfs_read_only)
    {
      printf ("bfsyncfs: some operation did not complete, mounting readonly\n");
      printf ("bfsyncfs: use bfsync continue %s to fix this\n", options.repo_path.c_str());
    }

  int fuse_rc = fuse_main (my_argc, my_argv, &bfsync_oper, NULL);

  server.stop_thread();

  inode_repo.save_changes();
  inode_repo.delete_unused_inodes (INodeRepo::DM_ALL);

  if (!bdb->close())
    {
      printf ("bfsyncfs: can't close bdb\n");
    }

  // disabled timeprof output (too noisy for normal users)
  // printf ("%s\n", TimeProf::the()->result().c_str());
  return fuse_rc;
}
