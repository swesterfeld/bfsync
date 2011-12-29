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

using std::string;
using std::vector;

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
      db = new Db (NULL, 0);
      db->set_cachesize (1, 0, 0);  // 1 GB cache size
      db->set_flags (DB_DUP);       // allow duplicate keys

      // Open the database
      u_int32_t oFlags = DB_CREATE;   // Open flags;

      db->open (NULL,                // Transaction pointer
                path.c_str(),          // Database file name
                NULL,                // Optional logical database name
                DB_BTREE,            // Database access method
                oFlags,              // Open flags
                0);                  // File mode (using defaults)

      bdb = new BDB();
      return true;
    }
  catch (...)
    {
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

void
write_string (vector<char>& out, const string& s)
{
  out.insert (out.end(), s.begin(), s.end());
  out.push_back (0);
}

void
write_guint32 (vector<char>& out, guint32 i)
{
  char *s = reinterpret_cast <char *> (&i);
  assert (sizeof (guint32) == 4);

  out.insert (out.end(), s, s + 4);
}

void
BDB::store_link (const LinkPtr& lp)
{
  vector<char> key;
  vector<char> data;

  write_string (key, lp->dir_id.str());

  write_guint32 (data, lp->vmin);
  write_guint32 (data, lp->vmax);
  write_string (data, lp->inode_id.str());
  write_string (data, lp->name);

  Dbt lkey (&key[0], key.size());
  Dbt ldata (&data[0], data.size());

  int ret = db->put (NULL, &lkey, &ldata, 0);
  assert (ret == 0);
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

}
