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

#define FUSE_USE_VERSION 26

#include "bfinode.hh"
#include "bflink.hh"
#include "bfsyncserver.hh"
#include "bfsyncfs.hh"
#include <sqlite3.h>

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

using std::string;
using std::vector;
using std::set;
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

static FILE *bf_debug_file = NULL;

FILE*
debug_file()
{
  if (!bf_debug_file)
    bf_debug_file = fopen ("/tmp/bfsyncfs.log", "w");

  return bf_debug_file;
}

// "foo/bar" => [ "foo", "bar" ]
vector<string>
split_name (const string& xname)
{
  string name = xname + "/";
  vector<string> result;
  string s;

  for (size_t i = 0; i < name.size(); i++)
    {
      if (name[i] == '/')
        {
          if (!s.empty())
            {
              result.push_back (s);
              s.clear();
            }
        }
      else
        {
          s += name[i];
        }
    }
  return (result);
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

string
make_object_filename (const string& hash)
{
  if (hash.size() != 40)
    return "";
  return options.repo_path + "/objects/" + hash.substr (0, 2) + "/" + hash.substr (2);
}


vector<string>
split (const string& path)
{
  vector<string> result;

  string s;
  for (size_t i = 0; i < path.size(); i++)
    {
      if (path[i] == '/')
        {
          if (!s.empty())
            {
              result.push_back (s);
              s = "";
            }
        }
      else
        s += path[i];
    }
  if (!s.empty())
    result.push_back (s);
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
  int   reader_count;
  int   writer_count;
  int   reorg_count;
  int   rdonly_count;

  LockState();

  void lock (FSLock::LockType lock_type);
  void unlock (FSLock::LockType lock_type);
} lock_state;

LockState::LockState()
{
  reader_count = 0;
  writer_count = 0;
  reorg_count = 0;
  rdonly_count = 0;
}

void
LockState::lock (FSLock::LockType lock_type)
{
  mutex.lock();
  while (1)
    {
      bool got_lock = false;

      switch (lock_type)
        {
          /* READ is allowed if:
             - no other thread writes at the same time
             - no other thread performs data reorganization (like during commit) at the same time

             Its ok to read if the filesystem is in readonly mode, and its ok to read if another thread
             is also reading.
           */
          case FSLock::READ:
            if (writer_count == 0 && reorg_count == 0)
              {
                got_lock = true;
                reader_count++;
              }
            break;
          /* WRITE is allowed if:
             - no other thread reads at the same time
             - no other thread writes at the same time
             - no other thread performs data reorganization (like during commit) at the same time
             - the filesystem is not in readonly mode
           */
          case FSLock::WRITE:
            if (reader_count == 0 && writer_count == 0 && reorg_count == 0 && rdonly_count == 0)
              {
                got_lock = true;
                writer_count++;
              }
            break;
          /* REORG is allowed if:
             - no other thread reads at the same time
             - no other thread writes at the same time
             - no other thread performs data reorganization (like during commit) at the same time
             - the filesystem is in readonly mode

             Reorg is ok during readonly mode (although reorg writes to the disk, it doesn't
             change the contents of the filesystem, therefore its technically something different
             than write).
           */
          case FSLock::REORG:
            if (reader_count == 0 && writer_count == 0 && reorg_count == 0)
              {
                got_lock = true;
                reorg_count++;
                assert (rdonly_count == 1);
              }
            break;
          /* RDONLY (making the filesystem readonly) is allowed if:
             - no other thread writes at the same time
             - no other thread performs data reorganization (like during commit) at the same time
             - the filesystem is not in readonly mode

             Reads performed in other threads do not affect making the FS readonly.
           */
          case FSLock::RDONLY:
            if (writer_count == 0 && reorg_count == 0 && rdonly_count == 0)
              {
                got_lock = true;
                rdonly_count++;
              }
            break;
          default:
            g_assert_not_reached();
        }
      if (got_lock)
        break;
      cond.wait (mutex);
    }
  mutex.unlock();
}

void
LockState::unlock (FSLock::LockType lock_type)
{
  mutex.lock();
  if (lock_type == FSLock::READ)
    {
      assert (reader_count > 0);
      reader_count--;
    }
  if (lock_type == FSLock::WRITE)
    {
      assert (writer_count > 0);
      writer_count--;
    }
  if (lock_type == FSLock::REORG)
    {
      assert (reorg_count > 0);
      reorg_count--;
    }
  if (lock_type == FSLock::RDONLY)
    {
      assert (rdonly_count > 0);
      rdonly_count--;
    }
  cond.broadcast();
  mutex.unlock();
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

enum IFPStatus { IFP_OK, IFP_ERR_NOENT, IFP_ERR_PERM };

INodePtr
inode_from_path (const string& path, IFPStatus& status)
{
  INodePtr inode ("root");
  printf ("inode_from_path (%s)\n", path.c_str());

  vector<string> path_vec = split (path);
  for (vector<string>::iterator pi = path_vec.begin(); pi != path_vec.end(); pi++)
    {
      if (!inode->search_perm_ok())
        {
          status = IFP_ERR_PERM;
          return INodePtr::null();
        }
      vector<LinkPtr> children = inode->children();

      bool found = false;
      for (vector<LinkPtr>::iterator ci = children.begin(); ci != children.end(); ci++)
        {
          const LinkPtr& child_link = *ci;

          if (child_link->name == *pi && !child_link->deleted)
            {
              inode = INodePtr (child_link->inode_id);
              found = true;
              break;
            }
        }
      if (!found)
        {
          printf ("  inode = NULL\n");
          status = IFP_ERR_NOENT;
          return INodePtr::null();
        }
    }

  printf ("  inode = %s\n", inode->id.c_str());
  status = IFP_OK;
  return inode;
}

static int
bfsync_getattr (const char *path_arg, struct stat *stbuf)
{
  const string path = path_arg;

  FSLock lock (FSLock::READ);

  if (path == "/.bfsync")
    {
      memset (stbuf, 0, sizeof (struct stat));
      stbuf->st_mode = 0755 | S_IFDIR;
      stbuf->st_uid  = getuid();
      stbuf->st_gid  = getgid();
      return 0;
    }
  else if (path == "/.bfsync/info")
    {
      memset (stbuf, 0, sizeof (struct stat));
      stbuf->st_mode = 0644 | S_IFREG;
      stbuf->st_uid  = getuid();
      stbuf->st_gid  = getgid();
      stbuf->st_size = special_files.info.size();
      return 0;
    }
  IFPStatus ifp;
  INodePtr  inode = inode_from_path (path, ifp);
  if (!inode)
    {
      if (ifp == IFP_ERR_NOENT)
        return -ENOENT;
      if (ifp == IFP_ERR_PERM)
        return -EACCES;
    }

  int inode_mode = inode->mode & ~S_IFMT;

  memset (stbuf, 0, sizeof (struct stat));
  stbuf->st_uid          = inode->uid;
  stbuf->st_gid          = inode->gid;
  stbuf->st_mtime        = inode->mtime;
  stbuf->st_mtim.tv_nsec = inode->mtime_ns;
  stbuf->st_ctime        = inode->ctime;
  stbuf->st_ctim.tv_nsec = inode->ctime_ns;
  stbuf->st_atim         = stbuf->st_mtim;    // we don't track atime, so set atime == mtime
  stbuf->st_nlink        = inode->nlink;
  if (inode->type == FILE_REGULAR)
    {
      if (inode->hash == "new")
        {
          // take size from new file
          struct stat new_stat;
          string new_filename = options.repo_path + "/new/" + inode->id;
          lstat (new_filename.c_str(), &new_stat);

          stbuf->st_size = new_stat.st_size;
        }
      else
        {
          stbuf->st_size = inode->size;
        }
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
read_dir_contents (const string& path, vector<string>& entries)
{
  bool            dir_ok = true;

  IFPStatus ifp;
  INodePtr inode = inode_from_path (path, ifp);
  if (inode)
    {
      vector<LinkPtr> children = inode->children();
      for (vector<LinkPtr>::iterator ci = children.begin(); ci != children.end(); ci++)
        {
          if (!(*ci)->deleted)
            entries.push_back ((*ci)->name);
        }
    }
  if (path == "/")
    {
      entries.push_back (".bfsync");
    }
  else if (path == "/.bfsync")
    {
      entries.push_back ("info");
    }

  return dir_ok;
}

static int
bfsync_opendir (const char *path, struct fuse_file_info *fi)
{
  IFPStatus ifp;
  INodePtr dir_inode = inode_from_path (path, ifp);
  if (!dir_inode)
    {
      if (ifp == IFP_ERR_NOENT)
        return -ENOENT;
      if (ifp == IFP_ERR_PERM)
        return -EACCES;
    }

  if (!dir_inode->search_perm_ok())
    return -EACCES;

  if (!dir_inode->read_perm_ok())
    return -EACCES;

  return 0;
}

static int
bfsync_readdir (const char *path, void *buf, fuse_fill_dir_t filler,
                off_t offset, struct fuse_file_info *fi)
{
  FSLock lock (FSLock::READ);

  debug ("readdir (\"%s\")\n", path);

  (void) offset;
  (void) fi;

  vector<string> entries;
  if (read_dir_contents (path, entries))
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
bfsync_open (const char *path, struct fuse_file_info *fi)
{
  int accmode = fi->flags & O_ACCMODE;
  // can both be true (for O_RDWR)
  bool open_for_write = (accmode == O_WRONLY || accmode == O_RDWR);
  bool open_for_read  = (accmode == O_RDONLY || accmode == O_RDWR);

  FSLock lock (open_for_write ? FSLock::WRITE : FSLock::READ);

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
  INodePtr  inode = inode_from_path (path, ifp);
  if (!inode)
    {
      if (ifp == IFP_ERR_NOENT)
        return -ENOENT;
      if (ifp == IFP_ERR_PERM)
        return -EACCES;
    }
  if (open_for_write && !inode->write_perm_ok())
    return -EACCES;

  if (open_for_read && !inode->read_perm_ok())
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
      const string& info = special_files.info;
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
bfsync_write (const char *path, const char *buf, size_t size, off_t offset,
              struct fuse_file_info *fi)
{
  FSLock lock (FSLock::WRITE);

  FileHandle *fh = reinterpret_cast<FileHandle *> (fi->fh);

  ssize_t bytes_written = 0;

  if (fh->fd != -1)
    {
      bytes_written = pwrite (fh->fd, buf, size, offset);
      if (bytes_written > 0)
        {
          IFPStatus ifp;
          INodePtr inode = inode_from_path (path, ifp);
          if (inode)
            inode.update()->set_mtime_ctime_now();
        }
    }

  return bytes_written;
}

static int
bfsync_mknod (const char *path, mode_t mode, dev_t dev)
{
  FSLock lock (FSLock::WRITE);

  IFPStatus ifp;
  INodePtr dir_inode = inode_from_path (get_dirname (path), ifp);
  if (!dir_inode)
    {
      if (ifp == IFP_ERR_NOENT)
        return -ENOENT;
      if (ifp == IFP_ERR_PERM)
        return -EACCES;
    }
  if (!dir_inode->search_perm_ok() || !dir_inode->write_perm_ok())
    return -EACCES;

  INodePtr inode (fuse_get_context());  // create new inode

  inode.update()->mode = mode & ~S_IFMT;

  if (S_ISREG (mode))
    {
      string filename = options.repo_path + "/new/" + inode->id;
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
  dir_inode.update()->add_link (inode, get_basename (path));
  return 0;
}

int
bfsync_chmod (const char *name, mode_t mode)
{
  FSLock lock (FSLock::WRITE);

  IFPStatus ifp;
  INodePtr inode = inode_from_path (name, ifp);
  if (!inode)
    {
      if (ifp == IFP_ERR_NOENT)
        return -ENOENT;
      if (ifp == IFP_ERR_PERM)
        return -EACCES;
    }

  if (fuse_get_context()->uid != 0 && fuse_get_context()->uid != inode->uid)
    return -EPERM;

  if (fuse_get_context()->uid != 0 && fuse_get_context()->gid != inode->gid)
    mode &= ~S_ISGID;

  inode.update()->mode = mode;
  inode.update()->set_ctime_now();
  return 0;
}

int
bfsync_chown (const char *name, uid_t uid, gid_t gid)
{
  FSLock lock (FSLock::WRITE);

  IFPStatus ifp;
  INodePtr inode = inode_from_path (name, ifp);
  if (!inode)
    {
      if (ifp == IFP_ERR_NOENT)
        return -ENOENT;
      if (ifp == IFP_ERR_PERM)
        return -EACCES;
    }

  uid_t context_uid = fuse_get_context()->uid;
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
bfsync_utimens (const char *name, const struct timespec times[2])
{
  FSLock lock (FSLock::WRITE);

  IFPStatus ifp;
  INodePtr inode = inode_from_path (name, ifp);
  if (!inode)
    {
      if (ifp == IFP_ERR_NOENT)
        return -ENOENT;
      if (ifp == IFP_ERR_PERM)
        return -EACCES;
    }

  inode.update()->mtime    = times[1].tv_sec;
  inode.update()->mtime_ns = times[1].tv_nsec;

  return 0;
}

int
bfsync_truncate (const char *name, off_t off)
{
  FSLock lock (FSLock::WRITE);

  IFPStatus ifp;
  INodePtr inode = inode_from_path (name, ifp);
  if (!inode)
    {
      if (ifp == IFP_ERR_NOENT)
        return -ENOENT;
      if (ifp == IFP_ERR_PERM)
        return -EACCES;
    }

  if (!inode->write_perm_ok())
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
bfsync_unlink (const char *name)
{
  FSLock lock (FSLock::WRITE);

  IFPStatus ifp;
  INodePtr inode_dir = inode_from_path (get_dirname (name), ifp);
  if (!inode_dir)
    {
      if (ifp == IFP_ERR_NOENT)
        return -ENOENT;
      if (ifp == IFP_ERR_PERM)
        return -EACCES;
    }
  if (!inode_dir->search_perm_ok() || !inode_dir->write_perm_ok())
    return -EACCES;

  INodePtr inode = inode_from_path (name, ifp);
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
      const uid_t uid = fuse_get_context()->uid;

      if (uid != 0 && inode_dir->uid != uid && inode->uid != uid)
        return -EACCES;
    }

  string filename = get_basename (name);
  if (!inode_dir.update()->unlink (filename))
    return -ENOENT;

  inode.update()->set_ctime_now();
  inode_dir.update()->set_mtime_ctime_now();
  return 0;
}

static int
bfsync_mkdir (const char *path, mode_t mode)
{
  FSLock lock (FSLock::WRITE);

  printf ("mkdir: %s\n", path);
  IFPStatus ifp;
  INodePtr inode_dir = inode_from_path (get_dirname (path), ifp);
  if (!inode_dir)
    {
      if (ifp == IFP_ERR_NOENT)
        return -ENOENT;
      if (ifp == IFP_ERR_PERM)
        return -EACCES;
    }

  if (!inode_dir->write_perm_ok())
    return -EACCES;

  printf ("inode is %s\n", inode_dir->id.c_str());

  INodePtr inode (fuse_get_context());  // create new inode

  inode.update()->type = FILE_DIR;
  inode.update()->mode = mode;

  inode_dir.update()->add_link (inode, get_basename (path));
  inode_dir.update()->set_mtime_ctime_now();
  return 0;
}

// FIXME: should check that name is a directory
static int
bfsync_rmdir (const char *name)
{
  FSLock lock (FSLock::WRITE);

  IFPStatus ifp;
  INodePtr inode_dir = inode_from_path (get_dirname (name), ifp);
  if (!inode_dir)
    {
      if (ifp == IFP_ERR_NOENT)
        return -ENOENT;
      if (ifp == IFP_ERR_PERM)
        return -EACCES;
    }
  if (!inode_dir->search_perm_ok() || !inode_dir->write_perm_ok())
    return -EACCES;

  INodePtr inode = inode_from_path (name, ifp);
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
      const uid_t uid = fuse_get_context()->uid;

      if (uid != 0 && inode_dir->uid != uid && inode->uid != uid)
        return -EACCES;
    }

  // check that dir is in fact empty
  vector<string> entries;
  if (read_dir_contents (name, entries))
    if (!entries.empty())
      return -ENOTEMPTY;

  string dirname = get_basename (name);
  if (!inode_dir.update()->unlink (dirname))
    return -ENOENT;

  inode_dir.update()->set_mtime_ctime_now();
  return 0;
}

static int
bfsync_rename (const char *old_path, const char *new_path)
{
  FSLock lock (FSLock::WRITE);

  IFPStatus ifp;
  INodePtr inode_old = inode_from_path (old_path, ifp);
  if (!inode_old)
    {
      if (ifp == IFP_ERR_NOENT)
        return -ENOENT;
      if (ifp == IFP_ERR_PERM)
        return -EACCES;
    }

  INodePtr inode_new = inode_from_path (new_path, ifp);

  INodePtr inode_old_dir = inode_from_path (get_dirname (old_path), ifp);
  if (!inode_old_dir->write_perm_ok())
    return -EACCES;

  // sticky directory
  if (inode_old_dir->mode & S_ISVTX)
    {
      const uid_t uid = fuse_get_context()->uid;

      if (uid != 0 && inode_old_dir->uid != uid && inode_old->uid != uid)
        return -EACCES;
    }

  INodePtr inode_new_dir = inode_from_path (get_dirname (new_path), ifp);
  if (!inode_new_dir->write_perm_ok())
    return -EACCES;

  if (inode_new)   // rename-replace
    inode_new_dir.update()->unlink (get_basename (new_path));

  inode_new_dir.update()->add_link (inode_old, get_basename (new_path));
  inode_old_dir.update()->unlink (get_basename (old_path));
  inode_old.update()->set_ctime_now();

  return 0;
}

static int
bfsync_symlink (const char *from, const char *to)
{
  FSLock lock (FSLock::WRITE);

  IFPStatus ifp;

  INodePtr dir_inode = inode_from_path (get_dirname (to), ifp);
  if (!dir_inode)
    {
      if (ifp == IFP_ERR_NOENT)
        return -ENOENT;
      if (ifp == IFP_ERR_PERM)
        return -EACCES;
    }
  if (!dir_inode->write_perm_ok())
    return -EACCES;

  INodePtr check_to = inode_from_path (to, ifp);
  if (check_to)
    return -EEXIST;

  INodePtr inode (fuse_get_context());
  inode.update()->mode = 0777;
  inode.update()->type = FILE_SYMLINK;
  inode.update()->link = from;

  dir_inode.update()->add_link (inode, get_basename (to));
  dir_inode.update()->set_mtime_ctime_now();
  return 0;
}

static int
bfsync_readlink (const char *path, char *buffer, size_t size)
{
  FSLock lock (FSLock::READ);

  IFPStatus ifp;
  INodePtr inode = inode_from_path (path, ifp);
  if (!inode)
    {
      if (ifp == IFP_ERR_NOENT)
        return -ENOENT;
      if (ifp == IFP_ERR_PERM)
        return -EACCES;
    }

  if (inode->type != FILE_SYMLINK)
    return -EINVAL;

  int len = inode->link.size();

  if (len >= size)
    len = size - 1;
  memcpy (buffer, inode->link.c_str(), len);

  buffer[len] = 0;
  return 0;
}

static int
bfsync_link (const char *old_path, const char *new_path)
{
  FSLock lock (FSLock::WRITE);

  IFPStatus ifp;
  INodePtr inode_old = inode_from_path (old_path, ifp);
  if (!inode_old)
    {
      if (ifp == IFP_ERR_NOENT)
        return -ENOENT;
      if (ifp == IFP_ERR_PERM)
        return -EACCES;
    }

  INodePtr inode_new = inode_from_path (new_path, ifp);
  if (inode_new)
    return -EEXIST;

  INodePtr inode_new_dir = inode_from_path (get_dirname (new_path), ifp);
  if (!inode_new_dir)
    {
      if (ifp == IFP_ERR_NOENT)
        return -ENOENT;
      if (ifp == IFP_ERR_PERM)
        return -EACCES;
    }
  if (!inode_new_dir->search_perm_ok() || !inode_new_dir->write_perm_ok())
    return -EACCES;

  inode_new_dir.update()->add_link (inode_old, get_basename (new_path));
  inode_new_dir.update()->set_mtime_ctime_now();
  inode_old.update()->set_ctime_now();
  return 0;
}


Server server;

static void*
bfsync_init (struct fuse_conn_info *conn)
{
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

}

int
main (int argc, char *argv[])
{
  string repo_path, mount_point;

  options.mount_debug = false;
  options.mount_all = false;
  options.mount_fg = false;

  int opt;
  while ((opt = getopt (argc, argv, "daf")) != -1)
    {
      switch (opt)
        {
          case 'd': options.mount_debug = true;
                    break;
          case 'f': options.mount_fg = true;
                    break;
          case 'a': options.mount_all = true;
                    break;
          default:  exit_usage();
                    exit (1);
        }
    }
  if (argc - optind != 2)
    {
      printf ("wrong number of arguments\n");
      exit_usage();
    }
  repo_path = argv[optind++];
  mount_point = argv[optind++];

  if (!g_path_is_absolute (repo_path.c_str()))
    repo_path = g_get_current_dir() + string (G_DIR_SEPARATOR + repo_path);

  if (!g_path_is_absolute (mount_point.c_str()))
    mount_point = g_get_current_dir() + string (G_DIR_SEPARATOR + mount_point);

  options.repo_path = repo_path;
  options.mount_point = mount_point;

  special_files.info  = "repo-type mount;\n";
  special_files.info += "repo-path \"" + repo_path + "\";\n";
  special_files.info += "mount-point \"" + mount_point + "\";\n";

  debug ("starting bfsyncfs; info = \n{\n%s}\n", special_files.info.c_str());

  if (!server.init_socket (repo_path))
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
  my_argv[my_argc++] = g_strdup ("-oattr_timeout=0");
  my_argv[my_argc] = NULL;


  string db_path = options.repo_path + "/db";
  int rc = sqlite_open (db_path);
  if (rc != SQLITE_OK)
    {
      printf ("bfsyncfs: error opening db: %d\n", rc);
      return 1;
    }
  int current_version = -1;
  sqlite3_stmt *stmt_ptr = NULL;
  string query = "SELECT * FROM history";
  rc = sqlite3_prepare_v2 (sqlite_db(), query.c_str(), query.size(), &stmt_ptr, NULL);

  if (rc != SQLITE_OK)
    {
      printf ("bfsyncfs: error running db query: %d\n", rc);
      return 1;
    }
  for (;;)
    {
      rc = sqlite3_step (stmt_ptr);
      if (rc != SQLITE_ROW)
        break;
      int version = sqlite3_column_int (stmt_ptr, 0);
      current_version = max (version, current_version);
    }
  if (rc != SQLITE_DONE)
    {
      printf ("bfsyncfs: stmt return %d\n", rc);
      return 1;
    }
  if (current_version == -1)
    {
      printf ("bfsyncfs: find current version in history table failed\n");
      return 1;
    }
  debug ("current version is %d\n", current_version);

  int fuse_rc = fuse_main (my_argc, my_argv, &bfsync_oper, NULL);

  INodeRepo::the()->save_changes();

  if (sqlite3_close (sqlite_db()) != SQLITE_OK)
    {
      printf ("bfsyncfs: can't close db\n");
    }
  return fuse_rc;
}
