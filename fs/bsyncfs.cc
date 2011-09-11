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

using std::string;

struct Options {
  string repo_path;
} options;

static int
bfsync_getattr (const char *path, struct stat *stbuf)
{
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
  (void) offset;
  (void) fi;


  string filename = options.repo_path + "/data" + path;
  GDir *dir = g_dir_open (filename.c_str(), 0, NULL);
  if (dir)
    {
      const char *name;

      filler (buf, ".", NULL, 0);
      filler (buf, "..", NULL, 0);

      while ((name = g_dir_read_name (dir)))
        {
          filler (buf, name, NULL, 0);
        }
      g_dir_close (dir);
    }
  else
    {
      return -ENOENT;
    }
  return 0;
}

static int
bfsync_open (const char *path, struct fuse_file_info *fi)
{
  string filename = options.repo_path + "/data" + path;

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

static struct fuse_operations bfsync_oper = { NULL, };

int
main (int argc, char *argv[])
{
  options.repo_path = "test";

  bfsync_oper.getattr  = bfsync_getattr;
  bfsync_oper.readdir  = bfsync_readdir;
  bfsync_oper.open     = bfsync_open;
  bfsync_oper.read     = bfsync_read;

  return fuse_main (argc, argv, &bfsync_oper, NULL);
}
