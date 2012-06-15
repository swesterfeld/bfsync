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

#include "bfsyncdb.hh"
#include "bfbdb.hh"
#include "bfleakdebugger.hh"
#include "bfhistory.hh"
#include "bftimeprof.hh"
#include "bfidsorter.hh"
#include "config.h"

#include <db_cxx.h>

#include <algorithm>
#include <set>

using BFSync::DataOutBuffer;
using BFSync::DataBuffer;
using BFSync::DbcPtr;
using BFSync::BDB_TABLE_CHANGED_INODES;
using BFSync::BDB_TABLE_INODES;
using BFSync::BDB_TABLE_LINKS;
using BFSync::string_printf;
using BFSync::History;
using BFSync::TimeProfHandle;
using BFSync::TimeProfSection;
using BFSync::BDBError;

using std::string;
using std::vector;
using std::map;
using std::set;

#undef major
#undef minor

//---------------------------- INode -----------------------------

static BFSync::LeakDebugger inode_leak_debugger ("(Python)BFSync::INode");

INode::INode() :
  valid (false),
  vmin (0), vmax (0),
  uid (0), gid (0),
  mode (0), type (0),
  size (0), major (0), minor (0), nlink (0),
  mtime (0), mtime_ns (0), ctime (0), ctime_ns (0),
  new_file_number (0)
{
  inode_leak_debugger.add (this);
}

INode::INode (const INode& inode) :
  valid (inode.valid),
  vmin (inode.vmin), vmax (inode.vmax),
  id (inode.id),
  uid (inode.uid), gid (inode.gid),
  mode (inode.mode), type (inode.type),
  hash (inode.hash), link (inode.link),
  size (inode.size), major (inode.major), minor (inode.minor), nlink (inode.nlink),
  mtime (inode.mtime), mtime_ns (inode.mtime_ns), ctime (inode.ctime), ctime_ns (inode.ctime_ns),
  new_file_number (inode.new_file_number)
{
  inode_leak_debugger.add (this);
}

INode::~INode()
{
  inode_leak_debugger.del (this);
}

//---------------------------- Link -----------------------------

static BFSync::LeakDebugger link_leak_debugger ("(Python)BFSync::Link");

Link::Link() :
  vmin (0), vmax (0)
{
  link_leak_debugger.add (this);
}

Link::Link (const Link& link) :
  vmin (link.vmin), vmax (link.vmax),
  dir_id (link.dir_id), inode_id (link.inode_id),
  name (link.name)
{
  link_leak_debugger.add (this);
}

Link::~Link()
{
  link_leak_debugger.del (this);
}

//----------------------- HistoryEntry --------------------------

static BFSync::LeakDebugger history_entry_leak_debugger ("(Python)BFSync::HistoryEntry");

HistoryEntry::HistoryEntry() :
  valid (false),
  version (0),
  time (0)
{
  history_entry_leak_debugger.add (this);
}

HistoryEntry::HistoryEntry (const HistoryEntry& he) :
  valid (he.valid),
  version (he.version),
  hash (he.hash), author (he.author), message (he.message),
  time (he.time)
{
  history_entry_leak_debugger.add (this);
}

HistoryEntry::~HistoryEntry()
{
  history_entry_leak_debugger.del (this);
}

//----------------------------- ID ------------------------------

static BFSync::LeakDebugger id_leak_debugger ("(Python)BFSync::ID");

ID::ID() :
  valid (false)
{
  id_leak_debugger.add (this);
}

ID::ID (const ID& id) :
  id (id.id),
  valid (id.valid)
{
  id_leak_debugger.add (this);
}

ID::ID (const string& str) :
  id (str),
  valid (true)
{
  id_leak_debugger.add (this);
}

ID::~ID()
{
  id_leak_debugger.del (this);
}

bool
ID::operator== (const ID& other) const
{
  return id == other.id && valid == other.valid;
}

//---------------------------- TempFile -----------------------------

static BFSync::LeakDebugger temp_file_leak_debugger ("(Python)BFSync::TempFile");

TempFile::TempFile() :
  pid (0)
{
  temp_file_leak_debugger.add (this);
}

TempFile::TempFile (const TempFile& tf) :
  filename (tf.filename),
  pid (tf.pid)
{
  temp_file_leak_debugger.add (this);
}

TempFile::~TempFile()
{
  temp_file_leak_debugger.del (this);
}

//---------------------------- JournalEntry -----------------------------

static BFSync::LeakDebugger journal_entry_leak_debugger ("(Python)BFSync::JournalEntry");

JournalEntry::JournalEntry()
{
  journal_entry_leak_debugger.add (this);
}

JournalEntry::JournalEntry (const JournalEntry& je) :
  operation (je.operation),
  state (je.state)
{
  journal_entry_leak_debugger.add (this);
}

JournalEntry::~JournalEntry()
{
  journal_entry_leak_debugger.del (this);
}

//---------------------------- Hash2FileEntry -----------------------------

static BFSync::LeakDebugger hash2file_entry_leak_debugger ("(Python)BFSync::Hash2FileEntry");

Hash2FileEntry::Hash2FileEntry() :
  valid (false),
  file_number (0)
{
  hash2file_entry_leak_debugger.add (this);
}

Hash2FileEntry::Hash2FileEntry (const Hash2FileEntry& h2f) :
  valid (h2f.valid),
  hash (h2f.hash),
  file_number (h2f.file_number)
{
  hash2file_entry_leak_debugger.add (this);
}

Hash2FileEntry::~Hash2FileEntry()
{
  hash2file_entry_leak_debugger.del (this);
}

//---------------------------------------------------------------

BDBPtr
open_db (const string& db, int cache_size_mb, bool recover)
{
  BFSync::BDB *bdb = BFSync::bdb_open (db, cache_size_mb, recover);

  BDBWrapper *wrapper = new BDBWrapper;
  wrapper->my_bdb = bdb;

  if (bdb)
    bdb->register_pid();

  return BDBPtr (wrapper);
}

void
remove_db (const string& db)
{
  DbEnv db_env (DB_CXX_NO_EXCEPTIONS);

  int ret = db_env.remove (db.c_str(), 0);
  if (ret != 0)
    throw BDBException (BFSync::BDB_ERROR_UNKNOWN);
}

bool
need_recover_db (const string& db)
{
  return BFSync::bdb_need_recover (db);
}

void
BDBPtr::close()
{
  if (ptr->my_bdb)
    ptr->my_bdb->close();
  ptr->my_bdb = NULL;
}

bool
BDBPtr::open_ok()
{
  return ptr->my_bdb != NULL;
}

void
BDBPtr::begin_transaction()
{
  BDBError err = ptr->my_bdb->begin_transaction();
  if (err)
    throw BDBException (err);
}

void
BDBPtr::commit_transaction()
{
  BDBError err = ptr->my_bdb->commit_transaction();
  if (err)
    throw BDBException (err);
}

void
BDBPtr::abort_transaction()
{
  BDBError err = ptr->my_bdb->abort_transaction();
  if (err)
    throw BDBException (err);
}

void
id_store (const ID& id, DataOutBuffer& data_buf)
{
  id.id.store (data_buf);
}

void
id_load (ID& id, DataBuffer& dbuf)
{
  id.id = BFSync::ID (dbuf);
  id.valid = true;
}

TimeProfSection tp_load_inode ("bfsyncdb.load_inode");

INode
BDBPtr::load_inode (const ID& id, unsigned int version)
{
  TimeProfHandle h (tp_load_inode);

  INode inode;
  DataOutBuffer kbuf;

  id_store (id, kbuf);
  kbuf.write_table (BDB_TABLE_INODES);

  Dbt ikey (kbuf.begin(), kbuf.size());
  Dbt idata;

  DbcPtr dbc (ptr->my_bdb); /* Acquire a cursor for the database. */

  int ret = dbc->get (&ikey, &idata, DB_SET);
  while (ret == 0)
    {
      DataBuffer dbuffer ((char *) idata.get_data(), idata.get_size());

      inode.vmin = dbuffer.read_uint32();
      inode.vmax = dbuffer.read_uint32();

      if (version >= inode.vmin && version <= inode.vmax)
        {
          inode.id   = id;
          inode.uid  = dbuffer.read_uint32();
          inode.gid  = dbuffer.read_uint32();
          inode.mode = dbuffer.read_uint32();
          inode.type = BFSync::FileType (dbuffer.read_uint32());
          inode.hash = dbuffer.read_string();
          inode.link = dbuffer.read_string();
          inode.size = dbuffer.read_uint64();
          inode.major = dbuffer.read_uint32();
          inode.minor = dbuffer.read_uint32();
          inode.nlink = dbuffer.read_uint32();
          inode.ctime = dbuffer.read_uint32();
          inode.ctime_ns = dbuffer.read_uint32();
          inode.mtime = dbuffer.read_uint32();
          inode.mtime_ns = dbuffer.read_uint32();
          inode.new_file_number = dbuffer.read_uint32();

          inode.valid = true; // found
          return inode;
        }
      ret = dbc->get (&ikey, &idata, DB_NEXT_DUP);
    }

  // not found -> return INode with valid == false
  inode = INode();
  g_assert (inode.valid == false);
  return inode;
}

TimeProfSection tp_load_all_inodes ("bfsyncdb.load_all_inodes");

vector<INode>
BDBPtr::load_all_inodes (const ID& id)
{
  TimeProfHandle h (tp_load_all_inodes);

  vector<INode> all_inodes;

  INode inode;
  DataOutBuffer kbuf;

  id_store (id, kbuf);
  kbuf.write_table (BDB_TABLE_INODES);

  Dbt ikey (kbuf.begin(), kbuf.size());
  Dbt idata;

  DbcPtr dbc (ptr->my_bdb); /* Acquire a cursor for the database. */

  int ret = dbc->get (&ikey, &idata, DB_SET);
  while (ret == 0)
    {
      DataBuffer dbuffer ((char *) idata.get_data(), idata.get_size());

      inode.vmin = dbuffer.read_uint32();
      inode.vmax = dbuffer.read_uint32();

      inode.id   = id;
      inode.uid  = dbuffer.read_uint32();
      inode.gid  = dbuffer.read_uint32();
      inode.mode = dbuffer.read_uint32();
      inode.type = BFSync::FileType (dbuffer.read_uint32());
      inode.hash = dbuffer.read_string();
      inode.link = dbuffer.read_string();
      inode.size = dbuffer.read_uint64();
      inode.major = dbuffer.read_uint32();
      inode.minor = dbuffer.read_uint32();
      inode.nlink = dbuffer.read_uint32();
      inode.ctime = dbuffer.read_uint32();
      inode.ctime_ns = dbuffer.read_uint32();
      inode.mtime = dbuffer.read_uint32();
      inode.mtime_ns = dbuffer.read_uint32();
      inode.new_file_number = dbuffer.read_uint32();

      inode.valid = true;
      all_inodes.push_back (inode);
      ret = dbc->get (&ikey, &idata, DB_NEXT_DUP);
    }
  return all_inodes;
}

TimeProfSection tp_store_inode ("bfsyncdb.store_inode");

void
BDBPtr::store_inode (const INode& inode)
{
  TimeProfHandle h (tp_store_inode);

  DataOutBuffer kbuf, dbuf;

  id_store (inode.id, kbuf);
  kbuf.write_table (BDB_TABLE_INODES);

  dbuf.write_uint32 (inode.vmin);
  dbuf.write_uint32 (inode.vmax);
  dbuf.write_uint32 (inode.uid);
  dbuf.write_uint32 (inode.gid);
  dbuf.write_uint32 (inode.mode);
  dbuf.write_uint32 (inode.type);
  dbuf.write_string (inode.hash);
  dbuf.write_string (inode.link);
  dbuf.write_uint64 (inode.size);
  dbuf.write_uint32 (inode.major);
  dbuf.write_uint32 (inode.minor);
  dbuf.write_uint32 (inode.nlink);
  dbuf.write_uint32 (inode.ctime);
  dbuf.write_uint32 (inode.ctime_ns);
  dbuf.write_uint32 (inode.mtime);
  dbuf.write_uint32 (inode.mtime_ns);
  dbuf.write_uint32 (inode.new_file_number);

  Dbt ikey (kbuf.begin(), kbuf.size());
  Dbt idata (dbuf.begin(), dbuf.size());

  DbTxn *txn = ptr->my_bdb->get_transaction();
  g_assert (txn);

  int ret = ptr->my_bdb->get_db()->put (txn, &ikey, &idata, 0);
  g_assert (ret == 0);

  ptr->my_bdb->add_changed_inode (inode.id.id);
}

TimeProfSection tp_delete_inode ("bfsyncdb.delete_inode");

void
BDBPtr::delete_inode (const INode& inode)
{
  if (!ptr->my_bdb->get_transaction())
    throw BDBException (BFSync::BDB_ERROR_NO_TRANS);

  TimeProfHandle h (tp_delete_inode);

  DataOutBuffer kbuf;

  id_store (inode.id, kbuf);
  kbuf.write_table (BDB_TABLE_INODES);

  Dbt ikey (kbuf.begin(), kbuf.size());
  Dbt idata;

  DbcPtr dbc (ptr->my_bdb, DbcPtr::WRITE); /* Acquire a cursor for the database. */

  // iterate over key elements to find inode to delete
  int ret = dbc->get (&ikey, &idata, DB_SET);
  while (ret == 0)
    {
      DataBuffer dbuffer ((char *) idata.get_data(), idata.get_size());

      unsigned int vmin = dbuffer.read_uint32();
      unsigned int vmax = dbuffer.read_uint32();

      if (inode.vmin == vmin && inode.vmax == vmax)
        {
          ret = dbc->del (0);
          g_assert (ret == 0);
        }
      ret = dbc->get (&ikey, &idata, DB_NEXT_DUP);
    }
}

unsigned int
BDBPtr::clear_changed_inodes (unsigned int max_inodes)
{
  unsigned int result;

  BDBError err = ptr->my_bdb->clear_changed_inodes (max_inodes, result);
  if (err)
    throw BDBException (err);

  return result;
}

ID
id_root()
{
  ID id;
  id.id = BFSync::ID::root();
  id.valid = true;
  return id;
}

TimeProfSection tp_load_links ("bfsyncdb.load_links");

std::vector<Link>
BDBPtr::load_links (const ID& id, unsigned int version)
{
  TimeProfHandle h (tp_load_links);

  vector<Link> result;

  DataOutBuffer kbuf;

  id_store (id, kbuf);
  kbuf.write_table (BDB_TABLE_LINKS);

  Dbt lkey (kbuf.begin(), kbuf.size());
  Dbt ldata;

  DbcPtr dbc (ptr->my_bdb); /* Acquire a cursor for the database. */

  // iterate over key elements and delete records which are in LinkVersionList
  int ret = dbc->get (&lkey, &ldata, DB_SET);
  while (ret == 0)
    {
      DataBuffer dbuffer ((char *) ldata.get_data(), ldata.get_size());

      guint32 vmin = dbuffer.read_uint32();
      guint32 vmax = dbuffer.read_uint32();

      if (version >= vmin && version <= vmax)
        {
          Link l;

          l.vmin = vmin;
          l.vmax = vmax;
          l.dir_id = id;
          id_load (l.inode_id, dbuffer);
          l.name = dbuffer.read_string();

          result.push_back (l);
        }
      ret = dbc->get (&lkey, &ldata, DB_NEXT_DUP);
    }
  return result;
}

TimeProfSection tp_load_all_links ("bfsyncdb.load_all_links");

std::vector<Link>
BDBPtr::load_all_links (const ID& id)
{
  TimeProfHandle h (tp_load_all_links);

  vector<Link> result;

  DataOutBuffer kbuf;

  id_store (id, kbuf);
  kbuf.write_table (BDB_TABLE_LINKS);

  Dbt lkey (kbuf.begin(), kbuf.size());
  Dbt ldata;

  DbcPtr dbc (ptr->my_bdb); /* Acquire a cursor for the database. */

  // iterate over key elements and delete records which are in LinkVersionList
  int ret = dbc->get (&lkey, &ldata, DB_SET);
  while (ret == 0)
    {
      DataBuffer dbuffer ((char *) ldata.get_data(), ldata.get_size());

      Link l;

      l.vmin = dbuffer.read_uint32();
      l.vmax = dbuffer.read_uint32();
      l.dir_id = id;
      id_load (l.inode_id, dbuffer);
      l.name = dbuffer.read_string();

      result.push_back (l);

      ret = dbc->get (&lkey, &ldata, DB_NEXT_DUP);
    }
  return result;
}

TimeProfSection tp_store_link ("bfsyncdb.store_link");

void
BDBPtr::store_link (const Link& link)
{
  TimeProfHandle h (tp_store_link);

  DbTxn *transaction = ptr->my_bdb->get_transaction();
  g_assert (transaction);

  DataOutBuffer kbuf, dbuf;

  id_store (link.dir_id, kbuf);
  kbuf.write_table (BDB_TABLE_LINKS);

  dbuf.write_uint32 (link.vmin);
  dbuf.write_uint32 (link.vmax);
  id_store (link.inode_id, dbuf);
  dbuf.write_string (link.name);

  Dbt lkey (kbuf.begin(), kbuf.size());
  Dbt ldata (dbuf.begin(), dbuf.size());

  int ret = ptr->my_bdb->get_db()->put (transaction, &lkey, &ldata, 0);
  g_assert (ret == 0);

  ptr->my_bdb->add_changed_inode (link.dir_id.id);
}

TimeProfSection tp_delete_link ("bfsyncdb.delete_link");

void
BDBPtr::delete_link (const Link& link)
{
  TimeProfHandle h (tp_delete_link);

  DataOutBuffer kbuf, dbuf;

  id_store (link.dir_id, kbuf);
  kbuf.write_table (BDB_TABLE_LINKS);

  dbuf.write_uint32 (link.vmin);
  dbuf.write_uint32 (link.vmax);
  id_store (link.inode_id, dbuf);
  dbuf.write_string (link.name);

  Dbt lkey (kbuf.begin(), kbuf.size());
  Dbt ldata (dbuf.begin(), dbuf.size());

  DbcPtr dbc (ptr->my_bdb, DbcPtr::WRITE); /* Acquire a cursor for the database. */

  // iterate over key elements to find link to delete
  int ret = dbc->get (&lkey, &ldata, DB_GET_BOTH);
  while (ret == 0)
    {
      size_t size = ldata.get_size();
      if (dbuf.size() == size && memcmp (dbuf.begin(), ldata.get_data(), size) == 0)
        {
          ret = dbc->del (0);
          g_assert (ret == 0);
        }

      ret = dbc->get (&lkey, &ldata, DB_NEXT_DUP);
    }

  ptr->my_bdb->add_changed_inode (link.dir_id.id);
}

TimeProfSection tp_delete_links ("bfsyncdb.delete_links");

void
BDBPtr::delete_links (const vector<Link>& links)
{
  if (links.size() == 0)
    return; // nothing to do

  TimeProfHandle h (tp_delete_links);

  set< vector<char> > delete_data_set;
  vector<char> all_key;

  for (vector<Link>::const_iterator li = links.begin(); li != links.end(); li++)
    {
      const Link& link = *li;

      DataOutBuffer kbuf, dbuf;

      id_store (link.dir_id, kbuf);
      kbuf.write_table (BDB_TABLE_LINKS);

      dbuf.write_uint32 (link.vmin);
      dbuf.write_uint32 (link.vmax);
      id_store (link.inode_id, dbuf);
      dbuf.write_string (link.name);

      // all links must belong to the same inode id
      if (li == links.begin())
        {
          all_key = kbuf.data();
        }
      else
        {
          g_assert (all_key == kbuf.data());
        }

      delete_data_set.insert (dbuf.data());
    }

  Dbt lkey (&all_key[0], all_key.size());
  Dbt ldata;

  DbcPtr dbc (ptr->my_bdb, DbcPtr::WRITE); /* Acquire a cursor for the database. */

  // iterate over key elements to find link to delete
  int ret = dbc->get (&lkey, &ldata, DB_SET);
  while (ret == 0)
    {
      char *dbegin = (char *) ldata.get_data();
      char *dend   = dbegin + ldata.get_size();

      vector<char> data (dbegin, dend);
      if (delete_data_set.count (data) != 0)
        {
          ret = dbc->del (0);
          g_assert (ret == 0);
        }

      ret = dbc->get (&lkey, &ldata, DB_NEXT_DUP);
    }

  ptr->my_bdb->add_changed_inode (links[0].dir_id.id);
}

void
BDBPtr::add_temp_file (const string& filename, unsigned int pid)
{
  BFSync::TempFile f;

  f.filename = filename;
  f.pid = pid;

  BDBError err = ptr->my_bdb->add_temp_file (f);
  if (err)
    throw BDBException (err);
}

std::vector<TempFile>
BDBPtr::load_temp_files()
{
  std::vector<TempFile> result;

  std::vector<BFSync::TempFile> temp_files = ptr->my_bdb->load_temp_files();
  for (vector<BFSync::TempFile>::const_iterator ti = temp_files.begin(); ti != temp_files.end(); ti++)
    {
      TempFile tf;
      tf.filename = ti->filename;
      tf.pid = ti->pid;
      result.push_back (tf);
    }

  return result;
}

void
BDBPtr::delete_temp_file (const string& name)
{
  ptr->my_bdb->delete_temp_file (name);
}

std::vector<JournalEntry>
BDBPtr::load_journal_entries()
{
  std::vector<BFSync::JournalEntry> jentries;
  BDBError err = ptr->my_bdb->load_journal_entries (jentries);
  if (err)
    throw BDBException (err);

  std::vector<JournalEntry> result;

  for (vector<BFSync::JournalEntry>::const_iterator ji = jentries.begin(); ji != jentries.end(); ji++)
    {
      JournalEntry je;
      je.operation = ji->operation;
      je.state     = ji->state;
      result.push_back (je);
    }
  return result;
}

void
BDBPtr::store_journal_entry (const JournalEntry& journal_entry)
{
  BFSync::JournalEntry je;
  je.operation = journal_entry.operation;
  je.state     = journal_entry.state;

  BDBError err = ptr->my_bdb->store_journal_entry (je);
  if (err)
    throw BDBException (err);
}

void
BDBPtr::clear_journal_entries()
{
  BDBError err = ptr->my_bdb->clear_journal_entries();
  if (err)
    throw BDBException (err);
}

void
do_walk (BDBPtr bdb, const ID& id, const string& prefix = "")
{
  INode inode = bdb.load_inode (id, 1);
  if (inode.valid)
    {
      if (inode.type == BFSync::FILE_DIR)
        {
          vector<Link> links = bdb.load_links (id, 1);
          for (vector<Link>::iterator li = links.begin(); li != links.end(); li++)
            {
              printf ("%s/%s\n", prefix.c_str(), li->name.c_str());
              do_walk (bdb, li->inode_id, prefix + "/" + li->name);
            }
        }
    }
}

void
BDBPtr::walk()
{
  ID root = id_root();
  do_walk (*this, root);
}

DiffGenerator::DiffGenerator (BDBPtr bdb_ptr) :
  dbc (bdb_ptr.get_bdb()),
  bdb_ptr (bdb_ptr)
{
  kbuf.write_table (BDB_TABLE_CHANGED_INODES);

  key = Dbt (kbuf.begin(), kbuf.size());

  dbc_ret = dbc->get (&key, &data, DB_SET);
  while (dbc_ret == 0)
    {
      DataBuffer dbuffer ((char *) data.get_data(), data.get_size());

      ID id;
      id_load (id, dbuffer);  // id is one changed id
      ids.insert (id.id);

      /* goto next record */
      dbc_ret = dbc->get (&key, &data, DB_NEXT_DUP);
    }
  ids.sort();

  bdb_ptr.get_bdb()->history()->read();
  v_old = bdb_ptr.get_bdb()->history()->current_version() - 1;
  v_new = bdb_ptr.get_bdb()->history()->current_version();
  id_pos = 0;
}

DiffGenerator::~DiffGenerator()
{
}

void
make_lmap (map<string, const Link*>& lmap, const vector<Link>& links)
{
  for (vector<Link>::const_iterator li = links.begin(); li != links.end(); li++)
    {
      const Link& link = *li;
      lmap[link.name] = &link;
    }
}

string
print_id (const ID& id)
{
  return id.id.str();
}

vector<string>
gen_iplus (const INode& inode)
{
  vector<string> result;
  result.push_back ("i+");
  result.push_back (print_id (inode.id));
  result.push_back (string_printf ("%u", inode.uid));
  result.push_back (string_printf ("%u", inode.gid));
  result.push_back (string_printf ("%u", inode.mode));
  result.push_back (string_printf ("%u", inode.type));
  result.push_back (inode.hash);
  result.push_back (inode.link);
  result.push_back (string_printf ("%" G_GUINT64_FORMAT, inode.size));
  result.push_back (string_printf ("%u", inode.major));
  result.push_back (string_printf ("%u", inode.minor));
  result.push_back (string_printf ("%u", inode.nlink));
  result.push_back (string_printf ("%u", inode.ctime));
  result.push_back (string_printf ("%u", inode.ctime_ns));
  result.push_back (string_printf ("%u", inode.mtime));
  result.push_back (string_printf ("%u", inode.mtime_ns));
  return result;
}

vector<string>
gen_ibang (const INode& i_old, const INode& i_new)
{
  vector<string> result;
  vector<string> old_str = gen_iplus (i_old);
  vector<string> new_str = gen_iplus (i_new);
  g_assert (old_str.size() > 2 && old_str.size() == new_str.size());

  result.push_back ("i!");
  result.push_back (old_str[1]);
  for (size_t i = 2; i < old_str.size(); i++)
    {
      if (old_str[i] != new_str[i])
        result.push_back (new_str[i]);
      else
        result.push_back ("");    // attribute not changed
    }
  return result;
}

vector<string>
gen_iminus (const INode& inode)
{
  vector<string> result;
  result.push_back ("i-");
  result.push_back (print_id (inode.id));
  return result;
}

vector<string>
gen_lplus (const Link *link)
{
  vector<string> result;
  result.push_back ("l+");
  result.push_back (print_id (link->dir_id));
  result.push_back (link->name);
  result.push_back (print_id (link->inode_id));
  return result;
}

vector<string>
gen_lbang (const Link *link)
{
  vector<string> result;
  result.push_back ("l!");
  result.push_back (print_id (link->dir_id));
  result.push_back (link->name);
  result.push_back (print_id (link->inode_id));
  return result;
}

vector<string>
gen_lminus (const Link *link)
{
  vector<string> result;
  result.push_back ("l-");
  result.push_back (print_id (link->dir_id));
  result.push_back (link->name);
  return result;
}

vector<string>
DiffGenerator::get_next()
{
  while (id_pos < ids.size() && diffs.empty())
    {
      ID id;
      id.id = ids.id (id_pos++);

      // generate i+ / i- and i! entries for id
      INode i_old = bdb_ptr.load_inode (id, v_old);
      INode i_new = bdb_ptr.load_inode (id, v_new);

      if (i_old.valid && i_new.valid)
        {
          if (i_old.vmin != i_new.vmin || i_old.vmax != i_new.vmax)
            diffs.push (gen_ibang (i_old, i_new));
        }
      else if (!i_old.valid && i_new.valid)
        {
          diffs.push (gen_iplus (i_new));
        }
      else if (i_old.valid && !i_new.valid)
        {
          diffs.push (gen_iminus (i_old));
        }

      // generate l+ / l- and l! entries for dir_id = id
      vector<Link> lvec_old = bdb_ptr.load_links (id, v_old);
      vector<Link> lvec_new = bdb_ptr.load_links (id, v_new);

      map<string, const Link*> lmap_old;
      map<string, const Link*> lmap_new;

      make_lmap (lmap_old, lvec_old);
      make_lmap (lmap_new, lvec_new);

      // sort l+ / l- and l! entries by link name
      set<string> link_names;

      for (map<string, const Link*>::iterator mi = lmap_new.begin(); mi != lmap_new.end(); mi++)
        link_names.insert (mi->first);

      for (map<string, const Link*>::iterator mi = lmap_old.begin(); mi != lmap_old.end(); mi++)
        link_names.insert (mi->first);

      for (set<string>::iterator lni = link_names.begin(); lni != link_names.end(); lni++)
        {
          const Link *l_old = lmap_old[*lni];
          const Link *l_new = lmap_new[*lni];

          if (!l_old && l_new)
            {
              diffs.push (gen_lplus (l_new));
            }
          else if (l_old && l_new)
            {
              if (l_old->inode_id.id != l_new->inode_id.id)
                diffs.push (gen_lbang (l_new));
            }
          else if (l_old && !l_new)
            {
              diffs.push (gen_lminus (l_old));
            }
        }
    }
  if (!diffs.empty())
    {
      vector<string> d = diffs.front();
      diffs.pop();
      return d;
    }
  else
    {
      return vector<string>();
    }
}

void
BDBPtr::store_history_entry (int version, const string& hash, const string& author, const string& message, int time)
{
  BFSync::HistoryEntry he;

  he.version = version;
  he.hash    = hash;
  he.author  = author;
  he.message = message;
  he.time    = time;

  ptr->my_bdb->store_history_entry (version, he);
}

HistoryEntry
BDBPtr::load_history_entry (int version)
{
  BFSync::HistoryEntry he;
  HistoryEntry result;

  if (ptr->my_bdb->load_history_entry (version, he))
    {
      result.valid      = true;
      result.version    = he.version;
      result.hash       = he.hash;
      result.author     = he.author;
      result.message    = he.message;
      result.time       = he.time;
    }
  else
    {
      result.valid      = false;

      result.version = result.time = 0;
      result.hash = result.author = result.message = "";
    }
  return result;
}

void
BDBPtr::delete_history_entry (unsigned int version)
{
  ptr->my_bdb->delete_history_entry (version);
}

unsigned int
BDBPtr::load_hash2file (const string& hash)
{
  return ptr->my_bdb->load_hash2file (hash);
}

void
BDBPtr::store_hash2file (const string& hash, unsigned int file_number)
{
  ptr->my_bdb->store_hash2file (hash, file_number);
}

void
BDBPtr::delete_hash2file (const string& hash)
{
  ptr->my_bdb->delete_hash2file (hash);
}

void
BDBPtr::add_deleted_file (unsigned int file_number)
{
  ptr->my_bdb->add_deleted_file (file_number);
}

vector<unsigned int>
BDBPtr::load_deleted_files()
{
  return ptr->my_bdb->load_deleted_files();
}

unsigned int
BDBPtr::clear_deleted_files (unsigned int max_files)
{
  unsigned int result;

  BDBError err = ptr->my_bdb->clear_deleted_files (max_files, result);
  if (err)
    throw BDBException (err);

  return result;
}

unsigned int
BDBPtr::gen_new_file_number()
{
  return ptr->my_bdb->gen_new_file_number();
}

//---------------------------- BDBPtr::tags -----------------------------

std::vector<std::string>
BDBPtr::list_tags (unsigned int version)
{
  return ptr->my_bdb->list_tags (version);
}

std::vector<std::string>
BDBPtr::load_tag (unsigned int version, const string& tag)
{
  return ptr->my_bdb->load_tag (version, tag);
}

void
BDBPtr::add_tag (unsigned int version, const string& tag, const string& value)
{
  BDBError err = ptr->my_bdb->add_tag (version, tag, value);
  if (err)
    throw BDBException (err);
}

void
BDBPtr::del_tag (unsigned int version, const string& tag, const string& value)
{
  BDBError err = ptr->my_bdb->del_tag (version, tag, value);
  if (err)
    throw BDBException (err);
}

/* refcounting BDB wrapper */

BDBPtr::BDBPtr (BDBWrapper *wrapper) :
  ptr (wrapper)
{
}

BDBPtr::BDBPtr (const BDBPtr& other)
{
  BDBWrapper *new_ptr = other.ptr;

  if (new_ptr)
    new_ptr->ref();

  ptr = new_ptr;
}

BDBPtr&
BDBPtr::operator=(const BDBPtr& other)
{
  BDBWrapper *new_ptr = other.ptr;
  BDBWrapper *old_ptr = ptr;

  if (new_ptr)
    new_ptr->ref();

  ptr = new_ptr;

  if (old_ptr)
    old_ptr->unref();

  return *this;
}


BDBPtr::~BDBPtr()
{
  if (ptr)
    {
      ptr->unref();
      /* eager deletion */
      if (ptr->has_zero_refs())
        delete ptr;
      ptr = NULL;
    }
}

BDBWrapper::BDBWrapper() :
  ref_count (1)
{
}

BDBWrapper::~BDBWrapper()
{
  if (my_bdb)
    {
      my_bdb->close();
      delete my_bdb;

      my_bdb = NULL;
    }
}

ChangedINodesIterator::ChangedINodesIterator (BDBPtr bdb_ptr) :
  dbc (bdb_ptr.get_bdb()),
  bdb_ptr (bdb_ptr)
{
  kbuf.write_table (BDB_TABLE_CHANGED_INODES);

  key = Dbt (kbuf.begin(), kbuf.size());
  dbc_ret = dbc->get (&key, &data, DB_SET);
}

ID
ChangedINodesIterator::get_next()
{
  if (dbc_ret == 0)
    {
      DataBuffer dbuffer ((char *) data.get_data(), data.get_size());

      ID id;
      id_load (id, dbuffer);

      /* goto next record */
      dbc_ret = dbc->get (&key, &data, DB_NEXT_DUP);

      return id;
    }
  ID xid;
  xid.valid = false;
  return xid;
}

ChangedINodesIterator::~ChangedINodesIterator()
{
}

INodeHashIterator::INodeHashIterator (BDBPtr bdb_ptr) :
  dbc (bdb_ptr.get_bdb()),
  bdb_ptr (bdb_ptr)
{
  dbc_ret = dbc->get (&key, &data, DB_FIRST);
  bdb_ptr.get_bdb()->history()->read();
}

INodeHashIterator::~INodeHashIterator()
{
}

string
INodeHashIterator::get_next()
{
  const set<unsigned int>& deleted_versions = bdb_ptr.get_bdb()->history()->deleted_versions();

  while (dbc_ret == 0)
    {
      string hash;
      char table = ((char *) key.get_data()) [key.get_size() - 1];

      if (table == BDB_TABLE_INODES)
        {
          DataBuffer dbuffer ((char *) data.get_data(), data.get_size());

          unsigned int vmin = dbuffer.read_uint32();
          unsigned int vmax = dbuffer.read_uint32();

          bool needed = false;
          if (vmax == VERSION_INF)
            {
              needed = true;
            }
          else
            {
              for (unsigned int version = vmin; version <= vmax; version++)
                {
                  if (!deleted_versions.count (version))
                    {
                      needed = true;
                      break;
                    }
                }
            }

          if (needed)
            {
              dbuffer.read_uint32();
              dbuffer.read_uint32();
              dbuffer.read_uint32();
              dbuffer.read_uint32();

              hash = dbuffer.read_string();
            }
        }
      dbc_ret = dbc->get (&key, &data, DB_NEXT);

      if (hash.size() == 40)  /* skip empty hash (for instance symlink) and "new" hash (newly changed inode) */
        {
          if (all_hashes.count (hash) == 0) // deduplication: never return the same hash twice
            {
              all_hashes.insert (hash);
              return hash;
            }
        }
    }
  return "";
}

AllINodesIterator::AllINodesIterator (BDBPtr bdb_ptr) :
  dbc (bdb_ptr.get_bdb()),
  bdb_ptr (bdb_ptr)
{
  dbc_ret = dbc->get (&key, &data, DB_FIRST);
}

AllINodesIterator::~AllINodesIterator()
{
  // known_ids.print_mem_usage();
}

ID
AllINodesIterator::get_next()
{
  while (dbc_ret == 0)
    {
      char table = ((char *) key.get_data()) [key.get_size() - 1];
      ID id;
      bool rid = false;

      if (table == BDB_TABLE_INODES)
        {
          DataBuffer kbuffer ((char *) key.get_data(), key.get_size());
          id_load (id, kbuffer);

          DataOutBuffer id_buf;
          /* we don't store the prefix part, because a..e should be enough to figure out
           * duplicates reliably - this results in a slightly reduced memory usage and
           * it should be a bit faster (because now all known IDs have the same size) */
          id_buf.write_uint32 (id.id.a);
          id_buf.write_uint32 (id.id.b);
          id_buf.write_uint32 (id.id.c);
          id_buf.write_uint32 (id.id.d);
          id_buf.write_uint32 (id.id.e);
          if (!known_ids.insert ((unsigned char *) id_buf.begin()))
            rid = true;
        }
      dbc_ret = dbc->get (&key, &data, DB_NEXT);
      if (rid)
        return id;
    }
  ID xid;
  xid.valid = false;
  return xid;
}

Hash2FileIterator::Hash2FileIterator (BDBPtr bdb_ptr) :
  bdb_ptr (bdb_ptr)
{
  int ret;

  DbTxn *txn = bdb_ptr.get_bdb()->get_transaction();

  /* Acquire a cursor for the database. */
  if ((ret = bdb_ptr.get_bdb()->get_db_hash2file()->cursor (txn, &cursor, 0)) != 0)
    {
      bdb_ptr.get_bdb()->get_db_hash2file()->err (ret, "DB->cursor");
      cursor = 0;
      dbc_ret = ret;
      return;
    }

  dbc_ret = cursor->get (&key, &data, DB_FIRST);
}

Hash2FileIterator::~Hash2FileIterator()
{
  if (cursor)
    cursor->close();
}


Hash2FileEntry
Hash2FileIterator::get_next()
{
  Hash2FileEntry h2f;

  if (dbc_ret == 0)
    {
      const unsigned char *kptr = (unsigned char *) key.get_data();
      DataBuffer dbuffer ((char *) data.get_data(), data.get_size());

      string hash;
      for (size_t i = 0; i < key.get_size(); i++)
        hash += string_printf ("%02x", kptr[i]); // slow!

      h2f.hash = hash;
      h2f.file_number = dbuffer.read_uint32();
      h2f.valid = true;

      dbc_ret = cursor->get (&key, &data, DB_NEXT);
      return h2f;
    }
  h2f.valid = false;
  return h2f;
}

//---------------------------- SortedArray -----------------------------

static BFSync::LeakDebugger sorted_array_leak_debugger ("(Python)BFSync::SortedArray");

SortedArray::SortedArray()
{
  sorted_array_leak_debugger.add (this);
}

SortedArray::~SortedArray()
{
  sorted_array_leak_debugger.del (this);
}

void
SortedArray::append (unsigned int i)
{
  array.push_back (i);
}

void
SortedArray::sort_unique()
{
  std::sort (array.begin(), array.end());

  // dedup
  vector<guint32>::iterator ai = std::unique (array.begin(), array.end());
  array.resize (ai - array.begin());
}

bool
SortedArray::search (unsigned int i)
{
  return std::binary_search (array.begin(), array.end(), i);
}

unsigned int
SortedArray::mem_usage()
{
  return array.capacity() * sizeof (&array[0]);
}

//---------------------------- INodeRepo -----------------------------

INodeRepoINode::INodeRepoINode (BFSync::INodePtr ptr) :
  ptr (ptr)
{
}

unsigned int
INodeRepoINode::uid()
{
  return ptr->uid;
}

void
INodeRepoINode::set_uid (unsigned int uid)
{
  ptr.update()->uid = uid;
}

unsigned int
INodeRepoINode::gid()
{
  return ptr->gid;
}

void
INodeRepoINode::set_gid (unsigned int gid)
{
  ptr.update()->gid = gid;
}

unsigned int
INodeRepoINode::type()
{
  return ptr->type;
}

void
INodeRepoINode::set_type (unsigned int type)
{
  ptr.update()->type = static_cast<BFSync::FileType> (type);
}

unsigned int
INodeRepoINode::mode()
{
  return ptr->mode;
}

void
INodeRepoINode::set_mode (unsigned int mode)
{
  ptr.update()->mode = mode;
}

string
INodeRepoINode::hash()
{
  return ptr->hash;
}

void
INodeRepoINode::set_hash (const string& hash)
{
  ptr.update()->hash = hash;
}

// link field

string
INodeRepoINode::link()
{
  return ptr->link;
}

void
INodeRepoINode::set_link (const string& link)
{
  ptr.update()->link = link;
}

// valid

bool
INodeRepoINode::valid()
{
  return ptr;
}

void
INodeRepoINode::add_link (INodeRepoINode& child, const string& name, unsigned int version)
{
  BFSync::Context ctx;
  ctx.version = version;

  ptr.update()->add_link (ctx, child.ptr, name);
}


vector<string>
INodeRepoINode::get_child_names (unsigned int version)
{
  BFSync::Context ctx;
  vector<string> result;

  ctx.version = version;
  ptr->get_child_names (ctx, result);
  return result;
}

INodeRepoINode
INodeRepoINode::get_child (unsigned int version, const string& name)
{
  BFSync::Context ctx;
  ctx.version = version;

  return INodeRepoINode (ptr->get_child (ctx, name));
}

INodeRepo::INodeRepo (BDBPtr bdb)
{
  assert (!BFSync::INodeRepo::the());
  inode_repo = new BFSync::INodeRepo (bdb.get_bdb());
  assert (BFSync::INodeRepo::the());
}

INodeRepo::~INodeRepo()
{
  assert (BFSync::INodeRepo::the());

  delete inode_repo;
  inode_repo = NULL;

  assert (!BFSync::INodeRepo::the());
}

INodeRepoINode
INodeRepo::load_inode (const ID& id, unsigned int version)
{
  BFSync::Context ctx;
  ctx.version = version;

  return INodeRepoINode (BFSync::INodePtr (ctx, id.id));
}

INodeRepoINode
INodeRepo::create_inode (const string& name, unsigned int version)
{
  BFSync::Context ctx;
  ctx.version = version;

  return INodeRepoINode (BFSync::INodePtr (ctx, name.c_str()));
}

INodeRepoINode
INodeRepo::create_inode_with_id (const ID& id, unsigned int version)
{
  BFSync::Context ctx;
  ctx.version = version;

  return INodeRepoINode (BFSync::INodePtr (ctx, NULL, &id.id));
}

void
INodeRepo::save_changes()
{
  inode_repo->save_changes();
}

//---------------------------- BDBException -----------------------------

BDBException::BDBException (BDBError error) :
  error (error)
{
}

string
BDBException::error_string()
{
  switch (error)
    {
      case BFSync::BDB_ERROR_NONE:
        return "no error";

      case BFSync::BDB_ERROR_UNKNOWN:
        return "unknown error";

      case BFSync::BDB_ERROR_TRANS_ACTIVE:
        return "transaction started, but another transaction is still active";

      case BFSync::BDB_ERROR_NO_TRANS:
        return "no transaction started";

      case BFSync::BDB_ERROR_NOT_FOUND:
        return "key not found";
    }
  return "unknown error";
}

string
time_prof_result()
{
  return BFSync::TimeProf::the()->result();
}

void
time_prof_reset()
{
  BFSync::TimeProf::the()->reset();
}

string
repo_version()
{
  return VERSION;
}
