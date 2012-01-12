/*
  bfsync: Big File synchronization tool

  Copyright (C) 2011-2012 Stefan Westerfeld

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

#include "glib.h"
#include "bfbdb.hh"
#include <string>
#include <vector>

struct ID {
  BFSync::ID id;

  ID();
  ID (const ID& id);
  ID (const std::string& id);
  ~ID();

  std::string str() const { return id.str(); }
};

struct INode {
  INode();
  INode (const INode& inode);
  ~INode();

  bool         valid;

  unsigned int vmin, vmax;
  ID           id;
  unsigned int uid, gid;
  unsigned int mode, type;

  std::string hash;
  std::string link;

  unsigned int size; // FIXME
  unsigned int major, minor;
  unsigned int nlink;
  unsigned int mtime, mtime_ns;
  unsigned int ctime, ctime_ns;
};

struct Link {
  Link();
  Link (const Link& link);
  ~Link();

  unsigned int vmin, vmax;
  ID dir_id;
  ID inode_id;
  std::string name;
};

struct HistoryEntry
{
  HistoryEntry();
  HistoryEntry (const HistoryEntry& he);
  ~HistoryEntry();

  bool          valid;

  unsigned int  version;
  std::string   hash;
  std::string   author;
  std::string   message;
  unsigned int  time;
};

class BDBWrapper
{
  unsigned int ref_count;
  BFSync::Mutex ref_mutex;
public:
  BDBWrapper();
  ~BDBWrapper();

  BFSync::BDB  *my_bdb;

  void
  ref()
  {
    BFSync::Lock lock (ref_mutex);

    g_return_if_fail (ref_count > 0);
    ref_count++;
  }

  void
  unref()
  {
    BFSync::Lock lock (ref_mutex);

    g_return_if_fail (ref_count > 0);
    ref_count--;
  }

  bool
  has_zero_refs()
  {
    BFSync::Lock lock (ref_mutex);

    return ref_count == 0;
  }
};

class BDBPtr {
  BDBWrapper *ptr;

public:
  BDBPtr (BDBWrapper *wrapper = NULL);
  BDBPtr (const BDBPtr& other);
  BDBPtr& operator= (const BDBPtr& other);

  ~BDBPtr();

  INode              load_inode (const ID *id, int version);
  void               store_inode (const INode *inode);
  std::vector<Link>  load_links (const ID *id, int version);
  void               walk();
  void               store_history_entry (int version,
                                          const std::string& hash,
                                          const std::string& author,
                                          const std::string& msg,
                                          int time);
  HistoryEntry       load_history_entry (int version);
  void               close();

  BFSync::BDB*
  get_bdb()
  {
    return ptr->my_bdb;
  }
};

extern BDBPtr             open_db (const std::string& db);
extern ID*                id_root();

class DiffGenerator
{
  BFSync::DbcPtr dbc;

  Dbt key;
  Dbt data;

  int dbc_ret;

  BDBPtr bdb_ptr;
  unsigned int v_old, v_new;
  std::vector< std::vector<std::string> > diffs;
public:
  DiffGenerator (BDBPtr bdb_ptr, unsigned int v_old, unsigned int v_new);
  ~DiffGenerator();

  std::vector<std::string> get_next();
};
