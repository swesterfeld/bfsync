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

#include "bfbdb.hh"
#include <glib.h>
#include <stdint.h>
#include <string>
#include <vector>
#include <set>

struct ID {
  BFSync::ID id;

  bool       valid;

  ID();
  ID (const ID& id);
  ID (const std::string& id);
  ~ID();

  std::string
  no_prefix_str() const
  {
    return id.no_prefix_str();
  }
  std::string
  str() const
  {
    return id.str();
  }
  bool
  operator== (const ID& other) const;
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

  uint64_t     size;
  unsigned int major, minor;
  unsigned int nlink;
  unsigned int mtime, mtime_ns;
  unsigned int ctime, ctime_ns;
  unsigned int new_file_number;
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

struct TempFile {
  TempFile();
  TempFile (const TempFile& tf);
  ~TempFile();

  std::string   filename;
  unsigned int  pid;
};

struct JournalEntry {
  JournalEntry();
  JournalEntry (const JournalEntry& je);
  ~JournalEntry();

  std::string   operation;
  std::string   state;
};

struct Hash2FileEntry {
  Hash2FileEntry();
  Hash2FileEntry (const Hash2FileEntry& h2fe);
  ~Hash2FileEntry();

  bool          valid;

  std::string   hash;
  unsigned int  file_number;
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

  bool               open_ok();

  void               begin_transaction();
  void               commit_transaction();
  void               abort_transaction();

  INode              load_inode (const ID& id, unsigned int version);
  std::vector<INode> load_all_inodes (const ID& id);
  void               store_inode (const INode& inode);
  void               delete_inode (const INode& inode);

  unsigned int       clear_changed_inodes (unsigned int max_inodes);

  std::vector<Link>  load_links (const ID& id, unsigned int version);
  std::vector<Link>  load_all_links (const ID& id);
  void               store_link (const Link& link);
  void               delete_link (const Link& link);

  void               walk();
  void               store_history_entry (int version,
                                          const std::string& hash,
                                          const std::string& author,
                                          const std::string& msg,
                                          int time);
  HistoryEntry       load_history_entry (int version);
  void               delete_history_entry (unsigned int version);

  void               store_hash2file (const std::string& hash, unsigned int file_number);
  unsigned int       load_hash2file (const std::string& hash);
  void               delete_hash2file (const std::string& hash);

  void               add_deleted_file (unsigned int file_number);
  std::vector<unsigned int>
                     load_deleted_files();
  unsigned int       clear_deleted_files (unsigned int max_files);

  void               add_temp_file (const std::string& filename, unsigned int pid);
  std::vector<TempFile> load_temp_files();
  void               delete_temp_file (const std::string& filename);

  std::vector<JournalEntry> load_journal_entries();
  void                      store_journal_entry (const JournalEntry& journal_entry);
  void                      clear_journal_entries();

  unsigned int       gen_new_file_number();

  void               close();

  BFSync::BDB*
  get_bdb()
  {
    return ptr->my_bdb;
  }
};

extern BDBPtr             open_db (const std::string& db, int cache_size_mb, bool recover);
extern ID                 id_root();
extern std::string        time_prof_result();

class DiffGenerator
{
  BFSync::DbcPtr dbc;

  BFSync::DataOutBuffer kbuf;

  Dbt key;
  Dbt data;

  int dbc_ret;

  unsigned int v_old, v_new;

  BDBPtr bdb_ptr;
  std::vector< std::vector<std::string> > diffs;
public:
  DiffGenerator (BDBPtr bdb_ptr);
  ~DiffGenerator();

  std::vector<std::string> get_next();
};

class ChangedINodesIterator
{
  BFSync::DbcPtr dbc;
  int dbc_ret;
  BDBPtr bdb_ptr;

  BFSync::DataOutBuffer kbuf;
  Dbt key, data;
public:
  ChangedINodesIterator (BDBPtr bdb_ptr);
  ~ChangedINodesIterator();

  ID get_next();
};

class INodeHashIterator
{
  BFSync::DbcPtr dbc;
  int dbc_ret;
  BDBPtr bdb_ptr;

  std::set<std::string> all_hashes;

  Dbt key, data;
public:
  INodeHashIterator (BDBPtr bdb_ptr);
  ~INodeHashIterator();

  std::string get_next();
};

class AllINodesIterator
{
  BFSync::DbcPtr dbc;
  int dbc_ret;
  BDBPtr bdb_ptr;

  std::set<BFSync::ID> known_ids;
  Dbt key, data;
public:
  AllINodesIterator (BDBPtr bdb_ptr);
  ~AllINodesIterator();

  ID get_next();
};

class Hash2FileIterator
{
  Dbc            *cursor;
  BDBPtr          bdb_ptr;
  int             dbc_ret;
  Dbt             key, data;
public:
  Hash2FileIterator (BDBPtr bdb_ptr);
  ~Hash2FileIterator();

  Hash2FileEntry get_next();
};

class SortedArray
{
private:
  SortedArray (const SortedArray& other); // should not  be used

  std::vector<guint32> array;

public:
  SortedArray();
  ~SortedArray();

  void append (unsigned int n);
  void sort_unique();
  bool search (unsigned int n);
};

class BDBException
{
private:
  BFSync::BDBError error;

public:
  BDBException (BFSync::BDBError error);

  std::string error_string();
};

const unsigned int VERSION_INF = 0xffffffff;

const unsigned int FILE_REGULAR     = BFSync::FILE_REGULAR;
const unsigned int FILE_SYMLINK     = BFSync::FILE_SYMLINK;
const unsigned int FILE_DIR         = BFSync::FILE_DIR;
const unsigned int FILE_FIFO        = BFSync::FILE_FIFO;
const unsigned int FILE_SOCKET      = BFSync::FILE_SOCKET;
const unsigned int FILE_BLOCK_DEV   = BFSync::FILE_BLOCK_DEV;
const unsigned int FILE_CHAR_DEV    = BFSync::FILE_CHAR_DEV;
