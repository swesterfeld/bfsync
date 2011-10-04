/*
  bfsync: Big File synchronization based on Git

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

#ifndef BFSYNC_GIT_FILE_HH
#define BFSYNC_GIT_FILE_HH

#include <string>
#include <map>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fuse.h>

#include "bfsyncfs.hh"

namespace BFSync {

enum FileType {
  FILE_NONE,
  FILE_REGULAR,
  FILE_SYMLINK,
  FILE_DIR,
  FILE_FIFO,
  FILE_SOCKET,
  FILE_BLOCK_DEV,
  FILE_CHAR_DEV
};

struct GitFile
{
  size_t      size;
  std::string hash;
  time_t      mtime;
  int         mtime_ns;
  time_t      ctime;
  int         ctime_ns;
  uid_t       uid;
  gid_t       gid;
  mode_t      mode;
  std::string link;
  FileType    type;
  dev_t       major;
  dev_t       minor;

  bool        updated;
  std::string git_filename;

  GitFile();
  bool parse (const std::string& filename);
  bool save (const std::string& filename);
  void set_mtime_ctime_now();
  void set_ctime_now();
};

class GitFilePtr
{
  GitFile *ptr;
public:
  enum Mode { LOAD, NEW };

  GitFilePtr (const std::string& filename, Mode mode = LOAD, fuse_context *context = NULL);
  ~GitFilePtr();

  operator bool() const
  {
    return (ptr != 0);
  }
  const GitFile*
  operator->()
  {
    return ptr;
  }
  GitFile*
  update() const
  {
    ptr->updated = true;
    return ptr;
  }
};

class GitFileRepo
{
public:
  std::map<std::string, GitFile*> cache;
  Mutex                           mutex;

  void uncache (const std::string& filename);
  void save_changes();

  static GitFileRepo *the();
};

}

#endif
