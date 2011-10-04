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

#include "bfgitfile.hh"
#include "bfsyncserver.hh"
#include "bfsyncfs.hh"

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

using namespace BFSync;

namespace BFSync {

Options options;

Options*
Options::the()
{
  return &options;
}

enum FileStatus
{
  FS_NONE,
  FS_GIT,
  FS_CHANGED
};

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

// "foo/bar/bazz" => d_foo/d_bar/i_bazz
string
name2git_name (const string& name, int type)
{
  vector<string> path = split_name (name);
  string result;

  for (size_t i = 0; i < path.size(); i++)
    {
      if (i + 1 < path.size())    // not last element
        result += "d_" + path[i] + "/";
      else                        // last element
        {
          if (type == GIT_FILENAME)
            result += "i_" + path[i];
          else // dirname
            result += "d_" + path[i];
        }
    }
  return result;
}

string
get_dirname (const string& dirname)
{
  char *dirname_c = g_path_get_dirname (dirname.c_str());
  string result = dirname_c;
  g_free (dirname_c);

  return result;
}

FileStatus
file_status (const string& path)
{
  GitFilePtr gf (path);
  if (gf)
    {
      if (gf->hash == "new")
        return FS_CHANGED;
      else
        return FS_GIT;
    }
  return FS_NONE;
}

string
make_object_filename (const string& hash)
{
  if (hash.size() != 40)
    return "";
  return options.repo_path + "/objects/" + hash.substr (0, 2) + "/" + hash.substr (2);
}


string
file_path (const string& path)
{
  FileStatus fs = file_status (path);
  if (fs == FS_CHANGED)
    return options.repo_path + "/new" + path;
  if (fs == FS_GIT)
    {
      GitFilePtr gf (path);
      if (gf)
        return make_object_filename (gf->hash);
    }
  return "";
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

void
copy_dirs (const string& path)
{
  vector<string> dirs = split (path);
  if (dirs.empty())
    return;

  dirs.pop_back();

  string dir_path = options.repo_path + "/new";
  for (vector<string>::iterator di = dirs.begin(); di != dirs.end(); di++)
    {
      dir_path += "/" + *di;
      mkdir (dir_path.c_str(), 0755);
    }
}

void
copy_on_write (const string& path)
{
  if (file_status (path) == FS_GIT)
    {
      copy_dirs (path);

      GitFilePtr gf (path);
      if (gf)
        {
          string new_name = options.repo_path + "/new" + path;

          if (gf->type == FILE_REGULAR)
            {
              string old_name = file_path (path);

              int old_fd = open (old_name.c_str(), O_RDONLY);
              int new_fd = open (new_name.c_str(), O_WRONLY | O_CREAT, 0644);

              vector<unsigned char> buffer (4096);
              ssize_t read_bytes;
              while ((read_bytes = read (old_fd, &buffer[0], buffer.size())) > 0)
                {
                  write (new_fd, &buffer[0], read_bytes);
                }
              close (old_fd);
              close (new_fd);
            }
          gf.update()->hash = "new";
        }
    }
}

//------------ permission checks

bool
search_perm_check (const GitFilePtr& gf, int uid, int gid)
{
  printf ("search_perm_check (%s)\n", gf->git_filename.c_str());
  if (uid == 0)
    return true;

  if (uid == gf->uid)
    {
      if (gf->mode & S_IXUSR)
        return true;
    }

  if (gid == gf->gid)
    {
      if (gf->mode & S_IXGRP)
        return true;
    }

  if (gf->mode & S_IXOTH)
    return true;

  return false;
}

bool
search_perm_ok (const string& name)
{
  printf ("search_perm_ok/1 (%s)\n", name.c_str());
  string dir = get_dirname (name);
  if (dir == "/")
    return true;

  printf ("search_perm_ok/2 (%s)\n", dir.c_str());
  GitFilePtr git_file (dir);
  if (!git_file || !search_perm_check (git_file, fuse_get_context()->uid, fuse_get_context()->gid))
    return false;
  else
    return search_perm_ok (dir);
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

static int
bfsync_getattr (const char *path, struct stat *stbuf)
{
  debug ("getattr (\"%s\")\n", path);

  if (string (path) == "/")  // take attrs for / from git/files dir, since we have no own attrs stored for that dir
    {
      if (lstat ((options.repo_path + "/git/files/").c_str(), stbuf) == 0)
        return 0;
    }

  FSLock lock (FSLock::READ);

  GitFilePtr git_file (path);
  if (git_file)
    {
      int git_mode = git_file->mode & ~S_IFMT;

      memset (stbuf, 0, sizeof (struct stat));
      stbuf->st_uid          = git_file->uid;
      stbuf->st_gid          = git_file->gid;
      stbuf->st_mtime        = git_file->mtime;
      stbuf->st_mtim.tv_nsec = git_file->mtime_ns;
      stbuf->st_ctime        = git_file->ctime;
      stbuf->st_ctim.tv_nsec = git_file->ctime_ns;
      stbuf->st_atim         = stbuf->st_mtim;    // we don't track atime, so set atime == mtime
      if (git_file->type == FILE_REGULAR)
        {
          if (git_file->hash == "new")
            {
              // take size from new file
              struct stat new_stat;
              string new_filename = options.repo_path + "/new" + path;
              lstat (new_filename.c_str(), &new_stat);

              stbuf->st_size = new_stat.st_size;
            }
          else
            {
              stbuf->st_size = git_file->size;
            }
          stbuf->st_mode = git_mode | S_IFREG;
        }
      else if (git_file->type == FILE_SYMLINK)
        {
          stbuf->st_mode = git_mode | S_IFLNK;
          stbuf->st_size = git_file->link.size();
        }
      else if (git_file->type == FILE_DIR)
        {
          stbuf->st_mode = git_mode | S_IFDIR;
        }
      else if (git_file->type == FILE_FIFO)
        {
          stbuf->st_mode = git_mode | S_IFIFO;
        }
      else if (git_file->type == FILE_SOCKET)
        {
          stbuf->st_mode = git_mode | S_IFSOCK;
        }
      else if (git_file->type == FILE_BLOCK_DEV)
        {
          stbuf->st_mode = git_mode | S_IFBLK;
          stbuf->st_rdev = makedev (git_file->major, git_file->minor);
        }
      else if (git_file->type == FILE_CHAR_DEV)
        {
          stbuf->st_mode = git_mode | S_IFCHR;
          stbuf->st_rdev = makedev (git_file->major, git_file->minor);
        }
      return 0;
    }

  if (string (path) == "/.bfsync")
    {
      memset (stbuf, 0, sizeof (struct stat));
      stbuf->st_mode = 0755 | S_IFDIR;
      stbuf->st_uid  = getuid();
      stbuf->st_gid  = getgid();
      return 0;
    }
  else if (string (path) == "/.bfsync/info")
    {
      memset (stbuf, 0, sizeof (struct stat));
      stbuf->st_mode = 0644 | S_IFREG;
      stbuf->st_uid  = getuid();
      stbuf->st_gid  = getgid();
      stbuf->st_size = special_files.info.size();
      return 0;
    }
  else
    {
      debug ("=> ERROR: %s\n", strerror (errno));
      return -errno;
    }
}

string
remove_di_prefix (const string& filename)
{
  // d_foo => foo
  // i_foo => foo
  // foo => xxx_foo;
  if (filename.size() > 2 && filename[1] == '_')
    {
      if (filename[0] == 'd' || filename[0] == 'i')
        return filename.substr (2);
    }
  return "xxx_" + filename;
}

bool
read_dir_contents (const string& path, vector<string>& entries)
{
  bool            dir_ok;
  set<string>     file_list;
  GDir           *dir;

  string git_files = options.repo_path + "/git/files/" + name2git_name (path, GIT_DIRNAME);
  dir = g_dir_open (git_files.c_str(), 0, NULL);
  if (dir)
    {
      GitFileRepo::the()->save_changes();

      const char *name;
      while ((name = g_dir_read_name (dir)))
        {
          string filename = remove_di_prefix (name);
          if (file_list.count (filename) == 0)
            {
              file_list.insert (filename);
              entries.push_back (filename);
            }
        }
      g_dir_close (dir);
      dir_ok = true;
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
  bool open_for_write = (accmode == O_WRONLY || accmode == O_RDWR);

  FSLock lock (open_for_write ? FSLock::WRITE : FSLock::READ);

  debug ("open (\"%s\")\n", path);

  if (string (path) == "/.bfsync/info")
    {
      FileHandle *fh = new FileHandle;
      fh->fd = -1;
      fh->special_file = FileHandle::INFO;
      fh->open_for_write = false;
      fi->fh = reinterpret_cast<uint64_t> (fh);
      return 0;
    }

  if (!search_perm_ok (path))
    return -EACCES;

  if (open_for_write)
    copy_on_write (path);

  string filename = file_path (path);
  printf ("open: translated filename = %s\n", filename.c_str());
  if (filename == "")
    return -ENOENT;

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
      if (offset < info.size())
        {
          bytes_read = size;
          if (offset + bytes_read > info.size())
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
          GitFilePtr gf (path);
          if (gf)
            gf.update()->set_mtime_ctime_now();
        }
    }

  return bytes_written;
}

static int
bfsync_mknod (const char *path, mode_t mode, dev_t dev)
{
  FSLock lock (FSLock::WRITE);

  GitFilePtr gf (path, GitFilePtr::NEW, fuse_get_context());
  gf.update()->mode = mode & ~S_IFMT;

  if (S_ISREG (mode))
    {
      copy_dirs (path);

      string filename = options.repo_path + "/new" + path;
      int rc = mknod (filename.c_str(), mode, dev);
      if (rc == 0)
        {
          GitFilePtr gf_dir (get_dirname (path));
          if (gf_dir)
            gf_dir.update()->set_mtime_ctime_now();

          gf.update()->type = FILE_REGULAR;
          gf.update()->hash = "new";
        }
      else
        {
          return -errno;
        }
    }
  else if (S_ISFIFO (mode))
    {
      gf.update()->type = FILE_FIFO;
    }
  else if (S_ISSOCK (mode))
    {
      gf.update()->type = FILE_SOCKET;
    }
  else if (S_ISBLK (mode))
    {
      gf.update()->type = FILE_BLOCK_DEV;
      gf.update()->major = major (dev);
      gf.update()->minor = minor (dev);
    }
  else if (S_ISCHR (mode))
    {
      gf.update()->type = FILE_CHAR_DEV;
      gf.update()->major = major (dev);
      gf.update()->minor = minor (dev);
    }
  else
    {
      return -ENOENT;
    }
  return 0;
}

bool
write_perm_ok (const GitFilePtr& gf, int uid, int gid)
{
  if (uid == 0)
    return true;

  if (uid == gf->uid)
    {
      if (gf->mode & S_IWUSR)
        return true;
    }

  if (gid == gf->gid)
    {
      if (gf->mode & S_IWGRP)
        return true;
    }

  if (gf->mode & S_IXOTH)
    return true;

  return false;
}

int
bfsync_chmod (const char *name, mode_t mode)
{
  FSLock lock (FSLock::WRITE);

  if (!search_perm_ok (name))
    return -EACCES;

  GitFilePtr git_file (name);

  if (git_file)
    {
      if (fuse_get_context()->uid != 0 && fuse_get_context()->uid != git_file->uid)
        return -EPERM;

      if (fuse_get_context()->uid != 0 && fuse_get_context()->gid != git_file->gid)
        mode &= ~S_ISGID;

      git_file.update()->mode = mode;
      git_file.update()->set_ctime_now();
      return 0;
    }
  return -ENOENT;
}

int
bfsync_chown (const char *name, uid_t uid, gid_t gid)
{
  FSLock lock (FSLock::WRITE);

  GitFilePtr gf (name);
  if (gf)
    {
      gf.update()->uid = uid;
      gf.update()->gid = gid;
      gf.update()->set_ctime_now();

      return 0;
    }
  return -ENOENT;
}

int
bfsync_utimens (const char *name, const struct timespec times[2])
{
  FSLock lock (FSLock::WRITE);

  GitFilePtr gf (name);
  if (gf)
    {
      gf.update()->mtime    = times[1].tv_sec;
      gf.update()->mtime_ns = times[1].tv_nsec;

      return 0;
    }
  return -ENOENT;
}

int
bfsync_truncate (const char *name, off_t off)
{
  FSLock lock (FSLock::WRITE);

  GitFilePtr gf (name);
  if (gf)
    {
      if (!search_perm_ok (name))
        return -EACCES;

      if (!write_perm_ok (gf, fuse_get_context()->uid, fuse_get_context()->gid))
        return -EACCES;

      copy_on_write (name);

      int rc = truncate (file_path (name).c_str(), off);
      if (rc == 0)
        {
          gf.update()->set_mtime_ctime_now();
          return 0;
        }
      return -errno;
    }
  else
    {
      return -EINVAL;
    }
}

static int
bfsync_unlink (const char *name)
{
  FSLock lock (FSLock::WRITE);

  // delete data for changed files
  if (file_status (name) == FS_CHANGED)
    {
      int rc = unlink (file_path (name).c_str());
      if (rc != 0)
        return -errno;
    }

  // delete git entry if present
  if (file_status (name) != FS_NONE)
    {
      string git_file = options.repo_path + "/git/files/" + name2git_name (name);

      GitFileRepo::the()->uncache (name);

      int rc = unlink (git_file.c_str());
      if (rc == 0)
        {
          return 0;
        }
      else
        {
          return -errno;
        }
    }
  return 0;
}

static int
bfsync_mkdir (const char *path, mode_t mode)
{
  FSLock lock (FSLock::WRITE);

  string filename = options.repo_path + "/new" + path;

  copy_dirs (path);

  int rc = mkdir (filename.c_str(), mode);
  if (rc == 0)
    {
      string git_dir  = options.repo_path + "/git/files/" + name2git_name (path, GIT_DIRNAME);

      mkdir (git_dir.c_str(), 0755);

      GitFilePtr gf (path, GitFilePtr::NEW, fuse_get_context());
      gf.update()->type = FILE_DIR;
      gf.update()->mode = mode;
      return 0;
    }

  return -errno;
}

static int
bfsync_rmdir (const char *name)
{
  FSLock lock (FSLock::WRITE);

  // check that dir is in fact empty
  vector<string> entries;
  if (read_dir_contents (name, entries))
    if (!entries.empty())
      return -ENOTEMPTY;

  // rmdir new directories
  struct stat st;
  string new_dirname = options.repo_path + "/new/" + name;
  if (lstat (new_dirname.c_str(), &st) == 0)
    {
      int rc = rmdir (new_dirname.c_str());
      if (rc != 0)
        return -errno;
    }

  // delete git entry if present
  if (file_status (name) == FS_GIT)
    {
      string git_file = options.repo_path + "/git/files/" + name2git_name (name);

      GitFileRepo::the()->uncache (name);

      int rc = unlink (git_file.c_str());
      if (rc != 0)
        return -errno;

      string git_dir = options.repo_path + "/git/files/" + name2git_name (name, GIT_DIRNAME);
      rc = rmdir (git_dir.c_str());
      if (rc != 0)
        return -errno;
    }
  return 0;
}

static int
bfsync_rename (const char *old_path, const char *new_path)
{
  FSLock lock (FSLock::WRITE);

  GitFilePtr gf (old_path);
  if (!gf)
    return -ENOENT;

  string old_git_file = options.repo_path + "/git/files/" + name2git_name (old_path);
  string new_git_file = options.repo_path + "/git/files/" + name2git_name (new_path);

  GitFileRepo::the()->uncache (old_path);

  int rc = rename (old_git_file.c_str(), new_git_file.c_str());
  if (rc != 0)
    return -errno;

  if (gf->type == FILE_DIR)
    {
      string old_git_dir = options.repo_path + "/git/files/" + name2git_name (old_path, GIT_DIRNAME);
      string new_git_dir = options.repo_path + "/git/files/" + name2git_name (new_path, GIT_DIRNAME);

      GitFileRepo::the()->save_changes();
      int rc = rename (old_git_dir.c_str(), new_git_dir.c_str());
      if (rc != 0)
        return -errno;
    }

  copy_dirs (new_path);

  copy_on_write (old_path);

  rename ((options.repo_path + "/new" + old_path).c_str(), (options.repo_path + "/new" + new_path).c_str());

  return 0;
}

static int
bfsync_symlink (const char *from, const char *to)
{
  FSLock lock (FSLock::WRITE);

  if (file_status (to) != FS_NONE)
    return -EEXIST;

  GitFilePtr gf (to, GitFilePtr::NEW, fuse_get_context());

  gf.update()->mode = 0777;
  gf.update()->type = FILE_SYMLINK;
  gf.update()->link = from;

  return 0;
}

static int
bfsync_readlink (const char *path, char *buffer, size_t size)
{
  FSLock lock (FSLock::READ);

  GitFilePtr gf (path);
  if (gf && gf->type == FILE_SYMLINK)
    {
      int len = gf->link.size();

      if (len >= size)
        len = size - 1;
      memcpy (buffer, gf->link.c_str(), len);

      buffer[len] = 0;
      return 0;
    }
  else
    {
      return -ENOENT;
    }
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

  int opt;
  while ((opt = getopt (argc, argv, "da")) != -1)
    {
      switch (opt)
        {
          case 'd': options.mount_debug = true;
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

  char *my_argv[32] = { NULL, };
  int my_argc = 0;

  my_argv[my_argc++] = "bfsyncfs";
  my_argv[my_argc++] = g_strdup (options.mount_point.c_str());
  if (options.mount_debug)
    my_argv[my_argc++] = "-d";
  if (options.mount_all)
    my_argv[my_argc++] = "-oallow_other";
  my_argv[my_argc] = NULL;

  int fuse_rc = fuse_main (my_argc, my_argv, &bfsync_oper, NULL);

  GitFileRepo::the()->save_changes();

  return fuse_rc;
}
