/*
  bfsync: Big File synchronization tool - FUSE filesystem

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

#include "bfbdb.hh"
#include "bftimeprof.hh"
#include "bfcfgparser.hh"
#include <db_cxx.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>

#include <set>


using std::string;
using std::vector;
using std::set;
using std::map;

namespace BFSync
{

BDB*
bdb_open (const string& path, int cache_size_mb, bool recover)
{
  BDB *bdb = new BDB();

  if (bdb->open (path, cache_size_mb, recover))
    return bdb;
  else
    return NULL;
}

BDB::BDB() :
  transaction (NULL),
  m_history (this)
{
}

bool
BDB::open (const string& path, int cache_size_mb, bool recover)
{
  Lock lock (mutex);

  if (need_recover (path) && !recover)
    {
      printf ("============================================================================\n");
      printf ("%s: need to recover repository\n", path.c_str());
      printf ("============================================================================\n");
      printf (" - some processes did not shut down properly\n");
      printf (" - use bfsync.py recover %s to fix this\n", path.c_str());
      printf ("============================================================================\n");
      return false;
    }

  try
    {
      bool result = true;
      int ret;

      int cache_size_gb = cache_size_mb / 1024;
      cache_size_mb %= 1024;

      string bdb_dir = path + "/bdb";

      repo_path = path;

      db_env = new DbEnv (DB_CXX_NO_EXCEPTIONS);
      db_env->set_shm_key (shm_id (path));
      db_env->set_cachesize (cache_size_gb, cache_size_mb * 1024 * 1024, 0); // set cache size
      db_env->set_lk_max_locks (100000);
      db_env->set_lk_max_objects (100000);
      db_env->log_set_config (DB_LOG_AUTO_REMOVE, 1);     // automatically remove old log files
      ret = db_env->open (bdb_dir.c_str(),
        DB_CREATE |            /* on-demand create */
        DB_INIT_MPOOL |        /* shared memory buffer subsystem */
        DB_INIT_TXN |          /* transactions */
        DB_INIT_LOG |          /* logging */
        DB_INIT_LOCK |         /* locking */
        (recover ? DB_RECOVER : 0) |           /* run recover */
        DB_SYSTEM_MEM,         /* use shared memory (instead of mmap) */
        0);
      if (ret == 0)
        {
          db = new Db (db_env, 0);
          db->set_flags (DB_DUP);       // allow duplicate keys

          // Open the database
          u_int32_t oFlags = DB_CREATE | DB_AUTO_COMMIT; // Open flags;

          ret = db->open (NULL,               // Transaction pointer
                          "db",               // Database name
                          NULL,               // Optional logical database name
                          DB_BTREE,           // Database access method
                          oFlags,             // Open flags
                          0);                 // File mode (using defaults)
          if (ret != 0)
            {
              db->err (ret, "open database 'db' failed");
              result = false;
            }


          db_hash2file = new Db (db_env, 0);
          ret = db_hash2file->open (NULL,
                                    "db_hash2file",
                                    NULL,
                                    DB_BTREE,
                                    oFlags,
                                    0);
          if (ret != 0)
            {
              db_hash2file->err (ret, "open database 'db_hash2file' failed");
              result = false;
            }

          db_seq = new Db (db_env, 0);
          ret = db_seq->open (NULL,
                              "db_seq",
                              NULL,
                              DB_BTREE,
                              oFlags,
                              0);
          if (ret != 0)
            {
              db_seq->err (ret, "open database 'db_seq' failed");
              result = false;
            }
          if (db_seq)
            {
              DataOutBuffer kbuf;

              kbuf.write_table (BDB_TABLE_NEW_FILE_NUMBER);
              Dbt key (kbuf.begin(), kbuf.size());

              new_file_number_seq = new DbSequence (db_seq, 0);
              ret = new_file_number_seq->set_cachesize (1024);
              g_assert (ret == 0);

              ret = new_file_number_seq->initial_value (1);
              g_assert (ret == 0);

              ret = new_file_number_seq->open (NULL, &key, DB_CREATE);
              g_assert (ret == 0);
            }
        }
      else
        {
          db_env->err (ret, "open database env failed");
          db = NULL;
          db_hash2file = NULL;
          db_seq = NULL;
          result = false;
        }

      if (!result)
        {
          if (db)
            {
              db->close (0);
              delete db;
              db = NULL;
            }
          if (db_hash2file)
            {
              db_hash2file->close (0);
              delete db_hash2file;
              db_hash2file = NULL;
            }
          if (db_seq)
            {
              db_seq->close (0);
              delete db_seq;
              db_seq = NULL;
            }
          if (db_env)
            {
              db_env->close (0);
              delete db_env;
              db_env = NULL;
            }
        }
      return result;
    }
  catch (...)
    {
      return false;
    }
}

bool
BDB::close()
{
  assert (db_env != 0);
  assert (db != 0);
  assert (db_hash2file != 0);

  int ret = db->close (0);
  delete db;
  db = NULL;

  assert (ret == 0);

  ret = db_hash2file->close (0);
  delete db_hash2file;
  db_hash2file = NULL;

  assert (ret == 0);

  ret = db_seq->close (0);
  delete db_seq;
  db_seq = NULL;

  assert (ret == 0);

  ret = db_env->close (0);
  delete db_env;
  db_env = NULL;

  del_pid();

  return (ret == 0);
}

void
BDB::begin_transaction()
{
  Lock lock (mutex);

  assert (transaction == NULL);

  int ret = db_env->txn_begin (NULL, &transaction, 0);
  g_assert (ret == 0);
  g_assert (transaction);
}

TimeProfSection tp_commit_transaction ("BDB::commit_transaction");

void
BDB::commit_transaction()
{
  Lock lock (mutex);

  TimeProfHandle h (tp_commit_transaction);

  int ret = transaction->commit (0);
  g_assert (ret == 0);

  /* checkpoint database every minute (min = 1) */
  ret = db_env->txn_checkpoint (0, 1, 0);
  g_assert (ret == 0);

  transaction = NULL;
}

void
BDB::abort_transaction()
{
  Lock lock (mutex);

  int ret = transaction->abort();
  g_assert (ret == 0);

  transaction = NULL;
}

DbTxn*
BDB::get_transaction()
{
  return transaction;
}

DataOutBuffer::DataOutBuffer()
{
  out.reserve (256);   // should be enough for most cases (avoids reallocs)
}

void
DataOutBuffer::write_string (const string& s)
{
  out.insert (out.end(), s.begin(), s.end());
  out.push_back (0);
}

void
DataOutBuffer::write_hash (const string& hash)
{
  unsigned char bin_hash[20];

  assert (hash.size() == 40);

  for (size_t i = 0; i < 20; i++)
    {
      unsigned char h = from_hex_nibble (hash[i * 2]);
      unsigned char l = from_hex_nibble (hash[i * 2 + 1]);
      assert (h < 16 && l < 16);

      bin_hash[i] = (h << 4) + l;
    }
  out.insert (out.end(), bin_hash, bin_hash + 20);
}

void
DataOutBuffer::write_vec_zero (const std::vector<char>& data)
{
  out.insert (out.end(), data.begin(), data.end());
  out.push_back (0);
}

void
DataOutBuffer::write_uint64 (guint64 i)
{
  char *s = reinterpret_cast <char *> (&i);
  assert (sizeof (guint64) == 8);

  out.insert (out.end(), s, s + 8);
}

void
DataOutBuffer::write_uint32 (guint32 i)
{
  char *s = reinterpret_cast <char *> (&i);
  assert (sizeof (guint32) == 4);

  out.insert (out.end(), s, s + 4);
}

void
DataOutBuffer::write_uint32_be (guint32 i)
{
  write_uint32 (GUINT32_TO_BE (i));
}

void
DataOutBuffer::write_table (char table)
{
  out.push_back (table);
}

void
write_link_data (DataOutBuffer& db_out, const LinkPtr& lp)
{
  db_out.write_uint32 (lp->vmin);
  db_out.write_uint32 (lp->vmax);
  lp->inode_id.store (db_out);
  db_out.write_string (lp->name);
}

TimeProfSection tp_store_link ("BDB::store_link");

void
BDB::store_link (const LinkPtr& lp)
{
  Lock lock (mutex);

  TimeProfHandle h (tp_store_link);

  g_assert (transaction != NULL);

  DataOutBuffer kbuf, dbuf;

  lp->dir_id.store (kbuf);
  kbuf.write_table (BDB_TABLE_LINKS);

  write_link_data (dbuf, lp);

  Dbt lkey (kbuf.begin(), kbuf.size());
  Dbt ldata (dbuf.begin(), dbuf.size());

  int ret = db->put (transaction, &lkey, &ldata, 0);
  assert (ret == 0);
}

TimeProfSection tp_delete_links ("BDB::delete_links");

void
BDB::delete_links (const ID& dir_id, const map<string, LinkVersionList>& link_map)
{
  Lock lock (mutex);

  TimeProfHandle h (tp_delete_links);

  DataOutBuffer kbuf;

  dir_id.store (kbuf);
  kbuf.write_table (BDB_TABLE_LINKS);

  Dbt lkey (kbuf.begin(), kbuf.size());
  Dbt ldata;

  DbcPtr dbc (this, DbcPtr::WRITE); /* Acquire a cursor for the database. */

  // iterate over key elements and delete records which are in LinkVersionList
  int ret = dbc->get (&lkey, &ldata, DB_SET);
  while (ret == 0)
    {
      DataBuffer dbuffer ((char *) ldata.get_data(), ldata.get_size());

      guint32 vmin = dbuffer.read_uint32();
      guint32 vmax = dbuffer.read_uint32();
      ID inode_id (dbuffer);
      string name = dbuffer.read_string();

      bool del = false;

      map<string, LinkVersionList>::const_iterator mapi = link_map.find (name);
      if (mapi != link_map.end())
        {
          const LinkVersionList& links = mapi->second;
          for (size_t i = 0; i < links.size(); i++)
            {
              if (links[i]->inode_id == inode_id && (links[i]->vmin == vmin || links[i]->vmax == vmax))
                del = true;
            }
        }

      if (del)
        {
          ret = dbc->del (0);
          assert (ret == 0);
        }
      ret = dbc->get (&lkey, &ldata, DB_NEXT_DUP);
    }
}

TimeProfSection tp_load_links ("BDB::load_links");

void
BDB::load_links (std::vector<Link*>& links, const ID& id, guint32 version)
{
  Lock lock (mutex);

  TimeProfHandle h (tp_load_links);

  DataOutBuffer kbuf;

  id.store (kbuf);
  kbuf.write_table (BDB_TABLE_LINKS);

  Dbt lkey (kbuf.begin(), kbuf.size());
  Dbt ldata;

  DbcPtr dbc (this); /* Acquire a cursor for the database. */

  // iterate over key elements and delete records which are in LinkVersionList
  int ret = dbc->get (&lkey, &ldata, DB_SET);
  while (ret == 0)
    {
      DataBuffer dbuffer ((char *) ldata.get_data(), ldata.get_size());

      guint32 vmin = dbuffer.read_uint32();
      guint32 vmax = dbuffer.read_uint32();
      ID inode_id (dbuffer);
      string name = dbuffer.read_string();

      if (version >= vmin && version <= vmax)
        {
          Link *l = new Link;

          l->vmin = vmin;
          l->vmax = vmax;
          l->dir_id = id;
          l->inode_id = inode_id;
          l->name = name;
          l->updated = false;

          links.push_back (l);
        }
      ret = dbc->get (&lkey, &ldata, DB_NEXT_DUP);
    }
}

DbEnv*
BDB::get_db_env()
{
  return db_env;
}

Db*
BDB::get_db()
{
  return db;
}

Db*
BDB::get_db_hash2file()
{
  return db_hash2file;
}

History*
BDB::history()
{
  return &m_history;
}

DataBuffer::DataBuffer (const char *ptr, size_t size) :
  ptr (ptr),
  remaining (size)
{
}

guint64
DataBuffer::read_uint64()
{
  assert (remaining >= 8);

  guint64 result;
  memcpy (&result, ptr, 8);
  remaining -= 8;
  ptr += 8;

  return result;
}

guint32
DataBuffer::read_uint32()
{
  assert (remaining >= 4);

  guint32 result;
  memcpy (&result, ptr, 4);
  remaining -= 4;
  ptr += 4;

  return result;
}

guint32
DataBuffer::read_uint32_be()
{
  return GUINT32_FROM_BE (read_uint32());
}

string
DataBuffer::read_string()
{
  string s;

  while (remaining)
    {
      char c = *ptr++;
      remaining--;

      if (c == 0)
        return s;
      else
        s += c;
    }
  assert (false);
}

void
DataBuffer::read_vec_zero (vector<char>& vec)
{
  while (remaining)
    {
      char c = *ptr++;
      remaining--;

      if (c == 0)
        return;
      else
        vec.push_back (c);
    }
  assert (false);
}

TimeProfSection tp_store_inode ("BDB::store_inode");

void
BDB::store_inode (const INode *inode)
{
  Lock lock (mutex);

  TimeProfHandle h (tp_store_inode);

  g_assert (transaction != NULL);

  DataOutBuffer kbuf, dbuf;

  inode->id.store (kbuf);
  kbuf.write_table (BDB_TABLE_INODES);

  dbuf.write_uint32 (inode->vmin);
  dbuf.write_uint32 (inode->vmax);
  dbuf.write_uint32 (inode->uid);
  dbuf.write_uint32 (inode->gid);
  dbuf.write_uint32 (inode->mode);
  dbuf.write_uint32 (inode->type);
  dbuf.write_string (inode->hash);
  dbuf.write_string (inode->link);
  dbuf.write_uint64 (inode->size);
  dbuf.write_uint32 (inode->major);
  dbuf.write_uint32 (inode->minor);
  dbuf.write_uint32 (inode->nlink);
  dbuf.write_uint32 (inode->ctime);
  dbuf.write_uint32 (inode->ctime_ns);
  dbuf.write_uint32 (inode->mtime);
  dbuf.write_uint32 (inode->mtime_ns);
  dbuf.write_uint32 (inode->new_file_number);

  Dbt ikey (kbuf.begin(), kbuf.size());
  Dbt idata (dbuf.begin(), dbuf.size());

  int ret = db->put (transaction, &ikey, &idata, 0);
  assert (ret == 0);
}

TimeProfSection tp_delete_inodes ("BDB::delete_inodes");

/**
 * delete INodes records which have
 *  - the right INode ID
 *  - matching vmin OR matching vmax
 */
void
BDB::delete_inodes (const INodeVersionList& inodes)
{
  Lock lock (mutex);

  TimeProfHandle h (tp_delete_inodes);

  if (inodes.size() == 0) /* nothing to do? */
    return;

  set<int> vmin_del;
  set<int> vmax_del;
  vector<char> all_key;

  for (size_t i = 0; i < inodes.size(); i++)
    {
      DataOutBuffer kbuf;

      inodes[i]->id.store (kbuf);
      kbuf.write_table (BDB_TABLE_INODES);
      if (i == 0)
        {
          all_key = kbuf.data();
        }
      else
        {
          assert (all_key == kbuf.data()); // all inodes should share the same key
        }
      vmin_del.insert (inodes[i]->vmin);
      vmax_del.insert (inodes[i]->vmax);
    }

  Dbt ikey (&all_key[0], all_key.size());
  Dbt idata;


  DbcPtr dbc (this, DbcPtr::WRITE); /* Acquire a cursor for the database. */

  // iterate over key elements and delete records which are in INodeVersionList
  int ret = dbc->get (&ikey, &idata, DB_SET);
  while (ret == 0)
    {
      DataBuffer dbuffer ((char *) idata.get_data(), idata.get_size());

      int vmin = dbuffer.read_uint32();
      int vmax = dbuffer.read_uint32();

      if (vmin_del.find (vmin) != vmin_del.end())
        {
          ret = dbc->del (0);
          assert (ret == 0);
        }
      else if (vmax_del.find (vmax) != vmax_del.end())
        {
          ret = dbc->del (0);
          assert (ret == 0);
        }
      ret = dbc->get (&ikey, &idata, DB_NEXT_DUP);
    }
}

TimeProfSection tp_load_inode ("BDB::load_inode");

bool
BDB::load_inode (const ID& id, unsigned int version, INode *inode)
{
  Lock lock (mutex);

  TimeProfHandle h (tp_load_inode);

  DataOutBuffer kbuf;

  id.store (kbuf);
  kbuf.write_table (BDB_TABLE_INODES);

  Dbt ikey (kbuf.begin(), kbuf.size());
  Dbt idata;

  DbcPtr dbc (this); /* Acquire a cursor for the database. */

  int ret = dbc->get (&ikey, &idata, DB_SET);
  while (ret == 0)
    {
      DataBuffer dbuffer ((char *) idata.get_data(), idata.get_size());

      inode->vmin = dbuffer.read_uint32();
      inode->vmax = dbuffer.read_uint32();

      if (version >= inode->vmin && version <= inode->vmax)
        {
          inode->id   = id;
          inode->uid  = dbuffer.read_uint32();
          inode->gid  = dbuffer.read_uint32();
          inode->mode = dbuffer.read_uint32();
          inode->type = BFSync::FileType (dbuffer.read_uint32());
          inode->hash = dbuffer.read_string();
          inode->link = dbuffer.read_string();
          inode->size = dbuffer.read_uint64();
          inode->major = dbuffer.read_uint32();
          inode->minor = dbuffer.read_uint32();
          inode->nlink = dbuffer.read_uint32();
          inode->ctime = dbuffer.read_uint32();
          inode->ctime_ns = dbuffer.read_uint32();
          inode->mtime = dbuffer.read_uint32();
          inode->mtime_ns = dbuffer.read_uint32();
          inode->new_file_number = dbuffer.read_uint32();
          return true;
        }
      ret = dbc->get (&ikey, &idata, DB_NEXT_DUP);
    }
  return false;
}

TimeProfSection tp_try_store_id2ino ("BDB::try_store_id2ino");

bool
BDB::try_store_id2ino (const ID& id, int ino)
{
  Lock lock (mutex);

  TimeProfHandle h (tp_try_store_id2ino);

  map<ino_t, ID>::const_iterator ni = new_id2ino_entries.find (ino);
  if (ni != new_id2ino_entries.end())  // recently allocated?
    return false;

  DataOutBuffer kbuf;

  // lookup ino to check whether it is already used:
  kbuf.write_uint32_be (ino);                  /* use big endian storage to make Berkeley DB sort entries properly */
  kbuf.write_table (BDB_TABLE_LOCAL_INO2ID);

  Dbt rev_ikey (kbuf.begin(), kbuf.size());
  Dbt rev_lookup;

  int ret = db->get (NULL, &rev_ikey, &rev_lookup, 0);
  if (ret == 0)
    return false;

  new_id2ino_entries[ino] = id;
  return true;
}

TimeProfSection tp_store_new_id2ino_entries ("BDB::store_new_id2ino_entries");

void
BDB::store_new_id2ino_entries()
{
  Lock lock (mutex);

  TimeProfHandle h (tp_store_new_id2ino_entries);

  g_assert (transaction != NULL);

  for (map<ino_t, ID>::const_iterator ni = new_id2ino_entries.begin(); ni != new_id2ino_entries.end(); ni++)
    {
      const ino_t& ino = ni->first;
      const ID&    id = ni->second;

      DataOutBuffer kbuf, dbuf;

      // add ino->id entry
      kbuf.write_uint32_be (ino);                  /* use big endian storage to make Berkeley DB sort entries properly */
      kbuf.write_table (BDB_TABLE_LOCAL_INO2ID);

      id.store (dbuf);
      Dbt rev_idata (dbuf.begin(), dbuf.size());
      Dbt rev_ikey (kbuf.begin(), kbuf.size());

      int ret = db->put (transaction, &rev_ikey, &rev_idata, 0);
      assert (ret == 0);

      kbuf.clear();
      dbuf.clear();

      // add id->ino entry
      id.store (kbuf);
      kbuf.write_table (BDB_TABLE_LOCAL_ID2INO);

      dbuf.write_uint32 (ino);

      Dbt ikey (kbuf.begin(), kbuf.size());
      Dbt idata (dbuf.begin(), dbuf.size());

      ret = db->put (transaction, &ikey, &idata, 0);
      assert (ret == 0);
    }
  // clear new entries
  new_id2ino_entries.clear();
}

TimeProfSection tp_load_ino ("BDB::load_ino");

bool
BDB::load_ino (const ID& id, ino_t& ino)
{
  Lock lock (mutex);

  TimeProfHandle h (tp_load_ino);

  DataOutBuffer kbuf;

  id.store (kbuf);
  kbuf.write_table (BDB_TABLE_LOCAL_ID2INO);

  Dbt ikey (kbuf.begin(), kbuf.size());
  Dbt idata;

  if (db->get (NULL, &ikey, &idata, 0) != 0)
    return false;

  DataBuffer dbuffer ((char *) idata.get_data(), idata.get_size());

  ino = dbuffer.read_uint32();
  return true;
}

void
BDB::store_history_entry (int version, const HistoryEntry& he)
{
  assert (version == he.version);

  g_assert (transaction != NULL);

  delete_history_entry (version);

  Lock lock (mutex);

  DataOutBuffer kbuf, dbuf;

  kbuf.write_uint32_be (version);        /* use big endian storage to make Berkeley DB sort entries properly */
  kbuf.write_table (BDB_TABLE_HISTORY);

  dbuf.write_string (he.hash);
  dbuf.write_string (he.author);
  dbuf.write_string (he.message);
  dbuf.write_uint32 (he.time);

  Dbt hkey (kbuf.begin(), kbuf.size());
  Dbt hdata (dbuf.begin(), dbuf.size());

  int ret = db->put (transaction, &hkey, &hdata, 0);
  assert (ret == 0);
}

bool
BDB::load_history_entry (int version, HistoryEntry& he)
{
  Lock lock (mutex);

  DataOutBuffer kbuf;

  kbuf.write_uint32_be (version);        /* use big endian storage to make Berkeley DB sort entries properly */
  kbuf.write_table (BDB_TABLE_HISTORY);

  Dbt hkey (kbuf.begin(), kbuf.size());
  Dbt hdata;

  if (db->get (transaction, &hkey, &hdata, 0) != 0)
    return false;

  DataBuffer dbuffer ((char *) hdata.get_data(), hdata.get_size());

  he.version = version;
  he.hash = dbuffer.read_string();
  he.author = dbuffer.read_string();
  he.message = dbuffer.read_string();
  he.time = dbuffer.read_uint32();

  return true;
}

void
BDB::delete_history_entry (int version)
{
  Lock lock (mutex);

  DataOutBuffer kbuf;

  kbuf.write_uint32_be (version);        /* use big endian storage to make Berkeley DB sort entries properly */
  kbuf.write_table (BDB_TABLE_HISTORY);

  Dbt hkey (kbuf.begin(), kbuf.size());
  Dbt hdata;

  DbcPtr dbc (this, DbcPtr::WRITE);

  // iterate over key elements and delete records which are in LinkVersionList
  int ret = dbc->get (&hkey, &hdata, DB_SET);
  while (ret == 0)
    {
      ret = dbc->del (0);
      assert (ret == 0);

      ret = dbc->get (&hkey, &hdata, DB_NEXT_DUP);
    }
}

TimeProfSection tp_add_changed_inode ("BDB::add_changed_inode");

void
BDB::add_changed_inode (const ID& id)
{
  Lock lock (mutex);

  TimeProfHandle h (tp_add_changed_inode);

  g_assert (transaction != NULL);

  // reverse lookup: inode already in changed set?
  int ret;
  DataOutBuffer kbuf, dbuf;

  id.store (kbuf);
  kbuf.write_table (BDB_TABLE_CHANGED_INODES_REV);

  Dbt rev_cikey (kbuf.begin(), kbuf.size());
  Dbt rev_cidata;

  ret = db->get (transaction, &rev_cikey, &rev_cidata, 0);
  if (ret == 0)
    return;       // => inode already in changed set

  // add reverse entry
  ret = db->put (transaction, &rev_cikey, &rev_cidata, 0);
  assert (ret == 0);

  // add to changed inodes table
  kbuf.clear();
  dbuf.clear();

  kbuf.write_table (BDB_TABLE_CHANGED_INODES);
  id.store (dbuf);

  Dbt cikey (kbuf.begin(), kbuf.size());
  Dbt cidata (dbuf.begin(), dbuf.size());

  ret = db->put (transaction, &cikey, &cidata, 0);
  assert (ret == 0);
}

void
BDB::clear_changed_inodes()
{
  Lock lock (mutex);

  g_assert (transaction != NULL);

  DataOutBuffer kbuf;
  kbuf.write_table (BDB_TABLE_CHANGED_INODES);

  Dbt key (kbuf.begin(), kbuf.size());
  Dbt data;

  DbcPtr dbc (this, DbcPtr::WRITE);  /* get write cursor */

  int ret = dbc->get (&key, &data, DB_SET);
  while (ret == 0)
    {
      DataBuffer dbuffer ((char *) data.get_data(), data.get_size());
      ID id (dbuffer);

      // delete normal entry
      dbc->del (0);

      // delete reverse entry
      DataOutBuffer rev_kbuf;

      id.store (rev_kbuf);
      rev_kbuf.write_table (BDB_TABLE_CHANGED_INODES_REV);

      Dbt rev_key (rev_kbuf.begin(), rev_kbuf.size());
      ret = db->del (transaction, &rev_key, 0);
      g_assert (ret == 0);

      /* goto next record */
      ret = dbc->get (&key, &data, DB_NEXT_DUP);
    }
}

TimeProfSection tp_gen_new_file_number ("BDB::gen_new_file_number");

unsigned int
BDB::gen_new_file_number()
{
  Lock lock (mutex);

  TimeProfHandle h (tp_gen_new_file_number);

  DataOutBuffer kbuf;

  kbuf.write_table (BDB_TABLE_NEW_FILE_NUMBER);
  Dbt key (kbuf.begin(), kbuf.size());

  db_seq_t value;
  int ret = new_file_number_seq->get (NULL, 1, &value, 0);
  g_assert (ret == 0);

  return value;
}

unsigned int
BDB::load_hash2file (const string& hash)
{
  DataOutBuffer kbuf;
  kbuf.write_hash (hash);

  Dbt key (kbuf.begin(), kbuf.size());
  Dbt data;

  int ret = db_hash2file->get (transaction, &key, &data, 0);
  if (ret == 0)
    {
      DataBuffer dbuffer ((char *) data.get_data(), data.get_size());
      unsigned int file_number = dbuffer.read_uint32();
      return file_number;
    }

  return 0; // not found
}

TimeProfSection tp_store_hash2file ("BDB::store_hash2file");

void
BDB::store_hash2file (const string& hash, unsigned int file_number)
{
  Lock lock (mutex);

  TimeProfHandle h (tp_store_hash2file);

  g_assert (transaction);

  DataOutBuffer kbuf, dbuf;
  kbuf.write_hash (hash);
  dbuf.write_uint32 (file_number);

  Dbt key (kbuf.begin(), kbuf.size());
  Dbt data (dbuf.begin(), dbuf.size());

  int ret = db_hash2file->put (transaction, &key, &data, 0);
  assert (ret == 0);
}

void
BDB::add_deleted_file (unsigned int file_number)
{
  Lock lock (mutex);

  g_assert (transaction);

  DataOutBuffer kbuf, dbuf;
  kbuf.write_table (BDB_TABLE_DELETED_FILES);
  dbuf.write_uint32 (file_number);

  Dbt key (kbuf.begin(), kbuf.size());
  Dbt data (dbuf.begin(), dbuf.size());

  int ret = db->put (transaction, &key, &data, 0);
  assert (ret == 0);
}

vector<unsigned int>
BDB::load_deleted_files()
{
  Lock lock (mutex);

  vector<unsigned int> files;

  DataOutBuffer kbuf;
  kbuf.write_table (BDB_TABLE_DELETED_FILES);

  Dbt key (kbuf.begin(), kbuf.size());
  Dbt data;

  DbcPtr dbc (this); /* Acquire a cursor for the database. */

  // iterate over all deleted files
  int ret = dbc->get (&key, &data, DB_SET);
  while (ret == 0)
    {
      DataBuffer dbuffer ((char *) data.get_data(), data.get_size());

      guint32 file_number = dbuffer.read_uint32();
      files.push_back (file_number);

      ret = dbc->get (&key, &data, DB_NEXT_DUP);
    }
  return files;
}

void
BDB::clear_deleted_files()
{
  Lock lock (mutex);

  g_assert (transaction != NULL);

  DataOutBuffer kbuf;
  kbuf.write_table (BDB_TABLE_DELETED_FILES);

  Dbt key (kbuf.begin(), kbuf.size());

  int ret = db->del (transaction, &key, 0);
  g_assert (ret == 0 || ret == DB_NOTFOUND);
}

void
BDB::add_temp_file (const TempFile& temp_file)
{
  Lock lock (mutex);

  g_assert (transaction);

  DataOutBuffer kbuf, dbuf;
  kbuf.write_table (BDB_TABLE_TEMP_FILES);
  dbuf.write_string (temp_file.filename);
  dbuf.write_uint32 (temp_file.pid);

  Dbt key (kbuf.begin(), kbuf.size());
  Dbt data (dbuf.begin(), dbuf.size());

  int ret = db->put (transaction, &key, &data, 0);
  assert (ret == 0);
}

vector<TempFile>
BDB::load_temp_files()
{
  Lock lock (mutex);

  vector<TempFile> files;

  DataOutBuffer kbuf;
  kbuf.write_table (BDB_TABLE_TEMP_FILES);

  Dbt key (kbuf.begin(), kbuf.size());
  Dbt data;

  DbcPtr dbc (this); /* Acquire a cursor for the database. */

  // iterate over all temp files
  int ret = dbc->get (&key, &data, DB_SET);
  while (ret == 0)
    {
      DataBuffer dbuffer ((char *) data.get_data(), data.get_size());

      TempFile tf;
      tf.filename = dbuffer.read_string();
      tf.pid = dbuffer.read_uint32();

      files.push_back (tf);

      ret = dbc->get (&key, &data, DB_NEXT_DUP);
    }
  return files;
}

void
BDB::delete_temp_file (const string& name)
{
  Lock lock (mutex);

  g_assert (transaction);

  DataOutBuffer kbuf;

  kbuf.write_table (BDB_TABLE_TEMP_FILES);

  Dbt tkey (kbuf.begin(), kbuf.size());
  Dbt tdata;

  DbcPtr dbc (this, DbcPtr::WRITE);

  // iterate over all temp files
  int ret = dbc->get (&tkey, &tdata, DB_SET);
  while (ret == 0)
    {
      DataBuffer dbuffer ((char *) tdata.get_data(), tdata.get_size());
      if (dbuffer.read_string() == name)
        {
          ret = dbc->del (0);
          assert (ret == 0);
        }

      ret = dbc->get (&tkey, &tdata, DB_NEXT_DUP);
    }
}

static inline int
make_shm_key (int n)
{
  return n * 256 | 0xbfbdb000;
}

int
BDB::shm_id (const string& path)
{
  CfgParser repo_cfg_parser;
  if (!repo_cfg_parser.parse (path + "/config"))
    {
      printf ("parse error in repo config:\n%s\n", repo_cfg_parser.error().c_str());
      exit (1);
    }

  int new_key = 1;

  map<string, vector<string> > cfg_values = repo_cfg_parser.values();
  const vector<string>& repo_id = cfg_values["repo-id"];
  if (repo_id.size() == 1)
    {
      string keys_filename = string_printf ("%s/%s", g_get_home_dir(), ".bfsync_keys");
      if (g_file_test (keys_filename.c_str(), GFileTest (G_FILE_TEST_IS_REGULAR | G_FILE_TEST_EXISTS)))
        {
          CfgParser keys_parser;
          if (keys_parser.parse (keys_filename))
            {
              map<string, vector<string> > keys_values = keys_parser.values();
              for (map<string, vector<string> >::iterator i = keys_values.begin(); i != keys_values.end(); i++)
                {
                  for (size_t k = 0; k < i->second.size(); k++)
                    new_key = std::max (atoi (i->second[k].c_str()) + 1, new_key);
                }

              vector<string>& v = keys_values["key-" + repo_id[0]];
              if (v.size() == 1)
                {
                  return make_shm_key (atoi (v[0].c_str()));
                }
            }
          else
            {
              printf ("parse error in %s:\n%s\n", keys_filename.c_str(), keys_parser.error().c_str());
            }
        }
      string apath;

      if (!g_path_is_absolute (path.c_str()))
        apath = g_get_current_dir() + string (G_DIR_SEPARATOR + path);
      else
        apath = path;

      FILE *keys_file = fopen (keys_filename.c_str(), "a");
      assert (keys_file);
      fprintf (keys_file, "key-%s %d; # path \"%s\"\n", repo_id[0].c_str(), new_key, apath.c_str());
      fclose (keys_file);
    }
  return make_shm_key (new_key);
}


bool
BDB::need_recover (const string& repo_path)
{
  int dead_count = 0;

  GDir *dir = g_dir_open ((repo_path + "/processes").c_str(), 0, NULL);
  if (dir)
    {
      const char *name;

      while ((name = g_dir_read_name (dir)))
        {
          int pid = atoi (name);
          g_assert (pid > 0);

          if (kill (pid, 0) != 0)
            {
              dead_count++;
            }
        }
      g_dir_close (dir);
    }
  return dead_count > 0;
}

void
BDB::add_pid (const string& repo_path)
{
  pid_filename = string_printf ("%s/processes/%d", repo_path.c_str(), getpid());
  FILE *pid_file = fopen (pid_filename.c_str(), "w");
  g_assert (pid_file);
  fclose (pid_file);
}

void
BDB::del_pid()
{
  if (!pid_filename.empty())
    unlink (pid_filename.c_str());
}

void
BDB::register_pid()
{
  add_pid (repo_path);
}

}
