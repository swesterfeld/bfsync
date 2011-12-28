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

namespace BFSync
{

Db *db = NULL;

bool
bdb_open (const string& path)
{
  assert (!db);

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

      return true;
    }
  catch (...)
    {
      return false;
    }
}

}
