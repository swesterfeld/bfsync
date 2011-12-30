/*
  bfsync: Big File synchronization tool

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

#ifndef BFSYNC_BDB_HH
#define BFSYNC_BDB_HH

#include <string>
#include <db_cxx.h>
#include "bfinode.hh"

namespace BFSync
{

enum BDBTables
{
  BDB_TABLE_INODES = 1,
  BDB_TABLE_LINKS  = 2
};

bool bdb_open (const std::string& path);
bool bdb_close();

class DataBuffer
{
  const char *ptr;
  size_t      remaining;

public:
  DataBuffer (const char *ptr, size_t size);

  guint32     read_uint32();
  std::string read_string();
};

class BDB
{
public:
  Mutex mutex;

  static BDB *the();

  Db*   get_db();

  void  store_link (const LinkPtr& link);
  void  delete_links (const LinkVersionList& links);
  void  load_links (std::vector<Link*>& links, const std::string& id, guint32 version);

  void  store_inode (const INode *inode);
  void  delete_inodes (const INodeVersionList& inodes);
  bool  load_inode (const ID& id, int version, INode *inode);
};

}

#endif
