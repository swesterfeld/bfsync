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

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <stdlib.h>

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
  FS_DATA
};

FileStatus
file_status (const string& path)
{
  struct stat st;

  if (lstat ((options.repo_path + "/new" + path).c_str(), &st) == 0)
    return FS_NEW;
  if (lstat ((options.repo_path + "/data" + path).c_str(), &st) == 0)
    return FS_DATA;

  return FS_NONE;
}

string
file_path (const string& path)
{
  FileStatus fs = file_status (path);
  if (fs == FS_NEW)
    return options.repo_path + "/new" + path;
  if (fs == FS_DATA)
    return options.repo_path + "/data" + path;
  return "";
}

void
copy_on_write (const string& path)
{
  if (file_status (path) == FS_DATA)
    {
      string old_name = options.repo_path + "/data" + path;
      int old_fd = open (old_name.c_str(), O_RDONLY);

      string new_name = options.repo_path + "/new" + path;
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
}


static int
bfsync_getattr (const char *path, struct stat *stbuf)
{
  string new_filename = options.repo_path + "/new" + path;
  if (lstat (new_filename.c_str(), stbuf) == 0)
    {
      return 0;
    }

  string filename = options.repo_path + "/data" + path;
  if (lstat (filename.c_str(), stbuf) == 0)
    {
      return 0;
    }
  else
    {
      return -errno;
    }
}

static int
bfsync_readdir (const char *path, void *buf, fuse_fill_dir_t filler,
                off_t offset, struct fuse_file_info *fi)
{
  GDir *dir;
  set<string> file_list;

  (void) offset;
  (void) fi;

  bool dir_ok = false;

  string filename = options.repo_path + "/data" + path;
  dir = g_dir_open (filename.c_str(), 0, NULL);
  if (dir)
    {
      const char *name;
      while ((name = g_dir_read_name (dir)))
        {
          if (file_list.count (name) == 0)
            {
              file_list.insert (name);
              filler (buf, name, NULL, 0);
            }
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
              filler (buf, name, NULL, 0);
            }
        }
      g_dir_close (dir);
      dir_ok = true;
    }

  if (dir_ok)
    {
      // . and .. are always there
      filler (buf, ".", NULL, 0);
      filler (buf, "..", NULL, 0);

      return 0;
    }
  else
    return -ENOENT;
}

static int
bfsync_open (const char *path, struct fuse_file_info *fi)
{
  string filename = file_path (path);
  if (filename == "")
    return -ENOENT;

  int fd = open (filename.c_str(), fi->flags);

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

static int
bfsync_read (const char *path, char *buf, size_t size, off_t offset,
             struct fuse_file_info *fi)
{
  string filename = options.repo_path + "/data" + path;

  ssize_t bytes_read = 0;
  (void) fi;

  int fd = open (filename.c_str(), O_RDONLY);
  if (fd != -1)
    {
      bytes_read = pread (fd, buf, size, offset);
      close (fd);
    }

  return bytes_read;
}


static int
bfsync_mknod (const char *path, mode_t mode, dev_t dev)
{
  string filename = options.repo_path + "/new" + path;

  int rc = mknod (filename.c_str(), mode, dev);
  if (rc == 0)
    return 0;
  else
    return -errno;
}

int
bfsync_chmod (const char *, mode_t)
{
  return -EINVAL;
}

int
bfsync_chown (const char *, uid_t, gid_t)
{
  return -EINVAL;
}

int
bfsync_utime (const char *, struct utimbuf *)
{
  return -EINVAL;
}

int
bfsync_truncate (const char *name, off_t off)
{
  if (file_status (name) == FS_DATA)
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

static struct fuse_operations bfsync_oper = { NULL, };

int
main (int argc, char *argv[])
{
  options.repo_path = "test";

  /* read */
  bfsync_oper.getattr  = bfsync_getattr;
  bfsync_oper.readdir  = bfsync_readdir;
  bfsync_oper.open     = bfsync_open;
  bfsync_oper.read     = bfsync_read;

  /* write */
  bfsync_oper.mknod    = bfsync_mknod;
  bfsync_oper.chown    = bfsync_chown;
  bfsync_oper.chmod    = bfsync_chmod;
  bfsync_oper.utime    = bfsync_utime;
  bfsync_oper.truncate = bfsync_truncate;

  return fuse_main (argc, argv, &bfsync_oper, NULL);
}
