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
#include <map>
#include <db_cxx.h>
#include <assert.h>
#include "bfinode.hh"
#include "bfhistory.hh"

namespace BFSync
{

enum BDBTables
{
  BDB_TABLE_INODES              = 1,
  BDB_TABLE_LINKS               = 2,
  BDB_TABLE_LOCAL_ID2INO        = 3,
  BDB_TABLE_LOCAL_INO2ID        = 4,
  BDB_TABLE_HISTORY             = 5,
  BDB_TABLE_CHANGED_INODES      = 6,
  BDB_TABLE_CHANGED_INODES_REV  = 7,
  BDB_TABLE_NEW_FILE_NUMBER     = 8,
  BDB_TABLE_DELETED_FILES       = 9,
  BDB_TABLE_TEMP_FILES          = 10,
  BDB_TABLE_JOURNAL             = 11,
};

enum BDBError
{
  BDB_ERROR_NONE = 0,
  BDB_ERROR_UNKNOWN,
  BDB_ERROR_TRANS_ACTIVE,
  BDB_ERROR_NO_TRANS,
  BDB_ERROR_NOT_FOUND,
};

BDB *bdb_open (const std::string& path, int cache_size_mb, bool recover);
bool bdb_need_recover (const std::string& path);

class DataBuffer
{
  const char *ptr;
  size_t      remaining;

public:
  DataBuffer (const char *ptr, size_t size);

  guint64     read_uint64();
  guint32     read_uint32();
  guint32     read_uint32_be();
  std::string read_string();
  void        read_vec_zero (std::vector<char>& vec);
};

class DataOutBuffer
{
  std::vector<char> out;

public:
  DataOutBuffer();

  void write_vec_zero (const std::vector<char>& data);
  void write_string (const std::string& s);
  void write_hash (const std::string& hash);
  void write_uint64 (guint64 i);
  void write_uint32 (guint32 i);
  void write_uint32_be (guint32 i);
  void write_table (char table);

  char*
  begin()
  {
    return &out[0];
  }
  size_t
  size()
  {
    return out.size();
  }
  const std::vector<char>&
  data() const
  {
    return out;
  }
  void
  clear()
  {
    out.clear();
  }
};

struct HistoryEntry
{
  int         version;
  std::string hash;
  std::string author;
  std::string message;
  int         time;
};

struct TempFile
{
  std::string   filename;
  unsigned int  pid;
};

struct JournalEntry
{
  std::string   operation;
  std::string   state;
};

class BDB
{
  DbTxn   *transaction;
  DbEnv   *db_env;
  Db      *db;
  Db      *db_hash2file;
  Db      *db_seq;
  DbSequence *new_file_number_seq;

  std::map<ino_t, ID> new_id2ino_entries;
  History  m_history;

  int      shm_id (const std::string& path);

  std::string  pid_filename;
  std::string  repo_path;

  Mutex mutex;

  void add_pid (const std::string& path);
  int  del_pid();

  BDBError ret2error (int ret);

public:
  Db*       get_db();
  Db*       get_db_hash2file();
  DbEnv*    get_db_env();
  History*  history();

  BDB();

  bool  need_recover (const std::string& path);
  bool  open (const std::string& path, int cache_size_mb, bool recover);
  void  register_pid();
  void  sync();
  bool  close();

  BDBError  begin_transaction();
  BDBError  commit_transaction();
  BDBError  abort_transaction();
  DbTxn*get_transaction();

  void  store_link (const LinkPtr& link);
  void  delete_links (const ID& dir_id, const std::map<std::string, LinkVersionList>& links);
  void  load_links (std::vector<Link*>& links, const ID& id, guint32 version);

  void  store_inode (const INode *inode);
  void  delete_inodes (const INodeVersionList& inodes);
  bool  load_inode (const ID& id, unsigned int version, INode *inode);
  void  add_changed_inode (const ID& id);
  BDBError  clear_changed_inodes (unsigned int max_inodes, unsigned int& result);

  bool  try_store_id2ino (const ID& id, int ino);
  bool  load_ino (const ID& id, ino_t& ino);
  void  store_new_id2ino_entries();

  unsigned int gen_new_file_number();

  bool  load_history_entry (int version, HistoryEntry& entry);
  void  store_history_entry (int version, const HistoryEntry& entry);
  void  delete_history_entry (int version);

  unsigned int load_hash2file (const std::string& hash);
  void  store_hash2file (const std::string& hash, unsigned int file_number);
  void  delete_hash2file (const std::string& hash);

  void  add_deleted_file (unsigned int file_number);
  std::vector<unsigned int>  load_deleted_files();
  BDBError  clear_deleted_files (unsigned int max_files, unsigned int& result);

  std::vector<TempFile> load_temp_files();
  BDBError  add_temp_file (const TempFile& temp_file);
  void  delete_temp_file (const std::string& filename);

  BDBError  load_journal_entries (std::vector<JournalEntry>& entries);
  BDBError  store_journal_entry (const JournalEntry& journal_entry);
  BDBError  clear_journal_entries();
};

class DbcPtr // cursor smart-wrapper: automatically closes cursor in destructor
{
public:
  Dbc *dbc;
  enum Mode { READ, WRITE };

  DbcPtr (BDB *bdb, Mode mode = READ)
  {
    assert (bdb);

    DbTxn *txn = bdb->get_transaction();
    if (mode == WRITE)
      {
        g_assert (txn != NULL); // writing without transaction is a bad idea
      }

    int ret = bdb->get_db()->cursor (txn, &dbc, 0);
    g_assert (ret == 0);
  }
  ~DbcPtr()
  {
    dbc->close();
  }
  Dbc*
  operator->()
  {
    return dbc;
  }
};

}

#endif
