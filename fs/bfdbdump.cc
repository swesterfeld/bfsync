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

#include "bfbdb.hh"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

using namespace BFSync;

using std::string;

int
main (int argc, char **argv)
{
  assert (argc == 2);

  if (!bdb_open (argv[1]))
    {
      printf ("error opening db %s\n", argv[1]);
      exit (1);
    }

  Db *db = BDB::the()->get_db();
  Dbc *dbcp;

  /* Acquire a cursor for the database. */
  int ret;
  if ((ret = db->cursor (NULL, &dbcp, 0)) != 0)
    {
      db->err (ret, "DB->cursor");
      return 1;
    }

  Dbt key;
  Dbt data;

  ret = dbcp->get (&key, &data, DB_FIRST);
  while (ret == 0)
    {
      char xk[key.get_size() + 1];
      memcpy (xk, (char *)key.get_data(), key.get_size());
      xk[key.get_size()] = 0;

      DataBuffer dbuffer ((char *) data.get_data(), data.get_size());
      int vmin = dbuffer.read_uint32();
      int vmax = dbuffer.read_uint32();
      string inode_id = dbuffer.read_string();
      string name = dbuffer.read_string();

      printf ("%s=%d|%d|%s|%s\n", xk, vmin, vmax, inode_id.c_str(), name.c_str());

      ret = dbcp->get (&key, &data, DB_NEXT_DUP);
      if (ret != 0)
        ret = dbcp->get (&key, &data, DB_NEXT);
    }

  if (!bdb_close())
    {
      printf ("error closing db %s\n", argv[1]);
      exit (1);
    }
}
