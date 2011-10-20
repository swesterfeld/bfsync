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
#include <vector>
#include <map>

#include "bfsql.hh"
#include "bfsyncfs.hh"
#include "bfidhash.hh"

namespace BFSync
{

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

class INode;
class INodePtr
{
  INode *ptr;
  INodePtr();
public:
  INodePtr (const Context& ctx, const ID& id);
  INodePtr (const Context& ctx);
  ~INodePtr();

  inline INodePtr (const INodePtr& other);
  inline INodePtr& operator= (const INodePtr& other);

  operator bool() const
  {
    return (ptr != 0);
  }
  const INode*
  operator->() const
  {
    return ptr;
  }
  inline INode* update() const;
  static INodePtr null();
};

}

#include "bflink.hh"
namespace BFSync
{

enum FileStatus
{
  FS_NONE,
  FS_RDONLY,
  FS_CHANGED
};

class INode
{
  static std::vector<ino_t> ino_pool;
  unsigned int              ref_count;
  Mutex                     ref_mutex;

public:
  int           vmin;
  int           vmax;

  ID            id;
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
  int           nlink;
  ino_t         ino;       /* inode number */

  std::map<std::string, LinkPtr> links;

  bool          updated;

  INode();
  ~INode();

  bool          save (SQLStatement& inode_stmt, SQLStatement& link_stmt);
  bool          load (const Context& ctx, const ID& id);

  void          set_mtime_ctime_now();
  void          set_ctime_now();

  FileStatus    file_status() const;
  std::string   new_file_path() const;
  std::string   file_path() const;
  void          copy_on_write();
  void          add_link (const Context& ctx, INodePtr to, const std::string& name);
  bool          unlink (const Context& ctx, const std::string& name);

  bool          read_perm_ok (const Context& ctx) const;
  bool          write_perm_ok (const Context& ctx) const;
  bool          search_perm_ok (const Context& ctx) const;

  void          load_or_alloc_ino();
  void          get_child_names (std::vector<std::string>& names) const;
  INodePtr      get_child (const Context& ctx, const std::string& name) const;

  void
  ref()
  {
    Lock lock (ref_mutex);

    ref_count++;
  }

  void
  unref()
  {
    Lock lock (ref_mutex);

    g_return_if_fail (ref_count > 0);
    ref_count--;
  }

  bool
  has_zero_refs()
  {
    Lock lock (ref_mutex);

    return ref_count == 0;
  }
};

inline INode*
INodePtr::update() const
{
  ptr->updated = true;
  return ptr;
}

class INodeRepo
{
private:
  std::map<int, std::map<ID, INode*> >   cache;
  SQLStatementStore                     *m_sql_statements;
public:
  std::map<ino_t, ID>   new_inodes;
  Mutex                 mutex;

  SQLStatementStore&
  sql_statements()
  {
    if (!m_sql_statements)
      m_sql_statements = new SQLStatementStore();

    return *m_sql_statements;
  }

  std::map<ID, INode*>& get_cache (const Context& ctx);

  enum SaveChangesMode { SC_NORMAL, SC_CLEAR_CACHE };

  void save_changes (SaveChangesMode sc = SC_NORMAL);
  void clear_cache();
  void free_sql_statements();

  enum DeleteMode { DM_ALL, DM_SOME };
  void delete_unused_inodes (DeleteMode dmode);
  int  cached_inode_count();

  static INodeRepo *the();
};

inline
INodePtr::INodePtr (const INodePtr& other)
{
  INode *new_ptr = other.ptr;

  if (new_ptr)
    new_ptr->ref();

  ptr = new_ptr;
}

inline INodePtr&
INodePtr::operator= (const INodePtr& other)
{
  INode *new_ptr = other.ptr;
  INode *old_ptr = ptr;

  if (new_ptr)
    new_ptr->ref();

  ptr = new_ptr;

  if (old_ptr)
    old_ptr->unref();

  return *this;
}

}

#endif
