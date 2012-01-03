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
#include <db_cxx.h>
#include <assert.h>
#include <string.h>

#include <set>


using std::string;
using std::vector;
using std::set;

namespace BFSync
{

BDB *bdb = NULL;
Db *db = NULL;

bool
bdb_open (const string& path)
{
  assert (!db);
  assert (!bdb);

  try
    {
      bdb = new BDB();

      Lock lock (bdb->mutex);

      db = new Db (NULL, 0);
      db->set_cachesize (1, 0, 0);  // 1 GB cache size
      db->set_flags (DB_DUP);       // allow duplicate keys

      // Open the database
      u_int32_t oFlags = DB_CREATE; // Open flags;

      db->open (NULL,               // Transaction pointer
                path.c_str(),       // Database file name
                NULL,               // Optional logical database name
                DB_BTREE,           // Database access method
                oFlags,             // Open flags
                0);                 // File mode (using defaults)

      return true;
    }
  catch (...)
    {
      db = NULL;
      bdb = NULL;
      return false;
    }
}

bool
bdb_close()
{
  assert (db != 0);
  assert (bdb != 0);

  delete bdb;
  bdb = NULL;

  int ret = db->close (0);
  delete db;
  db = NULL;

  return (ret == 0);
}

DataOutBuffer::DataOutBuffer (vector<char>& out) :
  out (out)
{
}

void
DataOutBuffer::write_string (const string& s)
{
  out.insert (out.end(), s.begin(), s.end());
  out.push_back (0);
}

void
DataOutBuffer::write_vec_zero (const std::vector<char>& data)
{
  out.insert (out.end(), data.begin(), data.end());
  out.push_back (0);
}

void
DataOutBuffer::write_uint32 (guint32 i)
{
  char *s = reinterpret_cast <char *> (&i);
  assert (sizeof (guint32) == 4);

  out.insert (out.end(), s, s + 4);
}

void
DataOutBuffer::write_table (char table)
{
  out.push_back (table);
}

void
write_link_data (vector<char>& out, const LinkPtr& lp)
{
  DataOutBuffer db_out (out);

  db_out.write_uint32 (lp->vmin);
  db_out.write_uint32 (lp->vmax);
  lp->inode_id.store (db_out);
  db_out.write_string (lp->name);
}

void
BDB::store_link (const LinkPtr& lp)
{
  Lock lock (mutex);

  vector<char> key;
  vector<char> data;

  DataOutBuffer kbuf (key);

  lp->dir_id.store (kbuf);
  kbuf.write_table (BDB_TABLE_LINKS);

  write_link_data (data, lp);

  Dbt lkey (&key[0], key.size());
  Dbt ldata (&data[0], data.size());

  int ret = db->put (NULL, &lkey, &ldata, 0);
  assert (ret == 0);
}

void
BDB::delete_links (const LinkVersionList& links)
{
  Lock lock (mutex);

  if (links.size() == 0) /* nothing to do? */
    return;

  set< vector<char> > del_links;
  vector<char> all_key;

  for (size_t i = 0; i < links.size(); i++)
    {
      vector<char> data;
      vector<char> key;
      DataOutBuffer kbuf (key);

      links[i]->dir_id.store (kbuf);
      kbuf.write_table (BDB_TABLE_LINKS);
      if (i == 0)
        {
          all_key = key;
        }
      else
        {
          assert (all_key == key); // all links should share the same key
        }
      write_link_data (data, links[i]);
      del_links.insert (data);
    }

  Dbt lkey (&all_key[0], all_key.size());
  Dbt ldata;


  /* Acquire a cursor for the database. */
  Dbc *dbc;

  int ret;
  ret = db->cursor (NULL, &dbc, 0);
  assert (ret == 0);

  // iterate over key elements and delete records which are in LinkVersionList
  ret = dbc->get (&lkey, &ldata, DB_SET);
  while (ret == 0)
    {
      vector<char> cursor_data ((char *) ldata.get_data(), (char *) ldata.get_data() + ldata.get_size());

      if (del_links.find (cursor_data) != del_links.end())
        {
          ret = dbc->del (0);
          assert (ret == 0);
        }
      ret = dbc->get (&lkey, &ldata, DB_NEXT_DUP);
    }

  dbc->close();
}

void
BDB::load_links (std::vector<Link*>& links, const ID& id, guint32 version)
{
  Lock lock (mutex);

  vector<char> key;
  DataOutBuffer kbuf (key);

  id.store (kbuf);
  kbuf.write_table (BDB_TABLE_LINKS);

  Dbt lkey (&key[0], key.size());
  Dbt ldata;

  /* Acquire a cursor for the database. */
  Dbc *dbc;

  int ret;
  ret = db->cursor (NULL, &dbc, 0);
  assert (ret == 0);

  // iterate over key elements and delete records which are in LinkVersionList
  ret = dbc->get (&lkey, &ldata, DB_SET);
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

  dbc->close();
}

Db*
BDB::get_db()
{
  return db;
}

BDB*
BDB::the()
{
  assert (bdb);
  return bdb;
}

DataBuffer::DataBuffer (const char *ptr, size_t size) :
  ptr (ptr),
  remaining (size)
{
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

void
BDB::store_inode (const INode *inode)
{
  Lock lock (mutex);

  vector<char> key;
  vector<char> data;

  DataOutBuffer kbuf (key), dbuf (data);

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
  dbuf.write_uint32 (inode->size);
  dbuf.write_uint32 (inode->major);
  dbuf.write_uint32 (inode->minor);
  dbuf.write_uint32 (inode->nlink);
  dbuf.write_uint32 (inode->ctime);
  dbuf.write_uint32 (inode->ctime_ns);
  dbuf.write_uint32 (inode->mtime);
  dbuf.write_uint32 (inode->mtime_ns);

  Dbt ikey (&key[0], key.size());
  Dbt idata (&data[0], data.size());

  int ret = db->put (NULL, &ikey, &idata, 0);
  assert (ret == 0);
}

/**
 * delete INodes records which have
 *  - the right INode ID
 *  - matching vmin OR matching vmax
 */
void
BDB::delete_inodes (const INodeVersionList& inodes)
{
  Lock lock (mutex);

  if (inodes.size() == 0) /* nothing to do? */
    return;

  set<int> vmin_del;
  set<int> vmax_del;
  vector<char> all_key;

  for (size_t i = 0; i < inodes.size(); i++)
    {
      vector<char> data;
      vector<char> key;

      DataOutBuffer kbuf (key);

      inodes[i]->id.store (kbuf);
      kbuf.write_table (BDB_TABLE_INODES);
      if (i == 0)
        {
          all_key = key;
        }
      else
        {
          assert (all_key == key); // all inodes should share the same key
        }
      vmin_del.insert (inodes[i]->vmin);
      vmax_del.insert (inodes[i]->vmax);
    }

  Dbt ikey (&all_key[0], all_key.size());
  Dbt idata;


  /* Acquire a cursor for the database. */
  Dbc *dbc;

  int ret;
  ret = db->cursor (NULL, &dbc, 0);
  assert (ret == 0);

  // iterate over key elements and delete records which are in INodeVersionList
  ret = dbc->get (&ikey, &idata, DB_SET);
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

  dbc->close();
}

bool
BDB::load_inode (const ID& id, int version, INode *inode)
{
  Lock lock (mutex);

  vector<char> key;
  DataOutBuffer kbuf (key);

  id.store (kbuf);
  kbuf.write_table (BDB_TABLE_INODES);

  Dbt ikey (&key[0], key.size());
  Dbt idata;

  /* Acquire a cursor for the database. */
  Dbc *dbc;

  int ret;
  ret = db->cursor (NULL, &dbc, 0);
  assert (ret == 0);

  ret = dbc->get (&ikey, &idata, DB_SET);
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
          inode->size = dbuffer.read_uint32();
          inode->major = dbuffer.read_uint32();
          inode->minor = dbuffer.read_uint32();
          inode->nlink = dbuffer.read_uint32();
          inode->ctime = dbuffer.read_uint32();
          inode->ctime_ns = dbuffer.read_uint32();
          inode->mtime = dbuffer.read_uint32();
          inode->mtime_ns = dbuffer.read_uint32();
          return true;
        }
      ret = dbc->get (&ikey, &idata, DB_NEXT_DUP);
    }
  return false;
}

bool
BDB::try_store_id2ino (const ID& id, int ino)
{
  Lock lock (mutex);

  vector<char> key;
  vector<char> data;

  DataOutBuffer kbuf (key);
  DataOutBuffer dbuf (data);

  // lookup ino to check whether it is already used:
  kbuf.write_uint32 (ino);
  kbuf.write_table (BDB_TABLE_LOCAL_INO2ID);

  Dbt rev_ikey (&key[0], key.size());
  Dbt rev_lookup;

  int ret = db->get (NULL, &rev_ikey, &rev_lookup, 0);
  if (ret == 0)
    return false;

  // add ino->id entry
  id.store (dbuf);
  Dbt rev_idata (&data[0], data.size());

  ret = db->put (NULL, &rev_ikey, &rev_idata, 0);
  assert (ret == 0);

  key.clear();
  data.clear();

  // add id->ino entry
  id.store (kbuf);
  kbuf.write_table (BDB_TABLE_LOCAL_ID2INO);

  dbuf.write_uint32 (ino);

  Dbt ikey (&key[0], key.size());
  Dbt idata (&data[0], data.size());

  ret = db->put (NULL, &ikey, &idata, 0);
  assert (ret == 0);

  return true;
}

bool
BDB::load_ino (const ID& id, ino_t& ino)
{
  Lock lock (mutex);

  vector<char> key;
  DataOutBuffer kbuf (key);

  id.store (kbuf);
  kbuf.write_table (BDB_TABLE_LOCAL_ID2INO);

  Dbt ikey (&key[0], key.size());
  Dbt idata;

  if (db->get (NULL, &ikey, &idata, 0) != 0)
    return false;

  DataBuffer dbuffer ((char *) idata.get_data(), idata.get_size());

  ino = dbuffer.read_uint32();
  return true;
}

}