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

struct Options {
  string repo_path;
} options;

enum FileStatus
{
  FS_NONE,
  FS_NEW,
  FS_GIT,
  FS_DEL
};

struct FileHandle
{
  int fd;
  enum { NONE, INFO } special_file;
};

struct SpecialFiles
{
  string info;
} special_files;

static FILE *debug_file = NULL;

#define DEBUG 0

static inline void
debug (const char *fmt, ...)
{
  // no debugging -> return as quickly as possible
  if (!DEBUG)
    return;

  if (!debug_file)
    debug_file = fopen ("/tmp/bfsyncfs.log", "w");

  va_list ap;

  va_start (ap, fmt);
  vfprintf (debug_file, fmt, ap);
  fflush (debug_file);
  va_end (ap);
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
enum
{
  GIT_FILENAME = 1,
  GIT_DIRNAME  = 2
};

string
name2git_name (const string& name, int type = GIT_FILENAME)
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

FileStatus
file_status (const string& path)
{
  struct stat st;

  if (path != "/" && lstat ((options.repo_path + "/del/" + name2git_name (path)).c_str(), &st) == 0)
    return FS_DEL;
  if (lstat ((options.repo_path + "/new" + path).c_str(), &st) == 0)
    return FS_NEW;
  if (lstat ((options.repo_path + "/git/files/" + name2git_name (path)).c_str(), &st) == 0)
    return FS_GIT;

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
  if (fs == FS_NEW)
    return options.repo_path + "/new" + path;
  if (fs == FS_GIT)
    {
      GitFile gf;
      if (gf.parse (options.repo_path + "/git/files/" + name2git_name (path)))
        return make_object_filename (gf.hash);
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

enum CopyAttrMode
{
  CA_NORMAL,
  CA_NO_CHMOD
};

void
copy_attrs (const GitFile& git_file, const string& path, CopyAttrMode mode = CA_NORMAL)
{
  if (mode == CA_NORMAL)  // chmod() does not work for symlinks
    {
      int git_mode = git_file.mode & ~S_IFMT;
      chmod (path.c_str(), git_mode);
    }

  // set atime and mtime from git file mtime
  timespec times[2];
  times[0].tv_sec = git_file.mtime;
  times[0].tv_nsec = git_file.mtime_ns;
  times[1] = times[0];

  utimensat (AT_FDCWD, path.c_str(), times, AT_SYMLINK_NOFOLLOW);

  // set uid / gid
  lchown (path.c_str(), git_file.uid, git_file.gid);
}

void
copy_dirs (const string& path, FileStatus status)
{
  vector<string> dirs = split (path);
  if (dirs.empty())
    return;

  dirs.pop_back();

  if (status == FS_DEL)
    {
      string dir = options.repo_path + "/del";

      for (vector<string>::iterator di = dirs.begin(); di != dirs.end(); di++)
        {
          dir += "/d_" + *di;

          mkdir (dir.c_str(), 0755);
        }
    }
  else if (status == FS_NEW)
    {
      string dir_path;

      for (vector<string>::iterator di = dirs.begin(); di != dirs.end(); di++)
        {
          dir_path += "/" + *di;

          string dir = options.repo_path + "/new" + dir_path;

          bool copy_a = (file_status (dir_path) == FS_GIT);

          mkdir (dir.c_str(), 0755);

          if (copy_a)
            {
              GitFile gf;
              if (gf.parse (options.repo_path + "/git/files/" + name2git_name (dir_path)))
                copy_attrs (gf, dir.c_str());
            }
        }
    }
  else
    {
      assert (false);
    }
}

void
copy_on_write (const string& path)
{
  if (file_status (path) == FS_GIT)
    {
      copy_dirs (path, FS_NEW);

      GitFile gf;
      if (gf.parse (options.repo_path + "/git/files/" + name2git_name (path)))
        {
          string new_name = options.repo_path + "/new" + path;

          if (gf.type == FILE_REGULAR)
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

              copy_attrs (gf, new_name);
            }
          else if (gf.type == FILE_DIR)
            {
              mkdir (new_name.c_str(), 0755);
              copy_attrs (gf, new_name);
            }
          else if (gf.type == FILE_SYMLINK)
            {
              symlink (gf.link.c_str(), new_name.c_str());
              copy_attrs (gf, new_name, CA_NO_CHMOD);
            }
        }
    }
}

static int
bfsync_getattr (const char *path, struct stat *stbuf)
{
  debug ("getattr (\"%s\")\n", path);

  if (file_status (path) == FS_DEL)
    {
      debug ("=> ENOENT\n");
      return -ENOENT;
    }

  if (string (path) == "/")  // take attrs for / from git/files dir, since we have no own attrs stored for that dir
    {
      if (lstat ((options.repo_path + "/git/files/").c_str(), stbuf) == 0)
        return 0;
    }

  string git_filename = options.repo_path + "/git/files/" + name2git_name (path);

  GitFile git_file;
  if (git_file.parse (git_filename))
    {
      int git_mode = git_file.mode & ~S_IFMT;
      if (git_file.type == FILE_REGULAR)
        {
          memset (stbuf, 0, sizeof (struct stat));
          if (git_file.hash == "new")
            {
              // take size from new file
              string new_filename = options.repo_path + "/new" + path;
              lstat (new_filename.c_str(), stbuf);
            }
          else
            {
              stbuf->st_size = git_file.size;
            }
          stbuf->st_mode = git_mode | S_IFREG;
          stbuf->st_uid  = git_file.uid;
          stbuf->st_gid  = git_file.gid;
          stbuf->st_mtime = git_file.mtime;
          stbuf->st_mtim.tv_nsec = git_file.mtime_ns;
        }
      else if (git_file.type == FILE_SYMLINK)
        {
          memset (stbuf, 0, sizeof (struct stat));
          stbuf->st_mode = git_mode | S_IFLNK;
          stbuf->st_uid  = git_file.uid;
          stbuf->st_gid  = git_file.gid;
          stbuf->st_size = git_file.link.size();
          stbuf->st_mtime = git_file.mtime;
          stbuf->st_mtim.tv_nsec = git_file.mtime_ns;
        }
      else if (git_file.type == FILE_DIR)
        {
          memset (stbuf, 0, sizeof (struct stat));
          stbuf->st_mode = git_mode | S_IFDIR;
          stbuf->st_uid  = git_file.uid;
          stbuf->st_gid  = git_file.gid;
          stbuf->st_mtime = git_file.mtime;
          stbuf->st_mtim.tv_nsec = git_file.mtime_ns;
        }
      else if (git_file.type == FILE_FIFO)
        {
          memset (stbuf, 0, sizeof (struct stat));
          stbuf->st_mode = git_mode | S_IFIFO;
          stbuf->st_uid  = git_file.uid;
          stbuf->st_gid  = git_file.gid;
          stbuf->st_mtime = git_file.mtime;
          stbuf->st_mtim.tv_nsec = git_file.mtime_ns;
        }
      else if (git_file.type == FILE_SOCKET)
        {
          memset (stbuf, 0, sizeof (struct stat));
          stbuf->st_mode = git_mode | S_IFSOCK;
          stbuf->st_uid  = git_file.uid;
          stbuf->st_gid  = git_file.gid;
          stbuf->st_mtime = git_file.mtime;
          stbuf->st_mtim.tv_nsec = git_file.mtime_ns;
        }
      else if (git_file.type == FILE_BLOCK_DEV)
        {
          memset (stbuf, 0, sizeof (struct stat));
          stbuf->st_mode = git_mode | S_IFBLK;
          stbuf->st_uid  = git_file.uid;
          stbuf->st_gid  = git_file.gid;
          stbuf->st_mtime = git_file.mtime;
          stbuf->st_mtim.tv_nsec = git_file.mtime_ns;
          stbuf->st_rdev = makedev (git_file.major, git_file.minor);
        }
      else if (git_file.type == FILE_CHAR_DEV)
        {
          memset (stbuf, 0, sizeof (struct stat));
          stbuf->st_mode = git_mode | S_IFCHR;
          stbuf->st_uid  = git_file.uid;
          stbuf->st_gid  = git_file.gid;
          stbuf->st_mtime = git_file.mtime;
          stbuf->st_mtim.tv_nsec = git_file.mtime_ns;
          stbuf->st_rdev = makedev (git_file.major, git_file.minor);
        }
      return 0;
    }
  printf ("::: no gitfile parsed\n");

  if (string (path) == "/.bfsync")
    {
      debug ("=> .bfsync\n");

      memset (stbuf, 0, sizeof (struct stat));
      stbuf->st_mode = 0755 | S_IFDIR;
      stbuf->st_uid  = getuid();
      stbuf->st_gid  = getgid();
      return 0;
    }
  else if (string (path) == "/.bfsync/info")
    {
      debug ("=> .bfsync/info\n");

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

  string del_files = options.repo_path + "/del/" + name2git_name (path);
  dir = g_dir_open (del_files.c_str(), 0, NULL);
  if (dir)
    {
      const char *name;
      while ((name = g_dir_read_name (dir)))
        {
          string filename = remove_di_prefix (name);
          if (file_list.count (filename) == 0 && string (name).substr (0, 2) == "i_")
            {
              file_list.insert (filename);
              // by inserting deleted files into the file_list (without updating result)
              // we ensure that they won't show up in ls
            }
        }
      g_dir_close (dir);
      dir_ok = true;
    }

  string git_files = options.repo_path + "/git/files/" + name2git_name (path, GIT_DIRNAME);
  dir = g_dir_open (git_files.c_str(), 0, NULL);
  if (dir)
    {
      const char *name;
      while ((name = g_dir_read_name (dir)))
        {
          string filename = remove_di_prefix (name);
          if (file_list.count (filename) == 0)
            {
              file_list.insert (filename);
              entries.push_back (filename);
            }
          // FIXME: free name
        }
      g_dir_close (dir);
      dir_ok = true;
    }

  string new_files = options.repo_path + "/new" + path;
  dir = g_dir_open (new_files.c_str(), 0, NULL);
  if (dir)
    {
      const char *name;
      while ((name = g_dir_read_name (dir)))
        {
          if (file_list.count (name) == 0)
            {
              file_list.insert (name);
              entries.push_back (name);
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
  debug ("open (\"%s\")\n", path);

  if (string (path) == "/.bfsync/info")
    {
      FileHandle *fh = new FileHandle;
      fh->fd = -1;
      fh->special_file = FileHandle::INFO;
      fi->fh = reinterpret_cast<uint64_t> (fh);
      return 0;
    }

  if (file_status (path) == FS_DEL)
    {
      if (fi->flags & O_CREAT)
        {
          unlink ((options.repo_path + "/del/" + name2git_name (path)).c_str());
        }
      else
        {
          return -ENOENT;
        }
    }
  int accmode = fi->flags & O_ACCMODE;
  if (accmode == O_WRONLY || accmode == O_RDWR)
    {
      copy_on_write (path);
    }

  string filename = file_path (path);
  printf ("open: translated filename = %s\n", filename.c_str());
  if (filename == "")
    return -ENOENT;

  int fd = open (filename.c_str(), fi->flags);

  if (fd != -1)
    {
      if (file_status (path) == FS_NEW)
        {
          GitFile git_file;
          git_file.type = FILE_REGULAR;
          git_file.mode = 0644;                             // FIXME: open mode?
          git_file.size = 0;                                // edited by bfsync2 on commit / along with hash
          git_file.hash = "new";
          git_file.save (options.repo_path + "/git/files/" + name2git_name (path));
        }

      FileHandle *fh = new FileHandle;
      fh->fd = fd;
      fh->special_file = FileHandle::NONE;
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
  close (fh->fd);
  delete fh;
  return 0;
}

static int
bfsync_read (const char *path, char *buf, size_t size, off_t offset,
             struct fuse_file_info *fi)
{
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
  FileHandle *fh = reinterpret_cast<FileHandle *> (fi->fh);

  ssize_t bytes_written = 0;

  if (fh->fd != -1)
    bytes_written = pwrite (fh->fd, buf, size, offset);

  return bytes_written;
}

static int
bfsync_mknod (const char *path, mode_t mode, dev_t dev)
{
  string git_file = options.repo_path + "/git/files/" + name2git_name (path);

  copy_dirs (path, FS_NEW);

  int new_mode = mode & ~S_IFMT;
  if (S_ISREG (mode))
    {
      string filename = options.repo_path + "/new" + path;
      int rc = mknod (filename.c_str(), mode, dev);
      if (rc == 0)
        {
          GitFile gf;
          gf.type = FILE_REGULAR;
          gf.hash = "new";
          gf.mode = new_mode;
          gf.save (git_file);
          return 0;
        }
      else
        return -errno;
    }
  else if (S_ISFIFO (mode))
    {
      GitFile gf;
      gf.type = FILE_FIFO;
      gf.mode = new_mode;
      gf.save (git_file);

      return 0;
    }
  else if (S_ISSOCK (mode))
    {
      GitFile gf;
      gf.type = FILE_SOCKET;
      gf.mode = new_mode;
      gf.save (git_file);

      return 0;
    }
  else if (S_ISBLK (mode))
    {
      GitFile gf;
      gf.type = FILE_BLOCK_DEV;
      gf.major = major (dev);
      gf.minor = minor (dev);
      gf.mode = new_mode;
      gf.save (git_file);

      return 0;
    }
  else if (S_ISCHR (mode))
    {
      GitFile gf;
      gf.type = FILE_CHAR_DEV;
      gf.mode = new_mode;
      gf.major = major (dev);
      gf.minor = minor (dev);
      gf.save (git_file);

      return 0;
    }
  return -ENOENT;
}

int
bfsync_chmod (const char *name, mode_t mode)
{
  GitFile gf;

  if (gf.parse (options.repo_path + "/git/files/" + name2git_name (name)))
    {
      gf.mode = mode;
      if (gf.save (options.repo_path + "/git/files/" + name2git_name (name)))
        {
          return 0;
        }
      else
        {
          return -EIO;
        }
    }
  return -ENOENT;
}

int
bfsync_chown (const char *name, uid_t uid, gid_t gid)
{
  GitFile gf;

  if (gf.parse (options.repo_path + "/git/files/" + name2git_name (name)))
    {
      gf.uid = uid;
      gf.gid = gid;
      if (gf.save (options.repo_path + "/git/files/" + name2git_name (name)))
        {
          return 0;
        }
      else
        {
          return -EIO;
        }
    }
  return -ENOENT;
}

int
bfsync_utimens (const char *name, const struct timespec times[2])
{
  GitFile gf;
  string git_file = options.repo_path + "/git/files/" + name2git_name (name);

  if (gf.parse (git_file))
    {
      gf.mtime    = times[1].tv_sec;
      gf.mtime_ns = times[1].tv_nsec;
      if (gf.save (git_file))
        {
          return 0;
        }
      else
        {
          return -EIO; // should never happen
        }
    }
  return -ENOENT;
}

int
bfsync_truncate (const char *name, off_t off)
{
  copy_on_write (name);

  if (file_status (name) != FS_NEW)
    return -EINVAL;
  else
    {
      int rc = truncate (file_path (name).c_str(), off);
      if (rc == 0)
        return 0;
      else
        return -errno;
    }
}

static int
bfsync_unlink (const char *name)
{
  // delete data for new files
  if (file_status (name) == FS_NEW)
    {
      int rc = unlink (file_path (name).c_str());
      if (rc != 0)
        return -errno;
    }

  // delete git entry if present
  if (file_status (name) == FS_GIT)
    {
      string git_file = options.repo_path + "/git/files/" + name2git_name (name);

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
  string filename = options.repo_path + "/new" + path;

  copy_dirs (path, FS_NEW);

  int rc = mkdir (filename.c_str(), mode);
  if (rc == 0)
    {
      string git_file = options.repo_path + "/git/files/" + name2git_name (path);
      string git_dir  = options.repo_path + "/git/files/" + name2git_name (path, GIT_DIRNAME);

      mkdir (git_dir.c_str(), 0755);

      GitFile gf;
      gf.type = FILE_DIR;
      gf.mode = mode;
      gf.uid = getuid();
      gf.gid = getgid();
      gf.save (git_file);
      return 0;
    }

  return -errno;
}

static int
bfsync_rmdir (const char *name)
{
  // check that dir is in fact empty
  vector<string> entries;
  if (read_dir_contents (name, entries))
    if (!entries.empty())
      return -ENOTEMPTY;

  // rmdir new directories
  if (file_status (name) == FS_NEW)
    {
      int rc = rmdir (file_path (name).c_str());
      if (rc != 0)
        return -errno;
    }

  // delete git entry if present
  if (file_status (name) == FS_GIT)
    {
      string git_file = options.repo_path + "/git/files/" + name2git_name (name);

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
bfsync_rename (const char *old_path, const char *new_path)
{
  copy_dirs (new_path, FS_NEW);

  copy_on_write (old_path);

  rename ((options.repo_path + "/new" + old_path).c_str(),
          (options.repo_path + "/new" + new_path).c_str());

  // make del entry if git entry is present
  if (file_status (old_path) == FS_GIT)
    {
      copy_dirs (old_path, FS_DEL);

      int fd = open ((options.repo_path + "/del/" + name2git_name (old_path)).c_str(), O_CREAT|O_WRONLY, 0644);
      if (fd != -1)
        {
          close (fd);
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
bfsync_symlink (const char *from, const char *to)
{
  copy_dirs (to, FS_NEW);

  int rc = symlink (from, (options.repo_path + "/new" + to).c_str());
  if (rc == 0)
    {
      string git_file = options.repo_path + "/git/files/" + name2git_name (to);

      GitFile gf;
      gf.type = FILE_SYMLINK;
      gf.link = from;
      gf.save (git_file);
      return 0;
    }
  return -errno;
}

static int
bfsync_readlink (const char *path, char *buffer, size_t size)
{
  int len = 0;
  if (file_status (path) == FS_GIT)
    {
      GitFile gf;
      if (gf.parse (options.repo_path + "/git/files/" + name2git_name (path)) && gf.type == FILE_SYMLINK)
        {
          len = gf.link.size();
          if (len >= size)
            len = size - 1;
          memcpy (buffer, gf.link.c_str(), len);
        }
      else
        {
          return -ENOENT;
        }
    }
  else
    {
      string filename = file_path (path);
      if (filename == "")
        return -ENOENT;

      len = readlink (filename.c_str(), buffer, size - 1);
    }
  if (len == -1)
    return -errno;
  buffer[len] = 0;
  return 0;
}

static void*
bfsync_init (struct fuse_conn_info *conn)
{
  conn->max_readahead = 10 * 128 * 1024;
  conn->max_write = 128 * 1024;

  conn->capable = FUSE_CAP_BIG_WRITES;
  conn->want    = FUSE_CAP_BIG_WRITES;

  struct fuse_context* context = fuse_get_context();
  return context->private_data;
}

static struct fuse_operations bfsync_oper = { NULL, };

int
main (int argc, char *argv[])
{
  string repo_path = "test";
  if (!g_path_is_absolute (repo_path.c_str()))
    repo_path = g_get_current_dir() + string (G_DIR_SEPARATOR + repo_path);

  options.repo_path = repo_path;

  special_files.info = "repo-path \"" + repo_path + "\";\n"
                     + "mount-point \"" + g_get_current_dir() + "/mnt\";\n";

  debug ("starting bfsyncfs; info = \n{\n%s}\n", special_files.info.c_str());

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

  return fuse_main (argc, argv, &bfsync_oper, NULL);
}
