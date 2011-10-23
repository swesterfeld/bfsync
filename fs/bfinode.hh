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
public:
  INodePtr (const Context& ctx, const ID& id);
  INodePtr (const Context& ctx);
  INodePtr (INode *inode = NULL);  // == null()
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
  INode* update() const;

  /* never use this if you can use update() instead */
  INode*
  get_ptr_without_update() const
  {
    return ptr;
  }
  static INodePtr null();
};

class INodeLinks;
class INodeLinksPtr
{
  INodeLinks *ptr;
public:
  INodeLinksPtr (INodeLinks *inode_links = NULL);
  inline INodeLinksPtr (const INodeLinksPtr& other);
  inline INodeLinksPtr& operator= (const INodeLinksPtr& other);

  ~INodeLinksPtr();

  operator bool() const
  {
    return (ptr != 0);
  }

  const INodeLinks*
  operator->() const
  {
    return ptr;
  }
  /* never use this if you can use update() instead */
  INodeLinks*
  get_ptr_without_update() const
  {
    return ptr;
  }
  INodeLinks* update() const;
  static INodeLinksPtr& null();
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

  INodeLinksPtr links;

  bool          updated;

  INode();
  INode (const INode& other);
  ~INode();

  bool          save (SQLStatement& inode_stmt);
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
  void          get_child_names (const Context& ctx, std::vector<std::string>& names) const;
  INodePtr      get_child (const Context& ctx, const std::string& name) const;

  void
  ref()
  {
    Lock lock (ref_mutex);

    g_return_if_fail (ref_count > 0);
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

class INodeVersionList
{
  std::vector<INodePtr> inodes;

public:
  size_t size() const;
  INodePtr& operator[] (size_t pos);
  void add (INodePtr& inode);
};

class LinkVersionList
{
  std::vector<LinkPtr> links;
public:
  size_t size() const;
  LinkPtr& operator[] (size_t pos);
  const LinkPtr& operator[] (size_t pos) const;
  void add (const LinkPtr& link);
  LinkPtr& find_version (int version);
  const LinkPtr& find_version (int version) const;
};

class INodeLinks
{
  unsigned int ref_count;
public:
  std::map<std::string, LinkVersionList> link_map;
  bool                                   updated;

  INodeLinks();
  ~INodeLinks();

  bool save (SQLStatement& stmt);

  void
  ref()
  {
    g_return_if_fail (ref_count > 0);
    ref_count++;
  }

  void
  unref()
  {
    g_return_if_fail (ref_count > 0);
    ref_count--;
  }

  bool
  has_zero_refs()
  {
    return ref_count == 0;
  }
};

class INodeRepo
{
private:
  SQLStatementStore              *m_sql_statements;
public:
  std::map<ID, INodeVersionList>  cache;
  std::map<ino_t, ID>             new_inodes;
  std::map<ID, INodeLinksPtr>     links_cache;
  Mutex                           mutex;

  SQLStatementStore&
  sql_statements()
  {
    if (!m_sql_statements)
      m_sql_statements = new SQLStatementStore();

    return *m_sql_statements;
  }

  enum SaveChangesMode { SC_NORMAL, SC_CLEAR_CACHE };

  void save_changes (SaveChangesMode sc = SC_NORMAL);
  void clear_cache();
  void free_sql_statements();

  enum DeleteMode { DM_ALL, DM_SOME };
  void delete_unused_inodes (DeleteMode dmode);
  int  cached_inode_count();
  int  cached_dir_count();

  static INodeRepo *the();

  INodeRepo();
  ~INodeRepo();
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

inline
INodeLinksPtr::INodeLinksPtr (const INodeLinksPtr& other)
{
  INodeLinks *new_ptr = other.ptr;

  if (new_ptr)
    new_ptr->ref();

  ptr = new_ptr;
}

inline INodeLinksPtr&
INodeLinksPtr::operator= (const INodeLinksPtr& other)
{
  INodeLinks *new_ptr = other.ptr;
  INodeLinks *old_ptr = ptr;

  if (new_ptr)
    new_ptr->ref();

  ptr = new_ptr;

  if (old_ptr)
    old_ptr->unref();

  return *this;
}

}

#endif
