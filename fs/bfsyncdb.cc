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

using BFSync::DataOutBuffer;
using BFSync::DataBuffer;
using BFSync::DbcPtr;
using BFSync::BDB_TABLE_CHANGED_INODES;
using BFSync::BDB_TABLE_INODES;
using BFSync::BDB_TABLE_LINKS;
using BFSync::string_printf;
using BFSync::History;

using std::string;
using std::vector;
using std::map;

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

//---------------------------------------------------------------

BDBPtr
open_db (const string& db, int cache_size_mb, bool recover)
{
  BFSync::BDB *bdb = BFSync::bdb_open (db, cache_size_mb, recover);

  BDBWrapper *wrapper = new BDBWrapper;
  wrapper->my_bdb = bdb;

  return BDBPtr (wrapper);
}

void
BDBPtr::close()
{
  ptr->my_bdb->close();
  ptr->my_bdb = NULL;
}

void
BDBPtr::begin_transaction()
{
  ptr->my_bdb->begin_transaction();
}

void
BDBPtr::commit_transaction()
{
  ptr->my_bdb->commit_transaction();
}

void
BDBPtr::abort_transaction()
{
  ptr->my_bdb->abort_transaction();
}

void
id_store (const ID *id, DataOutBuffer& data_buf)
{
  id->id.store (data_buf);
}

void
id_load (ID *id, DataBuffer& dbuf)
{
  id->id = BFSync::ID (dbuf);
  id->valid = true;
}

INode
BDBPtr::load_inode (const ID *id, unsigned int version)
{
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
          inode.id   = *id;
          inode.uid  = dbuffer.read_uint32();
          inode.gid  = dbuffer.read_uint32();
          inode.mode = dbuffer.read_uint32();
          inode.type = BFSync::FileType (dbuffer.read_uint32());
          inode.hash = dbuffer.read_string();
          inode.link = dbuffer.read_string();
          inode.size = dbuffer.read_uint32();
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

void
BDBPtr::store_inode (const INode* inode)
{
  DataOutBuffer kbuf, dbuf;

  id_store (&inode->id, kbuf);
  kbuf.write_table (BDB_TABLE_INODES);

  dbuf.write_uint32 (inode->vmin);
  dbuf.write_uint32 (inode->vmax);
  dbuf.write_uint32 (inode->uid);
  dbuf.write_uint32 (inode->gid);
  dbuf.write_uint32 (inode->mode);
  dbuf.write_uint32 (inode->type);
  dbuf.write_string (inode->hash);
  dbuf.write_string (inode->link);
  dbuf.write_uint32 (inode->size);
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

  DbTxn *txn = ptr->my_bdb->get_transaction();
  g_assert (txn);

  int ret = ptr->my_bdb->get_db()->put (txn, &ikey, &idata, 0);
  g_assert (ret == 0);

  ptr->my_bdb->add_changed_inode (inode->id.id);
}

void
BDBPtr::delete_inode (const INode& inode)
{
  DataOutBuffer kbuf;

  id_store (&inode.id, kbuf);
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

void
BDBPtr::clear_changed_inodes()
{
  ptr->my_bdb->clear_changed_inodes();
}

ID
id_root()
{
  ID id;
  id.id = BFSync::ID::root();
  id.valid = true;
  return id;
}

std::vector<Link>
BDBPtr::load_links (const ID *id, unsigned int version)
{
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
          l.dir_id = *id;
          id_load (&l.inode_id, dbuffer);
          l.name = dbuffer.read_string();

          result.push_back (l);
        }
      ret = dbc->get (&lkey, &ldata, DB_NEXT_DUP);
    }
  return result;
}

void
BDBPtr::store_link (const Link& link)
{
  DbTxn *transaction = ptr->my_bdb->get_transaction();
  g_assert (transaction);

  DataOutBuffer kbuf, dbuf;

  id_store (&link.dir_id, kbuf);
  kbuf.write_table (BDB_TABLE_LINKS);

  dbuf.write_uint32 (link.vmin);
  dbuf.write_uint32 (link.vmax);
  id_store (&link.inode_id, dbuf);
  dbuf.write_string (link.name);

  Dbt lkey (kbuf.begin(), kbuf.size());
  Dbt ldata (dbuf.begin(), dbuf.size());

  int ret = ptr->my_bdb->get_db()->put (transaction, &lkey, &ldata, 0);
  g_assert (ret == 0);
}

void
BDBPtr::delete_link (const Link& link)
{
  DataOutBuffer kbuf, dbuf;

  id_store (&link.dir_id, kbuf);
  kbuf.write_table (BDB_TABLE_LINKS);

  dbuf.write_uint32 (link.vmin);
  dbuf.write_uint32 (link.vmax);
  id_store (&link.inode_id, dbuf);
  dbuf.write_string (link.name);

  Dbt lkey (kbuf.begin(), kbuf.size());
  Dbt ldata (dbuf.begin(), dbuf.size());

  DbcPtr dbc (ptr->my_bdb, DbcPtr::WRITE); /* Acquire a cursor for the database. */

  // iterate over key elements to find link to delete
  int ret = dbc->get (&lkey, &ldata, DB_GET_BOTH);
  while (ret == 0)
    {
      ret = dbc->del (0);
      g_assert (ret == 0);

      ret = dbc->get (&lkey, &ldata, DB_NEXT_DUP);
    }
}

void
do_walk (BDBPtr bdb, const ID& id, const string& prefix = "")
{
  INode inode = bdb.load_inode (&id, 1);
  if (inode.valid)
    {
      if (inode.type == BFSync::FILE_DIR)
        {
          vector<Link> links = bdb.load_links (&id, 1);
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

  bdb_ptr.get_bdb()->history()->read();
  v_old = bdb_ptr.get_bdb()->history()->current_version() - 1;
  v_new = bdb_ptr.get_bdb()->history()->current_version();
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
  result.push_back (string_printf ("%u", inode.size));
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
DiffGenerator::get_next()
{
  while (dbc_ret == 0 && diffs.empty())
    {
      DataBuffer dbuffer ((char *) data.get_data(), data.get_size());

      ID id;
      id_load (&id, dbuffer);  // id is one changed id

      // generate i+ / i- and i! entries for id
      INode i_old = bdb_ptr.load_inode (&id, v_old);
      INode i_new = bdb_ptr.load_inode (&id, v_new);

      if (i_old.valid && i_new.valid)
        {
          diffs.push_back (gen_ibang (i_old, i_new));
        }
      else if (!i_old.valid && i_new.valid)
        {
          diffs.push_back (gen_iplus (i_new));
        }
      else if (i_old.valid && !i_new.valid)
        {
        }

      // generate l+ / l- and l! entries for dir_id = id
      vector<Link> lvec_old = bdb_ptr.load_links (&id, v_old);
      vector<Link> lvec_new = bdb_ptr.load_links (&id, v_new);

      map<string, const Link*> lmap_old;
      map<string, const Link*> lmap_new;

      make_lmap (lmap_old, lvec_old);
      make_lmap (lmap_new, lvec_new);

      for (map<string, const Link*>::iterator mi = lmap_new.begin(); mi != lmap_new.end(); mi++)
        {
          const Link *l_old = lmap_old[mi->first];
          const Link *l_new = lmap_new[mi->first];

          if (!l_old && l_new)
            diffs.push_back (gen_lplus (l_new));
        }

      /* goto next record */
      dbc_ret = dbc->get (&key, &data, DB_NEXT_DUP);
    }
  if (!diffs.empty())
    {
      vector<string> d = diffs.back();
      diffs.pop_back();
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
BDBPtr::add_deleted_file (unsigned int file_number)
{
  ptr->my_bdb->add_deleted_file (file_number);
}

vector<unsigned int>
BDBPtr::load_deleted_files()
{
  return ptr->my_bdb->load_deleted_files();
}

void
BDBPtr::clear_deleted_files()
{
  return ptr->my_bdb->clear_deleted_files();
}

unsigned int
BDBPtr::gen_new_file_number()
{
  return ptr->my_bdb->gen_new_file_number();
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
      id_load (&id, dbuffer);

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
}

INodeHashIterator::~INodeHashIterator()
{
}

string
INodeHashIterator::get_next()
{
  while (dbc_ret == 0)
    {
      string hash;
      char table = ((char *) key.get_data()) [key.get_size() - 1];

      if (table == BDB_TABLE_INODES)
        {
          DataBuffer dbuffer ((char *) data.get_data(), data.get_size());

          dbuffer.read_uint32();
          dbuffer.read_uint32();
          dbuffer.read_uint32();
          dbuffer.read_uint32();
          dbuffer.read_uint32();
          dbuffer.read_uint32();
          hash = dbuffer.read_string();
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
