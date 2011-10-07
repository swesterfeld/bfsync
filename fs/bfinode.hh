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

#ifndef BFSYNC_INODE_HH
#define BFSYNC_INODE_HH

#include <string>
#include "bfgitfile.hh" /* BFSync::FileType */

namespace BFSync
{

struct INode
{
  int           vmin;
  int           vmax;

  std::string   id;
  uid_t         uid;
  gid_t         gid;
  size_t        size;
  std::string   hash;
  time_t        mtime;
  int           mtime_ns;
  time_t        ctime;
  int           ctime_ns;
  mode_t        mode;
  std::string   link;
  FileType      type;
  dev_t         major;
  dev_t         minor;

  bool          updated;

  INode();
  ~INode();

  bool          save();

  void          set_mtime_ctime_now();
  void          set_ctime_now();
};

class INodePtr
{
  INode *ptr;
public:
  INodePtr (const std::string&  id);
  INodePtr (fuse_context       *context);

  operator bool() const
  {
    return (ptr != 0);
  }
  const INode*
  operator->() const
  {
    return ptr;
  }
  INode*
  update() const
  {
    ptr->updated = true;
    return ptr;
  }
};

}

#endif
